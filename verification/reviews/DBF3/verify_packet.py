"""Verify fixed Git content or the LF-canonical exported DBF3 packet."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
packet = Path(__file__).resolve().parent
manifest = json.loads((packet / "manifest.json").read_text(encoding="utf-8"))
head = manifest["content_head"]
use_git = sys.argv[1:] == ["--git"]
assert not sys.argv[1:] or use_git, "usage: verify_packet.py [--git]"
if use_git:
    subprocess.run(["git", "merge-base", "--is-ancestor", manifest["base"], head], cwd=root, check=True)
seen = set()
for entry in manifest["files"]:
    relative = entry["path"]
    assert not Path(relative).is_absolute() and ".." not in Path(relative).parts and relative not in seen
    seen.add(relative)
    path = (root / relative).resolve()
    assert path.is_relative_to(root), relative
    raw = subprocess.check_output(["git", "show", head+":"+relative], cwd=root) if use_git else path.read_bytes()
    if not use_git and entry.get("allow_crlf_normalization", False) and hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raw = raw.replace(b"\r\n", b"\n")
    assert len(raw) == entry["bytes"] and hashlib.sha256(raw).hexdigest() == entry["sha256"], relative
if use_git:
    tracked = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", head], cwd=root, text=True).splitlines()
    required = {p for p in tracked if p.startswith(("include/", "src/", "tests/", "examples/", "benchmarks/", "cmake/")) or p == "CMakeLists.txt"}
    assert required <= seen, sorted(required-seen)
print(f'DBF3 {head}: {len(seen)} fixed content files verified; mode={"git" if use_git else "files"}')
