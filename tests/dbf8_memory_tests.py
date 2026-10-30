"""OS envelope exercises malloc (outside C++ interception), timeout and integer bounds."""
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
launcher=Path(__file__).resolve().parents[1]/'tools'/'run_limited.py'
worker=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    def launch(name,command,timeout='20',limit='64'):
        report=root/(name+'.json');log=root/(name+'.log')
        p=subprocess.run([sys.executable,str(launcher),'--limit-mib',limit,'--timeout',timeout,
            '--report',str(report),'--log',str(log),'--',*command],capture_output=True,text=True,timeout=40)
        return p,report,log
    p,report,log=launch('malloc',[str(worker)])
    assert p.returncode==0,(p.stdout,p.stderr,log.read_text() if log.exists() else '')
    data=json.loads(report.read_text());assert data['limit_bytes']==64*1024*1024 and not data['timed_out']
    assert 'denied=1' in log.read_text()
    print(json.dumps(data));print(log.read_text().strip())
    # Windows children inherit the same aggregate Job budget; Linux RLIMIT_AS
    # intentionally remains a separate per-process address-space allowance.
    if os.name=='nt':
        parent='import subprocess;payload=bytearray(20*1024*1024);subprocess.run(['+repr(str(worker))+'],check=True)'
        p,report,log=launch('tree',[sys.executable,'-c',parent])
        assert p.returncode==0,(p.stdout,p.stderr,log.read_text() if log.exists() else '')
        admitted=int(re.search(r'admitted_mib=(\d+)',log.read_text()).group(1))
        assert 0<admitted<50,admitted
        print('aggregate Job parent 20MiB + child admitted_mib='+str(admitted))
    p,report,log=launch('timeout',[sys.executable,'-c','import time;time.sleep(10)'],timeout='0.1')
    assert p.returncode==1 and json.loads(report.read_text())['timed_out'],(p.stdout,p.stderr)
    p,report,log=launch('overflow',[str(worker)],limit=str(1<<64))
    assert p.returncode==2 and not report.exists() and not log.exists()
print('OS envelope malloc refusal / timeout / overflow guards PASS')
