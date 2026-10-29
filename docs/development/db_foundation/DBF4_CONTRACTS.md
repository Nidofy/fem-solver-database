# DBF4 — phase two durability contract

Baseline: `5349ccfd5f1dff49b87fac88ffa43b61dc5f40ed` (DBF3).
Scope: isolated database library, results CLI and standalone tests. No compression,
new numerical kernels, production integration or wire-format replacement.

## Levels and acknowledgement

`StreamFlush` drains C++ stream buffers and checks stream state. This is visibility
to another process, not a power-loss guarantee. `FileSync` additionally requests
the OS to synchronize the file. `PublicationSync` additionally synchronizes its
parent directory. POSIX supports the latter; Windows rejects this strict level
because this implementation does not provide standalone directory fsync.
Windows atomic checkpoint publication retains its existing flushed temporary file
and `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` behavior. A successful OS call
is not proof of physical media behavior, remote-filesystem semantics or power-loss
survival on a particular device.

Result barriers return the exact flushed byte prefix and last frame only after
the requested operation succeeds. A write/sync failure poisons the writer. Async
barriers execute on the storage-owning worker after queue drain; an acknowledgement
cannot be inferred from queue admission, `written` or the existing `committed`
stream-flush counter. Consumers must explicitly close an async writer and observe
errors before accessing its storage directly.

## Result → Restart → Link

The durable publisher holds the native result writer's lease, establishes a
result barrier, publishes the restart, then publishes the link. CLI checkpoints
use this path, defaulting to FileSync. The optional CLI durability selection
also applies to result-only completion. The existing path-based publisher remains
available for source compatibility; its caller-managed flush/lease precondition
does not provide the new barrier acknowledgement.

These are three ordered file operations, not a multi-file atomic transaction.
After a failed or interrupted operation, reopen and verify the existing exact
prefix/identity link before append. Do not blindly retry or assume the previous
restart survived. Typed errors distinguish failures before replacement from
replacement that succeeded or may have happened, and identify which stage of
the compound publication was reached. No on-disk durability assertion is added:
legacy link schema1 and Native1/Indexed2/TypedIndexed3 bytes remain compatible.

## Evidence obligations

Exercise native and indexed writes, file sync, replace and link publication
faults. Reopen in fresh processes after abrupt termination and verify valid
prefix or explicit refusal; test recovery before mutation. These are process
crash tests, not simulated electrical power loss. Run supported TSan separately
from Windows ASan; record environmental limits without substituting one for the
other. Freeze source for independent Astra/high review, repair findings, run the
CPU regression matrix and publish commands/raw evidence with source identities.
