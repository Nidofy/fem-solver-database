# C3D8R CPU formulation and validation

The standalone CPU lifecycle accepts linear isotropic C3D8R solids under
small displacements. It uses the node order and reference mapping of C3D8.
Shape setup integrates volume, row-sum mass and mean spatial gradients over
the eight Gauss locations. Force evaluation uses **one** constant strain and
stress per element. The stored integration weight equals the reference volume.

## Hourglass stiffness

Let G_a be the volume-averaged gradient of N_a. On the eight corners form four
sign patterns H from xi*eta, eta*zeta, zeta*xi and xi*eta*zeta. For each pattern
use gamma_a = H_a - G_a dot sum_b(H_b * (X_b - X_0)). This projection annihilates
affine displacements even on a warped brick. The geometric projection follows
the [Flanagan–Belytschko hourglass construction](https://2021.help.altair.com/2021/hwsolvers/rad/topics/solvers/rad/theory_element_hourglass_modes_r.htm).

For each Cartesian component let q = sum_a gamma_a u_a. This implementation
chooses a fixed elastic coefficient k = mu*cbrt(V)/80 for the unnormalized
sign patterns. Internal force adds k*q*gamma and artificial energy adds
k*q*q/2 per mode and component. The coefficient is a standalone numerical
choice. Production control types and coefficients require independent mapping.
ALLAE reports this artificial energy for active bricks. ALLSE reports the
physical elastic energy. Stability setup includes both stiffness contributions
before taking the absolute row-sum bound divided by the lumped mass.

## Reproducible checks

- On a unit cube with mu=40, each of the twelve pure hourglass modes has
  eigenvalue 4 and zero physical stress. At amplitude 0.01 the energy is 0.0016.
- Warped affine shear tests check stress and artificial energy. For the warped
  fixture with rho=2 and lambda=mu=40, a separate dense stiffness calculation
  gives a row-sum bound of 592.690649841414 and dt of 0.07804400767787935.
- In forced_hex_reduced.inp, the free node's mass is 1/8 and its X stiffness
  is 15 (12.5 physical and 2.5 artificial). The reference displacement is
  (0.001/15)*(1-cos(sqrt(120)*t)). ALLAE is 1.25 times displacement squared.
- Run the existing CTest entries hex8_contracts and lifecycle_process_pipeline.
  They cover both precisions, time and axial mesh refinement, model save/load,
  IP1 results, XML export and split restart. DBF5 consumer tests also exercise
  R/full/TET mixed sets with activity changes and bitwise trajectory parity.

## Migration and review

The legacy explicit database separates reference geometry, nodal mass,
integration data and time-step control. Transfer those meanings explicitly.
ModelBuilder's default IP count is one. For HEX8 migration pass the verified
count explicitly (1 for this uniform strain implementation, 8 for full Gauss).
Consumers should obtain HexPlan::views() or borrow both hourglass fields.
ModelStore rechecks the execution capability when it reloads a saved definition. Existing full-integration configurations retain their restart identity.

This batch implements R5.03/R5.05 on the standalone development branch. Formal
Astra/Opus acceptance, industrial model comparison, CUDA execution and FNL/FEG
capture are pending. Geometry validation samples Gauss locations. Finite strain,
plasticity and viscous hourglass control require separate formulations.

## Executed locally on 2026-11-02

| Toolchain | Verification | Result |
|---|---|---|
| Windows GCC 15.2, OpenMP, 4 threads | hex8_tests, DBF5 consumer including publication, pipeline.py against freshly built sdb_run | Passed, 178781 consumer checks |
| Windows MSVC 19.51 x64, ASan | hex8_tests, FP32/FP64 | Passed |
| WSL GCC 13.3, ASan and UBSan | hex8_tests, FP32/FP64 | Passed |

A separate read-only review found two compatibility cases. A legacy six-field
HexViews for a reduced plan now fails before writing IP results. A non-unit
C3D8R solid section still round-trips as storage-only. Both have regression
tests. Full SDK regression and commercial-solver comparison were outside this run.
