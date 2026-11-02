"""Report allowed CPUs or launch an OpenMP benchmark in a selected CPU pool."""
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def windows_topology():
    api = ct.WinDLL("kernel32", use_last_error=True)
    if api.GetActiveProcessorGroupCount() != 1:
        raise RuntimeError("group-spanning affinity requires a group-aware launcher")
    process = ct.c_void_p(-1)
    mask, system = ct.c_size_t(), ct.c_size_t()
    if not api.GetProcessAffinityMask(process, ct.byref(mask), ct.byref(system)):
        raise ct.WinError(ct.get_last_error())
    length = ct.c_ulong()
    query = api.GetSystemCpuSetInformation
    query(None, 0, ct.byref(length), process, 0)
    data = ct.create_string_buffer(length.value)
    if not query(data, len(data), ct.byref(length), process, 0):
        raise ct.WinError(ct.get_last_error())
    count = ct.c_ulong()
    default_sets = api.GetProcessDefaultCpuSets
    default_sets(process, None, 0, ct.byref(count))
    ids = (ct.c_ulong * count.value)()
    if not default_sets(process, ids, len(ids), ct.byref(count)):
        raise ct.WinError(ct.get_last_error())
    allowed = set(ids)
    rows = []
    offset = 0
    while offset < length.value:
        size, kind = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > length.value:
            raise ValueError("invalid CPU set record")
        if kind == 0:
            ident, group, cpu, core, _, numa, efficiency, flags = struct.unpack_from("<IH6B", data, offset+8)
            if (group == 0 and mask.value & (1 << cpu) and (not allowed or ident in allowed)
                    and (not flags & 2 or flags & 4)):
                rows.append(dict(cpu=cpu, core=core, numa=numa, efficiency=efficiency))
        offset += size
    return rows


def topology():
    if sys.platform == "win32":
        return windows_topology()
    rows = []
    for cpu in sorted(os.sched_getaffinity(0)):
        root = Path(f"/sys/devices/system/cpu/cpu{cpu}")
        def read(name):
            path = root / name
            return int(path.read_text()) if path.exists() else None
        node = next(root.glob("node[0-9]*"), None)
        rows.append(dict(cpu=cpu, core=[read("topology/physical_package_id"), read("topology/core_id")],
                         numa=int(node.name[4:]) if node else None, efficiency=read("cpu_capacity")))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pool", choices=["all", "high", "low"], default="all")
    parser.add_argument("--numa", type=int)
    parser.add_argument("--threads", type=int)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    rows = topology()
    if args.numa is not None:
        rows = [r for r in rows if r["numa"] == args.numa]
    if args.pool != "all":
        ranks = {r["efficiency"] for r in rows}
        if None in ranks or len(ranks) < 2:
            parser.error("OS did not expose distinct core classes in the selected domain")
        rank = (max if args.pool == "high" else min)(ranks)
        rows = [r for r in rows if r["efficiency"] == rank]
    if not rows or args.threads is not None and not 1 <= args.threads <= len(rows):
        parser.error("empty pool or thread count outside selected pool")
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    print(json.dumps(dict(pool=args.pool, threads=args.threads, processors=rows)), flush=True,
          file=sys.stderr if command else sys.stdout)
    if command:
        cpus = {r["cpu"] for r in rows}
        if sys.platform == "win32":
            api = ct.WinDLL("kernel32", use_last_error=True)
            mask = ct.c_size_t(sum(1 << cpu for cpu in cpus))
            if not api.SetProcessAffinityMask(ct.c_void_p(-1), mask):
                raise ct.WinError(ct.get_last_error())
        else:
            os.sched_setaffinity(0, cpus)
        env = os.environ.copy()
        if args.threads:
            env.update(OMP_NUM_THREADS=str(args.threads), OMP_DYNAMIC="FALSE")
        return subprocess.run(command, env=env).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
