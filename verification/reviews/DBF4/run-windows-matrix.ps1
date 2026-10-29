param(
    [Parameter(Mandatory=$true)][string]$CMake,
    [Parameter(Mandatory=$true)][string]$CTest,
    [Parameter(Mandatory=$true)][string]$GccBin,
    [Parameter(Mandatory=$true)][string]$VcVars,
    [string]$VcVersion = '14.44'
)
# Run from the standalone solver_database_v2 root, with its three configured
# GCC build directories. Creates a fresh English-locale MSVC cache to avoid
# localized showIncludes dependency mismatch. Never builds the parent solver.
$ErrorActionPreference = 'Stop'
$packet = Join-Path $PWD 'verification/reviews/DBF4'
$env:PATH = "$GccBin;$env:PATH"
$env:OMP_NUM_THREADS = '4'
git rev-parse HEAD | Set-Content "$packet/reproduced-source.txt"
foreach ($build in @('build-b01', 'build-b01-serial')) {
    & $CMake --build $build --clean-first --parallel 3 *> "$packet/$build-build-final.log"
    if ($LASTEXITCODE) { throw "$build build failed" }
    & $CTest --test-dir $build --output-on-failure --timeout 300 *> "$packet/$build-ctest-final.log"
    if ($LASTEXITCODE) { throw "$build tests failed" }
}
$runner = Join-Path $packet 'run-msvc-local.cmd'
@"
@echo off
call "$VcVars" -vcvars_ver=$VcVersion
if errorlevel 1 exit /b 1
set OMP_NUM_THREADS=4
set VSLANG=1033
"$CMake" -S . -B build-dbf4-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSDB_OPENMP=OFF -DSDB_SANITIZE=ON -DSDB_CUDA=OFF
if errorlevel 1 exit /b 1
"$CMake" --build build-dbf4-asan --clean-first --parallel 3
if errorlevel 1 exit /b 1
"$CTest" --test-dir build-dbf4-asan --output-on-failure --timeout 300
exit /b %errorlevel%
"@ | Set-Content -Encoding ascii $runner
& cmd /c $runner *> "$packet/msvc-fresh-build-ctest.log"
if ($LASTEXITCODE) { throw 'MSVC ASan build/tests failed' }
