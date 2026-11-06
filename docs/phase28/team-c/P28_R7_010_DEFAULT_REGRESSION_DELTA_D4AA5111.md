# P28-R7-010 Team C — Default Regression Delta at `d4aa5111`

**Preliminary evidence report.** This compares completed runner logs only. No
build or test was started for this comparison. The post-string-debt repair
freeze and full oracle run are still pending.

## Compared runs

| Run | Source SHA | Log | SHA-256 | Selected jobs | Runner concurrency |
| --- | --- | --- | --- | ---: | ---: |
| Candidate Default | `d4aa51111d62917c4a1caa180266291862c5b88d` | `tmp/phase28-r7-010-l7-query/tmp/r7-010-d4aa5111-regression-default.log` | `148e47e8cae74bf3cbdc7a7c6306bdaf573fd2de47bab014d6ab2d2c2d486b37` | 167/167 | 16 |
| `phase28-work` baseline Default | `150ab36b41e45504aaa400aa717da241be7542fb` | `tmp/phase28-r7-010-phase28-work-baseline/tmp/r7-010-baseline-default.log` | `3186f7e5344a409ce2e8458803f7d29b5867bc91c1e33ec24c5b78ce3fdd4bbd` | 167/167 | 4 |

The worktrees use the exact source SHAs above. The runner configurations differ
in concurrency and optimized-compiler path, so the comparison records observed
outcomes without claiming every difference is caused by source changes.

## Outcome delta

| Result | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PASS | 166 | 90 | -76 |
| FAIL | 1 | 76 | +75 |
| SKIP | 0 | 1 | +1 |
| Selected jobs | 167 | 167 | 0 |

The job-ID sets are identical. **75 failure IDs were added; no failure ID was
removed.** `check_phase21_borrowed_view_write` is the one inherited failure and
still fails. `shard_phase5_tooling` is a dependent skip because
`shard_phase4_diff` failed; it is not an additional failed job.

### Added failure IDs

```text
check_builtin_generic_typing
check_formatter_indexing
check_generic_specialization
check_phase15_15_string_api
check_phase15_8_cross_package_fast_debug
check_phase20_executor_fileio
check_phase21_absolute_root_boundary
check_phase21_addendum_a_c
check_phase21_early_condition_raise
check_phase21_fileio_close_full
check_phase21_fileio_root_cleanup
check_phase21_lock_fileio_adversarial
check_phase21_numeric_parse
check_phase21_on_fail_capture
check_phase21_original_af
check_phase21_peer_effects
check_phase21_peer_open_pipelines
check_phase21_raising_iterators
check_phase21_recover_reinit
check_phase21_root_expression_ingress
check_phase21_root_totality
check_phase21_source_free_proof_edges
check_phase21_standard_error_namespace
check_phase21_unary_not_speculation
check_phase21c_branch_errors
check_phase24_adversarial_architect
check_phase24_imported_generic_expressions
check_phase25_4_performance_baseline
check_phase25_char_string_sdk_packages
check_phase25_conformance
check_phase25_l7_repairs
check_phase25_nominal_identity
check_phase25_r2_wp1_call_chain
check_phase25_r2_wp2_member
check_phase25_r2_wp3_nominal
check_phase25_s2_char_string
check_phase25_s3_key_protocol
check_phase25_s4_fast_debug_adversarial
check_phase25_sb_check_display
check_phase25_team_a_semantics
check_phase25_typed_semantic_api
check_phase26_0_1_c3_parse_cache
check_phase26_1_1_x1_v10
check_phase26_1_1_x1_v9
check_phase26_1_1_x4
check_phase26_1_c1_corpus_cases
check_phase26_1_c1_scope
check_phase26_w2_inference_worklist
check_phase27_5_1_facts_fast_debug
check_phase27_5_for_match
check_phase27_accepts_rustc
check_phase27_est_mossi
check_phase27_optimizer_expression_uses
phase20_review_probes
phase25_key_protocol_fixtures
sdk_adv_data_margo_debug
sdk_adv_data_margo_release
sdk_adv_data_margo_run
sdk_adv_data_oracle_debug
sdk_adv_data_oracle_fast_debug
sdk_adv_data_oracle_release
sdk_adv_data_source_free
sdk_foundations
sdk_text_encoding
shard_abi_v10_units
shard_concurrency
shard_enum_modules
shard_fast_debug
shard_negative
shard_phase106_runtime
shard_phase25_int_payloads
shard_phase46_opt
shard_phase4_diff
shard_showcase
shard_swarm_cases
```

## Classification from the recorded output

These are classifications of the logged failure evidence, not a claim that all
75 are independent source defects. Categories overlap because aggregate jobs
can report several underlying failures.

- **Two non-semantic findings of different kinds:**
  `check_phase26_0_1_c3_parse_cache` hits a compile-hygiene defect: its
  isolated `-Werror` build rejects an unused `local_transfer` variable in
  `semantic_checker.cpp`. Separately,
  `check_phase25_4_performance_baseline` reports three fixture-integrity SHA
  mismatches (`packages/regex/src/main.moss`,
  `tests/phase106d1_borrowed_reads.moss`, and
  `tests/phase106d1_read_projection.moss`). These are logged integrity failures;
  this comparison did not inspect or alter those fixtures.
- **Checker-authority and fail-closed consumer failures appear repeatedly.**
  The logs include missing/ambiguous ReturnValue facts or EST types, invalid
  source-free ReturnValue authority, missing exact Root-ingress ExprIds or
  root-message copy/consume authority, missing indexed-read READ authority,
  missing Map/CollectionView authority, and missing Echo specialization
  context. Examples include `check_phase21_addendum_a_c`,
  `check_phase21_fileio_root_cleanup`, `check_formatter_indexing`,
  `check_phase15_15_string_api`, `check_generic_specialization`, and
  `shard_concurrency`.
- **Diagnostic or checked-result mismatches are also visible.** For example,
  `shard_negative` and `shard_phase25_int_payloads` expected
  `ENUM_MAP_KEY_UNSUPPORTED` for an enum Map key but logged
  `ROOT_UNHANDLED_RAISE`; `check_phase21_original_af` also logs an unexpected
  root-raise diagnostic.
- **Other aggregate package, SDK and shard failures report underlying compiler,
  interpreter, Margo or assertion failures.** This log-only pass does not
  attribute every nested package failure to one root cause. They remain
  unresolved failures, not reclassified as infrastructure without evidence.
- **One dependent skip:** `shard_phase5_tooling` skipped because
  `shard_phase4_diff` failed.

## Limits

This report does not prove which failure IDs are caused by a particular source
change. The candidate used 16 workers while the baseline used 4, and they used
different optimized-compiler paths. No test was rerun to normalize those
conditions. This is a preliminary Default checkpoint only; the Shadow log
comparison and the next frozen implementation's full oracle/inventory remain
pending. No implementation, test, runner configuration, or oracle file was
changed.
