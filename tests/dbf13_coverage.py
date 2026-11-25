import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from sdb_coverage import FOUNDATION, DISPOSITIONS, reconcile, load

inv = load(FOUNDATION / "DBF6_LEGACY_INVENTORY.json")
mapping = load(FOUNDATION / "DBF7_MAPPING_TEMPLATE.json")
layouts = load(ROOT / "docs/development/LEGACY_LAYOUT_DRAFT.json")
report = reconcile(inv, mapping, layouts)
assert len(report["subfiles"]) == 25 and report["fields"] == 2047
assert report["source_sha256"] == "4e23803fb24c17307be90839a6286a49d2335ea749be5b9091c6f889d86f6b4e"
assert sum(report["dispositions"].values()) == 2047 and not report["native_mapping_certified"]

def rejects(f):
    try:
        f()
    except ValueError:
        return
    raise AssertionError("corrupt field ledger accepted")

for change in ("missing", "duplicate", "identity", "empty target"):
    bad = copy.deepcopy(mapping)
    if change == "missing":
        bad["rules"].pop()
    elif change == "duplicate":
        bad["rules"][-1] = bad["rules"][0]
    elif change == "identity":
        bad["rules"][0]["source_name"] += "?"
    else:
        bad["rules"][0]["disposition"] = "stored"
    rejects(lambda: reconcile(inv, bad, layouts))
bad = copy.deepcopy(layouts)
bad["rules"][-1]["draft_wire_bytes"] += 1
rejects(lambda: reconcile(inv, mapping, bad))

def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'

scratch = Path(sys.argv[2])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as work:
    strict = Path(work) / "audit.json"
    check = subprocess.run([sys.executable, str(ROOT / "python/sdb_coverage.py"), "--output", str(strict),
                            "--require-mapped"], capture_output=True, text=True)
    strict_report = load(strict)
    assert check.returncode == int(bool(report["dispositions"].get("unsupported")))
    assert strict_report["all_declarations_mapped"] == (check.returncode == 0)
    # A single synthetic scalar exercises positive and negative capture comparisons.
    synthetic = copy.deepcopy(mapping)
    key = next(r["source"] for r in report["entries"]
               if r["draft_dtype"] == "f64" and r["draft_shape"] == [1] and r["name"])
    rule = next(r for r in synthetic["rules"] if r["source"] == key)
    rule.update(disposition="stored", target="test.scalar", dtype="f64", shape=[1],
                basis="global", units="m", target_units="mm", lifecycle="static",
                scale=1000, evidence="synthetic test fixture")
    source = {"version": 1, "capture_id": "fixture", "synthetic": True,
              "source_sha256": report["source_sha256"], "populated_keys": [key],
              "fields": {key: {"shape": [1], "units": "m", "basis": "global", "values": [1.5]}}}
    target = {"version": 1, "capture_id": "fixture",
              "fields": {"test.scalar": {"shape": [1], "units": "mm", "basis": "global", "values": [1500]}}}
    paths = [Path(work) / (name + ".json") for name in ("mapping", "source", "target")]
    for path, record in zip(paths, (synthetic, source, target)):
        path.write_text(json.dumps(record), encoding="utf-8")
    for correct in (True, False):
        if not correct:
            target["fields"]["test.scalar"]["values"] = [1501]
            paths[2].write_text(json.dumps(target), encoding="utf-8")
        out = Path(work) / f"comparison-{correct}.json"
        cmd = [sys.executable, str(ROOT / "python/sdb_coverage.py"), "--mapping", paths[0],
               "--capture", paths[1], "--target", paths[2], "--output", out]
        result = subprocess.run(cmd, capture_output=True, text=True)
        comparison = load(out)["capture"]
        assert result.returncode == int(not correct) and comparison["passed"] == correct
        assert comparison["qualification"] == "SYNTHETIC_ONLY" and not comparison["native_mapping_certified"]

    wire = Path(work) / "coverage.txt"
    with wire.open("w", encoding="utf-8") as out:
        print(quote(report["source_sha256"]), file=out)
        for r in report["entries"]:
            semantics = f"{r['name']} ({r['wire_type']}, {r['draft_bytes']} draft bytes)"
            print(quote(r["source"]), quote(r["target"] or r["boundary"]), quote(semantics),
                  quote(r["evidence"]), DISPOSITIONS.index(r["disposition"]), file=out)
    subprocess.run([sys.argv[1], wire], check=True)
