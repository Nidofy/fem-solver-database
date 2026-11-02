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
bending and transverse shear stiffness. Checkerboard penalties stabilize
unresolved modes; drill stiffness penalizes spin relative to surface curl
and variation of nodal drill. These are standalone coefficient choices.
The implementation rejects warped/nonrectangular shells, alternate profiles,
nonlinear kinematics, multiple steps and unsupported foundation data.

The isolated tests exercise six-DOF rigid motion, a uniform axial oscillator,
FP32/FP64 and model/restart reload. Layered recovery, point stresses, rotational
inertia tensors and industrial bending benchmarks remain to be qualified.
