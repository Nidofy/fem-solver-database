# Resume DB foundation work

Read this directory, the kit, actual Git status/history and the DBF1 fixed-content
manifest before editing. DBF1 self-tests do not constitute independent acceptance.
ALR1 and B02c historical review boundaries remain intact.

First reproduce `cmake --build build-b01 --parallel 3` and
`ctest --test-dir build-b01 --output-on-failure --timeout 300` with the matching
compiler runtime on PATH. Portable toolchain parameters and ASan commands are in
the review packet. Check manifest hashes before reviewing fixed content.

Next dependency-ordered work:

1. Independently review DBF1 schemas/fingerprints, old-file compatibility, enum
   narrowing, borrow invalidation and restoration failure atomicity.
2. Define a versioned typed ResultField extension using FieldDescriptor and the
   existing native/indexed formats. Carry integer/validity, history/IP/layer/time
   addresses through independent Reader and snapshot ownership. Retain golden
   native-v1/v2 results; do not create a second result database.
3. Add orientation references and typed shell/beam/layer and parameter/history
   bindings to ModelIR2 through explicit migration, validation and storage-only
   fixtures. Unknown execution must keep failing closed.
4. Add equation/constraint/load contracts and controlled input fixtures only after
   the corresponding model schema exists. Verify external keyword semantics then.
5. Build the full D7 composite storage fixture; current mixed-IP history and
   generic storage tests are not substitutes for every entity/address type.

Do not add new numerical kernels, alter the parent build, merge/push, or reuse
unaccepted B02c changes implicitly. Preserve unrelated files and user kit input.
Record CUDA/production execution as NOT_RUN unless actually verified. Continue
small compiled/serialized/queried slices, and keep the capability matrix honest.
