# DBF1 fixed-content review handoff

Date: 2026-10-29 Asia/Shanghai. **READY_FOR_REVIEW**; no independent acceptance.

- BASE / original REVIEW_HEAD: `9a3d754dbbc5c0b1720a93ecf29ee3278f705f73`.
- CONTENT_HEAD: `c5cd0622d7a52fb7c15db0e98dc33b5833d206a7`.
- Branch: `codex/astra-db-foundation-completion`.
- Inherited ALR1 review range starts at `480da32a17e3b60e6694b95e5c9ec83618e5b64b`.
- A subsequent documentation-only commit freezes this handoff, manifest/verifier
  and ledger pointer. Resolve it with `git log -1 --format=%H --
  docs/development/db_foundation/HANDOFF.md`; do not substitute it for content SHA.

| Commit | Task slice | Test/evidence |
|---|---|---|
| c5cd062 | D0 and DBF-001/002 rotation/enum repairs | exact old-source red probe and current-source green probe |
| c5cd062 | D1 metadata, D3 history, D5 restart, D6 model schema migration subset | foundation_contracts (171), process reader, legacy golden pair |
| c5cd062 | D2 ragged/sparse typed host storage | storage_contracts (82), scale byte counters |
| c5cd062 | D7 partial acceptance and inherited numerical compatibility | 15/15 GCC OpenMP, serial, MSVC ASan |

File inventory and exact Git-blob SHA256: `verification/reviews/DBF1/manifest.json`.
Run `python verification/reviews/DBF1/verify_packet.py --git` in the repository
to check fixed Git blobs, or omit `--git` to verify extracted/current files with
documented CRLF-to-LF text canonicalization. The manifest is integrity/provenance
evidence, not a cryptographic signature or independent acceptance report.

The user kit is retained as its original untracked directory and is not included
as authored implementation. No unrelated user files were committed. Parent-map
appendices remain local with the pre-existing parent work; their exact added
text and before/after hashes are in parent-doc-appendices.json.

Review focuses:

1. Complete HistorySchema identity and old fingerprint preservation; ensure an
   explicit required schema cannot enter any numerical backend silently.
2. Unknown enum/version rejection, dtype/shape bounds, low-budget storage encode,
   and decoder allocation boundaries.
3. Field/segment lifetime invalidation across copy/move/destruction; metadata
   spans remain caller-controlled borrows.
4. ModelIR1 byte preservation, ModelIR2 manifest agreement, state restoration
   failure atomicity and synthetic data being clearly storage-only.

See README, CONTRACTS, REVIEW, CAPABILITY_MATRIX, VERIFICATION and RESUME in this
directory for actual scope, assumptions and outstanding work. Strong ID wrappers,
general typed ResultDB fields, orientation/layer/section models, richer constraints
and loads, and the full D7 synthetic model are not delivered in this first batch.
No new physics, production integration, CUDA verification, push or stable merge.
