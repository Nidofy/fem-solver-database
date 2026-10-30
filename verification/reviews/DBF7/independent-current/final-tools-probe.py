import sys,copy
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'final-snapshot/tools'))
from migration_audit import audit
mapping={'version':1,'source_sha256':'x','rules':[{'source':'a','target':'b','disposition':'stored','dtype':'u64','shape':[1],'units':'1','target_units':'1','basis':'global','index_base':1,'lifecycle':'model','missing':'reject','evidence':'probe'}]}
payload={'shape':[1],'units':'1','basis':'global','index_base':1,'values':[2**64-1]}
source={'version':1,'source_sha256':'x','capture_id':'c','populated_keys':['a'],'fields':{'a':payload}}
target={'version':1,'capture_id':'c','fields':{'b':copy.deepcopy(payload)}}
assert audit(mapping,source,target)['passed']
for declared in (None,[],['a','a'],['a','lost'],['wrong']):
    s=copy.deepcopy(source);s['populated_keys']=declared
    try: audit(mapping,s,target)
    except ValueError: pass
    else: raise AssertionError(declared)
target['fields']['b']['values'][0]-=1
assert not audit(mapping,source,target)['passed']
print('incremental mapping checks=7 PASS')
