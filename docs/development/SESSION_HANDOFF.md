# Session handoff (minimal trusted context for a fresh GLM thread)

A fresh session MUST re-verify against real Git state before trusting this
file: `git status --porcelain=v1 -b`, `git log --oneline -12`, task pool.

## Current position (2026-10-28, after round-4 review and direct repair)

- Branch: `dev/glm-b01`. B01 state: **REPAIRED_PENDING_GATE**. The independent
  round-4 report on `63b24fc14c728af03b1089c864bab95c5bfa8d9b` was FAIL;
  it is preserved, not rewritten into PASS.
- R4-001/C2 parser repair: `d01ea18cb3c8d0ce8a6a32fc6849969f9fb2bda0`. R4-002/C3 stale handoff refreshed in
  the subsequent documentation commit. Resolve current HEAD with Git;
  the code-fix SHA and documentation tip are distinct identities.
- Current implementer verification: official controls **50/50**; all original
  round-4 reviewer fixtures **26/26** (including the three previously accepted
  escaped duplicate keys, now exit 1). No fifth-round independent review exists.
- BATCH_BASE `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`;
  BATCH_HEAD `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`;
  freeze `b58dfff04b484f2ed15524d51883e5f979fd1bc1`.
- Repair milestones: round 1 `02e1ee4`, round 2 `d6c67de`, round 3 `63b24fc`,
  round 4 code fix above. Repair history includes validator/control code,
  documentation AND historical lifecycle benchmark timing changes. The solver
  runtime include/src/tests/examples/CMakeLists.txt/cmake stays byte-identical
  to BATCH_HEAD; this repair introduces no benchmark or schema changes.
- Round 1/2 used two isolated Astra reviewers (B substituted for unavailable
  Opus). Round 3/4 used one user-authorized merged Astra High reviewer.
  Report copies are LF-normalized content-equal; raw and Git-blob SHA-256
  are separately recorded in `verification/reviews/B01/report-provenance.md`.
- R3-002 CLOSED by round-4 reviewer. R4-001 and R4-002 are now developer
  FIXED_VERIFIED; independent closure is pending, not presumed.

## Next-development authorization and commands

The user explicitly requested direct repair and a prompt for GLM next-round
development. After verifying the fixed SHA and replaying controls, GLM may
start **isolated B02 candidate development** from the fixed B01 baseline.
Do not stall merely because the older template requires acceptance before
all development. This limited exception does NOT authorize stable merge,
push, or changing B01 to ACCEPTED without an actual acceptance decision.
Use the coordinator's `sdb-b01-r4-repair-handoff/GLM_NEXT_DEVELOPMENT_PROMPT.md`
for the exact frozen handoff tip and candidate-work instructions.

1. Verify branch/status/ancestry; preserve user changes. Read findings and
   repair-report round-4 section and the unmodified round-4 report.
2. Run `python verification/reviews/B01/task-evidence/identity-controls.py`.
   Always externally pin base/head/freeze when invoking the identity validator;
   generic consistency alone cannot identify the intended batch.
3. Reuse a suitable isolated B02 worktree if one exists; otherwise branch from
   the fixed handoff SHA. Freeze B01 while developing R1.03/R1.04, then assess
   R2.04 measurement evidence before proposing any specialized fast path.
4. B02c `dev/glm-b02-candidate`, base `aaa74af`, head `b87fb53`, remains a
   distinct frozen candidate, not accepted or automatically merged. Read its
   evidence if relevant; do not silently combine it with the new B02 branch.
5. Keep B01 formal acceptance and any stable integration separate from B02
   candidate implementation. No protected branch merge/push authorization.

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
  `python verification/validate_batch_identity.py <manifest.yaml> --expect-base <fixed-base> --expect-head <fixed-head> --expect-freeze <fixed-freeze>`
  (all values external constants; use full SHAs above).
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
  reviewer report files are provenance-recorded copies per report-provenance.md (never
  author or alter reviewer content); CUDA stays BLOCKED_ENV
  here; no push/merge of protected branches without explicit user
  authorization.
