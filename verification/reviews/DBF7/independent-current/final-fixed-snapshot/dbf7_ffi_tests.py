import ctypes as C
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from sdb_reader import Reader

with Reader(sys.argv[1], sys.argv[2], dll_directories=sys.argv[3:4]) as r:
    assert r.frames == 4096
    assert r.history("U",1,4090,4095,max_samples=6) == [(float(i),float(i)) for i in range(4090,4096)]
    try:
        r.history("U",1,0,100,max_samples=2)
        raise AssertionError("budget not enforced")
    except RuntimeError:
        pass
    sentinel = C.c_double(123)
    assert r._lib.sdb_reader_time(r._handle,999999,C.byref(sentinel)) == -1
    assert sentinel.value == 123
    assert r._lib.sdb_reader_frame_count(None,None) == -1
try:
    r.frames
    raise AssertionError("closed reader accepted")
except RuntimeError:
    pass
with Reader(sys.argv[1], Path(sys.argv[2]).with_name("typed.sdbres"), dll_directories=sys.argv[3:4]) as typed:
    assert typed.entity_u64(19,"EXACT",1) == (1 << 64)-1
print("C ABI/Python checks PASS")
