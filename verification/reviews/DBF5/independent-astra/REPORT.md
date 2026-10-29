# DBF5 independent review

Date: 2026-10-29. Scope: independent phase-three and remaining standalone-roadmap review requested by the owner. This report does not certify production solver integration.

## Source identity and disposition

- Baseline: `a9a203c67d78ed8649e12e05c15214ae2b95ea63`.
- Initial review head: `493a6ddb6aed3a6837be3e3cfa8382a8da4b14d6` (31 files, 3,107 added lines).
- Reviewed repair: `7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
- Disposition: one actionable P2 finding at the initial head, repaired and independently reproduced as fixed. No remaining actionable finding from this scoped review. The owner's final clean platform matrix remains a separate acceptance gate.

The reviewer read the root repository guidance and maps, DBF5_REQUIREMENTS, DBF5_COMPRESSION, DBF5_CONSUMERS, DBF5_MIGRATION and RESULT_FORMAT_SPEC, then traced the codec, indexed read/write/recovery integration, async charge, CLI conversion, checked consumer interface, shared HEX force extraction and preprocessing migration seam. Production source was consulted for the documented FEG label/sequence/material/property/float-coordinate boundary. Neither the parent production solver nor its external dependencies were built.

## F1 — P2: recovery bypassed the v4 stored-block allocation cap (fixed)

At the initial head, `include/sdb/results/indexed_format.hpp:381` obtained a generic record header with `limit_`, then read its whole body with `native::body`. The normal v4 `block_at` path instead caps stored allocation at 5 MiB + 24 bytes. Recovery did not apply that cap before allocation, including for uncommitted complete block records after a valid footer.

The independent `recovery_allocation_probe.cpp` writes a valid empty v4 file and appends an 8 MiB, CRC-correct kind-2 record. An instrumented global `operator new` tracks individual requested allocations during `ResultReader(path, true)`. On the initial head, recovery succeeds and treats the record as a recoverable tail after requesting **8,388,608 bytes**, exceeding the **5,242,904-byte** stored-envelope cap. The generic default limit permits still larger records. This contradicts the new compressed recovery resource contract and exposes recovery to avoidable memory exhaustion.

The owner added a v4 kind-2 header-size check immediately after `native::header`, before either truncated-tail classification or body allocation. It leaves older formats and other record kinds unchanged. Owner regression additions cover complete and truncated oversized declarations, reader/writer rejection and source-byte preservation.

The reviewer rebuilt the *same* independent probe against the repair. Recovery now throws `compressed recovery block exceeds stored limit`; the largest `new` request is **1,576 bytes**. The probe's exit code is intentionally 1 on the fixed build because it is a detector for the original defect, not a conventional passing-test executable. Both the diagnostic and the allocation measurement are captured in the fixed log.

Evidence:

- `recovery_allocation_probe.cpp`
- `recovery-allocation-head.txt`
- `recovery-allocation-fixed.txt`
- The repaired integration suite run is recorded separately in `compressed-fixed-run.txt`.

## Independent verification

These are reviewer-executed checks, not repetitions of the owner's reported results without execution. All executables were freshly compiled into this review's ignored `scratch/` directory using Windows GCC 15.2.0. The real zstd runtime identified itself as 1.5.7. The module's existing local SDK header/import library was used; no dependency was installed or replaced.

| Check | Result | Evidence |
|---|---|---|
| Codec suite, real zstd | PASS, runtime 1.5.7 | `codec-run.txt` |
| Codec suite, feature disabled | PASS, including unavailable-codec rejection | `codec-none-run.txt` |
| Compressed integration at initial head | 54 checks PASS | `compressed-run.txt` |
| Compressed integration at repaired head | 60 checks PASS, including complete/truncated oversized recovery | `compressed-fixed-run.txt` |
| Consumer suite, serial FP32/FP64 TET4/HEX8/mixed and publication fixture | 81,803 checks PASS | `consumer-run.txt` |
| Migration golden, negative inputs, private mutation, checkpoint | 113 checks PASS | `migration-run.txt` |
| CLI conversion, append policy and verification campaign | 34 checks PASS | `cli-run.txt` |
| Independent repaired-outer-CRC hostile envelopes | 6 checks PASS | `envelope-run.txt` |
| Recovery allocation defect and repaired behavior | Reproduced before repair; cap enforced after repair | Recovery logs above |

The independent envelope campaign deliberately mutates the first committed v4 block and repairs every affected outer record CRC, block reference, directory CRC and footer reference. Consequently the failures cannot be explained by only the existing outer checksum gate. It verifies rejection of unknown envelope version, unknown codec, nonzero reserved flags, raw-size overflow, stored-span mismatch and wrong raw CRC. Each case requires the specific inner diagnostic. `envelope_corruption_probe.py` is the standalone reproducer.

Compiler commands used, relative to the standalone module root:

```text
g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -DSDB_WITH_ZSTD=1 -Iinclude -Ibuild-deps/zstd/include tests/dbf5_codec_tests.cpp build-deps/zstd/libzstd.dll.a -o <scratch>/codec.exe
g++ -std=c++20 -O1 -Iinclude tests/dbf5_codec_tests.cpp -o <scratch>/codec-none.exe
g++ -std=c++20 -O0 -Wa,-mbig-obj -Iinclude tests/dbf5_consumer_tests.cpp -o <scratch>/consumer.exe
g++ -std=c++20 -O0 -Wa,-mbig-obj -Iinclude tests/dbf5_migration_tests.cpp -o <scratch>/migration.exe
g++ -std=c++20 -O0 -Wa,-mbig-obj -DSDB_WITH_ZSTD=1 -Iinclude -Ibuild-deps/zstd/include tests/dbf5_compressed_tests.cpp build-deps/zstd/libzstd.dll.a -o <scratch>/compressed.exe
g++ -std=c++20 -O0 -Wa,-mbig-obj -DSDB_WITH_ZSTD=1 -Iinclude -Ibuild-deps/zstd/include examples/sdb_run.cpp build-deps/zstd/libzstd.dll.a -o <scratch>/sdb-run.exe
g++ -std=c++20 -O0 -Wa,-mbig-obj -Iinclude verification/reviews/DBF5/independent-astra/recovery_allocation_probe.cpp -o <scratch>/recovery-allocation.exe
```

`<scratch>` denotes `verification/reviews/DBF5/independent-astra/scratch`. The compiler's runtime directory and the installed zstd DLL directory were placed on the child process PATH. Consumer publication uses `tests/fixtures/abaqus/forced_tet.inp`; migration uses `tests/fixtures/dbf5/migration-golden.txt`. The CLI campaign is the existing `tests/dbf5_cli.py` with feature flag `1`.

## Contract assessment

- Compression is a real optional dependency. The custom allocator bounds zstd-owned allocations including headers; raw size, output capacity, single-frame span/content size, checksum requirement and raw CRC checks are present. The 42 MiB + 1,024-byte async addition is a conservative codec/staging admission charge, not a whole-process RSS guarantee.
- Policy/header identity is persisted. Explicit policy mismatch and unavailable requested codec reject before recovery truncation; absent append policy adopts the stored policy. Conversion preserves logical payloads and does not copy physical restart-link authorization.
- DBF4 publication/barrier implementations are reused. This review found no new weakening in the compressed path after F1's repair. It did not independently repeat the owner's full process-crash or sanitizer campaign.
- Checked input borrows validate owner generations at batch entry. Const raw views do not pin lifetime or prevent races; the documented caller-side no-mutation/no-destruction condition is necessary. Scratch/output ownership, activity binding and supported alias rejection match the documented v0 reference consumer boundary.
- The HEX force extraction preserves the original arithmetic ordering; both the old and new paths call the same force algebra. The executed parity suite includes actual TET4/HEX8 force/stress/strain and 80-step trajectories. It does not establish arbitrary production-family parity or a universal performance threshold.
- Migration preserves captured labels, one-based sequential nodes and LM equations separately from dense mesh connectivity. It checks coordinates against the FEG float policy and rejects unsupported inventory/orientation/hybrid cases. The imported mesh is explicitly storage-only. No direct Felem ABI translator or complete native physical model is claimed. Deterministic reduction establishes its declared label order, not bitwise equality with every legacy assembler.

## Scope, exclusions and handoff

Reviewer-created files are this report, the two independent probes and their text logs; generated binaries/data live under ignored `scratch/`. No solver implementation, production source, public legacy API, CMake topology or numerical algorithm was changed by the reviewer. The repair was made and committed by the owner.

Not implemented by this review: any additional product features, native adapters, new physical formulations or unrelated legacy allocator redesign. The older generic decoder/metadata accounting area is not certified by this bounded DBF5 review.

Not independently verified here: MSVC, OpenMP, Linux, ASan/UBSan/TSan, installed-package consumers, CUDA, production SDK/ABI, real production golden models, physical power-loss survival or target-hardware performance. Those claims require their own recorded evidence; owner's supplied results are not relabeled as reviewer results.

Assumptions: immutable prepared plan metadata, externally serialized raw-borrow lifetimes, compatible real zstd SDK/runtime, trusted native capture that explicitly inventories unsupported fields, and no concurrent writer outside the existing lease contract.

Migration exit gates remain the native-environment steps documented in DBF5_MIGRATION: freeze current ABI, implement native-owned qualified capture, compare every mapped identity/precision field, compare old/new force/mass/dt/history/reduction and restart on production golden cases, measure actual workloads, and retain the native fallback until QA signs off. The standalone roadmap closure is credible within those explicit exclusions; it is not permission to redirect production call sites.
