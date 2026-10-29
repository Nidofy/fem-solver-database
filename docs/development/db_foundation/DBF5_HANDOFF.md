# DBF5 phase three and remaining standalone delivery

User request: complete phase three and remaining work from the session notebook.
Baseline: DBF4 `a9a203c67d78ed8649e12e05c15214ae2b95ea63`.
Runtime source: `7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
Final review, verification and packet identities are in DBF5_VERIFICATION and
`verification/reviews/DBF5/manifest.json`.

Status: **COMPLETE within the standalone scope**. Independent Astra/high found
one P2 and verified its repair; no remaining actionable scoped findings. Final
GCC serial/OpenMP and MSVC ASan each pass 45/45. Linux focused ON/OFF suites,
sanitizers, installed package consumers and cross-platform reads pass; compressed
process campaigns pass 71 Windows and 87 Linux child checks. No runtime/test
source changed after the reviewed repair; later changes are docs and evidence.

## Delivered

- Optional real zstd with capped custom allocation, raw fallback, exact seven-lane
  typed blocks and persisted per-field policies in new CompressedIndexed4.
- Existing index/CRC/lease/durability/continuation paths extended to v4, with
  read, recovery and async admission limits. Formats 1–3 remain readable.
- CLI compression, complete logical verification and lossless conversion into a
  new file; formal result grammar for formats 1–4.
- NodeStateView, ElementBatchView, MaterialView, ShapeTableView, ResultSink and
  RestartLink v0; existing TET4/HEX8 reference force consumers with owned scratch,
  exact trajectory/publication/restart checks and measured batch overhead.
- SDK-independent migration snapshots with validated FNL/FEG/IDARRAY maps,
  explicit equation handling, private scratch, deterministic reduction/backwrite,
  golden model and a native integration guide.

The D1–D7 storage scope was closed in DBF3; DBF4 supplied durability. DBF5 closes
the remaining standalone compression/consumer/toolkit work. It does not make
stored shell/beam/unknown material definitions numerically executable.

## Entry points and changed files

Runtime: `io/compression.hpp`, `results/compressed_format.hpp`, existing result,
indexed, async and typed headers, `consumer_views.hpp`, `field.hpp`, `hex8.hpp`,
`migration/preprocessed.hpp`. CLI: `examples/sdb_run.cpp`. Build/package:
standalone CMake, `FindSDBZstd.cmake` and package config. Tests/benchmarks:
`dbf5_*`, migration golden, independent probes and evidence. Documentation:
DBF5 contracts/guides, RESULT_FORMAT_SPEC, capability/resume/index and parent
maps/module/migration/task notes plus notebook. No production kernel was edited.

## Migration sequence

1. Rebuild consumers against the new headers. Enable zstd only with a real SDK
   exposing the checked advanced allocator API; ensure its runtime is discoverable.
2. Use format 4 explicitly for new compressed files. Old readers cannot interpret
   it. Keep old files or convert to a new destination; never copy a continuation
   link across conversion. Preserve FileSync/publication failure handling.
3. In the current buildable production checkout, implement the extractor into
   the documented copied Snapshot, validate mirrors and equation numbering, then
   compare each supported family against its existing kernel. Keep actual Felem
   quirks confined to the adapter. Existing eecontact already copies its DOF list.
4. Validate actual restart fields, trial state, parallel reduction and output;
   repeat realistic batch benchmarks with agreed thresholds before switching a
   production call site. Tiny batches have measurable view acquisition overhead.

## Boundaries and assumptions

The standalone reference subset is existing CPU TET4/HEX8 with the documented
ownership and exclusive mutation rules. Borrow validation is not a concurrent
owner-destruction mechanism. ModelBuilder import is storage-only; an equation-zero
entry is not an inferred SPC. Deterministic reducer callbacks remain external:
callback failure can leave already-written destinations partially updated.

Native Felem ABI, external SDK solver, CUDA and device power-loss behavior are
**not locally compiled / not locally verified**. Process crashes and successful
OS sync calls are not physical power-loss certification. Full INP, Index64,
units algebra, frequency/complex arithmetic and new numerical kernels remain
separately scoped capabilities. The user kit and unrelated parent changes remain
preserved. No push, merge or release was performed.
