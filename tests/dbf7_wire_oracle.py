"""Independent Python CRC/prefix/cursor/value oracle for the DBF7 fixture."""
import struct
import sys
import zlib
from pathlib import Path


class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def take(self, n):
        assert 0 <= n <= len(self.data)-self.pos
        data = self.data[self.pos:self.pos+n]; self.pos += n; return data
    def get(self, fmt): return struct.unpack("<"+fmt, self.take(struct.calcsize("<"+fmt)))[0]
    def string(self): return self.take(self.get("Q")).decode()
    def end(self): assert self.pos == len(self.data)


def records(path, magic):
    r = Reader(path.read_bytes()); assert r.get("Q") == magic and r.get("I") == 1
    out = []
    while r.pos < len(r.data):
        kind, size, crc = r.get("I"), r.get("Q"), r.get("I")
        data = r.take(size); assert zlib.crc32(data) == crc; out.append((kind, data))
    return out


root = Path(sys.argv[1])
bundle = records(root/"bundle.sdb", 0x314C444E55424453)
assert [r[0] for r in bundle] == [1, 2, 3, 200, 201]
cursor = Reader(bundle[1][1])
assert (cursor.get("I"), cursor.get("Q"), cursor.get("Q"), cursor.get("d"), cursor.string()) == (1, 0, 5, .25, "thermal-test/v1")
cursor.end()
assert bundle[-1][1].endswith(struct.pack("<dd", 350, 300))
spectral = records(root/"spectral.sdb", 0x3154434550534453)
assert len(spectral) == 1 and spectral[0][0] == 1
r = Reader(spectral[0][1]); assert r.get("I") == 1 and r.get("Q") == 42
assert r.get("Q") != 0 and r.get("Q") == 0
assert (r.get("I"), r.get("I"), r.get("I"), r.string()) == (0, 0, 0, "")
tail = b"".join(struct.pack("<Q", len(values))+struct.pack("<"+"d"*len(values), *values)
                for values in ([0,10,20], [1,2,3,4,5,6], [0,1,2,3,4,5]))
assert spectral[0][1].endswith(tail)
print("DBF7 independent bundle/spectral wire oracle PASS")
