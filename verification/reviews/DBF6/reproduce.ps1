param([string]$CMake='cmake', [string]$Cxx='', [string]$Ninja='', [string]$Build='build-dbf6-reproduce')
$ErrorActionPreference='Stop'
Set-Location (Join-Path $PSScriptRoot '../../..')
$configure=@('-S','.', '-B',$Build, '-G','Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSDB_OPENMP=OFF', '-DSDB_ZSTD=OFF')
if($Cxx){$configure+="-DCMAKE_CXX_COMPILER=$Cxx"}
if($Ninja){$configure+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
& $CMake @configure
if($LASTEXITCODE){throw 'Standalone configure failed'}
& $CMake --build $Build --parallel 3
if($LASTEXITCODE){throw 'Standalone build failed'}
$ctest=Join-Path (Split-Path (Get-Command $CMake).Source) 'ctest.exe'
& $ctest --test-dir $Build --output-on-failure --timeout 300
if($LASTEXITCODE){throw 'Standalone tests failed'}
$prefix=Join-Path (Resolve-Path $Build).Path 'install'
& $CMake --install $Build --prefix $prefix
if($LASTEXITCODE){throw 'Standalone install failed'}
$consumer=$Build+'-consumer'
$external=@('-S','tests/install_foundation_consumer','-B',$consumer,'-G','Ninja',"-DCMAKE_PREFIX_PATH=$prefix")
if($Cxx){$external+="-DCMAKE_CXX_COMPILER=$Cxx"}
if($Ninja){$external+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
& $CMake @external
if($LASTEXITCODE){throw 'External consumer configure failed'}
& $CMake --build $consumer --parallel 2
if($LASTEXITCODE){throw 'External consumer build failed'}
& $ctest --test-dir $consumer --output-on-failure
if($LASTEXITCODE){throw 'External consumer tests failed'}
