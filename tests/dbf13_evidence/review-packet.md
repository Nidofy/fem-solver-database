# DBF13 candidate review

The Track S development package is based on `sdb-2.7.0`. The JIG-3 consumer pin and per-platform results are recorded in [manifest.yaml](manifest.yaml).

## Field coverage

[The field ledger](field-audit.csv) identifies all 2,047 declarations by subfile and one-based ordinal. Each row records the source line, draft ABI layout, assigned responsibility boundary and current mapping disposition. The DBF7 template currently declares all of them unsupported. Reviewed target mappings and paired captures from the same native case are still needed to qualify field semantics.

Run the audit from the SDK checkout with a new output filename.

```sh
python python/sdb_coverage.py --schema ../environ/DBSCHEMA.ASC --output field-audit.json
python python/sdb_coverage.py --mapping reviewed.json --capture legacy-case.json --target sdb-case.json --output case-audit.json
```

The first command cross-checks the original schema, frozen inventory, mapping and draft layouts. The second compares populated fields using the capture ID, exact integer values, units, shape, basis and missing-value policy. Add `--require-mapped` to require a disposition other than unsupported for every declaration. In C++, `sdb::coverage::audit` reports unsupported keys populated by a given capture and rejects unknown or duplicate keys.

## Read-only streaming

Configure with `-DSDB_C_API=ON`. The installed C header `sdb/c_api/history.h` exports `sdb_visit_history_f64_v1` for legacy entity histories and `sdb_visit_history_u64_v1` for exact typed entity histories. The interval includes both endpoints. A callback returns zero to stop. A failed read reports through the caller's error buffer and preserves callbacks already delivered.

The installed Python module `sdb_stream` accepts the shared library path and a callback. Returning `False` stops delivery. A masked UInt64 value arrives as `None`. Callback exceptions propagate to the caller.

```python
from sdb_stream import visit_history

samples = []
visit_history(library, result_path, "EXACT", 1, 0, 17.0, 19.0,
              lambda time, value: samples.append((time, value)), scalar="u64")
```

## Multi-file recovery

`dbf13_partition_faults` terminates a coordinator process after its temporary manifest write, immediately before replacement and immediately after replacement. Independent processes reopen the visible manifest and verify its exact generation. The test also repairs a missing shard and a truncated peer shard one at a time, checks the sibling file family and restores a two-part checkpoint onto three parts.

```sh
ctest --test-dir build -R "^dbf13_" --output-on-failure
```
