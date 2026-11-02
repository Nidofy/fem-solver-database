# Translation constraint consumer

Construct physics::PenaltyConstraints from a compiled model and a positive penalty
stiffness. Set nodal lumped masses in StateDB before evaluating. The owned force
is a restoring contribution to add to the right hand side once per evaluation.
For a row with gap g = G u - rhs the result is -k G^T g and the energy is k g^2/2.
For time integration, add frequency_squared_bound to the structural squared
frequency upper bound before computing the central difference limit.

The assembler consumes EQUATION, translation-only rigid and kinematic rows and
normalized distributing coupling weights from CompiledFoundation. It can be used
independently of CpuRun for a stored definition. Rotational arms, ties, local
bases and automatic CpuRun dispatch remain outside this consumer. A penalty
permits a finite gap. Scaling an equation changes its penalty stiffness.
