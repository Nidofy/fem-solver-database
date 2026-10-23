# Batch history

| Batch | State | Base SHA | Content HEAD | Tasks | Review | Notes |
|---|---|---|---|---|---|---|
| B01 | REPAIRED_PENDING_GATE | aaa74af | 719c3fb (freeze/packet b58dfff; fixes → FIX_HEAD, see repair-report.md) | R0.01✓ R0.02✓ R0.03✓ R1.01✓ R1.02✓ R0.04✓ R0.05(BLOCKED_ENV) | Astra-A: FAIL (4 findings); Astra-B (Opus substitute): HOLD (3 findings + 2 errata); all FIXED_VERIFIED in repair series | dev/glm-b01; packet in verification/reviews/B01 (findings.md, repair-report.md); no runtime code changed by repair; incremental re-review of BATCH_HEAD..FIX_HEAD requested |
| B02c (candidate) | FROZEN_FOR_BATCH_REVIEW | aaa74af | b87fb53 | R2.04a✓ (measurement+correctness half; fast-path deliberately not implemented — no overhead measured) | pending (optional) | EXTENDED_IDLE rules: separate worktree `../sdb-b02c`, branch dev/glm-b02-candidate, zero file overlap with B01; evidence in docs/development/b02c/ on that branch |

Legend for task marks: ✓ = IMPLEMENTED_SELF_TESTED, frozen for batch review
(never ACCEPTED before external review). Review outcomes are appended only
from real reviewer reports; reviewer files in the packet are verbatim copies
whose SHA-256 matches the user-provided handoff values.
