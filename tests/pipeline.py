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
                 "--output", result, "--save-model", model).strip())
    # No original INP is supplied to subsequent processes.
    invoke("--model", model, "--precision", precision, "--output", split,
           "--stop-after", "700", "--checkpoint", restart)
    invoke("--model", model, "--precision", precision, "--output", split,
           "--restart", restart, "--append")
    assert result.read_bytes() == split.read_bytes()
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

assert "CPU" in invoke("--input", Path(fixture).with_name("stored_types.inp"),
                        "--output", scratch / "unsupported.sdbres", fail=True)
assert "unknown option" in invoke("--typo", fail=True)
print("pipeline PASS")
