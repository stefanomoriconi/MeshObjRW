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
The primary, self-contained API is :func:`readOBJ` and :func:`writeOBJ` --
one ``import objrw_io`` is all that is required::

    import objrw_io

    mesh_in  = objrw_io.readOBJ("path/to/meshFile.obj")
    mesh_out = objrw_io.writeOBJ(mesh_in, "path/out/meshFile.obj",
                                 flagBinary=True)

``readOBJ`` transparently accepts an ASCII ``.obj`` **or** a binary
``.objrw`` file (auto-detected).  ``writeOBJ`` emits an ASCII ``.obj`` by
default, or a compact binary ``.objrw`` file when ``flagBinary=True``.

For full control the lower-level helpers (:func:`read`, :func:`read_ascii`,
:func:`read_binary`, :func:`write_ascii`, :func:`write_binary`) and the
:class:`Mesh` object are also exposed.  All geometry is returned as plain
Python lists / arrays (no numpy required).
"""

from __future__ import annotations

from ._version import __version__
from ._errors import OBJRWError
from ._library import library_path, build_info, version
from ._ctypes import OBJRWLibrary
from ._mesh import (
    Mesh,
    read,
    read_ascii,
    read_binary,
    write_ascii,
    write_binary,
    readOBJ,
    writeOBJ,
)

__all__ = [
    "__version__",
    "OBJRWError",
    "Mesh",
    # primary high-level API
    "readOBJ",
    "writeOBJ",
    # lower-level helpers
    "read",
    "read_ascii",
    "read_binary",
    "write_ascii",
    "write_binary",
    "library_path",
    "build_info",
    "version",
]
