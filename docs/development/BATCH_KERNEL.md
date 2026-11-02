# Eight-lane TET kernel

Select --tet-kernel batch8 for CpuRun. --omp enables deterministic OpenMP execution. The plan owns packed eight-lane AoSoA connectivity, reference gradients and material constants. Only valid elements are scattered into the original element/IP buffers; HEX and J2 postprocessing continue in their established paths. Rebuild and use the same kernel selection for checkpoint append.

Windows GCC 15.2, -O2 -fopenmp, four workers, 33334 TETs (partial final block), 10 force passes. Median of three trials in milliseconds. Both precisions passed the bitwise raw force oracle. The mixed HEX/TET lifecycle also ran through the CLI.

| Precision | Scalar | Batch8 |
|---|---:|---:|
| 32 | 7.374 | 6.269 |
| 64 | 8.882 | 7.302 |

The optional tiled8 variant buckets elements by the neighborhood of their node
indices, orders a private node table by first use and stages updated displacements
through this table each pass. Mesh labels, stored connectivity, integration
points and the global assembly order remain canonical. Its dedicated scratch
requests large pages on Windows, with ordinary pages on privilege/allocation
failure; Linux reports THP advice separately from actual page promotion.
Windows reported ordinary pages on this machine. A 33334-element four-thread
raw comparison passed in both precisions with zero hot aligned allocations.
No cache-miss or TLB-counter improvement is claimed.

mixed8 opts an FP64 elastic TET4 run into FP32 reference tables and element
arithmetic while retaining FP64 state, integrator and global assembly. It checks
finite float inputs/forces and samples one canonical element in every 32 per
increment, rotating with the state step. The maximum observed norm-relative
force, stress or strain discrepancy against a double scalar evaluation is
reported by the CLI. It is a sampled diagnostic, not an upper error bound.
The forced fixture completed 2000 increments with observed peak 1.31e-7 or
less. batch_kernel_contracts tests shuffled connectivity, idle elements, tails,
both worker policies and numerical drift over 36 steps. Other physics and
native single precision reject mixed8 rather than downgrading material history.
