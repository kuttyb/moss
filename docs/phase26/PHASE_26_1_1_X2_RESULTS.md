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
- The next Architect review at `c861efc2` found that Margo's ABI-v10 parser
  omitted `binary_contract_hash` from its field allowlist, so present binary
  hashes were not included in consumer fingerprints. The parser now collects
  the field. X1 v10 directly verifies that semantic-only legacy interfaces are
  accepted and distinct present binary hashes produce distinct fingerprint
  inputs; the focused gate passes.

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

## Final corpus parity repair checkpoint — 2026-10-27

The final Fast Debug sweep found three native-pass execution failures plus one
R12 guard hit. The causes and repairs are:

- `examples/swarm/Python/HashMap`: accept the source spelling `Vector [Int]()`
  as a typed empty-vector constructor before callable dispatch. This removes the
  R12 guard hit.
- `examples/swarm/Python/deque`: enter String `append`/`reserve` handling only
  for an actual String receiver; custom object methods with those names now
  reach normal method dispatch.
- `examples/swarm/modules/data_pipeline/analytics` and
  `.../repro/analytics_natural`: when scoped pipeline metadata is absent, retain
  the mapped runtime value's element type, and consult checked callback
  signatures where available. Both examples now execute successfully.

The full 438-package / 127-test-file sweep completed uninterrupted. The R12
guard fired zero times. Native-success/Fast-Debug-failure results are limited to
the three FileIO examples guarded by `[FAST_DEBUG_FILEIO]`, whose test
expectation was explicitly accepted by the Architect at `b81bdb4d`. Other
reported harness failures are cases where native also fails, or where its
strict build checks code intentionally outside the Fast Debug entry's scope;
they are not runtime parity mismatches. This sweep is recorded as a focused
corpus check; the formal all-gates run remains pending on the final frozen SHA.

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

## Previous X1–X4 checkpoint — `6b1ae847` — 2026-10-27

The final implementation is pushed on `phase26-1-m1`. Architect reviewed the
frozen X1+X2 tree at `6b1ae847` and returned GO. A prior NO-GO at `861fb2a9`
identified bodyless pruned declarations being visible to executable lookup;
the final commit separates signature lookup from executable dispatch, leaving
the R12 guard intact.

Final gates at the exact implementation SHA:

- Uninterrupted `make --jobs="$(nproc)" check`: **150 passed, 0 failed,
  0 skipped** (242.38 s wall). `shard_phase5_tooling` passed; the previously
  recorded LLDB-DAP timeout has been resolved on this work machine with
  `/usr/bin/lldb-dap-18`.
- Fast Debug corpus sweep: **438 packages, 349 mains, 127 test files**. The R12
  guard fired zero times. The only native-success/Fast-Debug-failure cases were
  `sdk/foundations/bytes/examples/range_copy`, `sdk/fs/examples/basic`, and
  `sdk/host/io/feasibility/consumer`, each guarded by `[FAST_DEBUG_FILEIO]` and
  covered by the Architect-accepted expectation at `b81bdb4d`. The raw strict
  return-code/output harness reported 98 failure rows; it compares native
  package-mode failures directly with selected-entry Fast Debug, so rows where
  native rejects unrelated package code are expected scope differences. The
  sweep summary and row log are under `tmp/phase26-1-1-fastdebug-corpus-6b1ae847.log`.
- Compiler-to-compiler differential against `origin/main`: **2,064 sources**,
  baseline 1,325 accepted and candidate 1,339 accepted; exactly 15 changed
  verdicts (12 C1 and 3 T1). All 1,325 unchanged accepted rows have identical
  Rust hashes. The 3 T1 rows newly emit Rust; the changed C1 package-context
  rows are fail-fast and emit none.
- `make c1-corpus-gates`: passed; 2,064 tracked sources, 438 packages,
  G-a **668/668**, G-b **68/68**, zero failures.
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: passed with target enforcement
  on; report `tmp/compiler-perf/m1-20261027-183253/M1_BASELINE.md`.
- `MOSS_PERF_BASELINE=0872d642 MOSS_PERF_REPS=5 make compiler-perf`: 57
  fixtures, aggregate median +0.6%, **no slowdown beyond the noise floor**.
- Nonblocking `MOSS_STAGED_ABI=v10` Fast Debug corpus run completed across the
  same 438 packages / 349 mains / 127 test files; R12 fired zero times. The
  strict harness recorded 120 failures, of which only two were
  native-success/Fast-Debug-failure, both Architect-accepted FileIO guards.

Bank review is pending. No merge to `main` has occurred.

## Final repair and gates — `f4b1686a` — 2026-10-27

The C1 corpus gate exposed a real package-check regression for implicit root
modules importing dependency modules. Per-file check and Fast Debug resolved
the checked dependency interface, but `margo build --check` skipped interface
lookup and rejected the same reachable call. The fix enables interface lookup
for semantic-only checks when Margo supplies `MOSS_MODULE_PATH`; native
Rust-emitting analysis keeps its existing package behavior. The X2 fixture now
asserts `margo build --check` accepts an implicit-root dependency import. The
Architect reviewed frozen commit `f4b1686a` and returned GO. This commit is
pushed to `origin/phase26-1-m1`.

Final gates at the exact implementation commit:

- Uninterrupted `make --jobs="$(nproc)" check`: **150 passed, 0 failed,
  0 skipped**, 238.59 seconds wall time. `shard_phase5_tooling` passed,
  including live DAP with `/usr/bin/lldb-dap-18`. The earlier timeout remains
  recorded historically; the successful rerun resolves it for gate readiness.
- Fast Debug against `origin/main`: **421 packages, 334 mains, 127 test
  files**; exactly three differences: the two documented T1 generic-import
  examples and the source-free dependency test case.
- Native-vs-Fast-Debug corpus run: **438 packages, 349 mains, 127 test files**;
  R12 guard hits **0**. The strict harness reported 98 raw rows; the three
  native-success/Fast-Debug-failure rows are the Architect-accepted
  `[FAST_DEBUG_FILEIO]` cases. Architect acceptance is recorded at
  `b81bdb4d`.
- Compiler corpus differential: **2,064 sources**; exactly **12 C1 + 3 T1**
  changed rows. All **1,325** sources accepted by both revisions have
  byte-identical generated Rust hashes. The changed rows are the 12 Calc /
  Toolkit C1 cases and the three generic-import T1 cases.
- `make c1-corpus-gates`: **G-a 669/669, G-b 67/67**, 438 packages, zero
  failures (10 no-hit packages, 9 terminal stale-interface cases, 12 known
  baseline packages).
- `MOSS_M1_ENFORCE_TARGETS=1 make package-perf`: passed with enforcement on;
  report is `tmp/compiler-perf/m1-20261027-200214/M1_BASELINE.md`.
- Five-repetition `MOSS_PERF_BASELINE=0872d642 make compiler-perf`: 57
  fixtures; aggregate medians **5.110 s baseline / 5.135 s candidate (+0.5%)**;
  no slowdown beyond the noise floor. Report:
  `tmp/compiler-perf/20261027-200238/LOCAL_REPORT.md`.
- Nonblocking ABI-v10 Fast Debug corpus run: **438 packages, 349 mains, 127
  test files**; R12 guard hits **0**. The strict harness reported 120 raw
  rows; only two were native-success/Fast-Debug-failure, both accepted FileIO
  guards.

X3's corrected acceptance is covered: a dependency blank-line edit rewrites
that provider's `.mossi`, Section 2 and `.locations`; normalized Section 1
digests stay stable, no consumers are rechecked, and xref advances from line
64 to 65. Real signature and generic-body edits still change Section 1.
X4 covers dependency edits between runs, dependency domains, compatible and
incompatible dependency generic instantiations, cross-package callbacks,
ambiguous callbacks, empty filters and no-file test execution. Focused X1 v9,
X1 v10, X2 and X4 gates pass.

The final compiler-perf report's `rw_packages_regex` and
`rw_sdk_datastructures` workloads measured 683.78 ms and 261.51 ms candidate
medians. These are compiler-perf check workloads, not separate Fast Debug
timings. No standalone Fast Debug timing study was run under Bank's no-timing-
studies instruction. M1 E0–E3 single-run Margo check wall observations are in
the package-perf report; for dependency-chain/core they were 37.565, 58.472,
76.954 and 119.787 ms. The report's Fast Debug identity/count table is also
included there.

The branch is pushed and awaits Bank review. Do not merge to `main` until
Bank's explicit go; certified compiler-perf remains a post-merge gate on clean
`main`.

## Bank pre-merge native agreement and G-b audit — follow-up in progress

At `f4b1686a`, Bank's native-agreement probe found that `margo build --check`
accepted the synthetic implicit-root dependency import while native
`margo build` and `margo test` rejected it as an unknown identifier. The cause
was that implicit compiled-interface lookup was enabled only for check-only,
file-scoped, or Fast Debug analysis. The current follow-up enables lookup
whenever Margo supplies a nonempty `MOSS_MODULE_PATH`. `check_phase26_1_1_x2.py`
now asserts that, on the same implicit-root fixture:

- `moss check <file>` rejects with `UNKNOWN_SYMBOL_OR_TYPE` without
  `MOSS_MODULE_PATH` and accepts with the current provider path;
- `margo build --check`, native `margo build`, and native `margo test` accept,
  and the native test executes the dependency-import test.

The focused X2 test and `make c1-corpus-gates` pass after this change. The C1
gate reports G-a **669/669**, G-b **67/67**, and zero failures. The frozen
Architect review issued GO on exact commit `e0b8dcc23351a1f119117bdaa8aa39648b101d57`
for the native implicit-import fix. Uninterrupted `make
--jobs="$(nproc)" check` on that commit completed **150/150 jobs, 0 failures,
0 skipped** (241.06s suite wall time).

The Fast Debug/native corpus audit was rerun after the native lookup change.
Counts below are for rows where the native command failed while the matching
Fast Debug command passed; the strict harness had 97 such mismatch rows in v9
and 119 in v10 overall, including reverse-direction mismatches.

| ABI mode | Native-fail / Fast-Debug-pass rows | Reachable native errors | Confirmed unreachable errors | Interface/tooling errors without a source-body location |
|---|---:|---:|---:|---:|
| v9 | 14 | 12 | 2 | 0 |
| v10 | 32 | 13 | 2 | 17 |

The exact-entry C1 scope traces came from `margo debug` for main entries and
`margo test --interp` for package tests, with `MOSS_PHASE26_C1_SCOPE_TRACE=1`.
They are preserved in the disposable v9/v10 classification JSON under `tmp/`;
the durable classification is:

- The two confirmed scope-only rows in both modes are
  `examples/swarm/Julia/Accumulator` (`Accumulator.merge@25` is absent from
  the `margo debug` trace) and
  `examples/swarm/modules/lib_and_app/sceneapp` (`report.print_header` is absent
  from the package test trace).
- Twelve v9 rows have native errors in code or dependency resolution present
  in the Fast Debug entry trace: `exported_generic_typed_callee/split`
  (`f`/`addone` link failure), `push_self_field_read/single` (`Links.link`,
  Rust E0502), `test_helper_fn/split` (reached test helper missing from Rust),
  `test_imports_not_in_main` (reached `other.triple`, missing test rlib),
  `test_module_mismatch/implicit_test` (reached `fa.helper` missing from Rust),
  `test_import/split` (reached `util.double`, missing test rlib),
  `test_local_type/split` (reached `Blob` type missing from Rust),
  `trait_only_module/split` (the traced `main`/`app.describe` needs the missing
  `traits` provider), `field_self_assign_struct/single` (`Holder.shift`, Rust
  E0502), both `mutate_loop_element` variants (reached `take_all`, Rust E0596),
  and `test_file_helper/split` (reached test helper missing from Rust). These
  are defects under Bank's reachability criterion, not accepted scope
  differences.
- ABI v10 adds one more reachable error:
  `tests/phase25/key_protocol/06j-proof-forged-interface/app`. Its native
  `KEY_PROOF_INVALID` is triggered by the reached `main` using
  `keyprov.name(...)` as a map key.
- The other 17 v10 rows are not source-body errors that can be shown
  unreachable: five `SOURCE_FREE_CONTRACT_INVALID` failures in
  `data_pipeline/analytics`, `data_pipeline/records`,
  `data_pipeline/repro/analytics_natural`, `lib_and_app/geolib`, and
  `sdk/ndarray`; plus twelve Margo `UnicodeDecodeError` failures while reading
  ABI-v10 `.mossi` data (`packages/compress`, `packages/hash`, `sdk/binary`,
  `sdk/foundations/acceptance` main/tests, its `nested_generic_int`,
  `nested_generic_string`, and `source_free_generics` repros,
  `sdk/foundations/collections/examples/basic` and `contains_only`, and
  `sdk/text-encoding-acceptance` main/tests). These remain unresolved interface
  and tooling failures.

The native implicit-root mismatch is fixed and reviewed, but the reachable
native errors and ABI-v10 interface/tooling rows block marking this branch as
awaiting Bank go. Do not merge. The issue has been reported to Bank for
direction; no further broad native backend changes are authorized by this
follow-up.
