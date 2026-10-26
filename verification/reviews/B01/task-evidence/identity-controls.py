"""Replayable identity-validator controls (round-2 repair evidence).

Covers the original positive/negative controls, the A-002 expected-identity
mismatch case, the A-005 fail-open replay on a real BATCH_HEAD checkout, and
the parsing edges (missing/empty/quoted/duplicate keys).

Run from the repository root:
  python verification/reviews/B01/task-evidence/identity-controls.py

Writes identity-controls.json next to this script and prints a PASS/FAIL line
per control. Exits 0 only if every control produced its expected outcome.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
REPO = HERE.parents[3]
VALIDATOR = REPO / "verification" / "validate_batch_identity.py"
MANIFEST = REPO / "verification" / "reviews" / "B01" / "manifest.yaml"

BASE = "aaa74afb3c264bbd7ca43555a7445c77994ccaa2"
HEAD = "719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5"
FREEZE = "b58dfff04b484f2ed15524d51883e5f979fd1bc1"
BAD = "719c3fb2a5cd3f6d8a8b13eec1b8a7c1d59dd1a4"


def run(manifest_text: str, git_dir: str, expect: dict | None = None) -> tuple[int, str]:
    with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False, encoding="utf-8",
                                      dir=tempfile.gettempdir()) as f:
        f.write(manifest_text)
        path = f.name
    cmd = [sys.executable, str(VALIDATOR), path, "--git-dir", git_dir]
    for key in ("base", "head", "freeze"):
        if expect and expect.get(key):
            cmd += [f"--expect-{key}", expect[key]]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    pathlib.Path(path).unlink()
    return r.returncode, (r.stdout + r.stderr).strip()


def main() -> int:
    original = MANIFEST.read_text(encoding="utf-8")
    # Real BATCH_HEAD detached clone: the A-005 fail-open replay must not rely
    # on incidental diffs in the working repository.
    batch_head = pathlib.Path(tempfile.mkdtemp(prefix="sdb-b01-batch-head-"))
    subprocess.run(["git", "clone", "--shared", "--no-checkout", str(REPO), str(batch_head)],
                   capture_output=True, check=True)
    subprocess.run(["git", "-C", str(batch_head), "checkout", "--detach", HEAD],
                   capture_output=True, check=True)
    controls = []

    def case(name, text, git_dir, expected_exit, expect=None, note=""):
        exit_code, output = run(text, git_dir, expect)
        controls.append({"case": name, "expected_exit": expected_exit, "actual_exit": exit_code,
                         "output": output, "git_dir_kind": "dev" if git_dir == str(REPO) else "batch-head",
                         "note": note})
        ok = "PASS" if exit_code == expected_exit else "FAIL"
        print(f"{ok} {name}: exit {exit_code} (expected {expected_exit})")

    pos_expect = {"base": BASE, "head": HEAD, "freeze": FREEZE}
    case("positive-with-expected-identity", original, str(REPO), 0, pos_expect,
         "real manifest + externally pinned base/head/freeze")
    case("positive-at-batch-head-checkout", original, str(batch_head), 0, pos_expect,
         "same, against the detached BATCH_HEAD fixture")
    case("bad-original-sha", original.replace(HEAD, BAD), str(REPO), 1,
         note="first-round nonexistent object")
    case("abbreviated-head", original.replace(HEAD, "719c3fb"), str(REPO), 1,
         note="short id must be rejected")
    case("base-not-ancestor", original.replace(f"batch_base_sha: {BASE}",
                                               f"batch_base_sha: {FREEZE}"), str(REPO), 1)
    case("freeze-before-head", original.replace(f"freeze_packet_sha: {FREEZE}",
                                                f"freeze_packet_sha: {BASE}"), str(REPO), 1)
    case("tainted-freeze", original.replace(f"freeze_packet_sha: {FREEZE}",
                                            "freeze_packet_sha: 420d5b74c451ad1bfcc41dd873939dbe3c103675"),
         str(REPO), 1, note="packet commit changes docs only; docs tip has benchmark source changes")
    case("different-valid-head-generic", original.replace(f"batch_content_head_sha: {HEAD}",
                                                          f"batch_content_head_sha: {FREEZE}"),
         str(REPO), 0, note="A-002: generically consistent but WRONG batch object — this is why "
                            "callers must always pass --expect-head")
    case("different-valid-head-with-expect", original.replace(f"batch_content_head_sha: {HEAD}",
                                                              f"batch_content_head_sha: {FREEZE}"),
         str(REPO), 1, pos_expect, note="A-002 closure: mismatch vs externally fixed head")
    case("expect-base-mismatch", original, str(REPO), 1, {"base": FREEZE, "head": HEAD, "freeze": FREEZE})
    case("a005-invalid-freeze-at-batch-head", original.replace(f"freeze_packet_sha: {FREEZE}",
                                                               "freeze_packet_sha: invalid-sha"),
         str(batch_head), 1, note="A-005 replay at real BATCH_HEAD checkout: fail-open must now fail")
    case("a005-invalid-freeze-dev-tree", original.replace(f"freeze_packet_sha: {FREEZE}",
                                                          "freeze_packet_sha: invalid-sha"),
         str(REPO), 1, note="same on dev tree; failure must come from the field, not incidental diffs")
    case("freeze-missing", original.replace(f"freeze_packet_sha: {FREEZE}\n", ""), str(REPO), 1,
         note="missing required field")
    case("freeze-empty-value", original.replace(f"freeze_packet_sha: {FREEZE}", "freeze_packet_sha:"),
         str(REPO), 1, note="present but empty")
    case("freeze-quoted-invalid", original.replace(f"freeze_packet_sha: {FREEZE}",
                                                   'freeze_packet_sha: "invalid-sha"'), str(REPO), 1,
         note="quoted values are unwrapped then validated")
    case("head-quoted-valid", original.replace(f"batch_content_head_sha: {HEAD}",
                                               f'batch_content_head_sha: "{HEAD}"'), str(REPO), 0,
         pos_expect, note="quoted well-formed 40-hex is accepted (documented rule)")
    case("duplicate-base-key", original + f"batch_base_sha: {FREEZE}\n", str(REPO), 1,
         note="duplicate identity keys are rejected, no silent override")

    (HERE / "identity-controls.json").write_text(json.dumps(controls, indent=2) + "\n", encoding="utf-8")
    shutil.rmtree(batch_head, ignore_errors=True)
    failed = [c["case"] for c in controls if c["actual_exit"] != c["expected_exit"]]
    print(f"controls: {len(controls) - len(failed)}/{len(controls)} behaved as expected")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
