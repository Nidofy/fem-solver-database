# Continuous development status

Updated after every task. Cross-session handoff details: SESSION_HANDOFF.md.

- Current batch: **B01** (reproducible baseline + output publication correctness)
- Batch base SHA: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`
- Working branch: `dev/glm-b01`
- Batch state: IN_PROGRESS (not yet FROZEN_FOR_BATCH_REVIEW)
- Latest commit: see `git log dev/glm-b01 -1`

## Task states (summary; authoritative list in TASK_POOL.yaml)

| Task | Status | Commit | Notes |
|---|---|---|---|
| R0.01 baseline | IMPLEMENTED_SELF_TESTED | bef76e3 | inventory + anchors verified from source |
| R0.02 build matrix | IMPLEMENTED_SELF_TESTED | a3779f0 | MinGW OMP/serial + MSVC ASan, 8/8 each |
| R0.03 ledger | IN_PROGRESS | — | this file + TASK_POOL.yaml scaffold |
| R0.04 benchmarks | PLANNED | — | |
| R0.05 CUDA probe | PLANNED | — | nvcc/nvidia-smi absent → expect BLOCKED_ENV |
| R1.01 lazy publish | PLANNED | — | anchor: ResultPublisher::publish forces-before-due |
| R1.02 time-layer contract | PLANNED | — | |

## Last full regression

- 2026-10-24, commit a3779f0: CTest 8/8 PASS on build-b01 (MinGW Release+OMP),
  build-b01-serial (serial), build-b01-asan (MSVC ASan). See
  verification/reviews/B01/task-evidence/R0.02.md.
