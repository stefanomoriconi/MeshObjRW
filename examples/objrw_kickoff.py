#!/usr/bin/env python3
"""objrw kick-off — the shortest possible read / write example.

This single, self-contained script shows the entire public API::

    import objrw_io
    mesh_in  = objrw_io.readOBJ("path/to/meshFile.obj")
    mesh_out = objrw_io.writeOBJ(mesh_in, "path/to/out.obj", flagBinary=True)

If you do not pass an input file, it builds a small unit cube in memory so
you can run it with no external data at all.  There is nothing to install
(no numpy, no ``pip install``) — you only need the C library to be built
once (see ``docs/python-usage.md`` for the build commands).

Run it
------
    python examples/objrw_kickoff.py                  # build a cube, round-trip it
    python examples/objrw_kickoff.py myMesh.obj       # start from an existing .obj

Expected output ends with ``Done.`` and verifies the binary round-trip.
"""

from __future__ import annotations

import os
import sys
import tempfile

# --- make `import objrw_io` work from any current working directory ---------
_here = os.path.dirname(os.path.abspath(__file__))   # <repo>/examples
_repo = os.path.dirname(_here)                        # <repo>
sys.path.insert(0, os.path.join(_repo, "python"))

import objrw_io  # noqa: E402


def _build_unit_cube() -> "objrw_io.Mesh":
    """Create an 8-vertex, 12-triangle unit cube [0, 1]^3 in memory.

    The winding matches the C reference cube, so ``stats()`` reports
    surface area == 6 and |signed volume| == 1 exactly.
    """
    m = objrw_io.Mesh()
    m.object_name = "objrw_kickoff_cube"

    verts = [
        (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
        (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1),
    ]
    for v in verts:
        m.add_vertex(*v)

    # 6 outward-wound quads, fan-triangulated (a, b, c), (a, c, d).
    quads = [
        (0, 1, 2, 3),  # bottom (z=0)
        (5, 4, 7, 6),  # top    (z=1)
        (4, 0, 3, 7),  # -x
        (1, 5, 6, 2),  # +x
        (3, 2, 6, 7),  # +y
        (4, 5, 1, 0),  # -y
    ]
    for a, b, c, d in quads:
        m.add_triangle(a, b, c)
        m.add_triangle(a, c, d)
    return m


def main(argv: list) -> int:
    tmp = tempfile.gettempdir()
    src_arg = argv[0] if argv else None

    # ------------------------------------------------------------------
    # Step 1 — get a source mesh on disk.
    # ------------------------------------------------------------------
    if src_arg is None:
        cube = _build_unit_cube()
        src = os.path.join(tmp, "objrw_kickoff_in.obj")
        cube.write_ascii(src)
        cube.close()
        print(f"[1] built a unit cube in memory -> {src}")
    else:
        src = src_arg
        print(f"[1] using input file: {src}")

    # ------------------------------------------------------------------
    # Step 2 — READ.  Encoding is auto-detected (ASCII .obj or .objrw).
    # ------------------------------------------------------------------
    try:
        mesh_in = objrw_io.readOBJ(src)
    except objrw_io.OBJRWError as e:
        print(f"ERROR: could not read {src!r} (code={e.code}): {e}")
        return 1

    mn, mx = mesh_in.bounding_box()
    area, vol = mesh_in.stats()
    print("[2] mesh_in = objrw_io.readOBJ(path)")
    print(f"      vertices      : {mesh_in.num_vertices}")
    print(f"      triangles     : {mesh_in.num_triangles}")
    print(f"      object_name   : {mesh_in.object_name!r}")
    print(f"      bounding box  : ({mn[0]:g},{mn[1]:g},{mn[2]:g}) -> "
          f"({mx[0]:g},{mx[1]:g},{mx[2]:g})")
    print(f"      area={area:.4f}  |volume|={abs(vol):.4f}")

    # ------------------------------------------------------------------
    # Step 3 — WRITE.  ASCII by default, compact binary when flagBinary=True.
    # ------------------------------------------------------------------
    out_ascii = os.path.join(tmp, "objrw_kickoff_out.obj")
    out_bin = os.path.join(tmp, "objrw_kickoff_out.objrw")
    objrw_io.writeOBJ(mesh_in, out_ascii, flagBinary=False)
    objrw_io.writeOBJ(mesh_in, out_bin, flagBinary=True)
    print("[3] objrw_io.writeOBJ(mesh, path, flagBinary=...)")
    print(f"      ASCII  ({os.path.getsize(out_ascii):6d} bytes): {out_ascii}")
    print(f"      binary ({os.path.getsize(out_bin):6d} bytes): {out_bin}")

    # ------------------------------------------------------------------
    # Step 4 — verify the binary round-trips (readOBJ auto-detects it).
    # ------------------------------------------------------------------
    back = objrw_io.readOBJ(out_bin)
    assert back.num_vertices == mesh_in.num_vertices
    assert back.num_triangles == mesh_in.num_triangles
    print(f"[4] binary round-trip verified: {back.num_triangles} triangles OK")

    mesh_in.close()
    back.close()
    print("Done. The lines above are exactly the public API.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
