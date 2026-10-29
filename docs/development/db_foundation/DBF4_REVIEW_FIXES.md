# DBF4 review and verification corrections

Baseline: `5349ccf`. Initial implementation: `e8b9bd6`.
Final reviewed library: `910510d5340bfac779a2510eec8c78def73c5134`.

## Original diagnostic hidden — owner regression finding

The first structured PublicationError wrapper exposed a generic what() message.
Existing lifecycle tests intentionally inject a failure inside atomic_stream and
assert its diagnostic. Both full GCC matrices failed that assertion (37/38).
The underlying cause was retained and old file preservation worked, but public
diagnostic compatibility regressed. Initial ASan results missed it because of a
separate stale build-cache problem described below.

The intermediate b677931 change tried to return a diagnostic from rethrowing the
stored exception. This attempt was superseded before final acceptance.

## P2: rethrown exception diagnostic lifetime — independent Astra finding

`std::rethrow_exception` may copy the exception. A custom exception with an inline
diagnostic buffer can therefore lose that buffer when the temporary rethrown
copy leaves its handler. Independent MSVC14.44 probing demonstrated an extra
destructor during what(), establishing a dangling returned pointer. Copying may
also allocate, contradicting the desired post-replacement behavior.

Final 910510d captures at most 1023 diagnostic characters into an owned fixed
buffer from the already active original handler, using bare throw. what() only
returns owned storage; cause() preserves the original exception. Independent
GCC/MSVC probes cover allocation denial after replacement, zero exception
copy/destruction during what(), copied PublicationError buffer independence,
long-string termination and non-std exceptions. The finding is resolved.

## MSVC dependency-cache mismatch — owner evidence correction

The old ASan CMake cache stored a Chinese showIncludes prefix, while the compiler
under vcvars emitted English. Ninja failed to discover header dependencies and
retained old executables. The first reported 38/38 and 39/39 ASan runs are not
final acceptance evidence. A fresh `build-dbf4-asan` cache under VSLANG=1033 has
the verified matching English prefix; final verification clean-builds all three
Windows configurations after the source freeze. No parent build files changed.

## Independent scope

See `verification/reviews/DBF4/independent-astra/REVIEW.md` and its retained
reproduction sources/logs. Review is bounded to the DBF4 change and relevant
inherited append/async/restart/link mechanisms. It is not blanket acceptance of
all inherited ALR1/B02c, every filesystem or physical power-loss survival.
