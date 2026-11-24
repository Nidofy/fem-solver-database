# DBF12 SDK contracts

## Paged history fields

Include `<sdb/paged_slot_field.hpp>`. Instantiate `PagedSlotField<T, Components, Layout, FixedPageCount>` with a nonzero 64-bit application schema ID and a nonzero 32-bit schema version. The final template argument is the number of slots per page. Values use the existing SoA or AoSoA component offset rules. Both the value allocation and the page metadata start on 64-byte boundaries.

`allocate(key)` creates a zero-initialized, invalid row and returns its local slot and generation. `set(handle, component, value)` marks the row valid. `set_valid` can preserve stored components while masking the row. Dictionary lookup accepts the full unsigned 64-bit key range. The open-addressed index and a LIFO free list give amortized constant-time allocation and erase in steady state. Rehashing compacts deleted buckets. Generation zero marks permanent exhaustion after a 32-bit generation would wrap.

Trial entry shares pages and the key index. A value change detaches the touched page. The first change to membership in a shared generation also detaches the flat key index. Rollback restores all page pointers, the high-water slot, free-list head and key index. Committed pools can be copied with COW isolation. Copying an active trial or copy-assigning into one raises a logic error. Moving transfers an active trial and leaves the source as an empty, reusable pool with the same schema. Handles acquired during an abandoned trial are scoped to that trial. Preserve domain keys across rank and transaction boundaries. Serialize calls to a pool on its owning rank.

```cpp
sdb::PagedSlotField<double, 3, sdb::AoSoA<8>> source(42, 1), receiver(42, 1);
auto h = source.allocate(1001);
source.set(h, 2, 7.25);
source.begin_trial();
receiver.begin_trial();
auto packet = source.export_migration_packets([](std::uint64_t k) { return k == 1001; }, true);
try {
    receiver.import_migration_packets(packet);
    receiver.commit();
    source.commit();
} catch (...) {
    receiver.rollback();
    source.rollback();
    throw;
}
```

Export writes selected keys in ascending order. Each packet row includes its key, schema ID, schema version, scalar type, component count, payload length, validity byte, little-endian component values and CRC32. The envelope also carries a count and CRC32. Import assigns fresh local handles. A malformed final row or a collision with any live key leaves the receiver at its pre-import state. The optional retirement flag requires an active trial. The transport and cross-rank commit decision belong to the caller.

Standalone `save(path)` and `restore(path)` preserve exact local slot numbers, generations, validity, logical components and free-list order. Packing logical components permits a reader to choose a different local page size or layout. `save_sidecar(path, parent)` and `restore_sidecar(path, parent)` bind the same records to the byte size and CRC64 of a published MultiBatchBundle file or partition manifest. Publish an immutable parent first, then distribute the parent and sidecar together. Each file has its own atomic publication boundary.

## Complex sample archives

Include `<sdb/results/streaming_spectral.hpp>`. The descriptor records the v2 axis kind, units, complex channel convention, application field schema ID, DOF count and chunk width. Time and frequency coordinates are nonnegative. Mode numbers are positive exactly representable integers. Buckling eigenvalues and load factors can be signed. Archive coordinates increase strictly. Magnitude/phase uses a nonnegative magnitude and a phase in radians. The application supplies its DOF ordering and associated field schema.

Each sample carries its coordinate, eigenvalue, natural frequency in Hz, generalized mass, damping ratio and ordered participation factors. Scalar metadata defaults to zero and factors default to an empty vector. Channel data is two Float64 values per DOF. ModelIR schemas through 11 admit only the v1 sample axis. The standalone spectral archive carries its v2 descriptor independently. The v1 axis descriptor keeps its original three wire fields. Legacy time result writers reject v2 descriptors. The existing in-memory SpectralSeries format retains its own reader.

```cpp
sdb::SampleAxisDescriptor axis{2, sdb::SampleAxisKind::FrequencyHz, "Hz", sdb::ComplexChannels::RealImag};
sdb::StreamingSpectralWriter writer(path, {axis, 2, 2, 99}, sdb::SpectralCodec::Rle);
sdb::SpectralSampleMetadata sample;
sample.coordinate = 12.5;
std::array<sdb::SpectralValue, 2> values{{{1.0, 0.25}, {2.0, -0.5}}};
writer.append(sample, values);
writer.finish();
sdb::StreamingSpectralReader reader(path);
auto slice = reader.slice(0);
reader.frequency_response(1, [](const auto &metadata, sdb::SpectralValue value) { /* consume one point */ });
```

The writer spools each sample and its fixed-width directory entry to disk. `finish()` publishes a single file through the SDK atomic writer. Allow temporary disk space for both the spool and final archive. An abandoned writer releases its private scratch files. Concurrent publication to the same path uses the SDK writer lease.

The reader verifies that directory coordinates are strictly increasing and the sample intervals cover the entire sample region contiguously. It scans directory records with constant memory at open, seeks to the requested 40-byte directory entry, and decodes at most one raw chunk at a time. Call `slice(index)` for ordinal lookup or `slice_at(coordinate)` for a binary search on the exact physical coordinate. A full slice also owns its returned DOF vector. For a single-DOF FRF, the reader seeks to the selected chunk through a per-sample offset table. Each fixed-size table entry binds its ordinal and offset with CRC64. The reader checks the full table against actual chunk boundaries during a full slice read. Old Wave 3 candidate frequency archives use an automatically removed on-disk offset index under the system temporary directory, built once when opening the archive. Current archives carry their index in the file and require no reader scratch. Callback delivery bounds memory independently of the number of frequencies. Damage in an unselected chunk is checked when that chunk is read. Directory entries, the footer and raw chunks have CRC64/ECMA-182 integrity checks. Stored integers and Float64 values are explicitly little-endian and independent of the SDK Index width.

The built-in byte-run codec emits deterministic count/value pairs and uses raw storage for blocks without savings. `SpectralCodec::Zstd` uses the existing synchronous SDK codec when configured with `SDB_ZSTD=ON`. Each decoded block has a 4 MiB absolute raw ceiling. Codec workspace limits are separate from the returned slice. Byte reproducibility of Zstd archives across installations depends on the encoder; pin the Zstd version when archiving reproducible builds.

## Downstream integration

Use the same SDK headers throughout a C++ consumer build. Pin JIG-2 to the SDB 2.6 release until the Wave 3 candidate passes its independent review. These standalone CPU data formats do not require MPI or CUDA. Windows Index32/64, Linux sanitizer, cross-system artifact hashes and measured allocation peaks are recorded on the DBF12 review branch.
