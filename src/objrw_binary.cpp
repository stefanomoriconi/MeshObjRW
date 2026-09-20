/*
 * objrw_binary.cpp
 * -----------------
 * Compact binary mesh format "objrw" plus geometry helpers
 * (bounding box, stats, and area-weighted per-vertex normals).
 *
 * objrw layout (all little-endian, fixed-size):
 *   byte[8]  magic        "OBJRW\0\0\0"
 *   u32      format_ver   = 1
 *   u64      num_vertices
 *   u64      num_texcoords
 *   u64      num_normals
 *   u64      num_triangles
 *   u64      name_len     (0 when no name)
 *   byte[]   name         (name_len bytes, only if name_len>0)
 *   float[]  positions    (3 * num_vertices)
 *   float[]  texcoords    (2 * num_texcoords)
 *   float[]  normals      (3 * num_normals)
 *   i32[]    tri_pos      (3 * num_triangles)
 *   i32[]    tri_tex      (3 * num_triangles)
 *   i32[]    tri_nor      (3 * num_triangles)
 *
 * The format is stable and versioned: readers reject format_ver != 1.
 */
#include "objrw_internal.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace objrw {

namespace {

constexpr uint8_t kMagic[8] = { 'O', 'B', 'J', 'R', 'W', 0, 0, 0 };
constexpr uint32_t kFormatVer = 1;

void put_u32(std::vector<uint8_t> &b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xff));
    b.push_back((uint8_t)((v >> 8) & 0xff));
    b.push_back((uint8_t)((v >> 16) & 0xff));
    b.push_back((uint8_t)((v >> 24) & 0xff));
}
void put_u64(std::vector<uint8_t> &b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back((uint8_t)((v >> (8 * i)) & 0xff));
}
void put_f32(std::vector<uint8_t> &b, float v) {
    uint32_t u; std::memcpy(&u, &v, 4);
    put_u32(b, u);
}
void put_i32(std::vector<uint8_t> &b, int32_t v) { put_u32(b, (uint32_t)v); }

/* --- readers over a (buf, &off) cursor --- */
struct Cursor {
    const uint8_t *p;
    size_t off;
    size_t n;
    bool ok;
    Cursor(const uint8_t *buf, size_t len) : p(buf), off(0), n(len), ok(true) {}
    bool need(size_t k) { return ok && off + k <= n; }
    uint32_t u32() {
        if (!need(4)) { ok = false; return 0; }
        uint32_t v = (uint32_t)p[off] | ((uint32_t)p[off+1] << 8) |
                     ((uint32_t)p[off+2] << 16) | ((uint32_t)p[off+3] << 24);
        off += 4; return v;
    }
    uint64_t u64() {
        if (!need(8)) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= (uint64_t)p[off+i] << (8*i);
        off += 8; return v;
    }
    float f32() {
        if (!need(4)) { ok = false; return 0.0f; }
        uint32_t u = (uint32_t)p[off] | ((uint32_t)p[off+1] << 8) |
                     ((uint32_t)p[off+2] << 16) | ((uint32_t)p[off+3] << 24);
        off += 4;
        float f; std::memcpy(&f, &u, 4); return f;
    }
    int32_t i32() { return (int32_t)u32(); }
    void skip(size_t k) { if (off + k > n) ok = false; else off += k; }
};

} /* namespace */

/* ================================================================== */
/*  Binary write                                                        */
/* ================================================================== */

objrw_status write_binary(const objrw_mesh_t *mesh, std::vector<uint8_t> &out) {
    if (!mesh) return OBJRW_ERR_NULL_POINTER;
    if (mesh->num_vertices == 0 || mesh->num_triangles == 0) return OBJRW_ERR_EMPTY;

    out.clear();
    for (int i = 0; i < 8; ++i) out.push_back(kMagic[i]);
    put_u32(out, kFormatVer);
    put_u64(out, mesh->num_vertices);
    put_u64(out, mesh->num_texcoords);
    put_u64(out, mesh->num_normals);
    put_u64(out, mesh->num_triangles);

    size_t name_len = 0;
    const char *name = nullptr;
    if (mesh->has_object_name && mesh->object_name) {
        name = mesh->object_name;
        name_len = std::strlen(name);
    }
    put_u64(out, name_len);
    if (name_len) { out.insert(out.end(), name, name + name_len); }

    auto append_f32s = [&](const float *arr, uint64_t count) {
        if (count == 0) return;
        if (!arr) { /* reserved but not provided: emit zeros */
            out.resize(out.size() + 4 * count, 0); return;
        }
        const uint8_t *b = (const uint8_t *)arr;
        out.insert(out.end(), b, b + 4 * count);
    };
    auto append_i32s = [&](const int32_t *arr, uint64_t count) {
        if (count == 0) return;
        if (!arr) { out.resize(out.size() + 4 * count, 0); return; }
        const uint8_t *b = (const uint8_t *)arr;
        out.insert(out.end(), b, b + 4 * count);
    };

    append_f32s(mesh->positions,  3 * mesh->num_vertices);
    append_f32s(mesh->texcoords,  2 * mesh->num_texcoords);
    append_f32s(mesh->normals,    3 * mesh->num_normals);
    append_i32s(mesh->tri_pos,    3 * mesh->num_triangles);
    append_i32s(mesh->tri_tex,    3 * mesh->num_triangles);
    append_i32s(mesh->tri_nor,    3 * mesh->num_triangles);
    return OBJRW_OK;
}

/* ================================================================== */
/*  Binary read                                                         */
/* ================================================================== */

static void *alloc_or_null(size_t bytes) { return bytes ? std::malloc(bytes) : nullptr; }

objrw_status read_binary(const uint8_t *buf, size_t len, objrw_mesh_t *mesh) {
    if (!mesh) return OBJRW_ERR_NULL_POINTER;
    if (len < 8 || std::memcmp(buf, kMagic, 8) != 0) return OBJRW_ERR_UNSUPPORTED;

    Cursor c(buf + 8, len - 8);   /* skip the 8-byte magic */
    uint32_t ver = c.u32();
    if (!c.ok || ver != kFormatVer) return OBJRW_ERR_UNSUPPORTED;

    uint64_t nv   = c.u64();
    uint64_t nvt  = c.u64();
    uint64_t nvn  = c.u64();
    uint64_t nt   = c.u64();
    uint64_t name_len = c.u64();
    if (!c.ok) return OBJRW_ERR_UNSUPPORTED;

    if (nv == 0 || nt == 0) return OBJRW_ERR_EMPTY;

    size_t name_bytes = (size_t)name_len;
    if (!c.need(name_bytes)) return OBJRW_ERR_UNSUPPORTED;
    const uint8_t *name_src = c.p + c.off;   /* name lives before the data arrays */
    c.skip(name_bytes);

    /* Guard against absurd sizes. */
    const uint64_t MAX_ELEMS = 1ULL << 32;
    if (nv > MAX_ELEMS || nvt > MAX_ELEMS || nvn > MAX_ELEMS || nt > MAX_ELEMS)
        return OBJRW_ERR_UNSUPPORTED;

    void *pos = alloc_or_null(3 * nv * sizeof(float));
    void *tex = alloc_or_null(2 * nvt * sizeof(float));
    void *nor = alloc_or_null(3 * nvn * sizeof(float));
    void *tp  = alloc_or_null(3 * nt * sizeof(int32_t));
    void *tt  = alloc_or_null(3 * nt * sizeof(int32_t));
    void *tn  = alloc_or_null(3 * nt * sizeof(int32_t));
    if (nv && !pos) return OBJRW_ERR_MEMORY;
    if (nvt && !tex) { std::free(pos); return OBJRW_ERR_MEMORY; }
    if (nvn && !nor) { std::free(pos); std::free(tex); return OBJRW_ERR_MEMORY; }
    if (!tp || !tt || !tn) {
        std::free(pos); std::free(tex); std::free(nor);
        std::free(tp); std::free(tt); std::free(tn);
        return OBJRW_ERR_MEMORY;
    }

    auto take = [&](void *dst, uint64_t elems, size_t esz) -> bool {
        size_t bytes = (size_t)elems * esz;
        if (elems == 0) return true;
        if (!c.need(bytes)) { c.ok = false; return false; }
        std::memcpy(dst, c.p + c.off, bytes);
        c.skip(bytes);
        return c.ok;
    };

    bool ok = true;
    ok = ok && take(pos, 3 * nv, sizeof(float));
    ok = ok && take(tex, 2 * nvt, sizeof(float));
    ok = ok && take(nor, 3 * nvn, sizeof(float));
    ok = ok && take(tp, 3 * nt, sizeof(int32_t));
    ok = ok && take(tt, 3 * nt, sizeof(int32_t));
    ok = ok && take(tn, 3 * nt, sizeof(int32_t));
    if (!ok) {
        std::free(pos); std::free(tex); std::free(nor);
        std::free(tp); std::free(tt); std::free(tn);
        return OBJRW_ERR_UNSUPPORTED;
    }

    char *name = nullptr;
    if (name_len) {
        name = (char *)std::malloc(name_bytes + 1);
        if (!name) {
            std::free(pos); std::free(tex); std::free(nor);
            std::free(tp); std::free(tt); std::free(tn);
            return OBJRW_ERR_MEMORY;
        }
        std::memcpy(name, name_src, name_bytes);
        name[name_bytes] = '\0';
    }

    /* Commit (replace previous contents). */
    std::free(mesh->positions); std::free(mesh->texcoords); std::free(mesh->normals);
    std::free(mesh->tri_pos); std::free(mesh->tri_tex); std::free(mesh->tri_nor);
    std::free(mesh->object_name);
    mesh->positions   = (float *)pos;
    mesh->texcoords   = (float *)tex;
    mesh->normals     = (float *)nor;
    mesh->tri_pos     = (int32_t *)tp;
    mesh->tri_tex     = (int32_t *)tt;
    mesh->tri_nor     = (int32_t *)tn;
    mesh->num_vertices  = nv;
    mesh->num_texcoords = nvt;
    mesh->num_normals   = nvn;
    mesh->num_triangles = nt;
    mesh->object_name   = name;
    mesh->has_object_name = (name != nullptr);
    return OBJRW_OK;
}

/* ================================================================== */
/*  Geometry helpers                                                    */
/* ================================================================== */

void compute_normals(const objrw_mesh_t *mesh, float *out) {
    if (!mesh || !out) return;
    const float *P = mesh->positions;
    uint64_t nv = mesh->num_vertices;

    /* zero init */
    for (uint64_t i = 0; i < nv * 3; ++i) out[i] = 0.0f;

    for (uint64_t t = 0; t < mesh->num_triangles; ++t) {
        int32_t i0 = mesh->tri_pos[3 * t + 0];
        int32_t i1 = mesh->tri_pos[3 * t + 1];
        int32_t i2 = mesh->tri_pos[3 * t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            (uint64_t)i0 >= nv || (uint64_t)i1 >= nv || (uint64_t)i2 >= nv) continue;

        const float *a = P + 3 * (size_t)i0;
        const float *b = P + 3 * (size_t)i1;
        const float *c = P + 3 * (size_t)i2;

        /* edge vectors */
        float e1x = b[0]-a[0], e1y = b[1]-a[1], e1z = b[2]-a[2];
        float e2x = c[0]-a[0], e2y = c[1]-a[1], e2z = c[2]-a[2];
        /* normal = cross(e1,e2) (area-weighted implicitly by magnitude) */
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;

        out[3*(size_t)i0+0] += nx; out[3*(size_t)i0+1] += ny; out[3*(size_t)i0+2] += nz;
        out[3*(size_t)i1+0] += nx; out[3*(size_t)i1+1] += ny; out[3*(size_t)i1+2] += nz;
        out[3*(size_t)i2+0] += nx; out[3*(size_t)i2+1] += ny; out[3*(size_t)i2+2] += nz;
    }

    /* normalise */
    for (uint64_t i = 0; i < nv; ++i) {
        float x = out[3*i+0], y = out[3*i+1], z = out[3*i+2];
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 1e-12f) {
            float inv = 1.0f / len;
            out[3*i+0] = x*inv; out[3*i+1] = y*inv; out[3*i+2] = z*inv;
        } else {
            out[3*i+0] = 0.0f; out[3*i+1] = 0.0f; out[3*i+2] = 1.0f;
        }
    }
}

} /* namespace objrw */
