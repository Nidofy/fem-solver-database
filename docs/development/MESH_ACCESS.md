# Immutable batch access — storage-only increment

## Scope and implementation

`sdb/mesh_access.hpp` adds `FixedBatchRead<T, N>` over existing MeshDB batches.
Batch topology width and storage dimensions are checked when the view is constructed;
material lookup and reference-coordinate borrowing happen once. No MeshDB/MaterialDB
implementation, schema, fingerprint, serialization or default layout is changed.
The view allocates no new packed connectivity and stores only references/metadata.

```cpp
sdb::FixedBatchRead<double, 4> batch(mesh, batch_index);
auto ids = batch.elements();           // dense global element indices, not labels
auto first_nodes = batch.column(0);    // contiguous existing local-node column
auto xyz = batch.coordinates();        // immutable global reference coordinates
const auto &material = batch.material();
auto chunk = batch.subspan(100, 200);  // same backing storage and original stride
```

`N` expresses connectivity width, not a unique topology: for example two topologies
may both have width four. Query `topology()` if a consumer requires a particular type.
Variable-width polygon batches are rejected; use the existing CSR query for those.
Material is the batch's base material. This view does not resolve sample-specific
material overrides; those remain available through the existing sample-point API.
`integration_points()` is batch metadata, not a substitute for sample layout queries.

Element order is exactly the existing batch order. This may differ from a global
application traversal; the helper never reorders entities or translates external IDs.
`column()` validates the column once, `at()` validates each access, and `node()` is
unchecked (require row<size() and local_node<N). `subspan()` checks ranges, accepts
an empty tail and preserves original column stride across nested subviews.

All data is read-only. The originating mesh must remain alive and unmoved while any
view, span, material reference or coordinate view is used. Construction from an rvalue
mesh is deleted. This is an explicit raw-borrow contract, not runtime lifetime tracking.
Concurrent reads are allowed while mesh lifetime is stable. Returned data cannot be
used to mutate mesh storage. Holding a view does not extend mesh ownership.

## Verification

Targets: `sdb_mesh_access_tests` and `sdb_mesh_access_benchmark`; CTest: `mesh_access`.
Tests cover float/double, mixed fixed/variable topology, multiple materials,
noncontiguous global IDs, zero-copy pointer identity, nested and empty subviews,
invalid ranges/width/batch index, rejected temporary owners, concurrent reads and
unchanged mesh fingerprint. Previous `field_access` regression remains enabled.

Local GCC 15.2 Windows Release: both CTest targets passed (2/2). Separate 64-bit
Index build is also checked. Other compilers, CUDA and production consumers have
not been verified for this increment. MinGW executables use static runtime linking.

## Synthetic measurement

The benchmark sums dense node indices and base material IDs using either per-record
queries or the prepared batch view. Both use identical batch/entity/local-node order
and validate an independently computed integer checksum. Setup is outside timing.
It uses 120000 four-node records, 16384 nodes, 30 traversals per sample, alternating
method order, one warmup pair and seven measured pairs. The geometry is synthetic
storage data; it is not a physical analysis or end-to-end workload.

One local run: scalar query median 0.005697 s, batch view median 0.000957 s,
checksum 117723830400. View size 80 bytes in this build; additional packed storage
zero. Existing MeshDB batches already consume packed storage; this API reuses it,
and does not claim that the underlying database has zero packing cost.

The measured difference includes removal of repeated bounds checks/material lookup
and compiler optimization of direct access. It must not be advertised as a generic
database or application speedup. Representative consumer integration and hardware
profiling are still needed before changing any default access strategy.

## Follow-up

1. Measure global-order workloads separately: batch traversal changes order and is
   not automatically interchangeable with a caller's numerical accumulation order.
2. Compare component-major field traversal with mapped traversal where the consumer
   explicitly permits reordering; retain mapping plans for order-sensitive workloads.
3. Evaluate chunk size and multi-field iteration with allocation/lifetime evidence;
   avoid introducing another copy of connectivity or state as a performance shortcut.
4. Cross-compiler verification before default adoption; current API remains opt-in.
