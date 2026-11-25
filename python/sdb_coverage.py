"""Audit the DBF6 inventory, DBF7 mapping and draft ABI layouts; optionally compare a capture.

Run from a source checkout. A complete declaration ledger is distinct from a
qualified production mapping. Optional capture comparisons reuse the DBF7 value,
units, index-base, missing-value and basis checks.
"""
import argparse
import json
import hashlib
from collections import Counter
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from legacy_layout_draft import layout
from legacy_schema_inventory import inventory as read_schema
from migration_audit import audit as compare_capture, load

FOUNDATION = ROOT / "docs/development/db_foundation"
DISPOSITIONS = ("stored", "reconstructed", "consumer_owned", "unsupported")


def keyed(rows):
    by_key = {r["source"]: r for r in rows}
    if len(by_key) != len(rows):
        raise ValueError("duplicate field key")
    return by_key


def reconcile(inventory, mapping, layouts):
    if (inventory.get("schema"), mapping.get("version"), layouts.get("version")) != (1, 1, 1):
        raise ValueError("unsupported inventory, mapping or draft version")
    identity = inventory["source_sha256"]
    if mapping["source_sha256"] != identity or layouts["source_sha256"] != identity:
        raise ValueError("source schema digest differs")
    rules, drafts = keyed(mapping["rules"]), keyed(layouts["rules"])
    rows = []
    groups = {}
    for group in inventory["subfiles"]:
        ext, fields = group["extension"], group["fields"]
        if ext in groups or len(fields) != group["declared_fields"]:
            raise ValueError(f"{ext}: duplicate subfile or incorrect field count")
        groups[ext] = len(fields)
        for ordinal, field in enumerate(fields, 1):
            key = f"{ext}:{ordinal}"
            rule, draft = rules.pop(key, None), drafts.pop(key, None)
            if rule is None or draft is None or field["ordinal"] != ordinal:
                raise ValueError(f"{key}: missing declaration, mapping or layout")
            dtype, shape, width = layout(field)
            expected_source = f'{inventory["source"]}:{field["line"]}'
            if (draft["source_name"] != field["name"] or rule["source_name"] != field["name"] or
                    draft["evidence"] != expected_source or draft["draft_field_ordinal_base"] != 1 or
                    (draft["draft_dtype"], draft["draft_shape"], draft["draft_wire_bytes"]) != (dtype, shape, width)):
                raise ValueError(f"{key}: mismatched source identity or wire layout")
            status = rule["disposition"]
            if status not in DISPOSITIONS or not rule["evidence"] or rule["boundary"] != group["target_boundary"]:
                raise ValueError(f"{key}: missing responsibility or evidence")
            if status != "unsupported":
                if (any(not rule.get(k) for k in ("target", "shape", "basis", "units", "target_units", "lifecycle"))
                        or rule["dtype"] not in ("f64", "i64", "u64")
                        or not isinstance(rule["shape"], list)
                        or any(type(n) is not int or n < 1 for n in rule["shape"])
                        or rule.get("index_base") not in (None, 0, 1) or rule.get("missing") != "reject"):
                    raise ValueError(f"{key}: incomplete or invalid mapping semantics")
            rows.append({"source": key, "name": field["name"], "declaration": expected_source,
                         "wire_type": field["wire_type"], "draft_dtype": dtype, "draft_shape": shape,
                         "draft_bytes": width, "boundary": rule["boundary"], "disposition": status,
                         "target": rule["target"] or None, "evidence": rule["evidence"]})
    if rules or drafts:
        raise ValueError("unknown mapping or layout field")
    counts = Counter(r["disposition"] for r in rows)
    return {"source_sha256": identity, "subfiles": groups, "fields": len(rows),
            "dispositions": {name: counts[name] for name in DISPOSITIONS},
            "native_mapping_certified": False, "entries": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=FOUNDATION / "DBF6_LEGACY_INVENTORY.json")
    parser.add_argument("--mapping", type=Path, default=FOUNDATION / "DBF7_MAPPING_TEMPLATE.json")
    parser.add_argument("--layouts", type=Path, default=ROOT / "docs/development/LEGACY_LAYOUT_DRAFT.json")
    parser.add_argument("--schema", type=Path, help="cross-check a raw DBSCHEMA.ASC against the entire inventory")
    parser.add_argument("--capture", type=Path, help="neutral source snapshot with populated_keys")
    parser.add_argument("--target", type=Path, help="matching neutral target snapshot")
    parser.add_argument("--output", type=Path, required=True, help="new report file")
    parser.add_argument("--require-mapped", action="store_true",
                        help="fail the mapping gate if any declared field is still unsupported")
    args = parser.parse_args()
    if bool(args.capture) != bool(args.target):
        parser.error("--capture and --target must be supplied together")
    inv, mapping = load(args.inventory), load(args.mapping)
    if args.schema and read_schema(args.schema) != inv:
        raise ValueError("raw schema differs from frozen inventory")
    report = reconcile(inv, mapping, load(args.layouts))
    report["raw_schema_checked"] = args.schema is not None
    report["input_sha256"] = {name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for name, p in vars(args).items() if name != "output" and isinstance(p, Path)}
    report["all_declarations_mapped"] = report["dispositions"].get("unsupported", 0) == 0
    if args.capture:
        report["capture"] = compare_capture(mapping, load(args.capture), load(args.target))
    with args.output.open("x", encoding="utf-8") as out:
        json.dump(report, out, indent=2, ensure_ascii=False, allow_nan=False)
        out.write("\n")
    print(f"subfiles={len(report['subfiles'])} fields={report['fields']} dispositions={report['dispositions']}")
    return int((args.require_mapped and not report["all_declarations_mapped"]) or
               not report.get("capture", {}).get("passed", True))


if __name__ == "__main__":
    raise SystemExit(main())
