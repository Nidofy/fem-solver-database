# Native result format specification — versions 1 through 4

This describes the existing standalone codecs, not the production legacy DB or
an HDF5/ODB implementation. All integers and IEEE binary32/binary64 scalars are
little endian, with no structure padding. Strings are u64 byte length followed
by bytes; vectors are u64 count then elements; fixed arrays have no count.
Native enum fields use u32, bool uses u8 0/1. The explicitly packed version4
policy/envelope codec identifiers below use u8 instead. Reads reject unknown
required versions/kinds, invalid counts and out-of-budget allocations.

## Common framing

Prefix: u64 magic followed by u32 framing version=1 (12 bytes).
Record: u32 kind, u64 body length, u32 CRC32(body), followed by body (16-byte
header). CRC32 uses reflected polynomial 0xedb88320, initial/final xor 0xffffffff.
CRC and FNV identities detect accidental corruption/mismatch; neither authenticates
data. Typed payload visitors in results/typed.hpp and native schema visitors in
results/results.hpp define field order. Native1 and Indexed2/3 goldens remain
byte-compatible; compressed blocks are never slipped into an older magic.

| Format | u64 magic | Layout |
|---|---|---|
| Native1 | 0x31534552424453 | model record0 then complete frame record1 sequence |
| Indexed2 | 0x32534552424453 | model0, identity10, initial footer5, append blocks2/directory4/footer5 |
| TypedIndexed3 | 0x33534552424453 | typed model0, identity10, same indexed commit sequence |
| CompressedIndexed4 | 0x34534552424453 | typed model0, compression policy11, identity10, same indexed commit sequence |

Native1 frame bodies serialize FrameInfo then vector<ResultField>; model0 uses
ResultModel's legacy visitor. Indexed2 value blocks contain vector<u64 entity ID>
and vector<f64> component values. TypedIndexed3 model0 extends that ResultModel
with sample-point layouts and basis catalogs. Its directory stores FrameInfo,
ResultBinding and field metadata, including schema and schema identity/version,
plus block full-address bounds. A typed value block serializes FieldDescriptor,
ResultAddress vector, validity bytes, schema ID/version, basis ID and the exact
scalar vector selected by FieldDescriptor::scalar. Missing values are canonical
zero with validity0; valid signed zero and integer extremes remain lossless.

## Indexed transaction and selective access

Identity10 is two u64 values: model/policy fingerprint and per-file random nonce.
For versions2/3 the fingerprint is FNV-1a(model0 body), offset basis
14695981039346656037, multiplier1099511628211. Version4 continues that same hash
over the policy11 body, binding compression configuration to every footer.

A reference is offset:u64, body length:u64, body CRC:u32. Directory records hold
references to value blocks. Footer5 holds Identity, count:u64, FrameInfo,
directory Ref, and 64 u64 ancestor offsets. FrameInfo is id/step/increment/
mesh_revision (four u64), time/local_time (two f64), precision/storage_precision
(two u8). Footer body is606 bytes; record total622 bytes. Ancestor[k] jumps back
2^k commits. Initial footer count0 has an empty directory reference. Nonzero
commits bind directory metadata to the footer and exact model/nonce identity.

Append emits all blocks and directory before the final footer, then flushes.
The existing durability barrier separately acknowledges stream/file/publication
synchronization; a complete footer alone is not an OS sync acknowledgement.
Normal open follows the last footer and bounded ancestor lookups. Selected field/
entity/address queries read only relevant blocks. They do not certify unread
blocks: CLI --verify reads all frames. Explicit recovery scans complete record
CRCs and committed directories/blocks; only a truncated tail is recoverable.
Corrupt complete records reject. Writer preflight precedes any recovery truncation.

## Version4 policy and block envelope

Policy11 body: version:u32=1, default Options, field-rule count:u32, then sorted
field-name/Options pairs. A name uses native string encoding. Options is codec:u8
(0=None,1=Zstd), compression level:i32, minimum raw bytes:u64, only-if-smaller:u8.
There are at most64 nonempty unique names, each at most1024 bytes. Names apply to
every region and basis group of that field. Append adopts the stored policy if
none is supplied; an explicit differing policy rejects before tail repair.

Each block2 body wraps the *unchanged version3 logical block bytes*:

| Offset | Field |
|---:|---|
| 0 | envelope version:u8=1 |
| 1 | actual codec:u8 (0 or1) |
| 2 | reserved:u16=0 |
| 4 | decoded byte count:u64 |
| 12 | stored byte count:u64 |
| 20 | CRC32 of decoded bytes:u32 |
| 24 | stored payload, exactly stored byte count |

Outer record CRC/reference protect the envelope and stored payload. The decoded
CRC protects the exact logical block. Zstd payload is exactly one ordinary frame
with known content size and checksum; skippable, concatenated, trailing, unknown-
size or checksum-free frames reject. Raw fallback requires stored=decoded size.
Decompression has independent raw/stored/workspace limits before allocation.
Recovery also checks the stored kind-2 record bound before body allocation or
incomplete-tail classification; an oversized truncated declaration is corruption,
not permission to truncate the file during repair.

Raw blocks are capped at4MiB; stored capacity5MiB; zstd's custom allocator caps
workspace at32MiB, including allocation headers. Typed writer batches shrink
below256 rows as required; a single oversize row rejects. Metadata, directory
and footer stay uncompressed. Optional zstd support uses a real1.5.5+ library and
bounded-allocation APIs; absent-codec builds reject declared zstd policies/blocks
explicitly. None-only version4 files remain usable without zstd.

Async admission adds42MiB+1024 bytes of conservative codec scratch per admitted
version4 frame to the existing owning-frame/validation charge. This includes
workspace and stored/envelope staging; it is not a process RSS cap. Small queue
budgets reject before transferring ownership. Decoder workspace may be lowered
to the remaining query budget; exhaustion rejects rather than exceeding it.

## Conversion and interoperability

`sdb_run --read file --verify` verifies every logical frame. `--convert-result new`
preserves typed schema, values, addresses, bases and missingness, writing version3
for --compression none or version4 for an enabled compression policy. Existing
destinations reject. Legacy frames receive the existing typed model binding;
their f64 value representation is preserved. Conversion does not copy a restart
link or claim authorization to continue a different physical result file.

Golden compatibility tests and the independent DBF5 Python oracle are executable
specification checks. The oracle compares decompressed version4 block bytes to
version3 blocks, including all seven scalar lanes and full address/validity data.
