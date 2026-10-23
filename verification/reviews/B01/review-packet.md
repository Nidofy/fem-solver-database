# B01 Review Packet — design, call chains and change rationale

Reviewers: read this, then `manifest.yaml`, `build-test.md`,
`numerical-validation.md`, `compat-matrix.md`, `open-risks.md`, and the diff
`aaa74af..719c3fb`. Code content HEAD is `719c3fb`; the freeze commit adds
only this packet and ledger updates.

## 1. What B01 delivers

1. **R1.01 (core runtime change)**: publication-driven force evaluation is now
   lazy. Before: `ResultPublisher::publish()` ran a full element kernel +
   assembly (`CpuExplicit::forces`) on **every** snapshot, before any output
   due-check, and `sdb_run` snapshots every increment — so every non-output
   increment paid a discarded force pass. After: due requests are selected
   first; the force evaluation runs only when a due frame actually contains a
   variable that needs constitutive data (everything except pure `U`).
2. **R1.02 (contract tests)**: the publication time-layer contract is locked
   by tests — read-only w.r.t. trajectory, repeatable, one coherent physical
   layer per frame, consistent across a dt-change/load-release step boundary,
   and byte-identical publication after restart (via the existing
   restart-append byte-equality tests).
3. **R0.x (baseline)**: frozen SHA + source/toolchain inventory (R0.01),
   three-configuration build/test matrix (R0.02), development ledger (R0.03),
   segmented cold/steady/IO benchmark baseline with eager-vs-lazy A/B (R0.04),
   honest CUDA BLOCKED_ENV (R0.05).

## 2. Exact code changes and call chains

### `include/sdb/results/results.hpp` — `ResultPublisher::publish()`

- New prologue: iterate `requests`, collect `due` (pointers to requests whose
  `OutputScheduler::due(r, info.increment, increments)` holds) and set
  `needs_forces` if any due variable `!= "U"`.
- `cpu.forces(state)` now executes only `if (needs_forces)`.
- Field emission loop iterates the pre-selected `due` list (semantics of
  dedup/regions/components/budget unchanged; pure restructure of the loop
  nest).
- Readers of force-derived data inside emission: `V` (velocity→acceleration→
  `internal_force`), `A` (same), `RF` (`internal_force` on fixed DOFs), `S`/
  `E` (`stress`/`strain`), `ALLSE` (stress+strain), `ALLKE` (via `V`). `U`
  reads displacement only — safe to publish without evaluation.

### `include/sdb/dynamics.hpp` — `CpuExplicit`

- Additive member `force_evaluations_` incremented at the end of each
  completed `forces()`; accessor `force_evaluations()`. No behavior change.

### `include/sdb/lifecycle.hpp` — `CpuRun`

- Forwarding accessor `force_evaluations()`.
- `save()` now calls `cpu_.forces(state_)` before serializing. Rationale: the
  eager path always left `internal_force` consistent with the current
  displacement at any `sdb_run` stop point (because publish had just
  evaluated); the lazy path would otherwise serialize a one-evaluation-stale
  force field at non-due stop points, changing restart **bytes** (not
  physics: the first post-restore `step()` re-evaluates forces before use).
  Verified byte-identical checkpoints at due (700) and non-due (733) stops.

### Call-chain impact analysis

- `sdb_run.cpp` `publish` lambda → `CpuRun::snapshot` → `publish()`: now O(1)
  on non-due increments (was O(elements)). `ResultWriter::write` already
  skipped empty frames — storage behavior unchanged.
- `CpuRun::next_step()`/`advance()`/`restore()`: untouched.
- `ResultReader`/`NativeResultStorage`/`RestartDB`/exporters: untouched.
- General contact module: untouched (does not use `ResultPublisher`).

## 3. Numbers to check (why we believe this is correct *and* profitable)

- Unit contract (in `lifecycle_contracts`): forced-tet fixture (2000
  increments, frequency 100): force evaluations 4002 → **2022** with per-
  increment publication; U-only output: **zero** publication evaluations
  (2001 total from initialize+2000 steps); empty-output: 2 → 1.
- Wall clock, same conditions A/B at 1M nodes (see `benchmarks/`):
  non-due publication 7.46 ms → 0.015 ms; due pure-U snapshot 15.75 → 8.68 ms;
  steady step unchanged (15.32 vs 15.29 ms); compile/IO unchanged.
- Byte-level: `.sdbres` (double/float/U-only), split-flow result and
  `.sdbrst` checkpoints (due and non-due stop points) identical pre/post.

## 4. Known limitations / deliberate scope

- The `ResultFrame` still carries `FrameInfo` for non-due increments (empty
  frame) — kept because `ResultWriter::write` contract and `sdb_run` flow
  depend on snapshot() returning frame info; cost is trivial.
- `snapshot()` on a due increment still evaluates forces when non-U fields are
  requested — required for a coherent layer; this is the intended cost.
- No change to reader full-scan (R4.x), no queue/backpressure (R1.03) — out
  of batch scope.
- `u-only.inp` benchmark variant and `build-b01-eager` A/B binary are scratch
  artifacts under gitignored build dirs; only JSONL evidence is committed.

## 5. Suggested reviewer attention points

- Astra: (a) due-selection ordering vs `emitted` dedup semantics; (b) any
  path where a due non-U frame could observe stale `internal_force` after
  R1.01 (we claim none: `needs_forces` covers every force-reading variable);
  (c) counter increments and `save()` evaluation placement vs restart byte
  contract; (d) benchmark methodology fairness (medians, same binaries).
- Opus: (a) published time layer of V (reconstruction `v+dt/2·a`) vs stored
  half-step velocity across `next_step` rephasing; (b) RF sign convention on
  constrained DOFs (unchanged, but now under laziness); (c) the analytic
  references in the new tests (forced oscillator ω=20; released oscillator
  ω=20 with a=−400u); (d) whether skipping evaluation for pure-U frames can
  mask a physics error (we claim U is displacement-only by construction).
