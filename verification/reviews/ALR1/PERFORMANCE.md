# ALR1 measured performance

Three repetitions; cells are median [minimum, maximum], milliseconds unless noted.
GCC 15.2 Release, Intel Core Ultra 9 285H; CpuRun executes serial, even in the OpenMP build.
OS cache was not evicted. Setup/first-step/steady-state are separated; no cold-disk claim.

## Indexed lifecycle stage harness

Tiny fixtures exercise I/O overhead and all integration paths; they do not establish scalable kernel throughput.
Every row writes 21 frames. TET has 5 nodes/2 elements/2000 increments; HEX has 8/1/1000; mixed has 9/2/1000.
All 36 runs internally verify exact restart state equality.

| Case | End to end through output drain | Snapshot total | Write/admit | Drain | Warm reopen | Random 20 frames | Node history |
|---|---|---|---|---|---|---|---|
| forced_tet.inp/32bit/sync | 9.053 [8.878, 14.6] | 0.185 [0.182, 0.284] | 1.734 [1.674, 2.948] | 0 [0, 0] | 0.133 [0.129, 0.255] | 1.123 [1.071, 1.75] | 1.177 [1.157, 2.036] |
| forced_tet.inp/64bit/sync | 7.395 [6.088, 21.67] | 0.179 [0.171, 0.328] | 1.714 [1.711, 2.785] | 0.001 [0, 0.001] | 0.138 [0.113, 0.299] | 1.098 [1.094, 2.106] | 1.153 [1.148, 3.451] |
| forced_tet.inp/32bit/async | 6.314 [5.831, 19.86] | 0.336 [0.255, 0.394] | 0.195 [0.099, 0.587] | 1.164 [1.121, 2.128] | 0.112 [0.11, 0.193] | 1.138 [1.122, 1.831] | 1.174 [1.143, 1.869] |
| forced_tet.inp/64bit/async | 6.774 [5.705, 26.03] | 0.302 [0.226, 0.393] | 0.208 [0.081, 0.418] | 1.182 [1.117, 10.5] | 0.109 [0.103, 0.187] | 1.111 [1.098, 1.703] | 1.154 [1.14, 1.82] |
| forced_hex.inp/32bit/sync | 8.305 [7.867, 29.3] | 0.22 [0.203, 0.516] | 3.033 [2.979, 7.341] | 0 [0, 0] | 0.112 [0.106, 0.18] | 2.203 [2.089, 3.385] | 1.647 [1.623, 2.82] |
| forced_hex.inp/64bit/sync | 8.689 [7.828, 29.51] | 0.236 [0.223, 0.399] | 3.011 [2.956, 4.872] | 0 [0, 0] | 0.125 [0.108, 0.196] | 2.112 [2.088, 3.444] | 1.505 [1.496, 2.62] |
| forced_hex.inp/32bit/async | 7.952 [7.662, 25.67] | 0.342 [0.323, 0.546] | 0.883 [0.784, 1.877] | 1.749 [1.74, 2.839] | 0.145 [0.106, 0.205] | 2.162 [2.146, 6.407] | 1.501 [1.498, 2.849] |
| forced_hex.inp/64bit/async | 8.766 [8.327, 24.11] | 0.343 [0.324, 5.645] | 1.563 [0.78, 5.298] | 2.132 [1.841, 3.014] | 0.109 [0.109, 0.161] | 2.216 [2.201, 3.223] | 1.595 [1.524, 2.408] |
| mixed_hex_tet.inp/32bit/sync | 8.936 [8.211, 16.88] | 0.241 [0.206, 0.335] | 3.117 [3.049, 6.608] | 0 [0, 0.001] | 0.224 [0.119, 0.301] | 2.169 [2.139, 3.86] | 1.506 [1.501, 2.816] |
| mixed_hex_tet.inp/64bit/sync | 8.842 [7.451, 16.17] | 0.194 [0.19, 0.446] | 3.09 [3.045, 5.384] | 0 [0, 0.001] | 0.142 [0.116, 0.269] | 2.179 [2.119, 5.112] | 1.538 [1.51, 10.2] |
| mixed_hex_tet.inp/32bit/async | 8.169 [7.649, 13.71] | 0.386 [0.297, 0.521] | 0.939 [0.88, 1.244] | 1.734 [1.68, 2.639] | 0.112 [0.11, 0.205] | 2.153 [2.081, 6.121] | 1.545 [1.495, 2.646] |
| mixed_hex_tet.inp/64bit/async | 8.475 [8.291, 18.09] | 0.304 [0.282, 0.549] | 1.008 [0.904, 1.293] | 1.922 [1.899, 2.735] | 0.156 [0.116, 0.177] | 2.351 [2.13, 4.156] | 1.606 [1.515, 2.723] |

End to end includes parse, compile, plan, initialize, storage setup, run and drain.
Checkpoint, reader and restart timing are separate. Standalone v1 encode diagnostics run outside this interval.
Sync write time and async admission time measure different operations; use end-to-end+drain for comparison.
The JSON includes every measured stage, file bytes, reader bytes, decoded bytes, queue charge and process peak.
The process peak is cumulative across cases in a process, not attributable to one phase.

## Independent 20,000-node / 5,000-TET legacy workload

Double, serial, 10 executed increments, 1 output frame; native v1. Compiled OpenMP maximum=4 is not its execution policy.

| Metric | Median [min, max] |
|---|---|
| compile_ms | 11.57 [11.49, 11.87] |
| run_setup_ms | 1.866 [1.775, 1.958] |
| initialize_ms | 0.225 [0.218, 0.234] |
| kernel_only_ms | 0.128 [0.127, 0.129] |
| steady_kernel_per_increment_ms | 0.2047 [0.2046, 0.2056] |
| snapshot_ms | 0.15 [0.133, 0.16] |
| native_write_ms | 28.62 [28.36, 32.16] |
| reader_scan_query_ms | 32.97 [32.41, 33.37] |
| restart_write_ms | 38.13 [36.58, 38.53] |
| restart_restore_ms | 33.77 [33.29, 34.09] |
| peak_process_bytes | 3.296e+07 [3.296e+07, 3.301e+07] |

## Independent 100,000-frame persistent-index workload

| Metric | Median [min, max] |
|---|---|
| frames | 1e+05 [1e+05, 1e+05] |
| write_ms | 1.045e+04 [1.039e+04, 1.225e+04] |
| open_ms | 4.883 [4.443, 5.116] |
| query_read_bytes | 2.518e+04 [2.518e+04, 2.518e+04] |
| value_bytes | 120 [120, 120] |

Five random single-entity queries decode 120 bytes total. An independent 100,000-node field test
asserts that a selected entity decodes no more than its 256-row block. See indexed_results_tests.cpp.
The additional index-memory.json run measures whole-test process peak, including recovery and malicious-input controls.
Reader counters count requested encoded bytes (including metadata), not physical disk sectors or OS-cache misses.
Legacy v1 reader counters are not instrumented. CUDA transfer/VRAM measurements are NOT_RUN.
No raw/view speedup, commercial solver comparison, compression throughput or power-loss durability is claimed.
