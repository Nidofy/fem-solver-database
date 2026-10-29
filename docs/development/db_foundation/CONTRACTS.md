# DBF1 type, storage and compatibility contracts

## Field and history semantics

`schema.hpp` defines fixed scalar kinds, ordered shapes/component labels, entity
association/location, shear convention, units/basis, configuration/time layer,
persistence/restart/missing policy and dense/ragged/sparse classification.
Version 1 descriptors validate enums, shape arithmetic, component uniqueness and
policy consistency. `UNSPECIFIED` is explicit; no unit system or coordinate
transform is inferred. Basis is descriptive text in this batch, not a validated
orientation reference. A name/basis change is identity-significant.

FieldRegistry keeps its original string/type/group API; a descriptor overload
checks scalar, component count, dense storage and forbidden missingness against
the actual Field type. Resolve handles before kernels. No per-value dynamic
dispatch or new allocator is introduced. Legacy fields have no descriptor;
requesting one throws instead of inventing semantics.

HistorySchema has a caller-defined ID/version and ordered named variables with
FieldDescriptor plus per-component FP64 wire initial values. Initialization must
be finite and representable in the model precision. Packed histories currently
require the model's FP32/FP64 scalar, element/IP location, current time, dense
values, Required restart and Persistent state. Other policies are rejected.
Trials retain existing begin/commit/rollback behavior. Initial values are applied
once at StateDB construction; result capture does not update history.

`IRHistoryBinding{material, model, history_schema}` attaches a known declarative
schema to an existing named material. Duplicate/missing references are rejected.
Named lookup returns a checked flat address using the element's IP and history
offsets. Names remain cold metadata; callers may resolve addresses before loops.
Unknown algorithms may store this typed declaration, but neither CpuRun nor
direct TET/HEX plans execute explicit histories. No opaque unknown state payload
is interpreted as a supported constitutive algorithm.

Without explicit schemas, legacy fingerprints remain identical. With explicit
schemas, a domain-separated suffix hashes every descriptor, ordered variable,
shape/component label, schema version and initial value. Thus count-preserving
changes cannot bypass either restart identity check. These FNV fingerprints and
CRCs detect accidental mismatches/corruption; they are not authentication.

## Ragged and sparse ownership

RaggedField stores uint64 offsets into Field<T,C,Layout>. Offsets start at zero,
are nondecreasing and end at the logical row count; empty segments are valid.
SparseField stores strictly increasing unique uint32 entity indices, a uint64
domain count at most UINT32_MAX, Field values and one uint8 validity per row.
Validity is exactly 0/1. A present zero differs from invalid or absent rows;
invalid rows still require finite numeric storage. UINT32_MAX is not a valid
entity index. No Index64 execution claim is made.

Values use existing aligned SoA/AoSoA and checked weak-lifetime views. Copy creates
an independent owner; assignment/move invalidates previous value views;
destruction expires their weak token. Metadata spans are ordinary borrows and
must not outlive or cross assignment/move/destruction. Returning a reference
from a checked access does not extend its lifetime. Concurrent mutation still
requires caller synchronization; these containers add no CUDA synchronization.

DTOs serialize logical entity-major rows, never padding, pointers, size_t object
representations or vector headers. Encoded lengths/offsets are uint64; scalar
kinds/enums use uint32; bool in the existing codec uses a validated uint8 0/1.
Wire bytes are little endian. Native unpack uses its existing allocation budget,
then validates schemas and storage invariants before returning an owner.
Encoded-size preflight rejects over-budget serialization before a DTO copy.
Successful encode still owns a DTO copy plus encoded bytes: this is not zero-copy
or a total resident-memory cap. Generic storage DTOs are not automatically
registered into ModelStore/ResultDB/RestartDB; that integration remains future work.

## Compatibility table

| Artifact | Prior version | DBF1 behavior |
|---|---|---|
| ModelIR / model manifest | 1 | default unchanged; exact old encoding/hash retained |
| ModelIR / model manifest | 2 (new) | old fields plus history-binding tail; required version validated before other IR decoding |
| native record envelope | 1 | unchanged CRC/length envelope; unknown record kinds remain rejected |
| .sdbres native | 1 / indexed 2 | unchanged; generic typed history result publication not added |
| .sdbrst state schema | 3 | unchanged field layout; model_schema is 1 or 2 and must match supplied model |
| lowlevel checkpoint | 2 | unchanged layout; explicit schema semantics bound by supplied model fingerprint |
| FieldDescriptor / ragged/sparse DTO | 1 (new) | bounded native codec; unknown versions reject |

Unknown required versions reject, and new files are not rewritten into a legacy
version by dropping their semantics. Old readers reject model schema 2. Model
files carry descriptors; restart requires that matching model, not an independent
schema recovery service. Reordered layouts still use layout-sensitive legacy
mesh identities; external SourceMap identity remains separate. Full semantic
identity independent of reordering is not delivered here.

Existing atomic-stream replacement and cooperative leases are reused. No new
process-crash or power-loss guarantee follows from these tests. Device residency
is reconstructed by existing runtime ownership, not persisted device pointers.
