@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-dbf7-asan -G Ninja -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDB_OPENMP=OFF -DSDB_SANITIZE=ON -DSDB_CUDA=OFF -DSDB_ZSTD=OFF
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-dbf7-asan --target sdb_dbf6_contract_tests sdb_dbf7_contract_tests sdb_dbf7_query_tests sdb_typed_results_tests --parallel 2
if errorlevel 1 exit /b 1
"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build-dbf7-asan -R "dbf6|dbf7|typed_results_contracts" --output-on-failure
exit /b %errorlevel%
