# ALR1 frozen implementation handoff

Date: 2026-10-29, Asia/Shanghai. State: **FROZEN_FOR_BATCH_REVIEW / READY_FOR_REVIEW**.
Implementation self-tests passed; no independent ACCEPTED verdict is claimed.

- Repository: `solver_database_v2`, remote `https://github.com/Nidofy/fem-solver-database.git`.
- Branch: `codex/astra-lifecycle-result-hardening`.
- BASE_SHA: `480da32a17e3b60e6694b95e5c9ec83618e5b64b`.
- Reviewed content HEAD_SHA: `8122adf2c149a672222ab81d8ac722e69931205c`.
- Runtime/test code SHA: `ca4b682e8181584c62bb1f5afe4e97a4f0276300`.
  The content HEAD adds the measured benchmark harness, CMake target and evidence.
- This handoff/ledger/README packet is a subsequent documentation-only freeze commit;
  resolve its exact SHA with `git log -1 --format=%H -- docs/development/astra_lifecycle_result/HANDOFF.md`.
  Do not substitute that packet SHA for the content SHA above.
- No push, stable-branch merge, release, new dependency or production-solver edit.
  GLM B02c (`b87fb53`, separate worktree) remains unaccepted and unintegrated.

## Implementation and commit map

| Area | Commit | Actual delivered boundary | Evidence |
|---|---|---|---|
| A0 | `8122adf` plus per-task logs | reproducible standalone CPU matrix and stage benchmark | final-*-ctest.txt, final-asan.txt, PERFORMANCE.md |
| A1 | `4c2c559` | reusable derived-only force/stress scratch; const publication and owning snapshots | lifecycle_tests.cpp all-state trajectories, repeated capture, due counters |
| A3 | `cbc312c` | byte-bounded async ownership/backpressure, worker failures, cancel/close, cooperative writer lease | async_results_tests.cpp; sync/async comparison and fault injection |
| A4 | `50204bd`, `ca4b682` | linked checkpoint/result prefix validation before append/recovery; precision-range gates | pipeline.py rejected-append byte preservation; lifecycle_tests.cpp input controls |
| A2 | `ed8262d`, `ca4b682` | opt-in native v2 lazy persisted frame index, selective entity/IP/time queries, bounded recovery validation | indexed_results_tests.cpp 100000 frames and separate 100000-node field |
| A6 | `ca4b682` | CPU full-integration small-strain C3D8, mixed TET4, 8-IP results/export and exact restart | hex8_tests.cpp, lifecycle_tests.cpp, pipeline.py |
| A5 CPU | `8122adf` | measured parse/compile/plan/step/output/read/restart and resident memory | 3 repetitions; raw JSON retained |
| A5 CUDA | existing source | BLOCKED_ENV / NOT_RUN; no lifecycle staging change or HEX CUDA kernel claimed | environment.txt; missing usable nvcc/nvidia-smi/default Toolkit path |

Source files changed: `include/sdb/{dynamics,hex8,lifecycle}.hpp`,
`include/sdb/model_compiler/compiler.hpp`, `include/sdb/io/native.hpp`,
`include/sdb/results/{results,indexed_format,async_writer,checkpoint_link,export}.hpp`,
`examples/sdb_run.cpp`, standalone `CMakeLists.txt`, `benchmarks/lifecycle_stages.cpp`.
Tests changed/added: lifecycle_tests.cpp, pipeline.py, async_results_tests.cpp,
indexed_results_tests.cpp, hex8_tests.cpp, forced_hex.inp and mixed_hex_tet.inp.
Documentation/evidence: README, existing development ledgers, this directory and
`verification/reviews/ALR1/`. The machine-readable content file list is in manifest.json.
The parent repository had existing uncommitted work before this task. Only dated
appendices were added to its CODEX_REPO_MAP, CODEX_MODULE_NOTES, CODEX_MIGRATION_NOTES
and CODEX_TASK_LOG; these remain uncommitted with the pre-existing edits. Exact appended
text and before/after byte hashes are retained in parent-doc-appendices.json.

## Capability and compatibility

| Formulation / material / analysis | Parsed | Representable | Executable | Verified |
|---|---|---|---|---|
| C3D4 isotropic linear elastic / constant-step explicit | yes | yes | CPU float/double; existing CUDA source | CPU serial/OpenMP + ASan; CUDA NOT_RUN |
| C3D8 full 2x2x2 / same material and analysis | yes | yes, 8 IP | CPU float/double | kernel serial/OpenMP, serial CpuRun/process path, ASan |
| Shared-node C3D4+C3D8 | yes | yes, variable IP offsets | CPU float/double | same CPU matrix and exact split restart |
| C3D8R | diagnosed | not mapped to full C3D8 | refused | rejection test |
| finite strain/rotation, plasticity, shells/beams through this new path | outside this extension | no new promise | no new implementation | NOT_RUN |

This does not change the existing subset parser into a full Abaqus parser. Existing
INCLUDE/Part/Instance/set/section/material/amplitude/output checks are reused; unsupported
physics rejects before stepping. Compiler/range checks prevent finite-double input from
silently becoming infinite float loads, mass or time-step data.

Native v1 remains the default write format and stays readable/appendable. Its reader
still scans history. Use `--indexed` for new v2 files; append auto-detects the actual format.
v2 normal open reads model, identity and last committed footer without decoding history.
Frame metadata is lazy; field values use at most 256 entity rows/block. Canonical ID row
order in v2 differs from possible input row order. Model metadata remains resident.
`ResultReader::index` is now a read-only FrameIndex facade returning metadata by value;
size/empty/at/subscript/front/back and const iteration remain, mutable vector operations
and references to its elements do not. Reader instances are single-caller due to their
reused stream. Independent reader instances can be used concurrently.

Restart/model payload formats remain compatible. Safe append now requires the matching
`.sdbrst.result-link` companion, validated while holding the writer lease. Legacy unlinked
restart can load and branch into a new output file; it cannot append unsafely. Linked
publication drains results, publishes checkpoint, then link. Mismatched/stale/ahead/behind
files refuse without mutation. `--recover-tail` only discards a verified incomplete tail;
CRC corruption is rejected. Prefix hashing is bounded-memory O(file size), including v2.
FNV/CRC are accidental mismatch detection, not authentication.

ResultPublisher receives const state. At displacement time t_n it reevaluates linear
stress/internal force in scratch; acceleration is (f_ext(t_n)-f_int(u_n))/m and velocity
is recentered from v_(n-1/2) with half a step of that acceleration. Fixed DOFs are zeroed
in the output. Solver state/history is not advanced. CpuRun snapshot convenience still
initializes a never-initialized run; the read-only guarantee applies after initialization.
Non-due/U-only paths avoid derived force work. All-state exact trajectory comparisons
cover disabled, sparse and every-step schedules, both precisions and all element mixes.

Async charge includes owned capacities, encoded frame and conservative validation/codec
scratch, held through in-flight append+flush; persistent model/solver memory and the
producer's not-yet-admitted frame are excluded. Oversized admission rejects. One producer,
one worker, cancel allowed from a control thread. Explicit close propagates failures;
destructor joins but cannot report an exception. Queued/written/committed are observable;
committed means stream-flushed, not fsync/power-loss durable. No approved codec was added,
so the delivered path is lossless uncompressed storage, not parallel compression.
Directory leases are cooperative; a crashed owner's stale lease requires operator action.

See [C3D8.md](C3D8.md) for formulas, node/IP order, stability bound, tolerances and references.
IP order is xi-fastest binary order, not an assertion of external solver IP numbering.
S/E exports are IP1; S@IP2..8/E@IP2..8 are separate arrays. Missing TET IPs are NaN only
in the dense visualization export. No commercial-solver or finite-rotation validation.

## Reproduction and test results

All paths below are relative to this standalone repository. Toolchain, hardware and
exact host paths: [environment.txt](../../../verification/reviews/ALR1/environment.txt).
Set PATH to the matching MinGW bin before launch; the original ambient-PATH run produced
DLL loader errors/stalls and is retained as failed environment evidence, not PASS.

```powershell
$cmake='C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja='C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$gcc='D:\Software\MinGw-w64\x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0\mingw64\bin\g++.exe'
./verification/reviews/ALR1/reproduce.ps1 -CMake $cmake -Ninja $ninja -Compiler $gcc -Build build-alr1-omp -OpenMP ON
./verification/reviews/ALR1/reproduce.ps1 -CMake $cmake -Ninja $ninja -Compiler $gcc -Build build-alr1-serial -OpenMP OFF
```

Recorded final commands used existing configured directories `build-b01` and
`build-b01-serial`: `cmake --build <dir> --parallel 3`, then
`ctest --test-dir <dir> --output-on-failure --timeout 300`, matching DLL PATH,
OMP_NUM_THREADS=4, Release, SDB_CUDA=OFF. Both build and CTest exit codes were 0.

MSVC run used `cmd /c verification\reviews\ALR1\run-asan.cmd` (exit 0), invoking
vcvars64.bat -vcvars_ver=14.44, build and CTest on build-b01-asan. For a fresh ASan
directory, in that developer environment configure with CMake/Ninja:
`-DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDB_OPENMP=OFF
-DSDB_CUDA=OFF -DSDB_SANITIZE=ON`, then build/test. This is AddressSanitizer only.

| Configuration | Current registered tests | Exit | Log | Runtime |
|---|---|---|---|---|
| GCC Release OpenMP | 11/11 PASS | 0 | final-omp-build.txt / final-omp-ctest.txt | 16.55 s |
| GCC Release serial | 11/11 PASS | 0 | final-serial-build.txt / final-serial-ctest.txt | 16.62 s |
| MSVC RelWithDebInfo ASan, serial | 11/11 PASS, no ASan diagnostic | 0 | final-asan.txt | 67.66 s |

These tests use runtime/test source ca4b682; serial/ASan builds also include the new
benchmark target at content HEAD. The OpenMP benchmark target was separately built and
run successfully. No runtime/test edits follow these final suites.
`python verification/reviews/ALR1/verify_packet.py` verifies the fixed base/content
identity and 69 content-file Git-blob hashes (exit 0). `git diff --check` passes.

Red evidence: readonly-red.txt and baseline-with-regression.txt expose mutation omitted
by older B01 tests; a4-red.txt exposes unsafe checkpoint-ahead append; a6-red.txt exposes
unsupported CPU HEX; precision-red.txt exposes float overflow. Intermediate a6-ctest.txt
has 10 tests and predates hex8_contracts registration; it is not the final A6 acceptance.
The aborted initial indexed performance run is documented in a2-initial-progress.txt.

Independent-process pipeline covers legacy/indexed, sync/async, float/double, pure/mixed
HEX, export/query, non-due checkpoint and exact uninterrupted-vs-resumed results. Fault
controls include CRC errors, huge CRC-correct lengths, index truncation boundaries,
worker append/flush failures, occupied writer and linked append mismatches.

## Performance evidence

[PERFORMANCE.md](../../../verification/reviews/ALR1/PERFORMANCE.md) and its JSON retain
three repetitions, medians/ranges, stage definitions, model sizes and byte counters.
Run with matching compiler PATH and OMP_NUM_THREADS=4, fresh output paths:

```powershell
./build-b01/sdb_lifecycle_stages.exe tests/fixtures/abaqus build-b01/stages-new
./build-b01/sdb_lifecycle_benchmark.exe 20000 256 build-b01/large-new
./build-b01/sdb_indexed_results_tests.exe tests/fixtures/abaqus/forced_tet.inp build-b01/index-new
python verification/reviews/ALR1/summarize_metrics.py
```

The first three commands were run three times with numbered output paths; stage output
contains 12 cases per trial. A separate index memory run exited 0 and sampled process
peak working set 67,702,784 bytes for the entire test (not just the lazy-open phase).
100000-frame write median 10.449 s, warm reopen median 4.883 ms; five entity probes
decode 120 bytes. 20000-node/5000-TET serial steady step median 0.20475 ms. Tiny stage
fixtures emphasize storage/setup overhead. No CPU parallel speedup inference, no physical
disk byte accounting, no OS-cache-evicted measurement, no GPU transfer/VRAM figures.

## Remaining limitations and independent review focus

1. Review numerical C3D8 Jacobian orientation, tensor shear conventions, warped-cell
   quadrature, mass and Gershgorin step bound, mixed adjacency and all-IP energy.
   Jacobian checks sample the eight Gauss points, not every point in the volume.
2. Review indexed footer identity/skip-pointer arithmetic, adversarial directory limits,
   committed-prefix recovery and query precision/identity. Normal selected reads do not
   detect corruption in unrelated blocks until those blocks are read.
3. Review async admission accounting, cancellation/failure wakeups and lease lifecycle.
   No TSan run, true process-crash campaign or power-loss durability proof; byte-tail fault
   injection does not substitute for them. No claims of hardware persistence.
4. Review restart/link publication ordering and validation-before-truncation. File hashes
   protect against accidental mismatch, not malicious rewriting. Hash cost remains O(N).
5. GCC 15.2 emits `-Wfree-nonheap-object` from optimized std::vector/native block encoding.
   It remains unsuppressed and unresolved as a compiler diagnostic. ASan reports no fault;
   that does not establish a proof that the warning is false. Inspect a2/stages build logs.
6. CUDA toolkit/device execution is BLOCKED_ENV / NOT_RUN, source not locally compiled.
   CUDA HEX and bounded pinned/event lifecycle collection were not implemented. UBSan,
   TSan, cross-device equivalence and parent production integration were not verified.
7. No compression dependency, MPI, multi-GPU, implicit solver, plasticity, finite rotation,
   shells/beams, C3D8R, ODB, or general-contact rewrite was added. Existing areas retained.

Migration: integrate only reviewed standalone commits into the internal buildable platform;
adapt compiled-model/state/result contracts at the boundary. Map external IP ordering
explicitly, choose native v1/v2 intentionally, and retain the link companion with each
checkpoint intended for append. Rerun deterministic/precision and hardware tests there.
Do not change parent build topology or substitute production dependencies. Parent map
appendices, if present, only point here; this module is not wired into production flow.

Next action: independent Opus review of BASE..content HEAD plus this packet. No further
feature expansion or stable integration is authorized by the self-test state.

Resume prompt: "Read ALR1 HANDOFF, STATUS, manifest and real Git status. Review
480da32a17e3b60e6694b95e5c9ec83618e5b64b..8122adf2c149a672222ab81d8ac722e69931205c;
preserve B02c and prior evidence. Verify reported tests and focus items independently.
Do not call it ACCEPTED or push/merge without the required independent/user gate."
