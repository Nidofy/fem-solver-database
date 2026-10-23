param(
    [string]$CMake = 'cmake',
    [string]$CTest = 'ctest',
    [string]$Generator = 'Ninja',
    [string]$Ninja = 'ninja',
    [int]$Nodes = 1000000
)
$ErrorActionPreference = 'Stop'
$sdbRoot = Split-Path $PSScriptRoot -Parent
$sdbBuild = Join-Path $sdbRoot 'build'
function Invoke-SdbTool([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Invoke-SdbTool $CMake @('-S', $sdbRoot, '-B', $sdbBuild, '-G', $Generator,
    "-DCMAKE_MAKE_PROGRAM=$Ninja", '-DCMAKE_BUILD_TYPE=Release', '-DSDB_OPENMP=ON')
Invoke-SdbTool $CMake @('--build', $sdbBuild, '--parallel', '4')
$env:OMP_NUM_THREADS = '4'
Invoke-SdbTool $CTest @('--test-dir', $sdbBuild, '--output-on-failure')
Invoke-SdbTool (Join-Path $sdbBuild 'explicit_tet.exe') @()
foreach ($sdbRun in 1..3) {
    $sdbOutput = & (Join-Path $sdbBuild 'sdb_benchmark.exe') $Nodes 20
    if ($LASTEXITCODE -ne 0) { throw 'Serial benchmark failed' }
    $sdbOutput | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot "benchmark_1m_serial_run$sdbRun.jsonl")
    foreach ($sdbThreads in 4,8) {
        $env:OMP_NUM_THREADS = "$sdbThreads"
        $sdbOutput = & (Join-Path $sdbBuild 'sdb_benchmark.exe') $Nodes 20 omp
        if ($LASTEXITCODE -ne 0) { throw 'OpenMP benchmark failed' }
        $sdbOutput | Set-Content -Encoding utf8 (Join-Path $PSScriptRoot "benchmark_1m_omp${sdbThreads}_run$sdbRun.jsonl")
    }
}
