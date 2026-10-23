# B01 open risks and non-finished items

## Honest status of blocked work

- **G0.01 / all CUDA paths**: `BLOCKED_ENV` — no NVIDIA device or toolkit on
  this host (R0.05). Nothing CUDA is verified; no CUDA claim made.
- **R1.03 (output queue/backpressure), R1.04 (failure injection), R4.x
  (reader indexing)**: not started (next batches). The reader still
  full-scans all frames on open (measured: read ≈ write time at 1M nodes).

## Known residual risks in what was delivered

1. **Semantics of state after a non-due snapshot** changed subtly (force
   fields no longer refreshed). In-repo callers are consistent; a future
   external caller reading `state.internal_force` after a non-due snapshot
   gets the step-time evaluation. Mitigation: documented in compat-matrix;
   R1.02 tests pin the trajectory fields as unaffected.
2. **`save()` extra evaluation**: one additional force pass per save call
   (user-triggered, rare). If a future batch adds expensive nonlinear
   constitutive evaluation, `save()` should be revisited (only needed to
   preserve restart bytes).
3. **Counter-based tests** couple to exact evaluation counts (2022/2001/1);
   intentional — they detect both regressions to eager behavior and
   accidental extra evaluations — but they will need updating if `initialize`
   or `advance` legitimately change evaluation counts.
4. **Benchmark noise**: single-user desktop; 200k steady/step spread 6–16%
   across 3 runs. Medians reported; 1M rows are single samples. The eager/lazy
   deltas (−1.3/−7.1 ms) far exceed noise; the "steady unchanged" claim is
   within noise, stated as such.
5. ~~OMP efficiency (~1.0x at 4 threads on this model) is a known open
   problem deferred to R2.01/R2.02 — not claimed fixed; recorded in R0.04.~~
   **RETRACTED (B01-A-001 / B01-O-002):** the lifecycle benchmark cannot
   execute OpenMP — `CpuRun` hard-codes `CpuPolicy::Serial` and offers no
   policy parameter, so the earlier "OMP4 ≈ serial" rows were two serial runs
   under different `OMP_NUM_THREADS` values (reviewer link-probe:
   `lifecycle_parallel_launches=0` vs explicit-control 1). **No parallel
   efficiency or bandwidth data exists for the lifecycle path.** Establishing
   one requires a deliberate public API change (R2 candidate, out of B01
   repair scope). The low-level `sdb_benchmark` does select OpenMP explicitly;
   historical `verification/benchmark_1m_omp4_*.jsonl` (previous host) used
   that real path.

## Deviations from the planning documents

- `AGENTS.md` does not exist in this repository (recorded missing in R0.01).
- Planning docs suggest `verification/reviews/<id>/task-evidence/` packets;
  implemented exactly so, with a `.gitignore` exception because historical
  `/verification/*` content was excluded for containing parent-repo paths.
  New packet content contains no parent-repo paths.
- B01 scope executed fully (R0.01–R0.05 + R1.01 + R1.02). R1.02 was included
  (not deferred) because it is test-only and directly pins R1.01.

## Suggested next-batch READY tasks (dependencies satisfied)

- R1.03 (needs R1.02 ✓), R1.04 (needs R1.02 ✓), R2.01/R2.04 (need R0.04 ✓).
  Recommended: B02 = R1.03 + R1.04 + R2.04 per masterplan.
