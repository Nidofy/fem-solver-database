"""Verify fixed Git content or the LF-canonical exported source packet."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
packet = json.loads((Path(__file__).parent / "manifest.json").read_text())
head = packet["content_head"]
use_git = sys.argv[1:] == ["--git"]
assert not sys.argv[1:] or use_git, "usage: verify_packet.py [--git]"
if use_git:
    subprocess.run(["git", "merge-base", "--is-ancestor", packet["base"], head], cwd=root, check=True)
seen = set()
for entry in packet["files"]:
    path = entry["path"]
    assert not Path(path).is_absolute() and ".." not in Path(path).parts and path not in seen
    seen.add(path)
    raw = subprocess.check_output(["git", "show", head + ":" + path], cwd=root) if use_git else (root / path).read_bytes()
    if not use_git and hashlib.sha256(raw).hexdigest() != entry["sha256"]:
        raw.decode("utf-8")
        raw = raw.replace(b"\r\n", b"\n")
    assert len(raw) == entry["bytes"] and hashlib.sha256(raw).hexdigest() == entry["sha256"], path
if use_git:
    tracked = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", head], cwd=root, text=True).splitlines()
    required = {p for p in tracked if p.startswith(("include/", "src/", "tests/", "examples/", "benchmarks/", "cmake/")) or p == "CMakeLists.txt"}
    assert required <= seen, sorted(required - seen)
print(f"DBF2 {head}: {len(seen)} fixed content files verified; mode={'git' if use_git else 'files'}")
