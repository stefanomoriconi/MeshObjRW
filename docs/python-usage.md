# `objrw_io` Python usage

`objrw_io` is a thin, **dependency-free** `ctypes` wrapper around the
`objrw` shared library. The primary, self-contained API is two functions —
`objrw_io.readOBJ(path)` to import a mesh and `objrw_io.writeOBJ(mesh, path,
flagBinary=...)` to export it — plus a `Mesh` object and lower-level
`read` / `write` helpers. Only the standard library is required
(`ctypes`, `os`, `pathlib`).

> **No numpy required.** Vertices are accessed as plain Python lists of
> `(x, y, z)` tuples. For very large meshes you can still pass the raw
> arrays through `ctypes` if you want, but the API is intentionally simple.

## Primary API: `readOBJ` / `writeOBJ`

A single `import objrw_io` is all that is needed to read and write a
triangular OBJ mesh:

```python
import objrw_io

# Import — encoding is auto-detected (ASCII .obj or binary .objrw).
mesh_in = objrw_io.readOBJ("path/to/meshFile.obj")

# Export — ASCII OBJ by default, compact binary when flagBinary=True.
mesh_out = objrw_io.writeOBJ(mesh_in, "path/to/output/meshFile.obj",
                             flagBinary=False)   # ASCII
# mesh_out = objrw_io.writeOBJ(mesh_in, "out.objrw", flagBinary=True)
```

Both functions are self-contained and validate their arguments:

| Function | Signature | Notes |
|----------|-----------|-------|
| `readOBJ` | `objrw_io.readOBJ(path) -> Mesh` | Auto-detects ASCII vs binary. Returns a new `Mesh`. |
| `writeOBJ` | `objrw_io.writeOBJ(mesh, path, flagBinary=False) -> Mesh` | `flagBinary=False` → ASCII `.obj`; `True` → binary `.objrw`. Returns the same `Mesh` (chainable). |

**Error handling**

* `TypeError` — raised immediately if `path` is not a non-empty string,
  `mesh` is not a `Mesh`, or `flagBinary` is not a boolean.
* `objrw_io.OBJRWError` — raised on I/O or parse failure. It carries a
  numeric `.code` and a human-readable message (e.g. `code=3` for a file
  that could not be opened).

## Installing / locating the library

The loader finds the shared library in this order:

1. **`OBJRW_LIBRARY_PATH`** — explicit override (a file path, or a directory
   containing the library).
2. **Relative search** — common locations next to the package, e.g.
   `build/`, `build/Release`, `build/Debug`, `dist/`.
3. **System search** — `ctypes.util.find_library("objrw")` (honours
   `PATH` / `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`).

Typical setups:

```powershell
# Windows (PowerShell) — after building with CMake
$env:PATH = "$PWD\build\Release;" + $env:PATH
# or point directly:
$env:OBJRW_LIBRARY_PATH = "$PWD\build\Release\objrw.dll"
```

```sh
# macOS / Linux
export PATH="$PWD/build:$PATH"
# or:
export OBJRW_LIBRARY_PATH="$PWD/build/libobjrw.dylib"   # .so on Linux
```

## Lower-level API (explicit encoding)

For explicit control over the encoding, the module-level `read` / `read_ascii`
/ `read_binary` / `write_ascii` / `write_binary` helpers and the `Mesh` object
are available. `read(path)` auto-detects the encoding just like `readOBJ`.

```python
import objrw_io

# read: encoding is auto-detected (ASCII .obj or binary .objrw)
m = objrw_io.read("model.obj")

print(m.num_vertices, m.num_triangles)
print(m.object_name)

print(m.bounding_box())          # ((minx,miny,minz), (maxx,maxy,maxz))
print(m.stats())                 # (area, signed_volume)

m.compute_normals()              # area-weighted per-vertex normals

# write: pick the encoding explicitly
m.write_ascii("out.obj")
m.write_binary("out.objrw")
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
| `write_binary(path)` | Save as binary `objrw`. |

### Building a mesh from scratch

```python
m = objrw_io.Mesh()
m.object_name = "my_mesh"

i0 = m.add_vertex(0.0, 0.0, 0.0)
i1 = m.add_vertex(1.0, 0.0, 0.0)
i2 = m.add_vertex(1.0, 1.0, 0.0)
m.add_triangle(i0, i1, i2)

m.write_ascii("built.obj")
```

### Error handling

All I/O and parse failures raise `objrw_io.OBJRWError`, which carries a
numeric `.code` (matching the C status enum) and a `.message` (from
`objrw_last_error()`). The high-level `readOBJ` / `writeOBJ` helpers also
validate their arguments up front and raise a plain `TypeError` if `path`
is not a non-empty string, `mesh` is not a `Mesh`, or `flagBinary` is not a
boolean.

```python
from objrw_io import OBJRWError

try:
    m = objrw_io.read("missing.obj")
except OBJRWError as e:
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
with objrw_io.read("model.obj") as m:
    print(m.stats())
# handle already released here
```

## Running the tests

```sh
python python/test_objrw_io.py
```

The suite builds a reference unit cube in-process, writes it to both
encodings, and verifies round-trip fidelity (counts, bounding box, area,
|volume|, name preservation). It also exercises the high-level
`readOBJ` / `writeOBJ` API (ASCII and binary round-trip, plus `TypeError`
argument validation). It ends with `ALL PYTHON TESTS PASSED` on success.

## Troubleshooting

- **`OSError: objrw shared library not found`** — set `OBJRW_LIBRARY_PATH`
  or add the build directory to `PATH`.
- **`ctypes.ArgumentError ... wrong type`** — you may be running an older
  build of the library; rebuild after header changes.
- **Wrong `code` value** — check `e.message`; most are file/parse issues in
  the input OBJ.
