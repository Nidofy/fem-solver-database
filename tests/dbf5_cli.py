"""Public compressed lifecycle, full verification and exact result conversion."""
from pathlib import Path
import subprocess
import sys
exe, fixture, scratch = (Path(x).resolve() for x in sys.argv[1:4])
enabled = sys.argv[4] == "1"
scratch.mkdir(parents=True, exist_ok=True)
checks = 0
def invoke(*args, ok=True):
    global checks
    p = subprocess.run([str(exe), *map(str,args)], capture_output=True, text=True, timeout=90)
    assert (p.returncode == 0) == ok, (args,p.returncode,p.stdout,p.stderr)
    checks += 1
    return p.stdout
def vacant(name):
    p=scratch/name
    p.unlink(missing_ok=True)
    return p

output=vacant("run.sdbres")
restart=vacant("run.sdbrst")
Path(str(restart)+".result-link").unlink(missing_ok=True)
if not enabled:
    invoke("--input",fixture,"--output",output,"--compression","zstd","--stop-after","2",ok=False)
    assert not output.exists()
    invoke("--input",fixture,"--output",output,"--typed-indexed","--stop-after","2")
    assert "verified_frames=" in invoke("--read",output,"--verify")
else:
    invoke("--input",fixture,"--output",output,"--compression","zstd","--compression-fields","U:1,S:none",
           "--async-mib","64","--stop-after","100","--checkpoint",restart)
    assert "format=4" in invoke("--read",output,"--verify")
    # No explicit format/policy: append must adopt persisted options and bind typed metadata.
    invoke("--input",fixture,"--output",output,"--restart",restart,"--append","--stop-after","200","--checkpoint",restart)
    original=output.read_bytes()
    invoke("--input",fixture,"--output",output,"--restart",restart,"--append","--stop-after","200",
           "--compression","zstd","--compression-level","9",ok=False)
    assert output.read_bytes()==original
    plain=vacant("plain.sdbres")
    restored=vacant("recompressed.sdbres")
    invoke("--read",output,"--convert-result",plain,"--compression","none")
    assert "format=3" in invoke("--read",plain,"--verify")
    invoke("--read",plain,"--convert-result",restored,"--compression","zstd")
    assert "format=4" in invoke("--read",restored,"--verify")
    for field in ("U","V","A","RF","S","E"):
        assert invoke("--read",output,"--field",field)==invoke("--read",plain,"--field",field)
        assert invoke("--read",output,"--field",field)==invoke("--read",restored,"--field",field)
    # Occupied output is never overwritten, and a malformed codec policy creates no output.
    invoke("--read",output,"--convert-result",plain,"--compression","zstd",ok=False)
bad=vacant("bad.sdbres")
invoke("--input",fixture,"--output",bad,"--compression","unknown",ok=False)
assert not bad.exists()
print(f"DBF5 CLI: {checks} checks PASS zstd={int(enabled)}")
