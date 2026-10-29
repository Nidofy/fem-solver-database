# DBF5 independent review corrections

Independent reviewer: Astra/high. Base `a9a203c67d78ed8649e12e05c15214ae2b95ea63`;
initial candidate `493a6ddb6aed3a6837be3e3cfa8382a8da4b14d6`;
fixed runtime source `7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
The complete report and original probes are in
`verification/reviews/DBF5/independent-astra/`.

## P2: recovery allocated a compressed body before applying its stored limit

Normal indexed reads bounded a v4 kind-2 body to 5 MiB plus its 24-byte envelope.
The explicit repair scanner instead used the generic 256 MiB record limit,
then allocated the complete body before deciding whether it was committed.
The independent probe appended a CRC-valid 8 MiB kind-2 record to a valid file:
the original scanner accepted a discardable tail and allocated 8,388,608 bytes.

The fix applies the compressed record cap immediately after reading its header,
before the incomplete-tail branch and before body allocation. Both complete and
truncated oversized declarations now reject. Writer recovery leaves their source
bytes unchanged. Six new regression assertions cover reader/writer refusal and
non-mutation for the two forms. The original allocation probe now rejects with
the stored-limit diagnostic; largest tracked allocation is 1,576 bytes.

Independent hostile-envelope probes additionally repair every enclosing checksum
and reference after mutating version, codec, reserved flags, raw size, stored span
or raw CRC. All six fail at the intended inner validation boundary. Consumer,
migration, codec, compressed result and CLI checks are separately reproduced.

## Evidence discipline

Initial Windows MSVC ASan 45/45 and no-codec results belong to candidate 493a6dd;
initial GCC runs were interrupted after the repair was identified. They are
retained in `pre-fix-493a6dd/`, not counted as final repaired-source acceptance.
Linux preliminary logs likewise retain their source identity. Final verification
uses clean Windows rebuilds and affected Linux recompilation. An unchanged Linux
codec/migration result may be reused only with recorded transitive input hashes.
No unchanged result is relabelled as a new compilation.

Final validation also corrected two harness issues without changing runtime code.
Interrupting the preliminary indexed test left a writer lease; after confirming
the test process was absent, its scratch directory was preserved under a new
name and a fresh scratch was used. Provenance inspection then found PowerShell5
had passed an unquoted OpenMP option as literal `$omp`: the first nominal serial
build was actually OpenMP. Its logs are retained under
`superseded-serial-openmp-option/`. The quoted option produces cache `OFF` and
no `-fopenmp` build flags; true serial receives a new clean build. Actual OpenMP
was already enabled; normalizing its cache to `ON` required no rebuilt objects.
