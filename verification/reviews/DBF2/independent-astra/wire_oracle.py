# Independent fixed-width wire oracle: no sdb reader/writer imports.
import struct,zlib,sys,json
from pathlib import Path
class R:
 def __init__(self,b):self.b=b;self.p=0
 def get(self,f):
  s=struct.Struct('<'+f);v=s.unpack_from(self.b,self.p);self.p+=s.size;return v[0] if len(v)==1 else v
 def text(self):return self.take(self.get('Q')).decode()
 def take(self,n):v=self.b[self.p:self.p+n];assert len(v)==n;self.p+=n;return v
 def vec(self,f):return [f() for _ in range(self.get('Q'))]
 def end(self):assert self.p==len(self.b),(self.p,len(self.b))
def records(path,magic):
 r=R(path.read_bytes());assert r.get('Q')==magic;assert r.get('I')==1
 out=[]
 while r.p<len(r.b):
  kind,size,crc=r.get('IQI');body=r.take(size);assert zlib.crc32(body)==crc;out.append((kind,body))
 return out
def source(r):return [r.text(),r.text(),r.get('Q'),r.get('Q'),r.get('I'),r.text(),r.get('Q'),r.get('Q')]
def model(b):
 r=R(b);ids=r.get('QQQB');coords=r.vec(lambda:r.get('ddd'));conn=r.vec(lambda:r.vec(lambda:r.get('I')))
 def descriptor():return [r.get('IBIIII'),r.text(),r.text()]
 desc=r.vec(descriptor);nodes=r.vec(lambda:source(r));elements=r.vec(lambda:source(r));regions=r.vec(lambda:[r.text(),r.vec(lambda:r.get('I'))]);samples=r.vec(lambda:r.vec(lambda:r.get('IIII')));bases=r.vec(lambda:[r.get('Q'),r.get('9d')]);r.end()
 return dict(ids=ids,coords=coords,conn=conn,desc=desc,nodes=nodes,elements=elements,samples=samples,bases=bases)
def payload(b):
 r=R(b);version=r.get('I');name=r.text();scalar=r.get('I');shape=r.vec(lambda:r.get('I'));components=r.vec(r.text)
 assoc,location,shear=r.get('III');units=r.text();basis=r.text();policies=r.get('IIIIII')
 address=r.vec(lambda:r.get('QIIIIIiQQIQIiIQIiII'));valid=r.vec(lambda:r.get('B'));schema=r.text();schema_version=r.get('I');basis_id=r.get('Q')
 values=r.vec(lambda:r.get(['f','d','i','I','q','Q','B'][scalar]));r.end()
 return dict(name=name,scalar=scalar,shape=shape,components=components,address=address,valid=valid,schema=schema,basis_id=basis_id,values=values)
def check(path):
 rec=records(path,0x33534552424453);m=model(rec[0][1]);assert len(m['nodes'])==32 and len(m['elements'])==8
 assert m['nodes'][0][:3]==['P','A',1] and m['nodes'][16][:3]==['P','B',1]
 assert abs(m['coords'][16][0])<1e-12 and abs(m['coords'][16][1]-10)<1e-12
 assert m['samples'][1]==[(1,1,1,1),(1,1,2,1),(1,2,1,2),(1,2,2,2),(1,2,3,2)]
 assert dict(m['bases'])[8]==(0,-1,0,1,0,0,0,0,1)
 fields=[payload(b) for k,b in rec if k==2];groups={}
 for f in fields:groups.setdefault(f['name'],[]).append(f)
 flags=groups['FLAGS'][0];assert flags['values']==[2**64-1,0] and flags['valid']==[1,0]
 plastic=groups['plastic'][0];idx=next(i for i,a in enumerate(plastic['address']) if a[:4]==(2,1,2,2));assert plastic['values'][idx*2:idx*2+2]==[2,19]
 tensor=groups['S_LOCAL'][0];assert tensor['components']==['XX','YY','ZZ','XY','XZ','YZ'] and tensor['basis_id']==8
 assert groups['GLOBAL'][0]['address'][0]==(0,)*19 and groups['GLOBAL'][0]['values']==[42]
 assert [f['address'][0][7] for f in groups['CONTACT']]==[3,4]
 rst=records(path.with_suffix('.sdbrst'),0x33545352424453);meta=R(rst[0][1]);assert meta.get('III')==(4,3,1);width=meta.get('B');meta.get('QQQQQQ');assert meta.get('dd')==(.5,.01);meta.end()
 state={}
 for kind,b in rst[1:-1]:
  r=R(b);size,comps,w,offset,count=r.get('QIBQQ');values=[r.get('B' if w==1 else 'f' if w==4 else 'd') for _ in range(count)];r.end();assert offset==0;state[kind]=values
 assert rst[-1]==(2**32-1,b'') and len(state)==20
 assert state[14][5]==19 and state[15][64]==.5 and state[20][32]==8 and state[11][5]==0
 assert all(x==2 for x in state[18]) and all(x==.5 for x in state[19])
 return dict(file=str(path),result_fields=len(fields),restart_fields=len(state),precision=width,status='PASS')
print(json.dumps([check(Path(sys.argv[1])/f'{kind}.sdbres') for kind in ['float','double']],indent=2))

