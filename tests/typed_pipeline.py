"""Real CPU run -> typed container -> linked restart; separate CLI typed reader."""
from pathlib import Path
import subprocess
import sys
import uuid

exe, fixture, integration, scratch = map(Path, sys.argv[1:])
directory = scratch / str(uuid.uuid4())
directory.mkdir(parents=True)
def run(*args):
    return subprocess.check_output([str(exe), *map(str, args)], text=True)
for precision in ("float", "double"):
    result = directory / (precision + ".sdbres")
    restart = directory / (precision + ".sdbrst")
    run("--input", fixture, "--precision", precision, "--typed-indexed", "--output", result,
        "--stop-after", 100, "--checkpoint", restart)
    run("--input", fixture, "--precision", precision, "--typed-indexed", "--output", result,
        "--append", "--restart", restart, "--stop-after", 200)
    reference = directory / (precision + "-reference.sdbres")
    run("--input", fixture, "--precision", precision, "--indexed", "--output", reference,
        "--stop-after", 200)
    assert run("--read", result, "--field", "U", "--entity", 5) == run("--read", reference, "--field", "U", "--entity", 5)
    exact = run("--read", integration / (precision + ".sdbres"), "--typed", "--field", "FLAGS")
    assert "18446744073709551615" in exact
    assert ",0,0\n" in exact  # invalid canonical zero is reported separately
    print(precision + ": typed-container linked restart equals uninterrupted indexed2; typed CLI exact uint64/mask PASS")
