import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from sdb_stream import visit_history

library, directory, masked_path, dll_dir = sys.argv[1:]
directory = Path(directory)

def run(visitor, begin=4090, end=4095):
    return visit_history(library, directory / "range.sdbres", "U", 1, 0, begin, end,
                         visitor, dll_directories=[dll_dir])

rows = []
assert run(lambda t, v: rows.append((t, v))) == 6
assert rows == [(n, n) for n in range(4090, 4096)]
assert run(lambda t, v: False) == 1
assert run(lambda t, v: True, 5000, 5001) == 0

def interrupt(t, v):
    raise LookupError("visitor failed")
try:
    run(interrupt)
except LookupError as exc:
    assert str(exc) == "visitor failed"
else:
    raise AssertionError("visitor exception lost")

try:
    run(interrupt, 5, 1)
except RuntimeError as exc:
    assert "interval" in str(exc)
else:
    raise AssertionError("reversed interval accepted")

exact = []
assert visit_history(library, directory / "typed.sdbres", "EXACT", 1, 0, 19, 19,
                     lambda t, v: exact.append(v), scalar="u64", dll_directories=[dll_dir]) == 1
assert exact == [2**64-1]

masked = []
assert visit_history(library, masked_path, "U64", 1, 0, 0, 0,
                     lambda t, v: masked.append(v), scalar="u64", dll_directories=[dll_dir]) == 1
assert masked == [None]
