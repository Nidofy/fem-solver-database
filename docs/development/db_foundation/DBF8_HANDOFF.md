# DBF8 local extensions — SDK 2.4.0

Date: 2026-10-30. Baseline: `30bedf6`. User confirmed that complete INP means
file-layer read/write, all keywords/data preserved, with explicit semantic and
solver capability boundaries. Production solver code and numerical algorithms
are unchanged. No merge or push is part of this delivery.

## Delivered interfaces

### Complete INP file layer

`tools/inp_document.py read model.inp model.json --root JOB_ROOT` captures source
bytes, comments, blank lines, CRLF/LF, BOM, raw parameter spellings and data,
including unknown keywords. `export model.json NEW_DIRECTORY` verifies hashes
and restores the original file tree; output files/directories are never replaced.
The JSON document is a lossless inspection/storage container, not executable IR.
The installed SDK supplies both tools and the offline keyword catalog under
`share/sdb/`. Python uses only the standard library.

General keyword continuation, quoted comma fields, empty data fields, keyword
case/embedded-space normalization, exact/unique keyword abbreviations, INCLUDE,
and external INPUT data files are recognized. Includes resolve relative to the
job submission root. Symlink targets must stay within that root; their logical
alias names survive export. Cycles, depth, expanded bytes, physical line length,
ambiguous abbreviations, unsafe portable paths/NTFS streams/device names, bad
hashes and duplicate JSON keys are rejected. Duplicate ordinary parameters are
retained with a diagnostic; ambiguous duplicate external INPUT is rejected.
Free text and keyword-specific continued data rows stay raw: no guessed per-card
physical grammar, expression evaluation or implicit solver authorization.

The 600-name offline catalog contains keyword names and official links, not
copied documentation prose. Its source SHA is recorded in
`data/inp_keywords_2025.json`. Each card carries `semantic_status`, `coverage`,
`known_keyword`; `executable` and `semantic_mapping_complete` stay false.

| Existing semantic route | Implemented subset and limits |
| --- | --- |
| `sdb/input/abaqus.hpp` | Conditional ModelIR importer: parts/instances, nodes/elements/sets, materials/density/isotropic elasticity, sections/surfaces, tabular amplitudes, steps/dynamic, boundary/load/output cards and foundation extensions. Every parameter, scope and element type still passes that importer's own checks. |
| Foundation extensions in that importer | EQUATION, ORIENTATION, COUPLING/KINEMATIC, initial VELOCITY, uniform-pressure DSLOAD have bounded representations; storage acceptance does not grant numerical execution. |
| `sdb/input/thermal_contract.hpp` | Separate constant nodal TEMPERATURE/CFLUX and initial TEMPERATURE fragments; explicit label-to-thermal-slot map; no full thermal deck/kernel. |
| INCLUDE | File reference expansion only. |
| Other known/unknown keywords | Raw preservation and inspection; no semantic mapping or numerical support is asserted. |

The file layer does not replace the strict ModelIR importer. `SUBSET_AVAILABLE_NOT_VALIDATED`
means an existing entry point, not that this particular card is mapped. Binary
FILE/LIBRARY dependencies are recorded as opaque references and are not bundled.
Absolute paths are preserved byte-exactly and therefore retain their original
job-root dependency on relocation; export does not rewrite paths. The root
sandbox and ambiguity rejection are intentional ingestion constraints.

References: [official general input syntax](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-inputsyntax.htm),
[official keyword browser](https://docs.software.vt.edu/abaqusv2025/English/SIMACAECAERefMap/simacae-c-gen-kwbrowser.htm).

### Global MeshDB Index64

Configure **the entire SDK/consumer build** with `-DSDB_INDEX_BITS=64` (default32).
`Index`, entity strong IDs, MeshDB connectivity/material/section/group mappings,
mechanical DOF equations, sparse matrix columns and result sample material IDs
follow that width. Installed CMake targets propagate the ABI definition. Widths
must never be mixed across C++ translation units/libraries. External labels and
FrameId remain uint64. Local IP/layer/component indices and independent schema
catalog ordinals retain their explicit widths; this is not every integer becoming64.

Index32 native envelopes remain version1, preserving historical golden bytes.
Index64 native envelopes use version2; readers reject the other width before
width-dependent metadata decoding. Sparse MatrixData also records/validates width;
legacy checkpoint width markers retain their check. Bare `native::pack` payloads
are not self-describing: callers must use a matching typed contract/file envelope.
Cross-width file conversion is not implemented. Native fixtures can be re-imported
from source separately in each build. Both width builds exercise actual small
model/CPU/result/restart paths; no allocation of >4 billion entities is claimed.

### COW state

Ordinary Field copies and `StateSession::begin_trial()` retain deep-copy behavior.
Opt in with `begin_trial(TrialPolicy::CopyOnWrite)` or low-level
`RegisteredState::begin_trial_cow()`. Values and validity masks initially share
buffers; metadata/layout/initialization descriptors are copied. The first write
detaches only that field/mask. Raw consumers must be joined before a phase
transition; StateSession supplies checked read/exclusive-write leases.

All staging allocations complete before original checked borrows are invalidated.
Failure to begin a trial or detach a value buffer leaves the original state intact.
Commit retains trial changes; rollback restores the original values/masks.
`trial_payload` remains a conservative full-detach reservation, not physical live
bytes. `copied_payload` counts deep operation payload estimates;
`detached_value_bytes` counts actual value-buffer detach bytes through owned write
leases. Metadata/allocator overhead is not hidden inside these payload counters.
Cumulative counters saturate instead of throwing after a mutation has published.

### Process memory envelopes

Link `sdb::process_memory` and call `process_memory::set_limit(bytes)` to intercept
executable C++ new/new[] (including aligned/nothrow forms) with thread-safe
admission, current/peak charge and rejection counters. Charges include requested
bytes and the guard's alignment/header reservation; they are not heap-provider
internal overhead or RSS. A failed quota check throws bad_alloc or returns null
according to the allocation API. This is opt-in and must not be combined with a
second global-new replacement. Foreign DLL runtimes, malloc/mmap and GPU memory
are outside this interception layer. A custom new_handler retains standard retry
semantics; it must itself release memory or throw.

`python tools/run_limited.py --limit-mib 64 --report report.json --log worker.log -- COMMAND`
provides the OS envelope for uninstrumented allocations. Windows creates a
suspended worker, assigns it to a Job before main, caps aggregate committed job
memory, then resumes it; closing the Job terminates remaining descendants.
Linux applies inherited RLIMIT_AS to each process and cleans up the worker group.
Linux RLIMIT_AS is per-process address space, not an aggregate process-tree quota.
Both modes enforce timeout, bounded integer arguments and exclusive evidence
outputs. Neither is an exact RSS ceiling. Windows's reported peak committed
usage can exceed the configured value slightly (observed ~0.22MiB); recorded
metrics are retained rather than rounded down. The 64MiB Windows worker admitted
61MiB of malloc buffers and then received allocation failure.

[Windows Job memory fields](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information)
describe the selected committed-memory metric.

## Verification and review

| Final configuration / check | Executed result |
| --- | --- |
| Windows GCC15, Index32, serial, C ABI ON, full CTest | 55/55 PASS |
| Windows GCC15, Index64, serial, full CTest | 52/52 PASS; historical Index32 golden successes stay in the32 build;64 adds explicit rejection |
| MSVC14.44, Index64, AddressSanitizer focus | 10/10 PASS, no sanitizer report |
| Linux GCC13, Index64, OpenMP, codec OFF | 11/11 PASS |
| Linux AddressSanitizer + UBSan, new contracts | 955 checks PASS, no report |
| Linux TSan, new contracts | 955 checks PASS with child-only `setarch x86_64 -R`; initial ASLR mapping startup failure retained |
| INP file layer | 9 test groups,600-name recognition, exact roundtrip, unsafe/corrupt/budget/alias/ambiguity rejection |
| Independent review | 5 P2 closed;1500 OOM budgets and dual-width self/cross-file probes |
| External installed C++ SDK | Index32 1/1 and Index64 1/1 PASS; exported ABI static_assert; actual dependency list excludes production/kernel headers |
| Installed INP tool | Capture/export original forced_tet.inp SHA256 unchanged |
| OS process envelopes | Windows and Linux malloc refusal/timeout/overflow checks PASS; Windows aggregate parent20MiB + child34MiB checked |
| COW benchmark | Four1M-double fields: deep trial+32,000,000 aligned bytes; COW trial+0; first field write+8,000,000 |

The synthetic benchmark's single measured begin times were13.525ms (deep) and2.920ms
(COW); these are a workload observation, not a generalized speedup claim.
Historical DBF6/7 manifests remain untouched. Independent review closed five P2 findings: failed-COW
borrow invalidation, NTFS ADS, symlink aliases, int iota overflow and duplicate
external INPUT dependency capture. See `verification/reviews/DBF8/independent-current/REVIEW.md`.
Independent evidence includes 1500 COW OOM budgets, dual-width self-read/cross-width
rejection and byte/path/alias probes. Final content hashes bind the reviewed source.

## Remaining environment / migration gates

No production repository build, native FNL/FEG/IDARRAY capture, production kernel
adapter, CUDA device execution, physical power-loss certification, or new physics
kernel is claimed. Linux optional zstd headers/library were unavailable in the
current environment; DBF8 Linux used codec OFF (DBF7 evidence is not relabeled).
Physical >4-billion-entity capacity and strict aggregate Linux process-tree quotas
need suitable resources/OS delegation. This delivery supplies the local seams;
production consumers must adopt the SDK ABI, phase/lease and capability contracts.
