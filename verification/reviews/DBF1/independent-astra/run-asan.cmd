@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /Od /Zi /fsanitize=address /I include verification\reviews\DBF1\independent-astra\independent.cpp /Fo:build-dbf1-review\independent-asan.obj /Fd:build-dbf1-review\independent-asan-compile.pdb /Fe:build-dbf1-review\independent-asan.exe /link /INCREMENTAL:NO /PDB:build-dbf1-review\independent-asan.pdb
if errorlevel 1 exit /b 1
build-dbf1-review\independent-asan.exe tests\fixtures\abaqus\mixed_hex_tet.inp build-dbf1-review\independent-asan-scratch
exit /b %errorlevel%
