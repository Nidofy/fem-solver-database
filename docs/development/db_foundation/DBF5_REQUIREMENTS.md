# DBF5 — phase three and remaining standalone roadmap

Baseline: DBF4 `a9a203c67d78ed8649e12e05c15214ae2b95ea63`.
The user authorized phase three and the remaining work. This closes the remaining
standalone delivery from notebook sections6/12–14: compression, consumer
contracts/reference consumers, and the migration toolkit. D1–D7 storage semantics
remain DBF3's baseline; DBF4 durability guarantees remain mandatory.

| Requirement | Implementation / acceptance evidence |
|---|---|
| Real optional bounded zstd, no codec substitution | io/compression.hpp, external SDK feature check, enabled/disabled tests, codec budget/CRC/hostile-frame tests |
| Reuse indexed transaction/query infrastructure | CompressedIndexed4 wraps TypedIndexed3 logical blocks; same directory/footer/recovery/lease/barrier paths |
| Per-field policy and lossless seven scalar lanes | persisted default/field Options, raw fallback, exact integers/validity/signed-zero tests, independent Python wire oracle |
| Read/write/admission resource bounds | 4MiB raw,5MiB stored,32MiB allocator cap; row splitting; async extra codec charge before ownership transfer |
| Formal result specification / CLI | RESULT_FORMAT_SPEC v1–v4; --verify/--convert-result and compression options; occupied-output/policy/no-codec negative tests |
| Durability maintained for compressed files | process fault campaign, fresh-reader/no-codec rejection, incomplete-tail/refusal and linked continuation tests |
| NodeState/ElementBatch/Material/Shape/ResultSink/RestartLink v0 | consumer_views.hpp composes Field borrows and existing publication/link owners; checked batch boundary and const raw kernel views |
| Actual TET4/HEX8 reference consumers | shared existing force algebra; mechanical HEX extraction preserves operation order; exact trajectories and activation parity |
| Adversarial/lifetime tests and measured overhead | mutation confined to owned scratch; expired/resized/moved/dead-plan/alias tests; paired small/large batch benchmarks, no universal1.3% claim |
| Migration toolkit | preprocessed.hpp, actual FNL/FEG/IDARRAY source mapping, checked copied snapshot→ModelBuilder and equation/state mapping, private scratch/reducer/backwrite, golden fixture |
| Independent review and complete handoff | fixed source Astra/high review, repair/retest, clean Windows matrices, Linux checks, capability ledger/maps/notebook and hashed packet |

## Explicit production boundary

The notebook says this checkout is not the latest production solver, and requires
standalone delivery before adapting the current internal kernel. Therefore the
toolkit is a compilable SDK-independent seam and native integration guide, not an
unverified direct replacement of Felem or FNL/FEG binary ABI. No production source
or numerical algorithm is replaced. The current eecontact source copies equation
indices into private storage; the older notebook's mutation example is retained
as an adversarial scenario, not incorrectly reported as a current defect.

Physical power-loss/device certification, CUDA validation, full native SDK
integration, arbitrary new physical kernels, Index64 execution, frequency/complex
arithmetic, units algebra and complete INP parsing remain separately scoped future
capabilities/environment gates. They are not silently folded into a claim that
every production model is executable. Original user kit and unrelated changes
are preserved. No push/merge/release is authorized by this delivery.
