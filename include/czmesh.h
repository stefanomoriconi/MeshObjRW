/*
 * czmesh.h
 * ----------
 * czmesh - a fast, portable, multi-platform library for reading and writing
 * Wavefront OBJ *triangular* mesh files, plus a compact binary mesh format
 * ("czobj"). The library is a plain C11 ABI (C linkage) shared library so it
 * can be consumed from C, C++, Python (ctypes) and other FFI languages.
 *
 * Capabilities
 *   * ASCII OBJ  : read & write  (v, vt, vn, f; 1-based and negative indices;
 *                   per-corner v/vt//vn syntax; n-gons fan-triangulated)
 *   * Binary czobj: read & write (custom, documented, little-endian, stable)
 *   * Geometry     : bounding box, mesh stats, GPU-friendly flat buffers
 *   * Parallelism : OpenMP two-phase parser / writer when available
 *   * GPU         : optional CUDA port (CZMESH_ENABLE_CUDA) for bulk
 *                   copy / normal / stats operations
 *
 * Multi-platform: Windows (MSVC/Clang), macOS, Linux (GCC/Clang).
 *
 * The public symbols all carry the `czmesh_` prefix to avoid collisions when
 * the library is embedded in larger projects.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CZMESH_H
#define CZMESH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Symbol visibility macro.
 *  - MSVC: mark public API for export when building the shared library
 *          (CZMESH_BUILD), or for import otherwise.
 *  - ELF / Mach-O: default visibility already handles this, so the macro
 *          expands to nothing.
 */
/*
 * On MSVC we mark the public API with dllexport.  Consumers linking against
 * the shared library will import these symbols; dllexport on the declaration
 * is a standard "export-all" approach and is safe for importers.  On ELF and
 * Mach-O, default visibility already exports non-static symbols, so the macro
 * is empty there.  All public symbols carry the czmesh_ prefix, so nothing
 * else is exposed.
 */
#if defined(_WIN32) && defined(_MSC_VER)
  #define CZMESH_API __declspec(dllexport)
#else
  #define CZMESH_API
#endif

/* ------------------------------------------------------------------ */
/*  ABI / version                                                       */
/* ------------------------------------------------------------------ */

/* Library version. Bumped on every ABI-breaking change. */
#define CZMESH_VERSION_MAJOR 1
#define CZMESH_VERSION_MINOR 0
#define CZMESH_VERSION_PATCH 0

/*
 * The binary "czobj" file magic. First 8 bytes of every czobj stream:
 *   'C','Z','O','B','J', 0x00, 0x00, 0x00
 * czmesh_read() sniffs these bytes to transparently dispatch between
 * the binary and the ASCII OBJ readers.
 */
#define CZMESH_BIN_MAGIC "CZOBJ" /* 5 chars, no NUL in the macro */

/* ------------------------------------------------------------------ */
/*  Error codes / result handling                                       */
/* ------------------------------------------------------------------ */

/*
 * Every public function that can fail returns one of these codes.
 * CZMESH_OK (0) means success; anything else is an error. The most
 * recent human-readable error message is available via czmesh_last_error().
 */
typedef enum {
    CZMESH_OK                 =  0,
    CZMESH_ERR_NULL_POINTER   =  1,
    CZMESH_ERR_MEMORY         =  2,
    CZMESH_ERR_IO_OPEN        =  3, /* cannot open / read the file       */
    CZMESH_ERR_IO_WRITE       =  4, /* cannot create / write the file    */
    CZMESH_ERR_PARSE          =  5, /* malformed geometry / bad index    */
    CZMESH_ERR_UNSUPPORTED    =  6, /* unsupported or corrupted format   */
    CZMESH_ERR_INCONSISTENT   =  7, /* index points past vertex count    */
    CZMESH_ERR_EMPTY          =  8, /* mesh has no geometry              */
    CZMESH_ERR_INTERNAL       =  99 /* anything else                     */
} czmesh_status;

/* ------------------------------------------------------------------ */
/*  Core data structures                                                */
/* ------------------------------------------------------------------ */

/*
 * czmesh_mesh_t
 *  Flat, GPU-friendly container for a triangular mesh.
 *
 *  All arrays are owned by the library and released by czmesh_mesh_free().
 *  Indices are 0-based and reference entries in the corresponding attribute
 *  array.  A value of CZMESH_NONE (-1) in tri_tex / tri_nor means "the
 *  triangle corner has no texture coordinate / normal".
 *
 *  Memory layout (SoA, contiguous, cache & GPU friendly):
 *     positions : float[3 * num_vertices]   x,y,z interleaved
 *     texcoords : float[2 * num_texcoords]  u,v  interleaved
 *     normals   : float[3 * num_normals]    x,y,z interleaved
 *     tri_pos   : int32 [3 * num_triangles] per-corner position index
 *     tri_tex   : int32 [3 * num_triangles] per-corner texcoord index
 *     tri_nor   : int32 [3 * num_triangles] per-corner normal index
 *
 *  If a mesh has no texture coordinates, tri_tex holds CZMESH_NONE for all
 *  corners and num_texcoords is 0.  Same for normals.
 */
typedef struct czmesh_mesh {
    float   *positions;   /* 3 * num_vertices */
    float   *texcoords;   /* 2 * num_texcoords (may be NULL) */
    float   *normals;     /* 3 * num_normals  (may be NULL) */
    int32_t *tri_pos;     /* 3 * num_triangles */
    int32_t *tri_tex;     /* 3 * num_triangles (CZMESH_NONE if absent) */
    int32_t *tri_nor;     /* 3 * num_triangles (CZMESH_NONE if absent) */

    uint64_t num_vertices;
    uint64_t num_texcoords;
    uint64_t num_normals;
    uint64_t num_triangles;

    /* Optional metadata carried through read/write (may be NULL/empty). */
    char    *object_name;
    int32_t  has_object_name; /* 0 / 1 */
} czmesh_mesh_t;

#define CZMESH_NONE (-1)

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/*
 * Allocate an empty mesh (all counts zero, arrays NULL).  On failure the
 * returned pointer is NULL.
 */
CZMESH_API czmesh_mesh_t *czmesh_mesh_create(void);

/*
 * Free a mesh and all memory it owns.  Safe to call with NULL.
 */
CZMESH_API void czmesh_mesh_free(czmesh_mesh_t *mesh);

/*
 * Resize the mesh so it can hold the requested number of vertices /
 * texcoords / normals / triangles.  Existing data is preserved.  Returns
 * CZMESH_OK or an error code.
 */
CZMESH_API czmesh_status czmesh_mesh_reserve(czmesh_mesh_t *mesh,
                                  uint64_t num_vertices,
                                  uint64_t num_texcoords,
                                  uint64_t num_normals,
                                  uint64_t num_triangles);

/*
 * Append helpers (for building a mesh programmatically).  All append the
 * value at the *end* of the corresponding array and increment the count.
 */
CZMESH_API czmesh_status czmesh_mesh_push_vertex(czmesh_mesh_t *m, float x, float y, float z);
CZMESH_API czmesh_status czmesh_mesh_push_texcoord(czmesh_mesh_t *m, float u, float v);
CZMESH_API czmesh_status czmesh_mesh_push_normal(czmesh_mesh_t *m, float x, float y, float z);
CZMESH_API czmesh_status czmesh_mesh_push_triangle(czmesh_mesh_t *m,
                                        int32_t p0, int32_t p1, int32_t p2,
                                        int32_t t0, int32_t t1, int32_t t2,
                                        int32_t n0, int32_t n1, int32_t n2);

/*
 * Set (or clear) the object name stored on the mesh.
 *
 * The library copies `name` into its own buffer (allocated with the same
 * allocator used elsewhere in the library, so the mesh remains fully
 * self-contained and portable across the C/Python boundary).  Pass `NULL`
 * to clear the name.  Returns CZMESH_ERR_MEMORY on allocation failure.
 */
CZMESH_API czmesh_status czmesh_mesh_set_name(czmesh_mesh_t *m, const char *name);

/* ------------------------------------------------------------------ */
/*  Reading                                                             */
/* ------------------------------------------------------------------ */

/*
 * Read a mesh from a file.  The encoding (ASCII OBJ vs binary czobj) is
 * auto-detected by sniffing the 8-byte magic at the start of the stream.
 *
 * Returns CZMESH_OK on success.  On failure the previous contents of *mesh
 * are left unchanged and a message is available via czmesh_last_error().
 */
CZMESH_API czmesh_status czmesh_read(const char *path, czmesh_mesh_t *mesh);

/*
 * Explicit ASCII-OBJ reader.  Equivalent to czmesh_read() when the file is
 * a plain text OBJ.  Exposed separately so callers can force the path.
 */
CZMESH_API czmesh_status czmesh_read_ascii(const char *path, czmesh_mesh_t *mesh);

/*
 * Explicit binary-czobj reader.  Exposed separately so callers can force
 * the path (avoids the sniff when you already know the encoding).
 */
CZMESH_API czmesh_status czmesh_read_binary(const char *path, czmesh_mesh_t *mesh);

/* ------------------------------------------------------------------ */
/*  Writing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Write a mesh to a file as ASCII OBJ.
 */
CZMESH_API czmesh_status czmesh_write_ascii(const char *path, const czmesh_mesh_t *mesh);

/*
 * Write a mesh to a file in the compact binary czobj format.
 */
CZMESH_API czmesh_status czmesh_write_binary(const char *path, const czmesh_mesh_t *mesh);

/*
 * Convenience: write ASCII OBJ (the canonical Wavefront text format).
 */
CZMESH_API czmesh_status czmesh_write(const char *path, const czmesh_mesh_t *mesh);

/* ------------------------------------------------------------------ */
/*  Geometry helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Compute the axis-aligned bounding box of the position array.
 *   min[3], max[3] may be NULL to skip writing that part.
 * Returns CZMESH_ERR_EMPTY if the mesh has no vertices.
 */
CZMESH_API czmesh_status czmesh_mesh_bounding_box(const czmesh_mesh_t *mesh,
                                       float min_out[3],
                                       float max_out[3]);

/*
 * Recompute per-vertex normals from the triangles (area-weighted, averaged
 * over adjacent triangles, then normalised).  Overwrites `normals` and sets
 * num_normals = num_vertices.  Returns CZMESH_ERR_EMPTY if there are no
 * triangles.
 */
CZMESH_API czmesh_status czmesh_mesh_compute_normals(czmesh_mesh_t *mesh);

/*
 * Mesh statistics: total surface area (sum of triangle areas) and total
 * volume (signed, via the divergence theorem).  Useful for sanity checks
 * and for comparing meshes after a round trip.
 *   area_out   may be NULL
 *   volume_out may be NULL
 */
CZMESH_API czmesh_status czmesh_mesh_stats(const czmesh_mesh_t *mesh,
                                double *area_out,
                                double *volume_out);

/* ------------------------------------------------------------------ */
/*  Diagnostics / info                                                  */
/* ------------------------------------------------------------------ */

/*
 * Human-readable, NUL-terminated string of the most recent error.  Returns
 * an empty string when the last operation succeeded.  Thread-local.
 */
CZMESH_API const char *czmesh_last_error(void);

/*
 * Build info string: compiler, OpenMP, CUDA support, platform.
 * Returns a static string (do not free).
 */
CZMESH_API const char *czmesh_build_info(void);

/*
 * Library version string, e.g. "1.0.0".  Returns a static string.
 */
CZMESH_API const char *czmesh_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CZMESH_H */
