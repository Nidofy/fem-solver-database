# Reviewer report provenance (B01) — accurate hash record (O-004 repair)

Repository `.gitattributes` (`* text=auto eol=lf`) normalizes CRLF→LF when a
file enters Git. The first-round reviewer reports each contained exactly one
trailing CRLF, so their **committed blob bytes differ from the original
handoff files by that single byte**; content is byte-equal after CRLF→LF
normalization. The round-2 originals contain no CRLF, so their blobs are
byte-identical to the originals. Earlier ledger text claiming plain
"verbatim / hashes match" was inaccurate and is corrected by this record
(finding B01-O-004); reviewer history and verdicts are unchanged.

## Hash table (SHA-256; computed 2026-10-25 with the commands below)

| Report | Original file (handoff dir) | Original bytes / sha256 | Git blob sha256 (after CRLF→LF) | Normalized == original content |
|---|---|---|---|---|
| A round-1 | `sdb-review-b01-handoff/reports/astra-batch-review.md` | 21625 / `ab3b58afffb754593b6ae099ebe3a576983a180ce1a61ae868c4bea1125bf6e0` | `78763a24e73c58e248fd5b6da65c2060be7841427a24aa5fe9640dc331d61318` | yes (1 trailing CRLF removed) |
| B round-1 | `sdb-review-b01-handoff/reports/opus-batch-review.md` | 20609 / `34e234840fc3964081be6e0cb58b1779760ea08971d1c0c2fbcd6c0388d9e191` | `ff74bae84c820ee4376783614951135e0955b826ecb0cbb5f6c250eec3f804c8` | yes (1 trailing CRLF removed) |
| A round-2 | `sdb-review-b01-incremental-handoff/reports/astra-incremental-review.md` | 16761 / `72327ed61378f2b52910c4660498fee057771e4d76293ca72b33e52c9d0347e6` | `72327ed61378f2b52910c4660498fee057771e4d76293ca72b33e52c9d0347e6` | yes (no CRLF; blob == original) |
| B round-2 | `sdb-review-b01-incremental-handoff/reports/opus-incremental-review.md` | 11448 / `731dd74e41ee335ace9fac867c81b1f670576712cc2e3e22c7d0222574beab22` | `731dd74e41ee335ace9fac867c81b1f670576712cc2e3e22c7d0222574beab22` | yes (no CRLF; blob == original) |
| R3 merged | `sdb-review-b01-r3-handoff/reports/merged-incremental-review-r3.md` | 16887 / `3c41df931f436fdbc96aff5ed90d1d52a88d71df112aff06cce347a5490dfa32` | `3c41df931f436fdbc96aff5ed90d1d52a88d71df112aff06cce347a5490dfa32` | yes (no CRLF; blob == original; single merged reviewer authorized by the user, GPT-6 Astra High — not a dual-lane review) |

## Machine-replayable verification

```sh
# original bytes and hash
python -c "import hashlib,sys;print(len(open(sys.argv[1],'rb').read()),hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" <original-path>
# committed blob bytes and hash (at any commit containing the copy)
git show <commit>:verification/reviews/B01/<copy-name> | python -c "import hashlib,sys;b=sys.stdin.buffer.read();print(len(b),hashlib.sha256(b).hexdigest())"
# content-equality after normalization
python -c "import sys;orig=open(sys.argv[1],'rb').read().replace(b'\r\n',b'\n');blob=open(sys.argv[2],'rb').read();print(orig==blob)" <original-path> <copy-path>
```

## Recording rule for future report copies

Always record BOTH hashes (original byte hash + committed blob hash) and the
normalization rule; assert content equality after CRLF→LF. A plain
"hashes match" claim is valid only when the original contains no CRLF.
