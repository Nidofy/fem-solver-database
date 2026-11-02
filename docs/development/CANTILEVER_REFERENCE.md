# Uniform cantilever first bending mode

The public reference is the uniform nonrotating beam result in NASA CR-197220,
Appendix B, equation B1. For the first transverse mode, beta L is the smallest
positive root of cos(beta L) cosh(beta L) = -1. With beta L =
1.875104068711961 the circular frequency is (beta L)^2 sqrt(EI/(rho A L^4)).
[NASA report and equation](https://ntrs.nasa.gov/api/citations/19950008509/downloads/19950008509.pdf)

The independent case generates a clamped unit-length B31 beam deck with a
0.01 by 0.01 rectangular section, E=100, rho=1 and nu=0. It compiles the
deck, constructs SectionRun, and extracts its actual element stiffness, nodal
translation mass and rotary inertia. Eliminate the root displacement and
rotation, assemble the global y-displacement/z-rotation subsystem, then
bisect on positive definiteness of K-omega^2 M. The analytic frequency is
0.1014986181 rad/time. This compares the discretized beam operator against a
slender-beam limit independently of time integration. Finite shear and rotary
inertia leave a small model difference at fine resolution.

Run CTest cantilever_bending_reference or the sdb_cantilever_reference binary.
The program emits reference-suite-style JSON records with normalized error,
per-mesh limit and pass/fail. Require decreasing error and a relative-error
envelope of 0.4/n^2+0.00015. On Windows GCC 15, both storage precisions passed.
For 4, 8, 16 and 32 elements the FP64 relative errors were 1.66525%, 0.42297%,
0.11210% and 0.03441%. The 32-element mode was 0.1014636903 rad/time.
No production comparison was performed.
