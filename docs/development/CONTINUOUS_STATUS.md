# Continuous development status

Updated after every task and review milestone. Cross-session handoff details:
SESSION_HANDOFF.md.

- Current batch: **B01 — REPAIRED_PENDING_GATE, round 2** (2026-10-25)
- Batch base SHA: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`
- Batch content HEAD (reviewed object, unchanged by repair): `719c3fb`
  (full: `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`)
- Freeze/packet commit: `b58dfff`
- Repair series round 1: `97b1b8a`/`816fb8d`/`5f66b7c`/`d5cad6c` + packet
  commit `02e1ee4` (= FIX_HEAD_SHA).
- Incremental re-review: A FAIL (A-002/A-004 NOT_CLOSED, new A-005/A-006),
  B PASS (new O-004 C3). Round-2 repair series: `23b8bc5` (strict identity
  parsing + expected pinning + 17 controls), `3c1f2c2` (run_setup timing
  boundary + compat counts), `b4be2a5` (single-source markdown rendering,
  0/72 cell mismatch), `994a607` (dual-hash report provenance), packet commit
  `d6c67de` (= NEW_FIX_HEAD_SHA; docs-only commits may follow).
- Working branch: `dev/glm-b01`
- Dual review: Reviewer A (GPT-6 Astra, `03` prompt) → CHANGES_REQUESTED/FAIL;
  Reviewer B (GPT-6 Astra, user-authorized Opus substitute, `04` prompt) →
  REQUEST_CHANGES/HOLD. Reports are content-exact copies: LF-normalized content equals the originals; original-byte and Git-blob SHA-256 are recorded separately in `verification/reviews/B01/report-provenance.md`. All 7 findings + 2 errata → FIXED_VERIFIED; ledger in
  `verification/reviews/B01/findings.md`.

## Task states (summary; authoritative list in TASK_POOL.yaml)

| Task | Status | Notes |
|---|---|---|
| R0.01–R0.04, R1.01, R1.02 | IMPLEMENTED_SELF_TESTED (batch repaired, pending gate) | evidence corrected per findings |
| R0.05 CUDA probe | BLOCKED_ENV | unchanged |

## Last full regression (at repair head, 2026-10-24)

- build-b01 (MinGW Release+OMP): CTest 8/8 PASS.
- build-b01-serial: CTest 8/8 PASS.
- build-b01-asan (MSVC, AddressSanitizer only; UBSan not executed on this
  host): CTest 8/8 PASS, no ASan findings.
- sdb_run byte-outputs identical to pre-R1.01 references (full run + non-due
  stop 733 checkpoint); identity validator positive/negative controls pass.
- Repair diff BATCH_HEAD..FIX_HEAD touches no runtime solver code.

## Next steps (external)

1. Coordinators run the round-3 targeted incremental re-review
   (`02e1ee4..NEW_FIX_HEAD` above; scope in repair-report.md round-2 section:
   A lane = identity tool + timing semantics + table/data consistency;
   B lane = identity changes + O-004 provenance).
2. Gate passes → user authorizes stable merge (GLM does not merge by itself).
3. B02 pool: R1.03 / R1.04 / R2.04(fast-path decision informed by B02c
   measurement: View overhead ≤1.3%, no raw fast-path justified yet).

## EXTENDED_IDLE candidate (produced under masterplan §3 rules)

- **B02c** on branch `dev/glm-b02-candidate` (worktree `../sdb-b02c`),
  base `aaa74af`, head `b87fb53`: R2.04a view-vs-raw fair statistics +
  layout correctness tests; zero file overlap with B01; optional separate
  review; droppable. Not part of B01 repair.
