"""Authenticate frozen fixture provenance, then run a separate C++ reader."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

exe, fixture, scratch = map(Path, sys.argv[1:])
scratch.mkdir(parents=True, exist_ok=True)
manifest = json.loads((fixture / "legacy-provenance.json").read_text())
for name, expected in manifest["files"].items():
    raw = bytes.fromhex((fixture / (name + ".hex")).read_text())
    assert len(raw) == expected["bytes"]
    assert hashlib.sha256(raw).hexdigest() == expected["sha256"]
    (scratch / name).write_bytes(raw)
subprocess.run([str(exe), "--legacy", str(scratch)], check=True)
assert (scratch / "schema2-resaved.sdbmodel").read_bytes() == (scratch / "schema2.sdbmodel").read_bytes()
print("pre-DBF2 schema2 exact bytes / native1 / indexed2 PASS")
