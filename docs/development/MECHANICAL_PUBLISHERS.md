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
