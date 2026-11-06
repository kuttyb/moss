# Team C independent verification — Phase 28 R7-010

Status: interim checkpoint; no overall GO. Verification was read-only with
respect to source, tests, frozen oracle inputs, and clone authorization.

## Exact inputs and builds

- Candidate: `993fbd54a5000d184dae5db7228da5fd63bffe4f`; source last changed at
  `1990dd82` per task handoff. Fresh detached worktree:
  `tmp/phase28-r7-010-team-c-verify-993fbd54`.
- Baseline: `150ab36b41e45504aaa400aa717da241be7542fb`. Fresh detached worktree:
  `tmp/phase28-r7-010-team-c-verify-baseline-150ab36b`.
- Candidate dev/opt build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-993fbd54-build moss moss-opt` (both passed).
- Baseline dev/opt build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-baseline-build moss moss-opt` (both passed).
- Candidate binaries SHA256: dev `d4bc514ab225ef81948b65726df7b79837290a80b01b12bb68fccde79ce31dcc`, opt `64b1ee3a6d07b1ea761f96d44729d7b7a5cfcf137c65999301da290ee04de952`.
- Baseline binaries SHA256: dev `6de238d137e07a11e80900f0109ba91cb53203ab2f0839fca94a73876337ab63`, opt `08db9ab0bd0ed9c8fcb35c8b4cf1c0c457aff77b5b111f0101ee25d2c61081c8`.

## Default matrix and candidate delta

Command in each isolated worktree:

```sh
env -u MOSS_EST python3 tests/run_parallel.py --jobs 16 \
  --compiler-dev tmp/team-c-<candidate-or-baseline>-build/moss-dev \
  --compiler-opt tmp/team-c-<candidate-or-baseline>-build/moss-opt
```

- Baseline: 167 selected; 166 PASS, 1 FAIL, 0 SKIP. The only failure is
  `check_phase21_borrowed_view_write`.
- Candidate: 167 selected; 97 PASS, 69 FAIL, 1 SKIP. It retains the baseline
  failure and has 68 additional failed job IDs plus one skip (`shard_phase5_tooling`).
  The exact candidate log is `tmp/team-c-993fbd54-default.log`.
- New failures include `check_phase26_1_1_x1_v9`, `check_phase26_1_1_x1_v10`,
  `check_phase27_est_mossi`, checker/consumer jobs, and multiple downstream
  shards. `check_phase25_4_performance_baseline` also fails its pinned corpus
  integrity step on three unrelated fixture SHA mismatches. There are no
  baseline failures cleared by the candidate.
- Candidate `take(place)` make-check preflight, run by importing the frozen
  harness and setting its compiler to the candidate binary: 17/18. The sole
  failure is `take_map_field`, Fast Debug invariant
  `nonprimitive Map view lacks checked nonescaping CollectionView authority`.

## Frozen ownership oracle and X1

Command:

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler tmp/team-c-993fbd54-build/moss-dev \
  --json-out tmp/team-c-993fbd54-oracle.json \
  --ledger-out tmp/team-c-993fbd54-oracle.md
```

Result: 60/60 semantic PASS; zero engine disagreements; 19/60 all-applicable-
gate PASS; 41/60 provenance failures. Generated Rust `.clone()` occurrences
unverified: 252; unauthorized: 252. Frozen input digests were preserved:

- expectations: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- diagnostic mappings: `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`
- runtime callsite registry: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`; registry has zero approved callsites.

Candidate focused X1 harness (`check_phase26_1_1_x1.py <candidate-dev>
tmp/team-c-993fbd54-x1`) passed with `ok:true`, covering `.mossi` authority
corruption/drop/duplicate/perturbation and native consumer checks. The generated
native app was then executed after temporarily hiding provider source; it
returned `4\n7\n9\n`. Fast Debug with physically removed provider source failed
closed with the stale/missing `.mossi` interface diagnostic, so physical-source-
free Fast Debug is not claimed.

## Clone source inventory

At the candidate source SHA, a read-only scan of `src/**/*.{cpp,h,hpp}` found
43 literal `.clone()` occurrences and 8 qualified `Arc::clone` occurrences.
Runtime support sites include three direct calls in `executor_runtime.cpp`,
five in `fileio_runtime.cpp`, and one in `char_string_ordering_runtime.cpp`;
there are seven marked qualified Arc clone sites in `executor_runtime.cpp`
and one unmarked Arc clone site in `fileio_runtime.cpp`. The frozen v2 runtime
registry approves none; no exception is made. Generated clone provenance is
reported by the oracle above.

## Shadow matrices and remaining verification

Candidate and baseline shadow matrices were started with
`MOSS_EST=shadow python3 tests/run_parallel.py --jobs 16 ...`, each using its
own detached worktree, compiler binaries, and build/cache paths. Candidate
Shadow completed at 167 selected; 97 PASS, 69 FAIL, 1 SKIP
(`shard_phase5_tooling`). Baseline Shadow completed at 167 selected; 165 PASS,
2 FAIL, 0 SKIP. Its failures are `check_phase21_borrowed_view_write` and
`shard_swarm_cases`. The candidate retains both IDs, adds 67 failed job IDs,
clears no baseline failed IDs, and adds one skip. The 67 new IDs are:

`check_builtin_generic_typing`, `check_formatter_indexing`,
`check_generic_specialization`, `check_phase15_15_string_api`,
`check_phase15_8_cross_package_fast_debug`, `check_phase20_executor_fileio`,
`check_phase21_fileio_close_full`, `check_phase21_fileio_root_cleanup`,
`check_phase21_lock_fileio_adversarial`, `check_phase21_numeric_parse`,
`check_phase21_on_fail_capture`, `check_phase21_original_af`,
`check_phase21_peer_effects`, `check_phase21_peer_open_pipelines`,
`check_phase21_recover_reinit`, `check_phase21_root_expression_ingress`,
`check_phase21_source_free_proof_edges`, `check_phase21_standard_error_namespace`,
`check_phase21_unary_not_speculation`, `check_phase24_adversarial_architect`,
`check_phase24_imported_generic_expressions`, `check_phase25_4_performance_baseline`,
`check_phase25_char_string_sdk_packages`, `check_phase25_conformance`,
`check_phase25_l7_repairs`, `check_phase25_nominal_identity`,
`check_phase25_r2_wp1_call_chain`, `check_phase25_r2_wp2_member`,
`check_phase25_r2_wp3_nominal`, `check_phase25_s2_char_string`,
`check_phase25_s3_key_protocol`, `check_phase25_s4_fast_debug_adversarial`,
`check_phase25_sb_check_display`, `check_phase25_team_a_semantics`,
`check_phase25_typed_semantic_api`, `check_phase26_1_1_x1_v10`,
`check_phase26_1_1_x1_v9`, `check_phase26_1_c1_corpus_cases`,
`check_phase26_1_c1_scope`, `check_phase26_w2_inference_worklist`,
`check_phase27_5_1_facts_fast_debug`, `check_phase27_5_for_match`,
`check_phase27_accepts_rustc`, `check_phase27_est_mossi`,
`check_phase27_optimizer_expression_uses`, `phase20_review_probes`,
`phase25_key_protocol_fixtures`, `sdk_adv_data_margo_debug`,
`sdk_adv_data_margo_release`, `sdk_adv_data_margo_run`,
`sdk_adv_data_oracle_debug`, `sdk_adv_data_oracle_fast_debug`,
`sdk_adv_data_oracle_release`, `sdk_adv_data_source_free`, `sdk_foundations`,
`sdk_text_encoding`, `shard_abi_v10_units`, `shard_concurrency`,
`shard_enum_modules`, `shard_fast_debug`, `shard_negative`,
`shard_phase106_runtime`, `shard_phase25_int_payloads`, `shard_phase46_opt`,
`shard_phase4_diff`, `shard_showcase`.

Candidate Default completed at 167 selected; 97 PASS, 69 FAIL, 1 SKIP.
Against Default baseline (166 PASS, 1 failure, no skips), it retains
`check_phase21_borrowed_view_write`, adds 68 failed job IDs, and adds one skip.
The candidate Default and Shadow logs are
`tmp/team-c-993fbd54-default.log` and `tmp/team-c-993fbd54-shadow.log`; baseline
logs are `tmp/team-c-baseline-default.log` and `tmp/team-c-baseline-shadow.log`.
This is an interim checkpoint, not final acceptance or merge authorization.
