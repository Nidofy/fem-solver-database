"""Independent process acceptance; no third-party Python dependencies."""
import csv
import math
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

exe, fixture, scratch = sys.argv[1:]
scratch = Path(scratch).resolve()
scratch.mkdir(parents=True, exist_ok=True)

def invoke(*args, fail=False):
    p = subprocess.run([exe, *map(str, args)], text=True, capture_output=True)
    if (p.returncode != 0) != fail:
        raise AssertionError(f"command {args}: {p.returncode}\n{p.stdout}\n{p.stderr}")
    return p.stdout + p.stderr

for precision, tolerance in (("float", 3e-6), ("double", 1e-7)):
    model = scratch / (precision + ".sdbmodel")
    result = scratch / (precision + ".sdbres")
    split = scratch / (precision + "-split.sdbres")
    restart = scratch / (precision + ".sdbrst")
    for path in (model, result, split, restart):
        path.unlink(missing_ok=True)
    print(invoke("--input", fixture, "--backend", "cpu", "--precision", precision,
                 "--output", result, "--save-model", model, "--async-mib", "1").strip())
    # No original INP is supplied to subsequent processes.
    invoke("--model", model, "--precision", precision, "--output", split,
           "--stop-after", "700", "--checkpoint", restart)
    invoke("--model", model, "--precision", precision, "--output", split,
           "--restart", restart, "--append")
    assert result.read_bytes() == split.read_bytes()
    # A checkpoint must bind the exact committed result prefix, not just its time.
    # stop 733 is deliberately not an output increment (last result is 700).
    partial = scratch / (precision + "-partial.sdbres")
    bound = scratch / (precision + "-bound.sdbrst")
    partial.unlink(missing_ok=True)
    invoke("--model", model, "--precision", precision, "--output", partial,
           "--stop-after", "733", "--checkpoint", bound, "--async-mib", "1")
    prefix = partial.read_bytes()
    link = Path(str(bound) + ".result-link")
    link_bytes = link.read_bytes()
    def refused_unchanged(path, *extra):
        before = path.read_bytes() if path.exists() else None
        invoke("--model", model, "--precision", precision, "--output", path,
               "--restart", bound, "--append", *extra, fail=True)
        assert (path.read_bytes() if path.exists() else None) == before
    # Missing/stale manifest, occupied writer, and incomplete tail fail without mutation.
    link.unlink()
    refused_unchanged(partial)
    link.write_bytes(link_bytes[:-1])
    refused_unchanged(partial)
    link.write_bytes(link_bytes)
    lease = Path(str(partial) + ".writer-lock")
    lease.mkdir()
    try:
        refused_unchanged(partial)
    finally:
        lease.rmdir()
    partial.write_bytes(prefix + b"partial")
    refused_unchanged(partial)
    invoke("--model", model, "--precision", precision, "--output", partial,
           "--restart", bound, "--append", "--recover-tail", "--stop-after", "733")
    assert partial.read_bytes() == prefix
    missing = scratch / (precision + "-missing.sdbres")
    missing.unlink(missing_ok=True)
    refused_unchanged(missing)
    # Same length altered data is not authorized by matching time/size alone.
    altered = bytearray(prefix)
    altered[-1] ^= 1
    partial.write_bytes(altered)
    refused_unchanged(partial)
    partial.write_bytes(prefix)
    # Another complete but older result prefix must be refused without mutation.
    old = scratch / (precision + "-older.sdbres")
    old.unlink(missing_ok=True)
    invoke("--model", model, "--precision", precision, "--output", old, "--stop-after", "600")
    older_bytes = old.read_bytes()
    invoke("--model", model, "--precision", precision, "--output", old,
           "--restart", bound, "--append", fail=True)
    assert old.read_bytes() == older_bytes
    invoke("--model", model, "--precision", precision, "--output", partial,
           "--restart", bound, "--append", "--async-mib", "1")
    assert partial.read_bytes() == result.read_bytes()
    # Linked checkpoint cannot be silently used a second time against advanced results.
    invoke("--model", model, "--precision", precision, "--output", partial,
           "--restart", bound, "--append", fail=True)
    assert partial.read_bytes() == result.read_bytes()
    rows = list(csv.DictReader(invoke("--read", result, "--field", "U", "--region", "FREE",
                                      "--entity", "4").splitlines()))
    assert len(rows) == 21
    for row in rows:
        t, u = float(row["time"]), float(row["value"])
        assert abs(u - 0.06 * (1 - math.cos(20 * t))) < tolerance
    out = scratch / precision
    xdmf = scratch / (precision + ".xdmf")
    invoke("--read", result, "--vtk", out, "--xdmf", xdmf)
    pvd = ET.parse(out / "series.pvd")
    entries = pvd.findall("./Collection/DataSet")
    assert len(entries) == 21
    for entry in entries:
        root = ET.parse(out / entry.attrib["file"])
        piece = root.find("./UnstructuredGrid/Piece")
        assert piece.attrib["NumberOfPoints"] == "5"
        arrays = {a.attrib["Name"]: a for a in piece.findall("./PointData/DataArray")}
        values = list(map(float, arrays["U"].text.split()))
        assert len(values) == 15
        assert abs(values[9] - 0.06 * (1 - math.cos(20 * float(entry.attrib["timestep"])))) < tolerance
    grids = ET.parse(xdmf).findall("./Domain/Grid/Grid")
    assert len(grids) == 21
    assert all(g.find("./Geometry/DataItem").attrib["Dimensions"] == "5 3" for g in grids)
    assert "unknown" in invoke("--read", result, "--field", "U", "--region", "bad", fail=True)
    assert "ahead" in invoke("--model", model, "--precision", precision, "--output", result,
                              "--restart", restart, "--append", fail=True)
    bad = scratch / (precision + "-bad.sdbres")
    bad.write_bytes(result.read_bytes()[:-3])
    assert "incomplete" in invoke("--read", bad, fail=True)
    print(f"independent_reader precision={precision} frames=21 reference_and_XML=PASS exact_restart=PASS")

# Indexed v2 uses per-file nonces; compare decoded values, never nonce bytes.
indexed = scratch / "indexed.sdbres"
indexed_split = scratch / "indexed-split.sdbres"
indexed_restart = scratch / "indexed.sdbrst"
for path in (indexed,indexed_split): path.unlink(missing_ok=True)
invoke("--model",model,"--output",indexed,"--indexed","--async-mib","1")
invoke("--model",model,"--output",indexed_split,"--indexed","--async-mib","1",
       "--stop-after","733","--checkpoint",indexed_restart)
invoke("--model",model,"--output",indexed_split,"--restart",indexed_restart,"--append")
for field in ("U","V","A","RF","S","E"):
    assert invoke("--read",indexed,"--field",field,"--entity","1") == invoke(
        "--read",indexed_split,"--field",field,"--entity","1")
print("indexed independent-process linked restart/async PASS")

assert "CPU" in invoke("--input", Path(fixture).with_name("stored_types.inp"),
                        "--output", scratch / "unsupported.sdbres", fail=True)
assert "unknown option" in invoke("--typo", fail=True)
print("pipeline PASS")
