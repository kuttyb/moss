# Phase 28 Round 4 Slice B Integration Report

## Integration record

- Target: `origin/phase28-work` at `123bd4e01a3efdf86b9d0b9b2688e2b32eab56c1`.
- Source: `origin/phase28-r4-slice-b` at `cc86da2e5538740e977b5db5cddb9c0196a71736`.
- Reviewed implementation: `0460274a7d206f1a68ed90d34a35c694a1539dfd` (Agent 2 GO).
- Integrated branch: `phase28-work`.
- Integrated merge commit: `0ee29b740871e8c75e24395e263fc0e7371946dc`.
- Source integrity: `git diff 0460274a HEAD -- src/ tests/` is completely empty. The Agent 2-reviewed implementation is preserved exactly without any source or test modifications during integration.
- Validation changes: none. No compiler or test expectation was edited during integration.

## Targeted validation

Targeted focused gates were run in default and `MOSS_EST=shadow` modes:

- `make --jobs="$(nproc)" moss-dev moss-opt`: PASS.
- `check_phase28_local_ownership.py`: PASS in default and `MOSS_EST=shadow`.
- `check_phase28_specialized_ownership_contract.py`: PASS in default and `MOSS_EST=shadow`.
- `check_phase28_take_consumer.py`: PASS in default and `MOSS_EST=shadow` (all 22 take/selector fail-closed checks pass in both modes).
- `check_phase28_team_b_consumer.py`: PASS in default and `MOSS_EST=shadow`.
- `verify_take_fixtures.py`: 18/18 PASS with Fast Debug, native, and optimized-native parity and zero clones.
- `check_phase27_string_debt.py`: PASS at 42 raw Stmt name comparisons / 7 binding comparisons.
- `verify_defects.py`: PASS (4 PASS, 4 KNOWN FAILURES).
- `check_agent_feedback.py`: PASS.
- `git diff --check`: PASS.

Focused gate anomaly on reviewed SHA `0460274a`:
- `check_phase28_selector_argument_effects.py`: fails in default and shadow on `take_selector_argument_effects.moss` at `fn:take_consume`. The test exercises `consume_selector(cursor: Cursor) -> Int: held = cursor; return held.index`. Under `0460274a`, `infer_straight_line_definition_effect` evaluates `held` across its projection `.index` and determines `READ` rather than `CONSUME`, publishing effect 0 instead of 2. Preserved as-is per instruction to keep `0460274a` unchanged.

## Full-suite regression delta against the 17-failure ledger

The 167-job parallel test suite ran once: 149 PASS / 18 FAIL.

### Comparison to the 17-failure ledger:
- **16 of the 17 ledger failures reproduced as expected:**
  - *Intentional compatibility changes (8):* `check_phase21_addendum_a_c`, `check_phase21_on_fail_capture`, `check_phase21_recovery_semantics`, `check_phase21_recover_reinit`, `check_builtin_generic_typing`, `check_phase25_team_a_semantics`, `shard_swarm_cases`, `check_phase26_1_c1_corpus_cases`.
  - *Incomplete Phase 28 / migration pending (6):* `check_generic_specialization`, `check_phase25_s1_checker_authority`, `shard_negative`, `shard_phase25_int_payloads`, `check_semantic_queries`, `shard_phase5_tooling`.
  - *Preexisting failures (2):* `check_phase21_borrowed_view_write`, `check_phase27_accepts_rustc`.
- **Intermittent infrastructure failure (1):**
  - `check_make_incremental_dependencies` PASSED in this run (20.73s).

### New regressions documented:
1. `check_phase21_expression_ownership_edges`:
   Fixture `phase21_expression_shared_variant_edges.moss` defines `consume_and_fail(number: Int, data: String)` containing `discarded = data; raise Failure.Bad`. Under `0460274a`, because `discarded` is unread after assignment, per-definition analysis concludes `READ` rather than `CONSUME`. Consequently, `data` is not marked consumed at the call site, and subsequent access in `recover` does not trigger expected `OWNERSHIP_USE_AFTER_CONSUME`.
2. `shard_enum_modules`:
   Negative tests `read_payload_consume` and `read_projection_payload_consume` expect `BORROWED_ENUM_PAYLOAD_CONSUME` on `let escaped = message; echo escaped`. Under `0460274a`, `escaped` is analyzed as a read-only borrow because it is only printed, so no consume error is emitted.

## Integration decision

Integration merged cleanly into `phase28-work`. The reviewed implementation `0460274a` was strictly preserved without edits to compiler source or test expectations. All new regressions and delta findings against the 17-failure ledger are fully documented above.

No merge to `main` has occurred.
