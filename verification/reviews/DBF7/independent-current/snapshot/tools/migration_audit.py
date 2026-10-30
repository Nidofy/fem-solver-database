"""Offline mapping qualification. Never opens a legacy DB or imports solver code.

Snapshot values are neutral, flattened JSON arrays. Integer identities remain
integers; only f64 values accept explicit affine conversion. Every populated key
needs a rule. Reports distinguish synthetic fixtures from captured native data.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def load(path):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError(f"duplicate JSON key: {key}")
            out[key] = value
        return out
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=pairs,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))


def skeleton(inventory):
    return {"version": 1, "source_sha256": inventory["source_sha256"],
            "qualification": "UNQUALIFIED", "rules": [
                {"source": f"{group['extension']}:{field['ordinal']}",
                 "source_name": field["name"], "disposition": "unsupported",
                 "target": "", "dtype": "", "shape": [], "units": "",
                 "target_units": "", "basis": "", "index_base": None,
                 "lifecycle": "", "missing": "reject", "scale": 1, "offset": 0,
                 "evidence": f"DBSCHEMA.ASC:{field['line']}",
                 "boundary": group["target_boundary"]}
                for group in inventory["subfiles"] for field in group["fields"]]}


def typed(values, dtype):
    if not isinstance(values, list):
        raise ValueError("flat array required")
    for x in values:
        if dtype == "f64":
            if type(x) not in (int, float) or not math.isfinite(x):
                raise ValueError("finite float required")
        elif dtype in ("i64", "u64"):
            lo, hi = (-(1 << 63), (1 << 63)-1) if dtype == "i64" else (0, (1 << 64)-1)
            if type(x) is not int or not lo <= x <= hi:
                raise ValueError("exact integer required")
        else:
            raise ValueError("unknown dtype")


def audit(mapping, source, target):
    if mapping.get("version") != 1 or source.get("version") != 1 or target.get("version") != 1:
        raise ValueError("snapshot/mapping version")
    if not mapping.get("source_sha256") or source.get("source_sha256") != mapping["source_sha256"]:
        raise ValueError("source schema identity")
    if source.get("capture_id") != target.get("capture_id") or not source.get("capture_id"):
        raise ValueError("capture identity")
    rules = {}
    for r in mapping["rules"]:
        if r["source"] in rules:
            raise ValueError("duplicate mapping rule")
        rules[r["source"]] = r
    findings = []
    mapped = set()
    populated = source["fields"]
    for key, payload in populated.items():
        try:
            r = rules[key]
            if r["disposition"] not in ("stored", "reconstructed", "consumer_owned"):
                raise ValueError("populated field unsupported")
            for name in ("target", "dtype", "units", "target_units", "basis", "lifecycle", "evidence"):
                if not r.get(name):
                    raise ValueError(f"missing mapping semantics: {name}")
            if r.get("missing") != "reject" or r.get("index_base") not in (None, 0, 1):
                raise ValueError("unsupported missing/index policy")
            if r["target"] in mapped:
                raise ValueError("ambiguous target mapping")
            mapped.add(r["target"])
            actual = target["fields"][r["target"]]
            shape = r["shape"]
            if not shape or any(type(v) is not int or v <= 0 for v in shape):
                raise ValueError("explicit positive shape required")
            for record, units in ((payload, r["units"]), (actual, r["target_units"])):
                if record.get("shape") != shape or record.get("units") != units or record.get("basis") != r["basis"]:
                    raise ValueError("shape/units/basis mismatch")
                if record.get("index_base") != r.get("index_base"):
                    raise ValueError("index-base mismatch; renumbering requires a separate exact map")
                typed(record["values"], r["dtype"])
                if len(record["values"]) != math.prod(shape):
                    raise ValueError("value count")
            scale, offset = r.get("scale", 1), r.get("offset", 0)
            if type(scale) not in (int, float) or type(offset) not in (int, float) or not math.isfinite(scale) or scale <= 0 or not math.isfinite(offset):
                raise ValueError("conversion")
            if r["dtype"] != "f64" and (scale != 1 or offset != 0):
                raise ValueError("integer identity conversion forbidden")
            atol, rtol = r.get("atol", 0), r.get("rtol", 0)
            if any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 for v in (atol, rtol)):
                raise ValueError("tolerance")
            if r["dtype"] != "f64" and (atol or rtol):
                raise ValueError("integer comparison must be exact")
            expected = [v * scale + offset for v in payload["values"]] if r["dtype"] == "f64" else payload["values"]
            for i, (x, y) in enumerate(zip(expected, actual["values"])):
                equal = math.isfinite(x) and abs(x-y) <= atol + rtol*abs(x) if r["dtype"] == "f64" else x == y
                if not equal:
                    raise ValueError(f"value mismatch at scalar {i}")
        except (KeyError, ValueError, TypeError, OverflowError) as exc:
            findings.append({"source": key, "error": str(exc)})
    for key in target["fields"].keys() - mapped:
        findings.append({"target": key, "error": "unmapped target field"})
    return {"version": 1, "capture_id": source["capture_id"], "populated": len(populated),
            "passed": not findings, "findings": findings,
            "qualification": "SYNTHETIC_ONLY" if source.get("synthetic", True) else "CAPTURE_COMPARISON_ONLY",
            "native_mapping_certified": False}


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("skeleton"); s.add_argument("inventory"); s.add_argument("output")
    a = sub.add_parser("compare"); a.add_argument("mapping"); a.add_argument("source"); a.add_argument("target"); a.add_argument("output")
    args = p.parse_args()
    if args.command == "skeleton":
        report = skeleton(load(args.inventory))
    else:
        report = audit(load(args.mapping), load(args.source), load(args.target))
        report["input_sha256"] = {name: hashlib.sha256(Path(getattr(args, name)).read_bytes()).hexdigest()
                                  for name in ("mapping", "source", "target")}
    # Exclusive output creation prevents overwriting a source/evidence file.
    with Path(args.output).open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
    return 0 if report.get("passed", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
