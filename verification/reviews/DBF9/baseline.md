# DBF9-00 · CPU baseline

On 2026-11-24 the independent SDB repository was at `0c7a7717c4b2ff9264237f02b3aa0efb49c79305` on `codex/standalone-database`. The tracked tree was clean. Pre-existing untracked delivery, validation and planning files are outside the batch. The development branch is `dev/sdb-dbf9`.

Windows 11 build 26200, Intel Core Ultra 9 285H (16 logical processors), 33,820,106,752 bytes RAM. MinGW-w64 UCRT GCC 15.2.0, CMake 4.3.1-msvc1, Ninja 1.13.2, Python 3.14.0, OpenMP 4.5. The matching mingw64/bin was first on PATH. OMP_NUM_THREADS=4.

```sh
cmake -S . -B build-dbf9-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON -DSDB_C_API=ON -DSDB_CUDA=OFF
cmake --build build-dbf9-cpu --parallel 8
ctest --test-dir build-dbf9-cpu --output-on-failure -j 4
```

All three commands exited 0. Baseline CTest passed **79/79 in 43.83 s** (Index32, optional zstd disabled). The baseline goldens and independent process readers are part of that count.

The separate `cmake -S . -B build-dbf9-cuda-probe -G Ninja -DSDB_CUDA=ON` probe returned 1 at CUDA language detection with `Failed to find nvcc`. The Windows graphics inventory reported Intel Arc 140T and an indirect display driver. Real CUDA is **BLOCKED_ENV — not locally compiled / not locally verified**.
