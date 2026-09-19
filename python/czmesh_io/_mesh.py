"""High-level Python object model for czmesh.

This module exposes :class:`Mesh` and the module-level :func:`read` /
:func:`read_ascii` / :func:`read_binary` / :func:`write_ascii` /
:func:`write_binary` helpers that most users will want.

A :class:`Mesh` is a thin, safe wrapper around a C ``czmesh_mesh_t*``.  It
owns the underlying memory: when the object is closed (or garbage collected)
the C mesh is released, so Python-side buffers never outlive it and there is
no cross-allocator mismatch.

All geometry is returned as plain Python sequences (no numpy dependency).
"""

from __future__ import annotations

from typing import List, Optional, Sequence, Tuple

from ._ctypes import CZMeshLibrary, CZMESH_NONE


class Mesh:
    """A triangular mesh backed by a C ``czmesh_mesh_t*``.

    Parameters
    ----------
    library:
        Optional :class:`czmesh_io._ctypes.CZMeshLibrary` instance to reuse.
        When omitted, a shared library handle is loaded (and cached).
    """

    def __init__(self, library: Optional[CZMeshLibrary] = None) -> None:
        self._lib = library or CZMeshLibrary()
        self._ptr = self._lib.create()
        self._closed = False

    # -- lifecycle ---------------------------------------------------------
    def close(self) -> None:
        """Release the underlying C mesh.  Safe to call multiple times."""
        if not self._closed:
            self._closed = True
            self._lib.free(self._ptr)
            self._ptr = None

    def __enter__(self) -> "Mesh":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:  # pragma: no cover - best effort cleanup
        try:
            self.close()
        except Exception:
            pass

    # -- counts ------------------------------------------------------------
    @property
    def num_vertices(self) -> int:
        return self._lib.num_vertices(self._ptr)

    @property
    def num_texcoords(self) -> int:
        return self._lib.num_texcoords(self._ptr)

    @property
    def num_normals(self) -> int:
        return self._lib.num_normals(self._ptr)

    @property
    def num_triangles(self) -> int:
        return self._lib.num_triangles(self._ptr)

    @property
    def object_name(self) -> Optional[str]:
        return self._lib.object_name(self._ptr)

    @object_name.setter
    def object_name(self, name: Optional[str]) -> None:
        self._lib.set_object_name(self._ptr, name)

    # -- geometry accessors (lists) ---------------------------------------
    @property
    def positions(self) -> List[Tuple[float, float, float]]:
        """Vertex positions as a list of ``(x, y, z)`` triples."""
        flat = self._lib.positions(self._ptr)
        return [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]

    @property
    def texcoords(self) -> List[Tuple[float, float]]:
        """Texture coordinates as a list of ``(u, v)`` pairs."""
        flat = self._lib.texcoords(self._ptr)
        return [tuple(flat[i:i + 2]) for i in range(0, len(flat), 2)]

    @property
    def normals(self) -> List[Tuple[float, float, float]]:
        """Vertex normals as a list of ``(x, y, z)`` triples."""
        flat = self._lib.normals(self._ptr)
        return [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]

    @property
    def triangles(self) -> List[Tuple[int, int, int, int, int, int, int, int, int]]:
        """Triangles as a list of 9-int tuples ``(p0..p2, t0..t2, n0..n2)``.

        A value of ``-1`` (``CZMESH_NONE``) means the corresponding
        texcoord/normal index is absent for that face.
        """
        p = self._lib.tri_pos(self._ptr)
        t = self._lib.tri_tex(self._ptr)
        n = self._lib.tri_nor(self._ptr)
        out = []
        for i in range(0, len(p), 3):
            out.append((
                p[i], p[i + 1], p[i + 2],
                t[i], t[i + 1], t[i + 2],
                n[i], n[i + 1], n[i + 2],
            ))
        return out

    # -- building blocks ---------------------------------------------------
    def add_vertex(self, x: float, y: float, z: float) -> int:
        """Append a vertex; returns its 0-based index."""
        idx = self.num_vertices
        self._lib.push_vertex(self._ptr, x, y, z)
        return idx

    def add_texcoord(self, u: float, v: float) -> int:
        """Append a texture coordinate; returns its 0-based index."""
        idx = self.num_texcoords
        self._lib.push_texcoord(self._ptr, u, v)
        return idx

    def add_normal(self, x: float, y: float, z: float) -> int:
        """Append a vertex normal; returns its 0-based index."""
        idx = self.num_normals
        self._lib.push_normal(self._ptr, x, y, z)
        return idx

    def add_triangle(self,
                     p0: int, p1: int, p2: int,
                     t0: int = CZMESH_NONE, t1: int = CZMESH_NONE, t2: int = CZMESH_NONE,
                     n0: int = CZMESH_NONE, n1: int = CZMESH_NONE, n2: int = CZMESH_NONE) -> int:
        """Append a triangle; returns its 0-based index.

        Absent texcoord/normal indices default to ``-1`` (``CZMESH_NONE``).
        """
        idx = self.num_triangles
        self._lib.push_triangle(self._ptr, p0, p1, p2, t0, t1, t2, n0, n1, n2)
        return idx

    # -- derived quantities ------------------------------------------------
    def bounding_box(self) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
        """Return ``(min_point, max_point)`` of all vertex positions."""
        return self._lib.bounding_box(self._ptr)

    def compute_normals(self) -> None:
        """(Re)compute per-vertex normals from face geometry."""
        self._lib.compute_normals(self._ptr)

    def stats(self) -> Tuple[float, float]:
        """Return ``(surface_area, signed_volume)`` of the closed mesh."""
        return self._lib.stats(self._ptr)

    # -- I/O ---------------------------------------------------------------
    def write(self, path: str) -> None:
        """Write as ASCII OBJ (the czmesh default for :func:`czmesh_write`)."""
        self._lib.write(path, self._ptr)

    def write_ascii(self, path: str) -> None:
        """Write the mesh as an ASCII Wavefront OBJ file."""
        self._lib.write_ascii(path, self._ptr)

    def write_binary(self, path: str) -> None:
        """Write the mesh as a ``.czobj`` binary file."""
        self._lib.write_binary(path, self._ptr)

    # -- misc --------------------------------------------------------------
    def to_dict(self) -> dict:
        """Return a JSON-friendly snapshot of the mesh contents."""
        return {
            "object_name": self.object_name,
            "num_vertices": self.num_vertices,
            "num_texcoords": self.num_texcoords,
            "num_normals": self.num_normals,
            "num_triangles": self.num_triangles,
            "positions": self.positions,
            "texcoords": self.texcoords,
            "normals": self.normals,
            "triangles": self.triangles,
        }

    def __repr__(self) -> str:  # pragma: no cover - diagnostic only
        name = self.object_name
        return (f"Mesh(name={name!r}, vertices={self.num_vertices}, "
                f"triangles={self.num_triangles})")


def _new_mesh(library: Optional[CZMeshLibrary] = None) -> Mesh:
    return Mesh(library)


def read(path: str, library: Optional[CZMeshLibrary] = None) -> Mesh:
    """Read a mesh, auto-detecting ASCII OBJ vs. binary ``.czobj``."""
    m = _new_mesh(library)
    try:
        m._lib.read(path, m._ptr)
    except Exception:
        m.close()
        raise
    return m


def read_ascii(path: str, library: Optional[CZMeshLibrary] = None) -> Mesh:
    """Read an ASCII Wavefront OBJ file explicitly."""
    m = _new_mesh(library)
    try:
        m._lib.read_ascii(path, m._ptr)
    except Exception:
        m.close()
        raise
    return m


def read_binary(path: str, library: Optional[CZMeshLibrary] = None) -> Mesh:
    """Read a binary ``.czobj`` file explicitly."""
    m = _new_mesh(library)
    try:
        m._lib.read_binary(path, m._ptr)
    except Exception:
        m.close()
        raise
    return m


def write_ascii(path: str, mesh: Mesh) -> None:
    """Write ``mesh`` as an ASCII Wavefront OBJ file."""
    if isinstance(mesh, Mesh):
        mesh.write_ascii(path)
    else:
        raise TypeError("write_ascii expects a czmesh_io.Mesh instance")


def write_binary(path: str, mesh: Mesh) -> None:
    """Write ``mesh`` as a binary ``.czobj`` file."""
    if isinstance(mesh, Mesh):
        mesh.write_binary(path)
    else:
        raise TypeError("write_binary expects a czmesh_io.Mesh instance")
