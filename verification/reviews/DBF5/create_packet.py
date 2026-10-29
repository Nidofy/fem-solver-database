"""Author a Git-bound manifest or export its canonical bytes; no build artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parents[3]
packet = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--content-head")
parser.add_argument("--archive", type=Path)
args = parser.parse_args()

def git(*values):
    return subprocess.check_output(["git", *values], cwd=root)

if args.content_head:
    head = git("rev-parse", args.content_head).decode().strip()
    paths = git("ls-tree", "-r", "--name-only", head).decode().splitlines()
    prefixes = ("include/", "src/", "tests/", "examples/", "benchmarks/", "cmake/",
                "docs/development/db_foundation/", "verification/reviews/DBF5/")
    selected = [p for p in paths if (p.startswith(prefixes) or p in ("CMakeLists.txt", "README.md", ".gitignore"))
                and not p.endswith("/manifest.json") and "/scratch/" not in p]
    records = []
    for relative in selected:
        data = git("show", head + ":" + relative)
        local = (root / relative).read_bytes()
        entry = {"path": relative, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        if local != data:
            assert local.replace(b"\r\n", b"\n") == data, relative
            entry["allow_crlf_normalization"] = True
        records.append(entry)
    manifest = {"base": "a9a203c67d78ed8649e12e05c15214ae2b95ea63",
                "review_head": "7ea0bf312619b019aa6dcd69f03731163e6ec6d4",
                "test_head": "7ea0bf312619b019aa6dcd69f03731163e6ec6d4",
                "content_head": head, "files": records}
    (packet / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Manifest: {len(records)} canonical files at {head}")
if args.archive:
    manifest = json.loads((packet / "manifest.json").read_text(encoding="utf-8"))
    # Exclusive creation prevents overwriting an earlier handoff.
    with zipfile.ZipFile(args.archive, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry in manifest["files"]:
            data = git("show", manifest["content_head"] + ":" + entry["path"])
            assert len(data) == entry["bytes"] and hashlib.sha256(data).hexdigest() == entry["sha256"]
            archive.writestr(entry["path"], data)
        archive.write(packet / "manifest.json", "verification/reviews/DBF5/manifest.json")
    with zipfile.ZipFile(args.archive) as archive:
        assert archive.testzip() is None
        for entry in manifest["files"]:
            assert hashlib.sha256(archive.read(entry["path"])).hexdigest() == entry["sha256"]
    print(f"Archive verified: {args.archive} sha256={hashlib.sha256(args.archive.read_bytes()).hexdigest()}")
