param([ValidateSet('build-b01','build-b01-serial')][string]$Build='build-b01')
$ErrorActionPreference='Stop'
Set-Location (Join-Path $PSScriptRoot '../../..')
$env:PATH='D:\Software\MinGw-w64\x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0\mingw64\bin;'+$env:PATH
$env:OMP_NUM_THREADS='4'
$cmake='C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
$label=if($Build -eq 'build-b01'){'gcc-openmp'}else{'gcc-serial'}
$omp=if($Build -eq 'build-b01'){'ON'}else{'OFF'}
$sdk=(Resolve-Path 'build-deps/zstd').Path.Replace('\','/')
git rev-parse HEAD | Out-File "$PSScriptRoot/$label-source.log"
& $cmake -S . -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSDB_OPENMP=$omp" -DSDB_ZSTD=ON "-DSDB_ZSTD_INCLUDE_DIR=$sdk/include" "-DSDB_ZSTD_LIBRARY=$sdk/libzstd.dll.a" *> "$PSScriptRoot/$label-configure.log"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Copy-Item -LiteralPath 'C:/Program Files/Git/mingw64/bin/libzstd.dll' -Destination "$Build/libzstd.dll" -Force
& $cmake --build $Build --clean-first --parallel 3 *> "$PSScriptRoot/$label-build.log"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $ctest --test-dir $Build --output-on-failure --timeout 300 *> "$PSScriptRoot/$label-ctest.log"
$result=$LASTEXITCODE
Get-Content "$PSScriptRoot/$label-ctest.log" -Tail 6
exit $result
