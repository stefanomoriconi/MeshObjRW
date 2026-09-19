# `czmesh_io` Python usage

`czmesh_io` is a thin, **dependency-free** `ctypes` wrapper around the
`czmesh` shared library. It exposes a `Mesh` object plus module-level
`read` / `write` helpers, and requires only the standard library
(`ctypes`, `os`, `pathlib`).

> **No numpy required.** Vertices are accessed as plain Python lists of
> `(x, y, z)` tuples. For very large meshes you can still pass the raw
> arrays through `ctypes` if you want, but the API is intentionally simple.

## Installing / locating the library

The loader finds the shared library in this order:

1. **`CZMESH_LIBRARY_PATH`** — explicit override (a file path, or a directory
   containing the library).
2. **Relative search** — common locations next to the package, e.g.
   `build/`, `build/Release`, `build/Debug`, `dist/`.
3. **System search** — `ctypes.util.find_library("czmesh")` (honours
   `PATH` / `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`).

Typical setups:

```powershell
# Windows (PowerShell) — after building with CMake
$env:PATH = "$PWD\build\Release;" + $env:PATH
# or point directly:
$env:CZMESH_LIBRARY_PATH = "$PWD\build\Release\czmesh.dll"
```

```sh
# macOS / Linux
export PATH="$PWD/build:$PATH"
# or:
export CZMESH_LIBRARY_PATH="$PWD/build/libczmesh.dylib"   # .so on Linux
```

## Quick start

```python
import czmesh_io

# read: encoding is auto-detected (ASCII .obj or binary .czobj)
m = czmesh_io.read("model.obj")

print(m.num_vertices, m.num_triangles)
print(m.object_name)

print(m.bounding_box())          # ((minx,miny,minz), (maxx,maxy,maxz))
print(m.stats())                 # (area, signed_volume)

m.compute_normals()              # area-weighted per-vertex normals

# write: pick the encoding explicitly
m.write_ascii("out.obj")
m.write_binary("out.czobj")
```

### `Mesh` reference

| Member | Description |
|--------|-------------|
| `num_vertices` | Number of position vectors (read-only). |
| `num_texcoords` | Number of texture-coordinate pairs (read-only). |
| `num_normals` | Number of normals (read-only). |
| `num_triangles` | Number of triangles (read-only). |
| `object_name` | Get/set the object name (str or `None`). |
| `positions` | List of `(x, y, z)` tuples. |
| `texcoords` | List of `(u, v)` tuples. |
| `normals` | List of `(x, y, z)` tuples. |
| `triangles` | List of `(pa, pb, pc, pt, pn)` tuples per corner. |
| `add_vertex(x, y, z)` | Append a vertex; returns its 0-based index. |
| `add_texcoord(u, v)` | Append a texture coordinate; returns its index. |
| `add_normal(x, y, z)` | Append a normal; returns its index. |
| `add_triangle(pa, pb, pc, pt=-1, pn=-1)` | Append a triangle. `pt`/`pn` default to `-1` (absent). |
| `bounding_box()` | `(min, max)` 3-tuples over all vertices. |
| `compute_normals()` | Recompute area-weighted per-vertex normals. |
| `stats()` | `(area, signed_volume)`. |
| `write_ascii(path)` | Save as ASCII OBJ. |
| `write_binary(path)` | Save as binary `czobj`. |

### Building a mesh from scratch

```python
m = czmesh_io.Mesh()
m.object_name = "my_mesh"

i0 = m.add_vertex(0.0, 0.0, 0.0)
i1 = m.add_vertex(1.0, 0.0, 0.0)
i2 = m.add_vertex(1.0, 1.0, 0.0)
m.add_triangle(i0, i1, i2)

m.write_ascii("built.obj")
```

### Error handling

All failures raise `czmesh_io.CZMeshError`, which carries a numeric `.code`
(matching the C status enum) and a `.message` (from `czmesh_last_error()`).

```python
from czmesh_io import CZMeshError

try:
    m = czmesh_io.read("missing.obj")
except CZMeshError as e:
    print(e.code, e.message)     # e.g. 3 "could not open file for reading"
```

| Code | Meaning |
|------|---------|
| 0 | OK (never raised) |
| 1 | Null pointer |
| 2 | Memory allocation failure |
| 3 | File could not be opened |
| 4 | I/O write error |
| 5 | Parse error |
| 6 | Feature unsupported by this build |
| 7 | Inconsistent / out-of-range data |
| 8 | Mesh empty |
| 99 | Internal error |

### Context manager

`Mesh` supports the context-manager protocol; the underlying handle is
released on `__exit__` and also by the garbage collector.

```python
with czmesh_io.read("model.obj") as m:
    print(m.stats())
# handle already released here
```

## Running the tests

```sh
python python/test_czmesh_io.py
```

The suite builds a reference unit cube in-process, writes it to both
encodings, and verifies round-trip fidelity (counts, bounding box, area,
|volume|, name preservation). It ends with `ALL PYTHON TESTS PASSED` on
success.

## Troubleshooting

- **`OSError: czmesh shared library not found`** — set `CZMESH_LIBRARY_PATH`
  or add the build directory to `PATH`.
- **`ctypes.ArgumentError ... wrong type`** — you may be running an older
  build of the library; rebuild after header changes.
- **Wrong `code` value** — check `e.message`; most are file/parse issues in
  the input OBJ.
