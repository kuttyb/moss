# Phase 28 Round 4 — Slice D Handoff

## Implementation and review

- Branch: `phase28-r4-slice-d`, based on `6b0433c8af071dc972b2bd89109e6be0e498de9b`.
- Reviewed implementation SHA: `0c722497f2c6639cf84c96b80a9748ead9308b0a`.
- Agent 2 verdict: **GO** on that exact SHA.
- Scope is limited to checker-owned typed exceptional ownership and provenance. No merge to `phase28-work` or `main`, and no consumer migration.

## Typed fact contract

`TypedExceptionalOwnershipFactV1` is paired with a checker-issued
`TypedExceptionalOwnershipRequirementV1`. Exact lookup binds source and recovery
statement-occurrence IDs, source EST arena and raising-operation `ExprId`, root
slot and argument index, typed raised variant, checked source/recovery
alternatives, specialization context, and specialization identity. Facts also
carry checked-summary provenance and state at the raise: binding identity,
reaching definition, type identity, consumed status, all borrow source binding
identities and storage paths, consumed bindings, and mutated storage paths.
Availability distinguishes `Present`, `MissingSource`, `MissingRecovery`,
`MissingOccurrence`, `MissingSummary`, and `ContradictoryAlternatives`; required
lookups fail closed on missing, duplicate, or contradictory authority.

Exceptional edges use Phase 21 checked raise alternatives and connect only to
matching `recover` arms. State reflects effects completed before the raise;
there is no rollback. A failed RHS does not publish an assignment's replacement
definition. Nested checked calls, Echo/Message arguments, and functional
pipeline callback stages retain their exact EST operation occurrence.

## Tested behavior

Focused fixtures and checker gates cover consume before raise and recovery use,
variant-specific arm routing, mutation state at exceptional exits, borrow
provenance and lifetime across recovery, rebind after last borrow, overlapping
borrow rejection, panic exclusion from typed recovery, imported source-free
WRITE/raise summaries, and checked functional callback summaries. The ownership
fact contract also exercises missing and perturbed authority. `take(place)`
regressions remain 18/18, with native/Fast Debug/optimized-native parity where
applicable. String debt remains 42 names / 7 bindings.

## Validation and regression delta

- `make --jobs="$(nproc)" moss-dev`: PASS (two pre-existing `type_ir.hpp`
  missing-field initializer warnings).
- `check_phase28_local_ownership.py`: PASS in default and EST-shadow modes.
- `check_phase21_source_free_standard_write_recovery.py`: PASS.
- `check_phase27_string_debt.py`: PASS (42/7).
- `check_agent_feedback.py`: PASS.
- `git diff --check`: PASS.
- `verify_take_fixtures.py`: 18/18 PASS on the final reviewed implementation,
  with checker/Fast Debug/native/optimized-native parity where applicable.
- Selector argument effects, specialized ownership contract, take/selector
  consumer (22 fail-closed checks), and Team B consumer: PASS in default and
  EST-shadow modes.
- Full default regression runner on the final reviewed implementation:
  167 jobs, 151 PASS / 16 FAIL / 0 SKIP. The 16 failed jobs are exactly the
  non-intermittent Round 3 ledger: `check_phase21_addendum_a_c`,
  `check_phase21_on_fail_capture`, `check_phase21_recovery_semantics`,
  `check_phase21_recover_reinit`, `check_phase21_borrowed_view_write`,
  `check_generic_specialization`, `check_phase26_1_c1_corpus_cases`,
  `check_phase25_team_a_semantics`, `check_phase25_s1_checker_authority`,
  `check_phase27_accepts_rustc`, `check_semantic_queries`,
  `check_builtin_generic_typing`, `shard_negative`,
  `shard_phase25_int_payloads`, `shard_swarm_cases`, and
  `shard_phase5_tooling`. No new failed job; no skips.
- Full EST-shadow regression runner on the final reviewed implementation:
  167 jobs, 151 PASS / 16 FAIL / 0 SKIP, with exactly the same 16 ledger names
  as default mode. No new failing job; no skips.

## Unsupported cases

Consumer migration remains outside Slice D. General collection ownership,
`copy`, expanded `take`, and `match consume` remain unsupported. Existing
FileIO close obligations are preserved. Any pipeline operation without a
complete checker-issued summary or exact stage occurrence fails closed; no
source-text fallback, hidden copy, implicit take, or exception rollback is
introduced.
