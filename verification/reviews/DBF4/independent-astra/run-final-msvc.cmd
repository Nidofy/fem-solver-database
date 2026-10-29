@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul
where cl
cl /Bv /nologo /std:c++20 /EHsc /Iinclude verification\reviews\DBF4\independent-astra\final_diagnostic_probe.cpp /Fo:verification\reviews\DBF4\independent-astra\scratch\final_diagnostic_probe.obj /Fe:verification\reviews\DBF4\independent-astra\scratch\final_diagnostic_probe.exe
if errorlevel 1 exit /b 1
verification\reviews\DBF4\independent-astra\scratch\final_diagnostic_probe.exe
cl /nologo /std:c++20 /EHsc /bigobj /Iinclude verification\reviews\DBF4\independent-astra\allocation_probe.cpp /Fo:verification\reviews\DBF4\independent-astra\scratch\allocation_probe_msvc.obj /Fe:verification\reviews\DBF4\independent-astra\scratch\allocation_probe_msvc.exe
if errorlevel 1 exit /b 1
verification\reviews\DBF4\independent-astra\scratch\allocation_probe_msvc.exe tests\fixtures\abaqus\forced_tet.inp verification\reviews\DBF4\independent-astra\scratch\allocation-msvc

cl /nologo /std:c++20 /EHsc verification\reviews\DBF4\independent-astra\rethrow_lifetime_probe.cpp /Fo:verification\reviews\DBF4\independent-astra\scratch\rethrow_lifetime_probe.obj /Fe:verification\reviews\DBF4\independent-astra\scratch\rethrow_lifetime_probe.exe
if errorlevel 1 exit /b 1
verification\reviews\DBF4\independent-astra\scratch\rethrow_lifetime_probe.exe
