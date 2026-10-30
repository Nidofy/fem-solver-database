#!/bin/bash
set -euo pipefail
cd /mnt/d/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2
cmake -S . -B build-dbf8-linux -DCMAKE_BUILD_TYPE=Release -DSDB_INDEX_BITS=64 -DSDB_OPENMP=ON -DSDB_ZSTD=OFF -DSDB_C_API=ON > verification/reviews/DBF8/logs/linux-final-configure.txt 2>&1
cmake --build build-dbf8-linux --target sdb_dbf8_contract_tests sdb_dbf8_malloc_worker sdb_dbf6_contract_tests sdb_dbf7_contract_tests sdb_dbf7_query_tests sdb_typed_results_tests -j2 > verification/reviews/DBF8/logs/linux-final-build.txt 2>&1
ctest --test-dir build-dbf8-linux -R 'dbf[678]_(contracts|process_reader|queries|tools|inp_document|process_envelope|wire_oracle)|typed_results_contracts' --output-on-failure > verification/reviews/DBF8/logs/linux-final-tests.txt 2>&1
python3 tests/dbf8_memory_tests.py build-dbf8-linux/sdb_dbf8_malloc_worker > verification/reviews/DBF8/logs/linux-process-envelope.txt 2>&1
