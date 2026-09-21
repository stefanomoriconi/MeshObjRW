# objrw

A fast, portable, multi-platform C/C++ library for **reading and writing
triangular mesh files**, with a documented binary companion format, an
optional GPU (CUDA) acceleration path, and a high-level **Python** wrapper
built on `ctypes`.

`objrw` reads and writes two encodings:

| Encoding | Extension | Notes |
|----------|-----------|-------|
| ASCII Wavefront **OBJ** | `.obj` | The canonical text format. `v`, `vt`, `vn`, `f`; 1-based & negative indices; per-corner `v/vt//vn` syntax; n-gons fan-triangulated. |
| Binary **objrw** (objrw) | `.objrw` | Compact, documented, little-endian, versioned. Auto-detected on read via an 8-byte magic header. |

A single read call, `objrw_read()` / `objrw_io.readOBJ()`, **auto-detects**
the encoding by sniffing the file header, so the same code transparently
handles both ASCII and binary files.

All public symbols carry the `objrw` prefix (library `objrw`, CLI
`objrw_cli`, tests `objrw_tests`, Python package `objrw_io`) so the
package does not clash with other dependencies in larger projects.

---

## Features

- **Portable C ABI** — a plain C11 shared library (C linkage) usable from C,
  C++, Python (`ctypes`), and other FFI languages.
- **Multi-platform** — builds and tests on **Windows (MSVC/Clang)**,
  **macOS**, and **Linux (GCC/Clang)**.
- **Fast & parallel** — the ASCII OBJ **reader** uses a two-phase, line-aligned
  chunked parse parallelised with **OpenMP** when available (fully functional
  and dependency-free when OpenMP is absent). The writer is single-threaded.
- **GPU option** — an optional **CUDA** port for the geometry helpers
  (bounding box, per-vertex normals, and area/volume statistics), gated
  behind `OBJRW_ENABLE_CUDA` (OFF by default, transparent CPU fallback).
- **Geometry helpers** — bounding box, surface area, signed volume, and
  area-weighted per-vertex normal recomputation.
- **Python** — `objrw_io`, a thin, dependency-free `ctypes` wrapper whose
  primary API is `objrw_io.readOBJ(path)` / `objrw_io.writeOBJ(mesh, path,
  flagBinary=...)`, plus a `Mesh` object and lower-level helpers. No numpy
  required.
- **Accurate & tested** — a C test suite (53 checks) plus Python end-to-end
  tests, all runnable on CI across the three platforms.

---

## Repository layout

```
.
├── include/
│   └── objrw.h              # Public C-ABI header
├── src/
│   ├── objrw_internal.h     # Internal declarations
│   ├── objrw.cpp            # Lifecycle, dispatch, geometry helpers
│   ├── objrw_ascii.cpp      # ASCII OBJ parser / writer
│   ├── objrw_binary.cpp     # objrw binary reader / writer
│   └── objrw_cuda.cu        # Optional CUDA kernels (OBJRW_ENABLE_CUDA)
├── tools/
│   └── objrw_cli.cpp        # Command-line front-end
├── tests/
│   └── objrw_tests.cpp      # C test suite (53 checks)
├── python/
│   ├── objrw_io/            # Python package (ctypes wrapper)
│   │   ├── __init__.py
│   │   ├── _version.py
│   │   ├── _errors.py
│   │   ├── _library.py       # cross-platform library discovery
│   │   ├── _ctypes.py        # prototype declarations + wrappers
│   │   └── _mesh.py          # high-level Mesh object
│   └── test_objrw_io.py     # Python end-to-end tests
├── docs/                     # detailed documentation
├── CMakeLists.txt
├── LICENSE                   # MIT
└── .github/workflows/ci.yml  # CI matrix (win / macos / linux)
```

---

## Building

### Prerequisites

- CMake ≥ 3.16
- A C++17 compiler:
  - **Windows:** MSVC (Visual Studio 2019/2022) or Clang
  - **macOS:** `clang` (Xcode command line tools)
  - **Linux:** `g++` or `clang++`
- *(optional)* CUDA toolkit, if you want the GPU path

### Build (any platform)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Windows (MSVC, multi-config) example:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces:

- `objrw` shared library — `objrw.dll` / `libobjrw.so` / `libobjrw.dylib`
- `objrw_cli` — command-line tool
- `objrw_tests` — the C test suite

### Options

| CMake option | Default | Description |
|--------------|---------|-------------|
| `OBJRW_BUILD_TESTS` | `ON` | Build the C test suite. |
| `OBJRW_BUILD_CLI` | `ON` | Build `objrw_cli`. |
| `OBJRW_ENABLE_OPENMP` | `ON` | Parallelise the ASCII OBJ reader with OpenMP (no-op if absent). |
| `OBJRW_ENABLE_CUDA` | `OFF` | Compile the optional CUDA kernels (`src/objrw_cuda.cu`). Requires a CUDA toolkit (`nvcc`) — enabled via CMake's `enable_language(CUDA)`. |

---

## Testing

### C tests

```sh
# run the test executable (location depends on build type)
./build/Release/objrw_tests          # Windows MSVC
# or, single-config builds:
./build/objrw_tests
```

Expected output ends with:

```
PASS: 53   FAIL: 0
ALL TESTS PASSED
```

### Python tests

Make sure the built library is discoverable (see below), then:

```sh
python python/test_objrw_io.py
```

Expected output ends with:

```
ALL PYTHON TESTS PASSED
```

---

## Command-line usage (`objrw_cli`)

```sh
objrw_cli info     model.obj          # summary: counts, bbox, area, name
objrw_cli ascii    in.objrw  out.obj  # binary -> ASCII
objrw_cli binary   in.obj    out.objrw# ASCII  -> binary
objrw_cli normals  in.obj  out.obj    # recompute per-vertex normals
objrw_cli version                                 # print build info
```

Examples:

```sh
objrw_cli info cube.obj
objrw_cli binary cube.obj cube.objrw
objrw_cli info cube.objrw
```

---

## Python usage (`objrw_io`)

`objrw_io` is a thin `ctypes` wrapper. It locates the shared library
automatically (source checkout layout, `OBJRW_LIBRARY_PATH`, or the system
path). A single `import objrw_io` is all that is needed:

```python
import objrw_io

# Import — encoding is auto-detected (ASCII .obj or binary .objrw).
mesh_in = objrw_io.readOBJ("path/to/meshFile.obj")
print(mesh_in.num_vertices, mesh_in.num_triangles)
print(mesh_in.bounding_box())      # (min, max)
print(mesh_in.stats())             # (area, signed_volume)

mesh_in.compute_normals()          # area-weighted per-vertex normals

# Export — ASCII OBJ by default, compact binary when flagBinary=True.
mesh_out = objrw_io.writeOBJ(mesh_in, "path/to/output/meshFile.obj",
                             flagBinary=False)          # ASCII .obj
# mesh_out = objrw_io.writeOBJ(mesh_in, "out.objrw", flagBinary=True)
```

`readOBJ` / `writeOBJ` validate their arguments and raise `TypeError` on
bad inputs or `objrw_io.OBJRWError` (with a numeric `.code`) on I/O or parse
failure. The lower-level `objrw_io.read` / `write_ascii` / `write_binary`
helpers and the `Mesh` object remain available for explicit control.

Building a mesh from scratch:

```python
m = objrw_io.Mesh()
m.object_name = "my_mesh"

i0 = m.add_vertex(0.0, 0.0, 0.0)
i1 = m.add_vertex(1.0, 0.0, 0.0)
i2 = m.add_vertex(1.0, 1.0, 0.0)
m.add_triangle(i0, i1, i2)

m.write_ascii("built.obj")
```

### Locating the library

The loader searches, in order:

1. `OBJRW_LIBRARY_PATH` (explicit override — a file path or a directory),
2. common locations relative to the package (`build/Release`, `build/`, …),
3. the system library search path (`ctypes.util.find_library`).

To point at a specific build:

```powershell
# Windows (PowerShell)
$env:OBJRW_LIBRARY_PATH = "$PWD\build\Release\objrw.dll"
# or make it discoverable by adding the dir to PATH:
$env:PATH = "$PWD\build\Release;" + $env:PATH
```

```sh
# macOS / Linux
export OBJRW_LIBRARY_PATH="$PWD/build/libobjrw.dylib"   # or .so
```

---

## C / C++ usage

```c
#include <objrw.h>
#include <stdio.h>

int main(void) {
    objrw_mesh_t *m = objrw_mesh_create();

    /* Auto-detect ASCII vs binary on read. */
    if (objrw_read("model.obj", m) != OBJRW_OK) {
        fprintf(stderr, "read failed: %s\n", objrw_last_error());
        objrw_mesh_free(m);
        return 1;
    }

    float mn[3], mx[3];
    objrw_mesh_bounding_box(m, mn, mx);

    double area, vol;
    objrw_mesh_stats(m, &area, &vol);
    printf("vertices=%llu area=%f vol=%f\n",
           (unsigned long long)m->num_vertices, area, vol);

    objrw_write_binary(m, "model.objrw");   /* save as binary */
    objrw_mesh_free(m);
    return 0;
}
```

Build against the library:

```cmake
add_executable(app main.c)
target_include_directories(app PRIVATE include/)
target_link_libraries(app PRIVATE objrw)   # or link objrw.dll / libobjrw.so
```

---

## The binary `objrw` format

All values are **little-endian**, fixed-size. Layout (version 1):

| # | Type | Bytes | Field |
|---|------|-------|-------|
| 1 | `byte[8]` | 8 | magic `"OBJRW\0\0\0"` |
| 2 | `u32` | 4 | `format_ver` (always `1`) |
| 3 | `u64` | 8 | `num_vertices` |
| 4 | `u64` | 8 | `num_texcoords` |
| 5 | `u64` | 8 | `num_normals` |
| 6 | `u64` | 8 | `num_triangles` |
| 7 | `u64` | 8 | `name_len` (0 when no name) |
| 8 | `byte[]` | `name_len` | `name` (only if `name_len > 0`) |
| 9 | `float[]` | `3·nv` | `positions` (x,y,z interleaved) |
| 10 | `float[]` | `2·nvt` | `texcoords` (u,v interleaved) |
| 11 | `float[]` | `3·nvn` | `normals` (x,y,z interleaved) |
| 12 | `i32[]` | `3·nt` | `tri_pos` (per-corner vertex index) |
| 13 | `i32[]` | `3·nt` | `tri_tex` (`-1` when absent) |
| 14 | `i32[]` | `3·nt` | `tri_nor` (`-1` when absent) |

Indices are 0-based. A value of `-1` (`OBJRW_NONE`) in `tri_tex` / `tri_nor`
means the corresponding corner has no texture coordinate / normal. See
[`docs/objrw-format.md`](docs/objrw-format.md) for the full specification.

---

## Documentation

- [`docs/objrw-format.md`](docs/objrw-format.md) — binary format specification
- [`docs/c-api.md`](docs/c-api.md) — C API reference
- [`docs/python-usage.md`](docs/python-usage.md) — Python `objrw_io` guide
- [`docs/build-guide.md`](docs/build-guide.md) — platform build guide

---

## License

MIT — see [LICENSE](LICENSE).
