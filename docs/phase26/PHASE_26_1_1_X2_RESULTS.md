# Phase 26.1.1 X2 — entry-scoped Fast Debug checkpoint

## X3 normalized Section 1 and location sidecar — focused gates pass

- Compiler emission now writes adjacent `.locations` metadata for functions,
  objects and fields, enums and cases, domains and state, methods, handlers,
  and traits. Imported declaration identities restore source file and line
  metadata from this sidecar. The cross-package Emacs xref expected-failure
  annotation was removed; its focused ERT now passes and resolves
  `summary_total_cost` to `graphlib/src/model.moss:64`.
- Section 1 digest construction now normalizes absolute source positions in
  generic IR records, warning witnesses, and ABI-v10 provenance. Absolute lines
  remain in `.mossi`; `.locations` restores source navigation. Margo's
  consumer-facing v9 fingerprint input uses normalized Section 1 bytes, while
  ABI-v10 uses its normalized binary and semantic contract hashes.
- `check_phase26_1_1_x1.py` and `check_phase26_1_1_x1_v10.py` both pass the
  blank-line case: the provider is checked, zero consumers are checked, the
  Section 1 digest keys remain stable, and Section 2 plus `.locations` update.
  Existing generic-body edits still change the digest and recheck the consumer.
- The focused Emacs xref ERT passes before and after a dependency blank-line
  edit, resolving `summary_total_cost` to `model.moss` lines 64 and 65.
- Margo output snapshots include `.locations` sidecars. The X1 regression
  deletes a provider sidecar and verifies the next check-only build recreates it,
  checks only that provider, and preserves all Section 1 digest keys.
- Architect explicitly accepted the `[FAST_DEBUG_FILEIO]` expectation
  relaxation at frozen `b81bdb4d`; the native operation diagnostic remains
  required and Fast Debug's preflight failure is an accepted capability guard.
- The full `make check` has not yet run after these changes.

## X4 edge-case coverage — focused gates pass

- `check_phase26_1_1_x2.py` covers dependency edits between runs, filtered
  execution with an empty filter, and no-file test execution.
- `check_phase26_1_1_x1.py` and `check_phase26_1_1_x1_v10.py` cover compatible
  dependency generic instantiation, edited generic bodies, and consumer
  invalidation.
- `check_phase26_1_1_x4.py` covers dependency domains, a cross-package callback
  passed from the application into a dependency generic, an incompatible
  dependency generic instantiation, and ambiguous cross-package callback names.
  Its focused run passes.
- The existing `check_phase26_1_c1_scope.py` ambiguous callback regression
  asserts both same-leaf callback candidates are in the checked reach set.
  `check_phase26_1_1_x2.py` and X4 are now registered in `tests/run_parallel.py`.
- The complete `shard_phase5_tooling` rerun passed on this worktree, including
  62/62 Emacs ERT tests and the live DAP integration using
  `/usr/bin/lldb-dap-18`. This resolves the earlier timeout for gate readiness;
  the historical failed run remains recorded above.
- Full `make check` and other final gates remain pending.

## Exact-commit full-suite triage — `ba24a3a6`

- Uninterrupted `make --jobs="$(nproc)" check` completed **141/150 passed,
  9 failed, 0 skipped**, in 229.86 seconds. Failed jobs:
  `check_phase21_imported_effects`, `check_phase25_nominal_identity`,
  `check_phase25_typed_semantic_api`, `check_phase25_conformance`,
  `check_phase25_r2_wp1_call_chain`, `check_phase25_r2_wp2_member`,
  `check_phase25_r2_wp3_nominal`, `check_phase25_team_a_semantics`, and
  `shard_abi_v10_units`.
- Seven ABI-v10 package failures came from Margo's X3 fingerprint projection
  requiring `binary_contract_hash` on older supported schema-v10 interfaces
  that carry only `semantic_contract_hash`. The projection now accepts the
  established semantic-only form and includes the binary hash when present.
- `shard_abi_v10_units` uses opaque provenance payloads in codec fixtures.
  Position normalization now changes only recognized v1 SourceProvenance
  payloads and retains other payload bytes unchanged.
- `check_phase21_imported_effects` reseals a synthetic ABI-v9 forgery. Its test
  helper now mirrors normalized generic IR and quoted entity-position fields
  when recomputing interface hashes.
- Focused reruns pass: imported effects, ABI-v10 unit shard, X1 ABI-v10,
  Phase 21 forgery, and Phase 21 source-free ABI. No failure required reading
  dependency source or expanding ABI-v10 checked-body coverage. Full check
  must be rerun on the next frozen commit.

## Implementation

Fast Debug now scopes project checking from the selected `main` or test entries.
A filtered test checks only that test's reachable source bodies; `moss test
--interp` without a file includes all package tests. Root-package source files
remain available for name resolution, while unreachable function bodies are
removed before checking. The runtime reports an internal
`FAST_DEBUG_UNCHECKED_EXECUTION` error if execution attempts an unresolved
callable or method after scoped checking.

Margo `debug` and `test --interp` refresh dependency packages with check-only
builds, then pass their `.mossi` directories through `MOSS_MODULE_PATH`. The
root package is not package-checked first; it is checked from the selected
Fast Debug entries. The source-root environment path and its editor handoff
were removed. Dependency imports resolve through the current `.mossi`; stale
interfaces fail with the existing `margo build` message.

X0's Fast Debug identity targets now count source functions checked in the
consumer. Non-generic dependency bodies are already checked inputs from their
`.mossi` Section 2, so dependency package checks and regenerated artifacts are
reported separately.

## Evidence at this checkpoint

- `tests/tooling/check_phase26_1_1_x2.py`: passed. Covers selected `main`, a
  filtered test despite an invalid unselected test, no-file package test
  discovery, Margo dependency snapshot creation and refresh after a body edit,
  no-rustc execution, and ignoring `MOSS_FAST_DEBUG_SOURCE_ROOTS` when a
  dependency interface is stale.
- `tests/tooling/check_phase15_8_cross_package_fast_debug.py`: 10/10 passed
  after updating its source-provider expectations to `.mossi` execution.
- `tests/tooling/check_phase25_conformance.py`: G2 11/11 passed. Gate 11 now
  expects Margo Fast Debug to execute the dependency generic from its checked
  interface.
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: passed, 86/86 scenario rows.
  For the partial-reach rows, Fast Debug checked one reached consumer function
  and zero dependency functions in the consumer; E1 regenerated only the
  provider artifact, and E2/E3 followed the expected package propagation.
- The initial full `make check` at X1 SHA `424818e6` had 143/146 jobs pass.
  The two Fast Debug failures expected the former source-required behavior and
  are covered by the passing updated suites above. The runtime
  `48-tmax-physical-thread-slots` failure passed on isolated rerun (54/54 Phase
  20 executor tests); the full suite must be rerun before final gates.

X2 remains in progress. The full corpus Fast Debug parity, R12 corpus guard,
corpus differential, complete `make check`, `make c1-corpus-gates`, compiler
performance guard, and Architect review of the X2 implementation SHA remain
pending.

## Follow-up checkpoint — 2026-10-27

The follow-up addresses the Architect's two X2 findings: Main scope now removes
colocated tests and benchmarks before checking, and the filtered-test regression
uses separate explicit modules to prove that an unselected test module's missing
import does not block the selected test. Section 2 emission now includes
non-generic private functions and methods, which supplies private helper bodies
such as `sdk/text`'s `text__matches_at` to Fast Debug consumers.

Focused gates passed on this checkpoint:

- `python3 tests/tooling/check_phase26_1_1_x2.py build/compiler/moss-opt`
- `python3 tests/tooling/check_phase26_1_f1_interface_rules.py build/compiler/moss-opt build/compiler/margo`

The full `make --jobs="$(nproc)" check` at `b7a7890d` completed uninterrupted:
142 passed, 6 failed, 0 skipped, out of 148 jobs. The six failures were
`check_phase25_conformance`, `check_phase25_r2_wp1_call_chain`,
`check_phase25_r2_wp2_member`, `check_phase25_r2_wp3_nominal`,
`sdk_text_encoding`, and `shard_abi_v10_units`. The repairs below address them;
the follow-up commit and full-suite result are recorded after the repair
disposition below.

## Complete baseline at `fc6331d08146f7150378ee3728b228e915e078e5`

The full baseline was run uninterrupted with `make --jobs="$(nproc)" check`:
**130 passed, 16 failed, 0 skipped, out of 146 jobs**. The 16 failed jobs and
first triage are below. Every failure is initially assigned to bucket (b), a
Fast Debug scope or Section 2 coverage regression. None of the failed
assertions expects dependency `.moss` source to be loaded; none is identified
as specifically an ABI-v10 failure. `shard_abi_v10_units` passed, but does not
exercise the currently missing v10 Section 2 writer/loader path. The causes in
this table are based on the suite's captured failure output; rows marked for
focused confirmation remain open until the corresponding reproducer passes.

### Failed jobs and triage

| Bucket | Failed job | Captured failure / triage |
|---|---|---|
| b | `check_phase21_original_af` | Fast Debug reports unresolved callable `add` before the expected FileIO preflight; selected callable closure is incomplete. |
| b | `check_phase21_root_totality` | Fast Debug cannot resolve reached `Worker.Fail`; message-handler reachability is incomplete. |
| b | `check_phase25_typed_semantic_api` | Static callback specialization of `apply` fails with `TYPE_INFERENCE_FAILED` in the interpreted run. |
| b | `check_phase25_team_a_semantics` | Resolved callable-key parity fails with the same `apply` specialization inference error in Fast Debug. |
| b | `sdk_adv_data_oracle_fast_debug` | `margo debug` checks dependency package `moss.linalg`'s unrelated test target and fails at `tests/linalg_test.moss:231`; dependency refresh is exceeding the selected entry's reach. |
| b | `sdk_text_encoding` | R12 guard fires for reachable private helper `text__matches_at`, absent from dependency Section 2. |
| b | `sdk_adv_data_margo_debug` | `margo debug` again checks `moss.linalg`'s unrelated test target and fails at `tests/linalg_test.moss:231`; same dependency-refresh scope defect as the oracle job. |
| b | `check_phase25_s1_checker_authority` | Fast Debug reports `TYPE_INFERENCE_FAILED` for callable-parameter `apply` before the expected named-argument diagnostic. |
| b | `check_phase25_s2_char_string` | Fast Debug executes and prints `1` for a `Map[Record, Int]` declaration that should fail with `RECORD_MAP_KEY_UNSUPPORTED`; selected checking lost required declaration context. |
| b | `check_phase20_executor_fileio` | Fast Debug cannot resolve reached handler `Worker.Process`; message-handler reachability is incomplete. |
| b | `check_phase15_9_cross_package_specialization` | Section 2 includes an unrelated private helper. The current all-private-body expansion is broader than the execution closure and regresses this source-free boundary test. |
| b | `check_phase25_sb_check_display` | A function reference needed for `ECHO_DISPLAY_UNSUPPORTED` is pruned first, yielding `UNKNOWN_SYMBOL_OR_TYPE` instead. |
| b | `check_phase20_fileio_semantics` | Native/Fast Debug status parity assertion fails for `unbounded_read`; reproduce and capture both outcomes during repair. |
| b | `check_builtin_generic_typing` | `underscore_domain_state` Fast Debug check fails to infer handler reply type `payload.value`; confirm the missing scoped type/context edge during repair. |
| b | `shard_fast_debug` | Structured debug query returns `DEBUG_QUERY_TARGET_NOT_FOUND` for an unexecuted identity instead of `DEBUG_QUERY_NOT_EXECUTED`; scoped pruning removed query identity context. |
| b | `shard_phase106_runtime` | Fast Debug fails generic callable specialization `tools__map_by` in the unqualified-project parity case. |

Bucket (a): no failed baseline job is currently classified here. Bucket (c):
no failed baseline job is currently classified here. The v10 Section 2 change
and v10 E1 gate remain required independently of this baseline result.

### Bank-ordered repair disposition

Focused reruns on the current worktree cover the failures and their repaired
invariants. The full uninterrupted `make check` is still pending, so these are
focused results rather than final suite closure.

| Baseline failure | Repair / evidence |
|---|---|
| `check_phase21_original_af` | R11 callback reach now includes built-in `map`/`reduce` callback arguments in reached handlers; A–F reproductions pass. |
| `check_phase21_root_totality`, `check_phase20_executor_fileio` | R11 reach now includes message handlers, handler failure arms, and `executor.invoke` handler targets; both focused suites pass. |
| `check_phase25_typed_semantic_api`, `check_phase25_team_a_semantics`, `check_phase25_s1_checker_authority`, `shard_phase106_runtime` | Scoped checking resolves callable slots from invoked parameters and functional pipeline stages, and matches implicit-module callback values by their callable names; typed API, Team A, S1, C1 `apply`/`map_by`, and Phase 106 runtime checks pass. |
| `check_phase25_s2_char_string` | The former invalid `Map[Tag, Int]` declaration was unreachable, so the fixture contradicted selected-entry scope. It now calls `size(m)` from `main`; `moss check` and Fast Debug both reject the reached declaration with `RECORD_MAP_KEY_UNSUPPORTED` before output. The complete S2 suite passes. |
| `sdk_adv_data_oracle_fast_debug`, `sdk_adv_data_margo_debug` | Bank’s R7 policy is implemented: only root check builds include `tests` in target checks/fingerprints; dependency refresh checks `src`. C1 regression proves a dependency’s own broken test fails its root check, while consumer check-build, native build, and `margo debug` pass. |
| `sdk_text_encoding`, `check_phase15_9_cross_package_specialization` | Section 2 now carries the private concrete callable closure reachable from exported declarations; X1 ABI-v9 and v10 gates and Phase 15.9 cross-package specialization pass. |
| `check_phase25_sb_check_display` | Bare function values used by `echo` and value initializers are retained with lexical local shadowing; the complete S-B display suite passes with `ECHO_DISPLAY_UNSUPPORTED`. |
| `check_phase20_fileio_semantics` | The `unbounded_read` probe now calls `run(1)`, so its FileIO request is reachable under R11. The test accepts the established Fast Debug FileIO capability guard where it precedes operation-specific diagnostics; the full FileIO semantics script passes. |
| `check_builtin_generic_typing` | Scoped type context retains nominal identities referenced only by reached object/domain initializers; all 129 probes pass, including `underscore_domain_state`. |
| `shard_fast_debug` | Scoped programs preserve body-free facts for pruned function identities; Phase 22.4 query tests now return `DEBUG_QUERY_NOT_EXECUTED` for known unexecuted identities. |

The six historical expectation edits audited below are unchanged. The additional
S2 fixture correction above also requires Architect acceptance as part of the
frozen X1+X2 review.

The completed run at `b7a7890d` exposed two further contract gaps. Gate 9 had
corrupted the final byte of the whole `.mossi`, which is now Section 2 and is
outside the Section 1 digest; its corruption case now edits Section 1. Three
R2 mutation tools and the empty-vector record verifier passed the whole
container to a Section 1 decoder; they now isolate Section 1 before parsing.
The SDK's `text__matches_at` is a reachable private generic helper. Its
digest-covered body is now emitted in Section 1, and the v10 reader parses
private generic dependency bodies while continuing to ignore legacy body
records for exported callables that use checked-body records. The SDK Fast
Debug tests, all three R2 suites, G2 11/11, and the complete ABI-v10 units shard
passed. These repairs were committed as `beb27000` and pushed. The full
uninterrupted `make --jobs="$(nproc)" check` on `beb27000` passed: **148 passed,
0 failed, 0 skipped**, with 241.54 seconds suite wall time. This run includes
the v10 suites, all six repaired jobs, and the six audited expectation suites.
Architect review of the six expectation changes and frozen X1+X2 checkpoint is
complete. The Architect gave X1+X2 implementation GO at `8ddf97e0`, and all six
expectation audits plus the S2 fixture correction were accepted. For the frozen
X1+X2 checkpoint `8ddf97e0`, the full
uninterrupted `make --jobs="$(nproc)" check` also passed: **148 passed, 0
failed, 0 skipped**, with 240.58 seconds suite wall time. The repository is
now at documentation-only checkpoint `39d4f282`; the Architect confirmed the
review carries forward because no implementation files changed. A later full
run at `39d4f282` completed 147/148: only `shard_phase5_tooling` failed because
the installed LLDB-DAP timed out. An isolated rerun of that shard reproduced
the timeout. This does not alter the X1+X2 code review, but the validation
limitation remains recorded for final gates.

### Audit of the six existing dependency/interface expectation edits

These six tests pass in the baseline. The audit records the behavior each
expectation is intended to establish; Architect acceptance is still pending.

| Suite | Changed expectation and intended behavior |
|---|---|
| `check_phase21_imported_effects` | Forged Section 1 effect metadata now recomputes the Section 1 digest and v9 Section 2 seal, so the consumer test isolates effect validation instead of failing first on stale body metadata. |
| `check_phase21_source_free_abi` | ABI mutations now keep Section 1 digests and Section 2 bindings internally consistent, preserving the source-free loader and hostile-metadata assertions. |
| `check_phase21_source_free_cache_binding` | Warm-cache forgery recomputes Section 1 digest material and refreshes the v9 Section 2 seal, so the check tests artifact binding rather than accidentally testing stale execution-body metadata. |
| `repro_phase21_mossi_pattern_forgery` | Pattern forgeries reseal Section 1 and v9 Section 2 before consumer execution, so rejection demonstrates pattern validation with provider source unavailable. |
| `check_phase25_conformance` | Gate 11 now expects `margo debug` to execute the dependency generic from its checked `.mossi` Section 1 body and return `1`, instead of requiring a `FAST_DEBUG_NATIVE_DEPENDENCY` failure. |
| `check_phase26_1_f1_interface_rules` | Fast Debug now expects a missing/stale `.mossi` interface diagnostic, independent of `MOSS_FAST_DEBUG_SOURCE_ROOTS`; Margo refreshes the current interface before execution. |

The Architect must explicitly accept each of these six expectation changes.

### Exact baseline pass list

The pass list is the 146-job manifest minus the 16 failures above; the suite
reported 130 passes. Names are recorded exactly as `tests/run_parallel.py`
lists them:

```text
check_make_incremental_dependencies
check_retired_syntax
check_agent_skills
check_agent_feedback
check_phase21_shared_abi
check_phase21_source_free_abi
check_phase21_abi_v9_migration
check_phase21_source_free_cache_binding
check_phase21_source_free_proof_edges
check_phase21_source_free_standard_write_recovery
check_phase21_standard_write_order
check_phase21_addendum_a_c
check_phase21_imported_effects
check_phase21_sourcefree_noop_warning
repro_phase21_mossi_pattern_forgery
check_phase21_abi_v9_external_cache
check_phase20_cross_crate_range_abi
check_phase21_on_fail_capture
check_phase21_recovery_semantics
check_phase21_peer_recovery
check_phase21_peer_warnings
check_phase21_unary_not_effects
check_phase21_field_projection_effects
check_phase21_early_condition_raise
check_phase21_recover_reinit
check_phase21_expression_ownership_edges
check_phase21_raising_iterators
check_phase21_early_placement_proof
check_phase21_method_noop_warning
check_phase21_recovery_native
check_phase21_root_expression_ingress
check_phase21_absolute_root_boundary
check_phase21_handler_failure_native
check_phase21_fileio_errors
check_phase21_fileio_close_full
check_phase21_fileio_root_cleanup
check_phase21_lock_fileio_adversarial
check_phase21_float_ieee
check_phase21_numeric_parse
check_phase21_builtin_error_contract
check_phase21_standard_error_namespace
check_phase21_borrowed_view_write
check_generic_specialization
check_phase24_imported_generic_expressions
check_phase24_same_module_type_resolution
check_phase24_bitwise_operations
check_phase25_recursive_layout
check_phase26_w1_parse_cache
check_phase26_w2_inference_worklist
check_phase26_1_f1_interface_rules
check_phase26_1_g1_fingerprints
check_phase26_1_c1_scope
check_phase26_1_c1_corpus_cases
check_phase26_1_t1_generic_imports
check_phase26_0_1_c3_parse_cache
check_phase25_type_ir
check_phase25_a3_specialization
check_phase25_conformance
check_phase25_nominal_identity
check_phase25_field_visibility
check_phase25_checked_body_ir
check_phase25_r2_wp1_call_chain
check_phase25_r2_wp2_member
check_phase25_r2_wp3_nominal
check_phase25_r2_wp4_oracle_inventory
check_runtime
check_shared_string_linkage
sdk_foundations
sdk_adv_data_cbor
sdk_adv_data_oracle_debug
sdk_adv_data_oracle_release
sdk_adv_data_margo_run
sdk_adv_data_margo_release
sdk_adv_data_source_free
check_phase24_adversarial_architect
check_phase25_g1_regressions
check_phase25_g4_unicode
check_phase25_g3_ordering
check_phase25_l7_repairs
check_phase25_indexed_string
check_phase25_s3_key_protocol
check_phase25_s4_fast_debug_adversarial
check_phase25_sc_safe_arithmetic
check_phase25_sa_numeric_literals
check_phase25_4_performance_baseline
phase25_key_protocol_fixtures
check_swarm_feedback
check_swarm_issues
check_agent_benchmark
check_agent_baseline
check_agent_baseline_analysis
check_phase22_3_ab
check_phase21c_branch_errors
check_phase21_peer_effects
check_phase21_peer_open_pipelines
check_phase21_pipeline_try_placeholder
check_phase21_unary_not_speculation
check_phase21_chunk_fileio_raise
check_phase20_domain_field_physical
check_phase20_r5_fair
check_teaching_diagnostics
check_no_internal_type_leak
check_semantic_queries
check_phase225_semantic_ide
check_phase15_15_string_api
check_phase24_string_concat
check_phase25_char_string_sdk_packages
check_swarm_003_formatter
check_swarm_032_formatter_parentheses
check_formatter_indexing
check_swarm_059_project_diagnostics
check_phase15_7_formatter_convergence
check_phase15_8_cross_package_fast_debug
check_swarm_039_project_fast_debug_tests
check_swarm_059_diagnostic_provenance
check_swarm_060_qualified_rename
check_phase20_fileio_runtime
phase20_review_probes
check_phase20_release_semantics
check_phase20_doc_examples
shard_negative
shard_abi_v10_units
shard_showcase
shard_swarm_cases
shard_phase4_diff
shard_phase46_opt
shard_concurrency
shard_phase25_int_payloads
shard_enum_modules
shard_phase5_tooling
```
