# Actual capability matrix

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
