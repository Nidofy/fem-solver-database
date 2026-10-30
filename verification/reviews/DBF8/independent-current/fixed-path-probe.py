from pathlib import Path
import sys,json,base64,hashlib
sys.path.insert(0,str(Path(__file__).parent/'fixed-snapshot/tools'))
from inp_document import source_name,DeckReader,save,load,export
checks=0
for name in ['dir/source.inp:hidden','NUL','NUL.txt','a/COM1.dat','a/LPT9','a/../b','a./b','x ','/abs','a\\b']:
    try:source_name(name)
    except ValueError:checks+=1
    else:raise AssertionError(name)
root=Path(__file__).parent/'safe-roundtrip';root.mkdir(exist_ok=True)
raw=b'** comment\r\n*UNKNOWN_CARD, CUSTOM=VALUE\r\n1,2,3\r\n'
(root/'main.inp').write_bytes(raw)
d=DeckReader(root).read(root/'main.inp');save(d,root/'deck.json');restored=load(root/'deck.json');export(restored,root/'out')
assert (root/'out/main.inp').read_bytes()==raw;checks+=1
try:save(d,root/'deck.json')
except FileExistsError:checks+=1
else:raise AssertionError('overwrite')
print('path/roundtrip checks=',checks,'PASS')
