# DBF4 independent Astra/high review

Final reviewed source: `910510d5340bfac779a2510eec8c78def73c5134`.
Initial reviewed source: `e8b9bd6702bfd8066fb2d338afcbcefe1fbb78b4`; intermediate diagnostic fix: `b677931ba03620eb070467a12b7447190fa8a9f0`.
Baseline: `5349ccfd5f1dff49b87fac88ffa43b61dc5f40ed`.
Date: 2026-10-29. Scope: isolated `solver_database_v2` DBF4 phase two.

## Verdict

**PASS within the stated durability and caller-ownership contract at the final reviewed SHA. No unresolved P0-P2 finding remains. Two diagnostic issues found during regression/review were repaired, as detailed below.** This is a bounded code review and focused standalone verification, not certification against power loss or a claim that all inherited storage code has been audited. Final phase acceptance also depends on the owner's complete CPU regression matrix and separately recorded Linux TSan evidence.

## Reviewed implementation and reasoning

- Reviewed the complete baseline-to-reviewed-source diff, including `io/durability.hpp`, both atomic checkpoint helpers, native result barriers, the indexed commit hook, async barrier tickets, the compound publisher, `CpuRun::model()`, CLI default/selection, build registration, documentation and tests.
- Traced the relevant inherited code: `FileLease`, native stream wrapper/records, result creation/append/recovery and last-frame publication, indexed footer commit, `CpuRun::save/restore`, `RestartDB::save`, and legacy link publish/verify. This was not a blanket review of unrelated ALR1 history or numerical kernels.
- Native acknowledgements occur after stream flush, requested file synchronization, optional parent synchronization, and successful prefix metadata construction. Write/sync errors poison the writer. Invalid or unsupported levels reject before barrier mutation. Windows strict parent synchronization is explicitly rejected; FileSync does not imply durable directory publication.
- Async acknowledgement is produced by the storage-owning worker after queue drain and the backend barrier, then transferred under the mutex. The worker validates the requested level. `Stats::committed` remains a stream-flush counter. Close joins the worker and reports errors. This relies on the documented single producer plus optional cancellation-thread contract; concurrent producers/direct storage access are not supported.
- Atomic helpers allocate the typed error object before replacement, record replacement attempt and confirmed replacement separately, preserve the original cause, and protect cleanup against masking the outcome. A failed replace system call is conservatively uncertain. No success is returned after a post-replace sync failure.
- `publish_durable` checks supported level, destination aliasing, run/model/result definition and configuration, mesh/precision, and result-ahead cursors before publication. It holds cooperative restart/link leases alongside the live result storage lease; publishes result barrier, restart, fingerprint, and link in order; and reports phase plus potentially changed destinations after partial publication.
- The new model accessor closes the obvious wrong-model argument gap. Compatibility publisher retains its explicitly documented caller-managed lease/flush precondition. Append preflight verification occurs before tail truncation. No persistence schema/record encoding changed in this diff.
- CLI joins async output before using native storage directly. Default completion/checkpoint uses FileSync; unsupported Windows PublicationSync fails before model/output mutation.

## Independent verification performed

1. Freshly rebuilt `allocation_probe.cpp` directly against final `910510d` headers with both Windows MinGW GCC 15.2 (C++20, O0) and explicitly pinned MSVC toolset 14.44.35207 / compiler 19.44.35228.0 (C++20). The probe denies subsequent ordinary C++ allocations and throws `std::bad_alloc` immediately after replacement. Both atomic helpers preserve published/uncertain flags and cause; both restart and link compound failure sites preserve correct phase/change flags. Restoring the changed restart and checking the old/new link confirms refusal/acceptance according to the actual visible files. It additionally checks original standard callback diagnostics, nonstandard-exception fallback, and calls `what()` while ordinary allocations remain denied. Result: PASS (`allocation-run.log`, `final-msvc-run.log`).
2. On the initial reviewed source, executed the owner's already-built focused Windows binaries in independent scratch directories: atomic **66**, result durability **93**, link publication **66** checks passed (`atomic-run.log`, `result-run.log`, `link-run.log`). These binaries were not rebuilt independently by this reviewer; the allocation probe was.
3. On the initial reviewed source, executed the strengthened fresh-process campaign with the owner's serial fault-driver build: **206 child checks passed** (`fault-campaign-run.log`). Reviewed its test-only additions beyond the source freeze: explicit per-stage outcomes, independent Python schema-1/CRC/FNV decoding, byte-prefix/restart/link comparisons, refusal while the dead child's stale lease is still present, and recovery refusal-before-mutation. Script SHA256 at review: `B8F948C8437CDAFF36A9694B75DF193DD50BDE76E988961DAFE4A9CBED596BDD`.
4. Reviewed both follow-up source deltas through final `910510d`; the only runtime change is PublicationError diagnostic preservation. The working CMake delta only registered an additional CLI test. Fresh final-source MSVC `final_diagnostic_probe.cpp` confirms owned diagnostic lifetime, independent storage after wrapper copy, and safe 1023-byte truncation (`final-msvc-run.log`).

The allocation probe uses a thread-local ordinary `operator new` denial. It does not exhaust the C runtime, exception runtime, OS allocator, aligned allocations, or every allocation site. The runtime can still allocate the exception object. The tests establish error-path behavior for the exercised sites, not universal out-of-memory survival.

## Findings and repair verification

**R1 — P2, original diagnostic compatibility, resolved.** At initial `e8b9bd6`, wrapping a callback exception in `PublicationError` replaced the original `std::exception::what()` text with the generic publication message. The owner's full GCC regression run found this at `tests/lifecycle_tests.cpp:408`, which expects the injected callback diagnostic. The initial focused checks did not cover this compatibility behavior. Final `910510d` preserves a bounded original diagnostic while retaining structured publication state and the cause. The independent final allocation probe checks this directly.

**R2 — P2, dangling diagnostic pointer on MSVC, resolved.** Intermediate fix `b677931` implemented `what()` by rethrowing `cause_`, catching `const std::exception&`, and returning that exception's `what()` pointer. This assumes the rethrown object is the retained object. The [C++ exception propagation specification](https://eel.is/c++draft/propagation) permits a new exception copy and copy-storage allocation. MSVC concretely does copy: `rethrow_lifetime_probe.cpp` uses a custom exception with an inline diagnostic and destructor counter; the counter rises from 1 to 2 during the diagnostic call, proving the copied diagnostic owner is destroyed before the returned pointer can be consumed (`rethrow-lifetime-run.log`). The probe avoids dereferencing the expired pointer.

Final `910510d` captures the active exception's message using bare rethrow inside the handler into a 1024-byte owned array. `what()` only returns the owned buffer (or stable base fallback); it neither rethrows nor builds a new message. Fresh MSVC final-source probing shows the destructor count remains 1 across `what()`, a copied publication error owns a distinct buffer, and a 3000-byte source message safely yields 1023 bytes plus a terminator. Both GCC and MSVC final-source allocation-denial probes pass. R2 is closed. Messages longer than 1023 bytes are intentionally truncated; `cause()` retains the exception-pointer channel subject to standard runtime behavior.

The owner additionally discovered stale MSVC cached regression binaries caused by mismatched localized showIncludes dependency parsing. Earlier owner ASan results are not accepted as final-source evidence; the owner is rebuilding the standalone matrix from a clean English-language dependency cache. This reviewer did not claim those ASan results as independent verification. The direct compiler invocations here freshly compile their complete translation units and do not depend on that cached dependency graph.

## Reproduction

Run from the standalone module root in PowerShell, with the MinGW `bin` directory prepended to PATH:

```powershell
$compiler = 'D:\Software\MinGw-w64\x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0\mingw64\bin\g++.exe'
& $compiler -std=c++20 -O0 '-Wa,-mbig-obj' -Iinclude verification/reviews/DBF4/independent-astra/allocation_probe.cpp -o verification/reviews/DBF4/independent-astra/scratch/allocation_probe.exe
& verification/reviews/DBF4/independent-astra/scratch/allocation_probe.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/DBF4/independent-astra/scratch/allocation
& build-b01/dbf4_atomic_probe.exe
& build-b01/dbf4_result_durability_probe.exe verification/reviews/DBF4/independent-astra/scratch/result
& build-b01/dbf4_link_probe.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/DBF4/independent-astra/scratch/link
$env:OMP_NUM_THREADS = '4'
python tests/dbf4_fault_campaign.py build-b01-serial/sdb_dbf4_fault_driver.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/DBF4/independent-astra/scratch/fault-campaign
```

The final MSVC commands are preserved in `run-final-msvc.cmd`; run that script from the module root. It invokes the installed x64 vcvars environment with `-vcvars_ver=14.44`, records `where cl` and `/Bv` compiler identity, directly compiles both final probes and the superseded-algorithm reproduction, and executes all three. The final pinned rerun supersedes earlier unpinned vcvars invocations; those earlier invocations did not establish a precise toolset version. `final-msvc-run.log` confirms toolset directory `14.44.35207`, compiler `19.44.35228.0`, and all expected outcomes. `rethrow_lifetime_probe.cpp` is a standalone reproduction of the superseded algorithm, retained to explain R2; it intentionally demonstrates the lifetime issue and is not the fixed implementation.

## Limits and handoff

Files added by this review are this report, the standalone allocation probe, compiler/source identity text, raw run logs, and generated scratch evidence under this review directory. No library/example/test implementation was edited and no commit was made. The owner made the two follow-up diagnostic repairs described above; this reviewer made no runtime edits.

The parent agent owns production documentation updates and final regression/migration evidence. Production solver and CUDA were not locally compiled or verified by this reviewer. POSIX parent-sync paths were statically reviewed here; Linux runtime/TSan runs belong to the separately recorded owner campaign. Electrical power loss, nonlocal filesystems, storage-controller behavior, hostile external writers, concurrent run mutation, and generalized file-identity/lease redesign are outside this verdict. Publication is three ordered operations, not a multi-file transaction. Consumers must retain exact linked files, close async writers explicitly, and verify before append after ambiguous failures; no wire migration is required.



