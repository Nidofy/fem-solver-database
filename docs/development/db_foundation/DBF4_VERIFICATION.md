# DBF4 phase-two verification

Baseline: `5349ccfd5f1dff49b87fac88ffa43b61dc5f40ed`.
Final independently reviewed library: `910510d5340bfac779a2510eec8c78def73c5134`.
Subsequent campaign oracle, CLI tests and evidence changes do not alter that
source boundary. Final test commit: `476998506f723af4f1db27a64e4b6ff028dd5e75`.
Independent Astra/high: PASS within the documented scope; no unresolved P0–P2.

## Final clean-build matrix

| Configuration | Result | CTest elapsed | Raw evidence under verification/reviews/DBF4 |
|---|---|---:|---|
| GCC15.2 OpenMP Release | 39/39, exit0 | 24.59 s | build-b01-build-final.log, build-b01-ctest-final.log |
| GCC15.2 serial Release | 39/39, exit0 | 24.41 s | build-b01-serial-build-final.log, build-b01-serial-ctest-final.log |
| MSVC14.44 ASan RelWithDebInfo | 39/39, exit0 | 145.01 s | msvc-fresh-build-ctest.log, msvc-fresh-test-detail.log |

Elapsed times are regression-run observations under concurrent build load, not
comparative performance claims. GCC detailed test output is also retained in
build-b01-test-detail.log and build-b01-serial-test-detail.log. All three runs
include the 10-case public CLI durability test and independent crash oracle.

## Dedicated evidence

| Test | Windows | WSL Linux |
|---|---:|---:|
| Atomic replacement/error-state contracts | 76 checks | 103 checks |
| Result/async durability contracts | 93 checks | 93 checks under TSan |
| Compound publication errors and identity preflight | 66 checks | 65 checks |
| Fresh-process crash/append campaign | 206 child checks, 47 abrupt exits | 254 child checks, 59 abrupt exits |
| Existing async ownership/backpressure/cancel/failure/native equality | full matrix | TSan PASS |
| Public CLI durability selection | 10 checks per configuration | 10 checks including strict PublicationSync success |

Platform count differences are deliberate: POSIX adds parent-directory failure
points; Windows tests strict unsupported-level refusal. The campaign starts with
legacy schema1 links and covers Native1, Indexed2 and TypedIndexed3. A Python
oracle independently decodes little-endian schema1 records, validates CRC32 and
FNV fingerprints, and asserts expected old/new result/restart/link states.
Fresh reader and append processes test stale-lease refusal, exact incomplete-tail
recovery, same-length corruption refusal and no mutation on rejected append.
Only the campaign's exact stale lease directories are removed, after reaping the
crashed child. Production code never steals locks.

## Linux / TSan reproducibility

Run `verification/reviews/DBF4/run-linux-focused.sh` from this standalone module.
It records source SHA, source diff, compiler and kernel, then builds the two
concurrency tests with GCC `-O1 -g -pthread -fsanitize=thread
-fno-omit-frame-pointer`, and builds normal atomic/link/fault probes. This run used
Ubuntu GCC 13.3.0 on WSL2 Linux 6.18.33.2. The workspace is on `/mnt/d` (v9fs), so
results validate the POSIX API path on that mounted filesystem, not native ext4
durability or a particular storage device.

Default-ASLR startup of the new TSan executable failed with `unexpected memory
mapping`; the raw failure is retained in `tsan-durability-runtime-failure.log`.
Both final TSan programs passed with `setarch x86_64 -R` scoped to the test process
and `TSAN_OPTIONS=halt_on_error=1`. No host setting was changed. A TSan pass is
bounded dynamic evidence for these schedules, not proof of all concurrent uses.

## Windows matrix reproduction

`verification/reviews/DBF4/run-windows-matrix.ps1` accepts explicit CMake, CTest,
GCC bin, and vcvars paths. Existing standalone build caches are:
`build-b01` (GCC15.2 Release/OpenMP), `build-b01-serial` (GCC15.2 Release, no OpenMP),
and fresh `build-dbf4-asan` (MSVC14.44 RelWithDebInfo/ASan, no OpenMP).
GCC runtime bin must lead PATH; MSVC ASan must execute within vcvars environment.
The parent solver is never configured or built.

Initial GCC matrices at e8b9bd6 failed the legacy lifecycle diagnostic assertion:
the structured wrapper hid the injected error text. Those failure logs are kept
as `pre-diagnostic-fix-gcc-*.log`. The first fix b677931 rethrew the stored exception
inside what(); independent Astra review demonstrated on MSVC that a permitted
exception copy made its returned diagnostic pointer dangle. Final 910510d owns a
1024-byte diagnostic captured from the active original handler without rethrow
copying; exceptionally long diagnostics are truncated, and cause() retains the
original exception. See DBF4_REVIEW_FIXES and the independent probes.

The initial build-b01-asan 38/38 and 39/39 runs are **superseded, not accepted**:
its cached Chinese showIncludes prefix did not match the compiler's English
output, so header changes were missed and old binaries survived. The final cache
was configured with VSLANG=1033, verified to use the English dependency prefix,
and rebuilt clean-first. Both GCC configurations were also rebuilt clean-first
after the final source freeze. Initial logs are retained for this audit trail.

Full regression includes old schema/format goldens, registered-state wire oracle,
real CPU numerics and split-restart pipelines. Compiler logs retain inherited
missing-field-initializer warnings and the allocation-fault test's intentional
malloc/free-based new/delete diagnostics; this is not a warning-free build claim.

## Limits

Fault callbacks throw or abruptly terminate dedicated processes. They do not
simulate electrical power loss, firmware caches, remote filesystems or every OS
I/O error. No physical device durability certification, exhaustive ENOSPC/EIO
campaign, production solver/CUDA build, new compression or numerical algorithms
are claimed. Windows atomic replace retains flushed temp + WRITE_THROUGH;
strict standalone parent synchronization remains unsupported and fails explicitly.
Older ALR1/B02c scopes are not automatically accepted by this review.
