# Batch history

| Batch | State | Base SHA | Content HEAD | Tasks | Review | Notes |
|---|---|---|---|---|---|---|
| B01 | FROZEN_FOR_BATCH_REVIEW | aaa74af | 719c3fb (freeze/packet commit b58dfff) | R0.01✓ R0.02✓ R0.03✓ R1.01✓ R1.02✓ R0.04✓ R0.05(BLOCKED_ENV) | pending | dev/glm-b01; packet in verification/reviews/B01 |
| B02c (candidate) | FROZEN_FOR_BATCH_REVIEW | aaa74af | b87fb53 | R2.04a✓ (measurement+correctness half; fast-path deliberately not implemented — no overhead measured) | pending | EXTENDED_IDLE rules: separate worktree `../sdb-b02c`, branch dev/glm-b02-candidate, zero file overlap with B01; evidence in docs/development/b02c/ on that branch |

Legend for task marks: ✓ = IMPLEMENTED_SELF_TESTED, frozen for batch review
(never ACCEPTED before external review). Review outcomes are appended only
from real reviewer reports; GLM never writes astra-review.md / opus-review.md.
