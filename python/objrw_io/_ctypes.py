"""ctypes bindings for the objrw C-ABI shared library.

This module declares the prototypes for every public function in
``include/objrw.h`` and provides a thin, error-checked wrapper around them.
It is an internal module; prefer using :class:`objrw_io.Mesh` directly.
"""

from __future__ import annotations

import ctypes
from typing import Optional, Tuple

from ._library import _load
from ._errors import OBJRWError

# ---------------------------------------------------------------------------
# Constants (mirror objrw.h)
# ---------------------------------------------------------------------------
OBJRW_OK = 0
OBJRW_NONE = -1

# objrw_status enum values
OBJRW_ERR_NULL_POINTER = 1
OBJRW_ERR_MEMORY = 2
OBJRW_ERR_IO_OPEN = 3
OBJRW_ERR_IO_WRITE = 4
OBJRW_ERR_PARSE = 5
OBJRW_ERR_UNSUPPORTED = 6
OBJRW_ERR_INCONSISTENT = 7
OBJRW_ERR_EMPTY = 8
OBJRW_ERR_INTERNAL = 99


def _check(rc: int) -> None:
    """Raise OBJRWError when rc != OBJRW_OK."""
    if rc != OBJRW_OK:
        lib = _load()
        lib.objrw_last_error.restype = ctypes.c_char_p
        msg = lib.objrw_last_error().decode("utf-8", "replace")
        raise OBJRWError(rc, msg)


class OBJRWLibrary:
    """Configured ctypes handle with prototypes for the objrw C API.

    Create one instance and keep it around; all methods wrap a single C call
    and check the return code.
    """

    def __init__(self) -> None:
        self._lib = _load()
        self._declare()

    # -- prototype declaration --------------------------------------------
    def _declare(self) -> None:
        lib = self._lib

        # Diagnostics (strings)
        lib.objrw_version.restype = ctypes.c_char_p
        lib.objrw_version.argtypes = []
        lib.objrw_build_info.restype = ctypes.c_char_p
        lib.objrw_build_info.argtypes = []
        lib.objrw_last_error.restype = ctypes.c_char_p
        lib.objrw_last_error.argtypes = []

        # Lifecycle
        lib.objrw_mesh_create.restype = ctypes.c_void_p
        lib.objrw_mesh_create.argtypes = []
        lib.objrw_mesh_free.restype = None
        lib.objrw_mesh_free.argtypes = [ctypes.c_void_p]

        # Append helpers
        _flt = ctypes.c_float
        _i32 = ctypes.c_int32
        _u64 = ctypes.c_uint64

        lib.objrw_mesh_reserve.restype = ctypes.c_int
        lib.objrw_mesh_reserve.argtypes = [
            ctypes.c_void_p, _u64, _u64, _u64, _u64
        ]

        lib.objrw_mesh_push_vertex.restype = ctypes.c_int
        lib.objrw_mesh_push_vertex.argtypes = [ctypes.c_void_p, _flt, _flt, _flt]

        lib.objrw_mesh_push_texcoord.restype = ctypes.c_int
        lib.objrw_mesh_push_texcoord.argtypes = [ctypes.c_void_p, _flt, _flt]

        lib.objrw_mesh_push_normal.restype = ctypes.c_int
        lib.objrw_mesh_push_normal.argtypes = [ctypes.c_void_p, _flt, _flt, _flt]

        lib.objrw_mesh_push_triangle.restype = ctypes.c_int
        lib.objrw_mesh_push_triangle.argtypes = [
            ctypes.c_void_p,
            _i32, _i32, _i32,
            _i32, _i32, _i32,
            _i32, _i32, _i32,
        ]

        lib.objrw_mesh_set_name.restype = ctypes.c_int
        lib.objrw_mesh_set_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

        # Read / write
        for name in ("objrw_read", "objrw_read_ascii", "objrw_read_binary"):
            getattr(lib, name).restype = ctypes.c_int
            getattr(lib, name).argtypes = [ctypes.c_char_p, ctypes.c_void_p]
        for name in ("objrw_write", "objrw_write_ascii", "objrw_write_binary"):
            getattr(lib, name).restype = ctypes.c_int
            getattr(lib, name).argtypes = [ctypes.c_char_p, ctypes.c_void_p]

        # Geometry helpers
        _f32arr = _flt * 3
        lib.objrw_mesh_bounding_box.restype = ctypes.c_int
        lib.objrw_mesh_bounding_box.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(_f32arr),  # actually a C array; use POINTER
            ctypes.POINTER(_f32arr),
        ]
        # Correct argtypes: C expects float[3]; use POINTER(c_float)
        lib.objrw_mesh_bounding_box.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_float),
        ]

        lib.objrw_mesh_compute_normals.restype = ctypes.c_int
        lib.objrw_mesh_compute_normals.argtypes = [ctypes.c_void_p]

        lib.objrw_mesh_stats.restype = ctypes.c_int
        lib.objrw_mesh_stats.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]

    # -- helpers -----------------------------------------------------------
    @staticmethod
    def _cstr(path: str) -> bytes:
        if isinstance(path, str):
            return path.encode("utf-8")
        return bytes(path)

    # -- public wrappers ---------------------------------------------------
    def create(self):
        ptr = self._lib.objrw_mesh_create()
        if not ptr:
            raise OBJRWError(OBJRW_ERR_MEMORY, "objrw_mesh_create returned NULL")
        return ptr

    def free(self, ptr) -> None:
        if ptr:
            self._lib.objrw_mesh_free(ptr)

    def reserve(self, ptr, nv, nvt, nvn, nt) -> None:
        _check(self._lib.objrw_mesh_reserve(
            ptr, ctypes.c_uint64(nv), ctypes.c_uint64(nvt),
            ctypes.c_uint64(nvn), ctypes.c_uint64(nt)))

    def push_vertex(self, ptr, x, y, z) -> None:
        _check(self._lib.objrw_mesh_push_vertex(
            ptr, ctypes.c_float(x), ctypes.c_float(y), ctypes.c_float(z)))

    def push_texcoord(self, ptr, u, v) -> None:
        _check(self._lib.objrw_mesh_push_texcoord(
            ptr, ctypes.c_float(u), ctypes.c_float(v)))

    def push_normal(self, ptr, x, y, z) -> None:
        _check(self._lib.objrw_mesh_push_normal(
            ptr, ctypes.c_float(x), ctypes.c_float(y), ctypes.c_float(z)))

    def push_triangle(self, ptr, p0, p1, p2, t0, t1, t2, n0, n1, n2) -> None:
        _check(self._lib.objrw_mesh_push_triangle(
            ptr,
            ctypes.c_int32(p0), ctypes.c_int32(p1), ctypes.c_int32(p2),
            ctypes.c_int32(t0), ctypes.c_int32(t1), ctypes.c_int32(t2),
            ctypes.c_int32(n0), ctypes.c_int32(n1), ctypes.c_int32(n2)))

    def read(self, path, ptr) -> None:
        _check(self._lib.objrw_read(self._cstr(path), ptr))

    def read_ascii(self, path, ptr) -> None:
        _check(self._lib.objrw_read_ascii(self._cstr(path), ptr))

    def read_binary(self, path, ptr) -> None:
        _check(self._lib.objrw_read_binary(self._cstr(path), ptr))

    def write(self, path, ptr) -> None:
        _check(self._lib.objrw_write(self._cstr(path), ptr))

    def write_ascii(self, path, ptr) -> None:
        _check(self._lib.objrw_write_ascii(self._cstr(path), ptr))

    def write_binary(self, path, ptr) -> None:
        _check(self._lib.objrw_write_binary(self._cstr(path), ptr))

    def bounding_box(self, ptr) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
        mn = (ctypes.c_float * 3)()
        mx = (ctypes.c_float * 3)()
        _check(self._lib.objrw_mesh_bounding_box(
            ptr, ctypes.cast(mn, ctypes.POINTER(ctypes.c_float)),
            ctypes.cast(mx, ctypes.POINTER(ctypes.c_float))))
        return (tuple(mn), tuple(mx))

    def compute_normals(self, ptr) -> None:
        _check(self._lib.objrw_mesh_compute_normals(ptr))

    def stats(self, ptr) -> Tuple[float, float]:
        area = ctypes.c_double()
        vol = ctypes.c_double()
        _check(self._lib.objrw_mesh_stats(
            ptr, ctypes.byref(area), ctypes.byref(vol)))
        return (area.value, vol.value)

    # -- mesh introspection (reads struct fields directly) -----------------
    def num_vertices(self, ptr) -> int:
        return ctypes.c_uint64.from_address(ptr + _OFF_NV).value

    def num_texcoords(self, ptr) -> int:
        return ctypes.c_uint64.from_address(ptr + _OFF_NVT).value

    def num_normals(self, ptr) -> int:
        return ctypes.c_uint64.from_address(ptr + _OFF_NVN).value

    def num_triangles(self, ptr) -> int:
        return ctypes.c_uint64.from_address(ptr + _OFF_NT).value

    def object_name(self, ptr) -> Optional[str]:
        if not ctypes.c_int32.from_address(ptr + _OFF_HAS_NAME).value:
            return None
        addr = ctypes.c_char_p.from_address(ptr + _OFF_NAME).value
        return addr.decode("utf-8", "replace") if addr else None

    def set_object_name(self, ptr, name: Optional[str]) -> None:
        """Set (or clear) the object name on the underlying C mesh.

        The C library copies the string into its own buffer (allocated with
        the same allocator the library uses elsewhere), so the mesh stays
        self-contained and is released cleanly by ``objrw_mesh_free``.
        Pass ``None`` to clear the name.
        """
        arg = ctypes.c_char_p(name.encode("utf-8")) if name is not None else None
        _check(self._lib.objrw_mesh_set_name(ptr, arg))

    def positions(self, ptr) -> list:
        return _read_float_array(ptr, _OFF_POS, self.num_vertices(ptr) * 3)

    def texcoords(self, ptr) -> list:
        n = self.num_texcoords(ptr)
        if n == 0:
            return []
        return _read_float_array(ptr, _OFF_TEX, n * 2)

    def normals(self, ptr) -> list:
        n = self.num_normals(ptr)
        if n == 0:
            return []
        return _read_float_array(ptr, _OFF_NOR, n * 3)

    def tri_pos(self, ptr) -> list:
        n = self.num_triangles(ptr)
        if n == 0:
            return []
        return _read_i32_array(ptr, _OFF_TP, n * 3)

    def tri_tex(self, ptr) -> list:
        n = self.num_triangles(ptr)
        if n == 0:
            return []
        return _read_i32_array(ptr, _OFF_TT, n * 3)

    def tri_nor(self, ptr) -> list:
        n = self.num_triangles(ptr)
        if n == 0:
            return []
        return _read_i32_array(ptr, _OFF_TN, n * 3)


def _read_float_array(mesh_ptr, field_offset: int, count: int) -> list:
    addr = ctypes.c_void_p.from_address(mesh_ptr + field_offset).value
    if not addr:
        return []
    arr = (ctypes.c_float * count).from_address(addr)
    return list(arr)


def _read_i32_array(mesh_ptr, field_offset: int, count: int) -> list:
    addr = ctypes.c_void_p.from_address(mesh_ptr + field_offset).value
    if not addr:
        return []
    arr = (ctypes.c_int32 * count).from_address(addr)
    return list(arr)


# ---------------------------------------------------------------------------
# objrw_mesh_t field offsets (computed at import time on a dummy struct)
#
# Layout (from include/objrw.h):
#   float   *positions;   // offset 0
#   float   *texcoords;   // offset 8
#   float   *normals;     // offset 16
#   int32_t *tri_pos;     // offset 24
#   int32_t *tri_tex;     // offset 32
#   int32_t *tri_nor;     // offset 40
#   uint64_t num_vertices;    // offset 48
#   uint64_t num_texcoords;   // offset 56
#   uint64_t num_normals;     // offset 64
#   uint64_t num_triangles;   // offset 72
#   char    *object_name;     // offset 80
#   int32_t  has_object_name; // offset 88
#   (4 bytes padding to 96)
#
# On 64-bit platforms pointers are 8 bytes.  On 32-bit they are 4 bytes and
# the offsets differ.  We compute them dynamically using ctypes.Structure so
# the wrapper works on both.
# ---------------------------------------------------------------------------
class _OBJRWMeshStruct(ctypes.Structure):
    _fields_ = [
        ("positions",   ctypes.POINTER(ctypes.c_float)),
        ("texcoords",   ctypes.POINTER(ctypes.c_float)),
        ("normals",     ctypes.POINTER(ctypes.c_float)),
        ("tri_pos",     ctypes.POINTER(ctypes.c_int32)),
        ("tri_tex",     ctypes.POINTER(ctypes.c_int32)),
        ("tri_nor",     ctypes.POINTER(ctypes.c_int32)),
        ("num_vertices", ctypes.c_uint64),
        ("num_texcoords", ctypes.c_uint64),
        ("num_normals", ctypes.c_uint64),
        ("num_triangles", ctypes.c_uint64),
        ("object_name", ctypes.c_char_p),
        ("has_object_name", ctypes.c_int32),
    ]


# Offsets for 64-bit platforms (8-byte pointers / 8-byte uint64).  This is the
# only architecture we currently target; the C struct is fixed-order and
# these values match the layout documented above and in include/objrw.h.
_OFF_POS       = 0
_OFF_TEX       = 8
_OFF_NOR       = 16
_OFF_TP        = 24
_OFF_TT        = 32
_OFF_TN        = 40
_OFF_NV        = 48
_OFF_NVT       = 56
_OFF_NVN       = 64
_OFF_NT        = 72
_OFF_NAME      = 80
_OFF_HAS_NAME  = 88
