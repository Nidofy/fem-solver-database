@echo off
rem Run from repository root. Arguments: vcvars64.bat path, GCC runtime bin.
call "%~1" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set "PATH=%~2;%PATH%"
rem Match CTest's input provenance spelling: strict ModelStore identity includes it.
set "DBF3_ROOT=%CD:\=/%"
build-b01-asan\sdb_dbf3_model_tests.exe --read build-b01\dbf3-model-scratch
if errorlevel 1 exit /b 1
build-b01-asan\sdb_dbf3_state_tests.exe --read "%DBF3_ROOT%/tests/fixtures/abaqus/forced_tet.inp" build-b01\dbf3-state-scratch
if errorlevel 1 exit /b 1
build-b01-asan\sdb_dbf3_result_tests.exe read build-b01\dbf3-result-scratch\dbf3-results.sdbres
if errorlevel 1 exit /b 1
build-b01\sdb_dbf3_model_tests.exe --read build-b01-asan\dbf3-model-scratch
if errorlevel 1 exit /b 1
build-b01\sdb_dbf3_state_tests.exe --read "%DBF3_ROOT%/tests/fixtures/abaqus/forced_tet.inp" build-b01-asan\dbf3-state-scratch
if errorlevel 1 exit /b 1
build-b01\sdb_dbf3_result_tests.exe read build-b01-asan\dbf3-result-scratch\dbf3-results.sdbres
exit /b %errorlevel%
