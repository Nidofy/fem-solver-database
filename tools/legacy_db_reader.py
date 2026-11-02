"""Bounded read-only probe for little-endian Win64 CAC DB, indexed fixed-length records."""
import argparse
import json
from pathlib import Path
import struct

from legacy_layout_draft import layout

# cfmDbase.h::_fhead (default Win64 packing); i32, 64-bit CAC_LARGE_INT.
FHEAD = struct.Struct("<q4s11iq3i4xq10i")
HEADER = struct.Struct("<iqiiq")
TYPES = {"INT":1, "REAL":2, "CHAR":3, "STR":4, "DOUBLE":5, "NODE_DATA":6}
FORMATS = {"INT":"<i","REAL":"<f","DOUBLE":"<d","CHAR":"<B"}


def unpack(data, fmt, at):
    if isinstance(fmt, str):
        fmt = struct.Struct(fmt)
    if at < 0 or at > len(data)-fmt.size:
        raise ValueError("record address outside file")
    return fmt.unpack_from(data, at)


def record(data, sub, case, key, *, block=None):
    if case < 1 or key < 1: raise ValueError("case and key must be one-based")
    init, = unpack(data, "<i", 170)
    if init < 0: raise ValueError("negative initial buffer")
    count, pos, blocks, indices, available = unpack(data, HEADER, 174+init+48)
    if not (0 <= count <= 50 and 0 < blocks and 0 < indices and 174+init+48+HEADER.size <= available <= len(data)):
        raise ValueError("unsupported or invalid DB header")
    data = memoryview(data)[:available]
    visited = set()
    for _ in range(count):
        if not pos or pos in visited: raise ValueError("invalid subfile chain")
        visited.add(pos)
        h = unpack(data,FHEAD,pos)
        name = h[1].split(b"\0")[0].decode("ascii")
        if name != sub["extension"]:
            pos=h[0]
            continue
        fixed, _, length, maxrecs, cases = h[2:7]
        nindex, nfields, varloc = h[10], h[14], h[16]
        if fixed not in (0,1) or nfields!=len(sub["fields"]) or not sub["count_matches"]:
            raise ValueError("only schema-matched indexed fixed-length records are supported")
        if not (1<=case<=cases and 1<=key<=maxrecs and 0<indices and (block is not None or (key-1)//indices<nindex)):
            raise KeyError("case or key outside subfile")
        ends_at = pos+FHEAD.size+8*cases
        types_at = ends_at+4*nfields
        case_list = types_at+nfields
        fields = []
        size = 0
        for i,field in enumerate(sub["fields"]):
            kind=field["wire_type"]
            width=layout(field)[2]
            end,=unpack(data,"<i",ends_at+4*i)
            type_id,=unpack(data,"<B",types_at+i)
            if width is None or end!=size+width or TYPES.get(kind)!=type_id:
                raise ValueError("field ABI differs from assumed schema")
            fields.append((size,width,kind))
            size=end
        if size+1!=length: raise ValueError("record length mismatch")
        if block is not None:
            if not fixed or varloc or case!=1 or not 1<=block<=h[3] or (block-1)//blocks>=h[9]:
                raise ValueError("invalid fixed-block addressing")
            page,=unpack(data,"<q",case_list+8*((block-1)//blocks))
            if page<=0: raise KeyError("block index missing")
            start,=unpack(data,"<q",page+8*((block-1)%blocks))
            if start<=0: raise KeyError("deleted or absent block")
            addr=start+(key-1)*length
        else:
            if fixed: raise ValueError("use --block for a fixed-block subfile")
            page_list,=unpack(data,"<q",case_list+8*(case-1))
            if page_list<=0: raise KeyError("case missing")
            page,=unpack(data,"<q",page_list+8*((key-1)//indices))
            if page<=0: raise KeyError("index page missing")
            addr,=unpack(data,"<q",page+8*((key-1)%indices))
            if addr<=0: raise KeyError("deleted or absent record")
        flag,=unpack(data,"<B",addr)
        if flag!=1: raise KeyError("record flag is inactive")
        addr+=1
        if varloc:
            if not 0<varloc<nfields or fields[varloc-1][2]!="INT":
                raise ValueError("invalid VARLOC length field")
            actual,=unpack(data,"<i",addr+fields[varloc-1][0])
            prefix=fields[varloc][0]
            group_size=size-prefix
            if actual<1+prefix or (actual-1-prefix)%group_size or actual>len(data)-addr+1:
                raise ValueError("invalid variable record length")
            groups=(actual-1-prefix)//group_size
            if groups*(nfields-varloc)>1000000: raise ValueError("variable field budget")
            fields=fields[:varloc]+[(off+i*group_size,w,k) for i in range(groups) for off,w,k in fields[varloc:]]
            size=actual-1
        if addr>len(data)-size: raise ValueError("truncated data record")
        values = []
        for i,(offset,width,kind) in enumerate(fields):
            col=i if i<nfields else varloc+(i-varloc)%(nfields-varloc)
            f=sub["fields"][col]
            if kind=="STR":
                value=bytes(data[addr+offset:addr+offset+width]).hex()
            elif kind=="NODE_DATA":
                val=unpack(data,"<i3f3i",addr+offset)
                value=dict(num=val[0],coord=list(val[1:4]),cs=val[4],cs_user=val[5],cs_output=val[6])
            else:
                value,=unpack(data,FORMATS[kind],addr+offset)
            values.append(dict(ordinal=i+1,name=f["name"],value=value))
        return values
    raise KeyError("subfile not found")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("database","inventory"):
        parser.add_argument(name,type=Path)
    parser.add_argument("extension")
    parser.add_argument("--block",type=int)
    for name in ("case","key"):
        parser.add_argument(name,type=int)
    args=parser.parse_args()
    with args.database.open("rb") as source:
        data=source.read((64<<20)+1)
    if len(data)>64<<20: raise ValueError("64 MiB synthetic probe budget")
    subs=json.loads(args.inventory.read_text())["subfiles"]
    sub=next(s for s in subs if s["extension"]==args.extension)
    print(json.dumps(record(data,sub,args.case,args.key,block=args.block),allow_nan=False))
