# DBF8 local extension plan

Baseline: 30bedf6. User requests full INP, global MeshDB Index64, COW and process memory control.

- Global selectable Index32/Index64 ABI: entity IDs, connectivity, source maps, matrices, CPU consumers and native file width gates.
- Explicit COW trials: ordinary copies stay deep; shared field values detach on write; leases remain mandatory for owned consumers.
- C++ process allocation governor and OS-limited worker launcher, with exact metric boundaries (committed memory/address space are not RSS).
- Full file-layer INP document reader/writer with preserved unknown cards and audited semantic capability. Version/all-keyword semantic interpretation requested as an optional clarification; no claim of arbitrary new solver physics.
- Independent regression/platform verification and updated migration/coverage evidence.

Completion: see DBF8_HANDOFF and the fixed evidence manifest. The user confirmed
file-layer complete INP; all three requested local extension areas are implemented
and verified within their explicit SDK/OS/semantic boundaries.
