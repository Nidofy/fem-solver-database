"""Enrich an unqualified mapping template with static field layout evidence."""
import argparse
import json
from pathlib import Path

# util/compsch/compsch.cpp::getFieldType, Windows production ABI assumption.
SCALARS = {"INT": ("i32",4), "REAL": ("f32",4), "DOUBLE": ("f64",8), "CHAR": ("byte",1)}


def layout(field):
    kind, size = field["wire_type"], field["size"]
    if kind in SCALARS:
        dtype, width = SCALARS[kind]
        return dtype, [1], width
    if kind == "STR":
        return "byte", [size+1], size+1
    if kind == "NODE_DATA":
        return None, None, None
    raise ValueError("unrecognized field type: " + kind)


def draft(inventory, template):
    if inventory["source_sha256"] != template["source_sha256"] or template["qualification"] != "UNQUALIFIED":
        raise ValueError("draft requires a matching unqualified inventory/template")
    by_key = {r["source"]: r for r in template["rules"]}
    if len(by_key) != len(template["rules"]):
        raise ValueError("duplicate mapping key")
    seen = set()
    rows = []
    for sub in inventory["subfiles"]:
        for f in sub["fields"]:
            key = f'{sub["extension"]}:{f["ordinal"]}'
            rule = by_key[key]
            if key in seen or rule["source_name"] != f["name"]:
                raise ValueError("field identity mismatch")
            seen.add(key)
            dtype, shape, width = layout(f)
            rows.append(dict(rule, draft_dtype=dtype, draft_shape=shape, draft_wire_bytes=width,
                             draft_field_ordinal_base=1, draft_value_index_base=None,
                             confidence=("ABI_LAYOUT_PENDING_CAPTURE" if width and sub["count_matches"] else
                                         "DECLARATION_COUNT_MISMATCH" if width else "NATIVE_STRUCT_ABI_REQUIRED")))
    if seen != by_key.keys():
        raise ValueError("inventory/template coverage mismatch")
    return dict(template, rules=rows)


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("inventory","template","output"):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    rows=draft(json.loads(args.inventory.read_text()),json.loads(args.template.read_text()))
    args.output.write_text(json.dumps(rows,ensure_ascii=True,indent=2)+"\n",encoding="utf8")
