# Phase 28 Round 4 Slice A Integration Report

## Integration record

- Target: `origin/phase28-work` at `5be319ba3c3f6bc165fd30474f790ff43c3241d9`.
- Source: `origin/phase28-r4-slice-a` at `2ed0df34642c58ac84101f66eac483417d830f96`.
- Reviewed implementation: `9b0fb7ef559c4a307ba168faec1b9bddd84b1c5f` (Agent 2 GO).
- Integration branch: `phase28-r4-slice-a-integration`.
- Integrated branch HEAD: `4d83969ecf94c887e40eb897cd05641eb4dcf7b0`.
- Merge: normal `--no-ff` merge; no conflicts and no conflict resolution required.
- Source integrity: reviewed implementation is an ancestor of the source HEAD. The range after the reviewed SHA contains documentation/status only; `src/` and `tests/` are unchanged.
- Validation changes: none. No compiler or test expectation was edited during integration.

## Targeted validation

All requested focused gates passed on the integrated branch:

- `make --jobs="$(nproc)" moss-dev`: PASS.
- `check_phase28_local_ownership.py`: PASS in default and `MOSS_EST=shadow`.
- `check_phase28_specialized_ownership_contract.py`: PASS in default and shadow; exact typed lookup, specialization isolation, missing/perturbed facts, and unsupported cases checked.
- `check_phase28_selector_argument_effects.py`: PASS in default and shadow.
- `check_phase28_take_consumer.py`: PASS in default and shadow; all 22 take/selector fail-closed cases passed in each mode.
- `check_phase28_team_b_consumer.py`: PASS in default and shadow.
- `verify_take_fixtures.py`: 18/18 PASS, including checker, Fast Debug, native, and optimized-native parity with zero hidden clones.
- `check_phase27_string_debt.py`: PASS at 42 raw Stmt name comparisons / 7 binding comparisons.
- `git diff --check`: PASS.

The default full `make --jobs="$(nproc)" check` invocation ran once. It built dev and optimized compilers and ran the complete default 167-job suite, which ended 149 PASS / 18 FAIL (exit 2). Make stopped at the default suite failure, so the full-suite EST shadow pass did not start. Focused ownership, selector, take, and consumer gates did pass under shadow.

## Regression delta against the Round 3 ledger

Seventeen failures match the existing `docs/phase28/ROUND_3_INTEGRATION.md` ledger:

- **Intentional compatibility changes (8):** `check_phase21_addendum_a_c`, `check_phase21_on_fail_capture`, `check_phase21_recovery_semantics`, `check_phase21_recover_reinit`, `check_builtin_generic_typing`, `check_phase25_team_a_semantics`, `shard_swarm_cases`, and `check_phase26_1_c1_corpus_cases`.
- **Incomplete Phase 28 behavior / migration pending (6):** `check_generic_specialization`, `check_phase25_s1_checker_authority`, `shard_negative`, `shard_phase25_int_payloads`, `check_semantic_queries`, and `shard_phase5_tooling`.
- **Preexisting failures (2):** `check_phase21_borrowed_view_write` and `check_phase27_accepts_rustc`.
- **Intermittent infrastructure failure (1):** `check_make_incremental_dependencies`.

One additional job, `check_phase21_standard_write_order`, failed once in the full parallel run at the optimized package output assertion. It passed on the target base in a direct run, passed four direct reruns on the integrated compiler, passed a filtered isolated-worktree run, and passed again in a 43-job Phase 21 parallel group. The failure was not reproduced, and no semantic conflict or regression is confirmed. It remains reported separately as an unconfirmed full-suite-only anomaly; it was not added to or hidden in the Round 3 ledger.

## Integration decision

**GO for integration to `phase28-work`, with the unconfirmed anomaly above disclosed.** The reviewed source is unchanged, all requested targeted authority and parity gates passed, and the only additional full-suite failure was not reproduced in repeated focused runs. The full `make check` command itself is not green: the 17 ledger failures plus the one unconfirmed anomaly are reported above. No unrelated debt or fixture expectation was changed.

No `main` merge or Slice B consumer migration is included.
