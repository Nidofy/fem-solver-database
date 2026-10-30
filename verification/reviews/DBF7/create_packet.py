"""Bind canonical committed source/evidence and optionally create a verified zip."""
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
    files = git("ls-tree", "-r", "--name-only", head).decode().splitlines()
    prefixes = ("include/", "src/", "tests/", "examples/", "benchmarks/", "cmake/", "tools/", "python/",
                "docs/development/db_foundation/", "verification/reviews/DBF6/", "verification/reviews/DBF7/")
    selected = [p for p in files if (p.startswith(prefixes) or p in (
        "CMakeLists.txt", "README.md", ".gitignore", ".gitattributes", "docs/development/TASK_POOL.yaml",
        "docs/development/CONTINUOUS_STATUS.md")) and p != "verification/reviews/DBF7/manifest.json"]
    entries = []
    for name in selected:
        data = git("show", head+":"+name)
        local = (root/name).read_bytes()
        entry = {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        if local != data:
            assert local.replace(b"\r\n", b"\n") == data, name
            entry["allow_crlf_normalization"] = True
        entries.append(entry)
    manifest = {"version": 1, "baseline": "b489414df6f1c45fdcb7b7b54b9f725e4c12ac4a", "content_head": head,
                "scope": "DBF6+DBF7 standalone; see scoped independent reviews and DBF7_HANDOFF", "files": entries}
    (packet/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(f"Bound {len(entries)} files to {head}")
if args.archive:
    manifest = json.loads((packet/"manifest.json").read_text(encoding="utf-8"))
    with zipfile.ZipFile(args.archive, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for entry in manifest["files"]:
            data = git("show", manifest["content_head"]+":"+entry["path"])
            assert hashlib.sha256(data).hexdigest() == entry["sha256"]
            archive.writestr(entry["path"], data)
        archive.write(packet/"manifest.json", "verification/reviews/DBF7/manifest.json")
    with zipfile.ZipFile(args.archive) as archive:
        assert archive.testzip() is None
        for entry in manifest["files"]:
            assert hashlib.sha256(archive.read(entry["path"])).hexdigest() == entry["sha256"]
    print("Verified archive", args.archive, hashlib.sha256(args.archive.read_bytes()).hexdigest())
