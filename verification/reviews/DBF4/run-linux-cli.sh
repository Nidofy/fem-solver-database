#!/bin/sh
set -eu
mkdir -p build-dbf4-tsan verification/reviews/DBF4
exec > verification/reviews/DBF4/cli-linux-commands.log 2>&1
set -x
git rev-parse HEAD
g++ -std=c++20 -O0 -pthread -Iinclude examples/sdb_run.cpp -o build-dbf4-tsan/sdb_run
python3 tests/dbf4_cli.py build-dbf4-tsan/sdb_run tests/fixtures/abaqus/forced_tet.inp build-dbf4-tsan/cli-scratch > verification/reviews/DBF4/cli-linux.log 2>&1
