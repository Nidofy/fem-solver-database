#!/bin/sh
# Run from the standalone solver_database_v2 root. Uses installed GCC/TSan only.
set -eu
mkdir -p build-dbf4-tsan verification/reviews/DBF4
log=verification/reviews/DBF4
{
    git rev-parse HEAD
    git diff --stat HEAD -- include src
    g++ --version
    uname -a
    stat -f -c 'workspace filesystem=%T' .
    printf '%s\n' 'Commands: run-linux-focused.sh (set -x below)'
} > "$log/linux-final-environment.log"
exec > "$log/linux-final-commands.log" 2>&1
set -x
g++ -std=c++20 -O1 -g -pthread -fsanitize=thread -fno-omit-frame-pointer -Iinclude tests/async_results_tests.cpp -o build-dbf4-tsan/async-results
setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 build-dbf4-tsan/async-results tests/fixtures/abaqus/forced_tet.inp build-dbf4-tsan/async-scratch > "$log/tsan-run.log" 2>&1
g++ -std=c++20 -O1 -g -pthread -fsanitize=thread -fno-omit-frame-pointer -Iinclude tests/dbf4_result_durability_tests.cpp -o build-dbf4-tsan/result-durability
setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 build-dbf4-tsan/result-durability build-dbf4-tsan/durability-scratch > "$log/tsan-durability-run.log" 2>&1
g++ -std=c++20 -O0 -pthread -Iinclude tests/dbf4_atomic.cpp -o build-dbf4-tsan/atomic
TMPDIR="$PWD/build-dbf4-tsan" build-dbf4-tsan/atomic > "$log/atomic-linux.log" 2>&1
g++ -std=c++20 -O0 -pthread -Iinclude tests/dbf4_link_tests.cpp -o build-dbf4-tsan/link
build-dbf4-tsan/link tests/fixtures/abaqus/forced_tet.inp build-dbf4-tsan/link-scratch > "$log/link-linux.log" 2>&1
g++ -std=c++20 -O0 -pthread -Iinclude tests/dbf4_fault_driver.cpp -o build-dbf4-tsan/fault-driver
python3 tests/dbf4_fault_campaign.py build-dbf4-tsan/fault-driver tests/fixtures/abaqus/forced_tet.inp build-dbf4-tsan/fault-scratch > "$log/fault-campaign-linux.log" 2>&1
printf '%s\n' 'Linux focused + TSan complete PASS'
