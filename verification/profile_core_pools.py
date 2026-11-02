"""Run sequential, interleaved core-pool trials. Stdout is evidence JSONL."""
import argparse
import json
from pathlib import Path
import random
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
launcher = [sys.executable, str(root/"tools/cpu_topology.py")]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("benchmark", type=Path)
    p.add_argument("--nodes", type=int, default=1000000)
    p.add_argument("--repetitions", type=int, default=20)
    p.add_argument("--samples", type=int, default=3)
    args = p.parse_args()
    cpus = json.loads(subprocess.check_output(launcher))["processors"]
    ranks = {c["efficiency"] for c in cpus}
    configurations = [("all", n, False) for n in (1, 2, 4, 8, 16) if n <= len(cpus)]
    if None not in ranks and len(ranks)>1:
        for pool, rank in [("low", min(ranks)), ("high", max(ranks))]:
            size = sum(c["efficiency"] == rank for c in cpus)
            configurations.extend((pool,n,False) for n in (2,4,8) if n<=size)
    configurations.append(("all", max(n for _,n,_ in configurations), True))
    for sample in range(args.samples):
        random.Random(sample).shuffle(configurations)
        for pool, n, touch in configurations:
            command = launcher+["--pool",pool,"--threads",str(n),"--",str(args.benchmark.resolve()),
                               str(args.nodes),str(args.repetitions),"omp"]
            if touch: command.append("first-touch")
            result = subprocess.run(command, text=True, capture_output=True)
            if result.returncode:
                sys.exit(result.stderr)
            topology = json.loads(result.stderr.splitlines()[0])
            for row in result.stdout.splitlines():
                sample_row = json.loads(row)
                if sample_row["threads"] != n or sample_row.get("first_touch",touch) != touch:
                    raise RuntimeError("benchmark did not execute requested placement configuration")
                sample_row.update(sample=sample,pool=pool,first_touch=touch,
                                  cpu_ids=[c["cpu"] for c in topology["processors"]])
                print(json.dumps(sample_row),flush=True)


if __name__ == "__main__":
    main()
