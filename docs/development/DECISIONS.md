# Architecture decision records (ADR)

Format: one entry per decision. Status: PROPOSED / ACCEPTED (batch review may
overturn) / SUPERSEDED. High-risk schema/physics/time-layer changes must be
PROPOSED with isolated prototype before becoming default paths.

## ADR-0001 — Publication-driven force evaluation becomes lazy (R1.01)

- Status: ACCEPTED in B01 after independent R5 closure and user authorization (2026-10-28)
- Decision: `ResultPublisher::publish()` computes the set of due output
  requests first; it evaluates constitutive state (`CpuExplicit::forces`) only
  when (a) at least one request is due AND (b) at least one due variable needs
  current force/stress/strain data (everything except pure `U`).
- Rationale: `sdb_run` publishes after every increment; unconditional
  re-evaluation costs a full element+assembly pass on every non-output step.
  The stored frame was already skipped when empty; the waste is compute only.
- Invariants kept: published U/V/A/RF/S/E/ALL* values at due frames are
  bit-identical to before (same single re-evaluation at the same state);
  trajectory fields (u, v, a, step, time) untouched by publication; stored
  `.sdbres` bytes unchanged.
- Rollback: single-commit revert; no schema, no default-path change.

## ADR-0002 — Time-layer contract for output publication (R1.02)

- Status: ACCEPTED in B01 after independent R5 closure and user authorization (2026-10-28)
- Decision: codify (tests, not new behavior) that publication at increment n
  of step s reports a coherent physical time layer: U at t[n]; S/E at t[n] via
  current-displacement re-evaluation; V reconstructed to full time
  `v(t) = v(t-Δt/2) + Δt/2·a(t)`; A at t[n]; and that publication never
  mutates trajectory fields nor commits history, across step boundaries, dt
  changes (velocity rephasing in `CpuRun::next_step`) and restart.
- Rationale: R1.01 makes the re-evaluation conditional; the contract tests
  prevent a future regression where skipping or double-evaluating forces
  desynchronizes published fields from the published displacement, or where
  publication perturbs the integration trajectory.
- Rollback: tests only; no runtime change.

## ADR-0003 — Force-evaluation counter instrumentation (supports R1.01)

- Status: ACCEPTED in B01 after independent R5 closure and user authorization (2026-10-28)
- Decision: `CpuExplicit::forces()` counts completed evaluations
  (`force_evaluations()`); `CpuRun` forwards it. Test-only observable, zero
  hot-path cost (one increment per call), no ABI break (header-only template).
