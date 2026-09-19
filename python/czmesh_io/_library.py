"""Locate and load the czmesh shared library across Windows / macOS / Linux.

Search order
------------
1. ``CZMESH_LIBRARY_PATH`` environment variable (explicit override).
2. Common relative locations near this package (``build/Release``,
   ``build/``, ``../build/Release`` etc.) - covers a source checkout where
   the library was built with CMake.
3. System search path via :func:`ctypes.util.find_library`.

The loaded handle is cached in a module-level so repeated imports are cheap.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import sys
from typing import List, Optional

# Candidate file names for the shared library, in priority order.
_LIB_NAMES = {
    "win32": ["czmesh.dll"],
    "darwin": ["libczmesh.dylib", "czmesh.dylib"],
    "linux": ["libczmesh.so"],
}
# Fallbacks with a version suffix (some toolchains emit these).
_LIB_NAMES_ALT = {
    "win32": ["czmesh.dll"],
    "darwin": ["libczmesh.1.dylib", "libczmesh.dylib"],
    "linux": ["libczmesh.so.1", "libczmesh.so"],
}


def _candidate_dirs() -> List[str]:
    """Build an ordered list of directories to search for the library."""
    here = os.path.dirname(os.path.abspath(__file__))
    pkg = os.path.dirname(here)                      # python/
    root = os.path.dirname(pkg)                      # repo root
    candidates = [
        os.path.dirname(here),                       # python/
        here,                                         # python/czmesh_io/
        os.path.join(root, "build"),
        os.path.join(root, "build", "Release"),
        os.path.join(root, "build", "Debug"),
        os.path.join(root, "build", "RelWithDebInfo"),
        os.path.join(root, "build", "MinSizeRel"),
        # MSVC multi-config: build/Release already covered above.
        os.path.join(root, "lib"),
        os.path.join(root, "libs"),
        os.path.join(pkg, "czmesh_io", "bin"),
    ]
    # De-duplicate while preserving order.
    seen = set()
    out: List[str] = []
    for d in candidates:
        key = os.path.normcase(os.path.abspath(d))
        if key not in seen:
            seen.add(key)
            out.append(d)
    return out


def _platform_key() -> str:
    if sys.platform.startswith("win"):
        return "win32"
    if sys.platform == "darwin":
        return "darwin"
    return "linux"


def _find_library_file() -> Optional[str]:
    key = _platform_key()
    names = _LIB_NAMES.get(key, ["czmesh"])
    alt = _LIB_NAMES_ALT.get(key, [])

    # 1) explicit env override
    env = os.environ.get("CZMESH_LIBRARY_PATH")
    if env:
        if os.path.isfile(env):
            return env
        # allow a directory too
        if os.path.isdir(env):
            for n in names + alt:
                p = os.path.join(env, n)
                if os.path.isfile(p):
                    return p
        raise CZMeshImportError(
            f"CZMESH_LIBRARY_PATH is set to {env!r} but the library was not found there."
        )

    # 2) relative search
    for d in _candidate_dirs():
        for n in names + alt:
            p = os.path.join(d, n)
            if os.path.isfile(p):
                return p

    # 3) system search
    found = ctypes.util.find_library("czmesh")
    if found:
        return found
    # Some platforms name it differently.
    found = ctypes.util.find_library("czmesh.so" if key == "linux" else "czmesh")
    if found:
        return found
    return None


class CZMeshImportError(ImportError):
    """Raised when the czmesh shared library cannot be located or loaded."""


_lib_handle: Optional[ctypes.CDLL] = None
_resolved_path: Optional[str] = None


def _load() -> ctypes.CDLL:
    """Load (and cache) the czmesh shared library handle."""
    global _lib_handle, _resolved_path
    if _lib_handle is not None:
        return _lib_handle

    path = _find_library_file()
    if path is None:
        raise CZMeshImportError(
            "Could not find the czmesh shared library (czmesh.dll / libczmesh.so / "
            "libczmesh.dylib). Build it with CMake first, or set the "
            "CZMESH_LIBRARY_PATH environment variable to the full path of the "
            "library file."
        )

    # On Windows the DLL needs its dependent DLLs (e.g. OpenMP) on the path.
    if sys.platform.startswith("win"):
        dll_dir = os.path.dirname(path)
        os.add_dll_directory = getattr(os, "add_dll_directory", None)
        if os.add_dll_directory is not None:
            try:
                os.add_dll_directory(dll_dir)
            except OSError:
                pass

    try:
        _lib_handle = ctypes.CDLL(path)
    except OSError as exc:  # pragma: no cover - platform specific
        raise CZMeshImportError(
            f"Found {path} but failed to load it: {exc}"
        ) from exc
    _resolved_path = path
    return _lib_handle


def library_path() -> str:
    """Return the absolute path of the loaded shared library."""
    _load()
    assert _resolved_path is not None
    return _resolved_path


def version() -> str:
    """Return the C library version string (e.g. ``'1.0.0'``)."""
    lib = _load()
    lib.czmesh_version.restype = ctypes.c_char_p
    return lib.czmesh_version().decode("utf-8", "replace")


def build_info() -> str:
    """Return the C library build-info string (compiler, OpenMP, CUDA, ...)."""
    lib = _load()
    lib.czmesh_build_info.restype = ctypes.c_char_p
    return lib.czmesh_build_info().decode("utf-8", "replace")
