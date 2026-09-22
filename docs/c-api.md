# `objrw` C API reference

All functions are exposed with a **plain C ABI** (extern "C"). The single
public header is `include/objrw.h`. Link against the shared library
`objrw` (Windows: `objrw.dll`, macOS: `libobjrw.dylib`, Linux:
`libobjrw.so`).

## Version

`OBJRW_VERSION` is `"1.0.0"`.

## Error handling

Every fallible function returns a `objrw_status`. On failure, call
`objrw_last_error()` to obtain a human-readable, **thread-local** message.

| Constant | Value | Meaning |
|----------|-------|---------|
| `OBJRW_OK` | 0 | Success. |
| `OBJRW_ERR_NULL_POINTER` | 1 | A required pointer was `NULL`. |
| `OBJRW_ERR_MEMORY` | 2 | Allocation failure. |
| `OBJRW_ERR_IO_OPEN` | 3 | File could not be opened for read/write. |
| `OBJRW_ERR_IO_WRITE` | 4 | I/O error during writing. |
| `OBJRW_ERR_PARSE` | 5 | Malformed / unreadable content. |
| `OBJRW_ERR_UNSUPPORTED` | 6 | Feature not supported by this build. |
| `OBJRW_ERR_INCONSISTENT` | 7 | Data is malformed (e.g. out-of-range index). |
| `OBJRW_ERR_EMPTY` | 8 | Mesh has no geometry. |
| `OBJRW_ERR_INTERNAL` | 99 | Unexpected internal error. |

The sentinel `OBJRW_NONE` is `-1` and means "no index" in a per-corner
`tri_tex` / `tri_nor` slot.

## The mesh object

`objrw_mesh_t` is an opaque handle (a pointer) that owns all geometry.
Allocate with `objrw_mesh_create()`, release with `objrw_mesh_free()`.

> The struct is also readable from other languages for introspection (the
> Python wrapper reads the array pointers and counts directly), but C/C++
> callers should prefer the accessor functions below for safety.

### Lifecycle

```c
objrw_mesh_t* objrw_mesh_create(void);
void           objrw_mesh_free(objrw_mesh_t *m);
objrw_status  objrw_mesh_reserve(objrw_mesh_t *m,
                                   uint64_t num_vertices,
                                   uint64_t num_texcoords,
                                   uint64_t num_normals,
                                   uint64_t num_triangles);
```

- `objrw_mesh_create` — returns a new, empty mesh, or `NULL` on OOM.
- `objrw_mesh_free` — releases all memory (returns `void`). Safe to call on `NULL`.
- `objrw_mesh_reserve` — optional pre-allocation hint for all four arrays;
  existing data is preserved and it does not change logical counts.

### Building geometry

```c
objrw_status objrw_mesh_push_vertex(objrw_mesh_t *m,
                                      float x, float y, float z);
objrw_status objrw_mesh_push_texcoord(objrw_mesh_t *m, float u, float v);
objrw_status objrw_mesh_push_normal(objrw_mesh_t *m,
                                      float x, float y, float z);
objrw_status objrw_mesh_push_triangle(objrw_mesh_t *m,
                                        int32_t p0, int32_t p1, int32_t p2,
                                        int32_t t0, int32_t t1, int32_t t2,
                                        int32_t n0, int32_t n1, int32_t n2);
```

Each corner supplies a position index `p*`, a texcoord index `t*`, and a
normal index `n*`. Use `OBJRW_NONE` (-1) for a corner's texcoord/normal when
it is absent. Position indices are 0-based and must reference previously
pushed vertices.

### Object name

```c
objrw_status objrw_mesh_set_name(objrw_mesh_t *m, const char *name);
```

Sets the mesh's object name (UTF-8). Pass `NULL` to clear it. When reading an
ASCII OBJ file, the first `o <name>` tag populates the name (the writer
emits it back as an `o` line); the name is preserved across binary
round-trips.

### I/O

```c
objrw_status objrw_read(objrw_mesh_t *m, const char *path);          /* auto-detect */
objrw_status objrw_read_ascii(objrw_mesh_t *m, const char *path);
objrw_status objrw_read_binary(objrw_mesh_t *m, const char *path);

objrw_status objrw_write(objrw_mesh_t *m, const char *path);          /* = ascii */
objrw_status objrw_write_ascii(objrw_mesh_t *m, const char *path);
objrw_status objrw_write_binary(objrw_mesh_t *m, const char *path);
```

`objrw_read` sniffs the 8-byte header to decide between the ASCII OBJ and
binary `objrw` paths (see [objrw-format.md](objrw-format.md)). The readers
replace the mesh contents (existing arrays are discarded first).

### Geometry helpers

```c
objrw_status objrw_mesh_bounding_box(objrw_mesh_t *m,
                                       float out_min[3], float out_max[3]);
objrw_status objrw_mesh_compute_normals(objrw_mesh_t *m);
objrw_status objrw_mesh_stats(objrw_mesh_t *m,
                                double *out_area, double *out_volume);
```

- `bounding_box` — min/max over all vertices. Returns `OBJRW_ERR_EMPTY`
  when there are no vertices.
- `compute_normals` — recomputes per-vertex normals, area-weighted, unit
  normalised, and adds them to the mesh if none exist.
- `stats` — surface area and signed volume (divergence theorem). Volume is
  meaningful for closed, consistently-wound meshes.

### Introspection

```c
const char* objrw_last_error(void);
const char* objrw_build_info(void);
const char* objrw_version(void);
```

- `objrw_last_error` — thread-local message from the most recent failing
  call in this thread.
- `objrw_build_info` — one-line summary of version, compiler, OpenMP and
  CUDA status, and platform.
- `objrw_version` — `OBJRW_VERSION` string.

## Threading

- Each `objrw_mesh_t` is **not** thread-safe for concurrent mutation.
- `objrw_last_error()` is per-thread, so concurrent reads of *different*
  meshes in different threads do not interleave their error messages.
- OpenMP parallelism (when enabled) is internal to a single read/write call
  and completes before the call returns.

## Minimal example

```c
#include <objrw.h>
#include <stdio.h>

int main(void) {
    objrw_mesh_t *m = objrw_mesh_create();
    if (!m) return 1;

    if (objrw_read("model.obj", m) != OBJRW_OK) {
        fprintf(stderr, "read failed: %s\n", objrw_last_error());
        objrw_mesh_free(m);
        return 1;
    }

    double area, vol;
    if (objrw_mesh_stats(m, &area, &vol) == OBJRW_OK)
        printf("area=%.3f vol=%.3f\n", area, vol);

    objrw_write_binary(m, "model.objrw");
    objrw_mesh_free(m);
    return 0;
}
```
