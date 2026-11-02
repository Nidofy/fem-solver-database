# Section and contact results

results/mechanical_publishers.hpp accepts compact sample-major recovered section
values. It assigns mesh-defined element, IP, layer and section-point addresses;
ordinary solid points consume no rows. The caller supplies physical units,
component order and shear convention in the descriptor.

append_contact_history appends CSTATUS, CSLIP_ACCUM and CFRICTION_WORK from a
committed general-contact Database. Supply the expected mechanical-state binding
and a stable nonzero domain generation. Result endpoints use the compiled mesh
IDs. Node/face witnesses are supported; rigid surfaces and edge witnesses need
new wire-level endpoint kinds. A zero-based contact point or face ordinal is
translated to its one-based ResultAddress. The database trial, frame, model,
time and snapshot generation are checked before publishing together.

The typed3 round trip in mechanical_publishers checks two distinct section
values, accumulated friction work, slip status and persistent contact identity.

## Physical section backend support

SectionRun and sdb_section_run honor the requested output intervals/frequencies,
regions and field/history flags for S/E. B31 RECT recovers the axial component
at four rectangular corners. In the declared local axes, y changes sign first,
then z, from negative to positive. Axial strain is centreline extension plus
z times the y rotation gradient minus y times the z rotation gradient.
Stress is E times that strain. This scalar does not include torsional or shear
stress distributions. S4R recovers local 11, 22 and 12 at its centre IP and
the declared equally spaced thickness points, bottom to top. It includes
membrane and signed curvature strains, plane-stress elasticity and tensor
shear strain. Artificial drilling and shear stabilization do not enter the
reported material tensor. Every recovery field has an explicit local-to-global
basis and uses typed3 result storage; inactive elements contribute no rows.
U/UR are nodal variables. Other explicit output requests, including energy,
are rejected by this dedicated backend. For a deck without output cards the
CLI retains its legacy initial/final U/UR result.

The section_recovery test verifies both signs of analytic pure-bending stress
and strain, parses S/E output cards, runs the axial fixture, and compares the
typed result frame count, section ordinal, basis and sign after reopening.
