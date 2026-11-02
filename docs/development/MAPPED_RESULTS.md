# Immutable indexed result access

Use ResultReader(path, false, byte_budget, ResultReadMode::MappedImmutable) on a
closed, immutable indexed2/3/4 file. The stream backend remains the default.
The file is mapped once read-only. Random-access footers, directories and
selected entity blocks are bounds-checked and CRC-verified from mapped pages.
Queries return owned decoded values. The mapping occupies file-sized virtual
address space and demands OS pages as needed. Initial model metadata and an
explicit recovery scan retain the existing stream path.

Use time_interval(begin,end) to select half-open frame indices covering a closed
physical-time range. Stream each selected field or frame to bound retained
results independently of the interval length. On POSIX, ensure no process
truncates or modifies the backing file while the reader exists; truncation
of a live mapping can raise SIGBUS. A copied immutable snapshot is appropriate
when a writer or repair process owns the original.

mapped_result_contract checks range and decoded frame parity with stream reads.
Local verification: Windows x64 GCC 15, generated C3D4 indexed trajectory.

The CLI selects immutable indexed reads with --read FILE --mmap. For indexed4,
--compression-workers N sets a bounded pool (1..64) on writes and conversions.
On Windows GCC 15.2 a three-worker zstd run with --buffer-mib 512 generated
21 frames; --mmap --verify decoded and validated every frame.
