"""Aggregate the retained three repetitions; no timings are synthesized."""
import json
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parent


def stats(values):
    return {"median": statistics.median(values), "min": min(values), "max": max(values)}


stage_groups = {}
for trial in range(1, 4):
    rows = [json.loads(line) for line in (ROOT / f"stages-{trial}.jsonl").read_text().splitlines()]
    assert len(rows) == 12
    for row in rows:
        key = f"{row['fixture']}/{row['precision'] * 8}bit/{'async' if row['async'] else 'sync'}"
        stage_groups.setdefault(key, []).append(row)
stages = {}
for key, rows in stage_groups.items():
    assert len(rows) == 3
    stages[key] = {name: stats([row[name] for row in rows]) for name, value in rows[0].items()
                   if isinstance(value, (float, int)) and not isinstance(value, bool)}
large_rows = [json.loads((ROOT / f"large-{trial}.jsonl").read_text()) for trial in range(1, 4)]
large = {key: stats([row[key] for row in large_rows]) for key, value in large_rows[0].items()
         if isinstance(value, (float, int))}
index_rows = []
for trial in range(1, 4):
    data = (ROOT / f"index-{trial}.txt").read_text()
    index_rows.append({key: float(value) for key, value in re.findall(r"(\w+)=([0-9.]+)", data)})
indexed = {key: stats([row[key] for row in index_rows]) for key in index_rows[0]}
result = {"repetitions": 3, "cache": "OS cache not evicted; reopen is warm-cache",
          "execution_policy": "serial", "stages": stages, "large_legacy_tet": large,
          "indexed_100000_frames": indexed}
(ROOT / "performance-summary.json").write_text(json.dumps(result, indent=2) + "\n")

def cell(values):
    return f"{values['median']:.4g} [{values['min']:.4g}, {values['max']:.4g}]"


lines = ["# ALR1 measured performance", "",
         "Three repetitions; cells are median [minimum, maximum], milliseconds unless noted.",
         "GCC 15.2 Release, Intel Core Ultra 9 285H; CpuRun executes serial, even in the OpenMP build.",
         "OS cache was not evicted. Setup/first-step/steady-state are separated; no cold-disk claim.", "",
         "## Indexed lifecycle stage harness", "",
         "Tiny fixtures exercise I/O overhead and all integration paths; they do not establish scalable kernel throughput.",
         "Every row writes 21 frames. TET has 5 nodes/2 elements/2000 increments; HEX has 8/1/1000; mixed has 9/2/1000.",
         "All 36 runs internally verify exact restart state equality.", "",
         "| Case | End to end through output drain | Snapshot total | Write/admit | Drain | Warm reopen | Random 20 frames | Node history |",
         "|---|---|---|---|---|---|---|---|"]
for key, values in stages.items():
    names = ["end_to_end_output_ms", "snapshot_total_ms", "write_or_admit_ms", "flush_ms",
             "warm_cache_reopen_ms", "random_20_frames_ms", "node_history_ms"]
    lines.append("| " + key + " | " + " | ".join(cell(values[name]) for name in names) + " |")
lines += ["", "End to end includes parse, compile, plan, initialize, storage setup, run and drain.",
          "Checkpoint, reader and restart timing are separate. Standalone v1 encode diagnostics run outside this interval.",
          "Sync write time and async admission time measure different operations; use end-to-end+drain for comparison.",
          "The JSON includes every measured stage, file bytes, reader bytes, decoded bytes, queue charge and process peak.",
          "The process peak is cumulative across cases in a process, not attributable to one phase.", "",
          "## Independent 20,000-node / 5,000-TET legacy workload", "",
          "Double, serial, 10 executed increments, 1 output frame; native v1. Compiled OpenMP maximum=4 is not its execution policy.", "",
          "| Metric | Median [min, max] |", "|---|---|"]
for name in ["compile_ms", "run_setup_ms", "initialize_ms", "kernel_only_ms",
             "steady_kernel_per_increment_ms", "snapshot_ms", "native_write_ms",
             "reader_scan_query_ms", "restart_write_ms", "restart_restore_ms", "peak_process_bytes"]:
    lines.append(f"| {name} | {cell(large[name])} |")
lines += ["", "## Independent 100,000-frame persistent-index workload", "",
          "| Metric | Median [min, max] |", "|---|---|"]
for name in indexed:
    lines.append(f"| {name} | {cell(indexed[name])} |")
lines += ["", "Five random single-entity queries decode 120 bytes total. An independent 100,000-node field test",
          "asserts that a selected entity decodes no more than its 256-row block. See indexed_results_tests.cpp.",
          "The additional index-memory.json run measures whole-test process peak, including recovery and malicious-input controls.",
          "Reader counters count requested encoded bytes (including metadata), not physical disk sectors or OS-cache misses.",
          "Legacy v1 reader counters are not instrumented. CUDA transfer/VRAM measurements are NOT_RUN.",
          "No raw/view speedup, commercial solver comparison, compression throughput or power-loss durability is claimed.", ""]
(ROOT / "PERFORMANCE.md").write_text("\n".join(lines))
print("Aggregated 36 stage runs, 3 large-model runs and 3 100000-frame runs.")
