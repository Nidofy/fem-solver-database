"""Inventory source-visible ZDF/ZBF writer operations for later capture comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import re

FILES = ("old/cacuser/fmpfzw.cpp", "old/cacuser/ZwResultComponent.cpp")


def inventory(root):
    writers, registrations, sources = [], [], {}
    for name in FILES:
        raw = (root/name).read_bytes()
        sources[name] = hashlib.sha256(raw).hexdigest()
        text = raw.decode("latin1")
        # Preserve original line numbers. Ignore disabled examples in C++ comments.
        text = re.sub(r"/\*.*?\*/",lambda m:"\n"*m[0].count("\n"),text,flags=re.S)
        owner = None
        for line_number,line in enumerate(text.splitlines(),1):
            line = line.split("//")[0]
            method = re.search(r"\b(Zw\w+::\w+)\s*\(",line)
            if method and line and not line[0].isspace():
                owner = method[1]
            if owner == "ZwExportResult::_GetComponents":
                factory = re.search(r"result\.push_back\((_G\w+)\(([^)]*)\)\)",line)
                if factory:
                    registrations.append(dict(factory=factory[1], variant=factory[2], line=line_number))
            for match in re.finditer(r"(?:->|\.)(WriteAttribute|WriteData|WriteBlock|WriteArray|WriteDataset|WriteVector|OpenFile)\s*\(",line):
                writers.append(dict(file=name,line=line_number,method=owner,operation=match[1],
                                    literals=re.findall(r'"([^"]*)"',line[match.end():])))
    if not registrations or not writers:
        raise ValueError("source layout changed; review inventory extractor")
    return dict(status="STATIC_WRITER_CALL_SITES_UNQUALIFIED",sources=sources,
                component_dispatch=registrations,writer_calls=writers)


if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("solver_root",type=Path)
    p.add_argument("output",type=Path)
    a=p.parse_args()
    a.output.write_text(json.dumps(inventory(a.solver_root),indent=2)+"\n",encoding="utf8")
