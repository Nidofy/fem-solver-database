"""Compile and run fixed-seed coverage-guided smoke tests on Linux."""
from pathlib import Path
import os
import subprocess
import sys

out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
for i in (1,2,3):
    exe = out/f"f{i}"
    corpus = out/f"c{i}"
    corpus.mkdir(exist_ok=True)
    subprocess.run(["clang++","-std=c++20","-O1","-g0","-fsanitize=fuzzer,address,undefined",
                    "-fno-sanitize-recover=all",f"-DSDB_FUZZ_TARGET={i}","-Iinclude",
                    "tests/fuzz/io_fuzzer.cpp","-o",exe],check=True)
    with (out/f"log{i}").open("wb") as log:
        subprocess.run([exe,corpus,"-runs=5000","-max_len=8192","-seed=42",f"-artifact_prefix={out}/"],
                       env=dict(os.environ,SDB_FUZZ_SEEDS=str(corpus)),stderr=log,check=True)
    print((out/f"log{i}").read_text().splitlines()[-1],flush=True)
