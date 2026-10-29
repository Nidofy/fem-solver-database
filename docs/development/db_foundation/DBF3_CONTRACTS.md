# DBF3 phase-one contracts

This additive standalone batch closes the remaining phase-one contracts on the
DBF2 baseline. It does not modify production solver code or add physical kernels.
The clause ledger is DBF3_REQUIREMENTS.md. Verification and independent review
have separate evidence; construction/serialization do not imply execution.

## Identity and topology

Strong IDs are typed boundaries, not a replacement for the existing Index32 hot
layout. SourceKey retains scoped external labels; NodeId/ElementId refer to
zero-based compiled indices. InstanceId follows lexicographically sorted instance
names (use `instance_id(name)` instead of assuming declaration order); StepId
follows analysis-step definition order.
FrameId identifies the stored logical frame ID; Reader overloads resolve it and
reject missing IDs rather than treating it as an ordinal. Old ordinal overloads
remain supported. Maximum-value StrongId sentinels reject rather than truncate.

TopologyLayout version 1 fixes node/edge/face numbering. Mesh local indices are
zero-based. Integration/layer/section point numbers are one-based, with zero for
an absent axis. `topology_face_nodes` reproduces existing kernel and Abaqus face
orders; `topology_face_permutation` gives their explicit permutation. Negative
side reverses the chosen ordering. Topology is not formulation or quadrature;
reduced/full integration remains in ElementDescriptor. Polygon topology has an
explicit node count and local capacity bound. Unknown versions reject.

SampleAxisDescriptor version 1 describes the existing physical-time axis,
separate from Current/HalfStep/Previous staggering and the wire version. Unknown
versions/kinds reject. Model schema4 preserves supplied axis units; current
result formats implicitly use Time/UNSPECIFIED and reject nondefault axis
metadata rather than discard it. Frequency/modal/complex systems are not added.

## Model schema4 and controlled input

ModelIR schemas1/2/3 visitors remain byte compatible. Schema4 appends
ModelExtensions after the existing foundation. The tail includes topology
numbering version, sample-axis contract, coupling records, amplitude policies,
property distributions, initial-condition events and reactivation policies.
Schema4 uses the existing ModelStore/compiler/query path; it is not a new DB.

- Coupling: kinematic/distributing definitions bind a reference node, scoped
  node region or surface, DOF mask and coordinate system. Distributing weights
  are explicit, positive, cover the resolved nodes and normalize deterministically.
- Amplitude: explicit step/total time domain, linear/constant interpolation and
  reject/clamp/linear extrapolation. Legacy policy is preserved when absent.
- Property: constant or table assignments bind region, material, parameter,
  optional orientation and source. Ambiguous overlapping assignment rejects.
  Compiled queries retain provenance; they do not evaluate new constitutive laws.
- Multistep: Replace clears earlier load contributions, Add preserves separate
  amplitude/direction/follower/source contributions, Remove removes the load.
  Initial events preserve identity/validity semantics; activation queries expose
  the effective active state and declared reactivation policy without executing it.

New INP subsets are root/assembly COUPLING followed by KINEMATIC with explicit
translational DOFs1-3, model-level node-label initial VELOCITY, and step-level
uniform pressure DSLOAD OP=MOD. Other options/scopes reject. Existing input
subsets remain. MODEL CHANGE is not mapped into the Explicit parser because the
documented procedure is Standard-only. Programmatic activation remains available.

Syntax references: official [COUPLING](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-coupling.htm),
[KINEMATIC](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-kinematic.htm),
[DSLOAD](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-dsload.htm),
[INITIAL CONDITIONS](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-initialconditions.htm),
and [amplitude continuation](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEPRCRefMap/simaprc-c-amplitude.htm).

## Registered State

`StateDB::register_field<Field<V,C,Layout>>` registers a descriptor, entity/sample
count, schema ID/version, exact typed initial components and optional basis ID.
Call `freeze_registered_fields()` before capture, trial or persistence. Resolve
`registered_field<F>(name)` once, then use ordinary Field typed views. No string
lookup, virtual dispatch or scalar variant is introduced into kernel loops.
Dispatch/virtual cloning is per field at cold registration/snapshot/restore paths.
Default StateDB has no extension heap allocation.

Supported scalar lanes match typed results: float/double, int32/uint32,
int64/uint64 and uint8. Dense Node/Element/Global entity fields and ordered
IntegrationPoint/SectionPoint sample fields are supported. Sample rows follow
MeshDB's complete sample layout, preserving layer/section point coordinates.
Unsupported Face/Contact/ElementNode registration rejects; existing ContactDB
and ragged/sparse storage primitives retain ownership of those separate contracts.

Required variables restore exact stored values and validity. Reconstruct and
Omit variables reset to their declared typed initial values; validity resets to
present. Omit does not promise recovery. An invalid slot has canonical zero.
Registration order, descriptor, count, schema identity/version, layout/width,
basis ID and typed initialization form the exact manifest contract. Different
same-length schemas cannot be restored by accident. Automatic schema migration
is not performed. Trial copy/rollback includes every extension; successful
replacement invalidates borrowed views, failed restore preserves them.

## Checkpoint and Restart extensions

| Container | No registered fields | With registered fields |
|---|---|---|
| low-level checkpoint | existing schema2 bytes | schema3 with manifest fingerprint and required extension blocks |
| lifecycle Restart | existing schema3/rotation schema4 bytes | schema5 with explicit rotation capability and required extension blocks |
| ModelStore | schemas1/2/3 unchanged | opt-in model schema4 tail |
| ResultDB | Native1/Indexed2/TypedIndexed3 unchanged | registered values use existing TypedIndexed3 |

Registered block kind200 is a canonical LE manifest. Kind201 contains uint64
entry index, scalar offset and count, followed by up to4096 exact typed scalars
(each followed by its validity byte when configured). Values are entity-major,
component-minor regardless of in-memory SoA/AoSoA layout. Non-Required fields
have no value records. Manifest schema1 stores the count and each field's
FieldDescriptor, uint64 row count, schema string/version, layout kind/width,
basis ID and typed initial vector. No C++ object representation is persisted.

The destination registers its expected schema before loading. Decoding checks
the exact manifest before values, rejects unexpected/missing/reordered blocks,
enforces file/record/count limits and restores into independent temporary state.
Core fields, rotations and extensions publish together only after all validation.
CpuRun also validates its cursor before publication. Legacy import refuses to
invent Required extension state; optional extension state reconstructs explicitly.

CRC and manifest fingerprints detect accidental corruption/mismatch, not
adversarial tampering. This batch retains the existing synchronization primitives;
it makes no new power-loss or multi-file durability guarantee. Compression is absent.

## Local histories and owned results

Local history values remain expressed in their declared local basis. The adapter
composes the resolved orientation, the instance rotation for part-scoped section
assignments, and the shell layer angle. This basis is stored in the result catalog
and each field group references it. It does not rotate the stored components or
silently change tensor/shear conventions. Missing/ambiguous basis assignments reject.
Generated basis IDs use the reserved range beginning at2^33 and are deterministic
for the sample layout; only local-history samples require this generated catalog.

`append_registered`/`snapshot_registered` produce owning exact typed payloads;
capture rejects trial state. Query can select an exact address, schema, region
and basis group. Cross-frame history queries reject semantic/basis drift.
Async admission uses the existing owned-snapshot capacity budget. No persistent
pointer into State or device memory is serialized.

## Migration and verification boundaries

Rebuild standalone consumers after these header changes. Register the same
extension schema and initialization before restore. Existing files need no
conversion; new required schemas need the new reader. There is no automatic
layout remap. Units are explicit metadata or UNSPECIFIED, not inferred algebra.
New physical definitions are storage-only and fail closed at execution gates.
Production SDK integration and CUDA are not locally compiled/verified.
