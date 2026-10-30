param([string]$CMake='cmake',[string]$Cxx='', [string]$Ninja='', [string]$Build='build-dbf7-reproduce')
$ErrorActionPreference='Stop'
Set-Location (Join-Path $PSScriptRoot '../../..')
$argsConfigure=@('-S','.', '-B',$Build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DSDB_OPENMP=OFF','-DSDB_ZSTD=OFF','-DSDB_C_API=ON')
if($Cxx){$argsConfigure+="-DCMAKE_CXX_COMPILER=$Cxx"}
if($Ninja){$argsConfigure+="-DCMAKE_MAKE_PROGRAM=$Ninja"}
& $CMake @argsConfigure
if($LASTEXITCODE){throw 'Standalone configure failed'}
& $CMake --build $Build --parallel 3
if($LASTEXITCODE){throw 'Standalone build failed'}
$ctest=Join-Path (Split-Path (Get-Command $CMake).Source) 'ctest.exe'
& $ctest --test-dir $Build --output-on-failure --timeout 300
if($LASTEXITCODE){throw 'Standalone tests failed'}
python tests/dbf7_wire_oracle.py (Join-Path $Build 'dbf7-scratch')
if($LASTEXITCODE){throw 'Wire oracle failed'}
& $CMake --install $Build --prefix (Join-Path $Build 'install')
if($LASTEXITCODE){throw 'Install failed'}
# Optional extended runs: benchmark target with 1000000 rows; query target with
# fixture, fresh directory and 1000000 frames. See HANDOFF for non-RSS budgets.
