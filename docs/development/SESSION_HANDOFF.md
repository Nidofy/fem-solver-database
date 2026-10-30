# Session handoff (minimal trusted context for a fresh GLM thread)

A fresh session MUST re-verify against real Git state before trusting this
file: `git status --porcelain=v1 -b`, `git log --oneline -12`, task pool.

## Current position (2026-10-30, DBF8)

SDK2.4 completes the user-confirmed INP file layer, global selectable MeshDB
Index64, explicit COW and C++/OS memory envelopes. Read
[DBF8 handoff](db_foundation/DBF8_HANDOFF.md), current task pool and DBF8 manifest.
Independent scoped review closed five P2 findings; platform and installed-consumer
evidence is bound in the new packet. Historical ALR1/B02c decisions remain unchanged.
No production adapter, kernel physics, CUDA, physical power-loss, cross-width
native conversion or exact-RSS claim. Preserve the original user kit. No push/merge.

## Historical position (2026-10-29, DBF1)

The current user explicitly requested implementation of SDB_DB_FOUNDATION_COMPLETION_KIT.
DBF1 continues from `9a3d754` on `codex/astra-db-foundation-completion`; it does not
inherit an independent acceptance verdict for ALR1. Read [DBF1](db_foundation/README.md),
[capabilities](db_foundation/CAPABILITY_MATRIX.md), and [resume](db_foundation/RESUME.md).
CPU serial/OpenMP/MSVC ASan pass 15/15 each. Current scope is READY_FOR_REVIEW;
full D1–D7, CUDA and production integration remain incomplete. Do not push/merge
or integrate B02c automatically. The DBF1 packet fixes its content SHA separately
from its subsequent documentation freeze commit.

## Historical position (2026-10-29, ALR1 freeze)

Current branch is `codex/astra-lifecycle-result-hardening`; base `480da32`, content
HEAD `8122adf2c149a672222ab81d8ac722e69931205c`. Read
[ALR1 HANDOFF](astra_lifecycle_result/HANDOFF.md), its manifest and STATUS first.
A1-A4 CPU lifecycle and A6 C3D8/mixed implementation are self-tested, not independently
accepted. Serial/OpenMP/MSVC ASan each passed 11/11. CUDA remains BLOCKED_ENV/NOT_RUN.
No ALR1 push/merge is authorized or performed. Next action is independent review;
no automatic next feature expansion. Preserve GLM B02c and historical B01 evidence.

The rest of this file records the prior B01 handoff, not current integration authority.

## Historical position (2026-10-28, independent R5 PASS and B01 acceptance)

- **B01 ACCEPTED** for the documented standalone CPU scope. Independent single
  GPT-6 Astra High R5 reviewed `15771815c016af66d064e5f4d66dc1dbc6b0a74a`
  and returned PASS / READY_TO_MERGE. R4-001/C2, R4-002/C3 and R3-001/A-005
  residual are CLOSED; no new finding.
- R5 actual checks: official 50/50, original R4 26/26, new independent 34/34;
  12/12 report hashes and 6/6 normalized matches. Report/evidence are recorded
  in verification/reviews/B01/acceptance.md; no C++/physics rerun is claimed.
- The user explicitly authorized merge and GitHub push after a passing review.
  Acceptance artifacts/ledgers are added after the reviewed tip; target is
  codex/standalone-database. Resolve local/remote refs for actual integrated
  HEAD rather than assuming the reviewed tip includes acceptance documents.
- BATCH_BASE aaa74afb3c264bbd7ca43555a7445c77994ccaa2;
  BATCH_HEAD 719c3fbbe4db1a4dfa0f9d324c3a376f885f47e5;
  freeze b58dfff04b484f2ed15524d51883e5f979fd1bc1.
- Fix milestones: round1 02e1ee4, round2 d6c67de, round3 63b24fc, round4
  parser d01ea18 and ledger 1577181. Runtime paths remain byte-identical
  to BATCH_HEAD. Earlier benchmark timing changes exist; R4/R5 increments
  add no benchmark/runtime/schema change.
- R1–R4 reports retain their original verdicts; R5 closes the final residual.
  R1/2 used two isolated Astra reviewers (Opus substitute disclosed); R3/4/5
  used single user-authorized reviewers. Raw/blob dual hashes and normalized
  content equality are recorded in report-provenance.md.

## Next development

1. Read Git refs/status and B01 acceptance. Continue B02 in a suitable isolated
   worktree from the accepted stable tip, with R1.03/R1.04 and measurement-led
   R2.04; do not restart the now-closed B01 repair loop.
2. Keep external fixed base/head/freeze expectations for identity validation.
   `python verification/reviews/B01/task-evidence/identity-controls.py` is
   the 50-control suite; the final archive preserves independent probes.
3. ../sdb-b02c / dev/glm-b02-candidate at b87fb53 remains a separate frozen
   unaccepted candidate; evidence may inform R2.04, no automatic merge.
4. B01 acceptance does not authorize unreviewed B02 stable integration or
   parent-solver changes. Preserve CUDA/UBSan/production limitations.

## Environment facts (verified 2026-10-24)

- Build (from repo root, Git Bash):

```sh
export PATH="/d/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin:$PATH"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="/c/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
"$CMAKE" -S . -B build-b01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDB_OPENMP=ON \
  -DCMAKE_CXX_COMPILER="D:/Software/MinGw-w64/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/g++.exe" \
  -DCMAKE_MAKE_PROGRAM="$NINJA"
"$NINJA" -C build-b01 && "$CMAKE" --build build-b01 --target test
```

- MSVC variant: **AddressSanitizer only** (`SDB_SANITIZE=ON`; the MSVC branch
  has no UBSan; MinGW ships no sanitizer runtimes at all). Build/test flow in
  `verification/reviews/B01/build-test.md`.
- Identity gate for every future packet:
  `python verification/validate_batch_identity.py <manifest.yaml> --expect-base <fixed-base> --expect-head <fixed-head> --expect-freeze <fixed-freeze>`
  (all values external constants; use full SHAs above).
- Historical CUDA probe was blocked. Current ALR1 nvcc/nvidia-smi/default Toolkit checks also fail; physical GPU absence is not asserted (CIM probe denied).
- `AGENTS.md` does not exist in this repo. clang-format:
  `"/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe" --style=file -i <files>`.
- The lifecycle benchmark always executes SERIAL (CpuRun has no policy
  parameter) — never draw parallel conclusions from it; every JSON row now
  carries `execution_policy:"serial"`.
- Exact-count test conventions live in tests/lifecycle_tests.cpp blocks
  labeled "R1.01" and "R1.02" — read before changing `publish()`/`advance()`.

## Working agreements (unchanged)

- Atomic commit per task/fix; update `docs/development/*` and
  `verification/reviews/<BATCH>/` after each unit.
- Never mark ACCEPTED before external review; no force-push/reset --hard;
  reviewer report files are provenance-recorded copies per report-provenance.md (never
  author or alter reviewer content); CUDA stays BLOCKED_ENV
  here; no push/merge of protected branches without explicit user
  authorization.
