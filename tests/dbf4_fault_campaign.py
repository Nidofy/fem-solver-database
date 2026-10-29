"""Fresh-process abrupt-exit evidence; this does NOT simulate storage power loss."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import zlib

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

def assert_link_bytes(folder, expected_step, result_prefix):
    """Independent schema-1 decoder: do not use C++ verify to choose expected outcomes."""
    wire=(folder/"state.sdbrst.result-link").read_bytes()
    assert struct.unpack_from("<QI",wire)==(0x314B4E494C424453,1)
    kind,size,crc=struct.unpack_from("<IQI",wire,12)
    body=wire[28:]
    assert kind==0 and size==60 and len(body)==60 and zlib.crc32(body)==crc
    schema,definition,configuration,step,result_size,result_hash,restart_size,restart_hash=struct.unpack("<I7Q",body)
    assert schema==1 and definition and configuration and step==expected_step
    def fnv(data):
        value=14695981039346656037
        for byte in data: value=((value^byte)*1099511628211)&0xFFFFFFFFFFFFFFFF
        return value
    restart=(folder/"state.sdbrst").read_bytes()
    assert result_size==len(result_prefix) and result_hash==fnv(result_prefix)
    assert restart_size==len(restart) and restart_hash==fnv(restart)

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
    original=(base/"result.sdbres").read_bytes()
    old_restart=(base/"state.sdbrst").read_bytes()
    old_link=(base/"state.sdbrst.result-link").read_bytes()
    assert_link_bytes(base,0,original)
    cases=common+([] if fmt=="native" else [("result","commit")])
    for target,point in cases:
        folder=scratch/(fmt+"-"+target+"-"+point)
        fresh_copy(base,folder)
        invoke("mutate",folder,target,point,expected=77)
        state=json.loads(invoke("inspect",folder))
        replaced=("replace-after","parent-before","parent-after","link-after")
        expected_linked=(target=="result" and point in ("write","commit")) or (target=="link" and point in replaced)
        expected_frames=1 if target=="result" and point in ("write","commit") else 2
        expected_step=100 if target=="link" or (target=="restart" and point in replaced) else 0
        assert (state["linked"],state["frames"],state["step"]) == (expected_linked,expected_frames,expected_step), (fmt,target,point,state)
        before=(folder/"result.sdbres").read_bytes()
        assert before[:len(original)]==original
        if expected_frames==1:
            assert state["valid_end"]==len(original)
            if point=="write": assert before==original and not state["tail"]
        else:
            assert state["valid_end"]==len(before)>len(original) and not state["tail"]
        assert ((folder/"state.sdbrst").read_bytes()==old_restart)==(expected_step==0)
        assert ((folder/"state.sdbrst.result-link").read_bytes()==old_link)==(not(target=="link" and point in replaced))
        if expected_linked: assert_link_bytes(folder,expected_step,before[:state["valid_end"]])
        # Reader validation above precedes lease removal. Append is attempted only after child death.
        assert (folder/"result.sdbres.writer-lock").is_dir()
        invoke("append-check",folder,"yes",expected=1)
        assert (folder/"result.sdbres").read_bytes()==before
        unlock_dead_child(folder)
        invoke("append-check",folder,"yes",expected=0 if expected_linked else 1)
        after=(folder/"result.sdbres").read_bytes()
        assert after==(before[:state["valid_end"]] if expected_linked else before)
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
