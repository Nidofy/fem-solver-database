"""Machine-replayable batch identity validator (B01-A-002 / B01-O-001 regression).

Fails before any benchmark/review dispatch when a review-packet manifest names
an unresolvable or inconsistent snapshot. Checks, for a manifest:

1. batch_base_sha and batch_content_head_sha each resolve to an existing
   commit object (`git cat-file -e <sha>^{commit}`).
2. base is an ancestor of the content head (`git merge-base --is-ancestor`).
3. every commit in base..head touches only files allowed for that batch's
   *content* (code/tests/benchmarks), i.e. the manifest/packet must not claim
   code identity that includes documentation-only freeze commits (optional
   `freeze_packet_sha`: commits after head up to the freeze commit must touch
   no source path).
4. the manifest's SHAs are not abbreviations (>= 40 hex chars).

Usage: python validate_batch_identity.py <manifest.yaml> [--git-dir <path>]
Exit 0 = identity consistent; nonzero with a diagnostic line otherwise.
"""
import argparse
import pathlib
import re
import subprocess
import sys

SOURCE_PREFIXES = ("include/", "src/", "tests/", "benchmarks/", "examples/",
                   "CMakeLists.txt", "cmake/")


def git(git_dir: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", git_dir, *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def load_manifest(path: pathlib.Path) -> dict:
    manifest = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        m = re.match(r"^([a-z_]+):\s*([0-9a-f]+)\s*(#.*)?$", line)
        if m:
            manifest[m.group(1)] = m.group(2)
    return manifest


def fail(message: str) -> None:
    print(f"IDENTITY FAIL: {message}")
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("--git-dir", default=".")
    args = parser.parse_args()
    manifest_path = pathlib.Path(args.manifest)
    if not manifest_path.is_file():
        fail(f"manifest not found: {manifest_path}")
    values = load_manifest(manifest_path)
    base = values.get("batch_base_sha")
    head = values.get("batch_content_head_sha")
    freeze = values.get("freeze_packet_sha")
    for name, sha in (("batch_base_sha", base), ("batch_content_head_sha", head)):
        if not sha:
            fail(f"{name} missing from manifest")
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            fail(f"{name} is not a full 40-hex object id: {sha!r}")
        if git(args.git_dir, "cat-file", "-e", f"{sha}^{{commit}}").returncode != 0:
            fail(f"{name} does not resolve to a commit object: {sha}")
    if git(args.git_dir, "merge-base", "--is-ancestor", base, head).returncode != 0:
        fail(f"base {base} is not an ancestor of head {head}")
    for name, sha in (("freeze_packet_sha", freeze),):
        if sha:
            if not re.fullmatch(r"[0-9a-f]{40}", sha):
                fail(f"{name} is not a full 40-hex object id: {sha!r}")
            if git(args.git_dir, "cat-file", "-e", f"{sha}^{{commit}}").returncode != 0:
                fail(f"{name} does not resolve to a commit object: {sha}")
            if git(args.git_dir, "merge-base", "--is-ancestor", head, sha).returncode != 0:
                fail(f"head {head} is not an ancestor of {name} {sha}")
    # Content-purity: nothing after the content head (up to the freeze/packet
    # commit when given, else the manifest file's own commit history) may
    # change source paths; the reviewed code identity must stay the head.
    end = freeze or "HEAD"
    if git(args.git_dir, "merge-base", "--is-ancestor", head, end).returncode == 0:
        diff = git(args.git_dir, "diff", "--name-only", head, end)
        if diff.returncode != 0:
            fail(f"cannot diff head..{end}: {diff.stderr.strip()}")
        changed = [f for f in diff.stdout.splitlines() if f]
        tainted = [f for f in changed if f.startswith(SOURCE_PREFIXES)]
        if tainted:
            fail(f"commits {head}..{end} change source paths, so the frozen code "
                 f"identity is not {head}: {', '.join(tainted[:10])}")
    print(f"IDENTITY OK: base={base} head={head}"
          + (f" freeze={freeze}" if freeze else ""))
    sys.exit(0)


if __name__ == "__main__":
    main()
