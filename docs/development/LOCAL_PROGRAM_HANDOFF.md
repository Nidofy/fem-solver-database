# Standalone CPU development handoff

Development branch: codex/standalone-database. Starting revision: d10b5ae.
The scope follows the requested A-F local program. A1-A3 had already shipped
when this sequence began. R9-R11, production solver integration and actual
CUDA-device acceptance are separate tracks.

| Area | Reproducible result and boundary |
|---|---|
| A4 | Elastic TET4 in current configuration, Cauchy stress, Euler-Almansi strain, finite tangent step checks and follower face pressure. The additional Green-Naghdi rate update is a separately tested material-point API; the lifecycle uses the total hyperelastic law. [Formulation and fixtures](FINITE_TET.md) |
| A5 | Solid body/face, rotating-frame, hydrostatic and traction loads with amplitude, propagation and reset. Check supported type/configuration pairs before migration. [Solid keyword matrix](SOLID_DLOAD_MATRIX.md), [shell and beam loads](SECTION_DLOAD.md) |
| A6 | Six-DOF rectangular B31 and flat rectangular S4R in the separate sdb_section_run lifecycle, with rectangular beam fibers, shell thickness samples, translational/rotational energies, distributed loads and restart. [Section contract](SECTION_ELEMENTS.md) |
| B1 | OS core classes, allowed CPU/NUMA sets, affinity launcher, first-touch option, 1-16 thread trials. Six high-class and ten low-class logical processors share one visible NUMA domain. [66 sample rows and interpretation](CPU_TOPOLOGY.md) |
| B2 | Deterministic parallel reference geometry calculation and batch packing. The unchanged FNV stream overlaps independent setup work. [Phase timings](PARALLEL_SETUP.md) |
| B3/B4 | Width-eight AoSoA, private tiled node order, large-page request with observable fallback, mixed FP32 element/FP64 global mode and drift audit. GCC confirmed vectorized inner loops. [Numerical and timing checks](BATCH_KERNEL.md) |
| B5 | Paired base/candidate PR workflow with five interleaved trials, median thresholds and matching workload/affinity. Hosted PR execution remains pending. [Gate](PERFORMANCE_GATE.md) |
| B6 | Persistent surface/rounded-edge BVH refit, bounded deterministic candidates and stationary-triangle CCD query. [Coverage and isolated timing](CONTACT_SEARCH_B6.md) |
| C1/C2 | Explicit penalty equations and kinematic/distributing couplings, with six-DOF rigid-arm force/moment transfer in section models; versioned C material ABI and validated trial registry. [Constraints](PENALTY_CONSTRAINTS.md), [registry](MATERIAL_PLUGIN.md) |
| C3 | Mechanical energy/work accounting persisted across restart, typed section/contact fields. [Energy ledger](ENERGY_AUDIT.md), [publishers](MECHANICAL_PUBLISHERS.md) |
| C4 | Immutable mapped indexed-file reads and bounded ordered parallel zstd compression. [Mapped query](MAPPED_RESULTS.md), [compression](PARALLEL_COMPRESSION.md) |
| D1/D2 | Three independent CLI reference histories, manufactured full/reduced brick refinement and a [public cantilever bending reference](CANTILEVER_REFERENCE.md). At 32 elements the B31 frequency differs from the slender-beam limit by 0.03441%. |
| D3/D4 | Clang libFuzzer INP/model/checkpoint smoke campaigns; private Linux ENOSPC and fsync/EIO publication failures and retry. [Coverage](IO_FUZZING.md), [faults](PUBLICATION_IO_FAILURES.md) |
| E | BLOCKED_ENV. Neither Windows PATH nor WSL exposed nvcc. Windows had no CUDA_PATH, compute-sanitizer or nvidia-smi on PATH. The toolkit was not installed in this round. PTX, kernel compilation, actual device timing and sanitizer checks remain unqualified. |
| F1-F3 | All 2047 source keys have draft layouts and confidence tags, 39 result dispatch entries and 28 writers are inventoried, and synthetic Win64 layouts cover all 25 ASC families. [Mapping and reader](LEGACY_LAYOUT_DRAFT.md), [output inventory](RESULT_FORMAT_INVENTORY.md) |

## Running the checks

Configure this standalone repository with CMake 3.24 or newer and a C++20
compiler. Enable SDB_C_API for the FFI tests. Linux SDB_IO_FAULT_TESTS enables
the isolated publication-failure campaign. SDB_ZSTD additionally requires a
real zstd 1.5.5+ SDK. SDB_FUZZING uses a separate Clang build. Run CTest from
the build directory with output on failure; set OMP_NUM_THREADS explicitly
when reproducing a timing result.

## Frozen-source verification

The final executable source revision is 367f1e19580260cddf8ac24d3651751c169db5bd.
All 1507 tracked blobs in the isolated Linux source snapshot matched Git.
The GCC 13.3 build enabled OpenMP, the C ABI, real zstd and the injected IO
failures. **79 of 79 CTests passed**. The installed section runner advanced
1000 increments and the installed reader verified both frames. Windows ASan,
Linux Index64 ASan/UBSan and Linux TSan were additional targeted runs. See
[per-test results and configuration](LOCAL_PROGRAM_VERIFICATION.json).
The earlier WSL build on a mounted Windows filesystem timed out writing the
100000-frame indexed fixture; that same test passed on the frozen native
Linux filesystem in 10.84 seconds. These are development-branch self-tests
and independent local reviews; industrial qualification is pending.

## Evidence that requires another environment

The Windows C4 mapping holds its read-only file handle until the view is released. POSIX mapped files must remain immutable for the lifetime of the reader.

Windows supplied only one NUMA domain; process affinity and page-touch
execution were exercised but inter-socket bandwidth and remote placement were
not measured. Windows used ordinary-page fallback here; Linux THP advice is
a request, not confirmation of promotion. This WSL environment exposed no
hardware cache/TLB PMU device or perf executable. The historical 8-thread
regression did not recur in the controlled matrix. Real FNL/FEG capture,
ZDF/ZBF binary comparisons, commercial-solver trajectories, physical
power-loss tests and formal external qualification require their respective
environments. Preserve field confidence and the unqualified mapping status
until captured bytes and engineering results have been compared.
