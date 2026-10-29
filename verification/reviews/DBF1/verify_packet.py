"""Validate fixed content blobs (--git) or LF-normalized archive/worktree files."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
packet = json.loads((root / 'verification/reviews/DBF1/manifest.json').read_text())
base = '9a3d754dbbc5c0b1720a93ecf29ee3278f705f73'
head = 'c5cd0622d7a52fb7c15db0e98dc33b5833d206a7'
assert packet['base'] == base and packet['content_head'] == head
use_git = sys.argv[1:] == ['--git']
assert not sys.argv[1:] or use_git, 'usage: verify_packet.py [--git]'
if use_git:
    subprocess.run(['git', 'merge-base', '--is-ancestor', base, head], cwd=root, check=True)
for entry in packet['files']:
    path = entry['path']
    assert not Path(path).is_absolute() and '..' not in Path(path).parts
    raw = (subprocess.check_output(['git', 'show', head + ':' + path], cwd=root)
           if use_git else (root / path).read_bytes())
    digest = hashlib.sha256(raw).hexdigest()
    if not use_git and digest != entry['git_blob_sha256']:
        raw.decode('utf-8')  # never normalize a binary artifact
        raw = raw.replace(b'\r\n', b'\n')
        digest = hashlib.sha256(raw).hexdigest()
    assert len(raw) == entry['bytes'] and digest == entry['git_blob_sha256'], path
print(f"DBF1 {head}: {len(packet['files'])} fixed content files verified; mode={'git blobs' if use_git else 'files (LF canonical)'}")
