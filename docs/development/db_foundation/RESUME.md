# Resume DB foundation work

## Current DBF7 continuation (2026-10-30)

Read DBF7_HANDOFF, DBF7_CONTRACTS and the DBF7 manifest first. SDK 2.3 includes
reviewed DBF6 plus local P0-P2 contracts/tooling. Independent DBF6/DBF7 scoped
reviews are closed; current platform results and limits are in the handoff.
Do not re-add existing time interval queries or use old DBF1/DBF5 status as current.
DBF6 original manifest remains historical and is not expected to match modified
DBF7 files. Use the new content manifest for this delivery.
Native capture/mirror work is the next production gate. Full INP, global MeshDB
Index64, global RSS/COW and unexposed FFI/spectral extensions are not claimed.
Preserve the original kit and the ALR1/B02c acceptance boundaries. No merge/push.

## Historical DBF5 continuation

User authorization extended to phase three and remaining standalone work.
Implementation base: `a9a203c67d78ed8649e12e05c15214ae2b95ea63`.
Reviewed runtime source: `7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
Status: COMPLETE within the standalone scope; independent scoped review closed,
Windows serial/OpenMP/MSVC ASan each45/45, Linux focused/sanitizer/package/crash
and cross-platform reader checks passed. No runtime/test edits after this SHA.
Read DBF5_HANDOFF, DBF5_VERIFICATION, DBF5_REVIEW_FIXES and the independent
DBF5 report before continuing. Verify the packet manifest before reusing evidence.
CompressedIndexed4, checked reference consumers and the copied-snapshot
migration seam now exist; do not repeat their historical gap lists.

Native integration must start in the current buildable production repository:
capture actual FNL/FEG/IDARRAY snapshots, compare mirror data and exact equation
maps, run legacy/new kernels per element family and benchmark real batches.
Preserve storage-only gates for unsupported physics. This checkout has no native
SDK/device or physical power-loss certification. ALR1/B02c are not implicitly
merged or globally accepted; the historical 1.3% view-overhead number is not a
general contract guarantee. Preserve the original untracked user kit.

## Historical DBF4 phase-two continuation

The user subsequently authorized phase two. Its implementation baseline is DBF3
`5349ccfd5f1dff49b87fac88ffa43b61dc5f40ed`; final reviewed source is
`910510d5340bfac779a2510eec8c78def73c5134`, final test commit `4769985`.
DBF4 is COMPLETE within its stated scope: independent Astra/high PASS, all three
clean-build Windows configurations 39/39, Linux TSan and process campaigns PASS.
Read DBF4_CONTRACTS, DBF4_HANDOFF and DBF4_VERIFICATION. Scope is explicit
durability barriers, ordered Result→Restart→Link publication, typed ambiguous
failure outcomes, fault campaigns and supported TSan. Compression and production
adapters remain separate later work. Older phase-one-only authorization notes
below are historical and superseded for durability by this request.

## Historical DBF3 phase-one continuation

Start with DBF3_HANDOFF, DBF3_VERIFICATION, DBF3_REQUIREMENTS and the independent
`verification/reviews/DBF3/independent-astra/REVIEW.md`. Final reviewed library
source is `c53ab3e462bc44740f1de91f91efa72db0eefbbf`. It includes the three
independently reproduced P1 fixes and the InstanceId documentation correction.
Verify the DBF3 manifest before reusing its evidence. Do not repeat the historical
DBF2 gap list: registered typed State, local history basis and schema4 semantic
definitions are now implemented within their documented contracts.

Keep packed FP32/64 material histories distinct from seven-lane registered dense
fields; generic Face/Contact registration is not supported. Existing CPU kernels
remain the numerical subset; new physical definitions are storage-only. Old
affected step-time-amplitude run configurations cannot silently resume after the
continuation correction. The original user kit remains preserved and untracked.

The user authorized phase one only for this continuation. Later durability,
compression and production-adapter phases are not started here. ALR1 and B02c
are not automatically accepted or merged. Preserve all unrelated parent work.

## Historical DBF2 state

Do not repeat the completed DBF1/DBF2 implementation tasks listed in the historical
section below. Read DBF2_HANDOFF, DBF2_VERIFICATION, DBF2_REVIEW_FIXES and the independent
report under `verification/reviews/DBF2/independent-astra/` first.

Implementation: `826e65352772cd10508297466e59cd6e11816388`; cross-library diagnostic
fix: `cfeeab7`; six independent-review corrections:
`1f39b7313a10002099ddc23c7ebb4b89a6633bdb`.

Verify the final DBF2 manifest before reproducing tests. Use the packet's
`reproduce.ps1` with explicit local CMake/CTest/GCC/Ninja paths; never build the
production parent repository. The three current standalone configurations are
build-b01 (GCC/OpenMP), build-b01-serial (GCC serial) and build-b01-asan (MSVC ASan).
The final handoff records independent review and test outcomes at these source
commits. Preserve the untracked completion kit and unrelated parent work.

Any future work should start from the actual capability matrix, not the original
gap checklist: typed model/result storage and the synthetic D7 composite now exist.
New physics, arbitrary typed StateDB registration, full INP grammar, automatic
local material-history basis adaptation and CUDA/production adapter verification
remain separate bounded extensions. Do not remove fail-closed gates to advertise
execution before implementing and validating the relevant numerical kernels.

## Historical DBF1 continuation instructions

Read this directory, the kit, actual Git status/history and the DBF1 fixed-content
manifest before editing. DBF1 self-tests do not constitute independent acceptance.
ALR1 and B02c historical review boundaries remain intact.

First reproduce `cmake --build build-b01 --parallel 3` and
`ctest --test-dir build-b01 --output-on-failure --timeout 300` with the matching
compiler runtime on PATH. Portable toolchain parameters and ASan commands are in
the review packet. Check manifest hashes before reviewing fixed content.

Next dependency-ordered work:

1. Independently review DBF1 schemas/fingerprints, old-file compatibility, enum
   narrowing, borrow invalidation and restoration failure atomicity.
2. Define a versioned typed ResultField extension using FieldDescriptor and the
   existing native/indexed formats. Carry integer/validity, history/IP/layer/time
   addresses through independent Reader and snapshot ownership. Retain golden
   native-v1/v2 results; do not create a second result database.
3. Add orientation references and typed shell/beam/layer and parameter/history
   bindings to ModelIR2 through explicit migration, validation and storage-only
   fixtures. Unknown execution must keep failing closed.
4. Add equation/constraint/load contracts and controlled input fixtures only after
   the corresponding model schema exists. Verify external keyword semantics then.
5. Build the full D7 composite storage fixture; current mixed-IP history and
   generic storage tests are not substitutes for every entity/address type.

Do not add new numerical kernels, alter the parent build, merge/push, or reuse
unaccepted B02c changes implicitly. Preserve unrelated files and user kit input.
Record CUDA/production execution as NOT_RUN unless actually verified. Continue
small compiled/serialized/queried slices, and keep the capability matrix honest.
