# Actual capability matrix

## DBF7 current additions

| Capability | Implemented and tested boundary |
| --- | --- |
| Time interval/streaming | Indexed skip-link positioning; cancellable sinks; vector sample budget; million Indexed2 frames |
| State ownership/memory | StateSession leases and payload admission; direct exact typed remap; no global RSS/COW claim |
| Inspectable restore | Single-file Batch/DOF/catalog/cursor/state bundle; expected registry still required |
| Equation matrices / Index64 | New typed CSR envelope with role, stage, explicit ordering and identities; MeshDB stays Index32 |
| Units / analysis / input | Integer dimension algebra, explicit conversion, slot assignment tables, bounded constant thermal fragments |
| Spectral / complex | Separate frequency/modal real-imaginary storage contract; no numerical kernel or time-file reinterpretation |
| C / Python SDK | Installed read-only ABI1 and ctypes, exact UInt64 entity selector, external C/C++ tests |
| Migration tooling | 2047-key unqualified template; populated-key and exact snapshot comparison; no native certification |
| Final evidence | DBF7_HANDOFF + manifest + independent-current reviews; scoped platform matrix |

## DBF6 current additions

| Capability | API / evidence | Boundary |
| --- | --- | --- |
| Capture completeness | DataCoverage; 25-subfile/2,047-field offline inventory | declaration completeness, not native mapping certification |
| General DOFs and equations | DofSpace; permutation, flattened relations, mechanical export, wire oracle | consumer supplies numbering/elimination; no new constraint algorithm |
| Addressed registered state | StateLayout + existing RegisteredState; dense/ragged/sparse, remap/trial | caller qualifies model membership; raw borrows require joined mutation |
| Independent persistence | FoundationStore; existing checkpoint/RestartDB/results extensions | expected registration required; file budget is not peak RSS |
| Kernel-independent consumer | consumer::v1 Batch/catalogs/bindings/AccessSet/snapshot | arbitrary metadata is storage-only until exact consumer capability checks |
| Local verification | GCC 47/47, MSVC ASan 3/3, external installed consumer | CPU standalone; native/CUDA/TSan/production speedup not claimed |

DBF6_HANDOFF records final evidence and source state. Existing mechanical routes
and consumer v0 remain available. Earlier statements below about missing general
DOF/addressed-state/consumer interfaces retain only their historical scope.

## DBF5 current additions

| Capability | API / evidence | Boundary |
|---|---|---|
| Optional real zstd | io/compression.hpp; SDK feature test and installed package | None/Zstd; bounded custom allocator; no substitute codec |
| Compressed typed results | CompressedIndexed4; persisted per-field policy; independent wire oracle | seven exact lanes, CRC/index/commit semantics retained; requires new reader |
| Bounded recovery/read/admission | stored/raw/workspace limits and async reserve | recovery rejects oversized complete/truncated blocks before allocation/repair; persistent model/frame costs remain separate |
| CLI verification/conversion | --verify, --convert-result, --compression | full logical scan or new-file conversion; no transferred restart authorization |
| Consumer view v0 | consumer_views.hpp, existing TET4/HEX8 force kernels | exact tested CPU trajectories and lifetime/alias guards; private scratch; no production call-site replacement |
| Measured view cost | paired small/large batches | workload-dependent; no universal <=1.3% guarantee |
| Migration toolkit | migration/preprocessed.hpp + golden + DBF5_MIGRATION | copied FNL/FEG/IDARRAY snapshot, exact equations, scratch/reducer; native Felem ABI not compiled |

DBF5_HANDOFF and DBF5_VERIFICATION contain final review and execution evidence.
This supersedes historical statements that compression and consumer tooling are
absent. Native integration, CUDA, full INP, Index64, units algebra and new physics
remain separate gates, not implied by D1–D7 storage completion.

## DBF4 phase-two additions

| Capability | API / evidence | Boundary |
|---|---|---|
| Explicit durability | durability::Level, NativeResultStorage/ResultWriter/AsyncResultWriter::barrier | StreamFlush/FileSync; strict PublicationSync POSIX only, rejected on Windows |
| Ordered publication | CheckpointLink::publish_durable, CLI --durability | exact result watermark before restart then link; no cross-file atomicity claim |
| Ambiguous outcome | PublicationError and LinkPublicationError | confirmed/potential replacement plus failed stage; caller reopens and verifies |
| Process failure campaign | dbf4_fault_driver + Python schema1/CRC/FNV oracle | Native1/Indexed2/Typed3 fresh readers; process termination is not power loss |
| Concurrency sanitizer | WSL Linux GCC TSan | separate from GCC Windows and MSVC ASan matrix; final evidence in DBF4_VERIFICATION |

Final acceptance status and source identities are recorded in DBF4_HANDOFF and
DBF4_VERIFICATION. The historical DBF3 matrix below remains its original bounded
acceptance record; its future-durability statement is superseded by DBF4.

Historical baseline: **DBF3 phase one**, source `c53ab3e`. Its table below
supersedes the corresponding DBF2 limitations; historical tables retain their
original scope. See DBF3_REQUIREMENTS, DBF3_CONTRACTS and DBF3_VERIFICATION for
clause evidence and the exact final verification matrix. Independent Astra/high
review is PASS WITH EXPLICIT SCOPE LIMITS; it is not blanket ALR1 certification.

| DBF3 capability | Public/runtime boundary | Persistence/query | Evidence | Status |
|---|---|---|---|---|
| Typed identity/topology | MeshDB typed overloads, CompiledModel instance/step/material/surface, FrameInfo/Reader FrameId; TopologyLayout and face/edge permutation | old wire preserved; explicit numbering version in model4 | dbf3_contract/model/result | PRESENT_VERIFIED bounded typed boundaries; old APIs retained |
| Time-axis extension contract | SampleAxisDescriptor v1, unknown kind/version rejects | model4 stores supplied units; current result formats reject nondefault axis metadata | dbf3_contract/model/result | PRESENT_VERIFIED Time only; no frequency/modal arithmetic |
| Coupling/property/amplitude/events | ModelExtensions, resolved scoped queries, provenance and effective steps | ModelIR4 builder→validator→compiler→ModelStore→fresh reader | dbf3_model + independent model probes | PRESENT_VERIFIED storage-only new physics |
| Controlled INP additions | explicit translational COUPLING/KINEMATIC, node-label initial VELOCITY, uniform DSLOAD OP=MOD | existing ModelIR/compiler only | dbf3 fixtures/negative imports | PRESENT_VERIFIED documented subset; not full grammar |
| Registered typed persistent state | frozen dense Node/Element/Global and complete IP/SectionPoint Fields; seven exact scalar lanes and validity | checkpoint3/Restart5 manifests + required blocks + independent typed snapshots | dbf3_state/result/wire/fault tests | PRESENT_VERIFIED; separate from FP32/64 packed material HistorySchema |
| Trial/restart policy and failure atomicity | deep trial/rollback, Required/Reconstruct/Omit, temporary core/rotation/extensions, staged CpuRun publication | exact schema/initialization/layout contract; old bytes preserved without extensions | malformed-wire, inertia, cursor and allocation failure probes | PRESENT_VERIFIED tested CPU cases; not exhaustive allocator/filesystem failures |
| Local material history basis | part instance × resolved orientation × layer rotation; components unchanged | deterministic catalog + basis-group/address selectors in existing TypedIndexed3 | dbf3_result, independent noncommuting-matrix oracle | PRESENT_VERIFIED; requires explicit unambiguous orientation |
| Executable inherited amplitude continuation | runtime Action origin, explicit redefinition and total-time distinction | selective CpuRun configuration version for affected runs | dbf3_model real transition/split restart + independent red/green | PRESENT_VERIFIED existing CPU subset |
| D7 composite/legacy/process closure | existing mixed synthetic composite + DBF3 model/state/result supplements and real CPU paths | schema1/2/3 + schema4 models, old/new restart and typed results | final34-test matrix + independent probes | PRESENT_VERIFIED CPU serial/OpenMP/MSVC ASan;129 cross-reader checks each way |

Registered fields do not imply arbitrary integer constitutive histories,
registered Face/Contact fields or all scalar/layout/entity combinations. Existing
ContactDB and explicit-address typed results retain their separate roles. CUDA
and production integration remain BLOCKED_ENV/not locally compiled or verified.
Durability enhancement, compression and production adapters are later phases.

The first table is the retained DBF1 audit snapshot. The DBF2 table below
supersedes its gap statuses; it does not retroactively change DBF1's scope.

Evidence is from REVIEW_HEAD `9a3d754` and the DBF1 diff, not a remote snapshot.
P/V/S/Q/R/E are Parsed / Validated / Stored / Queryable / Reloadable / Executable.
`—` means not applicable; `subset` is deliberately narrower than full support.
Verified means the final logs in VERIFICATION.md, not merely a declaration.
Exact symbol/file/line anchors are retained in
`verification/reviews/DBF1/source-evidence.json` for the frozen content.

| Capability | Source/API and runtime | Serialization/reader | Tests | P/V/S/Q/R/E | Status |
|---|---|---|---|---|---|
| Scoped external identity | model_ir::SourceKey/SourceMap, compiler context maps | ModelManifest nodes/elements, reconstruction | lifecycle_contracts instance cases | yes/yes/yes/yes/yes/subset | PRESENT_VERIFIED |
| Distinct strong entity IDs | model.hpp Id=uint64, Index=uint32 remain aliases | Existing wire widths | index boundary in foundation tests | —/subset/yes/yes/yes/subset | PARTIAL; strong wrappers MISSING |
| Checked capacities | memory.hpp product/sum, model.hpp index_of | bounded native containers | storage/foundation/lifecycle negatives | —/yes/yes/yes/yes/yes | PRESENT_VERIFIED for tested boundaries; no Index64 runtime |
| Field metadata | schema.hpp FieldDescriptor; FieldRegistry::add/descriptor | HistoryVariable in ModelIR2; native codec | foundation descriptor negatives | —/yes/yes/yes/yes/— | PARTIAL; arbitrary result/registry persistence not connected |
| Dense SoA/AoSoA | field.hpp Field/CheckedView | checkpoint fields, logical values | database/storage/foundation | —/yes/yes/yes/yes/yes | PRESENT_VERIFIED CPU |
| Generic ragged | storage.hpp RaggedField, offsets+Field | RaggedData/native encode/decode | storage_contracts | —/yes/yes/yes/yes/— | PRESENT_VERIFIED host primitive; not general ResultDB attachment |
| Generic sparse and validity | storage.hpp SparseField | SparseData/native encode/decode | storage_contracts exact int64 and missing/zero | —/yes/yes/yes/yes/— | PRESENT_VERIFIED host primitive |
| Matrix CSR/BSR contracts | implicit.hpp existing structures/apply | no new portable matrix schema | existing database tests | —/subset/yes/yes/no/subset | PARTIAL |
| Element topology/formulation | ElementDescriptor + compiler descriptor check | ModelIR/ResultModel | hex8/lifecycle/storage-only fixtures | yes/yes/yes/yes/yes/subset | PRESENT_VERIFIED for existing subset |
| Material histories | Material::history_schema, StateDB::history_address | ModelIR2 bindings, ModelStore, RestartDB, lowlevel checkpoint fingerprint | foundation_contracts/process_reader | no/yes/yes/yes/yes/no | PRESENT_VERIFIED storage-only declared FP32/FP64 schemas |
| Unknown material algorithm | IRHistoryBinding::model + explicit declarative history | model save/reload, matching state only | foundation execution refusal | no/yes/yes/yes/yes/no | PARTIAL; typed material parameter schema absent |
| Shell/beam/layers/orientation | existing IRSection properties, descriptors | existing ModelIR roundtrip | lifecycle_contracts | subset/subset/subset/subset/subset/no | PARTIAL; no new layers/orientation graph |
| Units and basis | ModelIR::units; FieldDescriptor::units/basis | exact metadata roundtrip | foundation metadata identity | no/subset/yes/yes/yes/no | PARTIAL; strings not dimension algebra or reference graph |
| SPC/amplitude/multistep loads | compiler AnalysisDB/LoadEvaluator | ModelIR, restart metadata | lifecycle/process pipeline | subset/yes/yes/yes/yes/subset | PRESENT_VERIFIED existing subset |
| Equations/tie/body/follower loads | no new implementations | no new schema | existing unsupported-input gates | no/no/no/no/no/no | DEFERRED |
| Read-only output/async ownership | ResultPublisher, AsyncNativeResultWriter | owning frame queue/native v1/v2 | lifecycle/async tests | —/yes/yes/yes/yes/yes | PRESENT_VERIFIED inherited ALR1 CPU scope |
| Native indexed query | indexed_format File, ResultReader | nonce-bound lazy committed footer/entity blocks | indexed_results_contracts | —/yes/yes/yes/yes/yes | PRESENT_VERIFIED inherited scope |
| Generic result/state address | ResultDescriptor IP/section/basis/time, State history offsets | existing floating ResultField only | existing result tests | subset/subset/subset/subset/subset/subset | PARTIAL; no new integer/validity result format |
| Restart atomicity | RestartDB::load temporary StateDB, lowlevel load_checkpoint | schema3 restart / v2 checkpoint unchanged | rotation/identity/mass/corruption negatives | —/yes/yes/yes/yes/subset | PRESENT_VERIFIED tested CPU cases |
| Old model compatibility | ModelIR1 exact bytes and unchanged legacy hash | frozen old model/restart hex files | foundation_legacy_golden process | —/yes/yes/yes/yes/yes | PRESENT_VERIFIED tested golden pair |
| Unknown wire enum overflow | native::decode checks underlying width | shared native decoder | regression red/green and enum oracle | —/yes/—/—/—/— | PRESENT_VERIFIED; semantic validation still type-specific |
| CUDA residency/lifetimes | existing cuda_memory/cuda_backend | rebuilt residency, no device pointers in files | no current device execution | —/subset/yes/yes/subset/subset | BLOCKED_ENV / NOT_RUN |
| Full D7 synthetic model | only mixed HEX/TET history and storage primitive fixtures now | partial closure | foundation/storage tests | no/subset/subset/subset/subset/no | DEFERRED full shell/beam/contact/load composite |

This matrix does not turn storage into physical execution. It also does not
equate absence of a failing test with a completed independent ALR1 review.

## DBF2 continuation

Construction through the programmatic ModelIR API is distinct from INP parsing.
Detailed semantics and intentionally bounded scope: [DBF2 contracts](DBF2_CONTRACTS.md).
Final verification and review evidence: [DBF2 verification](DBF2_VERIFICATION.md).

| Capability | Symbol/file; runtime storage | Serialization / independent query | Test | P/V/S/Q/R/E | Status |
|---|---|---|---|---|---|
| Strong identities | types.hpp StrongId; SourceMap::{node_id,element_id,reverse}; FieldRegistry::field_id | native fixed-width unsigned; existing source manifests | foundation_boundaries, model_foundation_contracts | —/yes/yes/yes/yes/subset | PRESENT_VERIFIED; additive typed boundaries, old APIs retained |
| CSR/BSR DTO | implicit.hpp MatrixData/SparseMatrix::data/from_data | encode/decode with schema/index/block metadata | foundation_boundaries | —/yes/yes/yes/yes/existing apply | PRESENT_VERIFIED CPU |
| Coordinate graph/orientations | foundation.hpp IRCoordinate/IROrientation; CompiledFoundation queries | ModelIR3; resolved result basis catalog | model_foundation_contracts/process_reader; integration | subset/yes/yes/yes/yes/no | PRESENT_VERIFIED storage-only |
| Typed materials/tables | MaterialDefinition/Parameter/Table; scalar lanes + axis arrays | ModelIR3; material(MaterialId) | model_foundation_contracts; exact uint64 oracle | no/yes/yes/yes/yes/no | PRESENT_VERIFIED storage-only; evaluator deferred |
| Solid/shell/beam sections | IRSolidSection/IRShellSection/IRBeamSection; resolved element IDs | ModelIR3, section(SectionId) | model_foundation_contracts/process_reader | legacy subset/yes/yes/yes/yes/no | PRESENT_VERIFIED storage-only new schema |
| Layer/IP/material histories | model.hpp SamplePoint; StateDB point_history_offsets_, history_address_at | ModelIR3, checkpoint2, Restart3/4, TypedIndexed3 | boundaries + integration float/double | no/yes/yes/yes/yes/no | PRESENT_VERIFIED persistent FP32/FP64 histories |
| Per-node DOFs | compiler node DOF map; DofMap::equation(NodeId,Dof) | reconstructed schema3 layout | model_foundation solid-only vs shared shell nodes | yes/yes/yes/yes/yes/no for new physics | PRESENT_VERIFIED storage allocation |
| Equations/tie/rigids | IREquation/IRTie/IRRigid; resolved typed references | ModelIR3 + CompiledFoundation | model_foundation references/DOFs/constraints | equation subset/yes/yes/yes/yes/no | PRESENT_VERIFIED storage-only |
| Nodal/face/body loads | IR*Load; load ID + step update, scoped region and coordinate | ModelIR3 + load(LoadId,StepId) | model_foundation load identity/lifetime/references | legacy subset/yes/yes/yes/yes/no new types | PRESENT_VERIFIED storage-only |
| Initial/activation | IRInitialCondition/IRActivation, resolved IDs; State active flags | ModelIR3; checkpoint and restart flags | model + integration independent reader | no/yes/yes/yes/yes/no events | PRESENT_VERIFIED storage; event executor deferred |
| Exact typed result/missingness | results/typed.hpp TypedValues, TypedFieldPayload | indexed3 blocks; typed_field/typed_history<T> | typed_results_contracts + typed_process_pipeline | —/yes/yes/yes/yes/— | PRESENT_VERIFIED seven scalar types |
| Full result address | ResultAddress + SourceMap + ResultSample | full-address block ranges; exact/entity/schema filters | 300-layer selective query, integration all associations | —/yes/yes/yes/yes/— | PRESENT_VERIFIED |
| Local tensor basis | ResultBasis; deterministic coordinate/orientation adapter | indexed3 model catalog and per-field basis_id | integration tensor/basis oracle; missing basis rejection | —/yes/yes/yes/yes/— | PRESENT_VERIFIED explicit fields; automatic local history adapter DEFERRED and rejects |
| Committed history output | ResultPublisher::snapshot_history/append_history | owns scalar buffers; async admission accounting | typed trial/immutability/budget tests; integration | —/yes/yes/yes/yes/— | PRESENT_VERIFIED |
| Rotational restart | io/restart.hpp State schema4, six optional fields | RestartDB temporary load and shape/capability check | integration truncation/mismatch/inertia atomicity | —/yes/yes/yes/yes/no new kernels | PRESENT_VERIFIED storage-only |
| Format compatibility | ModelStore schemas1/2/3; Native1/Indexed2/TypedIndexed3 | frozen pre-DBF1 and pre-DBF2 .hex fixtures | foundation_legacy_golden, foundation3_legacy_golden | —/yes/yes/yes/yes/subset | PRESENT_VERIFIED tested fixtures |
| Semantic/analysis/layout identity | CompiledModel::*_identity; canonical_definition | strict serialized ModelStore identity still guards restore | model reorder/provenance/material/load/history tests | —/yes/yes/yes/yes/— | PRESENT_VERIFIED; no automatic restart remap |
| D7 composite | two instances, transformations, mixed topology, material/layer/beam/load/constraint/activation data | Model3 → Restart4/typed result → fresh-process reader | foundation_integration + process_reader | programmatic/yes/yes/yes/yes/no | PRESENT_VERIFIED synthetic storage, not a numerical solution |
| Real CPU lifecycle | existing TET/HEX CpuRun and output scheduler | typed container + linked restart; existing old formats | lifecycle/hex8/pipeline + typed_process_pipeline | yes/yes/yes/yes/yes/yes subset | PRESENT_VERIFIED CPU |
| Entity/field/frame scaling | typed_results_scaling; async admission bytes and read_stats | 12 measured combinations retained as CSV | immutable-source + independent expected scalar | —/yes/yes/yes/yes/— | PRESENT_VERIFIED measured workload; no fixed speed guarantee |
| CUDA / production adapters | existing CUDA contracts; new cold host metadata | requires reviewed adapter/build validation | no available production SDK/device proof | —/—/—/—/—/— | BLOCKED_ENV / not locally compiled or verified |

Remaining deliberate limits: no Index64 runtime, complex/frequency samples, general
unit algebra, arbitrary dynamic integer StateDB histories, full INP grammar,
automatic local-material-history basis adapter, new physical kernels, MPI or GPU
algorithms. The foundation provides storage for shell/beam/unknown material data
and rejects execution. These limits are not hidden behind an overall "supported"
label.
