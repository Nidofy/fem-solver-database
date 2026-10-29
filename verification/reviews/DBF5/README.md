# DBF5 fixed-source evidence packet

Runtime and tests: `7ea0bf312619b019aa6dcd69f03731163e6ec6d4`.
Baseline: `a9a203c67d78ed8649e12e05c15214ae2b95ea63`.
Read `docs/development/db_foundation/DBF5_VERIFICATION.md` for final results,
assumptions, boundaries and interpretation. `independent-astra/REPORT.md` is the
independent review. A passing matrix is not production or power-loss certification.

Evidence groups:

- `gcc-*-source/configure/build/ctest.log`, `msvc-*`: final clean Windows builds.
- `windows-nocodec.txt`: fixed-source no-zstd compressed integration and CLI.
- `linux-final-*`, `linux-*-build/run.log`: Linux focused compile/run/exit records;
  reuse proofs explicitly identify unchanged binaries and their input closures.
- `compression-*`: real codec benchmark, separate-process fault campaign and
  independent LE/CRC/reference/zstd oracle. Original codec/benchmark evidence is
  a preliminary implementation measurement; final-source reruns are named in the
  verification document. Durability fault injection is process termination.
- `dbf5_consumer_*.txt`: exact parity summaries and paired benchmark measurements.
- `independent-astra/`: independent commands, original/red and repaired/green
  allocation probes, repaired-outer-checksum hostile envelopes, scope disposition.
- `pre-fix-493a6dd/`, `preliminary-493a6ddb6aed/`: superseded/interrupted runs;
  never substitute these for final acceptance.
- `superseded-serial-openmp-option/`: first nominal serial run actually enabled
  OpenMP because of a PowerShell argument quoting error; corrected final serial
  is separately rebuilt. Original stale-lock failure logs are also retained.

The scripts contain observed host toolchain locations; adjust them for another
machine. This archive does not vendor zstd headers/libraries or build executables.
Use an actual compatible SDK. No parent solver build is part of reproduction.

After extracting the archive at a module root:

```text
python verification/reviews/DBF5/verify_packet.py
```

Within the repository, also run with `--git` to verify the frozen Git content.
`manifest.json` binds canonical bytes and allows only recorded CRLF normalization.
It intentionally excludes itself from the content hash set. `create_packet.py`
exports the exact canonical Git bytes and refuses to overwrite an existing archive.
