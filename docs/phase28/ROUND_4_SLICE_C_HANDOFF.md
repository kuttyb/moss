# Phase 28 Round 4 — Slice C Loop Ownership

## Implementation

- Branch: `phase28-r4-slice-c` from authorized baseline
  `ea7208964b29edb3363aa5cd58acf6d5321af833`.
- Implementation SHA: exact source/test commit recorded in the final review
  closeout; no source or test edits follow that SHA unless Agent 2 re-reviews.
- Scope: checker-owned while/for ownership across loop entry, backedges,
  zero-iteration exits, and converged exits. No merge to `phase28-work` or
  `main`; Slice D is not started.

## Typed fact contract

`TypedLoopOwnershipFactV1` and `TypedLoopOwnershipRequirementV1` are keyed by
the exact loop statement occurrence and typed control-edge source/target. Each
fact identifies the loop EST occurrence, the reaching definition's RHS EST
occurrence, lexical binding and definition identities, definitions killed by
rebinding, source binding provenance, specialization context, converged
READ/WRITE/CONSUME access, edge kind, and per-definition availability state.
The checker publishes entry, backedge, zero-iteration-exit, and converged-exit
facts from the existing statement-occurrence graph. Exact lookup is
fail-closed for missing or contradictory records. Consumers do not yet use
these facts.

## Tested behavior

- Independent definitions and rebindings converge through while/for backedges.
- Whole-local RHS evaluation precedes replacement; reinitialization restores
  availability only after successful RHS evaluation.
- Zero-iteration paths preserve pre-loop definitions; loop-created definitions
  do not enter zero-iteration facts.
- Consumed loop-carried values reject when a continuing path lacks
  reinitialization; for-loop dead local transfers are included.
- Rebinding after a borrow's final reachable loop use is accepted; rebinding
  while the alias remains live is rejected.
- Unreachable backedges are excluded from ownership joins.
- FileIO lifecycle constraints and Slice A/B ownership behavior remain covered.

## Validation and regression delta

- `make --jobs="$(nproc)"`: PASS.
- `check_phase28_local_ownership.py`: PASS in default and EST-shadow modes,
  including loop fact drop/perturb fail-closed checks and Fast Debug/native
  parity on supported loop fixtures.
- Slice A/B ownership, specialized-fact, selector, `take`, and consumer gates:
  PASS. `verify_take_fixtures.py`: 18/18.
- EXPRESS-005 F11c/F11d: 2/2 PASS.
- String debt: 42 raw Stmt name comparisons / 7 binding comparisons.
- Agent feedback validation and `git diff --check`: PASS.
- Final default and EST-shadow parallel regression suites: each 167 jobs,
  151 PASS, 16 FAIL, 0 SKIP. All 16 failing job names match the
  non-intermittent Round 3 ledger in `ROUND_3_INTEGRATION.md`; no new failing
  job remains. The shadow run's `shard_swarm_cases` job reports the existing
  `put(take())` EST structural divergence, independently reproduced with the
  baseline compiler at `ea7208964b29edb3363aa5cd58acf6d5321af833`. An earlier
  validation attempt exposed a missing for-loop dead-transfer case; that
  implementation defect is fixed, covered by
  `for_loop_unused_owner_move.moss`, and both final suites pass
  `shard_enum_modules`.

## Known limits

Typed exception/recover edges, generalized collection transfer, `copy`,
expanded `take`, `match consume`, projection-origin propagation, imported
summary propagation, and consumer-side use of loop facts remain unsupported or
outside Slice C. Ownership consumer debt remains open.

## Review and handoff

- Independent Agent 2 verdict and exact reviewed SHA: pending.
- Recommended next slice after L7 authorization: Slice D, subject to its
  approved scope; do not begin it under this authorization.
