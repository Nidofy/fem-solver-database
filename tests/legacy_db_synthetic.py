"""Construct one case-indexed subfile from an ASC inventory and verify native-style offsets."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"tools"))
from legacy_db_reader import record
from legacy_layout_draft import layout


def generate(sub):
    # Independent byte layout following header.cpp and index.cpp, no reader packers.
    data=bytearray(250)
    data[:9]=b"SYNTHETIC"
    header=len(data)
    data.extend(bytes(128))
    data[header+8:header+12]=sub["extension"].encode().ljust(4,b"\0")
    body=bytearray()
    ends=[]
    codes=[]
    values=[]
    for f in sub["fields"]:
        k=f["wire_type"]; i=f["ordinal"]
        fmt,code={"INT":("<i",1),"REAL":("<f",2),"DOUBLE":("<d",5),"CHAR":("<B",3),"STR":(None,4)}[k]
        if fmt:
            v=i%250 if k=="CHAR" else i
            body.extend(struct.pack(fmt,v))
        else:
            raw=bytes([65])*f["size"]+bytes(1);body.extend(raw);v=raw.hex()
        values.append(v);ends.append(len(body));codes.append(code)
    struct.pack_into("<11i",data,header+12,0,0,len(body),200,2,2,1,0,2,0,0)
    struct.pack_into("<3i",data,header+64,len(ends),len(ends),0)
    data.extend(struct.pack("<4i",0,102,0,1))
    for end in ends: data.extend(struct.pack("<i",end))
    data.extend(bytes(codes))
    case_list=len(data)
    page_list=case_list+16
    page=page_list+16
    payload=page+100*8
    data.extend(struct.pack("<4q",0,page_list,0,page))
    data.extend(struct.pack("<2q",0,payload)+bytes(98*8))
    data.extend(body)
    struct.pack_into("<iqiiq",data,222,1,header,100,100,len(data))
    return data,values,page


class LegacyRecords(unittest.TestCase):
    def test_profiles(self):
        subs=json.loads((root/"docs/development/db_foundation/DBF6_LEGACY_INVENTORY.json").read_text())["subfiles"]
        for sub in subs:
            if sub["extension"] in ("set","ftn","ceq") or any(layout(f)[2] is None for f in sub["fields"]):continue
            with self.subTest(sub=sub["extension"]), tempfile.TemporaryDirectory() as temp:
                data,values,page=generate(sub)
                p=Path(temp)/"synthetic.db";p.write_bytes(data)
                self.assertEqual([r["value"] for r in record(p.read_bytes(),sub,2,102)],values)
                for case,key in [(1,102),(2,1),(2,101)]:
                    with self.assertRaises(KeyError): record(data,sub,case,key)
                with self.assertRaises(ValueError): record(data[:-1],sub,2,102)
                struct.pack_into("<q",data,page+8,-1)
                with self.assertRaises(KeyError):record(data,sub,2,102)
                struct.pack_into("<q",data,page+8,len(data)+1)
                with self.assertRaises(ValueError):record(data,sub,2,102)


if __name__=="__main__":
    unittest.main()
