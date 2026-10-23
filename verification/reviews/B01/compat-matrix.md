# B01 compatibility matrix

## File formats / schemas — NO changes

| Artifact | Schema/version fields | B01 change | Verification |
|---|---|---|---|
| `.sdbmodel` (ModelStore) | unchanged | none | reload fingerprint + semantics tests pass |
| `.sdbres` (ResultWriter/Reader) | magic, record kinds unchanged | none | byte-identical outputs pre/post (double, float, U-only) |
| `.sdbrst` (RestartDB) | RestartInfo unchanged | none | byte-identical checkpoints at stop 700 (due) and 733 (non-due) |
| v2 legacy checkpoints (`import_v2`) | unchanged | none | conversion-exactness tests pass |

## C++ API — additive only

| Interface | Change | Back-compat |
|---|---|---|
| `sdb::CpuExplicit<T>::force_evaluations()` | new const accessor | additive; header-only template |
| `sdb::lifecycle::CpuRun<T>::force_evaluations()` | new forwarder | additive |
| `sdb::lifecycle::CpuRun<T>::save()` | evaluates forces before serializing | output-byte compatible (verified); observable only via the new counter |
| `ResultPublisher::publish()` | due-first selection; conditional forces | behavior-identical for every stored byte; return type unchanged |
| all other public headers | untouched | — |

## Behavioral notes (non-breaking, documented)

1. A **non-due** `snapshot()` no longer touches `internal_force`/`stress`/
   `strain` (previously overwritten with identical values). Any external
   caller reading those state fields right after a non-due snapshot would see
   the step-time evaluation instead of a fresh one — no in-repository caller
   does, and the values differ only by which displacement they were computed
   at (documented README semantics for stored stress already say post-step
   forces belong to the preceding evaluation).
2. `save()` now always stores a displacement-consistent force field. Callers
   who saved **without** any preceding publication previously got a stale
   force field; they now get a fresh one. Restart-load ignores stored forces
   for physics (first `step()` re-evaluates), so physics is unaffected either
   way; only bytes change for that un-published-save pattern.
3. Benchmark JSON adds keys; old keys keep meaning except the generated model
   now runs 21 increments (was 2) with output frequency 10 (was 1) —
   historical `verification/*.jsonl` from other hosts are not comparable
   anyway; new baseline established under `benchmarks/`.

## Toolchain matrix (tested)

| Config | Result |
|---|---|
| MinGW g++ 15.2.0, Release, OpenMP | 8/8 PASS |
| MinGW g++ 15.2.0, Release, serial | 8/8 PASS |
| MSVC 14.44, RelWithDebInfo, ASan+UBSan, serial | 8/8 PASS, no findings |
| CUDA (any) | BLOCKED_ENV — untested, unclaimed |
