# B01 repair report (GLM-5.3, 2026-10-24)

## Identity

- `BATCH_BASE_SHA`: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`
- `BATCH_HEAD_SHA` (reviewed object, unchanged by repair):
  `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`
- Freeze/packet commit: `b58dfff04b484f2ed15524d51883e5f979fd1bc1`
- `FIX_HEAD_SHA`: `02e1ee40894189ff0274dc6f881273ea7d0ec5a8` (the ledger commit that carries this report; content fixes are the four commits listed below).

## Fix commits (one per merged root cause)

| Commit | Findings | Content |
|---|---|---|
| 97b1b8a | A-002 / O-001 | manifest head SHA corrected to the Git-resolved object (wrong value preserved in a comment); same fix in `docs/development/TASK_POOL.yaml`; new `verification/validate_batch_identity.py` with positive + negative controls (bad SHA → exit 1). |
| 816fb8d | A-001 / O-002 | lifecycle benchmark reports `execution_policy:"serial"` + `omp_compiled/omp_max_threads/omp_env` per row; "OMP4 ≈ serial / bandwidth-bound" diagnosis retracted in R0.04 evidence and open-risks; mislabeled raw JSONL moved to `benchmarks/superseded/` with explanatory README; fresh 16-run matrix under corrected `serial-env1/serial-env4` tags (numbers consistent: publication savings stand as serial measurements). |
| 5f66b7c | A-003 / O-003 | sanitizer claims corrected to "MSVC AddressSanitizer only; UBSan not executed" in build-test.md, manifest.yaml, compat-matrix.md, task-evidence/R0.02.md, task-evidence/R1.01.md, CONTINUOUS_STATUS.md; host-capability facts recorded (no libubsan/libasan in MinGW, no MSVC UBSan). |
| d5cad6c | A-004 + B errata | benchmark adds `run_setup_ms`, `initialize_ms`, `kernel_only_ms`, `plan_increments`, `executed_increments`, `frames_written`; matrix rerun with the fields; R0.04 phase semantics stated precisely; numerical-validation.md count errata fixed (2022 = 2000+1+21; released segment = 500 increments). |
| (final) | — | reviewer report copies recorded with accurate provenance (`astra-review.md`, `opus-review.md`; originals differ from committed blobs by one trailing CRLF, content byte-equal after CRLF→LF — see `report-provenance.md`, corrected in round 2 per B01-O-004), `findings.md`, this report, ledger updates. |

## Scope discipline

`git diff BATCH_HEAD..FIX_HEAD` touches **no runtime solver code**: only
`benchmarks/lifecycle_benchmark.cpp`, packet/evidence/docs, `.gitignore` and
the new validator. `include/`, `src/`, `tests/`, `examples/`, `CMakeLists.txt`
are byte-identical to the reviewed head. No public API change, no numerical
default-path change, no evidence backfill by renaming — the retracted claims
remain visible as retracted.

## Regression executed at fix head

- MinGW Release+OpenMP (`build-b01`): full rebuild, CTest **8/8 PASS**.
- MinGW Release serial (`build-b01-serial`): full rebuild, CTest **8/8 PASS**.
- MSVC 14.44 RelWithDebInfo **AddressSanitizer only** (`build-b01-asan`):
  rebuild, CTest **8/8 PASS**, no ASan diagnostics (UBSan not available on
  this host — recorded, not claimed).
- Byte compatibility re-verified with fix-head binaries: full-run
  `.sdbres` double identical to the pre-R1.01 reference; `--stop-after 733`
  (non-due) `.sdbrst` checkpoint identical.
- `python verification/validate_batch_identity.py
  verification/reviews/B01/manifest.yaml` → `IDENTITY OK` (exit 0);
  negative control (original bad SHA) → `IDENTITY FAIL` (exit 1).
- Fresh benchmark matrix (16 runs): every row carries
  `execution_policy:"serial"`, counts `plan 21 / executed 10 / frames 1`,
  and the new phase timings; eager/lazy publication deltas reproduce within
  noise (non-due publication removed ≈ 44–50% of one serial advance).

## Remaining risks / open items

- OpenMP lifecycle baseline does not exist (by design until a reviewed public
  API change); R2 must not cite B01 for parallel efficiency.
- UBSan coverage gap stands as a host limitation (BLOCKED_ENV-class for
  sanitizer coverage), now labeled honestly everywhere.
- CUDA: BLOCKED_ENV unchanged (R0.05).
- B02c candidate (`aaa74af..b87fb53`) untouched by this repair, still
  awaiting its own optional review.

## Incremental re-review scope requested

Reviewers: please verify the fix series `BATCH_HEAD..FIX_HEAD`
(5 commits, evidence/docs/benchmark-only) against the four merged findings:
(a) `benchmarks/README.md` + regenerated JSONL labels/fields close A-001/O-002
without any parallel-performance claim; (b) `manifest.yaml` +
`validate_batch_identity.py` controls close A-002/O-001; (c) the six
sanitizer-claim corrections close A-003/O-003; (d) benchmark phase fields +
R0.04/numerical-validation wording close A-004 and the two errata. No
numerical-physics re-review is required — runtime code is unchanged from the
reviewed head (byte-verified), per both original reports' suggestion that
evidence/identity issues, not physics, blocked the gate.

## Gate self-assessment

- All C0/C1 findings (A-001, O-001) and all C2 findings: **FIXED_VERIFIED**
  with recorded evidence; no defer was used (none needed).
- Both original reports exist, reviewed the same immutable object, and are
  recorded as content-exact copies (blob differs from original bytes only by
  CRLF→LF normalization; see `report-provenance.md`).
- Full applicable test suites pass on three configurations at fix head.

**Status: REPAIRED_PENDING_GATE** — ready for the coordinators' targeted
incremental re-review of `BATCH_HEAD..FIX_HEAD`. GLM does not self-declare
READY_FOR_ACCEPTANCE; that decision belongs to the reviewers/user after the
incremental pass. No push/merge to any protected branch was performed.
