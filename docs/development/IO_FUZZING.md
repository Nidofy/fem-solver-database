# Bounded coverage-guided reader tests

Linux Clang/compiler-rt: run python3 tests/fuzz/smoke.py BUILD_DIR or configure
with SDB_FUZZING=ON and SDB_OPENMP=OFF. Three independent libFuzzer targets
exercise the INP reader, native ModelIR decoder and checkpoint reader. Each smoke
campaign starts from a valid input and runs 5000 mutations with ASan/UBSan and
a fixed seed. The three WSL campaigns completed without a sanitizer finding.
Extend the corpus and run count for longer investigations. A named recursive
metadata hasher avoids a Clang 18 frontend crash on the previous recursive
generic lambda while retaining the canonical byte traversal and FNV constants.
