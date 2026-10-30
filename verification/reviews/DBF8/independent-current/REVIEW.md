# Independent DBF8 working-tree review

Date: 2026-10-30. Reviewer: independently delegated Codex review agent.
Scoped decision: PASS WITH EXPLICIT SCOPE LIMITS after corrections below. No
confirmed open P0/P1/P2 issue remains in the reviewed snapshots. This is not a
release, production numerical certification, named-model reviewer claim, or an
acceptance of later edits to the moving tree.

## Source identity

- `snapshot` / `source-hashes.json`: initial implementation used for reproductions.
- `fixed-snapshot` / `fixed-source-hashes.json`: corrected COW and path code used for
  the independent COW sweep and path/roundtrip probe.
- `final-tools` / `final-tools-hashes.json`: corrected logical-alias preservation
  used for the final symlink roundtrip probe.

Scope: Index configuration and strong IDs, native prefix epoch and existing
checkpoint width gate, CSR index metadata, Field COW and registry/session trial
publication, global C++ allocation accounting, OS worker launcher, lossless INP
document parsing/capture/save/load/export and offline keyword catalog use.
All reviewer writes are confined to this review directory. Implementations were
corrected by the parent, not the reviewer.

## Findings and resolution

1. **P2, CLOSED: COW trial OOM invalidated live checked borrows without starting a
   trial.** Original RegisteredField shared construction called fork_shared before
   subsequent metadata allocations could fail. The independently compiled sweep
   reproduced failure at an additional allocation allowance of 10688 bytes:
   in_trial=0 and value=7, but the original checked borrow was expired
   (`cow-probe.log`). The fix stages private shared fields without invalidation,
   finishes all throwing allocations, then invalidates borrows and publishes the
   trial using nonthrowing operations. An independent 1500-point sweep over
   allowances 0..47968 in steps of 32 found no partial invalidation after the fix
   (`fixed-cow-probe.log`). Raw borrows remain caller-managed phase-boundary data.

2. **P2, CLOSED: stored source names could address NTFS alternate data streams.**
   `dir/source.inp:hidden` passed load and export; Windows created a hidden stream
   while directory enumeration showed only source.inp (`path-probe.log`). Uniform
   portable source-name validation now rejects colon/ADS names, reserved devices,
   trailing dots/spaces and traversal forms. The independent fixed path test
   checked ten invalid names, exact CRLF/unknown-card byte roundtrip, and refusal
   to overwrite an existing document: 12 checks PASS (`fixed-path-probe.log`).

3. **P2, CLOSED: resolving an include symlink discarded its logical alias.**
   A real Windows alias.inp symlink to child.inp was referenced by main.inp. Capture
   stored child.inp while the original main bytes still referenced alias.inp;
   export succeeded, but reading the exported deck failed FileNotFoundError
   (`symlink-probe.log`). The fix uses resolved identities for boundary/cycle
   checks while retaining logical source paths for capture/export. The independent
   fixed probe confirms alias.inp is captured/exported and the exported deck reads
   successfully (`fixed-symlink-probe.log`). The symlink is materialized as ordinary
   source bytes in the export; recreating an OS symlink is not required.

4. **P2, CLOSED by static correction: int-valued iota counters limited large-index
   numbering.** DofMap's external-equation initialization and two indexed-result
   order vectors used literal int 0 as the iota counter. Its type is independent
   of the 64-bit destination; signed overflow would occur past INT_MAX. All three
   counters now explicitly use uint64_t or size_t. This was statically verified;
   allocating billions of entries was not attempted.

Additional boundary hardening: the launcher now rejects byte limits outside the
platform size_t/OS argument range before ctypes assignment. The initially suspected
zero-wrap unlimited job was NOT established: Windows rejected an exact 2^64 request
with ERROR_INVALID_PARAMETER before starting a worker. This is not reported as a
reproduced unlimited-memory bypass. Nonzero truncation/report ambiguity justified
explicit range validation.

## Independent execution

Windows GCC 15.2, C++20, O0, frozen include trees and copied process_memory.cpp:

- Original COW allocation-failure reproduction confirmed the issue above.
- Corrected COW sweep: 1500 budget points, no partial borrow invalidation found.
- Separate Index32 and Index64 executables each wrote/read a native prefixed vector;
  each rejected the other executable's file at the prefix before decoding metadata
  (`epoch.log`). This directly verifies the epoch gate, not every production format.
- Windows Python probes: original ADS/symlink reproductions, then corrected path
  validation/byte roundtrip and symlink roundtrip passed.
- An initial WSL attempt for the symlink probe was denied by the environment. The
  actual reproduced finding and correction evidence both came from Windows Python,
  which successfully created the test symlink inside this review directory.

Compiler logs, probe sources and logs are retained. No existing test results are
relabeled as independent execution.

## Retained boundaries

The allocation governor serializes reservation, live-limit update and release under
one atomic flag; admission occurs before malloc and failed malloc releases its
reservation. Static review found no confirmed limit-accounting race. It intercepts
linked executable C++ new, not all malloc/GPU/third-party heaps or RSS. Windows job
committed-memory and Linux per-process virtual-address limits are distinct policies.
No independent OS-wide memory stress or new sanitizer campaign was run here.

Index64 is a program-wide ABI choice; native file epochs reject mismatched builds.
Local topology/sample/component axes may remain narrower by schema. Huge physical
allocations and all native kernel Index64 paths have not been verified. The INP
layer preserves source bytes and represents cards, references and unknown metadata;
it does not establish per-keyword numerical semantics or executable solver coverage.

No full solver/native SDK build, CUDA, physical power-loss experiment, exhaustive
COW allocator fault proof, complete cross-format width matrix, or independent
600-keyword semantic certification was performed. The parent's broader platform
verification is separate evidence. Final working-tree hash reconciliation must be
recorded before reusing this review as evidence for a frozen delivery.

## Final runtime reconciliation and incremental closure

The final delivery source identity is recorded in `final-runtime-hashes.json`
(include, src, tools, data, python, cmake and CMakeLists.txt; generated Python
bytecode excluded). This records the bytes observed at review closure, not a Git
commit. All include/src files in `final-snapshot` match the current tree at closure.
The final INP implementation copied under `closure/tools/inp_document.py` also
matches the main tree (SHA256 ff9e6786fcbbcb5be356fba714bdbf167b66208fef74db9f1c6fc5d6957d3a67).

Changes since the prior correction snapshot were reviewed explicitly:

- shared_ptr uniqueness test changed to use_count()!=1 for C++20/MSVC availability;
  this preserves the detach condition. A freshly compiled focused smoke verified
  fork/detach isolation and old checked-view invalidation.
- StateSession cumulative copy/detach counters now saturate rather than throwing
  after publication. Boundary/ordinary sums passed the same independent smoke
  (`closure-smoke.log`).
- The three iota counters now use explicitly wide unsigned starting values.
- Keyword coverage remains advisory, names a conditional existing subset, and
  never grants solve authorization. Free-text unpaired quotes retain raw data;
  repeated ordinary parameters retain both values and a diagnostic. Export now
  checks decoded length and SHA256 before publishing the staged directory.

One additional **P2 finding, CLOSED**, arose from permitting repeated parameters:
non-INCLUDE `INPUT=a.dat, INPUT=b.dat` used next(...) to select only a.dat, despite
the diagnostic promising no chosen effective value. The original final-round probe
captured only duplicate.inp and a.dat (`final-inp-probe.log`). The correction rejects
ambiguous repeated external INPUT while retaining ordinary repeated parameters.
The final independent `closure-inp-probe.py` passed **8 checks**, including that
rejection, free-text preservation, repeated ordinary parameters, false solve
permission, exact original bytes, corrupted-export rejection, no published corrupt
directory, and staging cleanup (`closure-inp-probe.log`).

The report therefore closes five confirmed P2 findings in total. No confirmed open
P0/P1/P2 findings remain in the stated reviewed scope. The final scoped PASS stands.
Earlier 1500-point COW and interval test campaigns were not unnecessarily repeated;
this final round used targeted probes and source-delta inspection. The parent's
platform, package and complete CTest runs remain separately attributed evidence.
Any edits after `final-runtime-hashes.json` require appropriate incremental review.

### Last budget/build-only delta

The final reconciliation was refreshed after the parent declared runtime/build
freeze. Compared with the previous recorded runtime hashes, only
`tools/inp_document.py` and `CMakeLists.txt` changed; include/src were unchanged.
The old hash list is retained as `runtime-hashes-before-budget.json`.

The INP change rejects nonpositive/noninteger byte/depth/line budgets at entry,
including bool, before reads or file opens. An independent probe of the copied
final tool passed **20 checks** (`budget-probe.log`), covering -1, 0, True, 1.5,
and string values for all three reader budgets and the document load budget.
The CMake additions were statically checked as standalone build/test wiring;
no new library runtime behavior was introduced by that file.

`budget-closure/` preserves both changed files. The refreshed
`final-runtime-hashes.json` is the final source identity for this review:

- inp_document.py: 6d8b2c44aa7c954ef895b7a0c797d44e2fb0bbd32ba0571c23eba5881215cf6c
- CMakeLists.txt: a4374c2041aa46c4663b0bcad33dc41e6b98ae6fd06ebd860ca9a64742c5edc4

Scoped PASS is unchanged. Platform-wide CTest, sanitizers, installed consumers
and OS-envelope campaign totals reported by the parent remain parent execution
evidence, not newly executed checks by this reviewer.
