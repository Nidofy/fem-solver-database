# Six-DOF section consumer

Run sdb_section_run model.inp output.sdbres for a standalone S4R/B31 linear
elastic model. This single-step runner publishes initial/final U and UR and
exposes ModelStore and lifecycle RestartDB through SectionRun. It supports
constant zero translational/rotational constraints, CLOAD or moment with
amplitude, mass lumping, central difference and a conservative local Gershgorin
step limit. The default solid CpuRun capability gate remains separate.

B31 is a two-node reduced shear Timoshenko element with rectangular cross-section
dimensions and an explicit first transverse direction. It includes axial,
torsional, two bending and two shear terms, 5/6 shear factor, approximate
Saint-Venant rectangular torsion and isotropically lumped polar rotary inertia.
S4R is a flat rectangular Mindlin shell with center-point plane-stress membrane,
bending stiffness. Opposite-edge midpoint transverse shears supply the center
and linear shear terms with integrated weights A and A/3. Checkerboard penalties stabilize
unresolved modes; drill stiffness penalizes spin relative to surface curl
and variation of nodal drill. These are standalone coefficient choices.
The implementation rejects warped/nonrectangular shells, alternate profiles,
nonlinear kinematics, multiple steps and unsupported foundation data.

The isolated tests exercise six-DOF rigid motion, a uniform axial oscillator,
FP32/FP64 and model/restart reload. A constant-curvature kinematic field
recovers EI/2 for B31 and D/2 for S4R at unit length or area. Pure shell
transverse shear recovers the 5/6 shear energy. Rectangular beam torsion
matches the independent Saint-Venant odd Fourier series within 1%. Layered recovery, point stresses, rotational
inertia tensors and industrial bending benchmarks remain to be qualified.
