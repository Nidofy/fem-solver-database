# Solver Database V2

Independent C++20 finite element data infrastructure. The CPU implementation is
built and tested locally with GCC/OpenMP and MSVC AddressSanitizer. The native
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
| `field.hpp` | `Field<T,C,Layout>`, `View`, checked host views, `SoA`, `AoSoA<W>`, registry |
| `model.hpp` | `ModelBuilder<T>`, frozen `MeshDB<T>`, materials, sparse labels, batches, sets, DOFs, surfaces |
| `state.hpp` | persistent explicit fields, packed IP histories, rotations, current-coordinate view, trial/commit/rollback |
| `contact.hpp` | face connectivity, bounded parallel candidates/batches, generation-checked manifold pool |
| `implicit.hpp` | DOF map, CSR/BSR storage and apply, residual/tangent, matrix-free callback |
| `checkpoint.hpp` | versioned, checksummed, portable state checkpoints and atomic file replacement |
| `database.hpp` | optional aggregate owner and coordinated state/contact transactions |
| `dynamics.hpp` | TET4 reference geometry, lumped mass, serial/OpenMP forces and central difference |
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
model IDs, parameter arrays and a per-IP history count; only material model 1
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
stored stress/force after step belong to the preceding force evaluation; call
`CpuExplicit::forces` to produce CPU output at the final displacement. Changing
the time step requires a deliberate velocity re-centering scheme; this example
implements constant dt. Linear small-strain TET4 is the only physics kernel.
Beam/shell/hex/polygon storage does not imply corresponding physics support.

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

The `.sdbres` backend is native block I/O, not HDF5/ODB. C3D4 linear isotropic
small-strain CPU dynamics is the executable subset; `NLGEOM=NO` must be explicit.
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
