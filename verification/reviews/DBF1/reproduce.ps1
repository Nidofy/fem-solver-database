param(
    [Parameter(Mandatory=$true)][string]$CMake,
    [Parameter(Mandatory=$true)][string]$Ninja,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$Build = 'build-dbf1',
    [ValidateSet('ON','OFF')][string]$OpenMP = 'ON'
)
$ErrorActionPreference = 'Stop'
$env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
$env:OMP_NUM_THREADS = '4'
& $CMake -S . -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$Compiler" "-DSDB_OPENMP=$OpenMP" -DSDB_CUDA=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $CMake --build $Build --parallel 3
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path (Split-Path -Parent $CMake) 'ctest.exe') --test-dir $Build --output-on-failure --timeout 300
exit $LASTEXITCODE
