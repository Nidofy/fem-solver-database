"""Check the immutable content revision and retained Git-blob evidence hashes."""
import hashlib
import json
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[3]
packet = Path(__file__).with_name("manifest.json")
manifest = json.loads(packet.read_text(encoding="utf-8"))


def git(*args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


base = manifest["base_sha"]
head = manifest["content_head_sha"]
assert base == "480da32a17e3b60e6694b95e5c9ec83618e5b64b"
assert head == "8122adf2c149a672222ab81d8ac722e69931205c"
subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", base, head], check=True)
changed = set(git("diff", "--name-only", base, head).decode().splitlines())
assert changed == {item["path"] for item in manifest["content_files"]}
for item in manifest["content_files"]:
    data = git("show", f"{head}:{item['path']}")
    assert hashlib.sha256(data).hexdigest() == item["git_blob_sha256"], item["path"]
assert manifest["state"] == "FROZEN_FOR_BATCH_REVIEW"
assert all(item["exit_code"] == 0 for item in manifest["final_tests"])
print(f"PASS: immutable content identity and {len(changed)} Git-blob hashes; no independent acceptance implied")
