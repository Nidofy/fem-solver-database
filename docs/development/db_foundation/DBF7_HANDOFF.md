# DBF7 local continuation handoff

Date: 2026-10-30. SDK 2.3.0. Implementation baseline: DBF5 `b489414` plus the
previously uncommitted DBF6 implementation. See DBF7_CONTRACTS.md for API semantics.
This delivery combines the reviewed DBF6 foundation with the locally feasible
extensions authorized in the P0-P2 follow-up. No production solver build or merge.

## Delivered

- P0: independent DBF6 review, fixed-content evidence, current status reconciliation,
  additional Linux/TSan/OpenMP/codec verification; no historical acceptance relabeling.
- P0 migration: 2047-key semantic mapping template, complete populated-key checks,
  exact integer comparisons, explicit conversion/tolerances, offline diff reports,
  synthetic source/target fixtures and bounded INP keyword/include audit.
- P1 query: logarithmic footer-link time-bound lookup, cancellable streaming queries,
  optional vector sample budgets, typed UInt64 preservation and million-frame run.
- P1 state: isolated StateSession ownership, shared read/exclusive write leases,
  expired-lease checks, staging payload budgets/statistics, direct typed remap copying.
- P1 restore: single-file FoundationBundle containing Batch/DOFs/catalog/cursor/state,
  metadata inspection, exact receiver contract, staged failure-atomic restore.
- P1 matrix: equation-bound role/stage/order/revision contracts and additive Index64 CSR.
- P2 semantics: explicit unit conversion/dimension arithmetic, analysis assignments,
  constant thermal INP fragment importer, frequency/modal/complex storage and querying.
- Optional consumers: installed read-only C ABI, ctypes client, external pure C and
  C++ consumers and compiler dependency inspection.

New public files: units.hpp, matrix_contract.hpp, analysis_contract.hpp, spectral.hpp,
state_session.hpp, io/foundation_bundle.hpp, input/thermal_contract.hpp, c_api.h.
New implementation: src/c_api.cpp. Query changes are in results/results.hpp and
results/indexed_format.hpp; state metadata/remap additions are in registered_state.hpp.
Tests, tools, benchmarks, package installation and this documentation are additive.
The parent solver's APIs, project files and numerical algorithms were not changed.

## Executed verification

| Configuration / check | Result |
| --- | --- |
| Windows GCC serial, codec OFF, C ABI ON, complete CTest | 52/52 PASS |
| Windows GCC OpenMP + zstd, DBF6/7 + compressed-result focus | 8/8 PASS |
| Linux GCC OpenMP + zstd + C ABI, same focus | 9/9 PASS |
| MSVC 14.44 AddressSanitizer, DBF6/7 + typed-result focus | 8/8 PASS, no ASan report |
| Linux TSan, DBF6 contracts / DBF7 contracts | 67 / 41 checks PASS, no TSan report |
| External installed C++ and pure C consumers | 2/2 PASS |
| Installed compiler dependency inspection | foundation.hpp present; no dynamics/hex8/model_compiler/gpdbase/caefem headers |
| DBF7 GCC -> MSVC, MSVC -> GCC, Windows -> Linux, Linux -> Windows | Fresh-process bundle/spectral readers, 2 checks each PASS |
| DBF6 Windows -> Linux | 4 checks PASS |
| Independent Python bundle/spectral wire oracle | GCC/MSVC/Linux files PASS |
| Offline mapping/inventory/input tools | 4 unittest groups PASS; synthetic CLI comparison PASS |

Logs and source identities are under verification/reviews/DBF7/. Historical DBF6
evidence remains under DBF6. Final standalone test counts vary by optional C ABI;
do not apply the complete Windows 52-test count to the focused platform runs.
Existing compiler warnings remain; this is not a warning-free claim.

The independent DBF6 reviewer reproduced 67 contract checks, 4 fresh-reader checks,
and 1179 negative probes. DBF7 independent review identified and closed three P2s:
expired lease checked access, inconsistent spectral units, and failed thermal
input stream silently yielding an empty configuration. Fixed probes: 2560 initial
checks (2556 interval-oracle cases), 9 final C++ checks, 7 Python mapping checks.
Final reviewed header hashes match the delivered header content. Reviews are
PASS WITH EXPLICIT SCOPE LIMITS, not a named-model claim or production certification.

## Measurements and encountered failures

Final Linux synthetic indexed run: 1,000,000 frames, 971,002,313 file bytes,
100,410 ms write time, 2,313 bytes read to open, 156,108 bytes read across the tested
query/boundary operations. The format is Indexed2 with a tiny real field per frame;
this is not a million large typed/compressed-frame or production end-to-end benchmark.
Typed streaming and compressed-result regressions are separately covered above.

One-million-row, three-component state: 24,000,024 accounted resident payload bytes,
48,000,048 peak staging payload bytes, 48,000,048 cumulative copied payload bytes.
Recorded clone/trial/rollback: 12.74/12.84/0.31 ms. Process peak RSS: 73,508 KiB
(about 71.8 MiB), including the caller's source registry and other allocations.
This measurement demonstrates why payload budget is not global process RSS.

An initial interval test exceeded its I/O bound with binary search layered on
random footer lookup. Runtime was improved to search skip links directly; the
bound was retained. The Python DLL test initially could not find MinGW runtime
dependencies because Windows Python ignores PATH for this loading mode; explicit
DLL directories were added. A WSL /tmp log disappeared after the instance stopped;
the million-frame run was repeated with logs written directly to the workspace.
Independent review's duplicate-time fixture was correctly rejected by the writer;
it was changed to strictly increasing times, not used to weaken the contract.

## Remaining boundaries and migration

This completes the bounded local items in DBF7_PLAN, not every possible database
feature. Full INP grammar, end-to-end MeshDB Index64 conversion, fractional unit
algebra, lazy spectral archives, every typed FFI selector, automatic dynamic field
factories, COW transactions and a global allocator/RSS governor are not implemented.
They are explicit future product extensions, not silently enabled by this SDK.

The 2047 mapping entries remain UNQUALIFIED until actual source semantics and
captures are supplied. The synthetic example is not a native mapping certificate.
Real FNL/FEG/IDARRAY capture, element-family mirror parity, solver replacement and
production speedup require the buildable production repository. Production and
CUDA paths remain **not locally compiled / not locally verified**. Physical power
loss remains unverified. Process fault and atomic-file tests cannot certify it.

Next native action: capture a neutral populated-key inventory plus immutable model,
DOF numbering/order, basis and per-family sample definitions. Run offline mapping
comparison, then adapt the solver to these contracts with fallback and numerical/
restart/performance comparison gates. No reverse dependency into the database.

ALR1's historical FROZEN state remains historical; later scoped DBF acceptance does
not retroactively accept all ALR1 work. B02c remains unaccepted/unmerged. The original
untracked completion kit is preserved as planning input. No stable merge, push or
release has been performed. Current canonical state is the DBF7 manifest and handoff.
