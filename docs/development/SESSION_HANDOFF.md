# Session handoff (minimal trusted context for a fresh GLM thread)

A fresh session MUST re-verify against real Git state before trusting this
file: `git status --porcelain=v1 -b`, `git log --oneline -12`, task pool.

## Current position (2026-10-25, after round-2 incremental review + repair)

- **B01 round-2 repaired, pending round-3 targeted re-review gate.** Branch `dev/glm-b01`.
- Round-1: FIX_HEAD `02e1ee4`. Incremental review: A FAIL (A-002/A-004
  NOT_CLOSED; new A-005/A-006), B PASS (new O-004 C3). Round-2 fixes:
  `23b8bc5`/`3c1f2c2`/`b4be2a5`/`994a607` + packet `d6c67de`
  (= NEW_FIX_HEAD; `037b809`+ docs-only). Round-3 scope in
  `verification/reviews/B01/repair-report.md` (A: identity tool + timing
  semantics + table consistency; B: identity changes + O-004 provenance).
- Identity gate now requires external pinning, e.g.:
  `python verification/validate_batch_identity.py verification/reviews/B01/manifest.yaml --expect-base aaa74af... --expect-head 719c3fb... --expect-freeze b58dfff...`
  (full SHAs in manifest; replay controls:
  `python verification/reviews/B01/task-evidence/identity-controls.py`).
- Reviewer report copies: provenance rule in
  `verification/reviews/B01/report-provenance.md` (original vs CRLF→LF blob
  hashes; record both, never claim plain hash equality for CRLF originals).
  Reviewed object (unchanged by repair): `719c3fb` (full
  `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`); freeze packet `b58dfff`.
- Repair series (no runtime solver code changed): `97b1b8a` identity
  (A-002/O-001), `816fb8d` execution-policy/retraction (A-001/O-002),
  `5f66b7c` sanitizer relabel (A-003/O-003), `d5cad6c` phase fields + errata
  (A-004), `02e1ee4` findings/repair packet (= FIX_HEAD_SHA). Any commit
  after `02e1ee4` on this branch is docs/ledger bookkeeping only.
- Reviewers were two mutually isolated GPT-6 Astra instances (Reviewer B
  user-authorized to substitute for unavailable Opus; `opus-review.md`
  filename is workflow compatibility only, no Claude/Opus participation
  claimed). Reports are verbatim copies in `verification/reviews/B01/` with
  SHA-256 on record.
- Gate status: **REPAIRED_PENDING_GATE** — awaiting the coordinators'
  incremental re-review of `719c3fb..02e1ee4` (scope in
  `verification/reviews/B01/repair-report.md`). Until accepted: no stable
  merge, no B02 start on this branch.
- **B02c candidate** remains frozen separately: `dev/glm-b02-candidate`
  (worktree `../sdb-b02c`), base `aaa74af`, head `b87fb53`, zero overlap with
  B01; optional independent review; droppable. Key result: View-vs-raw
  overhead ≤1.3% serial / noise under OpenMP — no fast-path justified.

## What a resumed GLM should do (depends on what the user returns)

- **Gate passes + user authorizes merge**: merge accepted B01 exactly per the
  user's instructions (never push/merge protected branches without explicit
  authorization), then start B02 per `06_GLM53_RESUME_PROMPT.md`
  (pool: R1.03, R1.04, R2.04 — R2.04's fast-path half likely resolves to
  "not justified" per B02c's measurement; confirm from that branch's
  docs/development/b02c/ evidence).
- **New findings from incremental re-review**: repeat the repair loop
  (append to findings.md → minimal fix → regression → repair-report update).
- **If granted EXTENDED_IDLE again**: only isolated, non-overlapping,
  from-last-accepted work in a separate worktree (same rules as B02c).

## Environment facts (verified 2026-10-24)

- Build (from repo root, Git Bash):

```sh
export PATH="/d/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin:$PATH"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
"$CMAKE" -S . -B build-b01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON \
  -DCMAKE_CXX_COMPILER="D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe" \
  -DCMAKE_MAKE_PROGRAM="$NINJA"
"$NINJA" -C build-b01 && "$CMAKE" --build build-b01 --target test
```

- MSVC variant: **AddressSanitizer only** (`SDB_SANITIZE=ON`; the MSVC branch
  has no UBSan; MinGW ships no sanitizer runtimes at all). Build/test flow in
  `verification/reviews/B01/build-test.md`.
- Identity gate for every future packet:
  `python verification/validate_batch_identity.py <manifest.yaml>`
  (positive+negative controls proven 2026-10-24).
- No NVIDIA GPU / CUDA toolkit on this host → CUDA tasks stay BLOCKED_ENV.
- `AGENTS.md` does not exist in this repo. clang-format:
  `"/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe" --style=file -i <files>`.
- The lifecycle benchmark always executes SERIAL (CpuRun has no policy
  parameter) — never draw parallel conclusions from it; every JSON row now
  carries `execution_policy:"serial"`.
- Exact-count test conventions live in tests/lifecycle_tests.cpp blocks
  labeled "R1.01" and "R1.02" — read before changing `publish()`/`advance()`.

## Working agreements (unchanged)

- Atomic commit per task/fix; update `docs/development/*` and
  `verification/reviews/<BATCH>/` after each unit.
- Never mark ACCEPTED before external review; no force-push/reset --hard;
  reviewer report files are verbatim copies only; CUDA stays BLOCKED_ENV
  here; no push/merge of protected branches without explicit user
  authorization.
