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

---

# Round-2 repair (2026-10-25) — after incremental re-review

Inputs: `astra-incremental-review.md` (FAIL/NOT_READY; A-002/A-004
NOT_CLOSED, new A-005/A-006) and `opus-incremental-review.md`
(PASS/READY within B scope; new O-004 C3), both recorded in this packet with
accurate provenance. All five round-2 items reproduced before fixing
(see findings.md round-2 section for the reproduction record).

## Round-2 fix commits (one per root cause)

| Commit | Findings | Content |
|---|---|---|
| 23b8bc5 | A-002 residual + A-005 | validator rewrite: strict parsing (missing/empty/invalid/duplicate/quoted distinguished, freeze required, no HEAD fallback) + `--expect-base/head/freeze` external pinning; 17-control replay suite incl. fail-open replay on a detached BATCH_HEAD checkout (`task-evidence/identity-controls.py`, JSON output committed). |
| 3c1f2c2 | A-004 residual | `run_setup_ms` times construction only of the live instance (`std::optional::emplace`; destruction outside clock); compat-matrix count wording "plans 21 / executes 10 / writes 1". |
| b4be2a5 | A-006 | R0.04 table + delta bullets rendered programmatically from the same medians as baseline.json (`summarize.py --markdown`); 16-run matrix rerun (corrected run_setup semantics); post-fix cell check 0/72 mismatches; stale-table history note added. |
| 994a607 | O-004 | `report-provenance.md` dual-hash record (original bytes vs CRLF→LF blob) for all four reviewer reports + machine-replayable commands; ledger wording corrected; round-2 reports recorded (blob == original, no CRLF). |
| (final) | — | this round-2 section, findings round-2 ledger, development ledger updates. NEW_FIX_HEAD_SHA recorded in BATCH_HISTORY. |

## Scope discipline (round 2)

`git diff BATCH_HEAD(719c3fb)..round-2-head -- include src tests examples
CMakeLists.txt cmake` is empty; the only code-adjacent change is
`benchmarks/lifecycle_benchmark.cpp` (timing boundary inside the benchmark
program). No public API, no solver default path, no schema change, no
test-threshold relaxation.

## Round-2 regression actually executed (at 994a607)

- MinGW Release+OMP rebuild + full CTest: 8/8 PASS.
- MinGW serial rebuild + full CTest: 8/8 PASS.
- MSVC RelWithDebInfo ASan-only rebuild + full CTest: 8/8 PASS
  (UBSan still not executed on this host — unchanged recorded gap).
- Identity controls: 17/17 as expected (incl. BATCH_HEAD fail-open replay).
- Cell-level table/baseline consistency: 0/72 mismatches.
- Byte compatibility of `sdb_run` output vs pre-R1.01 reference: identical.

## Incremental re-review scope requested (round 3)

- **Reviewer A**: (a) identity tool — strict-parsing edges, external
  expected-value pinning, and the BATCH_HEAD fail-open replay
  (`task-evidence/identity-controls.py` + JSON); (b) benchmark timing
  boundary — `run_setup_ms` optional-emplace form excludes destruction;
  compat-matrix count wording; (c) consistency — regenerate
  `summarize.py --markdown` output and compare against R0.04 table at display
  precision; spot-check baseline.json medians vs raw JSONL.
- **Reviewer B**: (a) identity-related changes only (same validator/controls);
  (b) O-004 closure — recompute the four report hashes against
  `report-provenance.md` (originals at the two handoff directories).
- Neither reviewer needs to repeat the round-1 numerical-physics passes:
  runtime source remains byte-identical to BATCH_HEAD (verifiable with the
  one-line git diff above).

## Gate self-assessment (round 2)

All round-2 findings FIXED_VERIFIED with replayable evidence; no defer used.
Overall batch state remains **REPAIRED_PENDING_GATE** (round-2), awaiting the
targeted round-3 incremental re-review above. No push/merge of protected
branches performed.

---

# Round-3 repair (2026-10-26) — after the merged single-reviewer incremental review

Inputs: `merged-incremental-review-r3.md` (single GPT-6 Astra High, user
authorization recorded in the report; FAIL/NOT_READY, blocker R3-001/C2,
non-blocking R3-002/C3), evidence replayed from the reviewer's fixture
directory before any fix. Scope kept minimal: identity tool + controls +
ledger wording + this packet. No solver runtime, test, benchmark or schema
change; no C++ was rebuilt this round (nothing compiled changed — validator
and controls are Python; per the 05 workflow only targeted controls and
consistency checks were rerun).

## Round-3 fix commits

| Commit | Finding | Content |
|---|---|---|
| c5c44e7 | R3-001 (A-005 residual) | column-0 top-level parsing; quoted keys normalized into duplicate detection; 30-control suite (17 original + 4 counterexamples + 9 structural accept/reject rules), JSON committed. |
| 85d1667 | R3-002 + report recording | ledger wording qualified with dual-hash provenance links; round-3 report copy recorded (blob == original). |
| (final) | — | findings round-3 ledger, this section, development handoff updates. NEXT_FIX_HEAD_SHA recorded in BATCH_HISTORY. |

## Round-3 verification actually executed

- Four reviewer counterexamples re-run pre-fix (all exit 0 — reproduced) and
  post-fix against the reviewer's original fixtures (all exit 1, correct
  diagnostics: missing freeze ×2, duplicate identity key ×2).
- Full control suite: **30/30 as expected** (includes the round-2 BATCH_HEAD
  fail-open replay and the different-valid-head expected-identity case —
  A-002 behavior unchanged and not reopened).
- Reviewer's additional edge fixtures re-checked individually: crlf-valid /
  outer-whitespace / quoted-single-valid accepted; duplicate-identical /
  inline-comment / quoted-whitespace-empty rejected.
- Runtime zero-diff gate: `git diff --name-only 719c3fbbe4db…..HEAD --
  include src tests examples CMakeLists.txt cmake` → empty.
- Official manifest positive control with external pinning → IDENTITY OK.

## Incremental re-review scope requested (round 4, parser/controls + wording only)

Single or dual reviewer at the coordinator's choice; scope:
(a) `verification/validate_batch_identity.py` structural rules — column-0
top-level only, quoted-key normalization, duplicate contract; adversarial
probes beyond the 30 committed controls are welcome;
(b) `identity-controls.py` / `identity-controls.json` — replay the suite,
confirm the four round-3 counterexamples fail for the stated reasons;
(c) the R3-002 wording fixes in the three development ledgers (grep for
unqualified `verbatim` should only hit historical self-incriminating quotes);
(d) confirm `719c3fb..NEXT_FIX_HEAD` still has an empty runtime diff.
No numerical/physics re-review is required (runtime source unchanged since
BATCH_HEAD; verifiable with the one-line git diff above).

## Gate self-assessment (round 3)

R3-001 and R3-002 FIXED_VERIFIED with replayable evidence; no defer used.
Batch state remains **REPAIRED_PENDING_GATE** (round 3), awaiting the round-4
targeted review above. No push/merge of protected branches performed.


# Round 4 — review and direct repair (2026-10-28)

Independent single GPT-6 Astra High review of 63b24fc: **CHANGES_REQUESTED / FAIL**.
The report is preserved as `merged-incremental-review-r4.md`; no fifth-round
independent PASS is claimed. R3-002 is reviewer-CLOSED. A-002/A-004/A-006/O-004
remain closed. R3-001/A-005 had residual quoted-key escape handling, R4-001.

| Finding | Severity | Current implementation disposition | Evidence |
|---|---|---|---|
| B01-R4-001 (R3-001/A-005 residual) | C2 | FIXED_VERIFIED (implementer tests; independent closure pending) | `d01ea18cb3c8d0ce8a6a32fc6849969f9fb2bda0`; identity-controls.json 50/50; r4-repair-replay.json 26/26 |
| B01-R4-002 (stale session handoff) | C3 | FIXED_VERIFIED (documentation checked) | refreshed SESSION_HANDOFF, CONTINUOUS_STATUS, BATCH_HISTORY |

Three original escaped duplicate fixtures were reproduced before editing:
all exit 0 despite externally pinned identities. After the fix all exit 1
with `unsupported quoted key syntax`. Rather than implement full YAML,
the validator rejects double-quoted escapes and malformed quoted keys before
unknown-key filtering. Plain quoted keys retain the same duplicate checks;
single-quoted backslashes remain literal unknown-key text. The 20 new controls
include same/different/invalid duplicate values, escaped-only fields,
unsupported/incomplete escapes, escaped quotes, multiline/unclosed keys and
positive controls for literal single-quoted backslashes/metadata values.
Eighteen new negative controls also assert their specific diagnostic.

Evidence: `task-evidence/r4-repair-before.json`, `r4-repair-replay.json`,
`identity-controls.py`, `identity-controls.json`. Runtime diff against 719c3fb
is empty for include/src/tests/examples/CMakeLists.txt/cmake. Benchmark diff
against 63b24fc is empty; historical benchmark timing fixes remain present.
No C++ build, CTest, physics, benchmark, ASan/UBSan/CUDA/Linux or parent-solver
verification was run for this Python/documentation repair. Historical passes
are not recounted as current tests; production is not locally verified.

The user's latest instruction explicitly requests direct repair followed by
a GLM next-development prompt. Accordingly, GLM may begin isolated B02
candidate development from this fixed B01 baseline after takeover checks,
without waiting for another chat round. This does not declare B01 ACCEPTED,
fabricate an independent PASS, or authorize stable-branch merge/push.
Formal B01 state remains REPAIRED_PENDING_GATE; independent closure/acceptance
is required before stable integration. This user-authorized candidate-development
exception supersedes the older blanket instruction to stop all B02 work.
