# Bounded coverage-guided reader tests

Linux Clang/compiler-rt: run python3 tests/fuzz/smoke.py BUILD_DIR or configure
with SDB_FUZZING=ON and SDB_OPENMP=OFF. Three independent libFuzzer targets
exercise the INP reader, native ModelIR decoder and checkpoint reader. Each smoke
campaign starts from a valid input and runs 5000 mutations with ASan/UBSan and
a fixed seed. The three WSL campaigns completed without a sanitizer finding.
Extend the corpus and run count for longer investigations. A named recursive
metadata hasher avoids a Clang 18 frontend crash on the previous recursive
generic lambda while retaining the canonical byte traversal and FNV constants.

WSL final smoke coverage counters (5000 runs each):
- INP cov: 957 ft: 1818 corp: 120/1369b
- ModelIR cov: 480 ft: 702 corp: 74/10406b
- checkpoint cov: 367 ft: 658 corp: 22/18Kb
