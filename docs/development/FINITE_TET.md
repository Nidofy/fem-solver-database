# Current-configuration elastic TET4

All steps declaring NLGEOM=YES select schema 8 and a compressible neo-Hookean
TET4 with the input elastic constants. At each state form F=I+u_,X, J=det F
and current gradients F^-T grad_X N. The stored spatial tensors are Cauchy
stress [mu FF^T+(lambda log J-mu) I]/J and Euler-Almansi strain
(I-F^-T F^-1)/2. The constitutive law is objective under superposed rotation.
The force uses current volume J V0. ALLSE integrates reference energy
mu/2 (trace B-3) - mu log J + lambda/2 (log J)^2.

The local tangent row-sum bound uses the exact derivative of first Piola stress
with respect to F. Step evaluation rejects inverted elements and overlarge dt
at both current and proposed configurations. The interval is fixed, and the
bound does not include external follower-load tangent or eliminate physical
buckling. Pressure normal/area are recomputed from displaced face vertices.
Supported input is scalar CPU elastic C3D4 with fixed geometry mode for the
whole run. J2, HEX8, SIMD and mass/viscosity controls are excluded. Zero stress
under 90-degree rotation, a finite stretch, stretched-face load and exact
FP32/FP64 restart are the local acceptance checks.

## Independent Green-Naghdi material-point update

The TET4 lifecycle above uses a total hyperelastic law. The separate public
green_naghdi_trial(old_point, next_F, bulk, shear) in physics/objective_rate.hpp
integrates an isotropic hypoelastic **Cauchy stress** law. It returns a candidate
GreenNaghdiPoint containing the new spatial stress and total deformation
gradient. Committing consists of replacing the old point after acceptance.
Discarding a trial or receiving an exception leaves both history fields intact.

With F=R U, the stored stress is first rotated into the old polar frame.
Along a segment linear in the two endpoint right stretches, the increment
is the integral of sym(dU U^-1). A two-point Gauss rule is refined until
successive tensor increments agree to 2e-13 times max(1,max-component norm).
The update adds 2G times the deviatoric increment plus K times its trace and
rotates the result back with the new polar rotation. Internal arithmetic is
FP64 for either output precision. The routine rejects reflections, singular
or poorly conditioned stretches and unconverged quadrature. Subdivide the
physical loading path for large noncoaxial increments. This rate law is path
dependent. The neo-Hookean stored energy and tangent used by CpuRun are not
its energy or tangent, so this API has no implicit substitution into CpuRun.

objective_rate verifies transport of anisotropic prestress under a rigid
rotation, an analytic noncoaxial stretch integral, objectivity under differing
endpoint frame rotations, split-increment agreement, reverse loading along
the same path and failed-trial history preservation, at both precisions.
