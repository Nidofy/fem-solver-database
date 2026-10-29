# DBF3 independent-review corrections

Initial review source: `47cd22e95c32548a260c5c55df61ad317351aac4`.
Independent reviewer: GPT-6 Astra, high reasoning. Original probes and raw logs
are retained under `verification/reviews/DBF3/independent-astra/`.

| Finding | Independently reproduced trigger | Correction | Regression |
|---|---|---|---|
| P1 checkpoint inertia validation | negative rotation inertia publishes through low-level checkpoint while Restart rejects | `a7dec76`: validate inertia/inverse-inertia before contact/state publication | both precisions, negative/zero/reciprocal mismatch, unchanged core/rotation/registered checked views |
| P1 allocation failure after CpuRun restore publication | injected scalar-new failure178 invalidates old view and advances cursor despite exception | `926c6bd`: stage constraints and loads on temporary state; validate mask before nonallocating writes; no-throw state/cursor publication | restore/import scalar-new campaigns, existing cursor/manifest/late-corruption tests |
| P1 inherited step-time amplitude restarts | first-step endpoint force2 becomes0 on next step with no redefinition | runtime Action origin, endpoint hold for propagated step-time amplitude; selective configuration identity change | direct evaluator and real CpuRun transition/split restart; total-time and explicit redefinition controls |
| P2 InstanceId documentation | contract incorrectly said instance IDs follow declaration order | document lexicographic instance-name order; StepId remains definition order | existing typed instance lookup tests and reviewer source check |

The first two fixes were independently recompiled and reproduced green against
`926c6bd`: inertia rejects without publication; restore preserves views/cursor
through183 scalar-new failure points before success. Aligned/platform allocations
were not exhaustively injected. The final report records the final third-fix
source and consolidated verdict; this table is not a replacement for that report.

The amplitude correction deliberately invalidates old CpuRun configurations only
when an inherited step-time load is affected. Existing unaffected single-step
and total-time configurations retain their prior encoding. ModelIR and legacy
file wire layouts remain unchanged; an affected old checkpoint needs an explicit
restart/branch decision rather than silently adopting corrected loading semantics.
