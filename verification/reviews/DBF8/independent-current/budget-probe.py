import importlib.util
from pathlib import Path
p=Path(__file__).parent/'budget-closure/inp_document.py'
spec=importlib.util.spec_from_file_location('budget_review',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks=0
for value in (-1,0,True,1.5,'2'):
    for key in ('byte_limit','depth_limit','line_limit'):
        try:m.DeckReader('.',**{key:value})
        except ValueError:checks+=1
        else:raise AssertionError((key,value))
    try:m.load('does-not-exist',byte_limit=value)
    except ValueError:checks+=1
    else:raise AssertionError(value)
print('budget boundary checks=',checks,'PASS')
