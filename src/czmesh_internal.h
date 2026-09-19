/*
 * czmesh_internal.h
 * -----------------
 * Internal types and helper declarations for the czmesh implementation.
 * NOT part of the public API.
 */
#ifndef CZMESH_INTERNAL_H
#define CZMESH_INTERNAL_H

#include "../include/czmesh.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace czm {

/* ------------------------------------------------------------------ */
/*  File I/O helpers                                                    */
/* ------------------------------------------------------------------ */
bool read_file(const char *path, std::vector<uint8_t> &out);
bool write_file(const char *path, const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ */
/*  ASCII OBJ parsing / writing                                         */
/* ------------------------------------------------------------------ */
czmesh_status parse_ascii(const uint8_t *buf, size_t len, czmesh_mesh_t *mesh);
czmesh_status write_ascii(const czmesh_mesh_t *mesh, std::string &out);

/* ------------------------------------------------------------------ */
/*  Binary czobj parsing / writing                                      */
/* ------------------------------------------------------------------ */
czmesh_status read_binary(const uint8_t *buf, size_t len, czmesh_mesh_t *mesh);
czmesh_status write_binary(const czmesh_mesh_t *mesh, std::vector<uint8_t> &out);

/* ------------------------------------------------------------------ */
/*  Geometry helpers                                                    */
/* ------------------------------------------------------------------ */
void compute_normals(const czmesh_mesh_t *mesh, float *normals_out);

#ifdef CZMESH_ENABLE_CUDA
/* ------------------------------------------------------------------ */
/*  Optional GPU (CUDA) fast paths, implemented in czmesh_cuda.cu       */
/* ------------------------------------------------------------------ */
/* True if a usable CUDA device + runtime are present at run time.     */
bool cuda_available();

/* GPU implementations of the geometry helpers. Each returns a standard
 * czmesh_status; callers must fall back to the CPU path on any error so
 * the library keeps working on machines without a GPU. Buffers are plain
 * host memory; transfers are handled internally. */
czmesh_status cuda_bounding_box(const float *positions, uint64_t num_vertices,
                                float min_out[3], float max_out[3]);
czmesh_status cuda_compute_normals(const float *positions,
                                   const int32_t *tri_pos,
                                   uint64_t num_vertices, uint64_t num_triangles,
                                   float *normals_out);
czmesh_status cuda_stats(const float *positions, const int32_t *tri_pos,
                         uint64_t num_vertices, uint64_t num_triangles,
                         double *area_out, double *volume_out);
#endif // CZMESH_ENABLE_CUDA

} /* namespace czm */

#endif /* CZMESH_INTERNAL_H */
