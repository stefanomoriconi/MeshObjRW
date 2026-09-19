# czmesh

A fast, portable, multi-platform C/C++ library for **reading and writing
triangular mesh files**, with a documented binary companion format, an
optional GPU (CUDA) acceleration path, and a high-level **Python** wrapper
built on `ctypes`.

`czmesh` reads and writes two encodings:

| Encoding | Extension | Notes |
|----------|-----------|-------|
| ASCII Wavefront **OBJ** | `.obj` | The canonical text format. `v`, `vt`, `vn`, `f`; 1-based & negative indices; per-corner `v/vt//vn` syntax; n-gons fan-triangulated. |
| Binary **czobj** (czmesh) | `.czobj` | Compact, documented, little-endian, versioned. Auto-detected on read via an 8-byte magic header. |

A single read call, `czmesh_read()` / `czmesh_io.read()`, **auto-detects**
the encoding by sniffing the file header, so the same code transparently
handles both ASCII and binary files.

All public symbols carry the `czmesh` prefix (library `czmesh`, CLI
`czmesh_cli`, tests `czmesh_tests`, Python package `czmesh_io`) so the
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
  behind `CZMESH_ENABLE_CUDA` (OFF by default, transparent CPU fallback).
- **Geometry helpers** — bounding box, surface area, signed volume, and
  area-weighted per-vertex normal recomputation.
- **Python** — `czmesh_io`, a thin, dependency-free `ctypes` wrapper exposing
  a `Mesh` object and `read` / `write` helpers. No numpy required.
- **Accurate & tested** — a C test suite (53 checks) plus Python end-to-end
  tests, all runnable on CI across the three platforms.

---

## Repository layout

```
.
├── include/
│   └── czmesh.h              # Public C-ABI header
├── src/
│   ├── czmesh_internal.h     # Internal declarations
│   ├── czmesh.cpp            # Lifecycle, dispatch, geometry helpers
│   ├── czmesh_ascii.cpp      # ASCII OBJ parser / writer
│   ├── czmesh_binary.cpp     # czobj binary reader / writer
│   └── czmesh_cuda.cu        # Optional CUDA kernels (CZMESH_ENABLE_CUDA)
├── tools/
│   └── czmesh_cli.cpp        # Command-line front-end
├── tests/
│   └── czmesh_tests.cpp      # C test suite (53 checks)
├── python/
│   ├── czmesh_io/            # Python package (ctypes wrapper)
│   │   ├── __init__.py
│   │   ├── _version.py
│   │   ├── _errors.py
│   │   ├── _library.py       # cross-platform library discovery
│   │   ├── _ctypes.py        # prototype declarations + wrappers
│   │   └── _mesh.py          # high-level Mesh object
│   └── test_czmesh_io.py     # Python end-to-end tests
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

- `czmesh` shared library — `czmesh.dll` / `libczmesh.so` / `libczmesh.dylib`
- `czmesh_cli` — command-line tool
- `czmesh_tests` — the C test suite

### Options

| CMake option | Default | Description |
|--------------|---------|-------------|
| `CZMESH_BUILD_TESTS` | `ON` | Build the C test suite. |
| `CZMESH_BUILD_CLI` | `ON` | Build `czmesh_cli`. |
| `CZMESH_ENABLE_OPENMP` | `ON` | Parallelise the ASCII OBJ reader with OpenMP (no-op if absent). |
| `CZMESH_ENABLE_CUDA` | `OFF` | Compile the optional CUDA kernels (`src/czmesh_cuda.cu`). Requires a CUDA toolkit (`nvcc`) — enabled via CMake's `enable_language(CUDA)`. |

---

## Testing

### C tests

```sh
# run the test executable (location depends on build type)
./build/Release/czmesh_tests          # Windows MSVC
# or, single-config builds:
./build/czmesh_tests
```

Expected output ends with:

```
PASS: 53   FAIL: 0
ALL TESTS PASSED
```

### Python tests

Make sure the built library is discoverable (see below), then:

```sh
python python/test_czmesh_io.py
```

Expected output ends with:

```
ALL PYTHON TESTS PASSED
```

---

## Command-line usage (`czmesh_cli`)

```sh
czmesh_cli info     model.obj          # summary: counts, bbox, area, name
czmesh_cli ascii    in.czobj  out.obj  # binary -> ASCII
czmesh_cli binary   in.obj    out.czobj# ASCII  -> binary
czmesh_cli normals  in.obj  out.obj    # recompute per-vertex normals
czmesh_cli version                                 # print build info
```

Examples:

```sh
czmesh_cli info cube.obj
czmesh_cli binary cube.obj cube.czobj
czmesh_cli info cube.czobj
```

---

## Python usage (`czmesh_io`)

`czmesh_io` is a thin `ctypes` wrapper. It locates the shared library
automatically (source checkout layout, `CZMESH_LIBRARY_PATH`, or the system
path).

```python
import czmesh_io

# Auto-detect encoding (ASCII .obj or binary .czobj) on read.
m = czmesh_io.read("model.obj")
print(m.num_vertices, m.num_triangles)
print(m.bounding_box())      # (min, max)
print(m.stats())             # (area, signed_volume)

m.compute_normals()          # area-weighted per-vertex normals

m.write("model.czobj")        # save as binary (auto-detected later)
m2 = czmesh_io.read("model.czobj")
```

Building a mesh from scratch:

```python
m = czmesh_io.Mesh()
m.object_name = "my_mesh"

i0 = m.add_vertex(0.0, 0.0, 0.0)
i1 = m.add_vertex(1.0, 0.0, 0.0)
i2 = m.add_vertex(1.0, 1.0, 0.0)
m.add_triangle(i0, i1, i2)

m.write_ascii("built.obj")
```

### Locating the library

The loader searches, in order:

1. `CZMESH_LIBRARY_PATH` (explicit override — a file path or a directory),
2. common locations relative to the package (`build/Release`, `build/`, …),
3. the system library search path (`ctypes.util.find_library`).

To point at a specific build:

```powershell
# Windows (PowerShell)
$env:CZMESH_LIBRARY_PATH = "$PWD\build\Release\czmesh.dll"
# or make it discoverable by adding the dir to PATH:
$env:PATH = "$PWD\build\Release;" + $env:PATH
```

```sh
# macOS / Linux
export CZMESH_LIBRARY_PATH="$PWD/build/libczmesh.dylib"   # or .so
```

---

## C / C++ usage

```c
#include <czmesh.h>
#include <stdio.h>

int main(void) {
    czmesh_mesh_t *m = czmesh_mesh_create();

    /* Auto-detect ASCII vs binary on read. */
    if (czmesh_read("model.obj", m) != CZMESH_OK) {
        fprintf(stderr, "read failed: %s\n", czmesh_last_error());
        czmesh_mesh_free(m);
        return 1;
    }

    float mn[3], mx[3];
    czmesh_mesh_bounding_box(m, mn, mx);

    double area, vol;
    czmesh_mesh_stats(m, &area, &vol);
    printf("vertices=%llu area=%f vol=%f\n",
           (unsigned long long)m->num_vertices, area, vol);

    czmesh_write_binary(m, "model.czobj");   /* save as binary */
    czmesh_mesh_free(m);
    return 0;
}
```

Build against the library:

```cmake
add_executable(app main.c)
target_include_directories(app PRIVATE include/)
target_link_libraries(app PRIVATE czmesh)   # or link czmesh.dll / libczmesh.so
```

---

## The binary `czobj` format

All values are **little-endian**, fixed-size. Layout (version 1):

| # | Type | Bytes | Field |
|---|------|-------|-------|
| 1 | `byte[8]` | 8 | magic `"CZOBJ\0\0\0"` |
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

Indices are 0-based. A value of `-1` (`CZMESH_NONE`) in `tri_tex` / `tri_nor`
means the corresponding corner has no texture coordinate / normal. See
[`docs/czobj-format.md`](docs/czobj-format.md) for the full specification.

---

## Documentation

- [`docs/czobj-format.md`](docs/czobj-format.md) — binary format specification
- [`docs/c-api.md`](docs/c-api.md) — C API reference
- [`docs/python-usage.md`](docs/python-usage.md) — Python `czmesh_io` guide
- [`docs/build-guide.md`](docs/build-guide.md) — platform build guide

---

## License

MIT — see [LICENSE](LICENSE).
