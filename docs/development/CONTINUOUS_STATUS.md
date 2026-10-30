# Continuous development status

Updated after every task and review milestone. Cross-session handoff details:
SESSION_HANDOFF.md.

## Current DBF7 delivery (2026-10-30)

DBF6 independent review and the DBF7 local P0-P2 follow-up are closed within the
explicit standalone scope. Current evidence: db_foundation/DBF7_HANDOFF.md and
verification/reviews/DBF7/manifest.json. Windows complete 52/52; Windows OpenMP+
zstd focus 8/8; Linux focus 9/9; MSVC ASan focus 8/8; TSan and installed C/C++ pass.
Historical ALR1/B02c states below remain scoped, not blanket-accepted or merged.

## Historical DBF1 implementation (2026-10-29)

The user-requested foundation kit supersedes the ALR1 no-further-development
instruction for this development branch only. Current batch **DBF1 / READY_FOR_REVIEW**,
base `9a3d754dbbc5c0b1720a93ecf29ee3278f705f73`, branch
`codex/astra-db-foundation-completion`. Shared metadata, named material history,
schema-2 model/restart closure, generic ragged/sparse host primitives and two
reproduced P1 repairs are self-tested. Serial/OpenMP/MSVC ASan: 15/15 each.
See [DBF1 handoff](db_foundation/README.md) and capability matrix for incomplete
D1–D7 work. No independent acceptance, push or merge; CUDA/production NOT_RUN.

- Historical batch: **B01 — ACCEPTED, independent round-5 gate passed** (2026-10-28)
- Batch base SHA: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`
- Batch content HEAD (reviewed object, unchanged by repair): `719c3fb`
  (full: `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`)
- Freeze/packet commit: `b58dfff`
- Repair series round 1: `97b1b8a`/`816fb8d`/`5f66b7c`/`d5cad6c` + packet
  commit `02e1ee4` (= FIX_HEAD_SHA).
- Incremental re-review r2: A FAIL (A-002/A-004 NOT_CLOSED, new A-005/A-006),
  B PASS (new O-004 C3). Round-2 fixes `23b8bc5`/`3c1f2c2`/`b4be2a5`/`994a607`
  + packet `d6c67de`.
- Merged single-reviewer incremental r3 (GPT-6 Astra High, user-authorized):
  FAIL — R3-001/C2 (A-005 residual: block-scalar/nested text and quoted
  duplicate keys could still pass), R3-002/C3 (ledger wording). Round-3 fixes:
  `c5c44e7` (column-0 parsing + quoted-key duplicate detection, 30 controls),
  `85d1667` (wording + round-3 report provenance), packet `63b24fc`
  (= NEXT_FIX_HEAD_SHA).
- Round-4 merged review on `63b24fc`: FAIL (R4-001/C2; R4-002/C3).
  Direct fix `d01ea18cb3c8d0ce8a6a32fc6849969f9fb2bda0`: 50/50 controls and 26/26 reviewer fixtures
  pass in implementer verification (historical pre-R5 state).
- Independent R5 on `15771815c016af66d064e5f4d66dc1dbc6b0a74a`: PASS /
  READY_TO_MERGE. R4-001/R4-002 CLOSED; 50/50 official, 26/26 original,
  34/34 independent controls. User authorized merge/push after PASS.
  Evidence/scope: `verification/reviews/B01/acceptance.md`.
- Implementation branch: `dev/glm-b01`; stable integration target: `codex/standalone-database`.
- Dual review: Reviewer A (GPT-6 Astra, `03` prompt) → CHANGES_REQUESTED/FAIL;
  Reviewer B (GPT-6 Astra, user-authorized Opus substitute, `04` prompt) →
  REQUEST_CHANGES/HOLD. Reports are content-exact copies: LF-normalized content equals the originals; original-byte and Git-blob SHA-256 are recorded separately in `verification/reviews/B01/report-provenance.md`. All 7 findings + 2 errata → FIXED_VERIFIED; ledger in
  `verification/reviews/B01/findings.md`.

## Historical ALR1 freeze (2026-10-29; review status retained)

- Current batch: **ALR1 - FROZEN_FOR_BATCH_REVIEW / READY_FOR_REVIEW**, not ACCEPTED.
- Base: `480da32a17e3b60e6694b95e5c9ec83618e5b64b`; content HEAD:
  `8122adf2c149a672222ab81d8ac722e69931205c`; runtime/test SHA `ca4b682`.
- A1-A4 CPU lifecycle hardening and A6 C3D8/mixed path implemented and self-tested.
  A5 CPU evidence retained; CUDA BLOCKED_ENV/NOT_RUN; compression not added.
- Final standalone CTest serial/OpenMP/MSVC ASan: 11/11 each, exit 0.
- Authoritative current handoff: [ALR1 HANDOFF](astra_lifecycle_result/HANDOFF.md).
- No push/merge/release. B01 acceptance below is historical and does not accept ALR1.
  B02c remains unintegrated; R2.04 raw/view candidate ownership is unchanged.

## Task states (summary; authoritative list in TASK_POOL.yaml)

| Task | Status | Notes |
|---|---|---|
| R0.01–R0.04, R1.01, R1.02 | ACCEPTED (independent R5 gate + user authorization) | evidence corrected per findings |
| R0.05 CUDA probe | BLOCKED_ENV | unchanged |

## Last full regression (at repair head, 2026-10-24)

- build-b01 (MinGW Release+OMP): CTest 8/8 PASS.
- build-b01-serial: CTest 8/8 PASS.
- build-b01-asan (MSVC, AddressSanitizer only; UBSan not executed on this
  host): CTest 8/8 PASS, no ASan findings.
- sdb_run byte-outputs identical to pre-R1.01 references (full run + non-due
  stop 733 checkpoint); identity validator positive/negative controls pass.
- Repair diff BATCH_HEAD..FIX_HEAD touches no runtime solver code.

## Historical next steps after B01 acceptance (superseded by ALR1 freeze)

1. Fast-forward the reviewed B01 tip plus acceptance record into
   codex/standalone-database and push under user authorization. Query Git refs
   for actual completion SHA; do not infer it from this text.
2. GLM may start B02 from the accepted stable tip: R1.03/R1.04, then
   measurement-led R2.04. The old B02c is not automatically merged.
3. R5 verified Python/documentation gates only; historical CTest/physics
   evidence and environment limitations retain their original scope.

## EXTENDED_IDLE candidate (produced under masterplan §3 rules)

- **B02c** on branch `dev/glm-b02-candidate` (worktree `../sdb-b02c`),
  base `aaa74af`, head `b87fb53`: R2.04a view-vs-raw fair statistics +
  layout correctness tests; zero file overlap with B01; optional separate
  review; droppable. Not part of B01 repair.
