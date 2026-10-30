# DBF7 local foundation extensions (SDK 2.3)

Scope: independently buildable database, metadata and consumer contracts. No
production DB ABI, solver core or numerical algorithm is a dependency. Earlier
DBF6 manifest evidence remains historical; the DBF7 packet binds final content.

## Indexed time queries

ResultReader::time_interval returns [first,last) frame indices for a closed
physical-time interval. Native1 uses its resident index. Indexed2/TypedIndexed3/
CompressedIndexed4 search existing footer ancestor links directly, with O(log N)
footer probes per bound. No format change, interpolation or synthetic missing
samples. Result writers still require strictly increasing times.

ResultQuery::visit_history and visit_typed_history call a bool-returning sink;
false stops after the current sample. Their internal result memory is bounded by
one selected field, not the number of returned samples. Existing vector APIs are
wrappers with an optional max_samples limit. Callbacks already delivered cannot
be rolled back if a later sample is corrupt or changes schema/basis/region.
Typed UInt64 stays exact and validity remains distinct from zero. Readers/handles
are not made thread safe by this query extension.

## Owned state access and measured memory

StateSession copies an already frozen RegisteredState. This intentionally isolates
the new owner from references retained by callers of legacy get/add. Read leases
may coexist; a write lease or structural operation excludes all other leases.
Conflicts fail immediately rather than blocking. A lease pins the owner even if
the StateSession facade is destroyed. Returned LeaseView::at/raw/size also check
the lease token; after lease destruction, checked access rejects. A raw view or
reference already extracted must not outlive its lease. Do not concurrently
destroy a lease being used by a worker. Structural mutation is only exposed by
owner methods. The old Field and RegisteredState APIs retain their documented
joined-phase rules; this wrapper does not globally change those APIs.

Payload accounting includes typed storage padding, validity, initial scalar values
and address/offset/index arrays. Admission reserves old + trial + staged payload
before trial/remap/restore. high_water_payload and copied_payload report accounted
payload, not total allocations or bytes read from disk. Metadata strings, tree
nodes in remapping, codecs, external inputs and allocator overhead are excluded.
There is no global peak-RSS guarantee. Trial is still a deep copy. Remapping now
copies matching typed scalars/validity directly, eliminating per-row temporary
wire encode/allocate/decode. It preserves full address identity and exact UInt64.

## Atomic inspectable recovery bundle

FoundationBundle saves Batch (including DOF/material/section/topology), cursor,
inspectable field catalog and registered state blocks in one CRC-protected native
file. The catalog includes descriptor/schema, layout kind/width, addresses,
initial scalar bytes and restart policy. inspect reads metadata only; it is not
authorization to resume. Restore requires an explicitly registered receiver and
exact Batch/catalog/manifest match, validates all state blocks and EOF, and only
then publishes state and returns the cursor. Failed save leaves the previous
published file; failed restore leaves the receiver unchanged.

This gives single-file version consistency, not physical power-loss certification.
Existing native atomic publication guarantees and platform limitations apply.
Budget is encoded file bytes excluding the 12-byte prefix, not process memory.
No inferred C++ Field instantiation, arbitrary schema upgrader or dynamic factory
is introduced. Unknown schemas still require an explicitly capable consumer.

## Units, matrices and analysis

Unit represents the seven SI base dimensions with integer exponents [-64,64],
positive finite scale and an optional absolute-temperature offset. Conversion
requires equal dimensions; multiplication/division rejects affine temperatures
until callers explicitly choose a difference unit. Symbol definitions are supplied
by the caller. There is no natural-language unit parser or fractional-power algebra.

EquationMatrix<T,I> is a new portable CSR envelope (float/double, UInt32/UInt64
columns). It carries K/M/C/Kg/conductivity/capacity/coupling/user role, row/column
DofSpace identities, structure/value revisions, symmetry, units and equation stage.
Reduced entries explicitly use Canonical or Solver order for each axis; Solver
means the space's solver_to_canonical permutation. Full means DofSpace binding-slot
order and permits only Canonical. Symmetric matrices require the same identity
and order on both axes and exact transposed stored entries. require checks DOF
identity/dimensions; consumers must also honor order/role/stage. No implicit
permutation, assembly, elimination or factorization occurs. Existing MeshDB and
SparseMatrix remain Index32. A >2^32 column roundtrip proves wire/index width,
not a physically allocated billion-row model.

AnalysisContract stores a versioned consumer schema, step/time interval, output
times, and explicit slot-based initial/prescribed/load tables. Tables are finite,
strictly increasing and cover the whole step (initial values occur only at start).
Interpolation is linear, with no extrapolation. Prescribed slots must be marked
Prescribed; initial/prescribed units must match the DOF declaration and known
quantity dimensions. Load units/basis and physical applicability remain the named
consumer schema's responsibility; no load assembly kernel is supplied.

input/thermal_contract.hpp imports bounded standalone INP fragments containing
constant nodal TEMPERATURE, CFLUX (DOF 11), and INITIAL CONDITIONS TYPE=TEMPERATURE.
Labels are resolved through an explicit caller-supplied label-to-thermal-slot map.
Unknown keywords/options, sets, amplitudes, OP=NEW, failed streams and empty
fragments reject. The existing mechanical input reader is unchanged. inp_audit.py
provides a separate bounded include/keyword inventory against an explicit allowlist;
keyword acceptance there does not validate data grammar. Full INP is not claimed.

## Frequency/modal/complex storage

SpectralSeries is a separate versioned storage-only format: frequency in Hz or
positive integer mode numbers, explicit exp(+iwt)/exp(-iwt) phase convention,
modal normalization, addressed Float64 real/imaginary arrays, unit/model/DOF/basis
metadata, CRC persistence and closed-interval lookup. Mode numbers encoded as
doubles are limited to exact integers <=2^53-1. Units must match the field symbol.
No FFT, eigensolver, normalization computation or complex constitutive calculation
is supplied. Current time-result formats are not silently repurposed. This first
spectral container loads within an explicit file/decode budget; it is not a lazy
million-frequency reader.

## Read-only foreign-language boundary

SDB_C_API builds an optional shared sdb_reader and exports sdb::sdb_reader from the
installed package. c_api.h is C-only, ABI version 1, with opaque owned handles,
UTF-8 paths, thread-local error messages and status returns. Exceptions do not
cross the ABI. Calls on one handle must be serialized. Output arrays are caller
owned and must be valid, separate writable buffers. Failed calls preserve output
arguments. Legacy f64 history and exact typed UInt64 entity queries are supported;
face/contact/IP selectors and other typed scalar lanes are not yet exposed by ABI1.
Python ctypes client provides context-manager ownership and serializes its handle;
Windows compiler/runtime DLL directories are passed explicitly. C++ headers remain
the complete API. No FFI API writes database files or solver state.

## Migration audit

DBF7_MAPPING_TEMPLATE.json contains 2047 unique extension:ordinal keys and source
line evidence from the DBF6 inventory. Entries deliberately remain unsupported/
unqualified until filled with actual semantics. migration_audit.py compares neutral
snapshots against explicit rules for type, shape, units, basis, indexing, lifecycle,
conversion and tolerances. Populated-key capture must match actual captured fields;
all populated and target keys must be accounted for. Integer identities compare
exactly and cannot be converted/toleranced through double. JSON duplicate keys
reject. Output uses exclusive creation and records input hashes.

The fixture is SYNTHETIC_ONLY. Captured comparisons also do not automatically
certify native mappings. Control blocks, FNL/FEG and native work records need their
own capture inventory; source-schema field enumeration alone cannot expose them.
Capture at the adapter boundary must separately bind model, step, equation order,
element family, basis and source revision. Database code never loads legacy headers.
