# `czmesh` C API reference

All functions are exposed with a **plain C ABI** (extern "C"). The single
public header is `include/czmesh.h`. Link against the shared library
`czmesh` (Windows: `czmesh.dll`, macOS: `libczmesh.dylib`, Linux:
`libczmesh.so`).

## Version

`CZMESH_VERSION` is `"1.0.0"`.

## Error handling

Every fallible function returns a `czmesh_status`. On failure, call
`czmesh_last_error()` to obtain a human-readable, **thread-local** message.

| Constant | Value | Meaning |
|----------|-------|---------|
| `CZMESH_OK` | 0 | Success. |
| `CZMESH_ERR_NULL_POINTER` | 1 | A required pointer was `NULL`. |
| `CZMESH_ERR_MEMORY` | 2 | Allocation failure. |
| `CZMESH_ERR_IO_OPEN` | 3 | File could not be opened for read/write. |
| `CZMESH_ERR_IO_WRITE` | 4 | I/O error during writing. |
| `CZMESH_ERR_PARSE` | 5 | Malformed / unreadable content. |
| `CZMESH_ERR_UNSUPPORTED` | 6 | Feature not supported by this build. |
| `CZMESH_ERR_INCONSISTENT` | 7 | Data is malformed (e.g. out-of-range index). |
| `CZMESH_ERR_EMPTY` | 8 | Mesh has no geometry. |
| `CZMESH_ERR_INTERNAL` | 99 | Unexpected internal error. |

The sentinel `CZMESH_NONE` is `-1` and means "no index" in a per-corner
`tri_tex` / `tri_nor` slot.

## The mesh object

`czmesh_mesh_t` is an opaque handle (a pointer) that owns all geometry.
Allocate with `czmesh_mesh_create()`, release with `czmesh_mesh_free()`.

> The struct is also readable from other languages for introspection (the
> Python wrapper reads the array pointers and counts directly), but C/C++
> callers should prefer the accessor functions below for safety.

### Lifecycle

```c
czmesh_mesh_t* czmesh_mesh_create(void);
void           czmesh_mesh_free(czmesh_mesh_t *m);
czmesh_status  czmesh_mesh_reserve(czmesh_mesh_t *m,
                                   uint64_t num_vertices,
                                   uint64_t num_texcoords,
                                   uint64_t num_normals,
                                   uint64_t num_triangles);
```

- `czmesh_mesh_create` — returns a new, empty mesh, or `NULL` on OOM.
- `czmesh_mesh_free` — releases all memory (returns `void`). Safe to call on `NULL`.
- `czmesh_mesh_reserve` — optional pre-allocation hint for all four arrays;
  existing data is preserved and it does not change logical counts.

### Building geometry

```c
czmesh_status czmesh_mesh_push_vertex(czmesh_mesh_t *m,
                                      float x, float y, float z);
czmesh_status czmesh_mesh_push_texcoord(czmesh_mesh_t *m, float u, float v);
czmesh_status czmesh_mesh_push_normal(czmesh_mesh_t *m,
                                      float x, float y, float z);
czmesh_status czmesh_mesh_push_triangle(czmesh_mesh_t *m,
                                        int32_t p0, int32_t p1, int32_t p2,
                                        int32_t t0, int32_t t1, int32_t t2,
                                        int32_t n0, int32_t n1, int32_t n2);
```

Each corner supplies a position index `p*`, a texcoord index `t*`, and a
normal index `n*`. Use `CZMESH_NONE` (-1) for a corner's texcoord/normal when
it is absent. Position indices are 0-based and must reference previously
pushed vertices.

### Object name

```c
czmesh_status czmesh_mesh_set_name(czmesh_mesh_t *m, const char *name);
```

Sets the mesh's object name (UTF-8). Pass `NULL` to clear it. When reading an
ASCII OBJ file, the first `o <name>` tag populates the name (the writer
emits it back as an `o` line); the name is preserved across binary
round-trips.

### I/O

```c
czmesh_status czmesh_read(czmesh_mesh_t *m, const char *path);          /* auto-detect */
czmesh_status czmesh_read_ascii(czmesh_mesh_t *m, const char *path);
czmesh_status czmesh_read_binary(czmesh_mesh_t *m, const char *path);

czmesh_status czmesh_write(czmesh_mesh_t *m, const char *path);          /* = ascii */
czmesh_status czmesh_write_ascii(czmesh_mesh_t *m, const char *path);
czmesh_status czmesh_write_binary(czmesh_mesh_t *m, const char *path);
```

`czmesh_read` sniffs the 8-byte header to decide between the ASCII OBJ and
binary `czobj` paths (see [czobj-format.md](czobj-format.md)). The readers
replace the mesh contents (existing arrays are discarded first).

### Geometry helpers

```c
czmesh_status czmesh_mesh_bounding_box(czmesh_mesh_t *m,
                                       float out_min[3], float out_max[3]);
czmesh_status czmesh_mesh_compute_normals(czmesh_mesh_t *m);
czmesh_status czmesh_mesh_stats(czmesh_mesh_t *m,
                                double *out_area, double *out_volume);
```

- `bounding_box` — min/max over all vertices. Returns `CZMESH_ERR_EMPTY`
  when there are no vertices.
- `compute_normals` — recomputes per-vertex normals, area-weighted, unit
  normalised, and adds them to the mesh if none exist.
- `stats` — surface area and signed volume (divergence theorem). Volume is
  meaningful for closed, consistently-wound meshes.

### Introspection

```c
const char* czmesh_last_error(void);
const char* czmesh_build_info(void);
const char* czmesh_version(void);
```

- `czmesh_last_error` — thread-local message from the most recent failing
  call in this thread.
- `czmesh_build_info` — one-line summary of version, compiler, OpenMP and
  CUDA status, and platform.
- `czmesh_version` — `CZMESH_VERSION` string.

## Threading

- Each `czmesh_mesh_t` is **not** thread-safe for concurrent mutation.
- `czmesh_last_error()` is per-thread, so concurrent reads of *different*
  meshes in different threads do not interleave their error messages.
- OpenMP parallelism (when enabled) is internal to a single read/write call
  and completes before the call returns.

## Minimal example

```c
#include <czmesh.h>
#include <stdio.h>

int main(void) {
    czmesh_mesh_t *m = czmesh_mesh_create();
    if (!m) return 1;

    if (czmesh_read("model.obj", m) != CZMESH_OK) {
        fprintf(stderr, "read failed: %s\n", czmesh_last_error());
        czmesh_mesh_free(m);
        return 1;
    }

    double area, vol;
    if (czmesh_mesh_stats(m, &area, &vol) == CZMESH_OK)
        printf("area=%.3f vol=%.3f\n", area, vol);

    czmesh_write_binary(m, "model.czobj");
    czmesh_mesh_free(m);
    return 0;
}
```
