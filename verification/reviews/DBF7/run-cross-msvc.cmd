@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
build-dbf7-asan\sdb_dbf7_contract_tests.exe read build-dbf6\dbf7-scratch
exit /b %errorlevel%
