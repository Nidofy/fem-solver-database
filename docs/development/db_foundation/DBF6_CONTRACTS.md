# DBF6: standalone data, DOF, state and consumer contracts

Date: 2026-10-30. Baseline: `b489414`. Package version: 2.2.0.
This is an additive standalone implementation. Native DB, Felem, ZDF, solver
SDKs and production numerical kernels are not dependencies. Existing mechanical
MeshDB/StateDB, consumer v0, and existing wire formats remain available.

## Responsibility and coverage

`data_contract.hpp` provides a versioned `DataCoverage` contract. Capture supplies
the complete list of populated source keys; every key must have exactly one
`DataResponsibility` declaring target, owner, semantics, schema version and
evidence. Missing, duplicate and extra entries reject. Unsupported populated data
rejects by default; `allow_unsupported=true` is for an inventory report only.
This proves declaration completeness, not numerical equivalence of an adapter.

`DBF6_LEGACY_INVENTORY.json` inventories all 25 subfiles and 2,047 declared fields
of the inspected DBSCHEMA.ASC. Counts match for every subfile. Each group has a
proposed target boundary and remains explicitly **not qualified as a native
field mapping**. Reproduce offline with `tools/legacy_schema_inventory.py SOURCE
OUTPUT`; the source path is not part of the runtime or CMake dependencies.
Control blocks, FNL/FEG and native work records also need a populated-key capture
inventory when their corresponding production adapter is developed.

| Legacy responsibility | Current independent owner / migration rule |
| --- | --- |
| Identity, coordinates, connectivity, sets | Existing MeshDB/ModelIR/SourceMap; new consumer topology metadata for externally defined families |
| Material/section/orientation | Existing ModelFoundation; consumer material/section typed parameter/table catalogs and explicit sample addresses |
| DOF, equation numbering, local/internal variables | DofSpace; external numbering, local transforms and elimination are supplied by the consumer |
| Trial/history/thermal/contact/sample values | Existing RegisteredState, extended with explicit layouts and optional context identities |
| Matrix values and structure | Existing CSR/BSR; backend factorization, assembly and solver strategy remain consumer-owned |
| Loads, analysis configuration, step cursors | Existing ModelIR where supported; otherwise versioned consumer configuration and explicitly registered required state |
| Results and restart | Existing typed results/checkpoint/restart; FoundationStore for independent registered state plus DOF envelope |
| Native scratch/cache/file SDKs | Consumer/input/output adapter; no native memory image is a portable schema |

## DOF space v1

`dof_space.hpp` defines immutable `DofSpaceData -> DofSpace`. It is independent of
MeshDB's mechanical 3/6-DOF convention. `DofVariable` declares ID, quantity,
component count, units and basis. Quantities include translation, rotation,
temperature, pressure, multiplier and a named user quantity. There is no implicit
unit or basis conversion.

`DofKey = owner(Node/Element/Global), entity, local, variable, component`:
entity indices are zero based; Global entity is zero; Node local is zero; Element
local identifies an internal slot. Model node/element counts validate membership.
Variable IDs need not be dense. Keys are unique.

Bindings are Free, Prescribed, Inactive or Eliminated. A Free binding owns exactly
one canonical zero-based equation. All canonical equations are owned exactly
once. Prescribed/Inactive explicitly store a value, including nonzero inactive
values. Eliminated stores an affine offset plus sorted, unique, finite nonzero
terms referencing **Free slots only**. The external solver must flatten chains;
the database rejects cycles/chains rather than solving an MPC system.

`external_equations` preserves arbitrary distinct original labels in canonical
order. `solver_to_canonical` is a complete permutation; inverse mapping is
prepared once. No legacy zero/negative encoding is interpreted. `gather` evaluates
the declared affine map; `scatter` evaluates its transpose in slot order, omitting
prescribed/inactive contributions. Original slot forces remain available for
consumer reaction recovery. These convenience functions return owned doubles;
hot kernels can reuse the immutable mapping with their own scalar/scratch policy.
Nonfinite input and arithmetic overflow reject without changing caller output.

Encoding uses the existing bounded, fixed-width little-endian native codec.
Model/revision/variable/numbering/value/relation changes change the identity.
`DofMap::contract(mesh)` exports the existing mechanical map, preserving prescribed
values and checking the original mesh identity; it does not renumber it.

## Registered state with explicit layouts

`RegisteredState` is reused directly for a lightweight standalone state; it does
not allocate StateDB's mechanical vectors. `StateLayout` supplies immutable
explicit `ResultAddress` rows and one of:

- Dense: domain equals row count; no offsets/indices.
- Ragged: domain+1 monotonic offsets from zero to row count; empty segments allowed.
- Sparse: sorted unique domain indices; omitted rows are absent, not zero.

Values remain existing flat typed `Field<T,C,Layout>` arrays; SoA/AoSoA and all
seven scalar lanes are retained. Sparse absence and per-scalar validity are
separate. Existing canonical-zero validity rules remain enforced. There is no
second field allocator or per-value variant in a kernel loop.

Addresses reuse result conventions: node/element/face/local-point IDs are one
based; Global is the all-zero address. Contact has domain=1, entity=0, nonzero
interaction/generation/point and two distinct qualified endpoints. Face side is
explicit. Inactive address lanes must be zero. IP layer and section-point axes
must be either both present or both absent. `StateDB::register_addressed_field`
also checks mesh/local-node/face/sample/endpoint membership at registration.

The generic registry validates address structure; its caller owns the external
entity domain. `StateDB` registration validates against its MeshDB. Direct generic
remapping is a phase-boundary operation; native adapters must validate membership
in their current model before supplying replacement addresses.

`remap(name,new_layout)` stages new storage, retains values/validity only for exact
full address matches, and initializes new identities. Reused contact slots with
new generations do not inherit history. Removed identities disappear. Allocation
or validation failure leaves the old entry intact. A successful replacement
invalidates old field borrows even when row count is unchanged.

Standalone `begin_trial/commit/rollback` includes fields and addressing. A failed
commit validation retains the trial so callers can correct or roll back. Use one
transaction owner: StateDB users normally use StateDB's existing transaction,
not a nested independent registry transaction. Persistence and snapshot publication
reject active registered trials. Trial backup is a deep copy; dirty-block/COW
optimization is not claimed.

## Identity and persistence

Before freezing, `bind_context(model, dof_identity, consumer_identity=0)` binds the
registry. A generic Batch identity covers topology, formulation, DOFs, sample
layout, materials and sections. Bind batch-specific states to `batch.identity()`
and use Batch overloads of `bind_read`, `bind_write` and `snapshot`. Space-only
overloads require consumer_identity=0 and are suitable for a caller-managed
neutral state contract. Hashes detect accidental mismatch, not hostile forgery.

Registered manifest v1 bytes are unchanged when there are no explicit addresses
and no context. New manifests are v2: version/count, model/equation/consumer
identities, then existing field manifests and optional addressing. Existing
checkpoint and RestartDB already compare exact manifests, so addressed state
uses their established staged restore protocol. Old readers without matching
registration reject new layouts; no broad forward-reader claim is made.

`io/foundation_store.hpp` composes DOF metadata and registered state into one file:
12-byte native prefix, kind 1 DOF record, kind 200 registered manifest and kind
201 Required-field blocks (at most 4096 scalars/block). Existing CRC and atomic
publication helpers are reused. Restore validates the exact DOF data, stages an
initialized registry, reads/checks all blocks and EOF, then publishes once.
Reconstruct/Omit values return to declared initializers; required values restore.
Expected field types, initialization and address layout must be registered first.
`inspect_space` reads DOF metadata only and is not authorization to restart.

The FoundationStore budget covers encoded record bytes excluding the 12-byte
prefix. It is not a global peak-RSS budget: caller registration, staging, decoded
containers and manifests have their own memory costs. The existing 256-MiB native
default is configurable. File-sync/atomic replacement inherits existing platform
guarantees, not physical power-loss certification. A separately saved Batch
snapshot and state file are not a cross-file atomic transaction; expected Batch
identity prevents accepting mismatched definitions.

## Consumer v1

`consumer_contract.hpp` includes no dynamics/HEX/TET execution plans. `Batch` owns
immutable shared metadata: versioned arbitrary local topology tables, formulation
name/version, connectivity, element identities, explicit DOF slot lists, sample
addresses, and material/section catalog indices. It checks ranges, duplicate keys,
membership and references before construction. New topology names are storage
contracts; they do not enable a numerical kernel. `require`/`require_material`/
`require_section` are the external consumer's exact capability gates.

Material metadata reuses `MaterialDefinition`. Sections reuse the same typed
parameter/table payloads under a named section schema. Translation from existing
shell/beam/solid declarations is consumer-owned; schema-specific physical checks
(for example a particular section's admissible geometry) remain with that schema's
consumer. The database validates finite values, typed lanes, shapes, tables and
declared bounds, and never guesses a constitutive law or quadrature rule.

Batch snapshots roundtrip DOFs, topology, material and section metadata through
the existing native codec. Field bindings compare descriptor bytes, scalar/layout
type, schema/version, rows, basis, explicit addresses and state context. Values are
borrowed through CheckedView; metadata is prepared outside the loop. `AccessSet`
validates lifetime and disjoint read/write and write/write allocation ranges at
batch entry. Read/read sharing is allowed; private thread scratch/output fields
must be distinct. Concurrent writes to one owner remain unsupported.

Resolved raw views do not pin memory or lock threads. Owners must remain alive
and must not resize/move/remap/restore during a batch. Callers join all consumers
before structural mutation. Existing generation checks detect stale entry, not a
mid-loop race. Output snapshots own typed values, validity and addresses and have
an admission budget. Existing ResultPublisher now also publishes explicitly
addressed registered state through existing typed result files.

## Explicit boundaries

This batch closes the independent contracts and their tested persistence paths.
It does not assert all 2,047 legacy fields have qualified translations. Native
Felem adapters, broad INP parsing, ModelIR thermal/frequency analysis semantics,
complex/modal axes, all old element algorithms, local-basis transformation
algorithms, matrix role catalogs, cross-owner production restart orchestration,
CUDA and measured old-solver acceleration remain separately qualified work.
Existing production numerical behavior and root build files are unchanged.

Tests and final evidence are in `DBF6_HANDOFF.md`.
