# Continuous development status

Updated after every task. Cross-session handoff details: SESSION_HANDOFF.md.

- Current batch: **B01** — **FROZEN_FOR_BATCH_REVIEW** (2026-10-24)
- Batch base SHA: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`
- Batch content HEAD (all code/tests/benchmarks): `719c3fb`
- Freeze/packet commit: `b58dfff`
- Working branch: `dev/glm-b01` (freeze commit adds only packet/ledger files)
- Review packet: `verification/reviews/B01/` (manifest, review-packet,
  build-test, numerical-validation, compat-matrix, open-risks, task evidence,
  benchmark raw JSONL + summary)
- Astra/Opus reports: **pending** (to be provided externally; GLM must not
  author them)

## Task states (summary; authoritative list in TASK_POOL.yaml)

| Task | Status | Commit | Notes |
|---|---|---|---|
| R0.01 baseline | FROZEN_FOR_BATCH_REVIEW | bef76e3 | inventory + anchors |
| R0.02 build matrix | FROZEN_FOR_BATCH_REVIEW | a3779f0 | 3 configs × 8/8 |
| R0.03 ledger | FROZEN_FOR_BATCH_REVIEW | 12ffe5b | |
| R1.01 lazy publish | FROZEN_FOR_BATCH_REVIEW | fcc3c80 | 4002→2022 evals; bytes identical |
| R1.02 time-layer contract | FROZEN_FOR_BATCH_REVIEW | 0447789 | 14284 checks |
| R0.04 benchmarks | FROZEN_FOR_BATCH_REVIEW | d83ee85 | eager/lazy A/B |
| R0.05 CUDA probe | BLOCKED_ENV | 719c3fb | no NVIDIA device/toolkit |

## Last full regression (at content HEAD 719c3fb, 2026-10-24)

- build-b01 (MinGW Release+OMP): CTest 8/8 PASS.
- build-b01-serial: CTest 8/8 PASS.
- build-b01-asan (MSVC, AddressSanitizer only; UBSan not executed on this
  host): CTest 8/8 PASS, no ASan findings.
- sdb_run byte-outputs identical to pre-change binaries (double/float/U-only;
  restart at stop 700 and 733).

## Next steps (external)

1. User sends `03_ASTRA_BATCH_REVIEW_PROMPT.md` and
   `04_OPUS_BATCH_REVIEW_PROMPT.md` against frozen range
   `aaa74af..719c3fb` (+ packet commit) to both reviewers independently.
2. Reports returned → GLM runs `05_GLM53_REVIEW_FIX_PROMPT.md` flow
   (findings ledger, C0/C1 first, targeted re-review).
3. Accepted → merge to stable, open B02 (R1.03/R1.04/R2.04).

## EXTENDED_IDLE candidate (produced under masterplan §3 rules)

- **B02c** on branch `dev/glm-b02-candidate` (worktree `../sdb-b02c`),
  based on the stable SHA `aaa74af` — NOT stacked on unreviewed B01 code.
  Single commit `b87fb53`: R2.04a raw-vs-View fair statistics + cross-layout
  View correctness tests. Zero file overlap with B01 (touches
  tests/tests.cpp, benchmarks/benchmark.cpp, docs/development/b02c/ only).
  Key measured finding: View overhead vs raw ≤1.3% serial / within noise
  under OMP at 1M nodes — the assumed "View/raw bottleneck" is not
  reproduced; a raw fast-path optimization would be premature. Review this
  candidate as its own immutable snapshot (`aaa74af..b87fb53`) if desired;
  it can also be dropped freely (no B01 dependency).
