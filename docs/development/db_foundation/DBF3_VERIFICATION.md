# DBF3 phase-one verification

Baseline: `00b716657ee4a2d2cbc2a5bc26ee85c2cea48937`.
Final library/source: `c53ab3e462bc44740f1de91f91efa72db0eefbbf`.
Final test-only followup: `4b124df` (persisted-model fresh reader and diagnostics).
Initial implementation review source: `47cd22e95c32548a260c5c55df61ad317351aac4`.

Independent Astra/high verdict: **PASS WITH EXPLICIT SCOPE LIMITS** at c53ab3e.
All three reproduced P1 findings and the P2 InstanceId documentation mismatch
are closed. The exact independent coverage and red/green evidence are in
[REVIEW.md](../../../verification/reviews/DBF3/independent-astra/REVIEW.md).
Parent regression results below are separate from independent probes.

## Final configuration matrix

Evidence paths are relative to `verification/reviews/DBF3/`.

| Configuration | Outcome | Evidence |
|---|---|---|
| GCC15.2 Release/OpenMP | 34/34 PASS, exit0, 20.36s | openmp-final-build.txt / openmp-reader-build.txt / openmp-final-test.txt |
| GCC15.2 Release/serial | 34/34 PASS, exit0, 20.19s | serial-final-build.txt / serial-reader-build.txt / serial-final-test.txt |
| MSVC14.44 RelWithDebInfo/ASan, serial | 34/34 PASS, exit0, 75.69s | asan-reader-final.txt |
| GCC/MSVC cross-compiler model/state/result readers | 129 checks each direction, exit0 | cross-final.txt |
| Independent Python LE/CRC/manifest/scalar oracle | FP32/64 PASS on both compilers' outputs, exit0 | wire-oracle-msvc.txt / wire-oracle-gcc.txt / dbf3_wire_oracle CTest |

Focused counts:518 topology/identity/axis,233 model+48 reader,130 registered
state+26 reader,71 result+55 reader. GCC ordinary-new campaigns exercise118
restore/80 import failure points per precision; MSVC127/83. Every injected
failure preserves destination views/cursor before the successful attempt.
Different standard libraries allocate differently; these are measured points,
not a universal allocator-failure guarantee.

Builds are successful, not warning-free. Existing aggregate-initializer warnings
remain; GCC also warns about the test-only malloc/free-backed replacement
new/delete pair used for fault injection. Both operators are paired in that
executable; no replacement allocator is added to library or production code.
Raw diagnostic logs are retained without trimming compiler whitespace.

Final scaling reuses the12 existing entity/field/frame combinations, all value,
immutability and selective-read checks pass (`scaling-final.csv`). The10000×4×4
case is17,918,851 bytes; write452.76ms, open15.893ms, selective read0.805ms;
65,153 bytes read and2048 scalar bytes decoded. Async charge24,072,394 bytes is
an admission estimate, not process RSS. No fixed speedup is asserted.

The first cross-state harness reparsed input, which changes strict provenance:
GCC stored source paths with `/`, MSVC with `\`. The expected identity mismatch
is retained in cross-initial-provenance-mismatch.txt. Test-only commits1d5e45b
and4b124df save the writer's ModelStore and load that artifact in the fresh reader.
No library identity check was relaxed. All final matrices include this followup;
library code remains identical to the independently reviewed c53ab3e.

New tests cover topology/strong-ID/axis contracts, model4 semantics and fresh
reader, registered state and fresh reader, owned local-basis/registered results
and fresh reader, exact legacy byte goldens, independent Python wire decoding,
and ordinary operator-new restore/import failure campaigns. Existing 24 tests
retain TET4/HEX8 mechanics, contact, indexed100000-frame, async ownership/budget,
storage primitives, old formats and real INP→model→compute→result→restart paths.

The first implementation matrix reported32 tests before the wire and allocation
campaign tests were registered. Its logs are historical; acceptance uses the
final34-test matrix at the final source. A successful CTest count is not an
assertion of all conceivable scalar/layout/entity combinations.

## Independent baseline fixtures

The six `tests/fixtures/dbf3/legacy/*.hex` files were regenerated from an isolated
`git archive 00b7166` source tree, compiling and executing that tree's
model_foundation_tests and foundation_integration_tests. Source identity,
lengths and SHA256 are retained in provenance.json. Current readers check FP32/64
ModelIR3/Restart4 exact resave bytes and TypedIndexed3 semantic values. Earlier
schema1/2 and Native1/Indexed2 golden tests remain unchanged.

The Python oracle does not invoke C++ codec logic: it independently decodes LE
records, CRC32, manifest fields and typed values, recalculates the manifest
fingerprint, checks UINT64_MAX and negative integers beyond2^53, AoSoA logical
order, validity and absence of reconstructed/omitted payload records.

## Reproduction

Run from the standalone repository, not the production parent. Use the existing
configured build directories or configure equivalent clean standalone trees.
Put the selected GCC runtime bin first on PATH to avoid unrelated DLL versions.
Use the MSVC14.44 developer environment for ASan executables (including cross reads).

```powershell
cmake --build build-b01 --parallel 3
$env:OMP_NUM_THREADS='4'
ctest --test-dir build-b01 --output-on-failure --timeout 300
cmake --build build-b01-serial --parallel 2
ctest --test-dir build-b01-serial --output-on-failure --timeout 300
# In the configured MSVC14.44 developer environment:
cmake --build build-b01-asan --parallel 3
ctest --test-dir build-b01-asan --output-on-failure --timeout 300
# cross-read.cmd takes vcvars64.bat and GCC runtime-bin paths as two arguments.
python verification/reviews/DBF3/verify_packet.py
```

Raw logs preserve commands/configuration evidence. Reviewer probes that include
fixed47/fixed926 fixtures require extracting those Git revisions into
build-dbf3-review/fixed47 and fixed926 as documented in the review. Their initial
counterexamples remain distinct from green probes and should not be relabeled.

## Limits and migration

No production solver build, CUDA compilation/device execution, power-loss
campaign, compression, new physical kernel or production adapter was attempted.
Packed material HistorySchema remains FP32/64; registered seven-lane dense
entity/sample fields are a separate extension owner. Face/Contact retain their
existing storage owners. No new unit algebra or frequency/modal execution exists.
Ordinary producer-thread operator-new failure points are exercised, not every
aligned/platform allocator or filesystem failure. The independent review is not
a blanket acceptance of inherited ALR1 or B02c.

Rebuild consumers and register the same frozen state contract before restore.
Affected inherited step-time amplitude runs have a new CpuRun configuration
identity; pre-fix checkpoints cannot silently continue with changed load semantics.
Unaffected legacy configurations and file encodings retain compatibility.
