#!/bin/bash
set -euo pipefail
cd /mnt/d/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2
mkdir -p build-dbf8-linux verification/reviews/DBF8/logs
cmake -S . -B build-dbf8-linux -DCMAKE_BUILD_TYPE=Release -DSDB_INDEX_BITS=64 -DSDB_OPENMP=ON -DSDB_ZSTD=OFF -DSDB_C_API=ON > verification/reviews/DBF8/logs/linux-configure.txt 2>&1
cmake --build build-dbf8-linux --target sdb_dbf8_contract_tests sdb_dbf8_malloc_worker sdb_dbf6_contract_tests sdb_dbf7_contract_tests sdb_dbf7_query_tests sdb_typed_results_tests -j2 > verification/reviews/DBF8/logs/linux-build.txt 2>&1
ctest --test-dir build-dbf8-linux -R 'dbf8_contracts|dbf8_inp_document|dbf6_contracts|dbf6_wire|dbf7_contracts|dbf7_query|dbf7_tools|typed_results_contracts' --output-on-failure > verification/reviews/DBF8/logs/linux-tests.txt 2>&1
python3 tools/run_limited.py --limit-mib 64 --report verification/reviews/DBF8/logs/linux-memory.json --log verification/reviews/DBF8/logs/linux-memory-worker.txt -- build-dbf8-linux/sdb_dbf8_malloc_worker
