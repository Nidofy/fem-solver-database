param(
    [string]$CMake = 'cmake',
    [string]$CTest = 'ctest',
    [string]$Compiler = 'g++',
    [string]$Ninja = 'ninja'
)
$ErrorActionPreference = 'Stop'
$moduleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Push-Location $moduleRoot
try {
    python verification/reviews/DBF2/verify_packet.py --git
    if ($LASTEXITCODE -ne 0) { throw 'packet verification failed' }
    foreach ($mode in @('OFF', 'ON')) {
        $build = "build-dbf2-reproduce-$mode"
        & $CMake -S . -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$Compiler" -DCMAKE_BUILD_TYPE=Release "-DSDB_OPENMP=$mode" -DSDB_CUDA=OFF
        if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
        & $CMake --build $build --parallel 2
        if ($LASTEXITCODE -ne 0) { throw 'build failed' }
        $env:OMP_NUM_THREADS = '4'
        & $CTest --test-dir $build --output-on-failure --timeout 300
        if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
    }
} finally { Pop-Location }
