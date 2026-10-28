"""Machine-replayable batch identity validator.

Round-2 repair of B01-A-002 / B01-A-005, round-3 repair of B01-R3-001:

- R3-001: only column-0 lines can define identity fields — indented lines
  (nested mappings, literal/folded block-scalar text) never do, and quoted
  identity keys are normalized into the same duplicate-checked namespace
  instead of being silently skipped.
- A-005: parsing is strict. Values are captured as raw strings first, so a
  present-but-invalid field FAILS instead of being silently dropped. Distinguishes
  "field missing" (error: the batch-freeze contract requires it) from
  "field present but empty/invalid" (error with the offending value). Duplicate
  keys are an error. Quoted values are explicitly handled: surrounding single
  or double quotes are stripped and the inner value is validated (a quoted
  40-hex id is accepted; a quoted non-hex string is rejected).
- A-002: callers can pin the externally fixed identities via --expect-base /
  --expect-head / --expect-freeze. The manifest must match those expectations
  exactly; the check never takes its expectation from the manifest under test.
  There is no HEAD fallback any more: freeze is required and validated.

Checks, for a manifest <path> against --git-dir:
1. strict parse (see above); batch_base_sha, batch_content_head_sha and
   freeze_packet_sha are all required, full 40-hex object ids.
2. each resolves to an existing commit object (`git cat-file -e <sha>^{commit}`).
3. base is an ancestor of head; head is an ancestor of freeze.
4. `git diff --name-only head..freeze` touches no source path (the reviewed
   code identity must stay the head; the freeze commit adds only packet/docs).
5. any --expect-* value equals the manifest value.

Usage:
  python validate_batch_identity.py <manifest.yaml> [--git-dir <path>]
      [--expect-base <sha>] [--expect-head <sha>] [--expect-freeze <sha>]
Exit 0 = identity consistent and as expected; nonzero with a diagnostic line.
"""
import argparse
import pathlib
import re
import subprocess
import sys

SOURCE_PREFIXES = ("include/", "src/", "tests/", "benchmarks/", "examples/",
                   "CMakeLists.txt", "cmake/")
KNOWN_KEYS = ("batch_base_sha", "batch_content_head_sha", "freeze_packet_sha")


def git(git_dir: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", git_dir, *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def fail(message: str) -> None:
    print(f"IDENTITY FAIL: {message}")
    sys.exit(1)


def strip_quotes(raw: str) -> str:
    value = raw.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1].strip()
    return value


def load_manifest_strict(path: pathlib.Path) -> dict:
    """Parse top-level `key: value` lines strictly for KNOWN_KEYS.

    Top level means column 0: any indented line belongs to a nested mapping or
    a literal/folded block scalar and can never define an identity field
    (B01-R3-001 — the earlier form stripped indentation first, so block-scalar
    text like `notes: |` containing `freeze_packet_sha: …` faked a required
    field). Quoted keys (`"key":` / `'key':`) are normalized to the bare name
    and take part in the SAME duplicate detection — never silently skipped.
    This is a narrow, single-line key syntax, not a general YAML parser:
    double-quoted keys containing escapes and malformed quoted keys are
    rejected, including unknown keys, before identity-name filtering (R4-001).
    Single-quoted backslashes are literal, as in YAML, not escape sequences.

    Returns {key: raw-value-after-quote-strip}. Raises SystemExit via fail() on
    duplicate keys, empty values, or a missing required key. Comment lines and
    unknown keys are ignored.
    """
    seen: dict[str, str] = {}
    for lineno, raw_line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), start=1):
        if not raw_line or raw_line[0] in " \t":
            continue  # blank or indented: nested mapping / block scalar content
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith(('"', "'")):
            m = (re.match(r'^"([^"\\]+)"\s*:\s*(.*)$', stripped)
                 if stripped[0] == '"' else
                 re.match(r"^'([^']+)'\s*:\s*(.*)$", stripped))
            if not m:
                fail(f"line {lineno}: unsupported quoted key syntax "
                     "(keys must be single-line; double-quoted escapes are unsupported)")
            key, raw = m.group(1), m.group(2)
        else:
            m = re.match(r"^([A-Za-z0-9_]+)\s*:\s*(.*)$", stripped)
            if not m:
                continue
            key, raw = m.group(1), m.group(2)
        if key not in KNOWN_KEYS:
            continue
        if key in seen:
            fail(f"line {lineno}: duplicate identity key '{key}' "
                 f"(first value {seen[key]!r}, repeated as {raw!r})")
        if not strip_quotes(raw):
            fail(f"line {lineno}: '{key}' is present but empty")
        seen[key] = strip_quotes(raw)
    missing = [k for k in KNOWN_KEYS if k not in seen]
    if missing:
        fail(f"missing required identity field(s): {', '.join(missing)} "
             f"(the batch-freeze contract requires base, content head and freeze packet)")
    return seen


def check_commit_id(git_dir: str, name: str, sha: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        fail(f"{name} is not a full 40-hex lowercase object id: {sha!r}")
    if git(git_dir, "cat-file", "-e", f"{sha}^{{commit}}").returncode != 0:
        fail(f"{name} does not resolve to a commit object: {sha}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest")
    parser.add_argument("--git-dir", default=".")
    parser.add_argument("--expect-base", help="externally fixed expected batch_base_sha")
    parser.add_argument("--expect-head", help="externally fixed expected batch_content_head_sha")
    parser.add_argument("--expect-freeze", help="externally fixed expected freeze_packet_sha")
    args = parser.parse_args()
    manifest_path = pathlib.Path(args.manifest)
    if not manifest_path.is_file():
        fail(f"manifest not found: {manifest_path}")
    values = load_manifest_strict(manifest_path)
    base = values["batch_base_sha"]
    head = values["batch_content_head_sha"]
    freeze = values["freeze_packet_sha"]
    for name, expected, actual in (("batch_base_sha", args.expect_base, base),
                                   ("batch_content_head_sha", args.expect_head, head),
                                   ("freeze_packet_sha", args.expect_freeze, freeze)):
        if expected is not None and expected.lower() != actual.lower():
            fail(f"{name} {actual} does not match the externally fixed expectation {expected}")
    check_commit_id(args.git_dir, "batch_base_sha", base)
    check_commit_id(args.git_dir, "batch_content_head_sha", head)
    check_commit_id(args.git_dir, "freeze_packet_sha", freeze)
    if git(args.git_dir, "merge-base", "--is-ancestor", base, head).returncode != 0:
        fail(f"base {base} is not an ancestor of head {head}")
    if git(args.git_dir, "merge-base", "--is-ancestor", head, freeze).returncode != 0:
        fail(f"head {head} is not an ancestor of freeze {freeze}")
    diff = git(args.git_dir, "diff", "--name-only", head, freeze)
    if diff.returncode != 0:
        fail(f"cannot diff head..freeze: {diff.stderr.strip()}")
    tainted = [f for f in diff.stdout.splitlines() if f.startswith(SOURCE_PREFIXES)]
    if tainted:
        fail(f"commits {head}..{freeze} change source paths, so the frozen code "
             f"identity is not {head}: {', '.join(tainted[:10])}")
    print(f"IDENTITY OK: base={base} head={head} freeze={freeze}")
    sys.exit(0)


if __name__ == "__main__":
    main()
