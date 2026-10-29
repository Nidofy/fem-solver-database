"""Verify frozen DBF2 provenance, then run a separate current reader."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

if sys.argv[1] == "--freeze":
    source, target = map(Path, sys.argv[2:])
    target.mkdir(parents=True, exist_ok=True)
    manifest = {"source_commit": "00b716657ee4a2d2cbc2a5bc26ee85c2cea48937",
                "producer": "model_foundation_tests + foundation_integration_tests rebuilt from git archive",
                "files": {}}
    for precision in ("float", "double"):
        for extension in ("sdbmodel", "sdbres", "sdbrst"):
            name = f"{precision}.{extension}"
            raw = (source / name).read_bytes()
            (target / (name + ".hex")).write_text(raw.hex() + "\n", encoding="ascii")
            manifest["files"][name] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
    (target / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
else:
    executable, source, target = map(Path, sys.argv[1:])
    target.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / "provenance.json").read_text())
    for name, expected in manifest["files"].items():
        raw = bytes.fromhex((source / (name + ".hex")).read_text())
        assert len(raw) == expected["bytes"]
        assert hashlib.sha256(raw).hexdigest() == expected["sha256"]
        (target / name).write_bytes(raw)
    subprocess.run([str(executable), "--dbf3-legacy", str(target)], check=True)
    for precision in ("float", "double"):
        for extension in ("sdbmodel", "sdbrst"):
            assert (target / f"{precision}.{extension}").read_bytes() == (target / f"{precision}-resaved.{extension}").read_bytes()
    print("DBF2 schema3 model / Restart4 exact bytes / TypedIndexed3 values PASS")
