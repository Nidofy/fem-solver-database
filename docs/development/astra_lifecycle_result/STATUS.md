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
| A4.2 | input/abaqus.hpp, model_compiler/compiler.hpp; lifecycle_tests parser and instance controls | Subset implemented; path-range policy and further adversarial coverage pending |
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
