"""Crash the coordinator before/after manifest replacement and reopen in fresh processes."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

publish, reader, generations, scratch = sys.argv[1:]
generations, scratch = Path(generations), Path(scratch)
generation = (generations / "current").read_text().strip()
source = generations / generation / "2"
reference = source / "run.sdbmanifest"
scratch.mkdir(parents=True, exist_ok=True)


def run(*args, rc=0):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    if result.returncode != rc:
        raise AssertionError(f"{args}: {result.returncode}; expected {rc}: {result.stderr}")


def copy_parts(dest):
    for part in source.glob("part_*"):
        shutil.copyfile(part, dest / part.name)


for point in ("temp", "before", "after"):
    with tempfile.TemporaryDirectory(dir=scratch) as folder:
        dest = Path(folder)
        copy_parts(dest)
        manifest = dest / reference.name
        run(publish, reference, dest, "results", "none")
        previous = manifest.read_bytes()
        assert previous != reference.read_bytes()
        run(publish, reference, dest, "both", point, rc=86)
        expected = reference.read_bytes() if point == "after" else previous
        assert manifest.read_bytes() == expected
        run(reader, "verify", dest)
        run(reader, "results_only", dest)
        run(publish, reference, dest, "both", "none")
        assert manifest.read_bytes() == reference.read_bytes()
        run(reader, "rank", dest, 3, 1)

# With one missing and one truncated checkpoint the last results publication
# remains readable. Retry each repair independently before publishing both kinds.
with tempfile.TemporaryDirectory(dir=scratch) as folder:
    dest = Path(folder)
    copy_parts(dest)
    run(publish, reference, dest, "results", "none")
    previous = (dest / reference.name).read_bytes()
    first, second = dest / "part_0.sdbrst", dest / "part_1.sdbrst"
    first.unlink()
    with second.open("r+b") as out:
        out.truncate(13)
    for part in (first, second):
        run(publish, reference, dest, "both", "none", rc=1)
        assert (dest / reference.name).read_bytes() == previous
        run(reader, "results_only", dest)
        shutil.copyfile(source / part.name, part)
    run(publish, reference, dest, "both", "none")
    assert (dest / reference.name).read_bytes() == reference.read_bytes()
    run(reader, "verify", dest)

# Checkpoint and result consumers read their own shard family. Damage on two
# ranks always fails deep verification, including a one-byte torn final row.
for suffix in ("sdbrst", "sdbres"):
    with tempfile.TemporaryDirectory(dir=scratch) as folder:
        dest = Path(folder) / "2"
        shutil.copytree(source, dest)
        for rank in (0, 1):
            p = dest / f"part_{rank}.{suffix}"
            with p.open("r+b") as out:
                out.truncate(8 if rank == 0 else p.stat().st_size - 1)
        run(reader, "verify", dest, rc=1)
        if suffix == "sdbrst":
            run(reader, "results_only", dest)
            run(reader, "rank", dest, 3, 1, rc=1)
        else:
            run(reader, "rank", dest, 3, 1)
            run(reader, "results_only", dest, rc=1)
print("three coordinator crashes, partial-rank retry and two cross-rank truncations passed")
