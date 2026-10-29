# DBF5 consumer contract v0 and reference evidence

## Scope and entry points

`include/sdb/consumer_views.hpp`, namespace `sdb::consumer`, publishes contract
version **0**. This is an additive standalone CPU reference interface. The
production solver, legacy Felem APIs, preprocessing, contact physics and CUDA
are unchanged and **not locally compiled or verified** by this work.

| Endpoint | Concrete v0 meaning |
| --- | --- |
| `BorrowedField<T,C>` | Const, zero-copy SoA field borrow; checked `resolve`, `at`, `size` |
| `NodeStateView<T>::capture(state)` | Displacement, staggered velocity, acceleration, mass, inverse mass, node/element activity and model identity |
| `ElementBatchView<N>` | Dense connectivity (N=4 or 8), local-to-global element IDs, global integration-point offsets and live activity field |
| `MaterialView<T>` | Read-only precomputed isotropic linear elastic lambda/mu, one pair per batch element |
| `ShapeTableView<T,N>` | Read-only reference global gradients and integration weights; TET4 one point, HEX8 eight points |
| `ResultSink<T>` | Separate writable internal nodal force and integration-point stress/strain targets |
| `RestartLink` | Owned informational snapshot of existing CheckpointLink v1 identity, increment and exact prefix fingerprints |
| `ReferenceConsumer<T>` | Deterministic serial TET4/HEX8/mixed force evaluation using only the above borrowed contract inputs |

`CheckedView::validate/raw/size` are additive Field helpers. `raw()` validates
once at entry and preserves constness. The unchanged HEX8 force algebra was
mechanically extracted into `HexViews`/`hex_force`; existing `HexPlan::force`
forwards to that same helper. TET4 already provided `tet_force`. There are no
separately maintained numerical formulas in the new consumer.

Construction example:

```cpp
sdb::CpuExplicit<double> setup(mesh, state); // existing setup, outside consumer
sdb::consumer::ReferenceConsumer<double> consumer(setup.plan(), state);
auto node = sdb::consumer::NodeStateView<double>::capture(state);
sdb::consumer::ResultSink<double> sink(state.data().internal_force,
                                      state.data().stress, state.data().strain);
consumer.evaluate(node, sink);
```

## Lifetime, batching and ownership

Fields and the source state/plan remain caller-owned. Captured views copy no
nodal, connectivity, shape or material arrays; tests assert source pointer
identity. The consumer allocates its private local force scratch and snapshots
only element-ID/IP metadata once during setup. TET4 mixed-plan slots preserve
global element numbering; non-TET slots use an explicit invalid element marker.
HEX8 slots use the existing compact hex ordering. A call evaluates the entire
prepared batch and writes all force/stress/strain targets. There is no per-element
registry lookup, owned snapshot, reallocation or virtual dispatch in evaluation.
The reference consumer cannot be copied or moved, because its captured maps
borrow its own metadata fields.

Every call resolves generation-checked borrows before numerical writes. Resize,
swap, copy assignment, move, or destruction invalidates prior Field borrows,
including zero-length and same-length replacement. Material/shape fields carry
the same lifetime checks, so destroying the plan owner is rejected. Content
writes within the current allocation remain visible on the next call; this is
how element activation changes are observed. The consumer binds the original
state activity field and rejects a same-model view from another state.

**A resolved raw View has no automatic mid-loop lifetime check.** The caller
must keep owners alive and prevent resize, move, swap, destruction and concurrent
writes for the complete batch. Checked views detect stale entry; they do not
provide locks, storage pinning or race prevention. Capturing views and changing
metadata is serialized. Plan connectivity, shape, material and IP/element maps
are immutable after preparation; changing their content or structural metadata
requires rebuilding the setup/consumer. Concurrent evaluation on one consumer
is unsupported because local-force scratch is private mutable workspace.

Inputs are const at the type level. ResultSink is the only write surface;
aliased displacement/velocity/acceleration force targets and aliased
stress/strain targets are rejected before writes. Independently captured sinks
must also have disjoint targets. Callers retain responsibility for external
access through original Field owners; the contract is not a security boundary.

Tensor component order is XX, YY, ZZ, XY, YZ, ZX and strain uses tensor shear.
Node/element IDs are zero-based compact indices, never legacy labels or equation
numbers. `private_dof_scratch` is a synthetic node-major demonstration for the
adversarial consumer: it returns owned one-based numbering, checked for overflow,
then allows the old eecontact-style decrement on that scratch. It does **not**
represent production LM/IDARRAY equation mappings, constrained equations or MPC.
The separate migration adapter handles actual preprocessed equation mapping.

MaterialView v0 is deliberately the actual supported reference-kernel subset,
not a promise that arbitrary material history or new storage-only element
families can execute. Existing TetPlan/HexPlan setup rejects unsupported
formulations. Unspecified shape cache invalidation, element formulation changes
and production contact scratch still require the migration checklist.

## Result and restart composition

ResultSink borrows outputs and does not create a second persistence layer. The
integration test routes its stress/strain writes into frozen registered State
fields, calls the existing `ResultPublisher::snapshot_registered`, writes
TypedIndexed3 through `NativeResultStorage`, then calls existing
`CheckpointLink::publish_durable` at FileSync level. It reads back owned typed
values, restores the CpuRun restart with the same field contracts registered,
and invokes `CheckpointLink::verify` before accepting the pair.

RestartLink is only a value snapshot of that existing link. Its identity check
cannot authorize append, detect a bad file, or substitute for the existing
verified durable publication protocol. Hashes remain accidental-corruption
fingerprints, not authentication. Result snapshots deliberately own their
copies, so async publication cannot retain borrowed kernel memory.

## Verification and repeatable commands

`tests/dbf5_consumer_tests.cpp` runs six FP32/FP64 TET-only, HEX-only and shared
mixed-model 80-step central-difference trajectories, comparing force, stress,
strain, displacement, velocity and acceleration at every step with bitwise
equality against the existing CpuExplicit path. Activity changes at
steps 20 and 30 exercise inactive output clearing and reactivation. OpenMP
builds add three FP64 OpenMP-baseline/serial-consumer trajectories. Borrow expiry,
empty fields, move/swap/assignment, constness static assertions, alias rejection,
private scratch mutation, overflow, mismatched states and destroyed plan owners
are covered. Pass fixture/scratch paths to include typed publication and restart:

```text
sdb_dbf5_consumer_tests tests/fixtures/abaqus/forced_tet.inp <scratch-dir>
sdb_dbf5_consumer_benchmark 2000 200 21
sdb_dbf5_consumer_benchmark 2 20000 21
```

Local GCC15.2 `-std=c++20 -O0 -Wa,-mbig-obj` serial compilation and the final
suite passed **81,803 checks**. GCC15.2 `-O2 -fopenmp`, four threads, passed
**122,612 checks** including three OpenMP-baseline trajectories. Both include
publication/restart integration and bitwise trajectory comparisons. Raw summaries
are `verification/dbf5_consumer_serial.txt` and `dbf5_consumer_openmp.txt`.
Final integrated serial/OpenMP/MSVC and review evidence is recorded by the DBF5
handoff; do not infer a production or CUDA pass from these standalone tests.

## Measured cost and limits

`benchmarks/dbf5_consumer.cpp` warms setup, runs an odd number of alternating
paired samples, reports medians and the min/max of paired percent differences,
and consumes a checksum. The first probe uses identical shared force-kernel
loops, arrays and output stores. Only acquisition differs: direct raw fields
versus generation-checked borrowed contract fields, resolved once per batch.
The second probe compares complete existing CpuExplicit evaluation with complete
ReferenceConsumer evaluation; that difference also includes adjacency reduction
versus element scatter assembly and therefore is **not** isolated view overhead.
The pre-parsed model and prepared plans are reused; parsing/setup are excluded.

A GCC15.2 `-O2 -DNDEBUG`, serial FP64 run on this Windows host measured:

| Batch | Samples x repeats | Raw/checked kernel median (us) | Paired checked overhead median (min..max) | Full CPU/reference median (us) |
| --- | --- | --- | --- | --- |
| 2,000 mixed elements, 12,000 nodes | 21 x 200 | 441.800 / 441.390 | -0.541% (-6.493%..+12.560%) | 495.135 / 479.020 |
| 2 mixed elements, 12 nodes | 21 x 20,000 | 0.3816 / 0.5196 | +35.841% (+0.097%..+56.530%) | 0.44325 / 0.78735 |

Raw output is in `verification/dbf5_consumer_benchmark*.txt`. This host was not
reserved or frequency-pinned; distributions include scheduling noise. The tiny
batch exposes the fixed weak-lifetime validation cost. The earlier <=1.3% claim
is **not** a general contract guarantee: the large-batch median is within
measurement noise, while sample spread and small batches exceed that threshold. Real migration must repeat
on target hardware, realistic element batches and actual kernels, with a
pre-agreed threshold and model-by-model numerical acceptance.

## Handoff / exclusions

Changed files are field.hpp, hex8.hpp, consumer_views.hpp, the consumer tests,
consumer benchmark and this document (shared build registration is integrated
by the parent task). No production solver, public legacy API, numerical
algorithm, constitutive model or wire format is changed by this subtask.
Compilation assumes the independent C++20 standalone module and the existing
supported TET4/HEX8 reference subset. Final production verification, physical
contact behavior, GPU compilation, race campaigns for production adapters and
new element-family parity remain for the real buildable environment. Register
identical persistent fields before restart restore, rebuild consumers after any
owner replacement, and repeat legacy/new equation mapping and golden-model gates
before switching any production call site.
