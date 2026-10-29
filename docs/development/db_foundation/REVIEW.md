# D0 review and fixes

Review range: `480da32a17e3b60e6694b95e5c9ec83618e5b64b..9a3d754dbbc5c0b1720a93ecf29ee3278f705f73`.
The implementation base is the latter SHA. Local history, merge-base, worktrees,
source, tests and ALR1 handoff were inspected. This is a targeted foundation
review including inherited ALR1 changes, not independent acceptance of its whole
numerical implementation. The untouched B02c candidate is excluded.

## Findings

| ID | Severity / evidence | Trigger and consequence | Fix / regression |
|---|---|---|---|
| DBF-001 | P1, reproduced at frozen baseline | io/restart.hpp RestartDB::load accepts StateDB(mesh,true), constructs translation-only temporary, replaces all state; rotation silently disappears | reject rotational destination before any file read; foundation_regression_probe red exit 1, green exit 0; full preservation assertion in foundation_tests |
| DBF-002 | P1, reproduced at frozen baseline | io/native.hpp decode(enum) narrows wire uint32 256 to uint8-backed ImportMode::Solve=0; unknown enum can bypass fail-closed gates | reject underlying-width overflow before cast; hand-authored 4-byte oracle and old/new probe |
| DBF-003 | P1 semantic contract gap, static evidence | model.hpp Material stores only history count; names/order/version cannot be represented, so equal-count semantic changes have no identity | explicit HistorySchema, initialization/query, ModelIR2 serialization, full semantic fingerprint; six count-preserving mutations rejected by both restart APIs |
| DBF-004 | P2, collaborative review of new code | storage encode initially copied full DTO before checking budget | exact-size preflight before copying; boundary and byte-count checks |

DBF-003 is an added declarative contract, not a claim that every existing caller
had a wrong physical model. Legacy unnamed histories are preserved, and their
semantic interpretation remains the caller's responsibility. DBF-001/002 have
independent small reproduction programs compiling against both header sets.

## Inherited capabilities checked rather than reimplemented

- ALR1 ResultPublisher current-time scratch, const state and due-selection;
  lifecycle all-state trajectory and snapshot tests remain registered.
- Owning async queue/admission, worker error propagation and cooperative writer
  lease: existing async_results_contracts rerun.
- Checkpoint/result prefix binding and rejected-append byte preservation:
  existing separate-process pipeline rerun.
- Lazy indexed native-v2 footer/entity reads, CRC/resource checks: existing
  indexed_results_contracts rerun; no new sidecar mechanism.
- Full-integration C3D8/mixed TET4 path and float precision gates: existing
  hex8/lifecycle tests rerun; no new physical kernel.

No confirmed P0 was found in this targeted pass. This is not evidence that all
possible defects are absent. Full typed result fields, section-point semantics,
material parameter schemas, semantic model identity independent of reorder,
CUDA source/runtime verification and durability campaigns remain open.

GCC's previously recorded optimized vector/native-codec
`-Wfree-nonheap-object` diagnostic is retained, not suppressed or declared benign
from passing tests. MSVC ASan is a separate evidence point, not a proof about
all optimized compiler paths.
