# Static source layout draft

Run tools/legacy_layout_draft.py with DBF6_LEGACY_INVENTORY.json and
DBF7_MAPPING_TEMPLATE.json to emit the enriched 2047-key mapping template.
The generated draft keeps disposition and qualification intact. Destination
schema, units, frames and value index base require a semantic mapping or capture.
Field ordinals are one-based; that fact cannot establish the base of stored IDs.

The Windows ABI assumption follows compsch::getFieldType. INT declarations with
nominal size 2 use sizeof(int), and DOUBLE uses sizeof(double) even when the ASC
size disagrees. STR includes a trailing terminator slot. NODE_DATA uses the Win64 layout from old/include/dbtypetyp.h::node_data and old/include/gen1typ.h::point3Df (28 bytes). Count mismatches carry reduced confidence on every
field of that subfile. Native capture and round-trip qualification remain pending.

## Synthetic record probe

tests/legacy_db_synthetic.py builds a one-subfile .db per supported family,
using layout metadata from the ASC inventory. Its writer independently lays
out a 250-byte main header, a 128-byte Win64 _fhead, the case and key arrays,
field ends/types and a second index page. It reads case 2/key 102, and tests
missing cases/pages/keys, deleted keys, truncation and out-of-range addresses.
Use tools/legacy_db_reader.py FILE INVENTORY EXTENSION CASE KEY for inspection.
String bytes are returned as hex to preserve padding and encoding.

The reader assumes little endian, default x64 packing, six i32 date/time fields
and signed 64-bit offsets. It handles indexed records, VARLOC repeated groups and --block block-addressed fixed records. All 25 ASC families have synthetic readback, including the NODE_DATA structure. Record lengths include the first active/deleted status byte. Production packing and word sizes require validation against the captured headers. Sources: old/cacdb/header.cpp (main and subfile headers),
index.cpp::get_key (two-level case index, signed record addresses),
schema.cpp::sch_prepare_fldend, and include/cfmDbase.h::_fhead.
These tests use independent synthetic bytes. They do not certify the production
ABI, foreign endian files or actual plugin data. Local probe budget is 64 MiB.
