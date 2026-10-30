"""Refresh factual keyword names/URLs only; never copy documentation prose.
The generated catalog is checked in. Runtime parsing has no network dependency.
"""
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import urllib.request

URL="https://docs.software.vt.edu/abaqusv2025/English/SIMACAECAERefMap/simacae-c-gen-kwbrowser.htm"
class Links(HTMLParser):
    def __init__(self): super().__init__(); self.href=None;self.text=[];self.names={}
    def handle_starttag(self,tag,attrs):
        if tag=="a": self.href=dict(attrs).get("href","");self.text=[]
    def handle_data(self,data):
        if self.href is not None:self.text.append(data)
    def handle_endtag(self,tag):
        if tag=="a" and self.href is not None:
            name=" ".join("".join(self.text).split()).lstrip("*")
            if "SIMACAEKEYRefMap/simakey-r-" in self.href and name:
                self.names[name]=urllib.parse.urljoin(URL,self.href)
            self.href=None
if __name__=="__main__":
    with urllib.request.urlopen(URL,timeout=30) as response: raw=response.read(4*1024*1024)
    parser=Links();parser.feed(raw.decode('utf-8'))
    assert len(parser.names)>400
    out={"version":1,"release":"Abaqus 2025","source":URL,"source_sha256":hashlib.sha256(raw).hexdigest(),
         "scope":"Keyword names from published browser; parameter/data grammar is not certified by enumeration",
         "keywords":dict(sorted(parser.names.items()))}
    path=Path(__file__).resolve().parents[1]/'data'/'inp_keywords_2025.json';path.parent.mkdir(exist_ok=True)
    with path.open('x',encoding='utf-8') as stream:json.dump(out,stream,indent=2)
    print(len(parser.names),'keyword names recorded')
