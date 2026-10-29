"""Independent corruption campaign: repair outer CRCs/references to reach v4 envelope gates."""
from pathlib import Path
import struct, subprocess, sys, zlib
exe, source, root = map(lambda x: Path(x).resolve(), sys.argv[1:4])
root.mkdir(parents=True, exist_ok=True)
original = source.read_bytes()
def records(data):
    pos = 12
    while pos < len(data):
        kind, size, crc = struct.unpack_from('<IQI', data, pos)
        assert pos + 16 + size <= len(data)
        yield pos, kind, size, crc
        pos += 16 + size
assert struct.unpack_from('<Q', original)[0] == 0x34534552424453
block = next(r for r in records(original) if r[1] == 2)
def mutated(change):
    data = bytearray(original)
    off, kind, size, old_crc = block
    change(data, off + 16)
    new_crc = zlib.crc32(data[off+16:off+16+size])
    struct.pack_into('<I', data, off+12, new_crc)
    old_ref = struct.pack('<QQI', off, size, old_crc)
    new_ref = struct.pack('<QQI', off, size, new_crc)
    changed_directories = []
    for d_off, d_kind, d_size, d_crc in list(records(data)):
        if d_kind != 4: continue
        body = bytes(data[d_off+16:d_off+16+d_size])
        if old_ref not in body: continue
        assert body.count(old_ref) == 1
        body = body.replace(old_ref, new_ref)
        data[d_off+16:d_off+16+d_size] = body
        new_d_crc = zlib.crc32(body)
        struct.pack_into('<I', data, d_off+12, new_d_crc)
        changed_directories.append((d_off, d_size, d_crc, new_d_crc))
    assert len(changed_directories) == 1
    for f_off, f_kind, f_size, f_crc in list(records(data)):
        if f_kind != 5: continue
        for d_off, d_size, d_crc, new_d_crc in changed_directories:
            ref = struct.unpack_from('<QQI', data, f_off+16+74)
            if ref == (d_off, d_size, d_crc):
                struct.pack_into('<I', data, f_off+16+74+16, new_d_crc)
                struct.pack_into('<I', data, f_off+12, zlib.crc32(data[f_off+16:f_off+16+f_size]))
    # Every outer record remains CRC-correct. Only the logical envelope is invalid.
    for off, kind, size, crc in records(data):
        assert zlib.crc32(data[off+16:off+16+size]) == crc
    return data
cases = [
    ('version', lambda b,p: b.__setitem__(p,2), 'unknown compression envelope version'),
    ('codec', lambda b,p: b.__setitem__(p+1,254), 'unknown block codec'),
    ('reserved', lambda b,p: b.__setitem__(p+2,1), 'unknown compression feature flags'),
    ('raw-cap', lambda b,p: struct.pack_into('<Q',b,p+4,4*1024*1024+1), 'compressed block declared size outside limits'),
    ('stored-span', lambda b,p: struct.pack_into('<Q',b,p+12,1), 'compressed block declared size outside limits'),
    ('raw-crc', lambda b,p: b.__setitem__(p+20,b[p+20]^1), 'compression block raw CRC mismatch'),
]
for name, change, diagnostic in cases:
    path = root / (name+'.sdbres')
    path.write_bytes(mutated(change))
    p = subprocess.run([str(exe),'--read',str(path),'--verify'],capture_output=True,text=True,timeout=30)
    assert p.returncode != 0 and diagnostic in p.stderr, (name,p.returncode,p.stdout,p.stderr)
    print(name, 'PASS', p.stderr.strip())
print('independent repaired-outer-CRC envelope campaign: 6 PASS')
