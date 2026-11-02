# Eight-lane TET kernel

Select --tet-kernel batch8 for CpuRun. --omp enables deterministic OpenMP execution. The plan owns packed eight-lane AoSoA connectivity, reference gradients and material constants. Only valid elements are scattered into the original element/IP buffers; HEX and J2 postprocessing continue in their established paths. Rebuild and use the same kernel selection for checkpoint append.

Windows GCC 15.2, -O2 -fopenmp, four workers, 33334 TETs (partial final block), 10 force passes. Median of three trials in milliseconds. Both precisions passed the bitwise raw force oracle. The mixed HEX/TET lifecycle also ran through the CLI.

| Precision | Scalar | Batch8 |
|---|---:|---:|
| 32 | 7.374 | 6.269 |
| 64 | 8.882 | 7.302 |
