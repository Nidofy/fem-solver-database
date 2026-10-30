import sys,tempfile,json,base64,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'snapshot/tools'))
from inp_document import load,export
root=Path(__file__).parent/'path-probe';root.mkdir(exist_ok=True)
raw=b'node-data\n';name='dir/source.inp:hidden'
doc={'version':1,'entry':name,'files':{name:{'base64':base64.b64encode(raw).decode(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}}}
p=root/'ads.json';p.write_text(json.dumps(doc))
try:
    d=load(p);export(d,root/'exported')
    print('ADS accepted:',list((root/'exported/dir').iterdir()))
except Exception as e: print(type(e).__name__,str(e))
