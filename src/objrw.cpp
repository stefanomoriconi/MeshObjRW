/*
 * objrw.cpp
 * ----------
 * Public C-ABI implementation: mesh lifecycle, append helpers, file
 * dispatch (auto-detect ASCII vs binary), and diagnostics.
 */
#include "../include/objrw.h"
#include "objrw_internal.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* ================================================================== */
/*  Thread-local last error                                            */
/* ================================================================== */

namespace {
thread_local std::string g_last_error;

void set_last_error(const char *msg) { g_last_error = msg ? msg : ""; }
} // namespace

const char *objrw_last_error(void) { return g_last_error.c_str(); }

const char *objrw_version(void) {
    static const char *s = "1.0.0";
    return s;
}

const char *objrw_build_info(void) {
    static std::string s;
    if (s.empty()) {
        char ver[32];
        std::snprintf(ver, sizeof(ver), "%d.%d.%d",
                      OBJRW_VERSION_MAJOR, OBJRW_VERSION_MINOR, OBJRW_VERSION_PATCH);
        s  = std::string("objrw ") + ver;
#if defined(_MSC_VER)
        s += " | compiler: MSVC ";
        s += std::to_string(_MSC_VER);
#else
        s += " | compiler: " __VERSION__;
#endif
#if defined(_OPENMP)
        s += " | OpenMP: yes";
#else
        s += " | OpenMP: no (sequential)";
#endif
#if defined(OBJRW_ENABLE_CUDA)
        s += " | CUDA: compiled in";
#else
        s += " | CUDA: not compiled in";
#endif
#if defined(_WIN32)
        s += " | platform: windows";
#elif defined(__APPLE__)
        s += " | platform: macos";
#elif defined(__linux__)
        s += " | platform: linux";
#else
        s += " | platform: unknown";
#endif
    }
    return s.c_str();
}

/* ================================================================== */
/*  Mesh lifecycle                                                     */
/* ================================================================== */

objrw_mesh_t *objrw_mesh_create(void) {
    objrw_mesh_t *m = (objrw_mesh_t *)calloc(1, sizeof(*m));
    if (!m) { set_last_error("out of memory (mesh_create)"); return nullptr; }
    return m;
}

void objrw_mesh_free(objrw_mesh_t *m) {
    if (!m) return;
    free(m->positions);
    free(m->texcoords);
    free(m->normals);
    free(m->tri_pos);
    free(m->tri_tex);
    free(m->tri_nor);
    free(m->object_name);
    free(m);
}

/*
 * Grow one attribute array.  `arr` is the pointer field, `elem` the element
 * size in bytes, `count` the logical count of elements, `need` the required
 * element count.  Preserves data.
 */
template <typename T, int Stride>
objrw_status grow_array(T **arr, uint64_t count, uint64_t need) {
    if (need <= count) return OBJRW_OK;
    void *np = realloc(*arr, need * (size_t)Stride * sizeof(T));
    if (!np) { set_last_error("out of memory (grow_array)"); return OBJRW_ERR_MEMORY; }
    *arr = (T *)np;
    return OBJRW_OK;
}

objrw_status objrw_mesh_reserve(objrw_mesh_t *mesh,
                                  uint64_t num_vertices,
                                  uint64_t num_texcoords,
                                  uint64_t num_normals,
                                  uint64_t num_triangles) {
    if (!mesh) { set_last_error("objrw_mesh_reserve: null mesh"); return OBJRW_ERR_NULL_POINTER; }

    objrw_status st;
    if ((st = grow_array<float, 3>(&mesh->positions, mesh->num_vertices, num_vertices)) != OBJRW_OK) return st;
    if ((st = grow_array<float, 2>(&mesh->texcoords,  mesh->num_texcoords, num_texcoords)) != OBJRW_OK) return st;
    if ((st = grow_array<float, 3>(&mesh->normals,   mesh->num_normals,   num_normals))   != OBJRW_OK) return st;

    if ((st = grow_array<int32_t, 1>(&mesh->tri_pos, mesh->num_triangles * 3, num_triangles * 3)) != OBJRW_OK) return st;
    if ((st = grow_array<int32_t, 1>(&mesh->tri_tex, mesh->num_triangles * 3, num_triangles * 3)) != OBJRW_OK) return st;
    if ((st = grow_array<int32_t, 1>(&mesh->tri_nor, mesh->num_triangles * 3, num_triangles * 3)) != OBJRW_OK) return st;
    return OBJRW_OK;
}

/* ================================================================== */
/*  Append helpers                                                     */
/* ================================================================== */

objrw_status objrw_mesh_push_vertex(objrw_mesh_t *m, float x, float y, float z) {
    if (!m) { set_last_error("push_vertex: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    objrw_status st = objrw_mesh_reserve(m, m->num_vertices + 1, m->num_texcoords, m->num_normals, m->num_triangles);
    if (st != OBJRW_OK) return st;
    float *p = m->positions + 3 * m->num_vertices;
    p[0] = x; p[1] = y; p[2] = z;
    m->num_vertices++;
    return OBJRW_OK;
}

objrw_status objrw_mesh_push_texcoord(objrw_mesh_t *m, float u, float v) {
    if (!m) { set_last_error("push_texcoord: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    objrw_status st = objrw_mesh_reserve(m, m->num_vertices, m->num_texcoords + 1, m->num_normals, m->num_triangles);
    if (st != OBJRW_OK) return st;
    float *p = m->texcoords + 2 * m->num_texcoords;
    p[0] = u; p[1] = v;
    m->num_texcoords++;
    return OBJRW_OK;
}

objrw_status objrw_mesh_push_normal(objrw_mesh_t *m, float x, float y, float z) {
    if (!m) { set_last_error("push_normal: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    objrw_status st = objrw_mesh_reserve(m, m->num_vertices, m->num_texcoords, m->num_normals + 1, m->num_triangles);
    if (st != OBJRW_OK) return st;
    float *p = m->normals + 3 * m->num_normals;
    p[0] = x; p[1] = y; p[2] = z;
    m->num_normals++;
    return OBJRW_OK;
}

objrw_status objrw_mesh_push_triangle(objrw_mesh_t *m,
                                        int32_t p0, int32_t p1, int32_t p2,
                                        int32_t t0, int32_t t1, int32_t t2,
                                        int32_t n0, int32_t n1, int32_t n2) {
    if (!m) { set_last_error("push_triangle: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    uint64_t nt = m->num_triangles + 1;
    objrw_status st = objrw_mesh_reserve(m, m->num_vertices, m->num_texcoords, m->num_normals, nt);
    if (st != OBJRW_OK) return st;
    size_t base = 3 * (size_t)m->num_triangles;
    m->tri_pos[base + 0] = p0; m->tri_pos[base + 1] = p1; m->tri_pos[base + 2] = p2;
    m->tri_tex[base + 0] = t0; m->tri_tex[base + 1] = t1; m->tri_tex[base + 2] = t2;
    m->tri_nor[base + 0] = n0; m->tri_nor[base + 1] = n1; m->tri_nor[base + 2] = n2;
    m->num_triangles++;
    return OBJRW_OK;
}

objrw_status objrw_mesh_set_name(objrw_mesh_t *m, const char *name) {
    if (!m) { set_last_error("set_name: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    free(m->object_name);
    m->object_name = nullptr;
    m->has_object_name = 0;
    if (!name) return OBJRW_OK;
    size_t len = std::strlen(name);
    char *copy = (char *)malloc(len + 1);
    if (!copy) { set_last_error("set_name: out of memory"); return OBJRW_ERR_MEMORY; }
    std::memcpy(copy, name, len);
    copy[len] = '\0';
    m->object_name = copy;
    m->has_object_name = 1;
    return OBJRW_OK;
}

/* ================================================================== */
/*  File dispatch                                                      */
/* ================================================================== */

namespace {
bool starts_with_objrw(const uint8_t *buf, size_t len) {
    if (len < 8) return false;
    return std::memcmp(buf, "OBJRW", 5) == 0;
}
} // namespace

objrw_status objrw_read(const char *path, objrw_mesh_t *mesh) {
    if (!path || !mesh) { set_last_error("objrw_read: null argument"); return OBJRW_ERR_NULL_POINTER; }

    std::vector<uint8_t> buf;
    if (!objrw::read_file(path, buf)) {
        set_last_error("objrw_read: cannot open file");
        return OBJRW_ERR_IO_OPEN;
    }
    if (buf.empty()) { set_last_error("objrw_read: empty file"); return OBJRW_ERR_EMPTY; }

    if (starts_with_objrw(buf.data(), buf.size())) {
        return objrw::read_binary(buf.data(), buf.size(), mesh);
    }
    return objrw::parse_ascii(buf.data(), buf.size(), mesh);
}

objrw_status objrw_read_ascii(const char *path, objrw_mesh_t *mesh) {
    if (!path || !mesh) { set_last_error("objrw_read_ascii: null argument"); return OBJRW_ERR_NULL_POINTER; }
    std::vector<uint8_t> buf;
    if (!objrw::read_file(path, buf)) { set_last_error("objrw_read_ascii: cannot open file"); return OBJRW_ERR_IO_OPEN; }
    if (buf.empty()) { set_last_error("objrw_read_ascii: empty file"); return OBJRW_ERR_EMPTY; }
    return objrw::parse_ascii(buf.data(), buf.size(), mesh);
}

objrw_status objrw_read_binary(const char *path, objrw_mesh_t *mesh) {
    if (!path || !mesh) { set_last_error("objrw_read_binary: null argument"); return OBJRW_ERR_NULL_POINTER; }
    std::vector<uint8_t> buf;
    if (!objrw::read_file(path, buf)) { set_last_error("objrw_read_binary: cannot open file"); return OBJRW_ERR_IO_OPEN; }
    if (buf.empty()) { set_last_error("objrw_read_binary: empty file"); return OBJRW_ERR_EMPTY; }
    return objrw::read_binary(buf.data(), buf.size(), mesh);
}

objrw_status objrw_write_ascii(const char *path, const objrw_mesh_t *mesh) {
    if (!path || !mesh) { set_last_error("objrw_write_ascii: null argument"); return OBJRW_ERR_NULL_POINTER; }
    std::string out;
    objrw_status st = objrw::write_ascii(mesh, out);
    if (st != OBJRW_OK) return st;
    if (!objrw::write_file(path, (const uint8_t *)out.data(), out.size())) {
        set_last_error("objrw_write_ascii: cannot write file");
        return OBJRW_ERR_IO_WRITE;
    }
    return OBJRW_OK;
}

objrw_status objrw_write_binary(const char *path, const objrw_mesh_t *mesh) {
    if (!path || !mesh) { set_last_error("objrw_write_binary: null argument"); return OBJRW_ERR_NULL_POINTER; }
    std::vector<uint8_t> out;
    objrw_status st = objrw::write_binary(mesh, out);
    if (st != OBJRW_OK) return st;
    if (!objrw::write_file(path, out.data(), out.size())) {
        set_last_error("objrw_write_binary: cannot write file");
        return OBJRW_ERR_IO_WRITE;
    }
    return OBJRW_OK;
}

objrw_status objrw_write(const char *path, const objrw_mesh_t *mesh) {
    return objrw_write_ascii(path, mesh);
}

/* ================================================================== */
/*  Geometry helpers                                                   */
/* ================================================================== */

objrw_status objrw_mesh_bounding_box(const objrw_mesh_t *mesh,
                                       float min_out[3], float max_out[3]) {
    if (!mesh) { set_last_error("bounding_box: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    if (mesh->num_vertices == 0) { set_last_error("bounding_box: empty mesh"); return OBJRW_ERR_EMPTY; }
#ifdef OBJRW_ENABLE_CUDA
    if (objrw::cuda_available()) {
        float mn[3], mx[3];
        if (objrw::cuda_bounding_box(mesh->positions, mesh->num_vertices, mn, mx) == OBJRW_OK) {
            if (min_out) { min_out[0]=mn[0]; min_out[1]=mn[1]; min_out[2]=mn[2]; }
            if (max_out) { max_out[0]=mx[0]; max_out[1]=mx[1]; max_out[2]=mx[2]; }
            return OBJRW_OK;
        }
        /* fall through to the CPU path on any GPU error */
    }
#endif
    float mn[3] = { mesh->positions[0], mesh->positions[1], mesh->positions[2] };
    float mx[3] = { mn[0], mn[1], mn[2] };
    for (uint64_t i = 1; i < mesh->num_vertices; ++i) {
        for (int k = 0; k < 3; ++k) {
            float v = mesh->positions[3 * i + k];
            if (v < mn[k]) mn[k] = v;
            if (v > mx[k]) mx[k] = v;
        }
    }
    if (min_out) { min_out[0]=mn[0]; min_out[1]=mn[1]; min_out[2]=mn[2]; }
    if (max_out) { max_out[0]=mx[0]; max_out[1]=mx[1]; max_out[2]=mx[2]; }
    return OBJRW_OK;
}

objrw_status objrw_mesh_compute_normals(objrw_mesh_t *mesh) {
    if (!mesh) { set_last_error("compute_normals: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    if (mesh->num_triangles == 0) { set_last_error("compute_normals: no triangles"); return OBJRW_ERR_EMPTY; }
    uint64_t nv = mesh->num_vertices;
    float *buf = (float *)std::malloc(nv * 3 * sizeof(float));
    if (!buf) { set_last_error("compute_normals: out of memory"); return OBJRW_ERR_MEMORY; }
#ifdef OBJRW_ENABLE_CUDA
    if (objrw::cuda_available() &&
        objrw::cuda_compute_normals(mesh->positions, mesh->tri_pos, nv,
                                  mesh->num_triangles, buf) == OBJRW_OK) {
        std::free(mesh->normals);
        mesh->normals = buf;
        mesh->num_normals = nv;
        return OBJRW_OK;
    }
    /* fall through to the CPU path on any GPU error */
#endif
    objrw::compute_normals(mesh, buf);
    std::free(mesh->normals);
    mesh->normals = buf;
    mesh->num_normals = nv;
    return OBJRW_OK;
}

objrw_status objrw_mesh_stats(const objrw_mesh_t *mesh,
                                double *area_out, double *volume_out) {
    if (!mesh) { set_last_error("stats: null mesh"); return OBJRW_ERR_NULL_POINTER; }
    if (mesh->num_vertices == 0 || mesh->num_triangles == 0) {
        set_last_error("stats: empty mesh"); return OBJRW_ERR_EMPTY;
    }
    const float *P = mesh->positions;
    double area = 0.0, volume = 0.0;
#ifdef OBJRW_ENABLE_CUDA
    if (objrw::cuda_available()) {
        double a = 0.0, v = 0.0;
        if (objrw::cuda_stats(mesh->positions, mesh->tri_pos, mesh->num_vertices,
                            mesh->num_triangles, &a, &v) == OBJRW_OK) {
            if (area_out) *area_out = a;
            if (volume_out) *volume_out = v;
            return OBJRW_OK;
        }
        /* fall through to the CPU path on any GPU error */
    }
#endif
    for (uint64_t t = 0; t < mesh->num_triangles; ++t) {
        int32_t i0 = mesh->tri_pos[3*t+0], i1 = mesh->tri_pos[3*t+1], i2 = mesh->tri_pos[3*t+2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || (uint64_t)i0 >= mesh->num_vertices ||
            (uint64_t)i1 >= mesh->num_vertices || (uint64_t)i2 >= mesh->num_vertices) continue;
        double ax=P[3*(size_t)i0],   ay=P[3*(size_t)i0+1],   az=P[3*(size_t)i0+2];
        double bx=P[3*(size_t)i1],   by=P[3*(size_t)i1+1],   bz=P[3*(size_t)i1+2];
        double cx=P[3*(size_t)i2],   cy=P[3*(size_t)i2+1],   cz=P[3*(size_t)i2+2];
        double e1x=bx-ax, e1y=by-ay, e1z=bz-az;
        double e2x=cx-ax, e2y=cy-ay, e2z=cz-az;
        double nx=e1y*e2z-e1z*e2y, ny=e1z*e2x-e1x*e2z, nz=e1x*e2y-e1y*e2x;
        double cross_len2 = nx*nx + ny*ny + nz*nz;
        area += 0.5 * std::sqrt(cross_len2);
        /* signed tetrahedron volume via triple product (divergence theorem) */
        volume += (ax*(by*cz-bz*cy) - ay*(bx*cz-bz*cx) + az*(bx*cy-by*cx)) / 6.0;
    }
    if (area_out)   *area_out   = area;
    if (volume_out) *volume_out = volume;
    return OBJRW_OK;
}
