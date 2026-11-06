# P28-R7-010 — L7 Review Status

> **Recovery correction (2026-11-05):** The snapshot below is historical and
> its 13/60 oracle result is obsolete. The last complete oracle and clone
> inventory are recorded against tested source SHA
> `ae57a3e3eccd2fbbc4a85f94228fe86102d4929b`: 60/60 semantic PASS, zero engine
> disagreements, 19/60 all-gate PASS, 41 provenance-only failures, 252
> unclassified direct `.clone()` occurrences, and 588 qualified `Arc::clone`
> occurrences. The last full suites were 90/167 Default and 90/167 EST-shadow
> on `d4aa51111d62917c4a1caa180266291862c5b88d`, versus 166/167 for each
> baseline at `150ab36b41e45504aaa400aa717da241be7542fb`. These results belong
> to earlier tested SHAs and do not validate current staging `ff0e6eef`; no
> current-HEAD oracle or full-suite result is claimed. No clone is authorized
> by the counts.

## Frozen Team A candidate review

Independent exact-SHA review of `735a36c3ac2b1a6501721ca746cb7a6293ce6cd1`
returned **NO-GO**. The isolated Phase 26 C3 `-Werror` gate passed. The
overloaded-method identity issue remains open: Int and String `Choice.next`
overloads produced colliding ReturnValue facts, and the Phase 26 X1 gate failed.
The detailed repro, commands, and evidence are in
[`agent2-reviews/R7-010-team-a-735a-review.md`](agent2-reviews/R7-010-team-a-735a-review.md).
The frozen candidate remains intact and is not integrated. A follow-up must
produce a new frozen SHA and receive independent review before integration.

The follow-up is frozen at implementation SHA
`3dc803a241c49bbff7c6976fbdb0c749e3aba56d` on
`phase28-r7-010-team-a-overload-fix`. Independent re-review returned scoped
**GO** for the checker-side overloaded ReturnValue identity repair and
explicitly closed the overloaded-method issue and isolated Phase 26 C3
`-Werror` requirement. The detailed exact-SHA report is
[`agent2-reviews/R7-010-team-a-overload-fix-3dc803a2.md`](agent2-reviews/R7-010-team-a-overload-fix-3dc803a2.md).

The whole Team A stack remains **NO-GO**. A fresh exact build of baseline
`150ab36b` passes X1, while `735a36c3` and `3dc803a2` fail later at the same
check-only Fast Debug ReturnValue consumer. The overload repair did not change
that stack-level regression. Duplicate native Rust method-name lowering is
also present on the Team A candidate and baseline and is tracked separately.
None of these scoped results are full-suite or current-staging oracle evidence.

## X1 source-free Reply authority intake (2026-11-05)

The exact-SHA scoped-GO implementation `46ff31314cc6000e9543a7459e07a19a7119c703`
is integrated on isolated branch `phase28-r7-010-vertical-integration` as
`1990dd82`. It carries handler and `on_fail` Reply facts through `.mossi`
Section 2 and exact imported consumers. The combined tree rebuilds; X1 now
passes, including generated native output `4\n7\n9\n`, artifact and consumer
corruption cases, and check-only/native-profile parity. The overload authority
gate passes Default and EST-shadow, and isolated C3 `-Werror` passes. Agent 2's
review is archived at
[`agent2-reviews/R7-010-X1-authority-46ff3131.md`](agent2-reviews/R7-010-X1-authority-46ff3131.md).

Whole Phase 28 remains NO-GO. The earlier 92/167 EST-shadow result and the
Default preflight stop at 17/18 were measured on pre-X1 code SHA `12b9407c`;
they do not validate `1990dd82`. No full Default/EST-shadow suite, frozen
oracle, or final clone-provenance audit has run on the integrated tree. No
runtime clone exception is authorized.

On `1990dd82`, a fresh focused take harness is 17/18; only `take_map_field`
fails closed for missing bounded, nonescaping Map-view authority. The checker
producer/remap/use-context path is under scoped follow-up. A same-harness-SHA
comparison confirms baseline `150ab36b` passes 18/18 with native/optimized
parity, so this is a candidate regression. Independent Team C verification of
intake SHA `993fbd54a5000d184dae5db7228da5fd63bffe4f` is in progress in an
isolated worktree; no final Team C verdict is recorded yet.

**Branch:** `phase28-r7-010-wip-checkpoint`  
**Current HEAD:** `06a266385ab25ef35917d0c8bf3e7821c057d91c`  
**State:** isolated staging; no merge to `phase28-work` or `main`. The HEAD is
the committed integration checkpoint. The compiler repairs below are currently
uncommitted and have not received independent review or been pushed.

## Integrated reviewed deliveries

- Base `phase28-r7-008-integration` at `dedcb1ed6e7178abc1a4419415af09c976f0d6a1`.
- Team A R7-009: `27454cc6ac47b3749ff8fc45161fead661e887ca` (implementation
  `6abe746ae7d641c8abbc7970ec1ee2ec5b97bc56`), independent Agent 2 GO;
  integration merge `9cfff9731105353a4fa6bd9a7af317ed8c178f63`.
- Team C B4: `c1c677e7c57d13d7cb8bfd7a1b1de34b2f5ba804` (implementation and
  gate registration `81350ff6046cff120da61a1c35f4af8c91049804`), independent
  Agent 2 GO; integration merge `e34afb6c77eaa329a2a42365832893ad091fd50b`.
- Team B v2 oracle package imported from `66447bf8d923436b430b85b59aa3a323750988a1`
  after normative, holdout, and final artifact approvals. Only the v2 package
  and review/report documents were imported; the original v1 oracle remains
  byte-for-byte preserved.
- V2 expectation SHA-256:
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`;
  runtime allowlist SHA-256:
  `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`.

## L7 local changelist (uncommitted)

The working diff touches:

- `src/semantic_checker.cpp`, `src/interpreter.hpp`, `src/native_generator.cpp`,
  and `src/moss.cpp`: checker-authorized one-argument `Map.get` write-through
  projection and consumer use of checked access facts; initial typed
  `CollectionError` integration and fail-closed runtime/native handling.
- `tests/tooling/check_phase28_r7_006_checker_facts.py`: Map.get regression,
  corruption, and focused CollectionError cases.
- `docs/MOSS_COMPILER_CONTRACT.md` and
  `docs/phase28/P28_R7_010_RECONCILIATION_REPORT.md`: implementation notes and
  updated reconciliation status.
- `agent-feedback/phase28/team-l7-r7-010/` and
  `docs/phase28/P28_R7_010_POST-COLLECTION-ERROR-FIX_FAILURE_LEDGER.md`:
  feedback and the current fresh failure ledger. The older
  `P28_R7_010_POST_MAP_GET_FAILURE_LEDGER.md` is superseded.

The latest focused repair scopes Vector/Queue CollectionError facts to
`try`-body operations, avoiding blanket typed effects that rejected established
valid Vector/Queue use. Canonical type matching uses the compiler's lowercase
collection names. Temporary debug logging was removed.

## Validation evidence

- `make --jobs="$(nproc)" moss`: PASS after the current source edits (two
  existing missing-field-initializer warnings).
- `python3 tests/tooling/check_phase28_r7_006_checker_facts.py ./moss`: PASS,
  including Map.get write-through and focused recoverable Vector errors.
- Fresh 60-fixture v2 oracle, result JSON at
  `tmp/phase28-r7-010/oracle-after-fix/results.json` and durable ledger at
  `P28_R7_010_POST-COLLECTION-ERROR-FIX_FAILURE_LEDGER.md`: 13/60 pass all applicable
  gates; 15 semantic failures; 2 native/Rustc versus Fast Debug discrepancies;
  35 provenance-blocked fixtures; 524 generated `.clone()` occurrences
  unverified; 0 infrastructure failures. This is the latest result, not the
  earlier post-Map.get ledger.
- `git diff --check`: PASS. No full default or EST-shadow suite has been run.
- Earlier combined integration gates recorded in
  `P28_R7_010_RECONCILIATION_REPORT.md`: Team A 9/9 equivalence; Team C parity
  and corruption harness PASS (21 corruption cases); targeted ownership,
  checker facts, copy, match, take, specialized, and EST authority gates PASS.
  Those results predate the current uncommitted edits and should be rerun at
  the final checkpoint.

## Remaining blockers and next sequence

1. Repair the remaining semantic failures: CollectionError inference/totality
   (ORACLE-07/48/53/54/55), ownership joins and match lifetimes (including
   ORACLE-15/28/30/31/32/33/35/46/49), and associated diagnostics.
2. Resolve native discrepancies ORACLE-27 and ORACLE-51.
3. Implement clone provenance tied to checked copy authority; no occurrence
   missing provenance may be marked authorized.
4. Rerun targeted authority/corruption gates, then the full default and
   EST-shadow suites once each; compare both against `phase28-work` baselines.
5. Rerun the frozen 60-fixture oracle, obtain independent Agent 2 GO for the
   exact frozen implementation SHA, then commit/push the reviewed staging
   deliverable as authorized. No production merge is authorized.

No final SHA is frozen, no final independent review exists, and no push has
been made for the L7 repair changelist.
