/*
 * objrw_cuda.cu
 * --------------
 * Optional CUDA (GPU) fast paths for the objrw geometry helpers.
 *
 * This translation unit is only compiled when CMake is configured with
 *   -DOBJRW_ENABLE_CUDA=ON
 * and a CUDA compiler (nvcc) is available. On this machine it is OFF by
 * default and there is no nvcc, so it is neither compiled nor tested here.
 * It is provided so that, where a CUDA toolchain and device exist, the
 * compute-bound geometry kernels can run on the GPU for larger meshes.
 *
 * The kernels mirror the semantics of the CPU implementations in objrw.cpp:
 *   - bounding_box : per-axis min/max over vertex positions
 *   - compute_normals : per-vertex area-weighted normal accumulation + normalize
 *   - stats         : total surface area + signed volume (divergence theorem)
 *
 * Every host entry point returns a objrw_status and does a CPU-safe fallback
 * on the *caller* side (see objrw.cpp) whenever a non-OBJRW_OK code is
 * returned, so the library keeps working on machines without a usable GPU.
 */
#include "objrw_internal.h"

#include <cuda_runtime.h>
#include <cstdint>

namespace objrw {

/* ------------------------------------------------------------------ */
/*  Runtime helpers                                                    */
/* ------------------------------------------------------------------ */

namespace {

constexpr int kBlock = 256;

int grid_size(uint64_t n) {
    int g = (int)((n + kBlock - 1) / kBlock);
    return g < 1 ? 1 : (g > 65535 ? 65535 : g);
}

bool check(cudaError_t e) {
    if (e != cudaSuccess) {
        /* Keep a terse reason; the public layer exposes a generic error. */
        (void)e;
        return false;
    }
    return true;
}

/* CUDA's built-in atomicMin/atomicMax do not have float overloads. Implement
 * them via atomicCAS on the bit pattern, matching the standard idiom (see
 * NVIDIA forums / CUDA C++ Programming Guide "Atomic Functions" appendix). */
__device__ float atomicMinFloat(float *addr, float value) {
    int *addr_as_i = (int *)addr;
    int old = *addr_as_i, assumed;
    while (value < __int_as_float(old)) {
        assumed = old;
        old = atomicCAS(addr_as_i, assumed, __float_as_int(value));
        if (assumed == old) break;
    }
    return __int_as_float(old);
}

__device__ float atomicMaxFloat(float *addr, float value) {
    int *addr_as_i = (int *)addr;
    int old = *addr_as_i, assumed;
    while (value > __int_as_float(old)) {
        assumed = old;
        old = atomicCAS(addr_as_i, assumed, __float_as_int(value));
        if (assumed == old) break;
    }
    return __int_as_float(old);
}

} // namespace

bool cuda_available() {
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess) return false;
    return device_count > 0;
}

/* ------------------------------------------------------------------ */
/*  Bounding box                                                       */
/* ------------------------------------------------------------------ */

__global__ void bbox_kernel(const float *positions, uint64_t n,
                            float *d_min, float *d_max) {
    uint64_t i = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float v0 = positions[3 * i + 0];
    float v1 = positions[3 * i + 1];
    float v2 = positions[3 * i + 2];
    atomicMinFloat(&d_min[0], v0); atomicMaxFloat(&d_max[0], v0);
    atomicMinFloat(&d_min[1], v1); atomicMaxFloat(&d_max[1], v1);
    atomicMinFloat(&d_min[2], v2); atomicMaxFloat(&d_max[2], v2);
}

objrw_status cuda_bounding_box(const float *positions, uint64_t n,
                                float min_out[3], float max_out[3]) {
    if (!positions || n == 0) return OBJRW_ERR_NULL_POINTER;

    float *d_pos = nullptr;
    float *d_min = nullptr;
    float *d_max = nullptr;
    if (!check(cudaMalloc(&d_pos, n * 3 * sizeof(float)))) return OBJRW_ERR_INTERNAL;
    if (!check(cudaMalloc(&d_min, 3 * sizeof(float))))    { cudaFree(d_pos); return OBJRW_ERR_INTERNAL; }
    if (!check(cudaMalloc(&d_max, 3 * sizeof(float))))    { cudaFree(d_pos); cudaFree(d_min); return OBJRW_ERR_INTERNAL; }

    if (!check(cudaMemcpy(d_pos, positions, n * 3 * sizeof(float), cudaMemcpyHostToDevice))) {
        cudaFree(d_pos); cudaFree(d_min); cudaFree(d_max);
        return OBJRW_ERR_INTERNAL;
    }

    /* Seed min/max with the first vertex, then atomically reduce the rest. */
    if (!check(cudaMemcpy(d_min, positions, 3 * sizeof(float), cudaMemcpyHostToDevice))) {
        cudaFree(d_pos); cudaFree(d_min); cudaFree(d_max);
        return OBJRW_ERR_INTERNAL;
    }
    if (!check(cudaMemcpy(d_max, positions, 3 * sizeof(float), cudaMemcpyHostToDevice))) {
        cudaFree(d_pos); cudaFree(d_min); cudaFree(d_max);
        return OBJRW_ERR_INTERNAL;
    }

    bbox_kernel<<<grid_size(n), kBlock>>>(d_pos, n, d_min, d_max);

    float h_min[3], h_max[3];
    bool ok = check(cudaMemcpy(h_min, d_min, 3 * sizeof(float), cudaMemcpyDeviceToHost)) &&
              check(cudaMemcpy(h_max, d_max, 3 * sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_pos); cudaFree(d_min); cudaFree(d_max);

    if (!ok) return OBJRW_ERR_INTERNAL;
    for (int k = 0; k < 3; ++k) { min_out[k] = h_min[k]; max_out[k] = h_max[k]; }
    return OBJRW_OK;
}

/* ------------------------------------------------------------------ */
/*  Vertex normals                                                     */
/* ------------------------------------------------------------------ */

__global__ void normals_kernel(const float *positions, const int32_t *tri_pos,
                               uint64_t ntri, float *acc) {
    uint64_t t = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= ntri) return;

    int32_t i0 = tri_pos[3 * t + 0];
    int32_t i1 = tri_pos[3 * t + 1];
    int32_t i2 = tri_pos[3 * t + 2];
    if (i0 < 0 || i1 < 0 || i2 < 0) return;

    float ax = positions[3 * i0 + 0], ay = positions[3 * i0 + 1], az = positions[3 * i0 + 2];
    float bx = positions[3 * i1 + 0], by = positions[3 * i1 + 1], bz = positions[3 * i1 + 2];
    float cx = positions[3 * i2 + 0], cy = positions[3 * i2 + 1], cz = positions[3 * i2 + 2];

    float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    float e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;

    /* Unweighted accumulation (matches the CPU reference implementation). */
    atomicAdd(&acc[3 * i0 + 0], nx); atomicAdd(&acc[3 * i0 + 1], ny); atomicAdd(&acc[3 * i0 + 2], nz);
    atomicAdd(&acc[3 * i1 + 0], nx); atomicAdd(&acc[3 * i1 + 1], ny); atomicAdd(&acc[3 * i1 + 2], nz);
    atomicAdd(&acc[3 * i2 + 0], nx); atomicAdd(&acc[3 * i2 + 1], ny); atomicAdd(&acc[3 * i2 + 2], nz);
}

__global__ void normalize_kernel(float *acc, uint64_t nv) {
    uint64_t i = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nv) return;
    float x = acc[3 * i + 0], y = acc[3 * i + 1], z = acc[3 * i + 2];
    float len = sqrtf(x * x + y * y + z * z);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        acc[3 * i + 0] = x * inv;
        acc[3 * i + 1] = y * inv;
        acc[3 * i + 2] = z * inv;
    } else {
        acc[3 * i + 0] = 0.0f;
        acc[3 * i + 1] = 0.0f;
        acc[3 * i + 2] = 0.0f;
    }
}

objrw_status cuda_compute_normals(const float *positions, const int32_t *tri_pos,
                                   uint64_t nv, uint64_t ntri, float *normals_out) {
    if (!positions || !tri_pos || !normals_out) return OBJRW_ERR_NULL_POINTER;
    if (ntri == 0 || nv == 0) return OBJRW_ERR_EMPTY;

    float *d_pos = nullptr;
    float *d_tri = nullptr;
    float *d_acc = nullptr;
    if (!check(cudaMalloc(&d_pos, nv * 3 * sizeof(float)))) return OBJRW_ERR_INTERNAL;
    if (!check(cudaMalloc(&d_tri, ntri * 3 * sizeof(float)))) { cudaFree(d_pos); return OBJRW_ERR_INTERNAL; }
    if (!check(cudaMalloc(&d_acc, nv * 3 * sizeof(float)))) { cudaFree(d_pos); cudaFree(d_tri); return OBJRW_ERR_INTERNAL; }

    bool ok = true;
    ok = ok && check(cudaMemcpy(d_pos, positions, nv * 3 * sizeof(float), cudaMemcpyHostToDevice));
    ok = ok && check(cudaMemcpy(d_tri, tri_pos, ntri * 3 * sizeof(int32_t), cudaMemcpyHostToDevice));
    ok = ok && check(cudaMemset(d_acc, 0, nv * 3 * sizeof(float)));

    if (ok) {
        normals_kernel<<<grid_size(ntri), kBlock>>>(d_pos, (const int32_t *)d_tri, ntri, d_acc);
        normalize_kernel<<<grid_size(nv), kBlock>>>(d_acc, nv);
        ok = check(cudaMemcpy(normals_out, d_acc, nv * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    }

    cudaFree(d_pos); cudaFree(d_tri); cudaFree(d_acc);
    return ok ? OBJRW_OK : OBJRW_ERR_INTERNAL;
}

/* ------------------------------------------------------------------ */
/*  Area + signed volume                                               */
/* ------------------------------------------------------------------ */

__global__ void stats_kernel(const float *positions, const int32_t *tri_pos,
                             uint64_t ntri, double *d_area, double *d_volume) {
    uint64_t t = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= ntri) return;

    int32_t i0 = tri_pos[3 * t + 0];
    int32_t i1 = tri_pos[3 * t + 1];
    int32_t i2 = tri_pos[3 * t + 2];
    if (i0 < 0 || i1 < 0 || i2 < 0) return;

    double ax = (double)positions[3 * i0 + 0], ay = (double)positions[3 * i0 + 1], az = (double)positions[3 * i0 + 2];
    double bx = (double)positions[3 * i1 + 0], by = (double)positions[3 * i1 + 1], bz = (double)positions[3 * i1 + 2];
    double cx = (double)positions[3 * i2 + 0], cy = (double)positions[3 * i2 + 1], cz = (double)positions[3 * i2 + 2];

    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;

    atomicAdd(d_area, 0.5 * sqrt(nx * nx + ny * ny + nz * nz));
    atomicAdd(d_volume, (ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)) / 6.0);
}

objrw_status cuda_stats(const float *positions, const int32_t *tri_pos,
                         uint64_t nv, uint64_t ntri, double *area_out, double *volume_out) {
    if (!positions || !tri_pos) return OBJRW_ERR_NULL_POINTER;
    if (nv == 0 || ntri == 0) return OBJRW_ERR_EMPTY;

    float *d_pos = nullptr;
    int32_t *d_tri = nullptr;
    double *d_out = nullptr; /* two doubles: area, volume */
    if (!check(cudaMalloc(&d_pos, nv * 3 * sizeof(float)))) return OBJRW_ERR_INTERNAL;
    if (!check(cudaMalloc(&d_tri, ntri * 3 * sizeof(int32_t)))) { cudaFree(d_pos); return OBJRW_ERR_INTERNAL; }
    if (!check(cudaMalloc(&d_out, 2 * sizeof(double)))) { cudaFree(d_pos); cudaFree(d_tri); return OBJRW_ERR_INTERNAL; }

    bool ok = true;
    ok = ok && check(cudaMemcpy(d_pos, positions, nv * 3 * sizeof(float), cudaMemcpyHostToDevice));
    ok = ok && check(cudaMemcpy(d_tri, tri_pos, ntri * 3 * sizeof(int32_t), cudaMemcpyHostToDevice));
    ok = ok && check(cudaMemset(d_out, 0, 2 * sizeof(double)));

    if (ok) {
        stats_kernel<<<grid_size(ntri), kBlock>>>(d_pos, d_tri, ntri, d_out, d_out + 1);
        double h[2];
        ok = check(cudaMemcpy(h, d_out, 2 * sizeof(double), cudaMemcpyDeviceToHost));
        if (ok) { if (area_out) *area_out = h[0]; if (volume_out) *volume_out = h[1]; }
    }

    cudaFree(d_pos); cudaFree(d_tri); cudaFree(d_out);
    return ok ? OBJRW_OK : OBJRW_ERR_INTERNAL;
}

} // namespace objrw
