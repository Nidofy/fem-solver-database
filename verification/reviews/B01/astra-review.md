# B01 — Independent Astra Reviewer A batch review

Date: 2026-10-24 (Asia/Shanghai). Reviewer: actual GPT-6 Astra, independent first pass. No Reviewer B/Opus report was read or used. Scope: B01 only, not optional B02c.

**Verdict: CHANGES_REQUESTED**  
**Batch Gate: FAIL**

The lazy publication implementation passes the CPU numerical and compatibility checks executed here. The blocking finding is a false parallel-performance baseline: every `omp4` lifecycle run actually uses the serial execution policy. Correct that evidence before using B01 as the baseline for R2 work. No C0 defect or new silent physical error was found in the reviewed change. This is not a production solver or CUDA acceptance.

## 1. Immutable scope and provenance

- BASE_SHA: `aaa74afb3c264bbd7ca43555a7445c77994ccaa2`.
- BATCH_HEAD_SHA actually reviewed: `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`.
- Manifest claims `719c3fb2a5cd3f6d8a8b13eec1b8a7c1d59dd1a4`; `git cat-file -t` fails. The user-authorized short SHA `719c3fb` resolves unambiguously to the actual head above. Evidence: `astra-evidence/identity.log`.
- Clone: `D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/sdb-review-b01-astra`, created with `git clone --shared --no-checkout solver_database_v2 sdb-review-b01-astra`, then `git checkout --detach 719c3fb`.
- Packet documentation copied from main repository HEAD `7fee69152c474ed1eb67ed13de9988bb51e2a691`; code remains at the fixed B01 content SHA. Initial directory copy landed in `verification/reviews/reviews/B01`; relevant packet files were copied into the expected B01 directory. Staging copy is retained as provenance, not a second review.
- All changed runtime headers, benchmark and tests were inspected in full diff; `astra-evidence/full-batch.diff` preserves the complete change. Parent AGENTS.md, static development skill, solution/repository/module maps, batch review prompt, masterplan, ordered packet and raw benchmark inputs were read.
- No production source, CMake file, GLM checkout, branch, numerical algorithm, or public API was changed. Review probes and experimental include copies are confined to this isolated clone.

## 2. Environment and commands actually executed

Windows NT 10.0.26200.0 x64; 16 logical processors; processor identifier Intel64 Family 6 Model 197 Stepping 2, GenuineIntel. Exact CPU marketing name and P/E topology were unavailable (CIM denied, registry query failed); neither is inferred. CMake 4.3.1-msvc1/Ninja from VS 18 Professional, MinGW-w64 GCC 15.2.0, C++20, Release `-O3 -DNDEBUG`, OpenMP 4.5. See `astra-evidence/environment.json`, `configure.log`, `build.log`, and CMake cache. No affinity pinning. Another independent review and normal desktop activity may run concurrently, so timings are diagnostic only.

From the isolated root, with MinGW `bin` prepended to PATH and `OMP_NUM_THREADS=4`:

```powershell
$cmake='C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest='C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
& $cmake -S . -B build-astra -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON `
 '-DCMAKE_CXX_COMPILER=D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe' `
 '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
& $cmake --build build-astra -j 4
& $ctest --test-dir build-astra --output-on-failure
g++ -std=c++20 -O3 -DNDEBUG -fopenmp -Iinclude verification/reviews/B01/astra-evidence/adversarial.cpp -o verification/reviews/B01/astra-evidence/adversarial.exe
verification/reviews/B01/astra-evidence/adversarial.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/B01/astra-evidence/combination-verified
g++ -std=c++20 -O3 -DNDEBUG -fopenmp -Iinclude verification/reviews/B01/astra-evidence/policy_probe.cpp '-Wl,--wrap=GOMP_parallel' -o verification/reviews/B01/astra-evidence/policy_probe.exe
verification/reviews/B01/astra-evidence/policy_probe.exe 200000 4096 verification/reviews/B01/astra-evidence/policy-bench
```

Results: configure/build exit 0, 20/20 build actions; CTest 8/8 PASS in 1.15 seconds; independent adversarial harness exit 0; policy probe exit 0. The shell call containing CTest also attempted CIM afterward and returned 1 from that separate hardware query; this is not a CTest failure. Original CTest output and `build-astra/Testing/Temporary/LastTest.log` are retained.

Additional executed commands:

- `git archive aaa74af include -o .../base-include.zip`, expanded to `astra-evidence/base-source`; compiled the same adversarial.cpp with `-I.../base-source/include` and reran it into `base-combinations`. Exit 0, eight cases PASS.
- SHA-256 comparison of all 24 corresponding `.sdbres`/`.sdbrst` files: **24/24 identical**, captured in `base-head-compatibility.json`.
- Created `astra-evidence/eager-include` as a copy of head headers, replaced only `sdb/results/results.hpp` with `git show aaa74af:include/sdb/results/results.hpp`; compiled the same policy_probe.cpp with that include root. This is an explicitly labelled A/B experiment, not the base commit binary. The head counter and save consistency change remain in both sides.
- Ran alternating eager/lazy 200k-node benchmarks three times each (4096 MiB budget, fresh directories), captured `reviewer-{eager,lazy}.jsonl`, `.stderr`, and `process-wall.csv`. All six exits 0.
- `git diff --check aaa74af 719c3fb`: Markdown trailing-space warnings only (intentional hard-break style in imported plans); no source-whitespace finding.

The first adversarial fixture attempted load release by clearing IR loads, which means inherited load, not `OP=NEW`; its analytic check correctly failed. The harness was corrected to use an empty `IRActionBlock` with `reset=true`, then rebuilt and rerun. `adversarial-initial-fixture-error.log` preserves this reviewer-fixture mistake; it is not a product finding.

## 3. Independent cross-task adversarial tests

Source: `astra-evidence/adversarial.cpp`; raw results: `adversarial.log`, `adversarial-base.log`.

| Combination | Design and oracle | Actual result |
|---|---|---|
| A: scheduling × dedup × regional history × restart append | U every 7 increments, full U/V/A/RF/S/E/ALLKE/ALLSE every 11, duplicate full request, FREE history on 13 intervals; stop 733 and 1000; double and float. Every full-run frame published twice and packed bytes compared. Resume appends to prefix and compares whole result file. Forced-oscillator analytic final U oracle. | All four variants PASS; 449 frames each; exact repeat and append; double error 3.02721e-8, float 3.26547e-8. |
| B: U-only step × dt/load transition × final-output scheduling × restart | Step 0 dt=1e-4, period .1, U-only; step 1 dt=2e-4, period .1, explicit OP=NEW load release and full/interval history output. Stop 733 (non-due) and 1000 (step boundary), double and float. Compare packed snapshots on restore, whole append file, final cursor 1500 and closed-form released oscillator. | All four variants PASS; 196 frames each; double error 7.40102e-8, float 3.89509e-8. |

Both combinations also passed against the actual base headers. The 24 matching artifacts prove byte compatibility for these cases, including a non-due checkpoint and a step-boundary checkpoint; they do not prove arbitrary unsupported physics.

## 4. L1/L2 architecture review and cross-module risk matrix

Data path checked: ReaderRegistry/Abaqus IR → ModelCompiler/CompiledModel → CpuRun/StateDB/CpuExplicit → ResultPublisher → NativeResultStorage/ResultReader → RestartDB and existing VTK/XDMF pipeline tests. The changed boundary is publication selection plus save-time constitutive refresh. General contact remains a separate reference module; it is not newly integrated into CpuRun.

| Task | Cross-module risk | Verified this review | Unsupported / unverified / disposition |
|---|---|---|---|
| R0.01 | Immutable source identity and packet reproducibility | Base and real short-head resolved, detached clone, full diff stored | Manifest full head invalid; A-002. |
| R0.02 | Compiler/runtime sanitizers, process readers/exporters | Fresh MinGW Release/OpenMP build; all 8 tests incl. independent pipeline, numerical/reference and corruption checks | MSVC/serial configurations not rerun here. Claimed UBSan is not enabled by MSVC CMake branch; A-003. CUDA not verified. |
| R0.03 | Ledger, task scope and future handoff | Seven tasks and code boundary checked; freeze-doc SHA separated from content SHA | Correct propagated inaccurate performance/sanitizer/identity descriptions before handoff. No stable merge performed. |
| R0.04 | Real runtime policy, phase boundaries, I/O and scale claims | Own link-level parallel probe; same-source eager/lazy A/B 3 pairs at 200k; raw logs | omp4 is actually serial (A-001). Cold/kernel labels incomplete (A-004); no P/E/NUMA/cache-controlled, 1M repeated or end-to-end solver speedup proof. |
| R0.05 | CUDA host/device ownership and backend readiness | No CUDA source modified; packet correctly marks unavailable | BLOCKED_ENV; not locally compiled or device-verified; no CUDA PASS. |
| R1.01 | Due scheduling → U vs derived fields → CPU state | Due list order preserved; dedup still keyed name/region/history; force pass only if due non-U; CTest counters and independent combinations pass | Unknown future nonlinear/history constitutive updates need fresh contract; currently pure linear. Save still incurs a force pass. |
| R1.02 | Half-step velocity, load/dt transition, restart, result identity | Existing U/V/A/S analytic tests; own mixed schedules, release boundary, repeated snapshot, 24 base/head identical files | No production/non-TET4 physics validation; no proof for arbitrary concurrent mutation of public state. |

The due vector borrows request addresses only within a synchronous call; it neither escapes nor mutates the request vector. Output fields own their vectors. No new queue, stream, event, borrowed-pointer escape, asynchronous publication or lock is introduced. Current callers own and execute CpuRun serially; the added non-atomic evaluation counter is not a new safe-concurrency promise. `forces()` acquires the usual field views and overwrites linear derived fields without resizing topology or advancing history; view layout/stride/padding is unchanged. Existing database tests cover lifetime/layout contracts, but no new NUMA/race/device test is claimed.

`save()` now refreshes internal-force/stress/strain before serialization. This preserves eager CLI checkpoint bytes in tested scenarios and leaves trajectory state intact. It does change derived state/bytes for callers that previously saved without publication; the compat packet already discloses that limitation. Failure atomicity of all derived scratch fields on a failed save is not newly guaranteed. Model fingerprint, source ID mapping, file schemas, CRC and restore transactional replacement are unchanged; malformed-file paths were exercised by existing tests, not exhaustively fuzzed. The publisher still performs O(number of requests) due scanning, not unconditional O(1) for arbitrary request count.

## 5. Findings

### B01-A-001 — C1: advertised OMP4 performance baseline never enters OpenMP

**Locations:** `verification/reviews/B01/task-evidence/R0.04.md:24` and `:67`; `benchmarks/lifecycle_benchmark.cpp:79`; `include/sdb/lifecycle.hpp:63`; `include/sdb/dynamics.hpp:171` and `:13`.

**Trigger/reproduction:** build with SDB_OPENMP=ON, set OMP_NUM_THREADS=4 and run the lifecycle benchmark. CpuRun constructs `cpu_(model.mesh,state_)`; CpuExplicit's default policy is Serial. Environment thread count cannot override that enum. Run the supplied `policy_probe.cpp` with linker `--wrap=GOMP_parallel`: `policy.log` reports **lifecycle_parallel_launches=0**, then **after_explicit_OpenMP_control=1**. All six A/B runs reproduce zero lifecycle launches; the positive control confirms interception works.

**Impact:** the stated OMP4≈serial / bandwidth-limited threading-efficiency diagnosis is unsupported, and the supposed 1T/4T baseline is two serial runs. Feeding it into R2 optimization would prioritize a fictitious scaling result. Lazy-vs-eager *serial* publication savings remain real. This is an evidence/performance-contract defect, not a new physical error.

**Fix recommendation:** relabel existing rows as serial with environment thread-count variants, retract parallel-efficiency explanation in packet/ledger, and mark parallel baseline unavailable unless an explicitly selected OpenMP execution path is benchmarked. If adding CpuRun policy selection, document and test that public API change rather than silently assuming compilation selects a backend. Do not implement speculative bandwidth fixes from these numbers.

**Required regression:** assert/report actual execution policy and observed worker/parallel-region execution; run a serial negative control and OpenMP positive control. Same model, precision, outputs, step count, affinity and repeated sample protocol must be recorded before comparing 1T/4T.

### B01-A-002 — C2: frozen manifest names a nonexistent head object

**Location:** `verification/reviews/B01/manifest.yaml:11`.

**Reproduction:** `git cat-file -t 719c3fb2a5cd3f6d8a8b13eec1b8a7c1d59dd1a4` fails; `git rev-parse 719c3fb` returns `719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5`. Raw evidence in identity.log.

**Impact:** automated review/relay cannot independently check out the claimed full snapshot, and two reviewers could otherwise substitute different moving heads. This review proceeded only because the user explicitly fixed the short SHA and Git resolved it unambiguously.

**Fix recommendation:** write the actual full SHA, separately record packet/freeze SHA, and correct any propagated handoff value. Preserve the bad value in repair history.

**Required regression:** a packet validator must resolve both full SHAs as commits, assert base ancestry, and compare the expected fixed head; fail before benchmark/review dispatch on mismatch.

### B01-A-003 — C2: MSVC ASan evidence incorrectly claims UBSan

**Locations:** `verification/reviews/B01/build-test.md:33`, `manifest.yaml:17`, `compat-matrix.md:47`, `task-evidence/R0.02.md:32`; actual `CMakeLists.txt:17-24`.

**Reproduction/evidence:** under `if(MSVC)` the only sanitizer option is `/fsanitize=address`; `-fsanitize=address,undefined` exists only in the non-MSVC branch. The recorded MSVC configure command selects the MSVC branch. Thus an ASan run is not UBSan coverage. No independent UBSan executable/log is supplied. This conclusion follows from the repository flags and does not require assuming an external compiler capability.

**Impact:** arithmetic/other undefined-behavior coverage is overstated, affecting trust in R0.02's baseline and future regression claims. It does not invalidate the ASan or CPU tests that actually ran.

**Fix recommendation:** label MSVC as ASan only and mark UBSan unverified/BLOCKED_ENV, or add genuine supported UBSan execution with exact compiler flags and logs. Include the raw build and test logs in a reproducible packet instead of only summary assertions and a transient `/tmp` script reference.

**Required regression:** evidence-generation checks must derive sanitizer names from actual compile commands; a UBSan coverage claim must include an instrumented positive-control diagnostic and real suite results.

### B01-A-004 — C2: cold/kernel metric claims omit initialization and conflate full advance with kernel

**Locations:** `verification/reviews/B01/task-evidence/R0.04.md:9-17`; `benchmarks/lifecycle_benchmark.cpp:75-86`, `:91-95`, `:121-124`.

**Reproduction/evidence:** `compile_ms` stops at line 78, before CpuRun constructs StateDB/TetPlan (79) and initializes masses/state/first force (80). `kernel_ms` times `CpuRun::advance`, including loads, force assembly, integration and cursor/time updates, not a kernel-only region. The configured plan has 21 increments but the benchmark executes only 10 and writes one due-U frame. This is useful segmented measurement, but it is not a cold-start-through-first-output or complete-run I/O benchmark.

**Impact:** initialization/plan cost can change unseen and the required cold/kernel/steady/I/O comparison cannot be reproduced from these fields alone. Calling compile_ms IR→mesh→plan and describing 21 executed increments overstates coverage.

**Fix recommendation:** accurately rename/document existing measurements (retain compatibility aliases if needed); add explicit construction/initialize, actual kernel-only, full steady advance, and complete-run wall/I/O measurements, with executed increments/frames in JSON. A supported deferral must explicitly limit R0.04 claims; do not announce complete solver acceleration from isolated publication timings.

**Required regression:** benchmark smoke validation should check executed steps/frames and phase coverage, while performance comparison uses repeated cold/warm samples under recorded affinity/cache conditions. No brittle wall-time unit-test threshold is needed.

## 6. Performance conclusions (evidence, not promotional speedup)

Same model in own A/B: 200,000 nodes, 50,000 disconnected undeformed TET4, double, no load, identical compile options, generated 21-increment plan, only first 10 advanced, output frequency 10, one U frame written, same checkpoint behavior. The real work reduction is two discarded constitutive passes: force_evaluations 14→12; physics integration count, precision and written frame bytes are unchanged. It is a valid publication microbenchmark, not a realistic shared-node stressed production model.

Own three-sample medians, milliseconds:

| Metric | Eager experiment | Lazy fixed HEAD | Interpretation |
|---|---:|---:|---|
| IR compilation (cold component only) | 135.272 | 133.005 | Excludes runtime plan/state/init; no cold-start claim. |
| First full advance (`kernel_ms`) | 2.900 | 3.171 | Not kernel-only; noisy, not acceleration evidence. |
| Steady full advance / increment | 2.764125 | 2.666750 | Small difference under noise; one eager sample 4.445750. |
| Non-due snapshot | 1.436 | 0.013 | Avoided force pass confirmed by counter and source. |
| Due U snapshot | 3.187 | 1.717 | Avoided force pass; same U fields. |
| Native write | 290.927 | 294.212 | No I/O improvement established. |
| Read/scan/query | 343.622 | 344.728 | No reader improvement established. |
| Restart save incl. refresh | 340.378 | 350.537 | No improvement established. |
| Restart restore | 334.117 | 333.683 | No improvement established. |
| Full benchmark process wall (including generation/read/write/restore) | 1626.085 | 1616.019 | ~0.6% difference is not significant with this protocol/load. |
| True kernel-only | not measured | not measured | Required coverage gap, A-004. |

Raw developer 1M results are single samples, not repeated statistical baselines; their serial publication direction is consistent with this review. Their OMP4 interpretation is invalid. Cold hardware cache state was not controlled; P/E placement, NUMA and affinity unknown. I/O is normal cached filesystem I/O, not durability/flush-to-device latency. No GPU transfer/kernel/stream measurement exists. No whole-solver, GPU, 8T or production speedup is accepted.

## 7. Gate and repair order

**Batch Gate: FAIL** because B01-A-001 is C1 and the designated performance baseline is materially mislabelled. A-002/A-003/A-004 also require correction or explicit, bounded disposition. CUDA remains BLOCKED_ENV and cannot receive PASS. This review alone is not the combined two-reviewer gate; the coordinator owns final aggregation.

Recommended GLM priorities (no fixes made here):

1. Correct actual execution-policy evidence and remove invalid OpenMP efficiency diagnosis; remeasure only with a real supported parallel path or explicitly defer that baseline.
2. Correct and mechanically validate immutable SHA/freeze provenance.
3. Correct sanitizer labels and retain raw reproducible build/test evidence.
4. Correct benchmark phase/step metadata and fill or explicitly defer missing cold/kernel/end-to-end measurements.
5. Keep the independent combination cases as regression candidates when making targeted fixes; rerun CPU suite and base/head result compatibility after any behavioral change.

No need to split this narrowly scoped runtime change or revert lazy publication based on the verified evidence. No unnecessary new abstraction was introduced. Do not change the default numerical path merely to repair an evidence label. Revisit save-time extra evaluation before introducing nonlinear constitutive history in a later batch.

## 8. Handoff and limits

Files added: this report and `astra-evidence/` (probe sources, logs, experimental/base headers, binaries and reproducible output artifacts); frozen packet files copied into the isolated review tree. No implementation fix, no production compilation, no production source/API change, no Git commit/merge/push. All evidence remains local. MinGW CPU verification is real; MSVC/UBSan, Linux, CUDA and the external production solver are **not locally verified by this reviewer**. Assumptions: supported homogeneous-constraint, linear-TET4 lifecycle only, synchronous ownership as used by current callers. Migration: GLM should append focused repair commits and provide a new FIX_HEAD_SHA for incremental review; production integration requires its separate internal dependency/device/physics validation.
