# DBF5 verification and evidence

Base `a9a203c67d78ed8649e12e05c15214ae2b95ea63`; initial candidate
`493a6ddb6aed3a6837be3e3cfa8382a8da4b14d6`; fixed runtime source
`7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
All paths below are relative to `verification/reviews/DBF5/` unless noted.

## Final matrix

| Verification | Result | Raw evidence |
|---|---|---|
| Windows GCC15.2 OpenMP, clean fixed-source build | 45/45 PASS; cache normalized to ON, no rebuilt objects required | gcc-openmp-build/ctest.log, gcc-openmp-normalize-option.log |
| Windows GCC15.2 actual serial, clean fixed-source build | 45/45 PASS; cache OFF, no OpenMP flags, consumer 81,803 | gcc-serial-build/ctest.log |
| Windows MSVC14.44 ASan, fresh English cache, clean build | 45/45 PASS | msvc-asan-build-ctest.log |
| Windows no-codec fixed-source binaries | compressed integration 60; CLI 4 PASS | windows-nocodec.txt |
| Linux zstd ON normal focused suite | 8/8 PASS including typed/indexed/async and DBF5/CLI | linux-cmake-on-ctest.log |
| Linux zstd OFF focused suite | 3/3 PASS | linux-cmake-off-ctest.log |
| Linux TSan, fixed compressed async integration | 60 checks PASS, no race report | linux-tsan-dbf5_compressed_tests-run.log |
| Linux ASan+UBSan | codec, compressed 60, consumer 81,803, migration 113 PASS | linux-asan-*-run.log |
| Linux install and external find_package consumers, feature ON/OFF | both build and run PASS | linux-package-{on,off}-*.log |
| Final compressed process campaign | Windows 71 / Linux 87 child checks PASS | final-compression-fault-gcc-nocodec.txt, linux-compressed-fault-campaign.log |
| Final independent compression wire oracle | Windows1.5.7 and Linux1.5.5 PASS, 840 blocks/case | final-compression-wire-oracle-gcc.txt, linux-compressed-wire-oracle.log |
| Cross-platform native readers and oracle | Windows GCC/MSVC read Linux1.5.5 output; Linux reads Windows1.5.7 output; all PASS | windows-native-reader-linux-files.txt, msvc-native-reader-linux-files.txt, linux-native-reader-windows-files.log, windows-oracle-linux-files.txt |

The Linux sanitizer codec and migration binaries have unchanged transitive inputs
and were rerun with dependency/blob/runtime-input hash proof. Compressed and
consumer sanitizer binaries were rebuilt after the repair. Legacy async TSan also
passed at candidate 493a6dd; its explicitly preliminary evidence is retained,
while fixed-source concurrency acceptance uses the new compressed integration.
Linux's full focused command script ended with `failures=0`. Its ordinary
100,000-frame indexed test took 212.21 seconds on the mounted filesystem.

## Independent review

Astra/high reviewed runtime, CLI, dependency handling, consumers and migration.
One P2 recovery preallocation defect was independently reproduced and repaired.
The original probe changed from accepting an 8 MiB tail allocation to rejecting
before allocation (largest tracked allocation 1,576 bytes). See DBF5_REVIEW_FIXES
and the final `independent-astra/` report. Six inner-envelope corruption probes
repair outer CRC/reference chains and still reject correctly.

## Performance evidence

`final-compression-benchmark-gcc.csv` uses 10,000 rows, seven fields and three frames.
Typed3 is 22,811,431 bytes; repeated-value v4 is 4,590,195 bytes; random-value v4
is 5,481,867 bytes. Each has 840 blocks, 720 actually zstd encoded (U64 policy
is None). Structural addresses still compress in the random-value case; this
does not claim an entirely incompressible file. Independent Python wire parsing
checks decoded logical blocks and reference chains, not merely writer/reader
self-consistency.

`dbf5_consumer_benchmark*.txt` records 21 alternating paired samples. At 2,000
mixed elements the median isolated checked-view overhead was -0.541% within
noise; at two elements it was +35.841%. Host scheduling/frequency were not pinned.
No universal <=1.3% claim is accepted. Complete CPU/reference times additionally
include different assembly paths and are not isolated view costs.

The final GCC15.2 Release serial migration fixture repeated 2,000 iterations:
snapshot import 4.5875 us, private TET scratch 0.3945 us, reduction 0.0685 us;
checksum 60,000, 113 checks PASS (`final-migration-benchmark-gcc.txt`). These
tiny-fixture timings supplement the earlier O2 measurements, not a speedup claim.

## Reproduction and limits

`run-gcc.ps1`, `run-msvc.cmd` and `run-linux-focused.sh` record local commands.
Adjust toolchain/SDK locations for another machine. Build only this standalone
repository. Windows uses GCC15.2 or MSVC14.44; MSVC has a fresh English Ninja
cache with VSLANG=1033 to avoid the earlier DBF4 include-prefix cache problem.
Windows zstd is the actual installed 1.5.7 DLL; Linux uses installed 1.5.5.
Dependency provenance/header hashes are in DBF5_COMPRESSION. Libraries themselves
are release binaries, not instrumented dependency rebuilds.

WSL Ubuntu24.04 on /mnt/d v9fs/drvfs is the Linux environment. TSan uses only
per-process `setarch x86_64 -R`, with no host ASLR change. Preliminary and
interrupted logs are retained and labelled. MSVC supplies ASan, Linux focused
checks supply ASan+UBSan and TSan; these are distinct evidence, not interchangeable.
Compiler warnings are retained in raw logs; passing is not a warning-free claim.

The first nominal GCC serial matrix is superseded: an unquoted PowerShell option
became literal `$omp` and therefore enabled OpenMP. Final serial is rebuilt with
verified `SDB_OPENMP=OFF` and no `-fopenmp` flags. A preliminary test interruption
also left a stale writer lease; its scratch was preserved after verifying the
owner test had exited. Both diagnostics and all superseded logs are retained.
See `gcc-option-correction.txt` and `gcc-serial-stale-lock-recovery.txt`.

An extra direct MSVC cross-reader launch outside vcvars made no progress before
test output (zero CPU, ASan runtime not available on that shell's PATH), and was
stopped. Its partial log is retained as `windows-crossread-missing-msvc-env.txt`.
The actual cross-reader check uses `run-msvc-crossread.cmd`, initializes the same
14.44 environment as the successful 45-test matrix, and passes both files.
This is separate from the normal matrix; no failing launch was hidden as a pass.

Production/CUDA/full external SDK build, physical power loss, firmware caches and
other file systems/zstd distributions were not verified. Existing compatibility
goldens and numerical tests bound the supported standalone subset.
