"""Fresh-process abrupt-exit evidence; this does NOT simulate storage power loss."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

exe, fixture, scratch = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
checks = 0

def invoke(*args, expected=0):
    global checks
    p = subprocess.run([str(exe), *map(str,args)], capture_output=True, text=True, timeout=60)
    assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    checks += 1
    return p.stdout

def fresh_copy(base, target):
    if target.exists():
        # All deletion is restricted to this campaign's explicitly named scratch children.
        assert target.parent == scratch and target != scratch
        shutil.rmtree(target)
    shutil.copytree(base,target)

def unlock_dead_child(folder):
    # invoke() already reaped the child. Never steal a live writer's cooperative lease.
    for name in ("result.sdbres", "state.sdbrst", "state.sdbrst.result-link"):
        lease=folder/(name+".writer-lock")
        if lease.exists(): lease.rmdir() # empty exact known directories only

common = [("result","write"),("result","sync-before"),("result","sync-after"),
          ("restart","temp"),("restart","sync-before"),("restart","sync-after"),
          ("restart","replace-before"),("restart","replace-after"),
          ("link","link-before"),("link","temp"),("link","sync-before"),("link","sync-after"),
          ("link","replace-before"),("link","replace-after"),("link","link-after")]
if os.name != "nt":
    common += [(t,p) for t in ("restart","link") for p in ("parent-before","parent-after")]

for fmt in ("native","indexed","typed"):
    base=scratch/(fmt+"-baseline")
    if base.exists():
        assert base.parent==scratch
        shutil.rmtree(base)
    invoke("init",base,fixture,fmt)
    baseline=json.loads(invoke("inspect",base))
    assert baseline["linked"] and baseline["frames"]==1 and baseline["step"]==0
    cases=common+([] if fmt=="native" else [("result","commit")])
    for target,point in cases:
        folder=scratch/(fmt+"-"+target+"-"+point)
        fresh_copy(base,folder)
        invoke("mutate",folder,target,point,expected=77)
        state=json.loads(invoke("inspect",folder))
        assert state["frames"] in (1,2) and state["step"] in (0,100), (fmt,target,point,state)
        if target=="link" and point in ("replace-after","parent-before","parent-after","link-after"):
            assert state["linked"] and state["frames"]==2 and state["step"]==100, state
        elif target != "result":
            assert not state["linked"], (fmt,target,point,state)
        # Reader validation above precedes lease removal. Append is attempted only after child death.
        before=(folder/"result.sdbres").read_bytes()
        unlock_dead_child(folder)
        invoke("append-check",folder,"yes",expected=0 if state["linked"] else 1)
        after=(folder/"result.sdbres").read_bytes()
        assert after==(before[:state["valid_end"]] if state["linked"] else before)
        print(fmt,target,point,json.dumps(state,sort_keys=True))

    partial=scratch/(fmt+"-partial")
    fresh_copy(base,partial)
    original=(partial/"result.sdbres").read_bytes()
    (partial/"result.sdbres").write_bytes(original+b"partial-record")
    invoke("append-check",partial,"no",expected=1)
    assert (partial/"result.sdbres").read_bytes()==original+b"partial-record"
    invoke("append-check",partial,"yes")
    assert (partial/"result.sdbres").read_bytes()==original
    assert json.loads(invoke("inspect",partial))["linked"]

    corrupt=scratch/(fmt+"-corrupt")
    fresh_copy(base,corrupt)
    altered=bytearray(original)
    altered[-1]^=0x40
    (corrupt/"result.sdbres").write_bytes(altered)
    invoke("append-check",corrupt,"yes",expected=1)
    assert (corrupt/"result.sdbres").read_bytes()==altered

print(f"DBF4 abrupt process exit / fresh reader campaign PASS: {checks} child checks; no power-loss claim")
