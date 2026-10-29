"""Decode frozen pre-DBF1 files and exercise the new reader in another process."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

exe, fixture, scratch = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
scratch.mkdir(parents=True, exist_ok=True)
manifest = json.loads((fixture / "legacy-provenance.json").read_text())
for name, expected in manifest["files"].items():
    raw = bytes.fromhex((fixture / (name + ".hex")).read_text())
    assert len(raw) == expected["bytes"]
    assert hashlib.sha256(raw).hexdigest() == expected["sha256"]
    (scratch / name).write_bytes(raw)

# Recover a legacy linked-run checkpoint into a new result file, without append.
result = scratch / "legacy-reader.sdbres"
# CLI writer intentionally refuses replacement: use a per-run fresh filename.
import uuid
result = scratch / (str(uuid.uuid4()) + ".sdbres")
saved = scratch / (str(uuid.uuid4()) + ".sdbmodel")
subprocess.run([str(exe), "--model", str(scratch / "legacy.sdbmodel"),
                "--restart", str(scratch / "legacy.sdbrst"), "--output", str(result),
                "--save-model", str(saved), "--stop-after", "2"], check=True)
assert saved.read_bytes() == (scratch / "legacy.sdbmodel").read_bytes(), "legacy model bytes changed"
subprocess.run([str(exe), "--read", str(result), "--field", "U", "--entity", "4"], check=True)
print("frozen schema1 model / schema3 restart: independent process PASS")
