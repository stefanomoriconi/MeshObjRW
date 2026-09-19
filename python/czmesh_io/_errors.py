"""Exception type for czmesh_io."""

from __future__ import annotations


class CZMeshError(RuntimeError):
    """Raised when a czmesh C call fails.

    Attributes
    ----------
    code : int
        Numeric error code returned by the library (see ``czmesh_status``).
    message : str
        Human-readable message obtained from ``czmesh_last_error()``.
    """

    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"czmesh error (code {code}): {message}")
