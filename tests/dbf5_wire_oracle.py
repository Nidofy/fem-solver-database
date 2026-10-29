"""Independent DBF5 LE/CRC/zstd/typed-lane oracle; never imports C++ codecs.

Usage: python dbf5_wire_oracle.py BENCHMARK_OUTPUT [REAL_ZSTD_LIBRARY]
The benchmark emits repeated/random, native/compressed file pairs.
"""
import ctypes
import ctypes.util
import json
from pathlib import Path
import struct
import sys
import zlib

CAP = 4 * 1024 * 1024
FORMATS = "fdiIqQB"


class Cursor:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        assert 0 <= count <= len(self.data) - self.pos
        out = self.data[self.pos:self.pos + count]
        self.pos += count
        return out

    def read(self, fmt):
        values = struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))
        return values[0] if len(values) == 1 else values

    def string(self):
        return self.take(self.read("Q")).decode("utf-8")

    def vector(self, item):
        count = self.read("Q")
        assert count <= len(self.data) + 1
        return [item() for _ in range(count)]

    def end(self):
        assert self.pos == len(self.data), (self.pos, len(self.data))


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


class Zstd:
    def __init__(self, library):
        self.lib = ctypes.CDLL(library)
        for name in ("ZSTD_findFrameCompressedSize", "ZSTD_getFrameContentSize"):
            function = getattr(self.lib, name)
            function.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
            function.restype = ctypes.c_ulonglong if name.endswith("ContentSize") else ctypes.c_size_t
        self.lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
        self.lib.ZSTD_isError.restype = ctypes.c_uint
        self.lib.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        self.lib.ZSTD_decompress.restype = ctypes.c_size_t
        self.lib.ZSTD_versionString.restype = ctypes.c_char_p

    def decode(self, data, raw_bytes):
        assert raw_bytes <= CAP and len(data) <= 5 * 1024 * 1024
        assert data[:4] == b"\x28\xb5\x2f\xfd" and data[4] & 4
        source = ctypes.create_string_buffer(data)
        frame = self.lib.ZSTD_findFrameCompressedSize(source, len(data))
        assert not self.lib.ZSTD_isError(frame) and frame == len(data)
        assert self.lib.ZSTD_getFrameContentSize(source, len(data)) == raw_bytes
        destination = ctypes.create_string_buffer(raw_bytes)
        actual = self.lib.ZSTD_decompress(destination, raw_bytes, source, len(data))
        assert not self.lib.ZSTD_isError(actual) and actual == raw_bytes
        return destination.raw[:actual]


def descriptor(c):
    strings = [c.string() for _ in range(4)]
    location, time = c.read("II")
    components = c.vector(c.string)
    rest = c.read("IIBB")
    return strings, location, time, components, rest


def schema(c):
    assert c.read("I") == 1
    name, scalar = c.string(), c.read("I")
    shape, components = c.vector(lambda: c.read("I")), c.vector(c.string)
    association, location, shear = c.read("III")
    units, basis = c.string(), c.string()
    configuration, time, persistence, restart, missing, storage = c.read("6I")
    assert name == "LANE" + str(scalar) and scalar < 7
    assert shape == [1] and components == ["VALUE"]
    assert (association, location, shear, missing) == (0, 0, 0, 1)
    assert units == basis == "UNSPECIFIED"
    return name, scalar


def address(c):
    entity, *other = c.read("Q5Ii2Q")
    endpoints = (c.read("IQIi"), c.read("IQIi"))
    tail = c.read("II")
    assert not any(other) and endpoints == ((0, 0, 0, 0), (0, 0, 0, 0)) and tail == (0, 0)
    return entity


def payload_body(c):
    name, scalar = schema(c)
    addresses = c.vector(lambda: address(c))
    validity = c.vector(lambda: c.read("B"))
    schema_id, version, basis = c.string(), c.read("I"), c.read("Q")
    assert schema_id == "" and version == 1 and basis == 0
    values = c.vector(lambda: c.read(FORMATS[scalar]))
    assert len(values) == len(validity) == len(addresses) <= 256
    assert addresses == sorted(addresses) and len(set(addresses)) == len(addresses)
    assert validity == [int((entity - 1) % 13 != 0) for entity in addresses]
    assert all(valid or value == 0 for valid, value in zip(validity, values))
    return name, scalar, addresses, values, validity


def payload(raw):
    c = Cursor(raw)
    decoded = payload_body(c)
    c.end()
    return decoded


def options(c):
    codec, level, minimum, smaller = c.read("BiQB")
    assert codec in (0, 1) and smaller in (0, 1)
    return codec, level, minimum, smaller


def read_file(path, zstd):
    wire = path.read_bytes()
    magic, version = struct.unpack_from("<QI", wire)
    assert version == 1 and magic in (0x33534552424453, 0x34534552424453)
    compressed = magic == 0x34534552424453
    records, pos = {}, 12
    while pos < len(wire):
        assert len(wire) - pos >= 16
        offset = pos
        kind, count, crc = struct.unpack_from("<IQI", wire, pos)
        pos += 16
        assert count <= len(wire) - pos
        body = wire[pos:pos + count]
        assert zlib.crc32(body) == crc
        records[offset] = (kind, body, crc)
        pos += count
    assert pos == len(wire)
    items = list(records.items())
    assert items[0][1][0] == 0
    model = items[0][1][1]
    index = 1
    policy = b""
    if compressed:
        assert items[index][1][0] == 11
        policy = items[index][1][1]
        c = Cursor(policy)
        assert c.read("I") == 1 and options(c) == (1, 3, 0, 1)
        assert c.read("I") == 1 and c.string() == "LANE6" and options(c) == (0, 3, 256, 1)
        c.end()
        index += 1
    assert items[index][1][0] == 10
    identity = struct.unpack("<QQ", items[index][1][1])
    assert identity[0] == fnv(model + policy)
    blocks, footers, compressed_count = [], [], 0
    decoded_blocks = {}
    for offset, (kind, body, crc) in items[index + 1:]:
        if kind == 2:
            if compressed:
                assert len(body) >= 24
                version, codec, reserved, raw_size, stored_size, raw_crc = struct.unpack_from("<BBHQQI", body)
                assert version == 1 and codec in (0, 1) and reserved == 0
                assert raw_size <= CAP and stored_size == len(body) - 24
                raw = zstd.decode(body[24:], raw_size) if codec else body[24:]
                assert len(raw) == raw_size and zlib.crc32(raw) == raw_crc
                compressed_count += codec == 1
            else:
                raw = body
            typed = payload(raw)
            if compressed and typed[0] == "LANE6":
                assert codec == 0, "persisted None field override was not honored"
            blocks.append((offset, raw, typed))
            decoded_blocks[offset] = typed
        elif kind == 5:
            c = Cursor(body)
            assert c.read("QQ") == identity
            frame_count = c.read("Q")
            info = c.read("4Q2d2B")
            ref = c.read("QQI")
            ancestors = c.read("64Q")
            c.end()
            assert frame_count == len(footers)
            if frame_count:
                assert info[0] == info[2] == frame_count - 1 and info[4] == frame_count - 1
                directory = records[ref[0]]
                assert directory[0] == 4 and (len(directory[1]), directory[2]) == ref[1:]
                assert ancestors[0] == footers[-1]
                for k, ancestor in enumerate(ancestors):
                    assert ancestor == (footers[frame_count - (1 << k)] if frame_count >= (1 << k) else 0)
            else:
                assert ref == (0, 0, 0) and not any(ancestors)
            footers.append(offset)
        else:
            assert kind == 4
            c = Cursor(body)
            info = c.read("4Q2d2B")
            binding = c.read("5Qd")
            assert binding[:3] == (1, 2, 3) and binding[3:] == (info[1], info[2], info[4])
            fields = c.read("Q")
            assert fields == 7
            for field in range(fields):
                desc = descriptor(c)
                assert desc[0][0] == "LANE" + str(field) and c.read("B") == 1
                metadata = payload_body(c)
                assert metadata[:2] == ("LANE" + str(field), field) and metadata[2:] == ([], [], [])
                block_count = c.read("Q")
                assert block_count > 0
                previous = 0
                for _ in range(block_count):
                    first, last, row_count = c.read("QQI")
                    block_ref = c.read("QQI")
                    first_address, last_address = address(c), address(c)
                    entry = records[block_ref[0]]
                    assert entry[0] == 2 and (len(entry[1]), entry[2]) == block_ref[1:]
                    decoded = decoded_blocks[block_ref[0]]
                    assert decoded[:2] == metadata[:2] and row_count == len(decoded[2])
                    assert first == first_address == decoded[2][0] > previous
                    assert last == last_address == decoded[2][-1]
                    previous = last
            c.end()
    assert len(footers) == 4 and items[-1][1][0] == 5
    return model, blocks, compressed_count


def main():
    root = Path(sys.argv[1])
    library = sys.argv[2] if len(sys.argv) > 2 else ctypes.util.find_library("zstd")
    if not library and sys.platform == "win32":
        library = "C:/Program Files/Git/mingw64/bin/libzstd.dll"
    assert library, "provide an installed real zstd library"
    zstd = Zstd(library)
    summaries = []
    for case in ("repeated", "random"):
        model, baseline, _ = read_file(root / (case + "-native.sdbres"), zstd)
        other_model, compressed, count = read_file(root / (case + "-compressed.sdbres"), zstd)
        assert other_model == model and len(baseline) == len(compressed) and count > 0
        for (_, raw, typed), (_, other_raw, other_typed) in zip(baseline, compressed):
            assert raw == other_raw and typed == other_typed
            if case == "repeated":
                _, scalar, addresses, values, validity = typed
                expected = (1.25, 1.25, -(1 << 31) + 7, (1 << 32) - 8,
                            -(1 << 63) + 7, (1 << 64) - 8, 248)[scalar]
                assert values == [expected if valid else 0 for valid in validity]
        assert {typed[1] for _, _, typed in baseline} == set(range(7))
        summaries.append(dict(case=case, blocks=len(compressed), zstd_blocks=count,
                              raw_bytes=sum(len(raw) for _, raw, _ in compressed)))
    print(json.dumps(dict(result="PASS", zstd=zstd.lib.ZSTD_versionString().decode(), cases=summaries), sort_keys=True))


if __name__ == "__main__":
    main()
