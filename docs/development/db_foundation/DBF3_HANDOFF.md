# DBF3 phase-one handoff

Status: **COMPLETE within the agreed D1-D7 storage/semantic scope**.
Independent Astra/high review: **PASS WITH EXPLICIT SCOPE LIMITS**.
GCC serial, GCC OpenMP and MSVC ASan: **34/34 each**, all exit0.
GCC↔MSVC fresh model/state/result readers: **129 checks each direction**, exit0.

Final reviewed library: `c53ab3e462bc44740f1de91f91efa72db0eefbbf`.
Final test followup: `4b124df1144be703885edc59feafd0869790b069`.
Library code has not changed after independent review. The packet manifest
records the later evidence/documentation content commit; it does not change the
reviewed source boundary. Branch: `codex/astra-db-foundation-completion`.

## Commit → task → evidence

| Commit | Task | Evidence |
|---|---|---|
| 47cd22e | DBF3-A identity/topology/axis, DBF3-B schema4 semantics and controlled input, DBF3-C typed registered state and local bases, DBF3-D integration/goldens | dbf3_contract/model/state/result + independent readers |
| a7dec76 | reject invalid checkpoint inertia before publication; add independent registered-state wire oracle | rotation/state tests, Python LE oracle, independent inertia red/green |
| 926c6bd | stage CpuRun restore/import mask/load preparation before publication | float/double allocation fault campaigns + independent183-point restore sweep |
| c53ab3e | correct inherited step-time amplitude origin and selective run configuration identity; clarify InstanceId order |233 model checks, real transition and two split-restores, independent amplitude red/green |
| 1d5e45b / 4b124df | fresh state reader uses persisted source ModelStore, with stage diagnostics | all final matrices and bidirectional cross-compiler129-check reads |

## Changed module boundaries

- `topology.hpp`, `schema.hpp`, `model.hpp`: versioned numbering/axis contract and additive typed access.
- `model_ir/{model_ir,foundation,foundation_validate}.hpp`, `model_compiler/compiler.hpp`, `input/abaqus.hpp`, `io/model_store.hpp`: conditional model4 tail, scoped coupling/property/amplitude/event semantics, queries and controlled parsing.
- `registered_state.hpp`, `state.hpp`, `checkpoint.hpp`, `io/restart.hpp`, `lifecycle.hpp`: frozen typed fields, initialization/validity/policies, rollback, checkpoint3/Restart5, transactional CpuRun restore and configuration correction.
- `results/{results,indexed_format}.hpp`: owned registered snapshots, scoped local-history basis catalogs, typed frame lookup and basis-group query.
- Standalone CMake/tests/fixtures, foundation contracts/ledger, verification packet and original parent repository maps/migration/task notes were updated. Root production build files and numerical kernels were not changed.

## Scope and assumptions

New coupling, property, shell/beam/material and activation definitions are
storage-only; execution gates remain. Existing TET4/HEX8 CPU mechanics are
regression-tested. Destination extension contracts are registered/frozen before
restore. Packed per-material histories remain FP32/64. Seven-lane registered
fields cover documented dense entity/sample layouts, not arbitrary integer
constitutive histories or generic Face/Contact registration. Those existing
owners and explicit-address result routes remain distinct.

Topology/model/schema failures reject rather than substitute physics. Units are
supplied metadata or UNSPECIFIED. Current result axes are implicit physical time;
custom axis units require a future result format and explicitly reject today.
No full INP grammar, Index64 runtime, new frequency/modal arithmetic or new
physical kernels are claimed. No production/CUDA build/device validation was
possible. Durability enhancement, power-loss campaigns, compression, view-based
production consumers and migration adapters belong to later phases.

## Compatibility and migration

Rebuild consumers after header changes. Existing no-extension encodings remain
byte compatible and earlier schema files retain readers. Registered state adds
conditional required schemas; unknown/mismatched layouts or initialization reject
without publication. Restore from a saved ModelStore, preserving its provenance,
rather than assuming reimported source text has identical strict identity.

The amplitude fix changes configuration identity only for runs whose inherited
step-time loading changes behavior. Old affected checkpoints require an explicit
restart/branch decision; no silent numerical continuation is performed. This
does not force migration of unaffected single-step or total-time configurations.

No automatic acceptance of all inherited ALR1 or B02c occurred. No push, merge
or release was performed. The user's original completion kit remains untouched
and intentionally untracked; unrelated parent changes are preserved.

## Evidence and next boundary

Read DBF3_REQUIREMENTS for original-clause coverage, DBF3_CONTRACTS for API/wire
rules, DBF3_REVIEW_FIXES for findings, and DBF3_VERIFICATION for commands and raw
test/scaling/cross-compiler evidence. Run
`python verification/reviews/DBF3/verify_packet.py --git` to verify fixed Git content
or omit `--git` for an exported packet/current working-tree byte check.

Phase one stops here. The approved plan's next distinct phase is durability
contract/fault testing; compression follows separately. Neither is included in
this completion claim or started automatically by this handoff.
