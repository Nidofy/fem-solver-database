@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
set OMP_NUM_THREADS=4
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-dbf5-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDB_OPENMP=OFF -DSDB_SANITIZE=ON -DSDB_CUDA=OFF -DSDB_ZSTD=ON -DSDB_ZSTD_INCLUDE_DIR=D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2/build-deps/zstd/include -DSDB_ZSTD_LIBRARY=D:/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2/build-deps/zstd/zstd.lib
if errorlevel 1 exit /b 1
copy /y "C:\Program Files\Git\mingw64\bin\libzstd.dll" build-dbf5-asan\libzstd.dll >nul
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-dbf5-asan --clean-first --parallel 3
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build-dbf5-asan --output-on-failure --timeout 300
exit /b %errorlevel%
