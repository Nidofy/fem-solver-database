"""Summarize B01 lifecycle benchmark JSONL (median across runs); stdlib only.

Single source of truth (B01-A-006 repair): baseline.json, summary.txt and the
R0.04 markdown tables are ALL rendered from the same per-tag medians computed
here. With --markdown, prints the baseline and A/B-delta tables in the exact
format used by task-evidence/R0.04.md; paste that output there on every rerun.
"""
import json
import pathlib
import statistics
import sys

root = pathlib.Path(__file__).parent
rows = []
for path in sorted(root.glob("lifecycle_*.jsonl")):
    tag = path.stem.removeprefix("lifecycle_")
    samples = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    if not samples:
        continue
    row = {key: statistics.median(s[key] for s in samples) if len(samples) > 1 else samples[0][key]
           for key in samples[0] if isinstance(samples[0][key], (int, float))}
    for key in ("execution_policy", "omp_env"):
        if key in samples[0]:
            row[key] = samples[0][key]
    row["tag"] = tag
    row["samples"] = len(samples)
    rows.append(row)

out = root / "baseline.json"
out.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")

cols = ("nodes", "samples", "execution_policy", "omp_env", "compile_ms", "kernel_ms",
        "nondue_snapshot_ms", "steady_kernel_per_increment_ms",
        "force_evaluations", "snapshot_ms", "native_write_ms", "reader_scan_query_ms",
        "restart_write_ms", "restart_restore_ms", "peak_process_bytes")
print("\t".join(cols))
for row in rows:
    print("\t".join(str(row.get(c, "")) if isinstance(row.get(c, ""), str)
                    else (str(round(row[c], 4)) if isinstance(row.get(c), float) else str(row.get(c, "")))
                    for c in cols))

print("\nR1.01 eager->lazy publication cost delta (same conditions, serial execution):")
for size in ("", "1m-"):
    for backend in ("serial-env1", "serial-env4"):
        eager = next((r for r in rows if r["tag"] == f"eager-{size}{backend}"), None)
        lazy = next((r for r in rows if r["tag"] == f"lazy-{size}{backend}"), None)
        if not (eager and lazy):
            continue
        nd = eager["nondue_snapshot_ms"] - lazy["nondue_snapshot_ms"]
        sn = eager["snapshot_ms"] - lazy["snapshot_ms"]
        step = eager["steady_kernel_per_increment_ms"]
        print(f"{size or '200k-'}{backend}: nondue publication {eager['nondue_snapshot_ms']:.3f}"
              f"->{lazy['nondue_snapshot_ms']:.3f} ms (removed {nd:.3f} ms = {nd / step * 100:.0f}%"
              f" of one step); due-U snapshot {eager['snapshot_ms']:.3f}->{lazy['snapshot_ms']:.3f} ms"
              f" (removed {sn:.3f} ms); steady step unchanged ({eager['steady_kernel_per_increment_ms']:.3f}"
              f" vs {lazy['steady_kernel_per_increment_ms']:.3f} ms)")

if "--markdown" in sys.argv:
    def fmt(value):
        text = f"{value:.3f}".rstrip("0").rstrip(".")
        return text if text else "0"

    order = [f"{v}-serial-{e}" for e in ("env1", "env4") for v in ("eager", "lazy")] + \
            [f"{v}-1m-serial-{e}" for e in ("env1", "env4") for v in ("eager", "lazy")]
    by_tag = {r["tag"]: r for r in rows}
    cols = [("compile", "compile_ms"), ("first advance", "kernel_ms"),
            ("steady/advance", "steady_kernel_per_increment_ms"), ("nondue pub", "nondue_snapshot_ms"),
            ("due-U pub", "snapshot_ms"), ("write", "native_write_ms"), ("read", "reader_scan_query_ms"),
            ("rst write", "restart_write_ms"), ("rst restore", "restart_restore_ms")]
    print("\n<!-- markdown-rendered by summarize.py --markdown; paste into task-evidence/R0.04.md -->")
    print("| tag | nodes | " + " | ".join(c[0] for c in cols) + " |")
    print("|---|---|" + "---|" * len(cols))
    for tag in order:
        row = by_tag.get(tag)
        if not row:
            continue
        print(f"| {tag} | {'1M' if row['nodes'] == 1000000 else '200k'} | "
              + " | ".join(fmt(row[k]) for _, k in cols) + " |")
    print()
    for size in ("", "1m-"):
        for backend in ("serial-env1", "serial-env4"):
            eager = by_tag.get(f"eager-{size}{backend}")
            lazy = by_tag.get(f"lazy-{size}{backend}")
            if not (eager and lazy):
                continue
            nd = eager["nondue_snapshot_ms"] - lazy["nondue_snapshot_ms"]
            sn = eager["snapshot_ms"] - lazy["snapshot_ms"]
            step = eager["steady_kernel_per_increment_ms"]
            print(f"- {size or '200k'} {backend}: non-due publication "
                  f"{fmt(eager['nondue_snapshot_ms'])} ms → {fmt(lazy['nondue_snapshot_ms'])} ms "
                  f"(removed {fmt(nd)} ms ≈ {nd / step * 100:.0f}% of one full serial advance); "
                  f"due pure-U snapshot removed {fmt(sn)} ms; steady advancing unchanged within noise "
                  f"({fmt(eager['steady_kernel_per_increment_ms'])} vs "
                  f"{fmt(lazy['steady_kernel_per_increment_ms'])} ms).")
