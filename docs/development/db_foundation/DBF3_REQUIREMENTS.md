# DBF3 phase-one requirement ledger

Baseline: `00b716657ee4a2d2cbc2a5bc26ee85c2cea48937` on
`codex/astra-db-foundation-completion`. Scope is the user's phase one: D1-D7
semantic/storage closure. DBF4 durability, compression, new physics and production
adapters are separate work. This ledger supplements, not replaces, the DBF1/DBF2
capability matrix and their bounded independent reviews.

Acceptance columns distinguish programmatic construction, parsing, storage,
query, restoration and numerical execution. New physical definitions remain
storage-only. No whole-program completion is inferred from a test count.

Final outcome: phase-one contracts implemented and verified within the explicit
boundaries below. Reviewed library c53ab3e; test followup4b124df. Independent
Astra/high PASS with scope limits; three full CPU configurations34/34 each,
plus bidirectional cross-compiler129-check readers. See DBF3_HANDOFF and
DBF3_VERIFICATION for source/commit/evidence binding.

## Clause ownership and evidence targets

`Existing` means a baseline implementation with retained evidence that must pass
the final regression, not a fresh independent certification. `DBF3` means work
in this batch; its final verification is recorded in DBF3_VERIFICATION.md.

| Original clause | Implementation boundary | Evidence target | Track |
|---|---|---|---|
| D1.1 strong identity and hot-path size | types.hpp, MeshDB typed queries, SourceMap, CompiledModel, FrameInfo | foundation_boundaries, dbf3_contract/model/result | DBF3 |
| D1.2 scoped external identity/reorder | SourceMap and semantic/layout identity | model_foundation, integration, dbf3_model | Existing + DBF3 |
| D1.3 fixed wire widths/Index32 bounds | native codecs, index_of, StrongId::checked | boundaries, legacy goldens, wire oracles | Existing |
| D1.4 complete field descriptor | FieldDescriptor, typed results, registered-state manifest | typed_results, dbf3_state/result | Existing + DBF3 |
| D1.5 exact IDs/flags/missingness | seven typed lanes, validity masks, byte booleans | typed_results, dbf3_state/result | Existing + DBF3 |
| D1.6 units and distinct direction references | units UNSPECIFIED, coordinate/orientation graph, load/result basis | model_foundation, dbf3_model/result | Existing + DBF3 |
| D1.7 future sample-axis extension point | versioned SampleAxisDescriptor; current Time v1 only | dbf3_contract/model/result, unknown-axis rejection | DBF3 |
| D2 dense/ragged/sparse | Field, RaggedField, SparseField; fixed typed views | storage_contracts, dbf3_state | Existing + DBF3 |
| D2 CSR/BSR | MatrixData and bounded decoder | foundation_boundaries | Existing |
| D2 offsets/arithmetic/empty/limits | storage and native readers | storage_contracts, boundary/budget negatives | Existing + DBF3 |
| D2 cold dispatch/hot typed access | registry freeze and Field access | dbf3_state, checked-view tests | Existing + DBF3 |
| D2 lifetime/move/residency/optional allocation | Field generation, State optional extensions, CUDA contracts | database/storage/state tests; CUDA BLOCKED_ENV | Existing + DBF3 |
| D3.1 topology/formulation/interpolation/integration | ElementDescriptor + TopologyLayout v1 | dbf3_contract and compiler tests | DBF3 |
| D3.2 scoped entities/surfaces/reference points | ModelIR, SourceMap, compiled foundation | model_foundation, dbf3_model | Existing + DBF3 |
| D3.3 typed material schemas/tables | MaterialDefinition/Table | model_foundation + independent reader | Existing |
| D3.4 semantic histories/init/trials | HistorySchema, packed histories, registered state | foundation, dbf3_state/result | Existing + DBF3 |
| D3.5 solid/shell/beam sections | typed sections/layers/sample layouts | model_foundation, foundation_integration | Existing |
| D3.6 property distributions/provenance | schema4 extensions and compiled queries | dbf3_model | DBF3 |
| D3.7 storage-only/unknown required schema | compiler execution gates, strict restore manifests | model/state negatives and legacy fixtures | Existing + DBF3 |
| D4.1 semantic DOFs/optional allocation | DofMap/compiler | model_foundation, dbf3_model | Existing |
| D4.2 SPC/MPC/rigid/coupling/tie | foundation + schema4 coupling | model_foundation, dbf3_model | Existing + DBF3 |
| D4.3 directional/scoped loads | foundation loads and effective event queries | model_foundation, dbf3_model | Existing + DBF3 |
| D4.4 amplitude/initial/multistep/activation | schema4 policies and queries | dbf3_model + controlled fixtures | DBF3 |
| D4.5 one ModelIR/compiler | extensions in existing ModelIR pipeline | model roundtrip + existing process pipeline | Existing + DBF3 |
| D4.6 controlled input subset | input/abaqus.hpp, documented official syntax subset | dbf3 fixtures and negative import cases | DBF3 |
| D4.7 parse/store/execute distinction | import reports and fail-closed execution | model_foundation, dbf3_model | Existing + DBF3 |
| D5 full semantic address | ResultAddress/ResultSample/Binding + registered adapter | integration, dbf3_result | Existing + DBF3 |
| D5 mixed histories/layers/section points | sample layouts and state addressing | foundation_boundaries, integration, dbf3_result | Existing + DBF3 |
| D5 registered persistence policy | frozen registered state + transactional restart | dbf3_state/result | DBF3 |
| D5 contact history vs candidates | ContactDB/checkpoint + stable contact result addresses | database/general_contact/integration | Existing |
| D5 self-described local/typed results | TypedIndexed3 + scoped history basis adapter | dbf3_result + independent readers | DBF3 |
| D5 output schedule/restart watermark | CpuRun/CheckpointLink/output scheduler | lifecycle/hex8/process pipelines | Existing regression |
| D5 read-only capture/async ownership | history/registered snapshots + admission budget | async/typed_admission_budget/dbf3_result | Existing + DBF3 |
| D5 transactional restore/device residency | staged core+extension restore; residency reconstructed | dbf3_state, database tests; device BLOCKED_ENV | Existing + DBF3 |
| D6 logical/wire/state/layout version split | format contracts; conditional new model/restart/checkpoint versions | legacy byte goldens + new version negatives | Existing + DBF3 |
| D6 self-description/no struct dumps | fixed-width visitor codecs/registered manifest | independent scalar/wire oracles | Existing + DBF3 |
| D6 legacy/unknown required/optional policy | strict native records + explicit supported schemas | legacy fixtures + corruption negatives | Existing + DBF3 |
| D6 semantic/analysis/layout identity | canonical definition and manifest equality | model/state schema drift cases | Existing + DBF3 |
| D6 CRC vs identity/security | CRC for accidental damage, noncryptographic fingerprints | documented threat boundary | Existing |
| D6 bounded decoding | readers + registered manifest/field block caps | boundary/admission/state negative tests | Existing + DBF3 |
| D6 query/recovery/index binding | indexed reader and linked restart | indexed/async/process tests | Existing regression |
| D6 atomic visibility/crash/power-loss distinction | existing file primitives and documented limits | no new power-loss guarantee in phase one | Existing; DBF4 separate |
| D7 synthetic storage composite | existing two-instance mixed model + schema4/registered/local-basis supplements | integration and dbf3 suites | Existing + DBF3 |
| D7 real existing CPU subset | TET4/HEX8 lifecycle | serial/OpenMP/ASan + independent process reads | Existing + DBF3 |
| D7 malformed inputs/failed restore/ownership | adversarial tests across shared contracts | all focused negatives + independent review probes | Existing + DBF3 |
| D7 scaling/independent oracle/evidence | retained measured workloads and new exact-lane oracles | scaling CSV, cross-compiler reads, fixed-source review | Existing + DBF3 |

## Inherited review boundary

The notebook's claim that DBF1 absorbed the entire ALR1 review is unsupported.
DBF1 and DBF2 explicitly decline exhaustive inherited ALR1 certification.
Inherited A1 read-only output, A2 indexes, A3 async ownership, A4 restart-link,
A6 TET/HEX behavior are regression dependencies and targeted review inputs here.
They are not automatically relabeled as independently accepted. A5 CUDA remains
environment blocked. B02c is not merged or accepted by this work.

## Explicit scope boundaries

Index32 capacity checks satisfy this scope; no Index64 runtime claim. Physical
time is the only implemented sample axis; version/kind changes fail closed.
Units are supplied metadata, not an inferred unit system or unit algebra.
Only documented INP subsets are parsed. New coupling/shell/beam/material/activation
physics is not executed. Registered state uses supported dense entity/sample
layouts; existing ragged/sparse primitives and ContactDB remain their own owners.
Production integration and CUDA compilation/device validation are not locally
verified. Neither compression nor a power-failure campaign is part of DBF3.
