# B01 numerical validation

Fixture: `tests/fixtures/abaqus/forced_tet.inp` (two TET4 sharing a face;
ρ=1, E=100, ν=0; only node 40 x free; F=1 on that DOF from t=0).
Exact solution: u(t) = 0.06·(1 − cos 20t), v(t) = 1.2·sin 20t,
a(t) = 24·cos 20t; ω=20 from m=ρ/24, k=E/6. dt=1e-4 (DIRECT), 2000 increments.

## Pre-existing checks that still pass unchanged (baseline behavior locked)

- forced response error |u−u_ref| < 3e-6 (float) / 1e-7 (double) — passes at
  HEAD (printed `response_error=3.72e-08 / 3.03e-08`).
- coherent S at final frame (stress = 100·u, tol 1e-11 double).
- full-time velocity V vs 1.2 sin 20t at 5 sampled frames (tol 2e-5).
- ALLSE = ½·(100/6)·u² (tol 1e-7).
- restart append byte-identical (frames 8–20 published by the restored run
  byte-match the continuous run) — float & double.
- v2-checkpoint conversion exact trajectory; multi-step dt rephase analytic
  reference (tol 1e-6); tabular amplitude load analytic response (tol 2e-7);
  boundary activation/release analytic (tol 1e-7).

## New with B01 (R1.01/R1.02 tests; all in `tests/lifecycle_tests.cpp`)

1. Publication laziness counters (exact integer expectations, no tolerance):
   the 2000-increment run → 2022 evaluations with every-increment publication
   (2000 step evaluations + 1 initialize + 21 due publications); U-only output
   → 2001; no-output → 1 (initialize only). Trajectory
   bit-identical (`==`) between full-request and U-only runs.
2. Read-only/repeatability: trajectory signature (u, v, a, ext, contact,
   history, step, time, dt) bit-equal before/after two snapshots at **every**
   visited increment (2001 cursors × 2 calls); `in_trial()` false throughout.
3. Time-layer coherence at all 21 due frames: U (1e-7), V (2e-5), A (1e-4),
   S=100·U (1e-11), frame time == state time.
4. Cross step-boundary (dt 1e-4→2e-4 at t=0.1, load released via
   `*Cload,op=NEW`): released-oscillator analytic
   u(τ)=u₁cos20τ+(v₁/20)sin20τ, v=−20u₁sin20τ+v₁cos20τ, a=−400u;
   tolerances 2e-6 / 3e-5 / 2e-3 at each step-1 output frame. This validates
   that the half-step velocity rephasing in `CpuRun::next_step()` plus the
   V-reconstruction `v+dt/2·a` yields a consistent published layer under the
   new dt after the transition.

## Energy/identity invariants (unchanged code paths, still asserted)

- ALLKE uses reconstructed full-time V (consistent with published V).
- ALLSE uses re-evaluated stress/strain at the published displacement (i.e.,
  the same evaluation the new laziness gates — when ALLSE is requested the
  evaluation always runs, so energies cannot read stale state).
- Mesh fingerprint / restart cursor / config hash checks all unchanged.

## Tolerance rationale

Analytic-reference tolerances match or exceed the pre-existing ones for the
same quantities; A (1e-4) accounts for error propagation 400·δu with δu ≤
3e-8 accumulated; released-A (2e-3) covers the released segment's 500
increments (t=0.1→0.2 at dt=2e-4) plus the step-0 accumulated error.
All comparisons are against closed-form solutions, not self-consistency.
