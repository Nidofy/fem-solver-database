"""Summarize B01 lifecycle benchmark JSONL (median across runs); stdlib only."""
import json
import pathlib
import statistics

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
