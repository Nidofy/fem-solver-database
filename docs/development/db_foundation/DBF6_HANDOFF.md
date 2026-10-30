# DBF6 standalone foundation handoff

Baseline `b489414`; implementation is an uncommitted working-tree change.
Date: 2026-10-30. No production solver changes, merge, push or release.

## Delivered scope

- `sdb/foundation.hpp` exposes standalone data coverage, general DOF space,
  registered state addressing, consumer v1 and standalone state persistence.
- DataCoverage rejects silently omitted capture keys and unsupported populated
  fields. Offline legacy inventory contains 25 subfiles / 2,047 fields with all
  declared counts matching; native mappings are explicitly not qualified.
- DofSpace supports mechanical/thermal/pressure/multiplier/user variables,
  node/element-internal/global ownership, prescribed/inactive/flattened eliminated
  relations, external labels and an equation permutation. The existing mechanical
  DofMap has an additive export bridge.
- Existing RegisteredState now supports explicit dense/ragged/sparse addresses,
  element-node/face/contact rows, context identities, staged identity remapping,
  standalone trial/commit/rollback and existing checkpoint/restart/result paths.
- Consumer v1 can be constructed without CpuExplicit or a production model/kernel.
  It carries arbitrary versioned topology metadata, material/section catalogs,
  explicit samples and DOF lists, schema/capability gates, checked input/output
  views, alias validation, owned result snapshots and portable metadata snapshots.
- FoundationStore reuses native CRC, atomic publication and registered-state blocks;
  failed restore publishes nothing. Batch identity includes material/section and
  topology definitions and can be bound into state restore compatibility.

See [contracts](DBF6_CONTRACTS.md) for exact semantics, ownership and wire boundaries.

## Files and API migration

New public headers: foundation.hpp, data_contract.hpp, dof_space.hpp,
state_layout.hpp, consumer_contract.hpp, io/foundation_store.hpp. Existing public
headers extended: field.hpp, implicit.hpp, registered_state.hpp, state.hpp,
checkpoint.hpp and results/results.hpp. Existing interfaces remain available;
the new package minor version is 2.2.0. Native solver public APIs are unchanged.

Tests: dbf6_contract_tests.cpp, dbf6_wire_oracle.py and the external
install_foundation_consumer CMake project. CMake registers the new contract,
fresh-process reader and independent wire oracle tests. No root solver CMake or
Visual Studio projects changed. The offline inventory tool does not participate
in the library's build or runtime dependencies.

Example entry points:

```cpp
#include <sdb/foundation.hpp>
// Populate neutral declarations in the adapter, independently of old DB types.
sdb::DofSpace equations(definition);
sdb::consumer::v1::Batch batch(batch_data, equations, materials, sections);
sdb::RegisteredState state;
state.bind_context(definition.model, equations.identity(), batch.identity());
// state.add<Field<...>>(descriptor, rows, schema, version, initial, basis, layout);
state.freeze();
// bind_read/bind_write with batch + exact FieldRequirement; validate AccessSet
// before writes. Keep owners alive and layouts unchanged until workers join.
sdb::FoundationStore::save(path, equations, state);
```

Do not substitute the general space for a native equation map by guessing from
node order. Register all required history/analysis cursor fields and expected
layouts before restoring. The old mechanical ModelIR parser/3-or-6 MeshDB route
continues unchanged; general standalone consumers use DofSpace independently.

## Verification record

| Check | Executed result |
| --- | --- |
| Fresh Windows GCC 15.2 Release, OpenMP OFF, zstd OFF | 47/47 CTest PASS, including existing numerical/legacy/restart/result contracts |
| New DBF6 contract suite | 67 checks PASS, including private parallel outputs sharing read-only input |
| Fresh-process reader | 4 checks PASS for DOF, state, batch metadata and batch/state identity |
| Independent Python wire oracle | PASS for little-endian DOFs, sparse/ragged manifest, exact UInt64 and CRC records |
| Windows MSVC 14.44 RelWithDebInfo + AddressSanitizer | DBF6 3/3 CTest PASS; no ASan report |
| Installed external CMake consumer | 1/1 PASS using only `sdb::solver_database_v2` and `sdb/foundation.hpp` |
| Installed compiler dependency inspection | No dynamics.hpp, hex8.hpp, model_compiler, gpdbase or caefemnewinc dependency |
| GCC/MSVC cross-reader | Both directions PASS, 4 checks each; DOF/state/batch identity retained |
| Offline legacy schema inventory | 25 subfiles, 2,047 fields; zero declared/observed count mismatches |
| Diff whitespace check | PASS |

Initial integration attempts exposed two fixture issues: dt=0 is correctly
rejected by existing RestartDB; a repeated run correctly refuses an existing
result path. The fixture now sets dt=.01 and removes only its explicitly named
result output before creation. Original diagnostic logs are retained. Compiler
warnings from existing result aggregate initialization/alignment code remain;
passing is not a warning-free claim. GCC/ASan final checks were rebuilt and rerun
after these fixture corrections.

The full matrix was run without the optional codec and OpenMP; prior DBF5 codec
and OpenMP results are not relabelled as new DBF6 executions. No new TSan, CUDA,
Linux or physical power-loss verification is claimed.

`verification/reviews/DBF6/` retains final logs, a working-tree content manifest,
`verify_packet.py` and a portable PowerShell reproduction script. The manifest
does not create a Git commit or claim an independent code review.

## Limits and assumptions

The implementation establishes the described storage/consumer contracts, not
native field equivalence or production speedup. Full native Felem translation,
all input formats, thermal/frequency ModelIR extension, complex/modal axes, all
element/material algorithms, global memory scheduling, GPU and production cases
are not implemented or verified by DBF6. Original production/CUDA code remains
**not locally compiled / not locally verified**.

Schema-specific section/material physics and elimination/assembly algorithms
remain with the consumer. File budgets are not peak-memory guarantees; trial and
restore still stage copies. Current raw borrows require joined structural
mutation phases; this is not a storage-pinning or automatic race-prevention API.
FoundationStore does not infer field C++ types or new dynamic layouts on restart;
the receiver must register the expected contract. Separate Batch and state files
are matched by identity but are not published atomically as a multi-file set.

Migration order: qualify a populated-field inventory, provide immutable definitions
and equation maps, test standalone roundtrip and consumer contracts, then implement
native-owned read-only mirrors and per-module substitutions. Keep fallback and
numerical/restart/performance comparison gates in the native repository.
