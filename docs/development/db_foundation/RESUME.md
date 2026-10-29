# Resume DB foundation work

## Current DBF3 phase-one continuation

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
