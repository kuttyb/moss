# P28-R7-010 Team C — Shadow Regression Delta at `d4aa5111`

**Preliminary evidence report.** This compares saved runner logs only. No build
or test was started for this comparison. The post-string-debt repair freeze
and full oracle run are still pending.

## Compared runs

| Run | Source SHA | Log | SHA-256 | Selected jobs | Runner concurrency |
| --- | --- | --- | --- | ---: | ---: |
| Candidate Shadow | `d4aa51111d62917c4a1caa180266291862c5b88d` | `tmp/phase28-r7-010-l7-query/tmp/r7-010-d4aa5111-regression-shadow.log` | `3d513503e92f63c97d9120173551094afaa61f03fc7ad664114899b79a0286bb` | 167/167 | 16 |
| `phase28-work` baseline Shadow | `150ab36b41e45504aaa400aa717da241be7542fb` | `tmp/phase28-r7-010-phase28-work-baseline/tmp/r7-010-baseline-shadow.log` | `a5b2666f49b2a278a14f65e9c2b35fa94b6f84043e8b71fc2d1ac09a3f64f3f8` | 167/167 | 4 |

The worktrees use the exact source SHAs above. Runner concurrency and optimized
compiler paths differ, so this records outcomes without claiming every
difference is caused by source changes.

## Outcome delta

| Result | Baseline | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PASS | 165 | 90 | -75 |
| FAIL | 2 | 76 | +74 |
| SKIP | 0 | 1 | +1 |
| Selected jobs | 167 | 167 | 0 |

The job-ID sets are identical. **74 failure IDs were added; no failure ID was
removed.** The inherited failures `check_phase21_borrowed_view_write` and
`shard_swarm_cases` remain. `shard_phase5_tooling` changed from PASS to a
dependent skip because `shard_phase4_diff` failed in the candidate run.

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
```

## Classification from the recorded output

The labels describe logged evidence, not 74 independent source defects.
Categories overlap because aggregate jobs can report multiple underlying
failures.

- **Checker-authority and fail-closed consumer failures appear repeatedly.**
  Logged examples include missing or ambiguous ReturnValue facts/EST types,
  unavailable checked ReturnValue authority, missing exact Root-ingress
  ExprIds, missing root-message copy/consume authority, missing indexed READ
  or CollectionView authority, and missing Echo specialization context.
- **Diagnostic/checked-result mismatches are present.** `shard_negative` and
  `shard_phase25_int_payloads` expected `ENUM_MAP_KEY_UNSUPPORTED` for an enum
  Map key but logged `ROOT_UNHANDLED_RAISE`; `check_phase21_original_af` also
  logs an unexpected root-raise diagnostic.
- **Two explicit non-semantic issues are visible.**
  `check_phase26_0_1_c3_parse_cache` fails an isolated `-Werror` build on an
  unused `local_transfer` variable in `semantic_checker.cpp`.
  `check_phase25_4_performance_baseline` reports three fixture SHA mismatches
  (`packages/regex/src/main.moss`, `tests/phase106d1_borrowed_reads.moss`,
  and `tests/phase106d1_read_projection.moss`).
- **Other aggregate package, SDK and shard failures** report underlying
  compiler, interpreter, Margo or assertion failures. The log-only pass does
  not attribute every nested failure to a single cause; they remain unresolved.
- **One dependent skip:** `shard_phase5_tooling` skipped because
  `shard_phase4_diff` failed.

The 16-worker candidate and 4-worker baseline used different optimized compiler
paths. No rerun normalized those conditions. This is a preliminary Shadow
checkpoint; it does not replace full frozen-oracle and clone/provenance
validation against the next repaired source SHA. No implementation, test,
runner configuration, or oracle file was changed.
