# Independent DBF6 working-tree review

Date: 2026-10-30. Reviewer: independently delegated Codex review agent.
Baseline: b489414df6f1c45fdcb7b7b54b9f725e4c12ac4a.
Decision: PASS WITH EXPLICIT SCOPE LIMITS for the frozen DBF6 core reviewed here.
No confirmed P0/P1/P2 correctness findings were identified in this bounded review.
This is a working-tree review, not a release, merge, historical ALR1 acceptance,
or a claim about any named reviewer model. It does not certify ongoing DBF7 edits.

## Reviewed source identity

`source-hashes.json` binds the private `snapshot/include` copy and copied contract
test. These copies were used for both independent compilations. Review was against
core data_contract.hpp, dof_space.hpp, state_layout.hpp, consumer_contract.hpp,
io/foundation_store.hpp and registered_state.hpp, plus the mechanical export,
StateDB registration, checkpoint and result integration changes and underlying
Field/native codec validation paths.

`delta-from-dbf6.json` compares copied headers against the original DBF6 evidence
manifest. All copied headers represented in that manifest match except
include/sdb/results/results.hpp, which already included the parent's first DBF7
query extension at capture time. The DBF6 result integration was inspected, but
this decision does not independently accept the new interval-query feature.
The six new core headers and registered_state.hpp match the DBF6 manifest.
No production source, implementation, existing evidence, or Git commit was changed
by this review. Added files are confined to this review directory.

## Independent execution

Fresh direct compilation with Windows GCC 15.2, C++20, O0, big-object support,
using only snapshot/include; no reuse of an existing contract binary:

- Copied DBF6 contract executable: 67 checks PASS (`contracts.log`).
- Separate process reader: 4 checks PASS (`reader.log`).
- New adversarial probe: 1179 checks PASS (`probe.log`).
- Both compiler invocations exited zero (`compile.log`, `probe-compile.log`).

`probe.cpp` checks every truncated DOF wire prefix, trailing wire bytes, FoundationStore
truncation at 17-byte intervals, preservation of an existing target owner/value on
failed restore, preservation of an existing published file on three small save
budgets, rejection of trial persistence/copy, and failure-atomic malformed remap.
The probe uses existing fixture constructors, but its failure sweeps and assertions
were independently added. Original fixtures and production sources were not edited.
An initial PowerShell invocation failed to parse an unquoted comma-bearing GCC flag;
it never invoked the compiler. The quoted command then compiled successfully.

## Static conclusions and retained boundaries

- Supplied DOF numbering/permutations and flattened constraints are validated;
  no implicit MPC algorithm or old zero/negative equation translation is introduced.
- Generic StateLayout checks structural address validity. Native domain membership
  intentionally remains caller-owned; StateDB additionally checks its MeshDB.
- FoundationStore stages registry restore and checks exact manifest/DOF data and EOF
  before publication. The negative probes confirm representative failure atomicity.
- AccessSet validates allocation aliasing and checked-view generation at entry.
  It is explicitly not a pinning mechanism or mid-loop race prevention API.
- DataCoverage checks completeness against the caller-supplied populated-key list;
  it cannot establish that this list covers all native data. The schema inventory
  is not native field-equivalence certification.
- File-byte budgets are not peak-RSS limits; deep trial and restore copies remain.
  These documented limits are extension work, not findings against DBF6's contract.

## Not verified here

No full solver build, native Felem mapping, production numerical equivalence,
production performance, Linux, TSan, OpenMP, optional zstd, CUDA, physical power
loss, exhaustive allocator failures, hostile-hash collision resistance, or complete
wire fuzzing was executed by this reviewer. Existing prior evidence is not relabelled
as this review's execution. No full 47-test rerun or installed-consumer compilation
was performed here; those remain separate verification obligations.

Any subsequent core/runtime edits require an appropriately scoped follow-up review
and fresh evidence. Keep the original DBF6 manifest and this copied-source identity
rather than pretending that evidence applies to the moving working tree.
