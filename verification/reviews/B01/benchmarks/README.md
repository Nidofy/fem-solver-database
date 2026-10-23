# B01 benchmarks — corrected labeling (repair of B01-A-001 / B01-O-002)

**The original `lifecycle_*-omp4.jsonl` / `*-serial.jsonl` files in
`superseded/` are mislabeled.** Every lifecycle-benchmark run — including the
ones tagged `omp4` — executed the **serial** CPU policy: `CpuRun` constructs
`CpuExplicit` with its default `CpuPolicy::Serial`
(`include/sdb/lifecycle.hpp:63`, `include/sdb/dynamics.hpp:171`), and no
environment variable can select OpenMP through the lifecycle API. The
reviewer's link-level probe measured `lifecycle_parallel_launches=0` with an
explicit OpenMP positive control of 1
(`sdb-review-b01-handoff/reports/astra-evidence/policy.log`).

Consequences, accepted by this repair:

1. The original R0.04 statement "OMP4 ≈ serial (bandwidth-bound)" is
   **retracted**: those rows were two serial runs under different
   `OMP_NUM_THREADS` environment values. No parallel-efficiency or
   bandwidth diagnosis for the lifecycle path is supported by this data.
2. The eager-vs-lazy publication A/B remains valid **as serial measurements**
   (both sides serial, same conditions); the publication savings conclusions
   stand.
3. A true OpenMP lifecycle baseline does not exist yet: the lifecycle API has
   no policy selection. Measuring one requires a deliberate, documented
   public API change (out of B01 repair scope; candidate for R2 work). The
   low-level `sdb_benchmark` (benchmarks/benchmark.cpp) does select
   `CpuPolicy::OpenMP` explicitly via its `omp` argument.

The fresh `lifecycle_*.jsonl` files in this directory supersede the
`superseded/` ones: identical measurement protocol, but tags are
`serial-env1` / `serial-env4` (environment `OMP_NUM_THREADS`), and every
record embeds machine-readable policy fields
(`execution_policy:"serial"`, `omp_compiled`, `omp_max_threads`, `omp_env`)
so a future mislabel cannot recur silently. Raw superseded bytes are retained
for provenance; do not cite their tags.
