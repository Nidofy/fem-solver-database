# B01 — merged findings ledger and dispositions

Sources (copies in this directory; accurate provenance in `report-provenance.md`):

- `astra-review.md` — Reviewer A, GPT-6 Astra, `03_ASTRA_BATCH_REVIEW_PROMPT.md`.
  Original SHA-256 `ab3b58af…5bf6e0` (21625 bytes, one trailing CRLF);
  committed blob SHA-256 `78763a24…d61318` — content byte-equal after
  CRLF→LF normalization per `.gitattributes` (corrected wording, B01-O-004).
  Verdict `CHANGES_REQUESTED / FAIL`.
- `opus-review.md` — Reviewer B, **GPT-6 Astra acting under explicit user
  authorization in place of the unavailable Opus**, `04_OPUS_BATCH_REVIEW_PROMPT.md`
  (filename kept for workflow compatibility; no Claude/Opus participation is
  claimed anywhere in this packet). Original SHA-256 `34e23484…d9e191`
  (20609 bytes, one trailing CRLF); committed blob SHA-256
  `ff74bae8…3f804c8` — content byte-equal after CRLF→LF normalization.
  Verdict `REQUEST_CHANGES / HOLD`.

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

---

# Round 2 — incremental re-review (2026-10-25) and second repair

## Round-2 reviewer verdicts (reports recorded in this directory)

- Reviewer A (`astra-incremental-review.md`, GPT-6 Astra High, original/blob
  hashes in `report-provenance.md`): **INCREMENTAL_REVIEW: FAIL / NOT_READY**.
  A-001 CLOSED, A-003 CLOSED, A-002 NOT_CLOSED, A-004 NOT_CLOSED; new
  B01-A-005 (C2), B01-A-006 (C2).
- Reviewer B (`opus-incremental-review.md`, GPT-6 Astra High under explicit
  user authorization substituting for the unavailable Opus; no Claude/Opus
  participation): **INCREMENTAL_REVIEW: PASS / READY_FOR_ACCEPTANCE within
  B's targeted scope**; O-001/O-002/O-003 and both count errata CLOSED; new
  B01-O-004 (C3, non-blocking).
- Coordinator state: **NOT_READY / REVIEW_CHANGES_REQUIRED** — B's PASS does
  not override A's counterexamples; both opinions preserved above.

## Reproduction record (round 2, before fixing)

1. **A-002 residual**: manifest with content head replaced by the legal
   freeze commit `b58dfff` passed the round-1 validator (`IDENTITY OK`,
   exit 0) — generic consistency was checked, but the externally fixed
   `719c3fbbe4db…` was never asserted.
2. **A-005**: manifest with `freeze_packet_sha: invalid-sha` — the loader's
   regex silently dropped the line (field treated as missing), the script
   fell back to comparing head..HEAD, and on a **real detached BATCH_HEAD
   checkout** (replayed in `build-b01/identity-repro/batch-head-checkout`)
   the result was exit 0 `IDENTITY OK`. On the dev tree it only failed
   because benchmark diffs happened to exist after head — coincidental, not
   the field being validated.
3. **A-006**: cell-by-cell comparison of the R0.04 baseline table against
   `baseline.json` at display precision: **70/72 cells mismatched** (the
   table held round-1 rerun values; baseline/summary had been regenerated
   from the later rerun and the table was never synced).
4. **A-004 residual**: source-level — the round-1 `run_setup_ms` timed a
   lambda-local `CpuRun probe`, so its destruction ran inside the clock, and
   the actually-used `run` was constructed outside the timing; the evidence
   called the number "CpuRun construction". Also `compat-matrix.md` still
   said "now runs 21 increments" (plan count, not executed count).
5. **O-004**: recomputed — original round-1 report files differ from their
   committed blobs by exactly one trailing CRLF (`* text=auto eol=lf` in
   `.gitattributes`); content byte-equal after normalization. The ledger's
   "verbatim / hashes match" wording was wrong.

## Round-2 dispositions

| Finding | Severity | Disposition | Fix commit |
|---|---|---|---|
| B01-A-002 (residual: expected-identity assertion) | C2 | FIXED_VERIFIED | 23b8bc5 |
| B01-A-005 (invalid-field fail-open) | C2 | FIXED_VERIFIED | 23b8bc5 |
| B01-A-004 (residual: run_setup timing boundary; compat-matrix count) | C2 | FIXED_VERIFIED | 3c1f2c2 |
| B01-A-006 (table/raw inconsistency) | C2 | FIXED_VERIFIED | b4be2a5 |
| B01-O-004 (provenance wording) | C3 | FIXED_VERIFIED | 994a607 |

## Round-2 fix verification

- A-002/A-005: `verification/validate_batch_identity.py` rewritten — strict
  parsing (missing vs present-but-empty vs invalid vs duplicate vs quoted
  all distinguished; quotes unwrapped then validated; freeze required; no
  HEAD fallback) and `--expect-base/--expect-head/--expect-freeze` external
  identity pinning. Replay suite
  `task-evidence/identity-controls.py` → `identity-controls.json`:
  **17/17 controls as expected**, including the A-005 fail-open replay on a
  real detached BATCH_HEAD checkout (now exit 1) and A-002
  different-valid-head (generic exit 0 documented as insufficient; with
  `--expect-head` exit 1). Callers must always pass the expected values.
- A-004: `run_setup_ms` now times `std::optional<CpuRun>::emplace`
  construction of the instance actually used (destruction outside the
  clock); `compat-matrix.md` says "plans 21 increments, executes 10, writes
  1 frame". Definition change is labeled in R0.04 field list; affected
  samples rerun (see A-006).
- A-006: `summarize.py --markdown` renders the R0.04 table and delta bullets
  from the **same medians** as `baseline.json`; matrix rerun (16 rows, all
  `execution_policy:"serial"`, counts 21/10/1); post-fix cell check:
  **0/72 mismatches**. Old stale table preserved only in Git history.
- O-004: `report-provenance.md` records original-byte and normalized-blob
  SHA-256 for all four reviewer reports plus the normalization rule and
  machine-replayable commands; findings/repair-report wording corrected.
  Round-2 report copies contain no CRLF (blob == original bytes).

## Round-2 regression (at 994a607)

- build-b01 (MinGW Release+OMP): rebuild + CTest 8/8 PASS.
- build-b01-serial: rebuild + CTest 8/8 PASS.
- build-b01-asan (MSVC, AddressSanitizer only): rebuild + CTest 8/8 PASS.
- `git diff BATCH_HEAD..round-2-head -- include src tests examples
  CMakeLists.txt cmake` → empty (runtime code still byte-identical to the
  reviewed object).
- `sdb_run` full-run output byte-identical to the pre-R1.01 reference.
