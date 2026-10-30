"""Verify the DBF8 committed-content packet against a checkout or extracted archive."""
import hashlib
import json
import os
from pathlib import Path

packet = Path(__file__).resolve().parent
root = packet.parents[2]
manifest = json.loads((packet/"manifest.json").read_text(encoding="utf-8"))
seen = set()
for entry in manifest["files"]:
    relative = Path(entry["path"])
    assert not relative.is_absolute() and ".." not in relative.parts and entry["path"] not in seen
    seen.add(entry["path"])
    path = root/relative
    if entry.get("git_symlink"):
        # Git archives store the link text. zipfile extraction may materialize
        # it as a regular file; never dereference an archived test-fixture link.
        raw = os.readlink(path).encode("utf-8") if path.is_symlink() else path.read_bytes()
    else:
        path = path.resolve()
        assert path.is_relative_to(root)
        raw = path.read_bytes()
    if entry.get("allow_crlf_normalization"):
        raw = raw.replace(b"\r\n", b"\n")
    assert len(raw) == entry["bytes"] and hashlib.sha256(raw).hexdigest() == entry["sha256"], relative
print(f"DBF8 {len(seen)} source/evidence files verified at {manifest['content_head']}")
