import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from migration_audit import audit, skeleton, load
from inp_audit import audit as inp_audit


class Tools(unittest.TestCase):
    def setUp(self):
        self.mapping = {"version": 1, "source_sha256": "synthetic-schema", "rules": [
            {"source": "capture.node.labels", "target": "mesh.node.labels", "disposition": "stored",
             "dtype": "u64", "shape": [2], "units": "1", "target_units": "1", "basis": "identity",
             "index_base": 1, "lifecycle": "model", "missing": "reject", "evidence": "synthetic fixture"}]}
        self.payload = {"shape": [2], "units": "1", "basis": "identity", "index_base": 1,
                        "values": [9007199254740993, (1 << 64)-1]}
        self.source = {"version": 1, "source_sha256": "synthetic-schema", "capture_id": "fixture-1",
                       "synthetic": True, "populated_keys": ["capture.node.labels"], "fields": {"capture.node.labels": self.payload}}
        self.target = {"version": 1, "capture_id": "fixture-1", "fields": {"mesh.node.labels": copy.deepcopy(self.payload)}}

    def test_exact_integer_and_negative_cases(self):
        self.assertTrue(audit(self.mapping, self.source, self.target)["passed"])
        for mutate in (
            lambda t: t["values"].__setitem__(0, 9007199254740992),
            lambda t: t["values"].__setitem__(0, float(9007199254740993)),
            lambda t: t.update(units="mm"), lambda t: t.update(basis="other"),
            lambda t: t.update(index_base=0), lambda t: t.update(shape=[1, 2])):
            with self.subTest(mutate=mutate):
                target = copy.deepcopy(self.target); mutate(target["fields"]["mesh.node.labels"])
                self.assertFalse(audit(self.mapping, self.source, target)["passed"])
        self.source["fields"]["unknown"] = self.payload
        with self.assertRaises(ValueError): audit(self.mapping,self.source,self.target)
        self.source["populated_keys"].append("unknown")
        self.assertFalse(audit(self.mapping, self.source, self.target)["passed"])

    def test_conversion_and_unmapped(self):
        r = self.mapping["rules"][0]; r.update(dtype="f64", scale=.001, units="mm", target_units="m")
        self.payload.update(values=[1000, 2000], units="mm")
        self.target["fields"]["mesh.node.labels"].update(values=[1, 2], units="m")
        self.assertTrue(audit(self.mapping, self.source, self.target)["passed"])
        r.update(disposition="unsupported")
        self.assertFalse(audit(self.mapping, self.source, self.target)["passed"])

    def test_inventory(self):
        root = Path(__file__).resolve().parents[1]
        inv = load(root / "docs/development/db_foundation/DBF6_LEGACY_INVENTORY.json")
        rules = skeleton(inv)["rules"]
        self.assertEqual(len(rules), 2047)
        self.assertEqual(len({r["source"] for r in rules}), 2047)
        self.assertTrue(all(r["disposition"] == "unsupported" for r in rules))

    def test_include_limits(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); main = p/"main.inp"; child = p/"child.inp"
            main.write_text('*INCLUDE, INPUT="child.inp"\n*NODE\n1,0,0,0\n')
            child.write_text("** comment\n*ELEMENT, TYPE=C3D4\n")
            self.assertTrue(inp_audit(main,p,{"NODE", "ELEMENT"})["passed"])
            self.assertFalse(inp_audit(main,p,{"NODE"})["passed"])
            with self.assertRaises(ValueError): inp_audit(main,p,{"NODE"},byte_limit=8)
            child.write_text("*INCLUDE, INPUT=main.inp\n")
            with self.assertRaises(ValueError): inp_audit(main,p,{"NODE"})
            child.write_text("*INCLUDE, INPUT=../outside.inp\n")
            with self.assertRaises(ValueError): inp_audit(main,p,{"NODE"})


if __name__ == "__main__":
    unittest.main()
