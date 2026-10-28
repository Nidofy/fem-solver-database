# Lifecycle hardening status

BASE_SHA: 480da32a17e3b60e6694b95e5c9ec83618e5b64b
Status: IN_PROGRESS; no independent acceptance claimed.

| Area | Actual baseline and evidence | State |
|---|---|---|
| A0 | CMake registers 8 tests; lifecycle_tests.cpp/pipeline.py cover float/double, INP, export and exact split restart; tests/tests.cpp covers CPU serial/OpenMP | Existing; current revalidation underway |
| A1 | lifecycle.hpp advance + ResultPublisher due filtering already avoid non-due forces; ResultWriter already filters empty frames. New all-field regression fails baseline publication | Read-only repair implemented, validation underway |
| A2 | ResultReader constructor decodes every record; field/history decode full frames | Missing persistent/selective index |
| A3 | NativeResultStorage checks CRC, shape, budgets and flushes records; ResultFrame owns vectors | Synchronous present; async missing |
| A4.1 | RestartDB loads into temporary state; sdb_run compares only last time, no linked result watermark | Partial; unsafe gaps need rejection |
| A4.2 | input/abaqus.hpp, model_compiler/compiler.hpp; lifecycle_tests parser and instance controls | Subset implemented; cycle/depth/byte/entity/path-root protections already implemented and tested; further adversarial coverage pending |
| A5 | dynamics.hpp serial/OpenMP, src/cuda_backend.cu TET source; lifecycle benchmark reports serial | CPU baseline present; CUDA toolkit/device not observed, NOT_RUN |
| A6 | descriptors and StateDB variable IP offsets exist; TetPlan rejects non-TET4; compiler rejects C3D8 execution | Storage only; CPU/kernel/results integration missing |

Original unmodified runtime plus new regression: 7/8 CTest pass, lifecycle fails
at publication leaves trajectory untouched (expected exposure). Corrected matching
MinGW DLL PATH is required; first launch with ambient PATH stalled before test
execution and was terminated. Logs: verification/reviews/ALR1.
No production solver, parent build, stable integration, remote push or external
dependencies are changed. CUDA/UBSan/production verification remains unclaimed.

A1: IMPLEMENTED_SELF_TESTED / READY_FOR_REVIEW. Expanded float/double all-field trajectories and snapshot ownership pass; current OpenMP Release CTest 8/8, exit 0 (a1-ctest.txt).

A3 queue/lease: IMPLEMENTED_SELF_TESTED. Slow storage, ordered byte-budget backpressure, oversize admission, cancellation, append/flush failure and native sync/async byte equality pass. CTest 9/9 exit 0 (a3-ctest.txt). CLI --async-mib selects this path. Compression and explicit disk commit-marker format remain pending; no durability claim.

A4 linked append: IMPLEMENTED_SELF_TESTED; 9/9 CTest exit 0. Regression exposed checkpoint-ahead append before repair. float/double process tests cover non-due cursor 733, result ahead/behind, missing/stale link, missing result, occupied writer, corrupted prefix and explicit incomplete-tail recovery. All rejection cases preserve result bytes. No real power-loss proof.

A2 indexed native v2: IMPLEMENTED_SELF_TESTED, opt-in --indexed. 100000 frames
write about 13 s; warm-cache reopen 6.5 ms in first completed sample, no historical
values decoded. Five random single-entity queries decode 120 value bytes total.
100000-node field query decodes <=256 rows. Normal open retains one footer;
legacy v1 still scans. Recovery checks all complete records and rejects CRC damage;
truncated tail and malicious CRC-correct oversized directory tests pass. CTest
10/10 exit 0. Current public index is a read-only metadata facade (documented API
adjustment); normal selected reads do not promise detection of unqueried damage.

A6: IMPLEMENTED_SELF_TESTED / READY_FOR_REVIEW for CPU float/double. HexPlan
uses eight Gauss points, reference small strain, row-sum mass and a conservative
stiffness/mass stability bound; mixed TET4/HEX8 keeps all IP offsets. Kernel tests
include skew/warped geometry, analytic stiffness, rigid modes, patch, mass,
shared-node equilibrium, convergence, invalid Jacobians and OpenMP comparisons.
Process matrix covers pure/mixed, both precisions, both native formats, async,
non-due restart and IP8 export; C3D8R is rejected. Latest OpenMP CTest 11/11,
exit 0, including new precision-overflow gates. Earlier serial/ASan 11/11 passed;
final refresh after the input-range gate is pending. No GPU physics claim.

A4 precision follow-up: regression demonstrated float action overflow was accepted.
Compiler now checks peak amplitude-scaled actions against selected precision;
runtime also checks load range. Extreme finite amplitude interpolation has an
overflow-safe fallback; ordinary interpolation rounding is preserved. Underflow/
overflow in lumped mass or stability bounds is rejected before execution.
