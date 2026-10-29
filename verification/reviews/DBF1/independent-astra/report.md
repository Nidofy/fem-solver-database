# DBF1 independent review

Date: 2026-10-29, Asia/Shanghai.

**Verdict: PASS for the bounded DBF1 implementation slice.** No actionable
current-scope defect was established by this review. This does not certify
completion of the user's full D1–D7 program, accept the entire inherited ALR1
range, or authorize a stable merge/publication.

## Identity and independence

- BASE: `9a3d754dbbc5c0b1720a93ecf29ee3278f705f73`
- CONTENT_HEAD: `c5cd0622d7a52fb7c15db0e98dc33b5833d206a7`
- PACKET_HEAD: `40921ab71b61b92e73bba82621225aefccd91ddf`
- Reviewer: `/root/dbf1_independent_review`, separate from implementation agents.
- Requested configuration: Astra / High. Parent launcher confirmed actual
  `spawn_agent` arguments `model="gpt-6-astra"`, `reasoning_effort="high"`,
  `fork_turns="none"`. This is launcher evidence, not an in-process model query.
- Worktree HEAD was PACKET_HEAD. Its difference from CONTENT_HEAD contains only
  TASK_POOL, HANDOFF, manifest and packet verifier. No implementation changed
  during review. All 96 manifest entries matched both fixed Git blobs and the
  worktree (documented CRLF-to-LF canonicalization). An additional independent
  inventory check found 96 unique entries and coverage of all 75 tracked files
  under include/src/tests/examples/benchmarks/cmake plus CMakeLists.txt.

Read the fixed source diff, HANDOFF, CONTRACTS, CAPABILITY_MATRIX, VERIFICATION,
REVIEW, implementation README, user completion kit and relevant existing
Field/State/native/restart/checkpoint/backend code. Implementer reports were
used to identify claims, not accepted as proof. Tests below were run afresh.

## Review findings

No new P0/P1/P2 finding is reported. No finding is hidden by classifying a
current advertised behavior as deferred. The following table records concrete
review targets and evidence rather than inventing severity for passing checks.
All line anchors refer to CONTENT_HEAD.

| Area | Fixed source anchors | Independent evidence |
|---|---|---|
| Complete history identity | `include/sdb/model.hpp:261`, `include/sdb/schema.hpp:99` | Eleven count-preserving mutations per precision: schema ID/version, variable name/order, shape rank, component label, shear convention, units, basis, configuration and initial value. Both mesh and definition identities changed; both restart APIs rejected mismatches and preserved all target fields. |
| Version migration | `include/sdb/model_ir/model_ir.hpp:194`, `include/sdb/io/model_store.hpp:23`, `:47` | Schema 2 round trip; independently rewritten manifest with valid CRC but schema 1 rejected; unknown ModelIR version rejected. Fresh legacy golden test checked schema 1 model byte preservation and old restart consumption. |
| Enum and descriptor validity | `include/sdb/io/native.hpp:36`, `include/sdb/schema.hpp:62` | Hand-authored overflowing wire scalar enum refused; every descriptor enum's invalid value refused. Recompiled exact baseline demonstrates original ImportMode 256 narrowing defect and current fix. |
| Resource and wire boundaries | `include/sdb/storage.hpp:80`, `:107`, `:139`, `:174`; `include/sdb/io/native.hpp:35` | Hand-built LE sparse-int64 bytes round-trip exactly, including negative value outside exact FP64 range. Forged UINT64_MAX container count refuses before Field allocation; small budgets/truncation/unknown storage versions refuse. Encoded-size preflight inspected before DTO copy. |
| Missingness and lifetime | `include/sdb/storage.hpp:55`, `:123`; `include/sdb/field.hpp:43`, `:63` | Present numerical zero differs from absent/invalid rows; explicit validity persists. Copy preserves source view, assignment/move invalidates old views, ragged segment assignment invalidates its view. UINT64_MAX ragged values survive AoSoA round trip. Existing fresh storage tests additionally cover destruction and empty segments. |
| Execution refusal | `include/sdb/model_compiler/compiler.hpp:240`, `include/sdb/dynamics.hpp:116`, `include/sdb/hex8.hpp:40` | Declared histories and unknown algorithm 99001 refuse lifecycle/direct CPU execution. Failure preserves all caller state fields and an existing checked mass view. Static CUDA path constructs the same TetPlan; CUDA was not compiled or run. |
| Restart failure atomicity | `include/sdb/io/restart.hpp:68`, `:75`, `:127` | For FP32 and FP64, corrupt late history bytes, truncate final record and append trailing garbage: load refuses after entering restoration, complete destination snapshot and existing view remain unchanged. Successful restore equals every source field and clock/step value. Rotational destination baseline/current probe independently rerun. |

## Actual execution results

| Check | Actual result | Evidence in this directory |
|---|---|---|
| Fixed Git manifest verifier | exit 0, 96 files | `manifest-git.txt` |
| Current-file manifest verifier | exit 0, 96 files | `manifest-worktree.txt` |
| Independent inventory completeness | exit 0, 96 unique / all 75 relevant code/build/test files | `manifest-completeness.txt` |
| Fresh GCC 15.2 Release OpenMP configure/build, CUDA OFF | exit 0 | `configure.txt`, `build.txt` |
| Fresh CTest, OMP_NUM_THREADS=4 | exit 0; **15/15**, 16.53 seconds | `ctest.txt` |
| Reviewer-authored C++20 O0 serial probe | compile/run exit 0; **175 checks PASS** | `independent.cpp`, `probe-build.txt`, `probe-run.txt` |
| Same independent probe, MSVC 14.44 /Od AddressSanitizer | exit 0; **175 checks PASS**, no sanitizer diagnostic | `run-asan.cmd`, `probe-asan.txt` |
| Supplied narrow regression probe independently rebuilt with 37 exact BASE headers | expected exit 1; both baseline defects reproduced | `regression-red.txt` |
| Same regression probe rebuilt with current verified headers | exit 0; both refused correctly | `regression-green.txt` |

The fresh full suite includes lifecycle, foundation/process reader, storage,
legacy golden, mixed HEX8/TET4, indexed/async results, separate-process pipeline,
database, explicit numerics and general-contact tests. The 175-check probe was
written independently for this review; its wire oracle does not use the product
Writer. Its snapshot serializes every StateData field for full-state comparisons.

GCC's optimized native-vector `-Wfree-nonheap-object` diagnostics remain visible
in build.txt. They were not suppressed, and passing tests/ASan do not prove the
diagnostics benign. No concrete defect was established from those diagnostics.

## Reproduction

Run from `solver_database_v2/`. All outputs stay in the two authorized folders.

```powershell
$compiler = 'D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe'
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = 'C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
$env:OMP_NUM_THREADS = '4'
python verification/reviews/DBF1/verify_packet.py --git
python verification/reviews/DBF1/verify_packet.py
& $cmake -S . -B build-dbf1-review -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_CXX_COMPILER=$compiler" -DSDB_OPENMP=ON -DSDB_CUDA=OFF
& $cmake --build build-dbf1-review --parallel 3
& (Join-Path (Split-Path -Parent $cmake) 'ctest.exe') --test-dir build-dbf1-review --output-on-failure --timeout 300
& $compiler -std=c++20 -O0 -I include verification/reviews/DBF1/independent-astra/independent.cpp -o build-dbf1-review/independent.exe
& build-dbf1-review/independent.exe tests/fixtures/abaqus/mixed_hex_tet.inp build-dbf1-review/independent-scratch
cmd /c verification\reviews\DBF1\independent-astra\run-asan.cmd
```

BASE headers were exported with `git ls-tree -r --name-only BASE include` and
each `git show BASE:path` written below `build-dbf1-review/baseline/`, then the
existing `tests/foundation_regression_probe.cpp` was compiled with that include
directory and again with current include/. Both used C++20/O0 and forced_tet.inp;
restart artifacts were `build-dbf1-review/red.sdbrst` and `green.sdbrst`.

## Scope limits and subsequent work

The explicit deferrals are real incomplete full-program requirements, not
DBF1 regressions: strong entity IDs; coordinate/orientation reference graphs;
typed material parameter/table and layered/beam section schemas; richer
DOF/constraint/load storage; typed generic ResultDB fields and validity;
general state/contact/result/restart addressing; reorder-independent semantic
identity; extended input adapters and the full D7 composite fixture. The parent
should continue those slices without treating this PASS as full completion.

Generic ragged/sparse persistence is standalone DTO persistence, not automatic
attachment to ModelStore/ResultDB/RestartDB. History schemas are declarative,
FP32/FP64 and storage-only. Their name/basis/unit/version identity is covered;
no constitutive formula or physical interpretation was independently validated.

Resource budgets bound the existing codec accounting and encode payload, not
total process resident memory. Metadata spans remain ordinary caller-managed
borrows. Matching supplied models are required for restart; fingerprints/CRCs
are not cryptographic authentication. These are documented contracts, not new
findings. Rebuild consumers after cold Material/ModelIR growth; no binary ABI
compatibility claim should be made.

This reviewer did not rerun the entire MSVC suite or a second full GCC serial
suite; the fresh independent probe ran serial on both compilers, and the full
suite ran fresh under GCC/OpenMP. CUDA, Linux, UBSan/TSan, process-crash/power-loss
durability, exhaustive allocation-failure campaigns and parent production
integration are **not locally verified**. Production and CUDA are **not locally
compiled** in this review. The whole inherited ALR1 implementation was not
independently re-reviewed.

## Files changed and handoff

Only new reviewer reports/logs/repro source/script under
`verification/reviews/DBF1/independent-astra/`, plus generated review build,
baseline headers and scratch artifacts under `build-dbf1-review/`. No
implementation, shipped test, user kit or other documentation was modified.
No commit, push or merge was performed. No production implementation was added.
The fixed source remains suitable for parent continuation with this bounded
review gate recorded separately from future implementation and verification.
