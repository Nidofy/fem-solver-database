"""Synchronous callback-based history reads for the SDB read-only C ABI."""
import ctypes as C
import os
from contextlib import ExitStack
from pathlib import Path

_F64 = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_double, C.c_double)
_U64 = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_double, C.c_uint64, C.c_uint8)


def visit_history(library, path, field, entity, component, begin, end, visitor,
                  *, scalar="f64", dll_directories=()):
    """Visit (time, value); return False from visitor to stop. Masked u64 values are None.

    Returns the delivered count. Exceptions in the visitor propagate after the C call
    returns; samples already delivered remain with the caller. Memory used for output
    is independent of the number of matching frames.
    """
    if scalar not in ("f64", "u64") or not callable(visitor):
        raise ValueError("expected f64 or u64 and a callable visitor")
    if type(entity) is not int or not 0 <= entity < 2**64:
        raise ValueError("entity outside uint64")
    if type(component) is not int or not 0 <= component < 2**32:
        raise ValueError("component outside uint32")
    path_bytes = str(path).encode("utf-8")
    field_bytes = field.encode("utf-8")
    if b"\0" in path_bytes or b"\0" in field_bytes:
        raise ValueError("embedded NUL in result path or field")

    count = 0
    failure = None

    def receive(_context, time, value, valid=1):
        nonlocal count, failure
        try:
            count += 1
            return int(visitor(time, value if valid else None) is not False)
        except BaseException as exc:
            failure = exc
            return 0

    with ExitStack() as stack:
        if os.name == "nt":
            for directory in dll_directories:
                stack.enter_context(os.add_dll_directory(str(Path(directory).resolve())))
        lib = C.CDLL(str(Path(library).resolve()))
        callback = _F64 if scalar == "f64" else _U64
        visit = getattr(lib, f"sdb_visit_history_{scalar}_v1")
        visit.argtypes = [C.c_char_p, C.c_char_p, C.c_uint64, C.c_uint32, C.c_double,
                          C.c_double, callback, C.c_void_p, C.POINTER(C.c_char), C.c_size_t]
        visit.restype = C.c_int
        error = C.create_string_buffer(1024)
        rc = visit(path_bytes, field_bytes, entity, component, begin, end,
                   callback(receive), None, error, len(error))
        if failure is not None:
            raise failure
        if rc:
            raise RuntimeError(error.value.decode("utf-8", errors="replace"))
    return count
