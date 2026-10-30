"""Verify exact working-tree source and evidence bytes for the DBF6 packet."""
import hashlib
import json
from pathlib import Path

packet = Path(__file__).resolve().parent
root = packet.parents[2]
manifest = json.loads((packet / "manifest.json").read_text(encoding="utf-8"))
seen = set()
for entry in manifest["files"]:
    relative = Path(entry["path"])
    assert not relative.is_absolute() and ".." not in relative.parts and entry["path"] not in seen
    seen.add(entry["path"])
    path = (root / relative).resolve()
    assert path.is_relative_to(root)
    raw = path.read_bytes()
    assert len(raw) == entry["bytes"] and hashlib.sha256(raw).hexdigest() == entry["sha256"], relative
print(f"DBF6 {len(seen)} source/evidence files verified; {manifest['source_state']}")
