#!/bin/bash
set -euo pipefail
cd /mnt/d/Codes/Solver/ExplicitSolver_track_l_c0a_l0/solver_database_v2
g++ -std=c++20 -O1 -g -pthread -DSDB_INDEX_BITS=64 -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer tests/dbf8_contract_tests.cpp src/process_memory.cpp -o build-dbf8-linux/dbf8-asan > verification/reviews/DBF8/logs/linux-asan-build.txt 2>&1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 build-dbf8-linux/dbf8-asan > verification/reviews/DBF8/logs/linux-asan.txt 2>&1
g++ -std=c++20 -O1 -g -pthread -DSDB_INDEX_BITS=64 -Iinclude -fsanitize=thread -fno-omit-frame-pointer -fno-pie -no-pie tests/dbf8_contract_tests.cpp src/process_memory.cpp -o build-dbf8-linux/dbf8-tsan > verification/reviews/DBF8/logs/linux-tsan-build.txt 2>&1
setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 build-dbf8-linux/dbf8-tsan > verification/reviews/DBF8/logs/linux-tsan-noaslr.txt 2>&1
