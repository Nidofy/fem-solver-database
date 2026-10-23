# Layout, ownership and phase protocol

## Fields and IDs

`Id` is an external unsigned 64-bit stable label; `Index` is a compact uint32.
The maximum uint32 value is reserved as invalid. Models exceeding that capacity
fail before narrowing. Fields use size_t sizes and checked multiplication/addition.
Integer fields share the same typed layout machinery as float/double fields.

SoA storage address is `component * count + entity`. AoSoA<W> uses
`block * components * W + component * W + lane` and pads the last block.
The TET implementation uses SoA. Layout support in Field/Mirror does not claim
an AoSoA-optimized TET kernel. `resize` preserves logical entries across changing
strides and invalidates all borrowed views, even if allocation could be reused.
`write` increments content version; resize/assignment/swap changes structural
generation. Checked views hold a weak lifetime token and detect destruction,
resizing and replacement. Raw `View` is a trivially copyable pointer/count pair
with unchecked indexing for kernels. It cannot detect dangling pointers itself.

The registry owns fields and resolves names/types at initialization. Freezing the
registry prevents adding entries, not writing their numerical values. Registry
fields are user extensions; the fixed v2 checkpoint schema serializes StateDB
fields, not arbitrary registry entries. Extend the format/version explicitly when
adding persistent production fields.

MeshDB keeps global CSR connectivity and compact fixed-topology execution batches
with local-node-major connectivity. This intentionally duplicates connectivity to
provide both migration/general topology access and coalesced hot batches. Material
groups split by topology, material ID and IP count. Stress/strain are six-component
SoA over global IPs; material history is packed per element/IP/schema variable.
No universal oversized IP object is allocated. Reference coordinates are immutable;
`current_view` evaluates `reference + displacement` without a redundant field.

## CPU concurrency and memory

There is one owner per mutable state. Create/resize/resolve views before launching
workers. Obtain each writable view once on the controlling thread; workers write
disjoint entities or use an explicitly chosen atomic/reduction algorithm. Join
workers before resizing, checkpointing, trial transitions or rebinding views.
Concurrent calls to `Field::write()` on the same Field, or arbitrary simultaneous
read/write of the same scalar, are **not supported**. This is a phase ownership
contract; Field is not a lock around every numerical load/store.

ContactDB management operations lock their map/pool and return copies, never
unprotected references. Contact candidate reservation uses relaxed atomics because
consumption is after an external producer barrier. The reserved count is not a
publication indicator while producers are running. `append_batch` reserves an
entire span or reports it dropped, amortizing counter contention. No reservation
may cross reset/reserve. Pool handles use monotonically issued generations so a
rolled-back speculative handle cannot match a subsequently created contact.

Arena is a bounded monotonic CPU pool; its reset invalidates all scratch pointers.
Configure thread-local scratch capacity before use; increasing it while pointers
may be live is rejected. These pools own trivially destructible scratch values.
The solver preallocates element-force scratch and fixed adjacency. Serial and
OpenMP deterministic gather use the same summation order per node; the atomic
alternative permits last-bit differences. One execution instance is not reentrant.

Aligned allocator statistics cover aligned vectors, **not** every std::map,
unordered_map or standard vector allocation. Benchmarks additionally record OS
process peak working set. The benchmark's zero hot allocations means zero
instrumented aligned allocations; source inspection confirms the numerical loop
has no STL growth. It is not a global replacement-new allocation trace. NUMA
binding/first-touch placement is not implemented; std::vector value initialization
can first-touch pages on the construction thread. Affinity is caller-controlled.

## CUDA residency

CUDA objects are single-device and single-owner-stream. Mirror uses owned pinned
staging, device storage and an event. State machine:

```
HostLatest --upload()--> Uploading --finish()--> Synced
Synced --device_write()--> DeviceLatest --download()--> Downloading
Downloading --finish()--> Synced --host_write()--> HostLatest
```

Accessors reject stale/pending data. They do not transfer or synchronize implicitly.
Borrowed raw views have the same lifetime restriction as CPU views. `device_write`
marks the next queued kernel's result authoritative; enqueue that kernel on the
owner stream before starting a download. Cross-stream users must explicitly wait
on events in both directions before returning ownership. `Event::wait_on` supports
this dependency. The application must not keep raw host views while a transfer
uses the pinned storage. Scalar values are transferred, never host owner objects.

DeviceBuffer allocates/frees from a private CUDA memory pool in stream order.
Mirror/Candidates/Manifolds teardown waits for owned work to protect pinned memory;
that is a safety fallback, not a hot-loop synchronization strategy. Explicit
`finish`/`wait` report CUDA errors. Destructors cannot throw and count cleanup
errors. Pool reserved high-water is available separately from logical live bytes.
Setup uploads and final download synchronize explicitly; steps only enqueue kernels.

CudaExplicit owns the authoritative GPU dynamic state after construction. The CPU
StateDB used to initialize it is thereafter a snapshot. Download before saving or
reading current results. To restart, load the checkpoint on CPU, construct a GPU
instance from it, and call step (do not call initialize again). CPU-only restart
always yields HostLatest data; no process-local CUDA handle is serialized.

GPU candidate/manifold transfer structs are runtime POD-like records and contain
no pointers; checkpoint serialization still writes individual fields to avoid
ABI/padding dependencies. FaceTable exposes CSR facets for upload with DeviceBuffer.
No contact detection/force algorithm, inter-device copy or MPI transport is included.

## Checkpoint format 2

All scalar encodings are little-endian. An 8-byte magic precedes ordered chunks.
Each chunk has uint32 ID, uint64 payload length, uint32 CRC32 and its payload.
Chunk 0 contains version, scalar width, index width, endian marker, complete frozen
model fingerprint, step, time, dt, rotation and contact-presence flags. Field chunks
contain entity count, component count, scalar width and component-major values.
Contact chunk 100 holds both face/side identities, feature IDs, generation, last-seen
step, activity and history. Final chunk ID 0xffffffff has an empty body. Counts,
chunk order, checksum, finiteness, shape, activation and mass consistency are checked.

The same frozen model must be rebuilt before restoring. The canonical FNV-1a model
fingerprint includes labels, coordinates, DOFs, connectivity, materials, properties,
constraints, sets and surfaces. CRC/fingerprint protect accidental mismatch/corruption;
they are not cryptographic authentication. Rotation, stress, strain, activation,
forces and packed history are persistent; execution scratch, candidate lists, CSR
tangent/residual and trial backups are reconstructed. Saving an active trial fails.

Save writes a uniquely named sibling temp file, flushes buffers to the OS/device,
closes it, then atomically replaces the destination (`MoveFileExW` on Windows,
rename plus directory fsync on POSIX). A process interruption before replace leaves
the old destination and may leave a temp file. This is not a guarantee against
faulty storage hardware; actual power-loss injection was not tested. Restore checks
the complete file before publishing state. Default input size ceiling is 4 GiB,
caller configurable. Current implementation assembles complete file/chunks in RAM;
large checkpoints can have significant transient memory overhead.
