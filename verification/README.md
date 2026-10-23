# Standalone verification

Recorded on 2026-10-23 before extracting this directory into an independent Git
repository. These results apply to this module, not to any parent application.

| Configuration | Result |
|---|---|
| Windows MinGW GCC 15.2, Release, OpenMP enabled | 8/8 CTest passed |
| Windows MSVC 14.44, AddressSanitizer, OpenMP disabled | 8/8 CTest passed |
| Installed independent CMake consumer | Passed |

The suite includes 195 base database checks, 148 lifecycle checks, 1232 contact
contract checks and 4782 geometry/bundle checks in the MinGW/OpenMP configuration.
It also runs independent-process result/restart tests and explicit/implicit
contact examples. Numerical checks cover force-Jacobian differences, rigid spin,
force/energy patches, deterministic parallel comparison and analytical rebound.
Persistence checks cover CRC/truncation/budgets, multiple scalar/history chunks,
rollback and atomic mechanical/contact restoration.

Reproduce from the repository root with a C++20 compiler and CMake on PATH:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON -DSDB_CUDA=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Portable PowerShell entry points: `reproduce.ps1` (including benchmarks) and
`reproduce_lifecycle.ps1` (input/model/results/restart). Use fresh build/output
directories when changing toolchains. `summarize_benchmarks.py` processes locally
generated benchmark data.

CUDA, Linux, MPI, full commercial feature parity and large-contact scaling are
not locally verified. MSVC retains alignment-padding warnings. Raw historical
host logs, parent integration notes and parent-referencing source manifests are
intentionally excluded from this independent repository and retained locally.
