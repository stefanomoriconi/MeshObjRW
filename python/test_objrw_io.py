"""End-to-end tests for the objrw_io Python wrapper.

Builds a unit cube in Python, writes it as both ASCII OBJ and binary .objrw,
reads both back, and asserts geometry / derived-quantity equality, object-name
round-trip, bounding box, area/volume and recomputed normals.

Run directly (no pytest required):

    python python/test_objrw_io.py
"""

from __future__ import annotations

import os
import sys
import tempfile

# Make `import objrw_io` work regardless of the current directory.
_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _here)

import objrw_io  # noqa: E402
from objrw_io import OBJRWError  # noqa: E402


def _approx(a: float, b: float, tol: float = 1e-5) -> bool:
    return abs(a - b) <= tol


def _cube(m: "objrw_io.Mesh") -> None:
    """Populate a unit cube [0,1]^3 with 8 vertices and 12 triangles."""
    verts = [
        (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
        (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1),
    ]
    for v in verts:
        m.add_vertex(*v)

    # 6 outward-wound quads, fan-triangulated (a,b,c),(a,c,d) - matches the
    # C reference cube so |volume| == 1 and area == 6 exactly.
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


def test_library_loads() -> None:
    p = objrw_io.library_path()
    print(f"[ok] library: {p}")
    print(f"[ok] version: {objrw_io.version()}")
    print(f"[ok] build  : {objrw_io.build_info()}")


def test_build_and_derived() -> None:
    m = objrw_io.Mesh()
    _cube(m)
    m.object_name = "python_cube"

    assert m.num_vertices == 8, m.num_vertices
    assert m.num_triangles == 12, m.num_triangles
    assert m.num_texcoords == 0
    assert m.num_normals == 0

    mn, mx = m.bounding_box()
    assert _approx(mn[0], 0.0) and _approx(mx[0], 1.0), (mn, mx)
    assert _approx(mn[1], 0.0) and _approx(mx[1], 1.0), (mn, mx)
    assert _approx(mn[2], 0.0) and _approx(mx[2], 1.0), (mn, mx)

    area, vol = m.stats()
    assert _approx(area, 6.0), area
    assert _approx(abs(vol), 1.0), vol
    print(f"[ok] cube: area={area:.4f} vol={vol:.4f}")

    m.compute_normals()
    assert m.num_normals == 8, m.num_normals
    print(f"[ok] normals computed: {m.num_normals}")
    m.close()


def test_roundtrip(tmpdir: str) -> None:
    src = os.path.join(tmpdir, "cube.obj")
    bin_path = os.path.join(tmpdir, "cube.objrw")

    # Build reference mesh, save both encodings.
    ref = objrw_io.Mesh()
    _cube(ref)
    ref.object_name = "roundtrip_cube"
    ref.write_ascii(src)
    ref.write_binary(bin_path)
    ref.close()

    for path in (src, bin_path):
        m = objrw_io.read(path)  # auto-detect
        assert m.num_vertices == 8, (path, m.num_vertices)
        assert m.num_triangles == 12, (path, m.num_triangles)
        assert m.object_name == "roundtrip_cube", (path, m.object_name)
        mn, mx = m.bounding_box()
        assert _approx(mn[0], 0.0) and _approx(mx[2], 1.0), (path, mn, mx)
        area, vol = m.stats()
        assert _approx(area, 6.0), (path, area)
        assert _approx(abs(vol), 1.0), (path, vol)
        m.close()
        print(f"[ok] roundtrip {os.path.basename(path)}")


def test_explicit_readers(tmpdir: str) -> None:
    src = os.path.join(tmpdir, "r.obj")
    bin_path = os.path.join(tmpdir, "r.objrw")
    ref = objrw_io.Mesh()
    _cube(ref)
    ref.object_name = "explicit"
    ref.write_ascii(src)
    ref.write_binary(bin_path)
    ref.close()

    a = objrw_io.read_ascii(src)
    assert a.num_triangles == 12
    a.close()

    b = objrw_io.read_binary(bin_path)
    assert b.num_triangles == 12
    assert b.object_name == "explicit"
    b.close()
    print("[ok] explicit read_ascii / read_binary")


def test_error_on_missing_file(tmpdir: str) -> None:
    missing = os.path.join(tmpdir, "does_not_exist.obj")
    try:
        objrw_io.read(missing)
    except OBJRWError as e:
        print(f"[ok] missing file raises OBJRWError (code={e.code})")
        return
    raise AssertionError("expected OBJRWError for missing file")


def main() -> int:
    print("objrw_io end-to-end tests")
    print("=" * 40)
    test_library_loads()

    with tempfile.TemporaryDirectory() as tmpdir:
        test_build_and_derived()
        test_roundtrip(tmpdir)
        test_explicit_readers(tmpdir)
        test_error_on_missing_file(tmpdir)

    print("=" * 40)
    print("ALL PYTHON TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
