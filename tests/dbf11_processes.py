"""Independent rank processes and coordinator, plus publication/restart fault tests."""
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe, action, scratch = sys.argv[1], sys.argv[2], Path(sys.argv[3])
scratch.mkdir(parents=True, exist_ok=True)


def run(*args, reject=None):
    p = subprocess.run([exe, *map(str, args)], capture_output=True, text=True)
    if reject is None:
        if p.returncode:
            raise RuntimeError(f"{args}: {p.returncode} {p.stderr}")
        print(p.stdout, end="")
    elif p.returncode != 1 or reject not in p.stderr:
        raise RuntimeError(f"expected {reject!r} from {args}: {p.returncode} {p.stderr}")


def generation():
    root = Path(tempfile.mkdtemp(prefix="generation-", dir=scratch))
    for n in (1, 2, 4):
        folder = root / str(n)
        folder.mkdir()
        with concurrent.futures.ThreadPoolExecutor(max_workers=n) as pool:
            jobs = [pool.submit(run, "write", folder, n, r) for r in range(n)]
            for job in jobs:
                job.result()
        run("publish", folder, n)
        run("verify", folder)
    return root


def faults(root):
    run("target_faults", root)
    with tempfile.TemporaryDirectory(dir=scratch) as folder:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            jobs = [pool.submit(subprocess.run, [exe, "write", folder, "1", "0"],
                                capture_output=True, text=True) for _ in range(2)]
            assert sorted(job.result().returncode for job in jobs) == [0, 1]
        run("publish", folder, 1)
        run("verify", folder)
    count = 0
    for fault in ("missing", "truncated", "row", "axis", "schema", "duplicate", "missing_key", "global"):
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            part = Path(temp) / "2"
            shutil.copytree(root / "2", part)
            before = (part / "run.sdbmanifest").read_bytes()
            rst = part / "part_1.sdbrst"
            if fault == "missing":
                rst.unlink()
                # Opening the absent shard must fail in the coordinator before publication.
                message = "truncated/read failure"
            elif fault == "truncated":
                with rst.open("rb+") as f:
                    f.truncate(rst.stat().st_size - 1)
                message = "truncated or extended"
            elif fault == "row":
                with rst.open("rb+") as f:
                    f.seek(-1, os.SEEK_END)
                    byte = f.read(1)[0]
                    f.seek(-1, os.SEEK_END)
                    f.write(bytes([byte ^ 1]))
                message = "row checksum"
                run("verify", part, reject=message)
                # Only the owner of the corrupt row reads that payload.
                outcomes = [subprocess.run([exe, "rank", str(part), "3", str(r)],
                                           capture_output=True, text=True) for r in range(3)]
                assert sorted(p.returncode for p in outcomes) == [0, 0, 1]
                assert any("row checksum" in p.stderr for p in outcomes)
            else:
                rst.unlink()
                (part / "part_1.sdbres").unlink()
                mode = "step" if fault == "axis" else fault
                run("write", part, 2, 1, mode)
                message = {"axis": "generation", "schema": "field schema",
                           "duplicate": "duplicate owned global label",
                           "global": "divergent replicated global payload", "missing_key": "global ownership mismatch"}[fault]
            run("publish", part, 2, reject=message)
            assert (part / "run.sdbmanifest").read_bytes() == before
            count += 1
    print(f"publication_faults={count} target_faults=7 manifest_faults=2 concurrent_retry_winners=1/2 selected_corrupt_row_owners=1/3")


if action == "create":
    root = generation()
    pointer = scratch / (root.name + ".tmp")
    pointer.write_text(root.name, encoding="ascii")
    pointer.replace(scratch / "current")
else:
    root = scratch / (scratch / "current").read_text(encoding="ascii").strip()
    if action == "faults":
        faults(root)
    else:
        run(action, root)
