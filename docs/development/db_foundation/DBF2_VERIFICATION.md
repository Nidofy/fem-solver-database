# DBF2 verification

Implementation base: `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`.
Inherited review boundary remains `480da32a17e3b60e6694b95e5c9ec83618e5b64b`;
the independent DBF1 report does not assert a fresh exhaustive ALR1 audit.

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
