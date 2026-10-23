# Session handoff (minimal trusted context for a fresh GLM thread)

A fresh session MUST re-verify against real Git state before trusting this
file: `git status --porcelain=v1 -b`, `git log --oneline -5`, and the task
pool below. Read `docs/02_GLM53_IDLE_AUTOPILOT_PROMPT.md` and
`docs/01_CONTINUOUS_DEVELOPMENT_MASTERPLAN.md` first.

## Current position

- Branch: `dev/glm-b01` (from base `aaa74af` = `codex/standalone-database` HEAD)
- Batch: **B01** in progress — tasks R0.01/R0.02 done, R0.03 ledger created.
- Next tasks in order: R1.01 (core: lazy force evaluation in
  `ResultPublisher::publish`, include/sdb/results/results.hpp), then R1.02,
  R0.04, R0.05, then freeze the batch (see masterplan §8 for the packet).

## Environment facts (verified 2026-10-24)

- Build (from repo root, Git Bash):

```sh
export PATH="/d/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin:$PATH"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
"$CMAKE" -S . -B build-b01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON \
  -DCMAKE_CXX_COMPILER="D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe" \
  -DCMAKE_MAKE_PROGRAM="$NINJA"
"$NINJA" -C build-b01
"$CMAKE" --build build-b01 --target test
```

- MSVC ASan variant: see `verification/reviews/B01/task-evidence/R0.02.md`
  (build-b01-asan; vcvars64 -vcvars_ver=14.44; SDB_SANITIZE=ON SDB_OPENMP=OFF).
- No nvcc / nvidia-smi → CUDA tasks are BLOCKED_ENV on this host.
- `AGENTS.md` does not exist in this repo.

## Key code anchors for the remaining B01 tasks

- R1.01: `include/sdb/results/results.hpp` `ResultPublisher::publish()` calls
  `cpu.forces(state)` before any `OutputScheduler::due()` check;
  `examples/sdb_run.cpp` `publish` lambda snapshots every increment.
- R1.02: `include/sdb/lifecycle.hpp` `CpuRun::next_step()` (velocity
  rephasing across steps/dt changes); `ResultPublisher::publish` provenance
  strings; existing exactness tests in `tests/lifecycle_tests.cpp`
  (`numerical<float/double>`: "restart append byte-identical results").
- R0.04: `benchmarks/lifecycle_benchmark.cpp`, `benchmarks/benchmark.cpp`,
  `verification/summarize_benchmarks.py` (JSONL + summary conventions).

## Working agreements (unchanged)

- Atomic commit per task; update `docs/development/TASK_POOL.yaml`,
  `CONTINUOUS_STATUS.md`, and `verification/reviews/B01/task-evidence/<ID>.md`.
- Never mark ACCEPTED before external review; never force-push or reset --hard;
  CUDA unverified on this host stays BLOCKED_ENV.
