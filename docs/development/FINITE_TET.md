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
