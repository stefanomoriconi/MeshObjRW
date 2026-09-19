# `czobj` binary format specification

`czobj` is the compact, documented, versioned binary mesh format used by
`czmesh`. It is designed to be:

- **Fast** — one contiguous little-endian byte stream, no parsing.
- **Stable & versioned** — a `format_ver` field lets readers reject
  incompatible files cleanly.
- **Interoperable** — plain C ABI, so any language that can read a byte
  buffer can produce/consume the format.

## Encoding rules

- All integers and floats are **little-endian**.
- All fields are **fixed-size**; there are no alignment padding rules beyond
  the natural field sizes below.
- Floating-point values are IEEE-754 **single precision** (`float`, 32-bit).
- The object name (if present) is raw **UTF-8 bytes**, not NUL-terminated in
  the stream.

## File layout (version 1)

| # | Type | Size (bytes) | Field | Description |
|---|------|--------------|-------|-------------|
| 1 | `byte[8]` | 8 | `magic` | `"CZOBJ"` followed by three `0x00` bytes: `43 5A 4F 42 4A 00 00 00` |
| 2 | `u32` | 4 | `format_ver` | Format version. Readers require `== 1`. |
| 3 | `u64` | 8 | `num_vertices` | Number of position vectors. |
| 4 | `u64` | 8 | `num_texcoords` | Number of texture-coordinate pairs (0 if none). |
| 5 | `u64` | 8 | `num_normals` | Number of normal vectors (0 if none). |
| 6 | `u64` | 8 | `num_triangles` | Number of triangular faces. |
| 7 | `u64` | 8 | `name_len` | Byte length of the object name (0 when absent). |
| 8 | `byte[]` | `name_len` | `name` | UTF-8 object name. **Present only when `name_len > 0`.** |
| 9 | `float[]` | `3 · num_vertices` | `positions` | Vertices as interleaved `(x, y, z)`. |
| 10 | `float[]` | `2 · num_texcoords` | `texcoords` | Texture coordinates as interleaved `(u, v)`. |
| 11 | `float[]` | `3 · num_normals` | `normals` | Normals as interleaved `(x, y, z)`. |
| 12 | `i32[]` | `3 · num_triangles` | `tri_pos` | Per-corner position index (0-based). |
| 13 | `i32[]` | `3 · num_triangles` | `tri_tex` | Per-corner texcoord index, or `-1` when absent. |
| 14 | `i32[]` | `3 · num_triangles` | `tri_nor` | Per-corner normal index, or `-1` when absent. |

### Fixed-size integers

- `u32` — unsigned 32-bit little-endian.
- `u64` — unsigned 64-bit little-endian.
- `i32` — signed 32-bit little-endian (two's complement).

### Total size

For a mesh with no name:

```
total = 8 (magic)
      + 4 (format_ver)
      + 8·4 (counts)          = 32
      + 8 (name_len)          = 8
      + 3·nv + 2·nvt + 3·nvn + 3·nt·3   (data arrays)
```

Example: a unit cube (8 vertices, 12 triangles, no texcoords/normals/name)
is exactly:

```
 8  (magic)
+ 4  (format_ver)
+ 32 (4 × u64 counts)
+ 8  (name_len)
+ 96 (positions: 8 × 3 floats)
+ 0  (texcoords)
+ 0  (normals)
+ 144 (tri_pos: 12 × 3 × i32)
+ 144 (tri_tex: 12 × 3 × i32, all -1)
+ 144 (tri_nor: 12 × 3 × i32, all -1)
= 580 bytes
```

Use `czmesh_cli info <file>` or a hex dump to verify a concrete file.

## Auto-detection

`czmesh_read()` sniffs the first 8 bytes:

- If they equal `"CZOBJ\0\0\0"` → binary `czobj` reader.
- Otherwise → ASCII Wavefront OBJ reader.

This is why a single `read` call handles both encodings.

## Reader invariants (defence in depth)

A conforming reader **must** enforce all of:

1. File length ≥ the header size before reading the header.
2. `magic` == `43 5A 4F 42 4A 00 00 00`.
3. `format_ver` == `1`.
4. Every array read is bounds-checked against the remaining buffer
   (a `Cursor` that fails a `need(k)` check aborts cleanly).
5. No `i32` index in `tri_pos` / `tri_tex` / `tri_nor` is out of range of its
   array (`>=` its count → `CZMESH_ERR_INCONSISTENT`).

`czmesh`'s reader implements all five and returns a specific status code plus
a message available from `czmesh_last_error()`.

## Versioning policy

- **Additive changes** (new trailing fields) must increment `format_ver`;
  old readers reject the new file rather than misparse it.
- **Breaking changes** to existing field meanings must increment
  `format_ver` and be documented here.
- The library currently reads and writes only version `1`.
