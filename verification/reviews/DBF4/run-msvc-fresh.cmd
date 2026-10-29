@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
set OMP_NUM_THREADS=4
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-dbf4-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDB_OPENMP=OFF -DSDB_SANITIZE=ON -DSDB_CUDA=OFF
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-dbf4-asan --clean-first --parallel 3
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build-dbf4-asan --output-on-failure --timeout 300
exit /b %errorlevel%
