/*
 * czmesh_ascii.cpp
 * ----------------
 * ASCII Wavefront OBJ reader and writer for triangular meshes.
 *
 * Reader:
 *   * line-oriented, tolerant of comments, blank lines, and unknown keywords
 *     (o/g/usemtl/mtllib/s are skipped)
 *   * per-corner "v", "v/vt", "v//vn", "v/vt/vn" face syntax
 *   * 1-based positive and negative (relative-to-end) indices
 *   * n-gons fan-triangulated
 *   * two-phase OpenMP parallel parse when available (sequential otherwise)
 *
 * Writer:
 *   * emits v / vt / vn / f lines with %.9g formatting (exact float32
 *     round-trip), and an optional "o <name>" header
 */
#include "czmesh_internal.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace czm {

/* ================================================================== */
/*  File I/O                                                          */
/* ================================================================== */

bool read_file(const char *path, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    long sz = std::ftell(f);
    if (sz < 0) { std::fclose(f); return false; }
    std::rewind(f);
    out.resize((size_t)sz);
    size_t got = sz ? std::fread(out.data(), 1, sz, f) : 0;
    std::fclose(f);
    if (got != (size_t)sz) return false;
    return true;
}

bool write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    size_t put = len ? std::fwrite(data, 1, len, f) : 0;
    bool ok = (put == len);
    if (std::fclose(f) != 0) ok = false;
    return ok;
}

/* ================================================================== */
/*  Fast numeric helpers (locale-independent)                          */
/* ================================================================== */

namespace {

/* Parse a float starting at *s, advancing *end past the last consumed char.
   Returns 0.0f if no valid number was found. Accepts optional sign,
   decimal point and exponent. */
float parse_float(const char *s, const char **end) {
    const char *p = s;
    while (*p == ' ' || *p == '\t') ++p;
    bool neg = false;
    if (*p == '+') ++p;
    else if (*p == '-') { neg = true; ++p; }
    char *stop = nullptr;
    double v = std::strtod(p, &stop);
    if (stop == p) return 0.0f; /* no digits */
    if (neg) v = -v;
    if (end) *end = stop;
    return (float)v;
}

/* Parse a base-10 32-bit integer starting at *s. Returns 0 if not found. */
int32_t parse_int32(const char *s, const char **end) {
    const char *p = s;
    while (*p == ' ' || *p == '\t') ++p;
    bool neg = false;
    if (*p == '+') ++p;
    else if (*p == '-') { neg = true; ++p; }
    if (*p < '0' || *p > '9') { if (end) *end = p; return 0; }
    long long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
    if (end) *end = p;
    return neg ? (int32_t)(-v) : (int32_t)v;
}

/* Format a float with 9 significant digits (exact float32 round-trip). */
std::string fmt_float(float x) {
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%.9g", (double)x);
    return std::string(tmp);
}

} /* namespace */

/* ================================================================== */
/*  Per-line / per-corner parsing primitives                           */
/* ================================================================== */

namespace {

struct ChunkResult {
    std::vector<float>   v;   /* 3 per vertex   */
    std::vector<float>   vt;  /* 2 per texcoord */
    std::vector<float>   vn;  /* 3 per normal   */
    std::vector<int32_t> corners;   /* (p,t,n) triples, 0 = absent for t/n */
    std::vector<uint32_t> face_cstart; /* index into corners per face */
    std::vector<uint8_t>  face_nc;     /* corner count per face */
    std::string           obj_name;
};

/* Parse one "f ..." corner token of the form  p, p/t, p//n, p/t/n.
   Returns three ints (pos, tex, nor) with 0 meaning "absent" for tex/nor. */
inline void parse_face_token(const char *tok, size_t len,
                             int32_t &p, int32_t &t, int32_t &n) {
    p = 0; t = 0; n = 0;
    /* split on '/' */
    const char *a = tok;
    const char *slash1 = nullptr, *slash2 = nullptr;
    for (size_t i = 0; i < len; ++i) {
        if (tok[i] == '/') {
            if (!slash1) slash1 = tok + i;
            else { slash2 = tok + i; break; }
        }
    }

    auto seg = [&](const char *s, const char *e, int32_t &out) {
        if (e > s) out = parse_int32(s, nullptr); /* 0 if empty/invalid */
    };

    if (!slash1) {
        seg(a, tok + len, p);
    } else if (!slash2) {
        seg(a, slash1, p);
        seg(slash1 + 1, tok + len, t);
    } else {
        seg(a, slash1, p);
        seg(slash1 + 1, slash2, t);
        seg(slash2 + 1, tok + len, n);
    }
}

/* Parse a single chunk of the buffer [start,end) which is guaranteed to
   begin and end on line boundaries. */
void parse_chunk(const char *base, size_t start, size_t end, ChunkResult &r) {
    size_t line_begin = start;
    for (size_t i = start; i <= end; ++i) {
        bool eol = (i == end) || (base[i] == '\n');
        if (!eol) continue;
        size_t line_end = i; /* exclusive; newline at [i] */
        size_t lb = line_begin;
        size_t le = line_end;
        while (lb < le && (base[lb] == ' ' || base[lb] == '\t')) ++lb;
        while (le > lb && (base[le - 1] == ' ' || base[le - 1] == '\t' ||
                           base[le - 1] == '\r' || base[le - 1] == '\n')) --le;
        if (le > lb) {
            char tag = base[lb];
            if (tag == 'v' && (base[lb + 1] == ' ' || base[lb + 1] == '\t')) {
                /* vertex: v x y z [w] [r g b] */
                const char *s = base + lb + 2;
                float x = parse_float(s, &s);
                float y = parse_float(s, &s);
                float z = parse_float(s, &s);
                r.v.push_back(x); r.v.push_back(y); r.v.push_back(z);
            } else if (tag == 'v' && (base[lb + 1] == 't' || base[lb + 1] == 'T') &&
                       (base[lb + 2] == ' ' || base[lb + 2] == '\t')) {
                const char *s = base + lb + 3;
                float u = parse_float(s, &s);
                float v = parse_float(s, &s);
                r.vt.push_back(u); r.vt.push_back(v);
            } else if (tag == 'v' && (base[lb + 1] == 'n' || base[lb + 1] == 'N') &&
                       (base[lb + 2] == ' ' || base[lb + 2] == '\t')) {
                const char *s = base + lb + 3;
                float x = parse_float(s, &s);
                float y = parse_float(s, &s);
                float z = parse_float(s, &s);
                r.vn.push_back(x); r.vn.push_back(y); r.vn.push_back(z);
            } else if ((tag == 'f' || tag == 'F') &&
                       (base[lb + 1] == ' ' || base[lb + 1] == '\t')) {
                /* face: collect tokens */
                uint32_t cstart = (uint32_t)(r.corners.size() / 3);
                uint32_t nc = 0;
                const char *s = base + lb + 2;
                const char *limit = base + le;
                while (s < limit) {
                    while (s < limit && (*s == ' ' || *s == '\t')) ++s;
                    const char *tok = s;
                    while (s < limit && *s != ' ' && *s != '\t') ++s;
                    size_t tlen = (size_t)(s - tok);
                    if (tlen == 0) break;
                    int32_t p, t, n;
                    parse_face_token(tok, tlen, p, t, n);
                    if (p == 0) break; /* invalid corner: stop */
                    r.corners.push_back(p);
                    r.corners.push_back(t);
                    r.corners.push_back(n);
                    ++nc;
                }
                if (nc >= 3) {
                    r.face_cstart.push_back(cstart);
                    r.face_nc.push_back((uint8_t)nc);
                }
            } else if (tag == 'o' && (base[lb + 1] == ' ' || base[lb + 1] == '\t')) {
                /* object name: keep the first one seen (chunk 0 only stores) */
                size_t name_start = lb + 2;
                while (name_start < le && (base[name_start] == ' ' || base[name_start] == '\t')) ++name_start;
                if (name_start < le && r.obj_name.empty()) {
                    r.obj_name.assign(base + name_start, le - name_start);
                }
            }
            /* all other tags (g, usemtl, mtllib, s, comments, etc.) skipped */
        }
        line_begin = i + 1;
        if (i == end) break;
    }
}

} /* namespace */

/* ================================================================== */
/*  parse_ascii                                                        */
/* ================================================================== */

czmesh_status parse_ascii(const uint8_t *buf, size_t len, czmesh_mesh_t *mesh) {
    if (!mesh) { return CZMESH_ERR_NULL_POINTER; }
    const char *base = (const char *)buf;

#if defined(_OPENMP)
    int nthreads = 1;
    if (len > 16384) {
        int maxt = omp_get_max_threads();
        if (maxt < 1) maxt = 1;
        nthreads = maxt;
        if (nthreads > 16) nthreads = 16;
    }
#else
    int nthreads = 1;
#endif

    /* ---- Phase 0: split buffer into nthreads line-aligned chunks ---- */
    std::vector<size_t> chunk_start((size_t)nthreads + 1);
    chunk_start[0] = 0;
    if (nthreads == 1) {
        chunk_start[1] = len;
    } else {
        /* find line boundaries nearest to each ideal split point */
        size_t ideal = len / (size_t)nthreads;
        for (int i = 1; i < nthreads; ++i) {
            size_t target = (size_t)i * ideal;
            /* search forward for the next newline from target */
            size_t j = target;
            while (j < len && buf[j] != '\n') ++j;
            chunk_start[i] = (j < len) ? j + 1 : len;
        }
        chunk_start[nthreads] = len;
    }

    /* ---- Phase 1: parallel per-chunk parse (attributes + raw faces) ---- */
    std::vector<ChunkResult> results((size_t)nthreads);
    {
        bool have_error = false;
#pragma omp parallel for schedule(dynamic) reduction(|:have_error)
        for (int i = 0; i < nthreads; ++i) {
            parse_chunk(base, chunk_start[(size_t)i], chunk_start[(size_t)i + 1],
                        results[(size_t)i]);
        }
    }

    /* ---- Global totals (needed to resolve negative indices) ---- */
    uint64_t total_v = 0, total_vt = 0, total_vn = 0, total_tri = 0;
    for (int i = 0; i < nthreads; ++i) {
        total_v  += (uint64_t)results[(size_t)i].v.size()  / 3;
        total_vt += (uint64_t)results[(size_t)i].vt.size() / 2;
        total_vn += (uint64_t)results[(size_t)i].vn.size() / 3;
        for (size_t fi = 0; fi < results[(size_t)i].face_nc.size(); ++fi)
            total_tri += (uint64_t)results[(size_t)i].face_nc[fi] - 2;
    }

    if (total_v == 0 || total_tri == 0)
        return CZMESH_ERR_EMPTY;

    /* ---- Allocate output SoA buffers ---- */
    auto alloc = [](size_t n, size_t elem) -> void * { return std::malloc(n * elem); };
    if (!alloc(total_v * 3, sizeof(float)) || !alloc(total_vt * 2, sizeof(float)) ||
        !alloc(total_vn * 3, sizeof(float)) || !alloc(total_tri * 3, sizeof(int32_t)) ||
        !alloc(total_tri * 3, sizeof(int32_t)) || !alloc(total_tri * 3, sizeof(int32_t)))
        return CZMESH_ERR_MEMORY;

    float  *o_v   = (float *)alloc(total_v * 3, sizeof(float));
    float  *o_vt  = (float *)alloc(total_vt * 2, sizeof(float));
    float  *o_vn  = (float *)alloc(total_vn * 3, sizeof(float));
    int32_t *o_tp  = (int32_t *)alloc(total_tri * 3, sizeof(int32_t));
    int32_t *o_tt  = (int32_t *)alloc(total_tri * 3, sizeof(int32_t));
    int32_t *o_tn  = (int32_t *)alloc(total_tri * 3, sizeof(int32_t));

    /* ---- Phase 2: concatenate attributes + resolve/triangulate faces ---- */
    uint64_t v_off = 0, vt_off = 0, vn_off = 0, tri_off = 0;
    bool err = false;
    czmesh_status err_code = CZMESH_OK;
    for (int i = 0; i < nthreads && !err; ++i) {
        ChunkResult &r = results[(size_t)i];

        /* attributes */
        if (r.v.size())  std::memcpy(o_v  + 3 * v_off,  r.v.data(),  r.v.size()  * sizeof(float));
        if (r.vt.size()) std::memcpy(o_vt + 2 * vt_off, r.vt.data(), r.vt.size() * sizeof(float));
        if (r.vn.size()) std::memcpy(o_vn + 3 * vn_off, r.vn.data(), r.vn.size() * sizeof(float));
        v_off  += (uint64_t)r.v.size()  / 3;
        vt_off += (uint64_t)r.vt.size() / 2;
        vn_off += (uint64_t)r.vn.size() / 3;

        /* faces: fan-triangulate + resolve indices */
        for (size_t fi = 0; fi < r.face_nc.size() && !err; ++fi) {
            uint32_t cstart = r.face_cstart[fi];
            uint8_t  nc = r.face_nc[fi];
            /* resolve all corners first */
            for (uint8_t c = 0; c < nc; ++c) {
                int32_t rp = r.corners[3 * (cstart + c) + 0];
                int32_t rt = r.corners[3 * (cstart + c) + 1];
                int32_t rn = r.corners[3 * (cstart + c) + 2];
                auto resolve = [](int32_t raw, uint64_t total) -> int64_t {
                    if (raw == 0) return -1; /* absent -> CZMESH_NONE */
                    int64_t idx = (raw > 0) ? (int64_t)(raw - 1)
                                           : (int64_t)total + (int64_t)raw;
                    return idx;
                };
                int64_t ip = resolve(rp, total_v);
                int64_t it = resolve(rt, total_vt);
                int64_t in = resolve(rn, total_vn);
                if (rp != 0 && (ip < 0 || ip >= (int64_t)total_v)) {
                    err = true; err_code = CZMESH_ERR_INCONSISTENT; break;
                }
                if (rt != 0 && (it < 0 || it >= (int64_t)total_vt)) {
                    err = true; err_code = CZMESH_ERR_INCONSISTENT; break;
                }
                if (rn != 0 && (in < 0 || in >= (int64_t)total_vn)) {
                    err = true; err_code = CZMESH_ERR_INCONSISTENT; break;
                }
                r.corners[3 * (cstart + c) + 0] = (int32_t)ip;
                r.corners[3 * (cstart + c) + 1] = (int32_t)it;
                r.corners[3 * (cstart + c) + 2] = (int32_t)in;
            }
            if (err) break;
            /* fan triangulation: (0, i, i+1) for i in [1, nc-2] */
            for (int t = 1; t + 1 < (int)nc; ++t) {
                int32_t a = cstart + 0, b = cstart + t, c = cstart + t + 1;
                size_t out = 3 * (tri_off + (uint64_t)(t - 1));
                o_tp[out + 0] = r.corners[3 * a + 0];
                o_tp[out + 1] = r.corners[3 * b + 0];
                o_tp[out + 2] = r.corners[3 * c + 0];
                o_tt[out + 0] = r.corners[3 * a + 1];
                o_tt[out + 1] = r.corners[3 * b + 1];
                o_tt[out + 2] = r.corners[3 * c + 1];
                o_tn[out + 0] = r.corners[3 * a + 2];
                o_tn[out + 1] = r.corners[3 * b + 2];
                o_tn[out + 2] = r.corners[3 * c + 2];
            }
            tri_off += (uint64_t)nc - 2;
        }
    }

    if (err) {
        std::free(o_v); std::free(o_vt); std::free(o_vn);
        std::free(o_tp); std::free(o_tt); std::free(o_tn);
        return err_code;
    }

    /* ---- Commit into mesh (replace previous contents) ---- */
    std::free(mesh->positions); std::free(mesh->texcoords); std::free(mesh->normals);
    std::free(mesh->tri_pos); std::free(mesh->tri_tex); std::free(mesh->tri_nor);
    std::free(mesh->object_name);

    mesh->positions   = o_v;
    mesh->texcoords   = o_vt;
    mesh->normals     = o_vn;
    mesh->tri_pos     = o_tp;
    mesh->tri_tex     = o_tt;
    mesh->tri_nor     = o_tn;
    mesh->num_vertices  = total_v;
    mesh->num_texcoords = total_vt;
    mesh->num_normals   = total_vn;
    mesh->num_triangles = total_tri;

    /* object name: first non-empty across chunks */
    std::free(mesh->object_name);
    mesh->object_name = nullptr;
    mesh->has_object_name = 0;
    for (int i = 0; i < nthreads; ++i) {
        if (!results[(size_t)i].obj_name.empty()) {
            const char *src = results[(size_t)i].obj_name.c_str();
            size_t n = std::strlen(src) + 1;
            char *dup = (char *)std::malloc(n);
            if (dup) std::memcpy(dup, src, n);
            mesh->object_name = dup;
            mesh->has_object_name = 1;
            break;
        }
    }
    return CZMESH_OK;
}

/* ================================================================== */
/*  write_ascii                                                        */
/* ================================================================== */

czmesh_status write_ascii(const czmesh_mesh_t *mesh, std::string &out) {
    if (!mesh) return CZMESH_ERR_NULL_POINTER;
    if (mesh->num_vertices == 0 || mesh->num_triangles == 0) return CZMESH_ERR_EMPTY;

    /* Reserve a rough upper bound to reduce reallocations. */
    size_t est = 64 +
        mesh->num_vertices  * 32 +
        mesh->num_texcoords * 24 +
        mesh->num_normals   * 32 +
        mesh->num_triangles * 48;
    out.reserve(est);

    auto push_float = [](std::string &s, float x) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.9g", (double)x);
        s.append(tmp);
    };

    out.push_back('#');
    out.push_back(' ');
    out.append("generated by czmesh ");
    out.push_back('\n');

    if (mesh->has_object_name && mesh->object_name) {
        out.push_back('o');
        out.push_back(' ');
        out.append(mesh->object_name);
        out.push_back('\n');
    }

    for (uint64_t i = 0; i < mesh->num_vertices; ++i) {
        out.push_back('v'); out.push_back(' ');
        push_float(out, mesh->positions[3 * i + 0]); out.push_back(' ');
        push_float(out, mesh->positions[3 * i + 1]); out.push_back(' ');
        push_float(out, mesh->positions[3 * i + 2]); out.push_back('\n');
    }

    for (uint64_t i = 0; i < mesh->num_texcoords; ++i) {
        out.push_back('v'); out.push_back('t'); out.push_back(' ');
        push_float(out, mesh->texcoords[2 * i + 0]); out.push_back(' ');
        push_float(out, mesh->texcoords[2 * i + 1]); out.push_back('\n');
    }

    for (uint64_t i = 0; i < mesh->num_normals; ++i) {
        out.push_back('v'); out.push_back('n'); out.push_back(' ');
        push_float(out, mesh->normals[3 * i + 0]); out.push_back(' ');
        push_float(out, mesh->normals[3 * i + 1]); out.push_back(' ');
        push_float(out, mesh->normals[3 * i + 2]); out.push_back('\n');
    }

    for (uint64_t i = 0; i < mesh->num_triangles; ++i) {
        out.push_back('f');
        for (int k = 0; k < 3; ++k) {
            int32_t p = mesh->tri_pos[3 * i + k];
            int32_t t = mesh->tri_tex[3 * i + k];
            int32_t n = mesh->tri_nor[3 * i + k];
            out.push_back(' ');
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%d", (int)(p + 1)); /* 1-based out */
            out.append(tmp);
            if (t != CZMESH_NONE && n != CZMESH_NONE) {
                std::snprintf(tmp, sizeof(tmp), "/%d/%d", (int)(t + 1), (int)(n + 1));
                out.append(tmp);
            } else if (t != CZMESH_NONE) {
                std::snprintf(tmp, sizeof(tmp), "/%d", (int)(t + 1));
                out.append(tmp);
            } else if (n != CZMESH_NONE) {
                std::snprintf(tmp, sizeof(tmp), "//%d", (int)(n + 1));
                out.append(tmp);
            }
        }
        out.push_back('\n');
    }
    return CZMESH_OK;
}

} /* namespace czm */
