# DBF2 handoff

Branch: `codex/astra-db-foundation-completion`.
Implementation base: `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`.
Final status: **PASS for the bounded DBF2 implementation and verification scope**.
Independent Astra/high review covers source `1f39b7313a10002099ddc23c7ebb4b89a6633bdb`;
all six reported findings are closed. GCC serial/OpenMP and MSVC ASan each pass
24/24 tests. The later test-only instrumentation fix is `172b46c`; library code
is unchanged from the independently reviewed SHA.
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

Commit-to-task evidence:

| Commit | Task | Verification |
|---|---|---|
| 25cd74c | Record independent DBF1 review | DBF1 bounded PASS with independent probes |
| 826e653 | ModelIR3, typed results, sample states, rotation restart, D7 fixture | Initial GCC 23/23; per-type checks |
| cfeeab7 | Portable unknown-orientation diagnostic | MSVC ASan dependent tests 5/5 |
| 1f39b73 | Six independent review corrections and region query | Independent closed-finding probes; final library source |
| 172b46c | Producer-thread allocation instrumentation | Final GCC serial/OpenMP/MSVC ASan 24/24 each |
| Packet content head in manifest.json | Final docs/logs/review hashes | verify_packet.py Git/archive checks |

The fixed packet is under `verification/reviews/DBF2/`; `manifest.json` binds its
content commit and every delivered source/test/evidence file. Independent original
byte hashes are preserved by a narrowly scoped .gitattributes rule. Use
`python verification/reviews/DBF2/verify_packet.py --git` in the repository or omit
`--git` in an extracted archive. Review report:
[independent Astra report](../../../verification/reviews/DBF2/independent-astra/report.md).

Not implemented: new physical kernels, full INP grammar, automatic local-history
basis adapter, arbitrary dynamic typed StateDB histories, MPI, complex/frequency
samples or general unit algebra. Their unsupported execution/publication paths
remain explicit. No production SDK build, CUDA compile/device run or power-loss
durability certification was possible. These limits are recorded in the capability
matrix rather than hidden by the synthetic-storage acceptance result.
