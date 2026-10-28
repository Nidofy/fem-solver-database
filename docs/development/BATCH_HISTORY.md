# Batch history

| Batch | State | Base SHA | Content HEAD | Tasks | Review | Notes |
|---|---|---|---|---|---|---|
| B01 | REPAIRED_PENDING_GATE | aaa74af | 719c3fb (freeze/packet b58dfff; fixes → 02e1ee4, see repair-report.md) | R0.01✓ R0.02✓ R0.03✓ R1.01✓ R1.02✓ R0.04✓ R0.05(BLOCKED_ENV) | r1: A FAIL / B HOLD → repaired (02e1ee4). r2 incremental: A FAIL (A-002/A-004 NOT_CLOSED + A-005/A-006 new) / B PASS (O-004 C3 new) → repaired (d6c67de). r3 merged single-reviewer: FAIL (R3-001/C2 A-005 residual; R3-002/C3) → repaired (63b24fc). r4 merged: FAIL (R4-001/C2, R4-002/C3) → direct repair d01ea18; 50/50 controls, 26/26 reviewer fixtures; implementer verification, independent closure pending | dev/glm-b01; packet in verification/reviews/B01 (findings.md, repair-report.md); no runtime code changed by repair; incremental re-review of BATCH_HEAD..FIX_HEAD requested |
| B02c (candidate) | FROZEN_FOR_BATCH_REVIEW | aaa74af | b87fb53 | R2.04a✓ (measurement+correctness half; fast-path deliberately not implemented — no overhead measured) | pending (optional) | EXTENDED_IDLE rules: separate worktree `../sdb-b02c`, branch dev/glm-b02-candidate, zero file overlap with B01; evidence in docs/development/b02c/ on that branch |

Legend for task marks: ✓ = IMPLEMENTED_SELF_TESTED, frozen for batch review
(never ACCEPTED before external review). Review outcomes are appended only
from real reviewer reports; reviewer report copies follow `verification/reviews/B01/report-provenance.md`:
Reports are content-exact copies: LF-normalized content equals the originals; original-byte and Git-blob SHA-256 are recorded separately in `verification/reviews/B01/report-provenance.md`.

2026-10-28: User authorized isolated B02 candidate development after direct R4 repair. B01 remains REPAIRED_PENDING_GATE; no stable acceptance, merge or push implied. See SESSION_HANDOFF.
