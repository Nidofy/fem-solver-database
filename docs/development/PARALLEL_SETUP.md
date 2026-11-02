# Deterministic CPU setup

ModelBuilder::finalize(policy) and ModelCompiler::compile(ir, policy) accept an optional setup policy. CpuRun(model, controls, policy) uses the selected policy for TET geometry and deterministic stepping. The existing argument lists still select serial execution. A parallel build overlaps the unchanged canonical FNV byte stream with independent batch packing for meshes of at least 4096 elements. Shared-node reductions and material indexing retain element order.

Windows GCC 15.2, 200002 nodes, 66667 TETs, median of three runs per precision/policy. Timings include active workstation noise.

| Precision | Threads | finalize ms | TetPlan ms | end-to-end ms |
|---|---:|---:|---:|---:|
| 32 | 1 | 36.82 | 7.37 | 113.32 |
| 32 | 4 | 34.10 | 6.17 | 88.34 |
| 64 | 1 | 37.22 | 9.73 | 138.60 |
| 64 | 4 | 34.48 | 8.63 | 110.56 |

Both policies produced identical fingerprints and passed the raw force comparison. The CPU lifecycle benchmark also ran its non-due snapshot and restart check with the new omp argument. The checked consumer passed 178634 parity assertions.
