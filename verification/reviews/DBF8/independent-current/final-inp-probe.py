from pathlib import Path
import sys,copy
sys.path.insert(0,str(Path(__file__).parent/'final-snapshot/tools'))
from inp_document import DeckReader,export
root=Path(__file__).parent/'final-cases';root.mkdir(exist_ok=True)
(root/'a.dat').write_text('1,0,0,0\n');(root/'b.dat').write_text('2,0,0,0\n')
(root/'duplicate.inp').write_text('*NODE, INPUT=a.dat, INPUT=b.dat\n')
doc=DeckReader(root).read(root/'duplicate.inp')
print('duplicate INPUT captured files=',list(doc['files']))
raw=b'*HEADING\r\ntext with an unmatched " quote\r\n*UNKNOWN_CARD, P=1, P=2\r\n5,6\r\n'
(root/'valid.inp').write_bytes(raw);doc=DeckReader(root).read(root/'valid.inp')
assert doc['cards'][0]['data'][0]['fields'] is None
assert len(doc['cards'][1]['parameters'])==2 and doc['diagnostics']
assert all(not c['coverage']['solve_authorized'] for c in doc['cards'])
export(doc,root/'valid-output');assert (root/'valid-output/valid.inp').read_bytes()==raw
bad=copy.deepcopy(doc);bad['files']['valid.inp']['sha256']='0'*64
try:export(bad,root/'bad-output')
except ValueError:pass
else:raise AssertionError('corrupt export accepted')
assert not (root/'bad-output').exists()
assert not list(root.glob('.inp-export-*'))
print('final incremental checks=7 PASS')
