# Independent DBF2 review corrections

Independent reviewer: `gpt-6-astra`, high reasoning, user-authorized. Initial
implementation `826e65352772cd10508297466e59cd6e11816388`, followed by portable
orientation-diagnostic commit `cfeeab7`. The initial review correctly reported FAIL;
the fixes below require independent re-verification, not self-declared acceptance.
Exact initial evidence is retained in `verification/reviews/DBF2/independent-astra/`.

| Finding | Severity / reproduced behavior | Correction / regression |
|---|---|---|
| Semantic history drift | P1: identical schema ID/version accepted contradictory units; component 0 history joined XX=10 and YY=200 after reorder | Reject duplicate field semantic keys; compare complete selected history contract, basis and region; six drift cases and exact region query in typed_results_tests |
| Pre-admission scratch | P1: a 1,024-byte queue rejection allocated 13,600,098 bytes for 100,000 addresses | Allocation-free required_bytes; validation in admitted worker; global-new probe checks zero accounting allocation, bounded rejection and post-admission validation |
| Matrix allocation amplification | P1: 72-byte malformed 1024×1024 BSR DTO caused 8,388,608 aligned bytes before shape rejection | Validate value count before construction; reconstruction budget; host_memory allocation counter regression |
| Typed label sentinel | P2: live legacy UINT64_MAX node converted to invalid typed ExternalLabel | Checked adapter conversion, invalid typed-input rejection; retain raw legacy label API compatibility |
| Result source identity | P2 inherited validation gap: wrong source index/ID and duplicate scoped key accepted | Validate sources on Native1/Indexed2/TypedIndexed3 read and write, bounded pointer-sort scratch; 12 malformed writer/independent-decoder cases; synthetic scaling fixtures now populate real keys |
| Explicit sample truncation | P2: one explicitly stored IP of an eight-IP element was elided as canonical; reader accepted nonexistent IP8 | Preserve all explicit sample layouts; shortened-layout State/ResultModel/reader regression |

Additional cross-platform self-test correction: unknown orientation now has an
explicit stable diagnostic; the test no longer assumes libstdc++'s `map::at` text.
MSVC ASan's ordinary COFF section limit required `/bigobj` for the standalone
lifecycle interface; no numerical behavior or external library was substituted.

Focused corrected self-tests before freezing fixes: typed results **421 checks**,
boundary/matrix/layer **64 checks**, allocation-free admission plus worker validation
PASS. Full GCC serial/OpenMP, MSVC ASan and independent reviewer retests follow.
