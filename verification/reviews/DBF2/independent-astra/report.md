# DBF2 independent review

Reviewer: `/root/dbf2_independent_astra_review`; actual configured model `gpt-6-astra`, reasoning `high` (parent task/launcher evidence). Date: 2026-10-29.

Implementation BASE: `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`.
Initial implementation REVIEW_HEAD: `826e65352772cd10508297466e59cd6e11816388`.
Portable diagnostic followup: `cfeeab7fbd8235b35ed459de0461794a69b001f2`.
Inherited merge base: `480da32a17e3b60e6694b95e5c9ec83618e5b64b`.

Final verdict: **PASS for the bounded DBF2 implementation slice at `1f39b7313a10002099ddc23c7ebb4b89a6633bdb`**. All six independently reproduced findings below are closed by source review and fresh independent probes. The initial cfeeab7 verdict was **FAIL**; its reproduction sources and logs are retained. This is not a fresh exhaustive audit of inherited ALR1, a production/CUDA certification, or authorization to publish/merge. Full multi-configuration suite completion remains the parent acceptance gate.

## Scope and method

Read the repository static-development skill, parent repository map/module notes/solution map, the user completion kit, DBF2 contracts/capability matrix/verification document, and actual changed code. The reviewed boundary is the standalone `solver_database_v2` C++20 module. No parent production solver build, production edits, GPU build, or source commits were made by this reviewer. Owned files are confined to this review directory. Used static diff/source tracing, independent standalone adversarial C++ executables, and a separate Python fixed-width wire oracle. The oracle does not import or invoke the sdb reader/writer.

## Findings at initial content

| ID | Severity | Initial source anchor | Reproduced behavior | Required resolution |
|---|---|---|---|---|
| DBF2-R1 | P1 | results/results.hpp:433,454,714-731; results/indexed_format.hpp:159 | Same schema id/version/name accepts two descriptors differing in units. Across two valid frames, component order XX,YY -> YY,XX makes `typed_history(component=0)` return 10,200, silently joining different physical components. | Enforce one descriptor for a same-key field in a frame; history selection must verify full schema/basis/region identity across samples or explicitly transform/select semantic components. |
| DBF2-R2 | P1 | results/async_writer.hpp:40,168-177 | A 100,000-row typed frame rejected by a 1,024-byte queue budget allocates 13,600,098 bytes in admission calculation, because `required_bytes` calls allocating address-set validation before admission/backpressure. | Admission accounting must be allocation-free with respect to row count; charge before allocating validation scratch. |
| DBF2-R3 | P1 | implicit.hpp:110-111,125-127 | A 72-byte MatrixData with B=1024, one diagonal column, and no values decoded under budget=1024 allocates 8,388,608 aligned bytes before throwing value-shape mismatch. | Check expected value shape and reconstruction budget before SparseMatrix constructor allocation. |
| DBF2-R4 | P2 | model.hpp:197 | Existing ModelBuilder accepts a node label UINT64_MAX, but new typed external-label accessor returns the reserved invalid StrongId sentinel for that existing node. | Explicit checked conversion/rejection at typed boundary, preserving documented legacy compatibility. |
| DBF2-R5 | P2, inherited validation gap relevant to new format | results/results.hpp ResultReader source-map validation; typed_format.hpp:validate_model | TypedIndexed3 writer and reader accept node source entry index/id 999 at position zero and duplicate `(part,instance,label)` identities. | Validate dense index/runtime IDs and unique full source identities; test corrupted metadata on read, including public writer input. |
| DBF2-R6 | P2 | results/results.hpp:83-98 | Hex8 with 8 nominal IPs and legal explicit sample layout containing only IP1 has one StateDB sample, but ResultModel::from clears its explicit layout as canonical. Result address IP8 then validates despite absence from state. | Preserve explicit subset layouts, or only elide when count and all addresses/materials exactly match the canonical full layout. |

R1-R4 were reproduced at cfeeab7. R5/R6 probes used the worktree during parent fix development; their relevant source remained unchanged from cfeeab7 at reproduction. The parent was notified immediately and owns all fixes.

## Independent successful checks

- `wire_oracle.py` independently decoded actual float/double synthetic TypedIndexed3 and rotation restart files. It checks little-endian record headers, CRC32 for every record, scoped identities for repeated external labels, transformed coordinates, all 2+3 layer sample/material tuples, local basis matrix, ordered tensor components, exact UINT64_MAX and missing validity, a full-address plastic-history value, contact generation changes, global address/value, all 20 restart fields, rotations/inertia/torque, activation and time. Both precisions PASS, process exit 0. The initial oracle incorrectly counted 18 ResultAddress scalar members; reviewing its explicit format established 19 and corrected that oracle assertion. No library code was changed for this oracle.
- `restart_probe.cpp` compares a byte image of every destination state field plus clock/increment before and after four failed loads: CRC-valid NaN in final torque block, missing end record, trailing unknown record, and unknown required state schema. All four reject and preserve the complete destination image; exit 0. It also contains R5 reproduction.
- Static tracing confirmed schema 1/2 do not serialize a foundation tail, native codecs retain fixed-width representations, rotation capability mismatch rejects, sample histories use each sample's material width, and all nonempty foundation definitions gate CPU/Tet/Hex execution. CUDA transitively uses TetPlan but remains not locally compiled or device verified.

## Commands and exits

PowerShell, nested repository working directory. GCC runtime bin prepended to PATH:
`D:\Software\MinGw-w64\x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0\mingw64\bin`.

All four standalone probes compiled with:

```text
g++ -O1 -std=c++20 -Wa,-mbig-obj -Iinclude verification/reviews/DBF2/independent-astra/<probe>.cpp -o verification/reviews/DBF2/independent-astra/<probe>.exe
```

Probe names: adversarial, boundary_probe, restart_probe, sample_probe. Each compilation exited 0. The first attempted PowerShell command had an unquoted comma in `-Wa,-mbig-obj` and failed to parse before any source file was written; corrected by quoting that argument.

```text
adversarial.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/DBF2/independent-astra
boundary_probe.exe
restart_probe.exe build-b01/integration-scratch verification/reviews/DBF2/independent-astra
sample_probe.exe
python verification/reviews/DBF2/independent-astra/wire_oracle.py build-b01/integration-scratch
```

All final commands above exited 0. These reproduction probes print observed failures rather than use nonzero exit as their defect signal; inspect the retained `.log` output. The initial wire-oracle assertion run exited 1, then the corrected independent decoder exited 0.

## Remaining verification boundary

Parent-reported full GCC serial/OpenMP and MSVC ASan suites are contextual evidence, not claimed as independently rerun by this reviewer. The review does not prove power-loss durability, authentication, arbitrary allocator failure atomicity, all inherited ALR1 behavior, production SDK compatibility, or CUDA behavior. Synthetic storage values are not physics results. No new physical kernel was developed or certified.

Migration: rebuild standalone/downstream consumers after header changes; retain legacy format tests. The fixed-SHA closure below satisfies the independent bounded review gate; full-suite acceptance remains separately reported by the parent.

## Fixed-SHA closure

Fix REVIEW_HEAD: **`1f39b7313a10002099ddc23c7ebb4b89a6633bdb`**.
Read the complete 263-line fix diff, including regression test changes and source validation call sites. Confirmed this HEAD before compiling the fixed probes. No production source edit or commit was made by the reviewer.

| Finding | Independent final evidence | Status |
|---|---|---|
| R1 | `fixed_adversarial.log`: contradictory same-key descriptors accepted=0; component-order drift rejected=1. Source checks also include units, complete schema, schema id/version, basis id, stored region and history flag. Parent added six separate drift cases. | CLOSED |
| R2 | `fixed_adversarial.log`: the same 100,000-row request rejected under 1,024-byte budget allocates only **34 bytes**, attributable to the exception diagnostic; no O(row-count) validation tree. Worker validation occurs after charged ownership admission. | CLOSED |
| R3 | `fixed_boundary_probe.log`: 72-byte malformed matrix under budget=1024 rejects with **0 aligned bytes** allocated. Expected scalar count and reconstruction bytes checked before construction. | CLOSED |
| R4 | `fixed_boundary_probe.log`: legacy raw UINT64_MAX node label remains representable; typed conversion explicitly rejects the reserved invalid sentinel. Reverse typed lookup also rejects invalid label. | CLOSED |
| R5 | `fixed_restart_probe.log`: public writer rejects inconsistent source-map metadata. A separate bypass-written, CRC-valid typed file is also rejected by ResultReader, independently exercising read validation. Static review confirms validation applies to all three formats. | CLOSED |
| R6 | `fixed_sample_probe.log`: state samples=1, persisted explicit layouts=1, nonexistent IP8 accepted=0. All explicit layouts are preserved. | CLOSED |

Additional fixed-SHA results:

- Four complete-state restore atomicity probes still PASS, including the late NaN in the final rotation field.
- Python independent wire oracle still PASS for float/double result and restart fixtures, 20 result blocks and 20 restart fields per precision.
- `legacy_probe.cpp` freshly compiled against the fixed SHA: schema-1 and schema-2 frozen models load and resave to exactly the original bytes; frozen Native1 and Indexed2 files yield one initial frame and fifteen zero displacement scalars for five nodes. All four checks PASS.

The following fixed probes were each compiled afresh with the GCC O1 command above and exited 0 on both compilation and execution:

```text
fixed_adversarial.exe tests/fixtures/abaqus/forced_tet.inp verification/reviews/DBF2/independent-astra
fixed_boundary_probe.exe
fixed_restart_probe.exe build-b01/integration-scratch verification/reviews/DBF2/independent-astra
fixed_sample_probe.exe
legacy_probe.exe tests/fixtures verification/reviews/DBF2/independent-astra
python verification/reviews/DBF2/independent-astra/wire_oracle.py build-b01/integration-scratch
```

The fixed C++ probes assert their expected closure behavior and return nonzero on a failed assertion. The Python oracle ran separately from the C++ readers. Synthetic fixture files supplied by the parent are treated as test inputs; their expected values are checked independently against documented sample geometry and handwritten values, not read back through a shared library oracle.

## Handoff summary

Files changed by reviewer: only C++ probe sources, Python oracle, text logs, this report, and evidence manifest under `verification/reviews/DBF2/independent-astra/`; generated executables/native test data are disposable review scratch and must not be committed. No implementation source changes or commits by reviewer. Parent implementation fixes are in the exact fix SHA above.

Implemented by reviewer: reproducible adversarial checks and independent wire/legacy/restore oracles. Not implemented by reviewer: production changes, new physics, a second database/format, or source fixes. Verification unavailable: production SDK build, CUDA/device execution, power-failure durability, and exhaustive inherited ALR1 review. Assumptions: supported SourceMap runtime IDs are one-based dense IDs; explicit sample layouts may be shorter than nominal quadrature rules; field-history queries must preserve one semantic component/basis contract. These assumptions now match the documented and tested contracts.

No unresolved P0/P1/P2 finding established within this review's bounded scope. This statement does not convert deferred/blocked capabilities in CAPABILITY_MATRIX into verified implementation.
