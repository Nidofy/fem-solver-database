# Explicit mechanical energy audit

ENERGY OUTPUT supports ALLIE, ALLWK, ALLVD and ETOTAL in addition to the
existing ALLKE, ALLSE and ALLAE fields. An ALLWK/ALLVD/ETOTAL request enables
run-wide accumulation across every accepted increment, including increments
without output. Defaults retain version 1/2 restart headers. Audited restarts
use solver versions 3/4 (without/with explicit controls), carrying both work
accumulators and checking configuration and finite values on restore.

ALLWK accumulates the start-of-increment applied force dotted with the accepted
half-step velocity times dt. This includes the contact-force buffer. Constraint penalty reactions are excluded
from ALLWK. Their stored potential enters ALLIE and ETOTAL; a follower load is
evaluated at the current start-of-increment configuration. For varying loads
this uses the force that drove the explicit update. Prescribed moving supports
are outside the current homogeneous-boundary scope. ALLVD accumulates the bulk
viscosity power integrated over the reference volume at the force time level.
ALLIE sums recoverable material energy, hourglass energy and, for active J2
points, the exact piecewise-linear integral of the yield stress versus PEEQ
through all attained hardening knots. ALLIE uses the active element set;
energy removed by deactivation needs separate accounting. ETOTAL reports
ALLKE + ALLIE + ALLVD - ALLWK. Its initial value and discrete-time drift depend
on the initial conditions and timestep. It is not a contact friction ledger.

The forced TET fixture provides an independent work reference: unit force
times the single free displacement equals ALLWK. Tests compare float/double
elastic/J2 results, zero and nonzero viscosity, balance and split restart.

## Sectional models

SectionRun supports ALLKE, ALLSE, ALLIE, ALLWK, ALLAE, ALLVD and ETOTAL.
Full-time reconstructed velocities include all three translations and rotations.
ALLWK integrates the applied forces and moments against the accepted displacement
and angle increments, including the compiled distributed loads at their effective
step time. Penalty forces and moments are internal and excluded from ALLWK.
ALLSE is the physical beam/shell strain energy. ALLAE collects the shell
checkerboard and artificial drill modes. Edge shear interpolation belongs to
physical stiffness. ALLIE includes ALLSE, ALLAE and the constraint potential.
ALLVD is zero for the undamped section backend. ETOTAL subtracts ALLWK from
the sum of internal and kinetic energies. The standard audited lifecycle
restart header carries cumulative work through non-output increments.

section_energy parses a rectangular beam deck with a tip torque and an energy
output request. It checks torque times tip angle, the elastic constraint
potential, the energy residual and bitwise restart parity in both precisions.
