# DBF1 verification evidence

2026-10-29, standalone module only. All final build/test processes exited 0.
This is implementation self-verification: **READY_FOR_REVIEW**, not ACCEPTED.
The immutable content SHA and per-file hashes are in the DBF1 manifest produced
after the content commit. No runtime/test changes follow the final matrix.

| Configuration | Result | Time | Evidence under verification/reviews/DBF1 |
|---|---|---|---|
| GCC 15.2 Release OpenMP, OMP_NUM_THREADS=4 | 15/15 | 15.62 s | omp-build.txt, omp-build-final.txt, omp-ctest.txt |
| GCC 15.2 Release serial | 15/15 | 15.26 s | serial-build.txt, serial-build-final.txt, serial-ctest.txt |
| MSVC 14.44 RelWithDebInfo AddressSanitizer serial | 15/15; no sanitizer diagnostic | 56.01 s | asan.txt |
| Foundation API/float/double cases | 171 checks | functional test | foundation-checks.txt |
| Storage API/layout/integer/validity cases | 82 checks | functional test | storage-metrics.txt |
| Frozen-baseline defect probe | exit 1, both expected failures | red | regression-red.txt |
| Same probe/current headers | exit 0, both refusals preserve semantics | green | regression-green.txt |

The original pre-edit OpenMP suite also passed 11/11 in 17.08 s (tool transcript).
The red probe compiles against `git archive 9a3d754 include`; it does not rely on
runtime modifications or forged expected results. Probe compilation uses C++20
O0, without OpenMP. Full builds use the repository's existing configurations.

New tests registered in CMake:

- foundation_contracts: schemas, initialization, mixed 8/1 IP storage, queries,
  execution gates, same-count rename/reorder/version/component/shear/initial-value
  mismatch rejection, failed-restore preservation, descriptor/layout checks,
  native enum/bool wire oracle and decode budgets.
- foundation_process_reader: a distinct executable invocation loads synthetic
  model/restart files and checks manual values, including history sample 35=17.
- storage_contracts: exact signed int64 values beyond 2^53, missing vs zero,
  malformed offsets/order/flags, schema/type/width errors, AoSoA and expired views.
- foundation_legacy_golden: Python verifies SHA256 of frozen pre-change hex
  artifacts, invokes sdb_run to load old model/restart into a new run, checks
  byte-identical model re-save, then independently queries results.

The golden model/restart were produced by the pre-change build-b01 executable
at source REVIEW_HEAD before rebuilding. Provenance, byte lengths and SHA256 are
in tests/fixtures/foundation/legacy-provenance.json.
The exact baseline CLI and headers were also rebuilt with GCC C++20 O0, and
both generated files matched these golden bytes (golden-source-reproduction.txt).
This pair is not exhaustive coverage of every historical file version. Existing lifecycle_process_pipeline
continues to cover float/double, native/indexed, sync/async and split restart.

## Storage scale observations

| Logical rows | Tracked Field bytes (ragged+sparse) | Ragged encoded bytes | Sparse encoded bytes |
|---:|---:|---:|---:|
| 0 | 0 | 40 | 48 |
| 16 | 768 | 552 | 512 |
| 1024 | 49152 | 32808 | 29744 |
| 16384 | 786432 | 524328 | 475184 |

These are actual counters/encoded byte sizes from the test, not speedup claims
or physical disk I/O. Field bytes exclude standard-vector metadata, DTO copies,
wire buffers and process overhead. Metadata growth is explicit in encoded_bytes.
No device transfers, cache-cold benchmarks or process peak memory are claimed.

## Reproduction and limitations

Use verification/reviews/DBF1/reproduce.ps1 with CMake, Ninja and Compiler paths
for fresh GCC serial/OpenMP builds. Existing build names were build-b01 and
build-b01-serial. Put the matching compiler bin first in PATH. For the actual
ASan run: `cmd /c verification\reviews\ALR1\run-asan.cmd`, exit 0; this invokes
the installed MSVC 14.44 developer environment and build-b01-asan.

CUDA remains BLOCKED_ENV/NOT_RUN (no usable nvcc toolchain established), and
changed shared headers are **not locally compiled with CUDA**. Linux, UBSan,
TSan, process-crash/power-loss campaigns, full D7 storage composite and parent
production solver are **not locally verified**. No root production build was
attempted. Existing GCC optimized native-vector warnings are unsuppressed;
passing ASan does not classify those diagnostics as false positives.
