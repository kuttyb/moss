# R7-010 regression triage at source `602a1440`

## Exact run boundary

- Source implementation: `602a14404af92c64f22b20391ed1579e03570098`.
- Branch HEAD during both full runs: `b96a35b055e429056eb2f26ab8d37d65502be5b`;
  intervening commits contain only review/status documentation. The tested
  compiler source tree is the exact reviewed `602a1440` tree.
- `make --jobs="$(nproc)" check`: **115/167 PASS, 52 FAIL, 0 SKIP**; suite
  wall time 379.28 s.
- `make --jobs="$(nproc)" check-shadow`: **115/167 PASS, 52 FAIL, 0 SKIP**;
  suite wall time 401.67 s.
- Both runs reported the same 52 failing jobs. Phase 28 focused gates passed,
  including method-parameter ReturnValue in Default and EST-shadow, Root-ingress
  474/474 in both modes, and local ReturnValue 44/44 in both modes.
- The previous 166/167 baseline belongs to an earlier SHA. These exact-source
  suite results do not meet that baseline. The method-only ReturnValue fix did
  not change the broad parallel-suite total; its adjacent Phase 21 aggregate
  now reaches a distinct incoming-message-payload-to-reply ownership error.

## Root-cause families

The failed jobs are repeated observations of several separate compiler and
fixture roots; an SDK or aggregate job may expose more than one. The entries
below give the primary evidenced failure and the repair direction. Do not
collapse them into a generic “ownership regression.”

1. **ReturnValue facts and exact callable context.** `check_phase21_on_fail_capture`,
   `check_phase21_recover_reinit`, `check_phase26_w2_inference_worklist`,
   `check_phase25_team_a_semantics`, `check_phase25_l7_repairs`,
   `check_phase25_sb_check_display`, `check_phase27_accepts_rustc`,
   `check_phase20_executor_fileio`, `shard_showcase`, and `shard_concurrency`
   report missing/ambiguous ReturnValue facts, missing returned-binding
   identity, invalid source-free ReturnValue authority, or missing Echo
   specialization context. Root action: compare the exact checker producer,
   method/function/handler ownership environment, `.mossi` body projection, and
   each consumer's occurrence/context reconstruction; retain independent
   drop/duplicate/perturb checks. The method-parameter identity repair closes
   only the specific method-parameter source-identity hole.

2. **Exact call and specialization selection.** `check_phase25_conformance`
   reports an ambiguous exact call specialization; `check_phase27_5_1_facts_fast_debug`
   reports that a dropped call fact did not fail closed. These are call-edge
   authority/keying problems, not the method-parameter binding defect. Repair
   the checker-selected exact call occurrence/context and make the registered
   consumer corruption case target that key.

3. **Collection and indexed-read authority.** `check_phase21_borrowed_view_write`
   accepts a `Range` case the fixture expects to reject;
   `check_phase15_15_string_api`, `check_generic_specialization`, `sdk_foundations`, `sdk_text_encoding`,
   `check_phase25_s2_char_string`, `check_phase25_char_string_sdk_packages`,
   `check_formatter_indexing`, `shard_enum_modules`, and several advanced-data
   jobs expose missing Map view receiver/context, conditional Map READ, or
   indexed-read/projection facts. The generic-specialization basket also
   reports independent ownership failures. Split repairs by fact class:
   nonescaping CollectionView, conditional Map read, indexed element read, and
   source-free projection; keep each fail-closed when absent or contradictory.

4. **Raise/effect and Root-edge accounting.** `check_phase21_original_af` and
   `shard_fast_debug` report `ROOT_UNHANDLED_RAISE` where the fixtures expect
   another outcome; `check_phase21_lock_fileio_adversarial` reports a missing
   bare-raise transfer edge; `check_phase21_numeric_parse` has a ParseError
   effect mismatch; `check_phase21_peer_open_pipelines` has a different
   inferred raise set; `check_phase21c_branch_errors` fails during a large
   read pipeline probe; `shard_negative`, `shard_phase25_int_payloads`, and
   `shard_swarm_cases` also report Root-totality diagnostic mismatches. Trace
   the exact CFG edge/effect source per fixture before changing expected
   diagnostics.

5. **Ownership-contract or projection mismatches.**
   `check_phase26_1_c1_corpus_cases`, `check_phase25_s3_key_protocol`,
   `phase25_key_protocol_fixtures`, `check_phase24_adversarial_architect`,
   `check_phase25_r2_wp3_nominal`, `check_phase15_8_cross_package_fast_debug`,
   `check_builtin_generic_typing`, `shard_phase106_runtime`, and parts of
   advanced-data packages report illegal field consume, collection-element
   consume, use-after-consume, or domain-state consume. Inspect each exact Moss
   expression against the frozen ownership contract; repair the checker fact
   if the source operation is valid, otherwise update the source/fixture to an
   explicit `take`/`copy` or a legal borrow. Do not introduce implicit copies.
   `check_phase21_standard_error_namespace` reaches a known distinct incoming
   message-payload-to-reply ownership rejection after its method ReturnValue
   provider path succeeds. `check_phase25_s4_fast_debug_adversarial` separately
   fails on missing nonprimitive Root-ingress copy/fresh-transfer authority;
   that is not the primitive TypeRef gap closed by ORACLE-45. `shard_abi_v10_units`
   fails in a known-open nominal source case and needs its own source-free
   ABI/type-projection diagnosis.

6. **Downstream SDK and source-free fallout.**
   `sdk_adv_data_margo_debug`, `sdk_adv_data_margo_run`,
   `sdk_adv_data_margo_release`, `sdk_adv_data_oracle_debug`,
   `sdk_adv_data_oracle_fast_debug`, `sdk_adv_data_oracle_release`, and
   `sdk_adv_data_source_free` repeat collection/index, ownership, or imported
   authority failures from their package sources. They are consumers of the
   roots above, not seven independent compiler fixes. `check_phase25_s2_char_string`
   and the string/encoding suites likewise need their underlying indexed or
   conditional-Map authority corrected before package-level acceptance.

7. **Validation/tooling debt.** `check_phase25_4_performance_baseline` fails
   three stale fixture SHA pins (`packages/regex/src/main.moss`,
   `tests/phase106d1_borrowed_reads.moss`,
   `tests/phase106d1_read_projection.moss`); update only through the owning
   fixture review. `shard_phase5_tooling` has three Emacs dependent-package
   failures. `check_agent_benchmark` reports expected validation failure
   `AB014`. `phase20_review_probes` has two gating failures
   (`guard_executor_invoke_loop`, `bug_batch_index_inline_handler`) among other
   probe observations; isolate these cases before assigning them to another
   compiler family. These tooling/probe results do not establish Moss copy
   semantics.

## Exact failing-job set

The following 52 names appeared as FAIL in both full-suite summaries:

```text
check_phase21_original_af
check_phase21_on_fail_capture
check_phase21_recover_reinit
check_phase21_lock_fileio_adversarial
check_phase21_numeric_parse
check_phase21_standard_error_namespace
check_phase21_borrowed_view_write
check_phase26_w2_inference_worklist
check_generic_specialization
check_phase27_5_1_facts_fast_debug
check_phase26_1_c1_corpus_cases
check_phase25_conformance
check_phase25_team_a_semantics
sdk_foundations
sdk_adv_data_margo_debug
sdk_adv_data_oracle_fast_debug
sdk_adv_data_oracle_release
check_phase25_r2_wp3_nominal
sdk_text_encoding
sdk_adv_data_margo_run
sdk_adv_data_oracle_debug
sdk_adv_data_margo_release
check_phase24_adversarial_architect
sdk_adv_data_source_free
check_phase27_accepts_rustc
check_phase25_s2_char_string
check_phase25_s3_key_protocol
check_phase25_s4_fast_debug_adversarial
phase25_key_protocol_fixtures
check_phase25_l7_repairs
check_phase20_executor_fileio
check_phase21_peer_open_pipelines
check_agent_benchmark
check_phase21c_branch_errors
check_phase15_15_string_api
check_phase25_sb_check_display
check_phase25_char_string_sdk_packages
check_builtin_generic_typing
check_formatter_indexing
check_phase15_8_cross_package_fast_debug
shard_negative
shard_showcase
shard_abi_v10_units
shard_swarm_cases
shard_concurrency
shard_enum_modules
shard_phase25_int_payloads
shard_fast_debug
shard_phase106_runtime
phase20_review_probes
shard_phase5_tooling
check_phase25_4_performance_baseline
```

Full per-job stdout/stderr from the latest run is in the ignored
`build/tests/parallel/<job>/` directories in the isolated staging worktree.
This report records the durable family-level diagnosis and exact job names;
each implementation slice must still re-run its focused source fixture and
registered mutation controls.

## Next dependency

Continue ReturnValue producer/context and source-free projection triage from
the named failures, then split collection/index and Root/effect failures by
checker fact class. Keep the reviewed implementation candidate and both
independent reports in isolated staging. Resume runtime clone remediation only
after this failure-family ledger and its next repair checkpoint are recorded.
No clone exception is authorized.
