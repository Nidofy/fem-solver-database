"""Index64 must reject authenticated Index32 native files before publishing output."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe,fixtures=map(Path,sys.argv[1:])
manifest=json.loads((fixtures/'legacy-provenance.json').read_text(encoding='utf-8'))
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);name='legacy.sdbmodel'
    raw=bytes.fromhex((fixtures/(name+'.hex')).read_text())
    assert hashlib.sha256(raw).hexdigest()==manifest['files'][name]['sha256']
    (root/name).write_bytes(raw)
    out=root/'result.sdbres'
    p=subprocess.run([str(exe),'--model',str(root/name),'--output',str(out)],capture_output=True,text=True)
    assert p.returncode!=0 and 'native magic/version mismatch' in p.stderr,(p.returncode,p.stderr)
    assert not out.exists()
print('authenticated historical Index32 rejected by Index64 before output PASS')
