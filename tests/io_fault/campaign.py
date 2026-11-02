"""Linux isolated ENOSPC/EIO regression; no mount, privilege or disk fill required."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

worker, preload = map(os.path.abspath, sys.argv[1:])
with tempfile.TemporaryDirectory() as scratch:
    dst = Path(scratch)/"state"
    subprocess.run([worker,dst,"A"],check=True)
    for mode in ("full","io"):
        env = dict(os.environ, LD_PRELOAD=preload, SDB_FAULT_MODE=mode,
                   SDB_FAULT_PREFIX=str(dst)+".tmp.")
        result = subprocess.run([worker,dst,"B"],env=env)
        if result.returncode!=17 or dst.read_bytes()!=b"A"*32768 or list(dst.parent.glob("state.tmp.*")):
            raise SystemExit(f"{mode}: expected unmodified previous publication, rc={result.returncode}")
    subprocess.run([worker,dst,"C"],check=True)
    assert dst.read_bytes()==b"C"*32768
    print("ENOSPC/write, EIO/fsync, previous file, cleanup and retry PASS")
