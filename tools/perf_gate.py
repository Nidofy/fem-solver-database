"""Compare medians from same-host sdb_benchmark JSONL samples."""
import argparse
import json
import math
from pathlib import Path
from statistics import median


KEYS = ("precision", "nodes", "elements", "threads", "repetitions", "steps")
METRICS = ("node_view_ms", "gather_scatter_ms", "explicit_steps_ms", "end_to_end_ms")


def samples(path):
    cases = {}
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        key = tuple(row[k] for k in KEYS)
        if any(type(x) is not int or x <= 0 for x in key):
            raise ValueError("invalid workload identity")
        key += (row.get("pool", "all"), tuple(row.get("cpu_ids", [])),
                row.get("first_touch", False), row.get("tet_kernel","scalar"))
        if row["explicit_hot_allocations"] or row["node_hot_allocations"]:
            raise ValueError(f"unexpected hot allocation in {key}")
        times = [row[k] for k in METRICS]
        if any(type(x) not in (int,float) or not math.isfinite(x) or x <= 0 for x in times):
            raise ValueError(f"invalid timing in {key}")
        cases.setdefault(key, []).append(times)
    if not cases:
        raise ValueError("empty sample file")
    return cases


def compare(before, after, tolerance):
    if not math.isfinite(tolerance) or tolerance < 0:
        raise ValueError("invalid tolerance")
    if before.keys() != after.keys():
        raise ValueError("benchmark case sets differ")
    failures = []
    for key, old in sorted(before.items()):
        new = after[key]
        if len(old) < 3 or len(old) != len(new):
            raise ValueError(f"need matched counts and at least three samples: {key}")
        for i, name in enumerate(METRICS):
            a, b = (median(r[i] for r in samples) for samples in (old, new))
            if b > a * (1 + tolerance):
                failures.append(f"{key} {name}: {a:.4g} -> {b:.4g} ms ({b/a:.2f}x)")
    return failures


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline")
    p.add_argument("candidate")
    p.add_argument("--tolerance", type=float, default=.30)
    args = p.parse_args()
    failures = compare(samples(args.baseline), samples(args.candidate), args.tolerance)
    if failures:
        raise SystemExit("\n".join(failures))
    print("benchmark medians within tolerance")
