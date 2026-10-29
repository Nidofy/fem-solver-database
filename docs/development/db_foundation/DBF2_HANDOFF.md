# DBF2 handoff

Branch: `codex/astra-db-foundation-completion`.
Implementation base: `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`.
Initial status: **READY_FOR_REVIEW** after GCC/OpenMP 23/23 self-tests.
No push, merge or release is authorized or performed.

Changed files are limited to the standalone nested module: typed IDs/schema
adapters; MeshDB/StateDB sample layout; ModelIR/compiler/input/ModelStore;
result/indexed/async/export; RestartDB; CSR/BSR metadata; CLI; isolated tests,
goldens, CMake targets and this evidence. Parent repository documentation receives
append-only navigation/status notes. The user-supplied completion kit remains
untracked and unchanged.

Implemented capabilities and API contracts are in DBF2_CONTRACTS and the DBF2
CAPABILITY_MATRIX table. Verification commands/outcomes are in DBF2_VERIFICATION.
The synthetic fixture verifies storage; real numerical regression uses existing
TET/HEX kernels. Unknown new physical semantics fail closed at compilation report
and direct plan construction. No kernel, production API or production build was
modified.

Migration requires rebuilding standalone C++ consumers, retaining model/sample/
history metadata, and validating production adapters plus CUDA in their actual
build environment. Old schema1/2 and legacy results remain supported by the new
reader. Older readers intentionally reject the new required schemas.

Next gate: independently inspect the frozen implementation with Astra/high,
reproduce relevant negatives and review exact model/state/result semantics; fix
findings, rerun affected suites and record final fixed SHA and evidence hashes.
