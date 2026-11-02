# Translation constraint consumer

Construct physics::PenaltyConstraints from a compiled model and a positive penalty
stiffness. Set nodal lumped masses in StateDB before evaluating. The owned force
is a restoring contribution to add to the right hand side once per evaluation.
For a row with gap g = G u - rhs the result is -k G^T g and the energy is k g^2/2.
For time integration, add frequency_squared_bound to the structural squared
frequency upper bound before computing the central difference limit.

The assembler consumes EQUATION, translation-only rigid and kinematic rows and
normalized distributing coupling weights from CompiledFoundation. For a standalone CPU run, read the INP into ModelIR, set schema=6 and
penalty_stiffness to the chosen positive value, and compile it. CpuRun then
adds the restoring force at every load evaluation and re-evaluates it on
restore. Its step limit is 2/(2/h_structural + sqrt(constraint_bound)).
The schema and stiffness are persisted by ModelStore and included in restart
identity. Other foundation semantics continue through their existing capability
gates. The reference fixture adds a constraint to the forced tetrahedron,
checks the analytic oscillator response, and resumes after 200 increments.
SectionRun also accepts schema-6 equations and couplings. When rotary DOFs
exist, kinematic rows include reference rotation cross lever arm; rotational
equation rows use rotary inertia. A distributing cloud obtains reference
rotation through the normalized weighted rigid-motion least-squares fit about
its centroid, and translates the fitted motion to the reference point. The
transpose produces balanced nodal forces and a reference moment. The consumer
rejects singular clouds for nonzero moment requirements. A pure translational
reference retains the three-DOF weighted translation convention. The test
uses unequal weights and an off-centroid reference to check resultant force,
net torque and rigid-motion invariance. Surface ties and local bases remain
outside this consumer. A penalty
permits a finite gap. Scaling an equation changes its penalty stiffness.
