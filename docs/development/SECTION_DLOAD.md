# B31/S4R distributed loads

SectionRun accepts uniform B31 PX/PY/PZ in the global directions and P1/P2 in
the two rectangular section directions. Magnitude is force per unit length.
Each node receives half the resultant. The two-node Timoshenko interpolation
uses linear transverse displacement independent of rotation, so a load through
the centreline contributes no separate nodal couple. Its total moment equals
the centroidal lever arm crossed with the resultant.

Element-based S4R P acts along the positive connectivity normal. Surface-based
DSLOAD P acts inward: SPOS has the opposite sign of SNEG. Pressure is force
per unit area. The rectangular membrane distributes a quarter of the resultant
to each node. B31/S4R GRAV is an acceleration times physical nodal mass;
BX/BY/BZ is force per unit volume times nodal material volume. Reference
section geometry and global or local directions are fixed for this linear run.
The schema-10 line-load definition preserves both direction conventions.
Section-only P1/P2 decks are resolved as beam line loads. Mixed section/solid
execution, user-defined loads, moving follower directions, electromagnetic,
fluid and pipe-specific loads remain outside this consumer.

Successive explicit steps can change loads, time increment and output schedules
with the same homogeneous six-DOF boundary mask. A held step-time amplitude uses
the end of its originating step. OP=NEW clears prior DLOADs without clearing
surface DSLOADs or nodal CLOADs. Half-step velocities are rephased on transition.
Saved cursors include total and local increments and the active step. The tests
compare resultants, moments and section-point recovery after a checkpoint at
the load replacement boundary in FP32 and FP64.

[Beam line loads and shell pressure conventions](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEPRCRefMap/simaprc-c-loaddistributed.htm)
