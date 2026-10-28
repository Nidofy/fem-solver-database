param(
    [string]$CMake = 'cmake',
    [string]$Compiler = 'g++',
    [string]$Ninja = 'ninja',
    [string]$Build = 'build-alr1',
    [ValidateSet('ON','OFF')][string]$OpenMP = 'ON'
)
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$compilerPath = (Get-Command $Compiler).Source
$cmakePath = (Get-Command $CMake).Source
$ninjaPath = (Get-Command $Ninja).Source
$env:PATH = (Split-Path $compilerPath) + ';' + $env:PATH
$env:OMP_NUM_THREADS = '4'
& $cmakePath -S $module -B (Join-Path $module $Build) -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSDB_OPENMP=$OpenMP" -DSDB_CUDA=OFF "-DCMAKE_CXX_COMPILER=$compilerPath" "-DCMAKE_MAKE_PROGRAM=$ninjaPath"
if ($LASTEXITCODE) { throw 'configure failed' }
& $cmakePath --build (Join-Path $module $Build) --parallel 3
if ($LASTEXITCODE) { throw 'build failed' }
& (Join-Path (Split-Path $cmakePath) 'ctest.exe') --test-dir (Join-Path $module $Build) --output-on-failure --timeout 300
if ($LASTEXITCODE) { throw 'tests failed' }
