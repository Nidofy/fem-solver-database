# Solver Database V2

Current SDK **2.3.0** adds [DBF7 local foundation extensions](docs/development/db_foundation/DBF7_HANDOFF.md):
streaming indexed queries, owned state leases, atomic metadata/state bundles,
equation-bound Index64 CSR, units/analysis/spectral contracts and optional read-only
C/Python bindings. See the handoff for measured performance, independent reviews,
exact platform checks and explicitly unsupported extensions.

The [DBF6 standalone contracts](docs/development/db_foundation/DBF6_HANDOFF.md)
add general DOF/equation spaces, explicitly addressed registered state,
kernel-independent consumer v1 and a standalone foundation store. Start with
`#include <sdb/foundation.hpp>`; the original DBF6 package version was 2.2.0. Existing mechanical
interfaces and legacy encodings remain available. See the handoff for tested
scope and the distinction between data contracts and native solver integration.

The [DBF1 foundation batch](docs/development/db_foundation/README.md) adds validated
field/history metadata, schema-2 model history bindings, named mixed-IP state
queries, and ragged/sparse host storage. Histories with explicit schemas are
storage-only; unsupported execution is rejected. Schema-1 files remain readable
with unchanged legacy encoding. CPU serial/OpenMP/MSVC ASan each pass 15/15 tests;
this is READY_FOR_REVIEW, not independent acceptance or full D1–D7 completion.

Independent C++20 finite element data infrastructure. The CPU implementation includes
TET4 and full-integration C3D8 small-strain linear elasticity, mixed elements,
read-only snapshots, bounded asynchronous results and linked restart append.
It is built and tested locally with GCC serial/OpenMP and MSVC AddressSanitizer.
Current implementation evidence and review limits are in
[`ALR1 handoff`](docs/development/astra_lifecycle_result/HANDOFF.md). The native
CUDA implementation is **not locally compiled / not locally verified**: this
machine's CUDA configuration fails because `nvcc` is unavailable. See
[`verification/README.md`](verification/README.md) for verification scope.

This Git repository contains only the standalone database module, its contact
extension, reference numerical kernels, examples and tests. It does not include
the parent application, proprietary solver sources, parent Git history, internal
integration notes, local build artifacts or machine-specific verification logs.

## Build and run

From the repository root, with CMake 3.24+ and a C++20 compiler on PATH:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/explicit_tet
./build/sdb_benchmark 1000000 20
OMP_NUM_THREADS=4 ./build/sdb_benchmark 1000000 20 omp
```

Windows executables have `.exe`. In PowerShell use `$env:OMP_NUM_THREADS='4'`.
Use `-DSDB_OPENMP=OFF` for a serial-only build (no OpenMP dependency).
With MinGW, put the **matching compiler's bin directory first in PATH** during
execution; unrelated older `libstdc++` DLLs can cause status `0xc0000139`.
Nothing includes, links or changes the production solver's root CMake project.

CUDA 12+, a supported host compiler and a pool-capable NVIDIA device:

```sh
cmake -S . -B build-gpu -DSDB_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=80 -DCMAKE_BUILD_TYPE=Release
cmake --build build-gpu --parallel
ctest --test-dir build-gpu --output-on-failure
```

Choose the architecture for the actual GPU. On Windows use an x64 Visual Studio
developer prompt and a CUDA-supported MSVC version; MinGW is not a CUDA host
compiler. CUDA tests exercise memory synchronization, event ordering, bounded
candidate generation, manifold state transfer and the float/double TET4 example.
The CPU build never searches for CUDA. Multiple GPUs/MPI are not implemented.

Sanitizers: use `-DSDB_SANITIZE=ON -DSDB_OPENMP=OFF` in a separate build. MSVC
supports AddressSanitizer here (RelWithDebInfo, not `/RTC` Debug). GCC/Clang use
ASan+UBSan when their runtime libraries are installed. Local MinGW has no ASan
runtime; the recorded sanitizer execution uses MSVC instead.

## Public entry points

All headers are under `include/sdb/`; namespace `sdb`.

| Header | Implemented API |
|---|---|
| `memory.hpp` | 64-byte aligned STL allocator, checked sizes, arena, per-thread scratch, counters |
| `field.hpp`, `schema.hpp` | `Field<T,C,Layout>`, checked views, SoA/AoSoA, registry descriptors, HistorySchema |
| `storage.hpp` | checked ragged/sparse host fields, integer/validity metadata, native DTO encoding |
| `model.hpp` | `ModelBuilder<T>`, frozen `MeshDB<T>`, materials, sparse labels, batches, sets, DOFs, surfaces |
| `state.hpp` | persistent explicit fields, packed IP histories, rotations, current-coordinate view, trial/commit/rollback |
| `contact.hpp` | face connectivity, bounded parallel candidates/batches, generation-checked manifold pool |
| `implicit.hpp` | DOF map, CSR/BSR storage and apply, residual/tangent, matrix-free callback |
| `checkpoint.hpp` | versioned, checksummed, portable state checkpoints and atomic file replacement |
| `database.hpp` | optional aggregate owner and coordinated state/contact transactions |
| `dynamics.hpp`, `hex8.hpp` | TET4/HEX8 reference geometry, lumped mass, serial/OpenMP forces and central difference |
| `cuda_memory.hpp` | device pool, stream, event, pinned memory, explicit resident field mirrors |
| `cuda_contact.hpp` | GPU candidate append and contiguous manifold upload/download |
| `cuda_backend.hpp` | single-GPU TET4 execution, implemented in `src/cuda_backend.cu` |

Example with arbitrary sparse external labels:

```cpp
#include <sdb/database.hpp>
#include <sdb/dynamics.hpp>

sdb::Database<double> db(sdb::oscillator_model<double>());
sdb::CpuExplicit<double> run(db.model(), db.state(), sdb::CpuPolicy::Serial);
auto node = db.model().node_index(40);
db.state().data().displacement.write()(node, 0) = 0.01;
run.initialize(db.state(), 0.0001); // centered v0 -> half-step v[-1/2]
for (int step = 0; step != 1000; ++step) run.step(db.state());
db.save("run.checkpoint");
```

For a new mesh, use `ModelBuilder::add_material`, `add_part`, `add_node`,
`add_element`, optional sets/surfaces/constraints, then `finalize()`. Finalization
is the topology mutation boundary. Arbitrary constitutive schemas use numeric
model IDs, parameter arrays and a per-IP history count, with optional validated
HistorySchema metadata; only legacy material model 1 without an explicit schema
has an implemented physics kernel here. It uses `[density, Young's modulus, nu]`.

Install with `cmake --install ... --prefix <prefix>`. Consumers use
`find_package(SolverDatabaseV2 2 CONFIG REQUIRED)` and link
`sdb::solver_database_v2`; CUDA consumers additionally link `sdb::sdb_cuda`.
CPU template code instantiates in the consuming translation unit.

## Numerical scope and reference

The executable runs two positive-volume TET4s sharing a face. With `rho=1`,
`E=100`, `nu=0`, all but the x DOF at node 40 are constrained. That DOF has
mass `rho/24` and stiffness `E/6`, hence `omega=20` and
`u(t)=0.01*cos(20*t)`. Constant `dt=0.0001` is below the conservative geometry
bound. Tests also exercise affine strain, rigid translation, force balance,
shared-node assembly, deterministic versus atomic OpenMP, and split restart.

The time-step API stores displacement at t[n] and velocity at t[n-1/2]. Each
step evaluates forces at t[n] then advances velocity/displacement. Consequently
stored stress/force after step belong to the preceding force evaluation.
`CpuRun::snapshot` reconstructs current-time quantities in reusable read-only
scratch; `CpuExplicit::output_forces` is the low-level derived-data alternative. Changing
the time step requires a deliberate velocity re-centering scheme; this example
implements constant dt. Linear small-strain TET4 and full-integration HEX8 are
CPU physics kernels. Beam/shell/reduced-hex/polygon storage does not imply
corresponding physics support. The [C3D8 contract](docs/development/astra_lifecycle_result/C3D8.md)
defines quadrature, mass, stability, IP order and numerical verification.

Read [`docs/LAYOUT_AND_SYNC.md`](docs/LAYOUT_AND_SYNC.md) before borrowing views.
# Lifecycle extension (2.1)

The standalone CPU path now accepts a documented Abaqus subset and persists
models, results and linked restart state separately. Executable fixtures are in
`tests/fixtures/abaqus`; parser/compiler capability checks are in `include/sdb/input`
and `include/sdb/model_compiler`. Verification is summarized in
[`verification/README.md`](verification/README.md).

From this module directory after building, choose new output paths:

```text
build/sdb_run --input tests/fixtures/abaqus/forced_tet.inp --backend cpu --precision double --output run.sdbres --save-model run.sdbmodel
build/sdb_run --read run.sdbres --field U --region FREE --entity 4
build/sdb_run --read run.sdbres --vtk vtk-output --xdmf run.xdmf
build/sdb_run --model run.sdbmodel --output split.sdbres --stop-after 700 --checkpoint run.sdbrst
build/sdb_run --model run.sdbmodel --output split.sdbres --restart run.sdbrst --append
```

The `.sdbres` backend is native block I/O, not HDF5/ODB. C3D4 and full-integration C3D8 linear isotropic
small-strain CPU dynamics are the executable subset; `NLGEOM=NO` must be explicit.
Other stored formulations are gated before kernel construction. CUDA lifecycle
execution, general Abaqus compatibility and production integration are not
claimed. Existing low-level APIs and original tests remain supported.

## Independent general contact

Include `<sdb/general_contact/module.hpp>` and use
`sdb::general_contact::GeneralContact<T>` with an existing MeshDB and StateDB.
The shared explicit/implicit CPU path provides solid exterior domain rules,
Triangle/Quad projection, same-facet objective penalty/Coulomb contact, geometric
Jacobians, selected frictionless rounded edges, monitoring and atomic coupled
restart. The example `examples/general_contact_domain.cpp` demonstrates both
analysis paths. OpenMP parallelizes local projection, differentiation and laws;
some search and assembly stages remain serial.

Cross-facet friction history transfer, full deforming CCD, mortar, shell/thermal
contact and MPI/CUDA contact execution are not complete. The standalone static
triangle TOI primitive does not provide automatic whole-domain time-step cutback.
No commercial-solver feature parity or production qualification is claimed.

## Optional compressed results and consumer migration

DBF5 adds opt-in `CompressedIndexed4` results, checked consumer contracts and a
standalone FNL/FEG/IDARRAY migration toolkit. Configure with `-DSDB_ZSTD=ON`,
`-DSDB_ZSTD_INCLUDE_DIR=<SDK include>` and `-DSDB_ZSTD_LIBRARY=<SDK library>` to
link a real zstd SDK (1.5.5 or newer with the required advanced allocator API).
The default build remains dependency-free with respect to zstd. Unavailable
codecs are rejected explicitly. Install/export preserves this optional dependency.

```text
build/sdb_run --input tests/fixtures/abaqus/forced_tet.inp --output compressed.sdbres --compression zstd --compression-fields U:1,S:none --async-mib 64
build/sdb_run --read compressed.sdbres --verify
build/sdb_run --read compressed.sdbres --convert-result decoded.sdbres --compression none
```

Conversion writes a new result file, retains exact typed values and does not
create or transfer a restart continuation link. Append adopts the saved policy
when none is supplied; an explicitly conflicting policy fails before tail repair.
New format files require a v4-capable reader. Old formats remain readable.
See [DBF5 handoff](docs/development/db_foundation/DBF5_HANDOFF.md),
[format specification](docs/development/db_foundation/RESULT_FORMAT_SPEC.md),
[consumer contracts](docs/development/db_foundation/DBF5_CONSUMERS.md) and
[migration guide](docs/development/db_foundation/DBF5_MIGRATION.md).

## Indexed results, asynchronous output and safe continuation

```text
build/sdb_run --input tests/fixtures/abaqus/mixed_hex_tet.inp --output mixed.sdbres --indexed --async-mib 16 --stop-after 333 --checkpoint mixed.sdbrst
build/sdb_run --input tests/fixtures/abaqus/mixed_hex_tet.inp --output mixed.sdbres --restart mixed.sdbrst --append --async-mib 16
build/sdb_run --read mixed.sdbres --field S --entity 1 --ip 8
```

`--indexed` selects native indexed v2 for a new file. Legacy v1 remains the default
for byte compatibility; both formats are auto-detected on append/read. V2 opens
metadata and a fixed footer, then reads selected fields/entity blocks on demand.
Each v2 file has its own nonce, so compare decoded values across independent runs.
`ResultReader::index` now provides a read-only lazy metadata facade; size, empty,
at, subscript, front/back and const iteration remain available. Mutating a vector
of frame metadata is no longer part of the supported interface. Entity rows in
v2 use ascending internal IDs; SourceMap retains external labels and instances.
Use independent readers for simultaneous queries.

`--async-mib` bounds admitted snapshots plus encoding/validation scratch and
in-flight work; it excludes persistent model/solver data and the producer's current
frame. Oversized frames throw, queue saturation blocks, and close reports worker
errors. Always explicitly close an `AsyncResultWriter` before reporting success.
Legacy v1/v2 widen float losslessly to double. Optional typed v4 compression
preserves all seven scalar lanes exactly; see the DBF5 section below.
A completed flush is not a guarantee of physical power-loss durability.

`NativeResultStorage::barrier(level)` and `AsyncResultWriter::barrier(level)`
acknowledge a byte-prefix watermark at `StreamFlush`, `FileSync`, or
`PublicationSync`. The last level additionally syncs the parent directory and is
explicitly unsupported on Windows. Async barriers execute on the worker after
draining queued frames; `Stats::committed` still counts stream-flushed frames.
Write/sync failures poison the writer and must be observed by the caller.

CLI completion now defaults to `--durability file`; `stream` explicitly requests
the weaker stream flush, and `publication` requests strict parent-directory sync.
CLI checkpoints use `CheckpointLink::publish_durable` after joining async output:
result barrier, atomic restart, then atomic link. This ordering is not a multi-file
atomic transaction. `LinkPublicationError` reports the failing phase and whether
restart/link destinations may have changed; `durability::PublicationError` reports
replacement status and retains its underlying cause. Reopen and verify after an
ambiguous failure. Existing path-based `publish` remains compatible with its
caller-managed lease/flush contract. See [DBF4 contracts](docs/development/db_foundation/DBF4_CONTRACTS.md).

CLI checkpoints publish a `.sdbrst.result-link` companion after result commit.
Keep it with the checkpoint: append verifies the exact result prefix and refuses
result-ahead, checkpoint-ahead, changed/missing files or an occupied writer.
Legacy unlinked checkpoints remain readable and may branch into a new result file.
`--recover-tail` explicitly repairs an incomplete tail only after checking the
linked prefix; CRC damage is rejected. A `.writer-lock` directory implements the
cooperative single-writer rule. A crashed writer's stale lock requires explicit
operator recovery after confirming the owner is gone.

IP1 is available as S/E; IP2..8 are independent fields selected with `--ip` and
exported as S@IP2..8 / E@IP2..8. TET4 has no rows at IP2..8. Visualization fills
those absent cells with NaN, never with invented stress values.
