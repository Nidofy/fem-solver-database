"""Bounded INP include expansion and keyword inventory, not a full INP parser.

Use a consumer-owned allowlist; unsupported keywords fail the audit. This tool
does not claim to validate the data grammar of an allowed keyword.
"""
import argparse
import csv
import json
from pathlib import Path


def audit(path, root, allowed, byte_limit=16*1024*1024, depth_limit=32):
    root = Path(root).resolve()
    stack, records, unsupported = [], [], []
    used = 0
    def read(file):
        nonlocal used
        file = file.resolve()
        if not file.is_relative_to(root) or file in stack or len(stack) >= depth_limit:
            raise ValueError("include escape/cycle/depth")
        size = file.stat().st_size
        if size > byte_limit-used:
            raise ValueError("expanded input byte budget")
        # Bound the actual read too, in case the input grew after stat.
        with file.open("rb") as stream:
            raw = stream.read(byte_limit-used+1)
        used += len(raw)
        if used > byte_limit:
            raise ValueError("expanded input byte budget")
        stack.append(file)
        for number, line in enumerate(raw.decode("utf-8-sig").splitlines(), 1):
            line = line.strip()
            if not line or line.startswith("**") or not line.startswith("*"):
                continue
            parts = next(csv.reader([line[1:]], skipinitialspace=True))
            keyword = parts[0].strip().upper()
            params = {}
            for part in parts[1:]:
                pair = part.split("=", 1); key = pair[0].strip().upper()
                if key in params:
                    raise ValueError("duplicate keyword parameter")
                params[key] = pair[1].strip().strip('"\'') if len(pair) == 2 else None
            records.append({"file": str(file.relative_to(root)), "line": number, "keyword": keyword})
            if keyword == "INCLUDE":
                if set(params) != {"INPUT"} or not params["INPUT"]:
                    raise ValueError("INCLUDE requires one INPUT path")
                read(file.parent / params["INPUT"])
            elif keyword not in allowed:
                unsupported.append(records[-1])
        stack.pop()
    read(Path(path))
    return {"version": 1, "bytes": used, "keywords": records, "unsupported": unsupported,
            "passed": not unsupported, "grammar_qualified": False}


if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("input"); p.add_argument("--root", required=True)
    p.add_argument("--allow", required=True, help="JSON array of consumer-supported keyword names")
    a = p.parse_args(); result = audit(a.input, a.root, set(json.loads(Path(a.allow).read_text())))
    print(json.dumps(result, indent=2)); raise SystemExit(0 if result["passed"] else 1)
