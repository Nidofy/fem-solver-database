# DBF5 preprocessing migration toolkit

Status: isolated, SDK-independent transfer seam. `migration/preprocessed.hpp`
and its standalone tests are locally compiled and tested. Native Felem/FEG/CAC
integration, production analyses and CUDA are **not locally compiled or
verified**. The local solver source is proxy evidence, not a promise that the
current production ABI or formulation is identical.

The toolkit implements a concrete checked snapshot -> existing `ModelBuilder`
-> frozen `MeshDB` path, equation-vector -> nodal kinematics gathering, checked
consumer `NodeStateView` -> privately mutable legacy scratch, deterministic
nodal contribution reduction and label-based backwrite. It does not create
another database or add a second persistence format. State persistence uses the
existing checkpoint implementation. The original native solver remains intact.

## Evidence and ownership

The parent repository's `docs/CODEX_REPO_MAP.md`, `CODEX_MODULE_NOTES.md`,
`CODEX_INPUT_DB_RESULT_FLOW_MAP.md`, `CODEX_PREPROCESSED_MODEL_RUNTIME_FLOW_MAP.md`
and `session_handoff/DB_SESSION_NOTEBOOK_2026-10-29.md` establish the boundary.
The product chain imports/preprocesses before dispatching analysis; the standalone
module does not replace that chain.

Direct source observations, relative to the parent repository:

| Evidence | Confirmed meaning |
|---|---|
| `pstatnew/wrfnl.cpp:256-269` | Writes NDLB, IDARRAY and SEQND members through the existing FNL writer. |
| `pstatnew/WRFEG.CPP:889-894` | Copies material, property, material-CSYS, element label and total node count into group arrays. |
| `pstatnew/WRFEG.CPP:924-938` | Copies original node labels; `seqndaryptr[ndlab-1]` produces the one-based sequential node. Zero denotes a missing node in this native path. |
| `pstatnew/WRFEG.CPP:1072-1092` | Packs group coordinates into separate X/Y/Z runs; group storage is float. |
| `oldlib/FANLUTIL.cpp:2152`, structural solid branch | Builds node-major `idComponent[0], [1], [2]` LM entries for SOLID/TETRA and related families. Other families use different component/order rules. |
| `oldlib/FANLUTIL.cpp`, following the solid branch | Mixed/hybrid cases append component 6/internal-pressure entries. These are outside this seam. |
| `stat/ee3e.cpp:165-179` | Copies material/property/CSYS, LM, packed coordinates, original labels and sequential nodes into private Felem work. |
| `old/include/femtypetyp.h:211-344` | `fem_elem_data` contains geometry/identity, counts, integration flags, `el_dof`, temperatures, displacement history, loads/reactions and mutable kinematic work. It is much broader than a mesh view. |
| `stat/eecontact.cpp:3968-3971` | Current local source copies `el_dof[i]-1` into `_elementDof`, rather than decrementing canonical input in place. |

The notebook's historical `eecontact.cpp:339` in-place decrement example is
useful adversarial motivation, but is not the current local implementation.
Both forms require explicit separation of one-based equation IDs from
zero-based COO indices. Zero/inactive becomes -1 in a private COO copy and must
be handled by the native assembler's actual policy.

## Transfer schema and mapping

`PreprocessedSnapshot` is an owned, short-lived capture object, not a raw FEG
memory layout and not an on-disk schema. A translator compiled in the native
repository must copy the active, synchronized group into this object. The new
library never includes native solver headers. Native capture should happen
once per preprocessing epoch, under the native owner's synchronization.

| Native source | Transfer field | Existing new-library destination / validation |
|---|---|---|
| `NDLB_PTR_ID` | `fnl.ndlb` | Node labels in sequential-node order; `MeshDB::node_id/index`. Positive signed-32-bit legacy labels, unique. |
| `SEQND_PTR_ID`, indexed by original label minus one | `fnl.seqnd` sparse `(label,one_based_sequence)` pairs | Must be the exact inverse of NDLB; avoids allocating max-label-sized storage in the toolkit. |
| `Coord_xptr/yptr/zptr` or qualified coordinate cache | `fnl.reference` in sequence order | Canonical `MeshDB::reference`; finite double values. No unit or basis conversion is inferred. |
| `IDARRAY_PTR_ID -> idComponent[0..2]` | `fnl.idarray` | Adapter-owned equation map; 0 inactive, positive one-based equation, negative special encodings rejected. Not `MeshDB::dofs` and not an inferred SPC. |
| FNL active equation count | `fnl.equations` | Bounds equation vectors and IDs; signed legacy integer range. Equation numbers need not equal `node*3+component+1`. |
| `egroup_elab` | `FegElement::egroup_elab` | Stable element label; unique. |
| `egroup_elndlb`, `egroup_elseqnd` | Same-named element slices | Exact label/sequence cross-check, then frozen compact connectivity. Missing/zero nodes rejected. |
| `egroup_xyz` | Per-element `x[n],y[n],z[n]` float slice | Must equal the float cast of canonical reference at every node; original floats retained in `LegacyElementBinding::feg_reference`. |
| `egroup_eldof` | Per-element node-major XYZ slice | Must equal the corresponding IDARRAY values exactly. Stored separately in adapter bindings. |
| `egroup_mset` plus native material translation | Same label + explicit `materials` definitions | Resolves existing `MaterialDB`. No constitutive law is guessed from a label. |
| `egroup_gset` plus native property translation | `PropertyBinding{legacy_gset,part,property}` | Explicit mapping to existing `Part`; gset is not automatically a part ID. This narrow seam requires distinct part IDs per property binding. |
| `egroup_mCsys`, `egroup_lcns_exists` | Same-named flags | Only normalized no-orientation `mCsys=0` and no local constraint transforms admitted. Caller must prove any normalization. |
| Element-family / formulation flags | Explicit `topology`, `integration_points`, `hybrid_or_internal_nodes` | Only explicitly qualified Tet4/Hex8 translational families; exact connectivity/LM/coordinate shapes; hybrid/internal nodes rejected. Arity alone cannot identify a formulation. |
| Native populated fields outside this subset | `unsupported_fields` inventory | Any nonempty inventory rejects import. Capture must enumerate omissions; the seam cannot introspect native memory. |

Materials, property links, reference coordinates and topology go into the
existing builder. The equation/FEG precision bindings remain adapter-owned and
are not added to core ModelDB. Captured meshes are deliberately **storage-only**:
the equation map does not supply complete boundary/load/contact/constitutive
semantics. Existing `TetPlan` refuses execution. A production engineer must
translate the complete qualified definition through ModelIR/compiler before
executing a real analysis. Do not remove that gate just to run the mirror.

## Felem responsibility split and adapter recipe

| Native responsibilities | Contract/adapter owner | Native migration action |
|---|---|---|
| `elem_inf` labels, nodes, seqnodes and reference geometry | Frozen mesh + legacy identity/precision binding | Resolve stable IDs once; copy into private native work with native capacity checks. |
| `el_dof`, equation/COO base conversion | Adapter-private scratch | Retain captured LM, including zeros. Never substitute the reference consumer's unconstrained dense numbering. |
| `num_dof`, node counts, integration/formulation flags | Native family translator | Derive from the latest native formulation and verify every count; the generic snapshot cannot initialize the complete native ABI. |
| `totalDisp`, `elemDispPtr`, current/incremental/history vectors | `NodeStateView` + native kinematic policy | Copy the required quantity; explicitly distinguish total, incremental, previous and current-coordinate semantics. |
| Fmp/material parameters and Fshape/geometry tables | `MaterialView` / `ShapeTableView` or qualified native provider | Use declared material/sample schemas; no native pointer ownership crosses the seam. |
| `load`, `rforce`, stress/strain and IP history outputs | Output scratch + deterministic reducer / ResultSink | Keep categories, sample layout, basis, and reduction weights explicit; publish through the existing output owner. |
| Hourglass, pressure, thermal, local-CSYS, complex and uncondensed work | Native family translator | Reject until separately mapped and tested; never fill with invented defaults. |

Implemented and compilable orchestration:

```cpp
auto migrated = sdb::migration::import_preprocessed(captured);
sdb::StateDB<double> state(migrated.mesh);
auto displacement = sdb::migration::gather_equations(
    captured.fnl, equation_displacement, explicit_inactive_nodal_values);
sdb::migration::initialize_kinematics(migrated, state,
    {displacement, nodal_velocity, nodal_acceleration});
auto input = sdb::consumer::NodeStateView<double>::capture(state);
auto work = sdb::migration::make_private_scratch(migrated, sdb::ElementId(0), input);
// Native-side family translator copies work into its own Felem. It owns all
// remaining required native fields, capacity checks and formulation dispatch.
auto nodal = sdb::migration::reduce_nodal(migrated.mesh, contributions);
sdb::migration::backwrite_nodal(migrated.mesh, nodal, native_label_sink);
```

`make_private_scratch` validates the borrowed consumer view at entry and checks
model identity. Its vectors own their memory. Test consumers decrement every
private LM entry and overwrite geometry/displacement; canonical mesh, State and
captured LM remain unchanged. The caller must prevent concurrent structural
mutation/destruction while raw views are in use, as for the existing consumer
contract. A stale resized field is rejected before scratch construction.

`reference` preserves canonical doubles. `legacy_reference` preserves original
FEG floats. The native FEG coordinate reader extends float values into its
double work as well; exact legacy mirroring must use that precision policy.
Choosing canonical doubles can change rounding. Current coordinates require the
native formulation's explicit reference/displacement policy and are not guessed.

`gather_equations` needs caller-supplied inactive values because equation zero
does not prove a prescribed value is zero. Native local-constraint bases and
special negative IDs require their own qualified path and are rejected here.
`initialize_kinematics` validates all three arrays before modifying the State;
these are sequence-order Cartesian nodal values only. No mass, material history,
contact state, load, rotations or solver time is silently transferred.

## Reducer and backwrite template

`NodalContribution` identifies an element, local node, component and value.
`reduce_nodal` first validates every contribution, then sorts by original
element label/local-node/component and sums into owned nodal storage. Duplicate
keys and nonfinite inputs/results reject the complete operation. Separate
categories require separate reduction passes; contributions must already carry
any quadrature/averaging weights. No stress averaging rule is supplied.

The test includes floating-point cancellation `(1e16 + -1e16) + 1` and randomized
worker arrival order, checking the declared label order exactly. This establishes
the template's own order, not bitwise equivalence to an arbitrary native assembly
order. A production mirror must compare the actual native reducer and preserve
its order if bitwise matching is required.

`backwrite_nodal` validates the entire result before invoking a sink with original
node labels. The caller owns native DB/output transactions and the sink's
lifetime. A throwing external sink may have partially written; this template
does not claim cross-record atomicity. Use a staged/transactional native sink
when required. Kernel assembly, constitutive updates and result-file encoding
remain with their existing owners.

## Golden acceptance and local evidence

`tests/dbf5_migration_tests.cpp` defines an independent synthetic 13-node model:
two shared-node Tet4 and one Hex8, sparse nonmonotonic labels, distinct property
and part IDs, reversed equation numbering, and a nonzero inactive displacement.
`tests/fixtures/dbf5/migration-golden.txt` fixes all expected labels, sequences,
equations, coordinates, gathered values, mapped properties and reduced results.
No native input/model equivalence is claimed for this synthetic fixture.

Current standalone GCC 15.2 C++20, O2, `-Wall -Wextra -Wpedantic`,
`-Wa,-mbig-obj`: **113 checks pass**. The test also roundtrips initialized State
through the existing checkpoint and compares the same golden report. This is a
State persistence check; adapter-only equation/FEG bindings are not persisted by
checkpoint and must be captured again or carried by a native-owned artifact.

Negative cases cover zero/duplicate/negative-cast labels, malformed sequence
bases/inverses, missing shape rows, out-of-range/negative IDs, LM disagreement,
coordinate mismatch/nonfinite values, unsupported topology/local axes/hybrid
nodes/material/property/inventory, stale views, incomplete kinematics, duplicate
reduction keys, invalid nodes/components and nonfinite output. Initialization
and sink validation check no mutation before complete validation.

One observational tiny-fixture baseline (Windows, GCC O2, 2,000 iterations):

| Operation | Mean microseconds per call |
|---|---:|
| Validated snapshot -> frozen model | 6.061 |
| Tet4 private scratch from checked consumer view | 0.440 |
| Four-contribution deterministic reduction | 0.075 |

The test prints a checksum and accepts `--benchmark` to repeat measurement. These
numbers include allocation on a tiny synthetic case; they are not a production
speedup, memory scalability result or zero-copy claim. The reference-consumer
baseline is tracked separately in DBF5 consumer verification.

## Native-environment exit gates

1. Freeze the latest native source/ABI and record the preprocessing generation.
   Inventory all populated FNL/FEG/Felem fields and coordinate/equation bases.
2. Implement native-owned capture with signed-ID validation before conversion,
   offset/extent checks before reading flattened arrays, and explicit family,
   material, property, coordinate precision and local-basis qualification.
   The standalone seam receives already extracted per-element slices.
3. Compare captured vs native labels/sequences/LM/geometry/material/property at
   every node/element, including absent/inactive entries. Require 100% mirror
   agreement, with explicit float/double policy. Keep old and new output side by
   side; do not redirect production assembly during this gate.
4. Port the native private-scratch translator for one family at a time. Check
   required counts, flags, all input/history/output fields and mutation ownership.
   Execute single-thread and multithread old/new comparisons for internal force,
   mass, stable dt, stress/strain/history and reducer outputs before expanding.
5. Add actual native golden cases: elastic solid, constrained/prescribed motion,
   shared-node reduction, local axes, mixed/hybrid rejection, tied/rigid/contact,
   and restart. Unsupported families stay rejected until their own gate passes.
6. Measure real cases with matching compiler/options/thread counts, recording
   capture time/bytes, scratch traffic, peak RSS, kernel/reducer timings and
   output comparison. Establish a native baseline before claiming improvement.
7. Preserve current native preprocessing/output fallbacks until native QA signs
   off each family. Keep translators in the native repository pointing toward
   this library. Do not introduce reverse dependencies or copy native algorithms
   into the storage layer.

Files changed for this toolkit: one new migration header, one standalone test,
one golden report and this handoff. No production code, native public APIs,
architecture, numerical kernels or native build files were changed. Remaining
work requiring the production environment is explicit in the gates above.
