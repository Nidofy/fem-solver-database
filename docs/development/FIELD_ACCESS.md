# Storage access development, 2026-11-06

This additive API concerns dense field storage only. It contains no domain-specific
calculation, input format, model fixture or external implementation.

## First delivery

`sdb/field_access.hpp` offers two opt-in raw borrowing helpers:

* `component_span(view, c)` exposes a contiguous SoA component without allocation
  or copying. It preserves constness and rejects invalid components/null storage.
* `DenseAccessPlan<C, Layout>(rows, permutation)` validates a complete permutation
  of entity-major scalar indices and precomputes physical offsets for SoA or AoSoA.
  A plan may be shared by several fields of identical dimensions/layout. `bind(view)`
  checks shape once. The returned borrow uses one offset lookup per scalar access,
  with no repeated division/modulo/layout transform. `at()` additionally checks bounds.

Use `field.write()` before obtaining mutable borrows: COW detachment/versioning
still occurs at that boundary. Helpers never write through a read-only view or
detach behind a caller's back. Raw borrows must not outlive the field storage;
resize/move/swap/destruction, sharing or a subsequent COW detach can invalidate them.
Mapped borrows also depend on the plan: do not move/assign/destroy it while borrowed.
Binding a temporary plan is rejected. Concurrent access requires immutable plans
and nonoverlapping writes; synchronization is the caller's responsibility.
Checked lifetime monitoring is not provided by these hot-path helpers.

```cpp
auto raw = field.write();
auto column = sdb::component_span(raw, 0);
for (auto &value : column) value *= 2;
// When application order is mandatory:
sdb::DenseAccessPlan<3> plan(field.size(), logical_to_dense);
auto values = plan.bind(raw);
for (std::size_t i=0; i<values.size(); ++i) values[i] += 1;
```

The plan costs one size_t per scalar and O(N) setup, plus a temporary validation
bitset. It does not reorder storage, remove random gathers, or guarantee faster
execution. Empty fields are supported; partial/subset maps are intentionally rejected.
Existing Field/View layout, serialization and public behavior remain unchanged.

## Validation and measurement

`sdb_field_access_tests`: SoA and padded AoSoA-8 (35 rows), nontrivial permutation,
read/write equivalence, bounds/shape/null rejection, empty views, constness and COW.
`sdb_field_access_benchmark`: deterministic random permutation over 160000 rows of
three components, identical scalar operations, alternating execution order,
one discarded warmup pair and seven measured pairs. All final values checked.
Setup and plan memory are separate from traversal median times. This is a synthetic
access benchmark only; no end-to-end application speedup is inferred.

Build targets: `sdb_field_access_tests`, `sdb_field_access_benchmark`.
CTest name: `field_access`. Compiler flags/hardware affect results; do not impose
a universal speedup threshold or compare different builds as a controlled pair.

## Next bounded stages

The immutable batch-view stage is now implemented and independently tested; see
[MESH_ACCESS.md](MESH_ACCESS.md). It reuses existing packed storage without changing
MeshDB layout or adding another connectivity cache. Remaining stages stay opt-in.

1. Measure component-major contiguous access versus mapped order on independent
   workloads, recording allocation count and address-plan overhead.
2. Evaluate typed immutable connectivity/material read views that resolve metadata
   outside repeated access; preserve IDs, layout and ownership contracts.
3. Add multi-field/chunk iteration only when measurements show overhead remains;
   avoid introducing duplicated shadow state or changing default storage prematurely.
4. Test multiple compilers and layout sizes before promoting any path to default.

No production dependency or external performance data is bundled with this change.

## Local evidence

GCC 15.2, Windows, Release: fresh standalone CMake targets built successfully;
`ctest -R '^field_access$'` passed 1/1. MinGW targets link static runtime to avoid
ambient DLL selection. Other compilers/platforms have not been verified for this
increment. No preexisting implementation was modified beyond CMake registration.

One CMake benchmark invocation (7 measured pairs) reported dynamic median 0.066303 s,
planned median 0.063586 s, setup 0.001057 s, plan 3840000 bytes; all values verified.
A separate direct build reported 0.065971/0.058898 s. Variation across invocations
reinforces that these observations are not a portable throughput guarantee.
