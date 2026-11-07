# P28-R7-011 Step 0 suite comparison

Source candidate: `00b54a515364f9237ce758eb866fa621da5375b5` (code; later `ac6e3a13` is documentation-only). Baseline source: `45b586c2de3a405f96f0be8268947a1abb98d3c1`.

Commands used the repository-prescribed `tests/run_parallel.py` runner with `--jobs 8`, `build/compiler/moss-dev`, `build/compiler/moss-opt`, and isolated output roots. Candidate Default and `MOSS_EST=shadow` runs have identical failure sets.

| Run | Jobs | Pass | Fail | Skip |
|---|---:|---:|---:|---:|
| 45b586c2 baseline | 168 | 112 | 55 | 1 |
| 00b54a51 Default | 170 | 117 | 53 | 0 |
| 00b54a51 EST-shadow | 170 | 117 | 53 | 0 |

By name, **52 failing jobs are shared**. The three baseline failures that recover are `check_phase26_1_c1_scope`, `shard_phase46_opt`, and `shard_phase4_diff`. The only candidate failure name absent from the baseline failure set is `shard_phase5_tooling`; the baseline skipped that job because `shard_phase4_diff` failed. Candidate-only new registered gates `check_phase28_l8_callback_authority` and `check_phase28_l8_field_controls` pass.

When `shard_phase5_tooling` runs on 00b, it reports three Emacs-mode failures: `moss-mode-cross-package-xref-source-location-known-regression`, `moss-mode-dependent-package-check-and-compilation`, and `moss-mode-dependent-package-cross-module-navigation`. Since the job was skipped on baseline, this is newly exposed work, not a demonstrated regression. The Build Planner `OWNERSHIP_USE_AFTER_CONSUME` at source line 58 after transfer at line 47 occurs in the separate shared failing job `check_phase15_8_cross_package_fast_debug`; it is not the `shard_phase5_tooling` cause.

## Shared failure names

- `check_agent_benchmark`
- `check_builtin_generic_typing`
- `check_formatter_indexing`
- `check_generic_specialization`
- `check_phase15_15_string_api`
- `check_phase15_8_cross_package_fast_debug`
- `check_phase20_executor_fileio`
- `check_phase21_borrowed_view_write`
- `check_phase21_lock_fileio_adversarial`
- `check_phase21_numeric_parse`
- `check_phase21_original_af`
- `check_phase21_peer_open_pipelines`
- `check_phase21_standard_error_namespace`
- `check_phase21c_branch_errors`
- `check_phase24_adversarial_architect`
- `check_phase24_imported_generic_expressions`
- `check_phase25_4_performance_baseline`
- `check_phase25_char_string_sdk_packages`
- `check_phase25_conformance`
- `check_phase25_g3_ordering`
- `check_phase25_l7_repairs`
- `check_phase25_r2_wp3_nominal`
- `check_phase25_s2_char_string`
- `check_phase25_s3_key_protocol`
- `check_phase25_s4_fast_debug_adversarial`
- `check_phase25_sb_check_display`
- `check_phase25_sc_safe_arithmetic`
- `check_phase25_team_a_semantics`
- `check_phase26_1_c1_corpus_cases`
- `check_phase27_5_1_facts_fast_debug`
- `check_phase27_accepts_rustc`
- `check_phase27_optimizer_expression_uses`
- `phase20_review_probes`
- `phase25_key_protocol_fixtures`
- `sdk_adv_data_margo_debug`
- `sdk_adv_data_margo_release`
- `sdk_adv_data_margo_run`
- `sdk_adv_data_oracle_debug`
- `sdk_adv_data_oracle_fast_debug`
- `sdk_adv_data_oracle_release`
- `sdk_adv_data_source_free`
- `sdk_foundations`
- `sdk_text_encoding`
- `shard_abi_v10_units`
- `shard_concurrency`
- `shard_enum_modules`
- `shard_fast_debug`
- `shard_negative`
- `shard_phase106_runtime`
- `shard_phase25_int_payloads`
- `shard_showcase`
- `shard_swarm_cases`

## Baseline failure names not failing on candidate

- `check_phase26_1_c1_scope`
- `shard_phase46_opt`
- `shard_phase4_diff`

## Candidate failure names absent from baseline failures

- `shard_phase5_tooling` (baseline skipped; see cause above)
