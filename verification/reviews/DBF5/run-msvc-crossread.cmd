@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
git rev-parse HEAD
build-dbf5-asan\sdb_run.exe --read build-dbf5-linux-final\compression-benchmark\repeated-compressed.sdbres --verify
if errorlevel 1 exit /b 1
build-dbf5-asan\sdb_run.exe --read build-dbf5-linux-final\compression-benchmark\random-compressed.sdbres --verify
exit /b %errorlevel%
