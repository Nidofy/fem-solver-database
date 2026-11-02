"""Run three closed-form single-DOF reference histories through the installed CLI."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

# fixture, sequential result node, mass, stiffness, force, duration, sample interval
CASES = [
    ("forced_tet", 4, 1/24, 100/6, 1., .2, .01),
    ("forced_hex", 2, 1/8, 200/9, .001, .1, .01),
    ("forced_hex_reduced", 2, 1/8, 15., .001, .1, .01),
]


def run(exe, fixtures):
    metrics = []
    with tempfile.TemporaryDirectory() as temp:
        for name, node, mass, stiffness, force, end, interval in CASES:
            source = fixtures / (name + ".inp")
            for precision, limit in [("double", 2e-6), ("float", 5e-5)]:
                result = Path(temp) / (name + precision + ".sdbres")
                subprocess.run([exe, "--input", source, "--precision", precision, "--output", result],
                               check=True, stdout=subprocess.PIPE)
                output = subprocess.check_output([exe, "--read", result, "--field", "U",
                                                  "--entity", str(node)], text=True)
                rows = list(csv.DictReader(output.splitlines()))
                if len(rows) != round(end/interval)+1:
                    raise ValueError(f"{name}: incomplete sample history")
                errors = []
                for i, row in enumerate(rows):
                    t = float(row["time"])
                    if not math.isclose(t, i*interval, abs_tol=1e-7):
                        raise ValueError("unexpected sample time")
                    expected = force/stiffness*(1-math.cos(math.sqrt(stiffness/mass)*t))
                    error = abs(float(row["value"])-expected)/(force/stiffness)
                    if not math.isfinite(error):
                        raise ValueError("nonfinite displacement")
                    errors.append(error)
                metrics.append(dict(case=name, precision=precision, samples=len(rows),
                                    deck_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                                    normalized_linf=max(errors),
                                    normalized_rms=math.sqrt(sum(e*e for e in errors)/len(errors)),
                                    limit=limit, passed=max(errors)<=limit))
    return metrics


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("executable", type=Path)
    p.add_argument("fixtures", type=Path)
    args = p.parse_args()
    metrics = run(args.executable.resolve(), args.fixtures)
    print(json.dumps({"schema":1, "reference":"analytic", "metrics":metrics}, indent=2))
    raise SystemExit(int(not all(m["passed"] for m in metrics)))
