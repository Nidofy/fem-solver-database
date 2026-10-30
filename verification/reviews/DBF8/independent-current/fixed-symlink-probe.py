from pathlib import Path
import sys,os
sys.path.insert(0,str(Path(__file__).parent/'final-tools/tools'))
from inp_document import DeckReader,export
root=Path(__file__).parent/'symlink-case-fixed';root.mkdir(exist_ok=True)
(root/'child.inp').write_text('*NODE\n1,0,0,0\n')
(root/'main.inp').write_text('*INCLUDE, INPUT=alias.inp\n')
os.symlink('child.inp',root/'alias.inp')
doc=DeckReader(root).read(root/'main.inp')
export(doc,root/'exported')
print('captured=',list(doc['files']))
print('exported alias exists=',(root/'exported/alias.inp').exists())
try:DeckReader(root/'exported').read(root/'exported/main.inp')
except Exception as e:raise AssertionError('export reread failed') from e

