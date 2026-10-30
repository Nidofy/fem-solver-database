"""Lossless INP document layer, independent of solver/ModelIR numerical support.

All source bytes and unknown cards survive storage/export. General keyword/CSV
syntax is parsed; per-keyword data grammar and physics remain explicitly unmapped.
Relative external paths use the job submission root, as documented for Abaqus.
Parameter expressions are data and are NEVER executed.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import tempfile


def key(value): return "".join(value.upper().split())


def csv_fields(line):
    fields, start, quoted = [], 0, False
    for i, char in enumerate(line):
        if char == '"': quoted = not quoted
        elif char == ',' and not quoted:
            fields.append(line[start:i].strip()); start = i+1
    if quoted: raise ValueError("unclosed quoted field")
    fields.append(line[start:].strip())
    return fields


class Catalog:
    def __init__(self, names=(), complete=False):
        self.names = {key(name):name for name in names}
        if len(self.names) != len(names): raise ValueError("ambiguous normalized keyword catalog")
        self.complete = complete
    def resolve(self, name):
        normalized = key(name)
        if normalized in self.names: return self.names[normalized], True
        if self.complete:
            matches = [value for k,value in self.names.items() if k.startswith(normalized)]
            if len(matches)>1: raise ValueError("ambiguous keyword abbreviation: "+name)
            if matches: return matches[0], True
        return name.upper().strip(), False


def default_catalog():
    path = Path(__file__).resolve().parents[1]/"data"/"inp_keywords_2025.json"
    if not path.exists(): raise FileNotFoundError("offline INP keyword catalog missing")
    data = json.loads(path.read_text(encoding="utf-8"))
    return Catalog(list(data["keywords"]), complete=True)


class DeckReader:
    def __init__(self, root, catalog=None, byte_limit=64*1024*1024, depth_limit=32, line_limit=256):
        self.root = Path(root).resolve(); self.catalog = catalog or default_catalog()
        self.byte_limit,self.depth_limit,self.line_limit = byte_limit,depth_limit,line_limit
        self.used=0; self.active=set(); self.files={}; self.cards=[]; self.references=[]; self.diagnostics=[]
    def path(self, value):
        # Quoted paths retain spaces; unquoted spaces are ignored by INP syntax.
        name = value[1:-1] if value.startswith('"') and value.endswith('"') else "".join(value.split())
        if not name or len(name)>256: raise ValueError("invalid external path length")
        p=Path(name)
        if os.name!='nt' and PureWindowsPath(name).is_absolute(): raise ValueError("foreign absolute path")
        p=(self.root/p).resolve()
        if not p.is_relative_to(self.root): raise ValueError("external input escapes allowed job root")
        return p
    def source(self, path):
        path=path.resolve()
        if not path.is_relative_to(self.root): raise ValueError("input outside allowed job root")
        with path.open('rb') as stream: raw=stream.read(self.byte_limit-self.used+1)
        self.used+=len(raw)
        if self.used>self.byte_limit: raise ValueError("expanded document byte budget")
        relative=path.relative_to(self.root).as_posix()
        previous=self.files.get(relative)
        digest=hashlib.sha256(raw).hexdigest()
        if previous and previous['sha256']!=digest: raise ValueError("source changed during capture")
        self.files[relative]={"sha256":digest,"bytes":len(raw),"base64":base64.b64encode(raw).decode('ascii')}
        text=raw.decode('utf-8-sig')
        lines=text.splitlines()
        if raw and not raw.endswith(b'\n'): self.diagnostics.append({"file":relative,"line":len(lines),"warning":"missing final line feed preserved"})
        out=[]
        for number,line in enumerate(lines,1):
            if not line.strip() or line.startswith('**'): continue
            if len(line)>self.line_limit or any(ord(c)>127 for c in line): raise ValueError(f"{relative}:{number}: non-ASCII or overlong input line")
            out.append((number,line.strip()))
        return relative,out
    def expand(self, path, depth=0, data_only=False):
        path=path.resolve()
        if depth>=self.depth_limit or path in self.active: raise ValueError("include depth/cycle")
        self.active.add(path)
        try:
            source,lines=self.source(path)
            if data_only:
                if any(text.startswith('*') for _,text in lines): raise ValueError("keyword in external data file")
                return [{"file":source,"line":number,"raw":text,"fields":csv_fields(text),"trailing_comma":text.endswith(',')} for number,text in lines]
            i=0
            while i<len(lines):
                number,text=lines[i];i+=1
                if not text.startswith('*') or text.startswith('**'): raise ValueError(f"{source}:{number}: data outside keyword")
                logical=text
                while logical.endswith(','):
                    if i==len(lines) or lines[i][1].startswith('*'): raise ValueError("missing keyword continuation")
                    logical+=lines[i][1];i+=1
                fields=csv_fields(logical[1:])
                if not fields[0]: raise ValueError("empty keyword")
                canonical,known=self.catalog.resolve(fields[0]); params=[]; names=set()
                for raw in fields[1:]:
                    name,sep,value=raw.partition('='); name=key(name)
                    if not name or name in names: raise ValueError("empty/duplicate keyword parameter")
                    names.add(name); params.append({"name":name,"value":value.strip() if sep else None,"raw":raw})
                card={"file":source,"line":number,"keyword":canonical,"spelling":fields[0],"known_keyword":known,
                      "parameters":params,"data":[],"semantic_status":"NOT_INTERPRETED"}
                while i<len(lines) and not lines[i][1].startswith('*'):
                    n,row=lines[i];i+=1
                    card['data'].append({"file":source,"line":n,"raw":row,"fields":csv_fields(row),"trailing_comma":row.endswith(',')})
                card_index=len(self.cards);self.cards.append(card)
                if canonical=='INCLUDE':
                    # INPUT may itself be uniquely abbreviated on INCLUDE.
                    if len(params)!=1 or not 'INPUT'.startswith(params[0]['name']) or not params[0]['value'] or card['data']:
                        raise ValueError("INCLUDE requires one INPUT path and no inline data")
                    target=self.path(params[0]['value']);self.references.append({"card":len(self.cards)-1,"kind":"include","target":target.relative_to(self.root).as_posix()})
                    self.expand(target,depth+1)
                elif 'INPUT' in names:
                    value=next(p['value'] for p in params if p['name']=='INPUT')
                    if value is None or card['data']: raise ValueError("external INPUT and inline data conflict")
                    target=self.path(value);self.references.append({"card":len(self.cards)-1,"kind":"data","target":target.relative_to(self.root).as_posix()})
                    card['data']=self.expand(target,depth+1,True)
                for p in params:
                    if p['name'] in ('FILE','LIBRARY'):
                        self.references.append({"card":card_index,"kind":"opaque_external","value":p['value']})
        finally: self.active.remove(path)
    def read(self, entry):
        path=Path(entry).resolve();self.expand(path)
        return {"version":1,"entry":path.relative_to(self.root).as_posix(),"files":self.files,"cards":self.cards,
                "references":self.references,"diagnostics":self.diagnostics,"expanded_bytes":self.used,
                "syntax_profile":"Abaqus 2025 general keyword/data syntax; strict duplicate-parameter rejection",
                "semantic_mapping_complete":False,"executable":False}


def save(document, output):
    output=Path(output)
    with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=output.parent,delete=False) as stream:
        temp=Path(stream.name)
        try:
            json.dump(document,stream,ensure_ascii=False,allow_nan=False);stream.flush();os.fsync(stream.fileno())
        except BaseException:
            stream.close();temp.unlink();raise
    try: os.link(temp,output) # atomic, refuses to replace an existing output
    finally: temp.unlink()


def load(path, byte_limit=256*1024*1024):
    with Path(path).open('rb') as stream: raw=stream.read(byte_limit+1)
    if len(raw)>byte_limit: raise ValueError("document storage budget")
    def unique(items):
        out={}
        for k,v in items:
            if k in out: raise ValueError("duplicate JSON key")
            out[k]=v
        return out
    doc=json.loads(raw,object_pairs_hook=unique)
    if doc.get('version')!=1 or doc.get('entry') not in doc['files']: raise ValueError("document schema/entry")
    total=0
    for name,file in doc['files'].items():
        p=Path(name)
        if not name or p.is_absolute() or PureWindowsPath(name).drive or '..' in p.parts or '\\' in name: raise ValueError("unsafe stored source path")
        data=base64.b64decode(file['base64'],validate=True);total+=len(data)
        if total>byte_limit or len(data)!=file['bytes'] or hashlib.sha256(data).hexdigest()!=file['sha256']: raise ValueError("source integrity/budget")
    # Derived cards are inspection metadata, never authorization to execute.
    doc['semantic_mapping_complete']=False;doc['executable']=False
    return doc


def export(document, output):
    output=Path(output).resolve()
    if output.exists(): raise FileExistsError(output)
    staging=Path(tempfile.mkdtemp(prefix='.inp-export-',dir=output.parent))
    try:
        for name,file in document['files'].items():
            target=(staging/name).resolve()
            if not target.is_relative_to(staging): raise ValueError("source path escapes output")
            target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as stream: stream.write(base64.b64decode(file['base64'],validate=True))
        if output.exists(): raise FileExistsError(output)
        staging.rename(output)
    except BaseException:
        if staging.parent!=output.parent or not staging.name.startswith('.inp-export-') or staging==output:
            raise RuntimeError("refusing cleanup outside the created staging directory")
        shutil.rmtree(staging);raise


def main():
    p=argparse.ArgumentParser();s=p.add_subparsers(dest='command',required=True)
    r=s.add_parser('read');r.add_argument('input',type=Path);r.add_argument('output',type=Path);r.add_argument('--root',type=Path)
    e=s.add_parser('export');e.add_argument('document',type=Path);e.add_argument('directory',type=Path)
    a=p.parse_args()
    if a.command=='read':
        doc=DeckReader(a.root or a.input.resolve().parent).read(a.input);save(doc,a.output)
        print(json.dumps({"files":len(doc['files']),"cards":len(doc['cards']),"unknown":sum(not c['known_keyword'] for c in doc['cards']),"semantic_mapping_complete":False}))
    else: export(load(a.document),a.directory)
if __name__=='__main__':main()
