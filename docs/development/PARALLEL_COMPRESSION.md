# Ordered indexed4 compression

NativeResultStorage accepts an optional trailing compression_workers argument
(1 by default, 2 through 64 for parallel encoding). Each worker owns one raw
entity block. The writer drains futures in field and block order and publishes
the footer only after all blocks are emitted. Scheduling and worker count are
runtime properties; the v4 result format and append codec policy stay compatible.

Admission for parallel mode reserves one codec workspace, one raw block and
the existing stored/envelope allowance per worker. Construction rejects counts
that exceed the configured storage budget; AsyncResultWriter also charges the
full reserve per admitted frame. These bounds cover codec/block scratch, not
the caller's result frame, thread stacks, model, or the field sort index.
Use the same published prefix to compare serial vs parallel append byte-for-byte
(including identical footer ancestor offsets and nonce).

Windows GCC 15: DBF5 compressed integration passed 63 checks with zstd enabled
and disabled, including model identity, ordered append bytes, native
field bit patterns, truncation recovery and admission rejection.
