# B01 build & test evidence (commands, exit codes, environments)

All runs executed 2026-10-24 from repository root in Git Bash on the B01
branch at the commits listed. Machine: Windows 10.0.26200 x64, 16 logical
cores, 32 GB RAM class desktop.

## Final batch-head regression (all at content HEAD 719c3fb)

### MinGW-w64 g++ 15.2.0 — Release, OpenMP ON (build-b01)

```sh
export PATH="/d/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin:$PATH"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
"$CMAKE" -S . -B build-b01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON \
  -DCMAKE_CXX_COMPILER="D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe" \
  -DCMAKE_MAKE_PROGRAM="$NINJA"        # exit 0
"$NINJA" -C build-b01                  # exit 0 (20/20 targets)
"$CMAKE" --build build-b01 --target test
# → "100% tests passed, 0 tests failed out of 8", exit 0
```

Tests: general_contact_contracts, general_contact_demo, general_contact_geometry,
general_contact_domain_demo, lifecycle_contracts, lifecycle_process_pipeline,
database_contracts, explicit_numerics. `lifecycle_contracts` reports
`lifecycle checks=14284 PASS` (float+double both, restart exactness included).

### MinGW — Release, serial (build-b01-serial)

Same as above with `-DSDB_OPENMP=OFF`. Build exit 0; CTest
`100% tests passed, 0 tests failed out of 8`, exit 0.

### MSVC 14.44 — RelWithDebInfo, ASan+UBSan, serial (build-b01-asan)

Script (`vcvars64 -vcvars_ver=14.44` then cmake configure with
`-DSDB_SANITIZE=ON -DSDB_OPENMP=OFF`, build `-j 6`, ctest): exit 0,
`100% tests passed, 0 tests failed out of 8`, no sanitizer diagnostics.

### Byte-compatibility checks (final HEAD binaries)

```sh
./build-b01/sdb_run.exe --input tests/fixtures/abaqus/forced_tet.inp --output F.sdbres
cmp F.sdbres <pre-fix reference>       # IDENTICAL (double)
# also verified: float run, U-only INP variant, --stop-after 700 and 733
# with --checkpoint (restart files byte-identical to pre-fix binaries)
```

Pre-fix references were produced by the untouched pre-change binaries from the
user's original `build/` directory (same source aaa74af) and by build-b01
before the change; both agree.

## Intermediate task-level runs (see task-evidence/*.md for details)

- R0.02 matrix (3 configs) at a3779f0: 8/8 each.
- R1.01 exposing run (eager publish, counter+tests compiled): FAILED at
  `no-output publication costs no force evaluations` (expected exposure).
- R1.01 post-fix: `lifecycle checks=6154 PASS` → after R1.02: `14284 PASS`.
- R0.04 benchmark matrix: 16 runs total (see `benchmarks/*.jsonl`).

## Environment blockers

- CUDA suite: not runnable — no NVIDIA GPU / nvcc / toolkit (R0.05 evidence).
  Configure attempt error recorded verbatim in task-evidence/R0.05.md.
