# Source-side ZDF/ZBF comparison inventory

tools/result_format_inventory.py reads the two legacy exporter translation units
and records source fingerprints, component factory variants and literal writer
arguments with line locations. The JSON is a lexical call-site inventory. Dynamic
key values, plugin implementation and HDF5 object layouts require a written file.

The legacy OpenOutputFile forms <prefix>_Subcase<id>_Step<id>.<suffix>.
zbf/ZBF selects HDF5, other suffixes select JSON. The writer registers header,
result set, optional modal metadata, substep attributes and result components.
Component publication depends on analysis, store flags, element type and location.
For production qualification, compare the same originating subcase, step, entity
ID, basis, location, section/layer and component. Record missing blocks separately
from numeric zeros. Thermal and modal cases require distinct axis metadata.
