"""Independent LE decoder for registered-state fixtures (no C++ codec reuse)."""
from pathlib import Path
import struct
import sys
import zlib

class Cursor:
    def __init__(self, data):
        self.data, self.pos = data, 0
    def read(self, fmt):
        fmt = "<" + fmt
        size = struct.calcsize(fmt)
        assert self.pos + size <= len(self.data)
        value = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += size
        return value[0] if len(value) == 1 else value
    def string(self):
        n = self.read("Q")
        assert self.pos + n <= len(self.data)
        value = self.data[self.pos:self.pos+n].decode("utf-8")
        self.pos += n
        return value
    def vector(self, fn):
        return [fn() for _ in range(self.read("Q"))]
    def end(self):
        assert self.pos == len(self.data)

def records(raw, prefix):
    out, pos = [], prefix
    while pos < len(raw):
        kind, size, crc = struct.unpack_from("<IQI", raw, pos)
        pos += 16
        assert size <= len(raw) - pos
        body = raw[pos:pos+size]
        assert zlib.crc32(body) == crc
        out.append((kind, body))
        pos += size
    assert pos == len(raw) and out[-1] == (0xffffffff, b"")
    return out

def manifest(raw):
    c = Cursor(raw)
    assert c.read("I") == 1
    result = []
    for _ in range(c.read("Q")):
        assert c.read("I") == 1  # FieldDescriptor.version
        name, scalar = c.string(), c.read("I")
        shape, components = c.vector(lambda: c.read("I")), c.vector(c.string)
        association, location, shear = c.read("III")
        units, basis = c.string(), c.string()
        configuration, time, persistence, restart, missing, storage = c.read("IIIIII")
        count, schema, version = c.read("Q"), c.string(), c.read("I")
        layout, width, basis_id = c.read("IIQ")
        fmt = ["f", "d", "i", "I", "q", "Q", "B"][scalar]
        initial = c.vector(lambda: c.read(fmt))
        assert schema == "synthetic-storage-fixture.state" and version == 1
        assert association == 0 and location == 0 and shear == 0
        assert units == basis == "UNSPECIFIED" and basis_id == 0
        assert configuration == 0 and time == 1 and storage == 0
        assert count == 5 and len(components) == len(initial) == shape[0]
        result.append(dict(name=name, scalar=scalar, fmt=fmt, count=count,
                           width=len(initial), restart=restart, missing=missing,
                           initial=initial, layout=layout, block_width=width))
    c.end()
    assert [x["name"] for x in result] == ["exact", "signed", "rebuild", "transient", "vector"]
    assert result[0]["initial"] == [9007199254740993]
    assert result[1]["initial"] == [-9007199254740993]
    assert result[2]["restart"] == 1 and result[3]["restart"] == 2
    assert result[4]["layout"] == 1 and result[4]["block_width"] == 4
    return result

def verify(path, precision, checkpoint=False):
    raw = path.read_bytes()
    if checkpoint:
        assert struct.unpack_from("<Q", raw)[0] == 0x325642444253
        rec = records(raw, 8)
        assert struct.unpack_from("<I", rec[0][1])[0] == 3
        fingerprint = struct.unpack_from("<Q", rec[0][1], 42)[0]
    else:
        assert struct.unpack_from("<QI", raw) == (0x33545352424453, 1)
        rec = records(raw, 12)
        assert struct.unpack_from("<I", rec[0][1])[0] == 5
        binding = dict(rec)[199]
        fingerprint, rotation = struct.unpack("<QB", binding)
        assert rotation == 0
    metadata = dict(rec)[200]
    h = 14695981039346656037
    for b in metadata:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    assert h == fingerprint
    fields = manifest(metadata)
    assert fields[2]["scalar"] == (0 if precision == 4 else 1)
    lanes = {}
    for kind, body in rec:
        if kind != 201:
            continue
        c = Cursor(body)
        entry, offset, count = c.read("QQQ")
        assert entry in (0, 1, 4) and offset == 0
        d = fields[entry]
        assert count == d["count"] * d["width"]
        values, validity = [], []
        for _ in range(count):
            values.append(c.read(d["fmt"]))
            if d["missing"]:
                validity.append(c.read("B"))
        c.end()
        assert entry not in lanes
        lanes[entry] = (values, validity)
    assert set(lanes) == {0, 1, 4}
    assert lanes[0][0] == [(1 << 64)-1] + [9007199254740993] * 4
    assert lanes[1][0] == [-9007199254740993] * 5
    assert lanes[4][0] == [1, 0, 3] + [1, 2, 3] * 4
    assert lanes[4][1] == [1, 0, 1] + [1] * 12

root = Path(sys.argv[1])
for tag, precision in (("f", 4), ("d", 8)):
    verify(root / f"registered-{tag}.sdbrst", precision)
    verify(root / f"registered-{tag}.chk", precision, True)
print("Independent registered-state LE/CRC/manifest/exact integer/validity/policy oracle PASS (FP32/FP64)")
