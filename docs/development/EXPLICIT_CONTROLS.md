# A3. Fixed-reference explicit controls

C3D4 (elastic or J2), C3D8 and C3D8R accept run-wide CPU controls. Defaults
retain the legacy mass, timestep and restart configuration. For example:

    sdb_run --input model.inp --output result.sdbres --refined-tet-dt --mass-target-dt 0.001 --mass-elset SOLID --max-added-mass-fraction 0.05 --bulk-linear 0.06

The SDK entry points are CpuRun(model, controls) and the trailing ExplicitControls
argument to CpuExplicit. These options belong to the run configuration. Supply
the same options when resuming a lifecycle checkpoint. A .sdbmodel stores the
physical material data. Configuration-free v2 checkpoints cannot encode a
controlled run. Controlled lifecycle restarts carry solver version 2 and the
control fingerprint. A low-level RestartDB::load requires a StateDB already
bound to matching controls; it preserves that binding during replacement.
Default lifecycle restarts retain solver version 1. Quasistatic equivalence or accuracy after scaling must be checked
against an unscaled trajectory for the model of interest.

## Fixed mass scaling

Choose --mass-factor F (F >= 1) or --mass-target-dt h. The latter only adds
mass where the element's damped stability bound is below h. F=(h/h_e)^2 for
those elements. Scaling applies once at setup, to each selected element's
row-sum nodal masses before shared-node assembly. Stiffness, material density,
constitutive response and external force definitions retain their physical
values. An optional ELSET restricts the mass change; without it all elements
are eligible. --max-added-mass-fraction rejects a run if added mass divided by
original total mass exceeds the specified budget. A target is an element limit;
the global minimum, the automatic 0.8 factor and the step period still govern
the actual increment. An unselected neighbor can control the run.

The CLI reports total physical/added mass and the controlling source instance,
element label, mass factor and stable dt. CpuRun::controls().elements exposes
the physical mass, undamped dt and applied scale for every compiled element.
This is the fixed-initial subset of the legacy FACTOR and BELOW_MIN semantics.
Time-varying scaling, subcycling and INP mass-scaling cards are separate work.

## Bulk viscosity and stability

Let L = cbrt(reference volume), c = sqrt((lambda+2 mu)/rho), and theta =
div(v) at the stress integration point. Use reference density and shape gradients
and the stored staggered velocity, respecting fixed and inactive node DOFs.
Linear viscosity adds hydrostatic stress b1*rho*c*L*theta to internal force
only. Reported S and ALLSE describe the material. At each integration point
the bulk contribution to nodal internal power is V_q * p * theta >= 0.
The linear and compressive quadratic terms follow the
[explicit bulk viscosity formulation](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEGSARefMap/simagsa-c-ovwbulkvisc.htm).

The standalone quadratic variant requires **both** --bulk-quadratic b2 and
--max-compression-rate R (inverse time). It adds rho*(b2 L)^2 times theta
times min(R,max(0,-theta)). The coefficient saturates for compression above R,
so the force is continuous, dissipative, and has bounded slope. A small R can
reduce shock damping. The linear and quadratic coefficients both default to zero.

--refined-tet-dt uses the absolute row-sum bound of M_e^-1 K_e including all
translational DOFs, with h=1.9/sqrt(bound). The element volume cancels for
TET4. HEX8 already computes the row sums including the C3D8R hourglass term.
An assembled system with shared nodes inherits the largest local bound by the
Rayleigh quotient. This estimate uses the elastic stiffness for J2.

For viscosity, let B=lambda+2mu/3 and eta=b1*rho*c*L+2*rho*(b2 L)^2*R.
The viscous tangent is bounded by (eta/B)*K_elastic in quadratic form because
the elastic energy contains B*theta^2. If the undamped bound is h, the damped
bound is h/(hypot(1,r)+r), r=eta/(B*h). This enforces dt^2 K + 2 dt C <= 4 M
for the linearized backward-velocity central difference update. With scaled
inertia, multiplying this damped step by sqrt(F) is conservative. The viscous
coefficients always use the physical density. The bound is a local stability
estimate. It does not predict accuracy, shock resolution or total runtime.

## Reproducible acceptance

Run explicit_control_contracts and lifecycle_process_pipeline in the standalone
build. The forced two-TET fixture, with its increment set to automatic and
unchanged mass, takes 18 legacy increments or 7 refined increments over 0.2 s.
The refined displacement at the free node is about 0.09659 versus the analytic
0.09922 (absolute budget 0.003). A separate fixed-step test checks that scaling
mass by four doubles the oscillator period. A damped free oscillator checks
the analytical displacement and accounts for energy lost to viscous work with
a 0.3% relative budget. FP32/FP64 patch tests cover compression below/above
the quadratic cap, expansion, a divergence-free velocity, equilibrium and
material stress. Other checks cover local scaling, an unscaled controlling
neighbor, configured mass budget rejection and exact split restart.

Windows GCC 15.2 ran both precisions, including deterministic serial/OpenMP
force parity. Windows MSVC 19.51 ASan and WSL GCC 13.3 Index64 ASan/UBSan
passed the numerical and restart contracts. The separate-process pipeline
compared a controlled result file byte for byte across checkpoint append and
rejected a resume with missing controls. The existing lifecycle suite passed
78,323 checks. Independent batch review identified control/state mismatches and
failure during step rephasing; the implementation binds the low-level state and
restores the prior cursor, mask and fields on a failed viscous rephase.

Scope corresponds to the small-strain part of R6.05. The finite-deformation
prerequisite R6.03, industrial model comparisons, production FNL/FEG capture,
and CUDA verification are pending.
