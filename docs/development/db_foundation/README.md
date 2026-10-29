# Database foundation implementation

## Current continuation: DBF5

The authorized standalone roadmap now includes DBF3 D1–D7 closure, DBF4
durability and DBF5 compression, consumer contracts/reference parity and migration
toolkit. Start with [DBF5_HANDOFF](DBF5_HANDOFF.md),
[DBF5_VERIFICATION](DBF5_VERIFICATION.md), [RESUME](RESUME.md) and the current
[capability matrix](CAPABILITY_MATRIX.md). Historical gap lists below retain
their original scope. Native production adaptation remains an environment gate.

## Historical continuation: DBF2

The DBF1 independent Astra/high review passed within its declared scope; its report
is retained under `verification/reviews/DBF1/independent-astra/report.md` and committed
in `25cd74c2bc713f865e9086bd58dc3982f4fcadf9`. Development continued immediately.

DBF2 adds the ModelIR3 foundation schema, typed identities and matrix DTOs,
coordinate/material/section/load/constraint definitions, heterogeneous layer
histories, TypedIndexed3 results, schema4 rotational restart, a synthetic composite
and independent-process compatibility tests. No new physics kernel was added.

Read [DBF2 contracts](DBF2_CONTRACTS.md), the current table in
[CAPABILITY_MATRIX](CAPABILITY_MATRIX.md), [DBF2 verification](DBF2_VERIFICATION.md)
and [DBF2 handoff](DBF2_HANDOFF.md). The sections below retain DBF1's original scope
and exclusions as historical evidence; they are not DBF2's current gap list.

## Historical DBF1 implementation

2026-10-29 (Asia/Shanghai). This is the first bounded implementation batch from
`docs/SDB_DB_FOUNDATION_COMPLETION_KIT`, not completion of all D1–D7 goals.
The current user request authorizes new development after the historical ALR1
freeze; it does not independently accept ALR1 or authorize publishing/merging.

## Baseline and ownership

- REVIEW_HEAD / implementation base: `9a3d754dbbc5c0b1720a93ecf29ee3278f705f73`.
- Proven inherited-change base: `480da32a17e3b60e6694b95e5c9ec83618e5b64b`,
  the actual merge-base with `codex/standalone-database`.
- Development branch: `codex/astra-db-foundation-completion`.
- The nested module was clean except for the user-provided kit. It has one active
  writer team in this session; the unrelated `sdb-b02c` worktree is not incorporated.
- Parent solver files and existing uncommitted parent work are preserved. Only
  dated navigation/verification notes are appended to the four parent maps/logs.
- Two child agents used `gpt-6.1-sol`, medium reasoning, for D0 and scoped
  implementation. This records the actual configuration, not the kit's suggested
  high effort. Shared schema/registry and integration/testing belong to the parent.
  Collaborative self-review is not independent acceptance.

## Delivered vertical slices

1. Cold, validated `FieldDescriptor` metadata in the existing `FieldRegistry`.
   History descriptors persist through the existing ModelIR/ModelStore path.
2. Named/versioned material `HistorySchema`, mixed-IP initialization and named
   state addressing; complete semantic identity gates both restart paths.
3. ModelIR schema 2 optional history-binding tail; schema 1 retains its exact
   encoding and legacy mesh fingerprint. No blanket file-version bump.
4. Ragged/sparse host fields reusing `Field` layouts, lifetime checks and native
   codec; integer values and explicit validity remain distinct from floating data.
5. Reject lifecycle rotation loss before restoration, and reject enum wire-width
   overflow before narrowing. Both defects have old-source red/new-source green
   probes, rather than only static hypotheses.

Explicit material histories are storage-only and are refused by both lifecycle
and direct numerical plan construction. No constitutive formula was added.

See [capabilities](CAPABILITY_MATRIX.md), [contracts](CONTRACTS.md),
[review findings](REVIEW.md), [verification](VERIFICATION.md) and
[resume instructions](RESUME.md). The fixed-content review manifest is under
`verification/reviews/DBF1/`. State after successful self-tests is
**READY_FOR_REVIEW**, never ACCEPTED without an independent gate.

## Exclusions and migration

D1–D7 remain partially implemented: strong entity IDs, coordinate/orientation
reference graphs, parameter/table schemas, layered sections, equations/loads,
typed generic results and arbitrary state-registration restart policies still
require subsequent vertical slices. The synthetic history fixture does not
claim the full mixed shell/beam/contact acceptance model or a physical solution.
No input keyword was added; programmatic ModelIR schema 2 is the new builder path.

Rebuild C++ consumers because `Material` and `ModelIR` grow cold metadata. Existing
aggregate initializers remain source-compatible; binary ABI compatibility is not
claimed. New readers consume old files; old readers intentionally reject schema 2.
Migrate only reviewed module commits into the internal buildable platform, retain
schema metadata with each model, and rerun its hardware/adapter tests there.
Production integration and CUDA are **not locally compiled / not locally verified**.
