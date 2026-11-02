# Static source layout draft

Run tools/legacy_layout_draft.py with DBF6_LEGACY_INVENTORY.json and
DBF7_MAPPING_TEMPLATE.json to emit the enriched 2047-key mapping template.
The generated draft keeps disposition and qualification intact. Destination
schema, units, frames and value index base require a semantic mapping or capture.
Field ordinals are one-based; that fact cannot establish the base of stored IDs.

The Windows ABI assumption follows compsch::getFieldType. INT declarations with
nominal size 2 use sizeof(int), and DOUBLE uses sizeof(double) even when the ASC
size disagrees. STR includes a trailing terminator slot. NODE_DATA depends on an
external structure layout. Count mismatches carry reduced confidence on every
field of that subfile. Native capture and round-trip qualification remain pending.
