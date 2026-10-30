"""Optional read-only ctypes client for sdb_reader ABI v1 (no third-party packages)."""
import ctypes as C
import threading


class Reader:
    def __init__(self, library, path):
        self._lock = threading.RLock()
        self._lib = C.CDLL(str(library))
        lib = self._lib
        lib.sdb_abi_version.restype = C.c_uint32
        if lib.sdb_abi_version() != 1:
            raise RuntimeError("unsupported sdb_reader ABI")
        lib.sdb_last_error.restype = C.c_char_p
        lib.sdb_reader_open.argtypes = [C.c_char_p, C.POINTER(C.c_void_p)]
        lib.sdb_reader_close.argtypes = [C.c_void_p]
        lib.sdb_reader_close.restype = None
        lib.sdb_reader_frame_count.argtypes = [C.c_void_p, C.POINTER(C.c_uint64)]
        lib.sdb_reader_time.argtypes = [C.c_void_p, C.c_uint64, C.POINTER(C.c_double)]
        lib.sdb_reader_history_f64.argtypes = [C.c_void_p,C.c_char_p,C.c_uint64,C.c_uint32,C.c_double,C.c_double,
            C.POINTER(C.c_double),C.POINTER(C.c_double),C.c_size_t,C.POINTER(C.c_size_t)]
        lib.sdb_reader_entity_u64.argtypes = [C.c_void_p,C.c_uint64,C.c_char_p,C.c_uint64,C.c_uint32,
            C.POINTER(C.c_uint64),C.POINTER(C.c_uint8)]
        self._handle = C.c_void_p()
        self._check(lib.sdb_reader_open(str(path).encode("utf-8"), C.byref(self._handle)))

    def _check(self, code):
        if code:
            raise RuntimeError(self._lib.sdb_last_error().decode("utf-8", errors="replace"))

    def close(self):
        with self._lock:
            if self._handle:
                self._lib.sdb_reader_close(self._handle)
                self._handle = C.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def frames(self):
        with self._lock:
            out = C.c_uint64()
            self._check(self._lib.sdb_reader_frame_count(self._handle, C.byref(out)))
            return out.value

    def history(self, field, entity, begin, end, component=0, max_samples=100000):
        if not isinstance(max_samples, int) or not 0 <= max_samples <= 10000000:
            raise ValueError("invalid Python output sample budget")
        with self._lock:
            times = (C.c_double * max_samples)(); values = (C.c_double * max_samples)(); count = C.c_size_t()
            self._check(self._lib.sdb_reader_history_f64(self._handle,field.encode(),entity,component,begin,end,
                times,values,max_samples,C.byref(count)))
            return [(times[i], values[i]) for i in range(count.value)]

    def entity_u64(self, frame, field, entity, component=0):
        with self._lock:
            value, valid = C.c_uint64(), C.c_uint8()
            self._check(self._lib.sdb_reader_entity_u64(self._handle,frame,field.encode(),entity,component,
                C.byref(value),C.byref(valid)))
            return value.value if valid.value else None
