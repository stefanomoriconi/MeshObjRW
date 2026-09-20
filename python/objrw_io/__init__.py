"""objrw_io - high-level Python bindings for the objrw shared library.

objrw is a fast, portable C/C++ library that reads and writes triangular
mesh files in two encodings:

  * ASCII Wavefront OBJ (``.obj``)
  * a compact binary format called ``objrw`` (``.objrw``)

The library auto-detects the encoding on read by sniffing an 8-byte magic
header, so a single :func:`objrw_io.read` call transparently handles both.

This package is a thin ``ctypes`` wrapper around the compiled shared
library (``objrw.dll`` / ``libobjrw.so`` / ``libobjrw.dylib``), giving
script users the same performance as the C/C++ front-end while staying
convenient from Python.

Quick start
-----------
>>> import objrw_io
>>> m = objrw_io.read("model.obj")
>>> print(m.num_vertices, m.num_triangles)
>>> m.compute_normals()
>>> m.write("model.objrw")            # save as binary
>>> m2 = objrw_io.read("model.objrw")  # auto-detects binary

All geometry is exposed as plain Python lists / arrays (no numpy required).
"""

from __future__ import annotations

from ._version import __version__
from ._errors import OBJRWError
from ._library import library_path, build_info, version
from ._ctypes import OBJRWLibrary
from ._mesh import Mesh, read, read_ascii, read_binary, write_ascii, write_binary

__all__ = [
    "__version__",
    "OBJRWError",
    "Mesh",
    "read",
    "read_ascii",
    "read_binary",
    "write_ascii",
    "write_binary",
    "library_path",
    "build_info",
    "version",
]
