# DBF2 model, state and result closure

This continues DBF1 on `codex/astra-db-foundation-completion` after the independent
Astra/high review of DBF1. It is additive standalone database development. The
production solver and its build files are outside this change.

## Identity and layout

`types.hpp` provides explicit, trivially copyable `StrongId` wrappers for external
labels, nodes, elements, instances, materials, sections, surfaces, fields, steps,
frames, coordinates, orientations, constraints and loads. Default values reserve
the maximum underlying unsigned integer as invalid. Internal IDs remain 32 bit;
external/frame IDs use 64 bit. No Index64 runtime is claimed. Existing numerical
APIs retain their aliases; SourceMap, FieldRegistry and DofMap add typed boundaries.
The legacy raw-label API can retain UINT64_MAX; conversion to the new ExternalLabel
wrapper explicitly rejects that reserved sentinel instead of returning an invalid
wrapper for a live entity. Result readers and writers validate dense internal IDs,
indices, nonzero external labels and unique scoped source keys.

SourceMap resolves `(part, instance, external label)` in both directions. Runtime
reordering changes layout indices, not those source identities. Semantic identity
canonicalizes identity-keyed definition order and removes source text locations.
Connectivity, step order, history component order, layer order and table axes
remain meaningful. `semantic_identity()`, `analysis_identity()` and
`layout_identity()` are separate queries. Existing ModelStore identity remains
the strict serialized definition hash for backward-compatible restart matching.
Reordering therefore does not authorize restoring an old layout without remapping.
All hashes are accidental-mismatch detectors, not authentication.

The FieldRegistry assigns stable insertion IDs and checks type on retrieval.
It remains a host registry; attaching an arbitrary registry entry does not enroll
it in StateDB or automatically make it persistent.

CSR/BSR reuse SparseMatrix, adding a portable DTO with scalar type, 32-bit columns,
64-bit offsets/row count, block size, and general/symmetric flag. Sorted unique
columns, shapes, finite values and transpose equality for symmetric matrices are
validated. The zero-row matrix is legal. Encoding preflights its byte budget.
Decode validates scalar count before constructing backing storage and bounds the
reconstruction bytes separately; a short corrupt DTO cannot trigger a large block
matrix allocation before rejection.

## ModelIR schema 3

Schema 3 adds `ModelFoundation` after the existing schema-2 history bindings.
Schema 1/2 serialization has no new trailing bytes. The model compiler resolves
the following cold definitions to typed IDs without hot-loop string dispatch:

| Definition | Persisted meaning and validation |
|---|---|
| Coordinates and orientations | Parent graph, origin, right-handed orthonormal rotations; missing parents and cycles reject |
| Material parameters | Schema/version/algorithm, named scalar lanes, shapes, units and optional bounds; unsigned 64-bit values never pass through double |
| Material tables | Ordered axes, Cartesian shape, component count, interpolation and extrapolation policy |
| Solid/shell/beam sections | Scoped assignments; shell thickness, offset, layer materials/angles/section points; beam area/inertias/torsion/orientation |
| Constraints | Linear equation node/DOF/coefficient terms, tie surface references, rigid reference/dependent node regions |
| Loads | Nodal force, face pressure/traction, body acceleration/force density, direction/coordinate/amplitude/follower flag and step update events |
| Initial/activation state | Named node/DOF initial quantities with validity; element region activation events |

Integer bounds encoded as doubles are restricted to the exact binary64 integer
range. Unbounded int64/uint64 values retain their full range. Material table
metadata is stored and validated; no new constitutive table evaluator is claimed.
Unknown declared material algorithms have no fabricated elastic constants and
are storage-only. All nonempty foundation definitions currently require the
storage-only execution gate; a definition is not proof of an executable kernel.

Schema-3 DOFs are per node: nodes incident to shell/beam elements receive rotations;
solid-only nodes retain translations. Old schemas retain their established layout.
The compiler owns source-to-runtime mappings; no typed wrapper changes the hot
index representation.

The controlled INP addition is root/assembly explicit-node `*EQUATION` and root
rectangular `*ORIENTATION` (including an additional axis rotation). Node-set
equation expansion and cylindrical/spherical orientations reject. Other new
foundation families are currently programmatic ModelIR APIs. Existing input
material/section/load subsets remain available; this is not a full Abaqus parser.
Primary syntax references:
[EQUATION](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-equation.htm),
[ORIENTATION](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-orientation.htm).

## State and restart

MeshDB can freeze sample points `(integration point, layer, section point, material)`.
Layer and section point are both zero for ordinary integration points or both
positive for layered samples. Duplicate addresses reject. StateDB packs histories
using each sample's actual material schema; the additional per-point history
offset pool is allocated only for explicit sample layouts. Pure legacy solid
models do not allocate that pool or rotation fields. Named address lookup resolves
sample and variable/component before returning an offset.

History schemas remain model-scalar FP32/FP64 persistent committed values; generic
integer result fields do not imply integer constitutive-history storage. StateDB's
existing trial/rollback contract applies. Derived/transient registry entries are
not silently included in restart. Persistent ContactDB history and rebuildable
candidate caches retain the existing contact contracts.

The restart container magic stays unchanged. Translation-only saves use state
schema 3 with the existing fourteen fields. State schema 4 appends all six rotation
fields (angle, velocity, acceleration, inertia, inverse inertia, torque), and requires
a destination constructed with rotation capability. Mismatched capability rejects.
Load validates into a temporary StateDB and publishes only after all blocks, end
record, activation flags and mass/inertia inverse relationships validate. An older
reader rejects schema 4. No device pointer or residency state is serialized.

## Typed indexed results

`ResultFormat::TypedIndexed3` extends the existing indexed ResultDB and reuses its
commit/footer/index/recovery machinery. Native1 and Indexed2 bytes remain compatible.
The typed format has distinct magic `0x33534552424453`, contains schema-described
fields, and binds frames to mesh, exact definition, configuration and time/step.
Seven scalar types are supported: float32/64, int32/64, uint32/64 and uint8.
`TypedValues` selects a homogeneous vector per field, not a variant per value.

Full ResultAddress stores entity, IP, layer, section point, element node, face/side,
or contact domain/interaction/generation/point and two stable source endpoints.
Entity IDs are one-based runtime IDs resolved by the persisted SourceMap. Local
point indices are one-based; zero means absent. Contact endpoint kind 0 is node,
1 is element node and 2 is element face. Contact addresses never persist transient
ContactHandle slots. Global samples use the all-zero address.

Validity is per scalar, empty means all valid. Invalid values use canonical zero;
zero with validity 1 is a measured zero. Floating values must be finite. Field
descriptors preserve ordered tensor components, shear convention, units, basis,
configuration, physical time and restart/persistence policy. A basis reference
resolves to the file's right-handed rotation catalog. Compiler orientation IDs map
to result IDs `1 + orientation ID`; nonzero coordinate IDs map to `2^32 + ID`.
ID zero denotes global/unspecified basis.

`typed_field` and `typed_history<T>` support exact full addresses, entity filters,
history-schema ID and exact stored-region selection. Ambiguous names reject. A
history query compares the complete field schema, schema ID/version, basis and
region between contributing samples, rejecting changes such as tensor-component
reordering. Per-frame reads still expose each explicitly stored schema. Conflicting
same-key descriptors within a frame reject. Ordinary legacy `field`
queries do not silently coerce typed integers into double. Full-address sorted
blocks allow selective lookup when hundreds of layer points share one entity.
`snapshot_history` captures owned committed values without advancing state; its
default clock uses the committed state time/increment. Analysis step may be supplied
explicitly. Async admission charges include vector capacities, addresses, metadata,
encoding and validation scratch; that charge is not process RSS.
Admission accounting allocates no validation tree. Payload validation happens in
the worker after the owning frame and its scratch allowance have been admitted.

`sdb_run --typed-indexed` selects the new container for existing CPU output.
`--read file --typed --field NAME` prints exact scalar values, validity and address
columns. The CLI deliberately emits full components/locations for typed fields;
the C++ query API supplies finer address/schema filtering. Existing VTK/XDMF
exporters reject typed files before creating output rather than dropping metadata.

## Compatibility and environment boundary

| Object | Old formats | New format | Migration |
|---|---|---|---|
| Model | schema 1 and 2 | schema 3 foundation tail | New reader accepts all; frozen old bytes tested |
| Results | Native1 / Indexed2 | TypedIndexed3 | New reader dispatches by magic; old writer rejects typed payloads |
| Lifecycle restart | state schema 3 | state schema 4 for rotations | Explicit capability matching; no implicit downcast |
| Low-level checkpoint | existing v2 | no version change | Sample semantics included in model fingerprint |
| Matrix DTO | no portable schema | schema 1 | New isolated API; existing apply path unchanged |

Unknown required schemas/record types reject. There is no general unknown-optional
extension retention mechanism or compressed codec. Existing atomic replacement
and committed-prefix crash recovery do not constitute a power-loss durability
guarantee. Resource limits are enforced by the existing native decoder and indexed
directory/block budgets, with additional typed payload checks.

The composite is explicitly a synthetic-storage-fixture, including two transformed
instances, mixed solid/shell/beam integration rules, layer histories, all result
associations, local tensors, exact integers, missing values, constraints, loads,
curves and activation. Its manually assigned values are storage oracles. Real CPU
physics regression remains the established linear TET/HEX lifecycle pipeline.
No new physical kernel, nonlinear material algorithm, MPI, GPU algorithm, general
unit algebra, arbitrary dynamic StateDB field persistence, or full INP grammar is
claimed. CUDA and production integration are **not locally compiled / not locally
verified**; downstream headers changed and consumers must rebuild.
