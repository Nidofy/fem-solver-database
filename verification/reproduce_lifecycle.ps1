param(
    [string]$CMake = 'cmake',
    [string]$CTest = 'ctest',
    [string]$Build = 'build-lifecycle'
)
$ErrorActionPreference = 'Stop'
$module = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = Join-Path $module $Build
& $CMake -S $module -B $buildPath -DSDB_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
& $CMake --build $buildPath --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& $CTest --test-dir $buildPath -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
$program = Join-Path $buildPath 'sdb_run.exe'
if (-not (Test-Path -LiteralPath $program)) { $program = Join-Path $buildPath 'Release/sdb_run.exe' }
$demo = Join-Path $buildPath ('demo-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $demo | Out-Null
& $program --input (Join-Path $module 'tests/fixtures/abaqus/forced_tet.inp') --backend cpu --precision double --output (Join-Path $demo 'run.sdbres') --save-model (Join-Path $demo 'run.sdbmodel')
if ($LASTEXITCODE -ne 0) { throw 'INP execution failed' }
& $program --read (Join-Path $demo 'run.sdbres') --field U --region FREE --entity 4
if ($LASTEXITCODE -ne 0) { throw 'independent reader failed' }
& $program --read (Join-Path $demo 'run.sdbres') --vtk (Join-Path $demo 'vtk') --xdmf (Join-Path $demo 'run.xdmf')
if ($LASTEXITCODE -ne 0) { throw 'export failed' }
Write-Output "Verified demo: $demo"
