# Batch history

| Batch | State | Base SHA | Content HEAD | Tasks | Review | Notes |
|---|---|---|---|---|---|---|
| B01 | ACCEPTED | aaa74af | 719c3fb (freeze/packet b58dfff; fixes → 02e1ee4, see repair-report.md) | R0.01✓ R0.02✓ R0.03✓ R1.01✓ R1.02✓ R0.04✓ R0.05(BLOCKED_ENV) | r1: A FAIL / B HOLD → repaired (02e1ee4). r2 incremental: A FAIL (A-002/A-004 NOT_CLOSED + A-005/A-006 new) / B PASS (O-004 C3 new) → repaired (d6c67de). r3 merged single-reviewer: FAIL (R3-001/C2 A-005 residual; R3-002/C3) → repaired (63b24fc). r4 merged: FAIL (R4-001/C2, R4-002/C3) → direct repair d01ea18; 50/50 controls, 26/26 reviewer fixtures; implementer verification → r5 independent PASS on 1577181 (50/50 + 26/26 + 34/34), R4-001/R4-002 CLOSED | dev/glm-b01; packet in verification/reviews/B01 (findings.md, repair-report.md); no runtime code changed by repair; user-authorized stable integration; acceptance.md records scope |
| B02c (candidate) | FROZEN_FOR_BATCH_REVIEW | aaa74af | b87fb53 | R2.04a✓ (measurement+correctness half; fast-path deliberately not implemented — no overhead measured) | pending (optional) | EXTENDED_IDLE rules: separate worktree `../sdb-b02c`, branch dev/glm-b02-candidate, zero file overlap with B01; evidence in docs/development/b02c/ on that branch |

Legend: ✓ marks implemented tasks; the State column determines acceptance.
B01 is accepted after R5; B02c remains frozen and unaccepted. Review outcomes are appended only
from real reviewer reports; reviewer report copies follow `verification/reviews/B01/report-provenance.md`:
Reports are content-exact copies: LF-normalized content equals the originals; original-byte and Git-blob SHA-256 are recorded separately in `verification/reviews/B01/report-provenance.md`.

Historical pre-R5 authorization allowed isolated B02 only. Later on 2026-10-28,
the user authorized B01 merge/push after review; independent R5 passed.
Current state is ACCEPTED; see acceptance.md and SESSION_HANDOFF.
