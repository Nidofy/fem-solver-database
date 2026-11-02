# Distributed loads in small-strain solids

The INP solve reader accepts step-level DLOAD with OP=MOD and optional AMPLITUDE.
BX, BY and BZ are force per reference volume. GRAV uses material density and
a normalized direction. P1 through P6 generate compressive pressure on the
corresponding valid solid face. C3D4 uses exact constant volume/triangle
integrals. C3D8 and C3D8R use 2x2x2 geometry integration for body loads and
2x2 bilinear area-vector integration for quadrilateral pressure faces.
The nodal equivalents retain amplitude and originating-step information so
local- and total-time amplitudes follow the existing LoadEvaluator contract.
Variables are stored as schema-7 typed body/face loads and resolved at compile
time. The physical density is independent of numerical mass scaling.
Rotational inertia loads, user subroutines, shell/beam distributed loads,
hydrostatic depth laws and current-configuration follower pressures require
additional formulations. Unsupported DLOAD tokens fail at import.
