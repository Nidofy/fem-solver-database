#!/bin/sh
# Run from the independent solver_database_v2 root. No system installs/downloads.
# Installed SDK/runtime inputs may be overridden with ZSTD_INCLUDE/ZSTD_LIBRARY.
# Each command keeps its first raw output and exit code; failures are never retried.
set -u
root=$(pwd)
build="$root/build-dbf5-linux-final"
logs="$root/verification/reviews/DBF5"
sdk=${ZSTD_INCLUDE:-"$root/build-deps/zstd-1.5.5/include"}
library=${ZSTD_LIBRARY:-/usr/lib/x86_64-linux-gnu/libzstd.so.1.5.5}
fixture=tests/fixtures/abaqus/forced_tet.inp
mkdir -p "$build" "$logs"
failures=0
reuse=${SDB_REUSE_UNAFFECTED:-0}
{
    git rev-parse HEAD
    git status --short --untracked-files=no
    git diff --stat HEAD -- include src tests examples CMakeLists.txt cmake
    g++ --version
    cmake --version
    uname -a
    stat -f -c 'workspace filesystem=%T' .
    findmnt -T . || true
    printf 'SDK=%s\nLIBRARY=%s\n' "$sdk" "$library"
    sha256sum "$sdk/zstd.h" "$library"
    python3 -c 'import ctypes,sys; z=ctypes.CDLL(sys.argv[1]); z.ZSTD_versionString.restype=ctypes.c_char_p; print("actual zstd runtime",z.ZSTD_versionString().decode())' "$library"
    printf '%s\n' 'TSan disables ASLR for each test process with setarch -R; system ASLR configuration is unchanged.'
    printf '%s\n' 'Preinstalled release zstd/OpenMP runtimes are not sanitizer-instrumented. Process crashes do not simulate physical power loss.'
} > "$logs/linux-final-environment.log" 2>&1
exec > "$logs/linux-final-commands.log" 2>&1
set -x
run() {
    name=$1
    shift
    "$@" > "$logs/linux-$name.log" 2>&1
    result=$?
    printf '%s exit=%s\n' "$name" "$result" | tee -a "$logs/linux-final-status.log"
    if [ "$result" -ne 0 ]; then failures=$((failures+1)); fi
    return "$result"
}
: > "$logs/linux-final-status.log"

# An explicitly requested resumed run may reuse only binaries whose complete
# non-system dependency closure has no changed file since the recorded baseline.
# Earlier logs remain in preliminary-493a6ddb6aed; affected consumers rebuild.
if [ "$reuse" = 1 ]; then
    run reuse-input-proof python3 - "$sdk" "$build" <<'EOF'
import hashlib,pathlib,shlex,subprocess,sys
baseline='493a6ddb6aed3a6837be3e3cfa8382a8da4b14d6'
root=pathlib.Path.cwd()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
changed=set(subprocess.check_output(['git','diff','--name-only',baseline,head],text=True).splitlines())
changed.update(subprocess.check_output(['git','diff','--name-only','HEAD'],text=True).splitlines())
print('Reusing selected baseline binaries',baseline,'with dependency proof against',head)
for test in ('dbf5_codec_tests','dbf5_migration_tests'):
    output=subprocess.check_output(['g++','-std=c++20','-MM','-DSDB_WITH_ZSTD=1','-Iinclude','-I'+sys.argv[1],'tests/'+test+'.cpp'],text=True)
    dependencies=shlex.split(output.split(':',1)[1].replace('\\\n',' '))
    normalized={str(pathlib.Path(p).resolve().relative_to(root)) for p in dependencies}
    assert not changed.intersection(normalized),(test,changed.intersection(normalized))
    for dependency in sorted(normalized):
        print(test,hashlib.sha256((root/dependency).read_bytes()).hexdigest(),dependency)
    binary=pathlib.Path(sys.argv[2])/('asan-'+test)
    print('BINARY',hashlib.sha256(binary.read_bytes()).hexdigest(),str(binary))
EOF
    proof_status=$?
    if [ "$proof_status" -ne 0 ]; then exit "$proof_status"; fi
fi

# TSan: actual asynchronous worker tests; no OpenMP runtime mixed into TSan.
tsan_tests='async_results_tests dbf5_compressed_tests'
if [ "$reuse" = 1 ]; then tsan_tests=dbf5_compressed_tests; fi
for test in $tsan_tests; do
    if run "tsan-$test-build" g++ -std=c++20 -O1 -g -pthread -fsanitize=thread -fno-omit-frame-pointer \
        -DSDB_WITH_ZSTD=1 -Iinclude "-I$sdk" "tests/$test.cpp" "$library" -o "$build/tsan-$test"; then
        if [ "$test" = async_results_tests ]; then
            run "tsan-$test-run" setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 "$build/tsan-$test" "$fixture" "$build/tsan-async-scratch" || true
        else
            run "tsan-$test-run" setarch x86_64 -R env TSAN_OPTIONS=halt_on_error=1 "$build/tsan-$test" "$build/tsan-compressed-scratch" || true
        fi
    fi
done

# ASan+UBSan: raw first runs are retained, including any loader/startup failure.
for test in dbf5_codec_tests dbf5_compressed_tests dbf5_consumer_tests dbf5_migration_tests; do
    ready=0
    case "$reuse:$test" in
        1:dbf5_codec_tests|1:dbf5_migration_tests) ready=1 ;;
        *) if run "asan-$test-build" g++ -std=c++20 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
            -DSDB_WITH_ZSTD=1 -Iinclude "-I$sdk" "tests/$test.cpp" "$library" -o "$build/asan-$test"; then ready=1; fi ;;
    esac
    if [ "$ready" = 1 ]; then
        case "$test" in
            dbf5_codec_tests) run "asan-$test-run" "$build/asan-$test" || true ;;
            dbf5_compressed_tests) run "asan-$test-run" "$build/asan-$test" "$build/asan-compressed-scratch" || true ;;
            dbf5_consumer_tests) run "asan-$test-run" "$build/asan-$test" "$fixture" "$build/asan-consumer-scratch" || true ;;
            dbf5_migration_tests) run "asan-$test-run" "$build/asan-$test" tests/fixtures/dbf5/migration-golden.txt "$build/asan-migration-scratch" || true ;;
        esac
    fi
done

# CMake installation remains entirely inside this workspace.
if run cmake-on-configure cmake -S . -B "$build/on" -DSDB_ZSTD=ON -DSDB_OPENMP=ON \
    "-DSDB_ZSTD_INCLUDE_DIR=$sdk" "-DSDB_ZSTD_LIBRARY=$library" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O1; then
    if run cmake-on-build cmake --build "$build/on" -j 2 --target sdb_run sdb_dbf5_codec_tests \
        sdb_dbf5_compressed_tests sdb_dbf5_consumer_tests sdb_dbf5_migration_tests sdb_async_results_tests \
        sdb_indexed_results_tests sdb_typed_results_tests sdb_dbf5_fault_driver sdb_dbf5_compression; then
        run cmake-on-ctest env OMP_NUM_THREADS=4 ctest --test-dir "$build/on" --output-on-failure \
            -R '^(dbf5_codec|dbf5_compressed|dbf5_consumer|dbf5_migration|dbf5_cli|async_results_contracts|indexed_results_contracts|typed_results_contracts)$' || true
        run cmake-on-install cmake --install "$build/on" --prefix "$build/install-on" || true
    fi
fi
if run cmake-off-configure cmake -S . -B "$build/off" -DSDB_ZSTD=OFF -DSDB_OPENMP=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O1; then
    if run cmake-off-build cmake --build "$build/off" -j 2 --target sdb_run sdb_dbf5_codec_tests sdb_dbf5_compressed_tests sdb_dbf5_fault_driver; then
        run cmake-off-ctest ctest --test-dir "$build/off" --output-on-failure -R '^(dbf5_codec|dbf5_compressed|dbf5_cli)$' || true
        run cmake-off-install cmake --install "$build/off" --prefix "$build/install-off" || true
    fi
fi

mkdir -p "$build/consumer-source"
cat > "$build/consumer-source/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.24)
project(DBF5InstalledConsumer LANGUAGES CXX)
find_package(SolverDatabaseV2 CONFIG REQUIRED)
add_executable(smoke main.cpp)
target_link_libraries(smoke PRIVATE sdb::sdb_lifecycle)
EOF
cat > "$build/consumer-source/main.cpp" <<'EOF'
#include <sdb/consumer_views.hpp>
#include <sdb/io/compression.hpp>
#include <iostream>
int main() {
    using namespace sdb::compression;
    std::vector<std::uint8_t> input(1024,42);
    Options options;
    if (available(Codec::Zstd)) options.codec=Codec::Zstd;
    auto block=encode(input,options);
    if (decode(block.codec,block.payload,block.raw_bytes,block.raw_crc)!=input) return 1;
    std::cout<<"installed package consumer PASS zstd="<<available(Codec::Zstd)<<'\n';
}
EOF
for feature in on off; do
    if run "package-$feature-configure" cmake -S "$build/consumer-source" -B "$build/consumer-$feature" \
        "-DCMAKE_PREFIX_PATH=$build/install-$feature" "-DSDB_ZSTD_INCLUDE_DIR=$sdk" "-DSDB_ZSTD_LIBRARY=$library"; then
        if run "package-$feature-build" cmake --build "$build/consumer-$feature" -j 2; then
            run "package-$feature-run" "$build/consumer-$feature/smoke" || true
        fi
    fi
done

# Fresh processes and independent wire oracle use binaries built from this run.
run compressed-fault-campaign python3 tests/dbf5_fault_campaign.py "$build/on/sdb_dbf5_fault_driver" \
    "$fixture" "$build/fault-scratch" "$build/off/sdb_dbf5_fault_driver" || true
if run compression-benchmark "$build/on/sdb_dbf5_compression" "$build/compression-benchmark" 10000; then
    run compressed-wire-oracle python3 tests/dbf5_wire_oracle.py "$build/compression-benchmark" "$library" || true
fi
{
    git rev-parse HEAD
    git diff --stat HEAD -- include src tests examples CMakeLists.txt cmake
} > "$logs/linux-final-source-end.log" 2>&1
printf 'Linux focused completion: failures=%s (see every raw log and exit status)\n' "$failures" | tee -a "$logs/linux-final-status.log"
test "$failures" -eq 0
