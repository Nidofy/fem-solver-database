# DBF2 verification

Implementation base: `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`.
Inherited review boundary remains `480da32a17e3b60e6694b95e5c9ec83618e5b64b`;
the independent DBF1 report does not assert a fresh exhaustive ALR1 audit.

## Final outcome

Independent Astra/high verdict: **PASS, bounded DBF2 slice**, at library/source
commit `1f39b7313a10002099ddc23c7ebb4b89a6633bdb`. The review reproduced and closed
three P1 and three P2 findings; original failures and independent green probes are
retained. See [independent report](../../../verification/reviews/DBF2/independent-astra/report.md)
and [fix map](DBF2_REVIEW_FIXES.md). No unresolved P0/P1/P2 was established in scope.

The final test-only followup `172b46c1d2746d52646a8f4a65a1044d9ed5c1af` isolates
allocation instrumentation to the producer thread. MSVC's empty worker-side
`std::list` allocates a sentinel during asynchronous startup, unlike libstdc++;
counting all threads made the producer-only allocation assertion nondeterministic.
The test still detects the original O(rows) producer allocation and checks worker
validation. No library code changed after the independent review SHA.

| Final configuration | Outcome / exit | Time | Evidence under verification/reviews/DBF2 |
|---|---|---|---|
| GCC 15.2 Release, OpenMP ON | 24/24, exit 0 | 16.68 s | openmp-final-test.txt, openmp-details.txt |
| GCC 15.2 Release, OpenMP OFF | 24/24, exit 0 | 16.48 s | serial-final-test.txt |
| MSVC 14.44 RelWithDebInfo, ASan, OpenMP OFF | 24/24, exit 0 | 97.33 s | asan-final.txt, asan-details.txt |
| GCC reads MSVC model/result/restart, both precisions | 32 checks, exit 0 | — | cross-msvc-to-gcc-final.txt |
| MSVC ASan reads GCC model/result/restart, both precisions | 32 checks, exit 0 | — | cross-gcc-to-msvc-final.txt |
| Independent Python wire oracle, both precisions | PASS, exit 0 | — | wire-oracle-final.txt |

Focused final counts: 211 model checks + 42 fresh-process checks; 421 typed result
checks; 64 boundary/matrix/layer checks; 48 composite checks + 32 fresh-process
checks. Allocation admission is checked by heap instrumentation, not a modeled
counter alone. Existing lifecycle, contact, HEX/TET mechanics, async, legacy
goldens and 100,000-frame indexed tests remain in the full suite.

Final Release scaling CSV: `typed-results-scaling-final.csv`, all 12 cases PASS.
With complete synthetic source keys, 10,000 entities × 4 fields × 4 frames use
17,918,851 file bytes; the measured write/open/query times are 342.361/10.485/0.713
ms. A one-row query reads 65,153 bytes and decodes 2,048 scalar bytes. Conservative
single-frame admission charge is 24,072,394 bytes; it is not process peak RSS.
No performance ratio is asserted.

The remaining sections retain the earlier freeze and intermediate failure history.

## Self-test evidence at review freeze

- GCC 15.2 Release/OpenMP: **23/23 CTest tests passed**, exit 0, 17.41 seconds.
- New focused suites: 59 boundary/matrix/layer checks; model worker 211 plus 42
  separate-process checks; typed worker 363 checks; 48 composite checks and 32
  separate-process composite checks. CTest logs retain final compiled counts.
- Frozen schema1/schema2 model files, Native1/Indexed2 result files, and old
  translation restart remain readable. Schema1/2 re-save bytes are compared exactly.
- Real CPU FP32/FP64 runs write TypedIndexed3, publish linked checkpoints, append
  after restore, and match uninterrupted Indexed2 displacement histories exactly.
- GCC serial and MSVC 14.44 ASan runs are in progress at the initial review freeze;
  final outcomes will be recorded below. Production/CUDA are NOT_RUN.

Commands (from the nested repository):

```powershell
cmake --build build-b01 --parallel 2
ctest --test-dir build-b01 --output-on-failure --timeout 300
cmake --build build-b01-serial --parallel 2
ctest --test-dir build-b01-serial --output-on-failure --timeout 300
cmd /c verification\reviews\ALR1\run-asan.cmd
build-b01\sdb_typed_results_scaling.exe build-dbf2-golden\typed-scaling-final
```

GCC runtime bin must be first on PATH. OpenMP tests use OMP_NUM_THREADS=4;
the serial build has OpenMP disabled. Existing ASan runner selects MSVC 14.44.
Initial ASan compile reached COFF section capacity in the expanded CLI template
instantiations; `/bigobj` was added only to this standalone lifecycle interface.
This is an object-format capacity flag, not a missing-library workaround.

The first old-result oracle assumed four nodes/two frames; inspecting the frozen
forced_tet fixture established five nodes, two elements, frequency=100 and a
stop-after=1 initial-only frame. The corrected oracle checks those actual fixture
properties and exact initial zeros. Production decoding code was not changed to
make this assertion pass.

## Scaling evidence

`tests/typed_results_scaling.cpp` checks 12 entity/field/frame combinations:
100/1,000/10,000 entities × 1/4 fields × 1/4 frames. It reports file bytes,
write/open/selective query time, actual reader bytes/scalar bytes, conservative
async admission charge and source immutability. No fixed performance multiplier
or process-RSS claim is made. Open must decode zero scalar value bytes; a selected
uint64 value is checked against an independent arithmetic oracle.

Worker GCC O1 sample: 10,000 × 4 × 4 wrote 17,758,851 bytes; 313.855 ms write,
9.718 ms open and 0.667 ms query. Query read 65,153 bytes and decoded 2,048 scalar
bytes (a 256-row block) to return one row. A final Release run is retained in
the fixed review evidence. Machine timing is descriptive, not a guarantee.

## Limits

Full solver/production SDK build was not attempted. CUDA compilation and hardware
execution, power-failure durability, full Abaqus syntax and new physical algorithms
remain unverified or explicitly outside scope. Self-tests are not an independent
review. The independent review report and exact reviewed SHA are recorded in
DBF2_HANDOFF when available.
