"""Exception type for objrw_io."""

from __future__ import annotations


class OBJRWError(RuntimeError):
    """Raised when a objrw C call fails.

    Attributes
    ----------
    code : int
        Numeric error code returned by the library (see ``objrw_status``).
    message : str
        Human-readable message obtained from ``objrw_last_error()``.
    """

    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"objrw error (code {code}): {message}")
