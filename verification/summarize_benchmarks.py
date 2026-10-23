"""Summarize three sequential local benchmark samples; no third-party dependency."""
import json
import pathlib
import statistics

root = pathlib.Path(__file__).resolve().parent
summary = []
for backend in ("serial", "omp4", "omp8"):
    runs = [json.loads(line) for path in sorted(root.glob(f"benchmark_1m_{backend}_run*.jsonl"))
            for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    for precision in (32, 64):
        samples = [row for row in runs if row["precision"] == precision]
        if len(samples) != 3:
            raise RuntimeError(f"Expected three samples: {backend}/{precision}")
        row = {key: statistics.median(sample[key] for sample in samples)
               for key in samples[0]}
        row["backend"] = backend
        row["samples"] = len(samples)
        row["node_view_raw_ratio"] = row["node_view_ms"] / row["node_raw_ms"]
        row["gather_view_raw_ratio"] = row["gather_scatter_ms"] / row["raw_gather_scatter_ms"]
        summary.append(row)
for row in summary:
    serial = next(s for s in summary if s["backend"] == "serial" and s["precision"] == row["precision"])
    row["explicit_speedup"] = serial["explicit_steps_ms"] / row["explicit_steps_ms"]
    row["explicit_efficiency"] = row["explicit_speedup"] / row["threads"]
(root / "benchmark_summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
for row in summary:
    print(row["backend"], row["precision"],
          "node", round(row["node_view_ms"], 3), "raw", round(row["node_raw_ms"], 3),
          "gather", round(row["gather_scatter_ms"], 3), "raw", round(row["raw_gather_scatter_ms"], 3),
          "steps", round(row["explicit_steps_ms"], 3), "e2e", round(row["end_to_end_ms"], 3),
          "candidate", round(row["candidate_ms"], 3), "batch", round(row["candidate_batch_ms"], 3),
          "speedup", round(row["explicit_speedup"], 2))
