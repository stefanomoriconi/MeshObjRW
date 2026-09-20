/*
 * objrw_internal.h
 * -----------------
 * Internal types and helper declarations for the objrw implementation.
 * NOT part of the public API.
 */
#ifndef OBJRW_INTERNAL_H
#define OBJRW_INTERNAL_H

#include "../include/objrw.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace objrw {

/* ------------------------------------------------------------------ */
/*  File I/O helpers                                                    */
/* ------------------------------------------------------------------ */
bool read_file(const char *path, std::vector<uint8_t> &out);
bool write_file(const char *path, const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ */
/*  ASCII OBJ parsing / writing                                         */
/* ------------------------------------------------------------------ */
objrw_status parse_ascii(const uint8_t *buf, size_t len, objrw_mesh_t *mesh);
objrw_status write_ascii(const objrw_mesh_t *mesh, std::string &out);

/* ------------------------------------------------------------------ */
/*  Binary objrw parsing / writing                                      */
/* ------------------------------------------------------------------ */
objrw_status read_binary(const uint8_t *buf, size_t len, objrw_mesh_t *mesh);
objrw_status write_binary(const objrw_mesh_t *mesh, std::vector<uint8_t> &out);

/* ------------------------------------------------------------------ */
/*  Geometry helpers                                                    */
/* ------------------------------------------------------------------ */
void compute_normals(const objrw_mesh_t *mesh, float *normals_out);

#ifdef OBJRW_ENABLE_CUDA
/* ------------------------------------------------------------------ */
/*  Optional GPU (CUDA) fast paths, implemented in objrw_cuda.cu       */
/* ------------------------------------------------------------------ */
/* True if a usable CUDA device + runtime are present at run time.     */
bool cuda_available();

/* GPU implementations of the geometry helpers. Each returns a standard
 * objrw_status; callers must fall back to the CPU path on any error so
 * the library keeps working on machines without a GPU. Buffers are plain
 * host memory; transfers are handled internally. */
objrw_status cuda_bounding_box(const float *positions, uint64_t num_vertices,
                                float min_out[3], float max_out[3]);
objrw_status cuda_compute_normals(const float *positions,
                                   const int32_t *tri_pos,
                                   uint64_t num_vertices, uint64_t num_triangles,
                                   float *normals_out);
objrw_status cuda_stats(const float *positions, const int32_t *tri_pos,
                         uint64_t num_vertices, uint64_t num_triangles,
                         double *area_out, double *volume_out);
#endif // OBJRW_ENABLE_CUDA

} /* namespace objrw */

#endif /* OBJRW_INTERNAL_H */
