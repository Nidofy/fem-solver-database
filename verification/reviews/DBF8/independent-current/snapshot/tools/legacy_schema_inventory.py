"""Offline inventory only. No solver/old-DB dependency in the library or its build."""
import argparse
import hashlib
import json
from pathlib import Path
import re

TARGETS = {
    "ctr": "consumer-owned versioned analysis configuration",
    "set": "ModelIR sets / frozen regions",
    "csy": "ModelFoundation coordinates / explicit basis; non-Cartesian adapter required",
    "nod": "MeshDB reference coordinates / SourceMap / DofSpace",
    "ele": "MeshDB / topology and formulation contracts / batch metadata",
    "gst": "ModelFoundation sections / consumer SectionCatalog",
    "est": "element formulation contract / explicit capability validation",
    "mst": "MaterialDefinition / MaterialCatalog / registered material state",
    "cns": "ModelIR boundaries / DofSpace prescribed bindings",
    "cpd": "coupling declarations / consumer-supplied flattened DOF relations",
    "ceq": "ModelFoundation equations / DofSpace eliminated bindings",
    "lod": "ModelFoundation loads / consumer-owned slot load evaluation",
    "epr": "face load declarations / consumer batch loads",
    "eht": "registered thermal fields / consumer-owned thermal load definitions",
    "pts": "input adapter geometry (not a kernel storage obligation)",
    "lns": "input adapter geometry (not a kernel storage obligation)",
    "pat": "input adapter geometry (not a kernel storage obligation)",
    "ini": "initial conditions / registered field initialization",
    "dis": "typed node/element-node/IP/section results",
    "phs": "paired typed fields; frequency/complex semantics require further qualification",
    "dyn": "StateDB or registered kinematics / typed results",
    "rfc": "registered reactions / explicit face/contact addresses / typed results",
    "trl": "registered thermal fields / typed results",
    "eig": "typed fields; modal axis/normalization semantics require further qualification",
    "ftn": "material tables / amplitude policies / consumer-owned functions",
}


def inventory(path):
    raw = path.read_bytes()
    # Names in the current schema are ASCII; non-ASCII comments need not be interpreted.
    lines = raw.decode("latin1").splitlines()
    records = []
    current = None
    for line_number, line in enumerate(lines, 1):
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        if text == "BEGIN":
            if current is not None:
                raise ValueError("nested schema record")
            current = {"line": line_number, "fields": []}
        elif text == "END":
            if current is None or "extension" not in current or "declared_fields" not in current:
                raise ValueError("incomplete schema record")
            current["target_boundary"] = TARGETS.get(current["extension"], "UNCLASSIFIED")
            current["mapping_status"] = "NATIVE_FIELD_EQUIVALENCE_NOT_QUALIFIED"
            current["count_matches"] = len(current["fields"]) == current["declared_fields"]
            records.append(current)
            current = None
        elif current is not None:
            ext = re.match(r"EXTENSION\s*,\s*(\w+)", text)
            count = re.match(r"FIELDS\s*,\s*(\d+)", text)
            field = re.match(r'"([^"]*)"\s*,\s*(\w+)\s*,\s*(\d+)', text)
            if ext:
                current["extension"] = ext[1]
            elif count:
                current["declared_fields"] = int(count[1])
            elif field:
                current["fields"].append({"ordinal": len(current["fields"]) + 1,
                                          "name": field[1], "wire_type": field[2],
                                          "size": int(field[3]), "line": line_number})
    if current is not None or len({r["extension"] for r in records}) != len(records):
        raise ValueError("unfinished or duplicate schema records")
    return {"schema": 1, "source": path.name, "source_sha256": hashlib.sha256(raw).hexdigest(),
            "status": "INVENTORY_ONLY_NOT_PRODUCTION_MAPPING_CERTIFICATION", "subfiles": records}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = inventory(args.source)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    counts = [(r["extension"], r["declared_fields"], len(r["fields"])) for r in report["subfiles"] if not r["count_matches"]]
    print(json.dumps({"subfiles": len(report["subfiles"]), "fields": sum(len(r["fields"]) for r in report["subfiles"]),
                      "count_mismatches": counts}))
