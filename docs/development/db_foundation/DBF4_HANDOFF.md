# DBF4 phase-two handoff

Status: **COMPLETE within the approved durability/fault-verification scope**.
Final library/CLI source: `910510d5340bfac779a2510eec8c78def73c5134`.
Final test followup: `476998506f723af4f1db27a64e4b6ff028dd5e75`.
Baseline: `5349ccfd5f1dff49b87fac88ffa43b61dc5f40ed`.
Branch: `codex/astra-db-foundation-completion`.

## Delivered

- Shared file durability levels, OS sync/capability checks and explicit fault hooks.
- Native/result-writer/async barriers acknowledging exact byte prefixes after the
  requested operation; failed I/O poisons the writer and async errors propagate.
- Additive `CheckpointLink::publish_durable`: result barrier → restart → link,
  identity/path checks, cooperative leases, typed stage and ambiguous-change errors.
- Atomic helper error outcomes distinguish pre-replace, attempted and confirmed
  replacement. Original bounded diagnostic text is owned without post-replace
  message allocation; the full exception remains accessible through cause().
- CLI defaults to FileSync, exposes `--durability stream|file|publication`, and
  rejects unsupported strict Windows parent synchronization before creation.
- Fresh-process fault campaign plus independent Python link/CRC/FNV oracle,
  legacy links, incomplete-tail recovery, stale leases, mismatches and refusals.

Existing Native1/Indexed2/TypedIndexed3, checkpoint and restart wire encodings
remain unchanged. Legacy path-based publication retains its caller-managed
lease/flush precondition. `CpuRun::model()` and the storage barrier virtual are
documented additive standalone APIs; consumers must recompile.

## Verification and review

GCC serial, GCC OpenMP and fresh MSVC ASan each passed **39/39**, exit0, after
clean builds at the final source. Windows atomic/result/link checks: **76/93/66**.
Windows crash campaign: **206 child checks, 47 abrupt exits**. Linux POSIX
atomic/link: **103/65**; crash campaign: **254 child checks, 59 abrupt exits**.
Existing async and new durability93 tests passed Linux GCC TSan. CLI10 passed
on Windows and Linux, including actual strict PublicationSync on the latter.

Independent Astra/high review: **PASS within stated scope; no unresolved P0–P2**.
It independently confirmed and retested the MSVC diagnostic-lifetime finding.
Owner regression testing also caught original diagnostic loss. Final diagnostics
use owned fixed storage; fresh GCC/MSVC allocation-denial and lifetime probes pass.

Initial ASan passes were invalidated after discovering a Chinese/English
showIncludes mismatch in the old cache. The new cache uses VSLANG=1033 and a
verified English dependency prefix. Raw failed/superseded evidence is retained;
only final clean runs count. See DBF4_REVIEW_FIXES and DBF4_VERIFICATION.

## Limits, assumptions and migration

One producer owns async writes/barriers/close; another thread may cancel. Close
and join async output before accessing native storage or publishing a checkpoint.
Model/run and writer mutations are serialized by the caller. Leases are cooperative;
stale locks are never stolen automatically. After ambiguous publication, reopen
and verify the pair before append, or branch explicitly. Three ordered files
do not constitute a multi-file atomic transaction.

Tests demonstrate process-crash recovery and successful OS synchronization calls,
not electrical power-loss survival. Linux tests ran under WSL2 on `/mnt/d` v9fs;
no native-ext4/device/remote-filesystem certification is claimed. Default-ASLR
TSan startup failure is recorded; final tests used per-process `setarch -R` without
host changes. This is bounded concurrency evidence, not all possible schedules.
Windows strict standalone directory sync is unsupported and explicitly rejects.

No compression, new physical kernels, production adapters, CUDA build/device
verification, full INP grammar or production solver changes were implemented.
Production remains not locally compiled/verified. ALR1/B02c are not blanket
accepted. Parent maps/migration/task notes and the session notebook were updated;
unrelated parent changes and the untracked original completion kit are preserved.
No push, merge or release was performed.

## Evidence packet and next boundary

`verification/reviews/DBF4/manifest.json` binds source, tests, relevant foundation
documents, reproduction scripts and raw review/test evidence to a content commit.
Run `python verification/reviews/DBF4/verify_packet.py --git` in the repository,
or omit --git to validate an exported packet. `create_packet.py --archive <path>`
exports the recorded canonical bytes and verifies the archive.

The next separate planned stage is compression. It was not started as part of
phase two. Read DBF4_CONTRACTS before extending result blocks or checkpoint
publication so compression cannot weaken prefix/acknowledgement guarantees.
