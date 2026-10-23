# Session handoff (minimal trusted context for a fresh GLM thread)

A fresh session MUST re-verify against real Git state before trusting this
file: `git status --porcelain=v1 -b`, `git log --oneline -8`, task pool.

## Current position (2026-10-24, end of session)

- **B01 frozen for review** on `dev/glm-b01`: reviewed code range
  `aaa74af..719c3fb`; packet/ledger commits after it (`b58dfff`+). Packet:
  `verification/reviews/B01/` (start at manifest.yaml).
- **B02c candidate frozen** on `dev/glm-b02-candidate` (worktree
  `../sdb-b02c`), base `aaa74af`, head `b87fb53`: R2.04a view-vs-raw fair
  statistics + layout correctness tests; zero file overlap with B01;
  can be reviewed separately or dropped.
- What is in B01: baseline/inventory (R0.01), 3-config test matrix (R0.02),
  ledger (R0.03), lazy publication R1.01 (core change:
  `include/sdb/results/results.hpp` + counter in `dynamics.hpp` + `save()`
  consistency eval in `lifecycle.hpp`), time-layer contract tests R1.02,
  segmented benchmarks + eager/lazy A/B (R0.04), CUDA BLOCKED_ENV (R0.05).
- All evidence: `verification/reviews/B01/` (manifest.yaml first).

## What a resumed GLM should do (depends on what the user returns)

- **If Astra/Opus findings exist**: follow `05_GLM53_REVIEW_FIX_PROMPT.md` —
  build the findings ledger `findings.md`, fix C0/C1 first, atomic fix
  commits, targeted regression, update `repair-report.md`, set
  `REPAIRED_PENDING_GATE`.
- **If batch accepted & merged to stable**: start B02 from new stable HEAD
  per `06_GLM53_RESUME_PROMPT.md` — pool suggests R1.03 + R1.04 + R2.04.
- **If granted EXTENDED_IDLE**: only independent, non-overlapping low-risk
  tasks in a SEPARATE worktree from the last accepted SHA (never on top of
  the frozen unreviewed tree). Candidate: R2.04 raw-vs-view benchmarks.

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

- MSVC ASan variant: `verification/reviews/B01/build-test.md` (vcvars64
  -vcvars_ver=14.44, SDB_SANITIZE=ON, build-b01-asan).
- No NVIDIA GPU / CUDA toolkit on this host → CUDA tasks stay BLOCKED_ENV.
- `AGENTS.md` does not exist in this repo. clang-format:
  `"/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe" --style=file -i <files>`
- Exact-count test conventions live in tests/lifecycle_tests.cpp blocks
  labeled "R1.01" and "R1.02" — read before changing `publish()`/`advance()`.

## Working agreements (unchanged)

- Atomic commit per task; update `docs/development/*` and
  `verification/reviews/<BATCH>/task-evidence/<ID>.md` after each task.
- Never mark ACCEPTED before external review; no force-push/reset --hard;
  never author astra-review.md / opus-review.md; CUDA stays BLOCKED_ENV here.
