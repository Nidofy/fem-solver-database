"""Exercise the public CLI completion policy without relying on fault hooks."""
from pathlib import Path
import os
import subprocess
import sys

exe, fixture, scratch = map(lambda x: Path(x).resolve(), sys.argv[1:4])
scratch.mkdir(parents=True, exist_ok=True)
checks = 0

def invoke(name, *args, ok=True):
    global checks
    output = scratch / (name + ".sdbres")
    restart = scratch / (name + ".sdbrst")
    for file in (output, restart, Path(str(restart) + ".result-link")):
        file.unlink(missing_ok=True)
    command = [str(exe), "--input", str(fixture), "--output", str(output),
               "--stop-after", "2", "--checkpoint", str(restart), *args]
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert (result.returncode == 0) == ok, (command, result.stdout, result.stderr)
    if ok:
        assert output.exists() and restart.exists() and Path(str(restart) + ".result-link").exists()
    else:
        assert not output.exists() and not restart.exists()
    checks += 1
    return result.stdout

for format_args in ([], ["--indexed"], ["--typed-indexed"]):
    name = "native" if not format_args else format_args[0][2:]
    assert "durability=file" in invoke(name + "-default", *format_args, "--async-mib", "1")
    assert "durability=stream" in invoke(name + "-stream", *format_args, "--durability", "stream")
    invoke(name + "-publication", *format_args, "--durability", "publication", ok=os.name != "nt")
invoke("invalid", "--durability", "imaginary", ok=False)
print(f"DBF4 CLI: {checks} checks passed")
