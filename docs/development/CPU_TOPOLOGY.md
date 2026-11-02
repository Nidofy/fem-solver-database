# Local placement measurements

Use tools/cpu_topology.py to inspect the allowed CPUs, or launch a program after
restricting its inherited affinity, e.g. --pool high --threads 4 -- PROGRAM ARGS.
The class ranking is relative to OS-reported efficiency (Windows) or capacity
(Linux). The Windows launcher supports a single processor group. WSL on this
host exposes 12 CPUs of equal capacity and cannot distinguish host core types.

AlignedAllocator offers opt-in parallel first writes on large allocations via
the allocating thread's parallel_first_touch flag. The benchmark selects this
with the final "omp first-touch" arguments. Workers touch at 4 KiB intervals
before vector initialization. Physical NUMA placement depends on affinity,
allocator page reuse and OS policy. This host has one visible NUMA domain.

verification/profile_core_pools.py runs three interleaved samples per configuration
and preserves the original kernel JSON plus pool, affinity and first-touch mode.
verification/core_pools_windows_gcc15.jsonl records 66 FP32/FP64 rows on Windows
GCC 15.2 (-O2 -fopenmp), 1M nodes, 20 repetitions. FP32 4-thread node-loop medians
were 11.91 ms in the six-CPU high pool and 27.01 ms in the ten-CPU low pool.
In the unrestricted pool, the medians at 4, 8 and 16 threads were 11.49, 8.98
and 7.70 ms. Placement measurably affects the kernel. The historic 8-thread
regression did not reproduce in this run, and its original cause remains open.
