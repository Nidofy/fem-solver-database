# B01 — merged findings ledger and dispositions

Sources (verbatim copies in this directory, hashes verified against the
user-provided values):

- `astra-review.md` — Reviewer A, GPT-6 Astra, `03_ASTRA_BATCH_REVIEW_PROMPT.md`.
  SHA-256 `ab3b58af…5bf6e0`. Verdict `CHANGES_REQUESTED / FAIL`.
- `opus-review.md` — Reviewer B, **GPT-6 Astra acting under explicit user
  authorization in place of the unavailable Opus**, `04_OPUS_BATCH_REVIEW_PROMPT.md`
  (filename kept for workflow compatibility; no Claude/Opus participation is
  claimed anywhere in this packet). SHA-256 `34e23484…d9e191`. Verdict
  `REQUEST_CHANGES / HOLD`.

Both reports reviewed the same immutable object
`719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5` (user-pinned short SHA `719c3fb`;
the manifest's original full-SHA string was wrong — finding 2 below).

## Ledger (original numbering preserved; merged root causes; dispositions after repair)

| Finding | Severity (orig.) | Root cause group | Disposition | Fix commit |
|---|---|---|---|---|
| B01-A-001 | C1 | #1 false parallel baseline | FIXED_VERIFIED | 816fb8d |
| B01-O-002 | C2 | #1 (same root cause, independent counter-example) | FIXED_VERIFIED (merged with A-001) | 816fb8d |
| B01-A-002 | C2 | #2 frozen identity | FIXED_VERIFIED | 97b1b8a |
| B01-O-001 | C1 | #2 (same root cause, independent counter-example) | FIXED_VERIFIED (merged with A-002) | 97b1b8a |
| B01-A-003 | C2 | #3 sanitizer overclaim | FIXED_VERIFIED | 5f66b7c |
| B01-O-003 | C2 | #3 (same root cause) | FIXED_VERIFIED (merged with A-003) | 5f66b7c |
| B01-A-004 | C2 | #4 benchmark phase semantics | FIXED_VERIFIED | d5cad6c |
| (Reviewer B errata) | C3-doc | #4 adjacent count errors in numerical-validation.md | FIXED_VERIFIED | d5cad6c |

No C0 was raised by either reviewer. Both explicitly found **no new physical
error in the B01 CPU TET4 change** (Reviewer A: "No C0 defect or new silent
physical error was found"; Reviewer B: "未发现失败反例…不建议仅因这些证据问题回退
lazy publication"). Severity disagreement between reviewers on root causes #1
and #2 was resolved by repairing at the higher (C1) priority, preserving both
original ratings in this table.

## Reproduction record (before fixing)

1. **#1**: source-level — `include/sdb/lifecycle.hpp:63` constructs
   `cpu_(model.mesh, state_)`; `include/sdb/dynamics.hpp:171` default
   `CpuPolicy::Serial`; config string `…serial/deterministic`
   (`lifecycle.hpp:84`). Reviewer A's link probe
   (`astra-evidence/policy.log`): `lifecycle_parallel_launches=0`,
   explicit OpenMP control `=1`. Confirmed: no lifecycle benchmark run can
   execute OpenMP; my original "OMP4 ≈ serial (bandwidth-bound)" statement was
   two serial runs under different env vars.
2. **#2**: `git cat-file -t 719c3fb2a5cd…` → exit 128 (object absent);
   `git rev-parse 719c3fb` → `719c3fbbe4db…`. Confirmed my freeze-time full
   hash was fabricated from memory.
3. **#3**: `CMakeLists.txt` MSVC branch (`/fsanitize=address /Zi` only) vs
   non-MSVC branch (`-fsanitize=address,undefined`). Confirmed MSVC run was
   ASan-only; additionally verified this host has no UBSan-capable runtime at
   all (MinGW ships neither libasan nor libubsan; MSVC has no UBSan mode).
4. **#4**: benchmark source inspection — `compile_ms` ends before
   `CpuRun` construction; `kernel_ms` times a full `advance()`; plan=21
   increments, executed=10, frames=1, none of which the JSON recorded.

## Regression additions (what now enforces each fix)

1. `benchmarks/lifecycle_benchmark.cpp` emits `execution_policy:"serial"`,
   `omp_compiled`, `omp_max_threads`, `omp_env` in every JSON row
   (mislabeling is now machine-visible); evidence docs corrected and the
   invalid diagnosis retracted (`benchmarks/README.md`,
   `task-evidence/R0.04.md`, `open-risks.md`); mislabeled raw files preserved
   under `benchmarks/superseded/`.
2. `verification/validate_batch_identity.py`: resolves base/head/freeze as
   commit objects, asserts full-40-hex ids, base ancestry, head→freeze
   ancestry, and that head..freeze changes no source path. Positive control
   passes on the repaired manifest; negative control with the original bad
   SHA fails with exit 1.
3. All five sanitizer-claim locations corrected to "AddressSanitizer only;
   UBSan not executed" with the host-capability reason recorded.
4. Benchmark emits `run_setup_ms`, `initialize_ms`, `kernel_only_ms`,
   `plan_increments`, `executed_increments`, `frames_written`; R0.04 evidence
   states exact phase semantics; the two numerical-validation count errors
   ("2002-step", "1500 post-transition increments") corrected.

## Out of repair scope (unchanged facts, not defects)

- CUDA remains BLOCKED_ENV (no NVIDIA device/toolkit on host).
- No runtime solver code changed in repair (`git diff 719c3fb..FIX_HEAD`
  touches only benchmarks/, docs, packet evidence, .gitignore and the
  validator) — result-file byte compatibility re-verified at fix head.
- A true OpenMP lifecycle baseline requires a public API change (CpuRun
  policy selection); deliberately NOT introduced during repair per both
  reviewers' recommendation. Candidate for R2 with its own review.
