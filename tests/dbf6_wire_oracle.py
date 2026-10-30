"""Independent little-endian/CRC reader for the DBF6 neutral mixed-DOF fixture."""
import struct
import sys
import zlib
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        assert n >= 0 and n <= len(self.data) - self.pos
        out = self.data[self.pos:self.pos+n]
        self.pos += n
        return out

    def get(self, code):
        return struct.unpack("<" + code, self.take(struct.calcsize("<" + code)))[0]

    def string(self):
        return self.take(self.get("Q")).decode()

    def vector(self, fn):
        count = self.get("Q")
        assert count <= 100000
        return [fn() for _ in range(count)]

    def end(self):
        assert self.pos == len(self.data)


raw = Reader((Path(sys.argv[1]) / "foundation.sdbf").read_bytes())
assert raw.get("Q") == 0x31444E554F464453 and raw.get("I") == (2 if len(sys.argv)>2 and sys.argv[2]=="64" else 1)
records = []
while raw.pos < len(raw.data):
    kind, length, crc = raw.get("I"), raw.get("Q"), raw.get("I")
    body = raw.take(length)
    assert zlib.crc32(body) == crc
    records.append((kind, body))
assert records[0][0] == 1 and records[1][0] == 200
d = Reader(records[0][1])
assert (d.get("I"), d.get("Q"), d.get("Q"), d.get("Q"), d.get("Q")) == (1, 42, 2, 1, 1)
variables = d.vector(lambda: (d.get("I"), d.get("I"), d.get("I"), d.string(), d.string(), d.string()))
assert [v[3] for v in variables] == ["u", "temperature", "pressure", "lambda"]


def binding():
    key = (d.get("I"), d.get("Q"), d.get("I"), d.get("I"), d.get("I"))
    return key, d.get("I"), d.get("Q"), d.get("d"), d.vector(lambda: (d.get("Q"), d.get("d")))


bindings = d.vector(binding)
assert [b[2] for b in bindings[:5]] == [1, 0, 2, 3, 4]
assert bindings[5][3] == 333 and bindings[6][4] == [(0, 2), (1, -1)]
assert d.vector(lambda: d.get("Q")) == [101, 505, 99, 444, 888]
assert d.vector(lambda: d.get("Q")) == [4, 2, 0, 3, 1]
d.end()
m = Reader(records[1][1])
assert m.get("I") == 2
field_count = m.get("Q")
assert field_count == 6 and m.get("Q") == 42 and m.get("Q") != 0
assert m.get("Q") == 0  # no batch-specific consumer context in this neutral registry
fields = []
lanes = ["f", "d", "i", "I", "q", "Q", "B"]
for _ in range(field_count):
    assert m.get("I") == 1
    name, scalar = m.string(), m.get("I")
    shape, components = m.vector(lambda: m.get("I")), m.vector(m.string)
    association, location, shear = m.get("I"), m.get("I"), m.get("I")
    units, basis = m.string(), m.string()
    config, time, persistence, policy, missing, storage = [m.get("I") for _ in range(6)]
    count, schema = m.get("Q"), m.string()
    version, layout, width, basis_id = m.get("I"), m.get("I"), m.get("I"), m.get("Q")
    initial = m.vector(lambda: m.get(lanes[scalar]))
    if m.get("B"):
        assert m.get("I") == 1
        domain = m.get("Q")
        offsets, indices = m.vector(lambda: m.get("Q")), m.vector(lambda: m.get("Q"))
        addresses = m.vector(lambda: m.take(96))
        assert len(addresses) == count
        if name == "contact":
            assert storage == 2 and domain == 16 and indices == [2, 9]
            assert struct.unpack_from("<Q", addresses[0])[0] == 0
            assert struct.unpack_from("<I", addresses[0], 92)[0] == 1
        if name == "element_node":
            assert offsets == [0, 2] and scalar == 5 and initial == [9007199254740993]
    fields.append((name, scalar, count * len(components), policy, missing))
m.end()
expected = [i for i, f in enumerate(fields) if f[3] == 0]
assert len(records) == 2 + len(expected)
for record, index in zip(records[2:], expected):
    kind, body = record
    assert kind == 201
    b = Reader(body)
    assert b.get("Q") == index and b.get("Q") == 0
    name, scalar, count, policy, missing = fields[index]
    assert b.get("Q") == count
    values = []
    for _ in range(count):
        values.append(b.get(lanes[scalar]))
        if missing:
            assert b.get("B") in (0, 1)
    if name == "temperature":
        assert values == [300, 333]
    if name == "element_node":
        assert values == [9007199254740993, 9007199254740993]
    b.end()
print("DBF6 independent DOF/state wire oracle PASS")
