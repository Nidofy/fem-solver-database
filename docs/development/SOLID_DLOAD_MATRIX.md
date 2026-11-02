# Native small-strain 3D solid distributed loads

| Element-based keyword | Standalone implementation |
|---|---|
| BX, BY, BZ, GRAV, P1 through P6 | Existing force-per-volume, acceleration, or inward face pressure. Face indices must exist. |
| CENT, CENTRIF | Radial acceleration per unit angular speed squared projected normal to the supplied axis. CENT magnitude already includes density. CENTRIF multiplies physical material density. |
| ROTA | Negative angular acceleration crossed with position relative to the axis, weighted by material density. |
| CORIO | Magnitude includes density times angular speed. A row-sum lumped per-node coefficient multiplies minus twice the cross product of axis direction and velocity. |
| HPn | Inward pressure varying linearly with global Z, zero above the surface. The reference pressure level must be below the zero level. |
| OP=NEW | Replaces all propagated element-based DLOADs, including loads defined on other sets. CLOAD and surface-based DSLOAD have independent histories. |
| Whole-model target | Blank gravity/rotation target selects the whole mechanical solid model. |

CENT/ROTA fields use consistent body quadrature (degree-two tetra rule and
2x2x2 brick rule). The axis is fixed in the reference frame. For CORIO the
per-node 3x3 skew operator is integrated at the centered velocity via a Cayley
solve restricted to free DOFs. This preserves kinetic energy of free rigid
translation in a pure Coriolis field. Simultaneous artificial bulk viscosity is
rejected until a coupled velocity treatment is validated. HPn clips integration
intervals at the waterline before face quadrature. On a general bilinear warped
face this is numerical quadrature. Magnitude alone is amplitude-scaled.

Schema 9 identifies the additional load data. Older schemas retain their
native layouts. MeshOnly preserves unsupported element DLOAD cards as opaque.
The rotating-frame and hydrostatic variants use Standard's keyword conventions
as standalone CPU extensions. Finite-configuration rotation/HP, ROTDYNF
complex rotor matrices, user DLOAD/VDLOAD callbacks, ambient fluid/porous,
pipe/elbow, acoustic, traction and Aqua load families have no execution
route in this slice. Such cards require a corresponding formulation, reference
state or external data. The parser rejects unknown load types.

[Keyword rows and product scope](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEKEYRefMap/simakey-r-dload.htm)
[Rotating-frame force derivation](https://docs.software.vt.edu/abaqusv2025/English/SIMACAETHERefMap/simathe-c-centcoriolis.htm)
[Hydrostatic pressure conventions](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEPRCRefMap/simaprc-c-loaddistributed.htm)

CTest inertial_load_contracts checks both precisions, signed resultants,
partial immersion, pure Coriolis energy, OP=NEW and exact split restart.
