# Independent DBF7 working-tree review

Date: 2026-10-30. Reviewer: independent delegated Codex review agent.
Final scoped conclusion: PASS WITH EXPLICIT SCOPE LIMITS after the two reproduced
findings were corrected and independently retested. No open confirmed P0/P1/P2
finding remains in this bounded review. This is not release certification, native
mapping acceptance, a named-model review claim, or acceptance of later changes.

## Bound source

The initial source copies are in `snapshot`, bound by `source-hashes.json`.
The correction/time-index copies are in `fixed-snapshot`, bound by
`fixed-source-hashes.json`. The latest matrix-ordering, populated-key audit, and
Windows Python loader updates are retained under `supplement` and bound by
`supplement-hashes.json`; these supplemental changes were statically reviewed.
The parent was modifying the main working tree concurrently; only these recorded
contents are covered. All reviewer writes were confined to this directory.

Inspected: units, equation matrices, analysis assignments, spectral storage,
StateSession and RegisteredState metadata, FoundationBundle, interval/streaming
result queries, C ABI, Python wrapper, migration audit and INP inventory tools.

## Closed findings

1. P2: StateSession's original Lease::field returned an ordinary CheckedView.
   The lease could expire while the checked write view stayed live; the independent
   `probe.cpp` wrote 999 through it while a new reader lease was held. Although the
   initial comments restricted use to the lease duration, the new guarded entry
   point did not enforce that restriction. The corrected LeaseView has a weak
   lease token checked by at/raw/size. Independently retested: expired at/raw reject
   and the active reader observes the unchanged value. Previously resolved raw
   pointers/references still require caller discipline; automatic pointer revocation
   or prevention of a concurrent lease destructor is not claimed.

2. P2: SpectralSeries accepted conflicting unit declarations: field.units=K and
   Unit.symbol=m, length dimensions. The independent original probe printed
   `conflicting field K / unit m accepted`. The fix requires matching symbols;
   the fixed probe now verifies rejection. A Unit remains a caller-supplied
   definition, not a global symbol registry or automatic physical-model checker.

Additional contract clarification raised during review: matrix DOF identity alone
could not identify whether reduced entries used canonical or solver equation order.
The parent added explicit row_order/column_order, restricted Full to canonical
slot order, and required matching orders for symmetric matrices. These checks and
wire visitation were statically inspected in the supplemental copy. There is no
implicit matrix permutation operation and no global Index64 retrofit claim.

## Executed evidence

- Windows GCC 15.2, C++20, O0, isolated snapshot include trees.
- Original independent probe compiled and reproduced both findings (`probe.log`).
- Corrected independent probe compiled and ran: **2560 checks PASS**
  (`fixed-probe.log`).
- The corrected probe includes 2556 interval comparisons against STL lower_bound /
  upper_bound for a 129-frame, strictly increasing nonuniform time sequence,
  testing both endpoints and empty/outside intervals. This exercises direct
  footer skip-link time_bound on indexed files, including around powers of two.
- The initial duplicate-time test fixture was correctly refused by the existing
  writer's strict monotonicity contract. Its diagnostic is retained under
  `rejected-duplicate-time-fixture.log`; the fixture was corrected without loosening
  production validation. The invalid fixture was not counted as a runtime defect.

`probe.cpp` / `fixed-probe.cpp`, frozen source, compiler logs and runtime logs are
retained. Probes reuse existing small model fixture constructors while independently
adding the adversarial assertions and time-bound oracle.

## Scope limits and static observations

FoundationBundle compares expected batch and registration metadata, stages state,
and checks EOF before publishing; its inspect API expressly does not certify state
blocks. No new failure-atomicity defect was established by static tracing. Payload
budgets still exclude allocator/container overhead and are not RSS guarantees.

The C ABI stages history outputs before writing caller buffers and catches C++
exceptions. Exact UInt64 access remains typed. Handles require serialized calls and
valid caller buffers. Python uses a per-reader lock; latest Windows loading permits
explicit DLL directories. No independent FFI binary build or Python integration run
was executed in this review; the parent's verification remains separate evidence.

The migration audit keeps integer comparison exact and refuses unsupported populated
fields; latest supplemental code also requires a supplied populated_keys inventory
to equal the actual capture fields. Neither can prove completeness of the producer's
inventory or certify native mapping. INP tooling explicitly inventories keywords and
bounded includes; it does not validate full input grammar.

Not executed here: complete CTest suites, million-frame measurements, optional zstd,
OpenMP, Linux/sanitizers, full parser certification, production SDK/kernel builds,
real native snapshots, CUDA or physical power-loss tests. These cannot be inferred
from this review's focused PASS. Later edits need matching verification/review.

## Final incremental review (2026-10-30)

Scope: new input/thermal_contract.hpp, direct typed RegisteredField remap,
explicit matrix row/column ordering, captured populated_keys equality, and added
UInt64 streaming/Python ABI test paths. Initial final-round source copies and hashes
are `final-snapshot` / `final-source-hashes.json`. Corrected copies and hashes are
`final-fixed-snapshot` / `final-fixed-source-hashes.json`.

A further P2 finding was independently reproduced: thermal fragment import checked
only badbit after reading. A stream with failbit already set was silently accepted
as an empty AnalysisContract (`final-probe.log`: failed input accepted assignments=0).
The parent fixed entry-state validation and post-read fail-without-EOF detection,
and rejected empty assignment fragments. The correction was independently compiled
and exercised (`final-fixed-probe.log`, **9 checks PASS**), covering failed streams,
comment-only fragments, a valid constant CFLUX fragment, and the remap/matrix cases
below. This finding is CLOSED; the scoped PASS conclusion remains unchanged.

The independent incremental C++ probe also verifies:

- AoSoA<4>, two-component UInt64 remapping preserves UINT64_MAX and 2^53+1 exactly.
- Per-component missing validity moves with the correct row/component; retained
  invalid values remain zero, and a new identity receives the declared initializer.
- Symmetric matrices reject different row/column order; Solver order roundtrips;
  Full-stage matrices reject Solver ordering.

`final-tools-probe.py` ran against the copied tool and passed **7 checks**, covering
exact UInt64 comparison and rejection of missing, duplicate, extra and incorrect
populated-key inventories. Added typed-stream/Python ABI tests were statically
inspected; their execution is part of the parent's matrix, not claimed as reviewer
execution. The earlier 2560-check query oracle was not rerun because interval runtime
code was unchanged in this incremental request.

No further confirmed correctness findings remain in the reviewed content. These
hashes cover source snapshots, not a moving working tree; future edits require
appropriate follow-up. No production source or implementation was edited by the
reviewer.
