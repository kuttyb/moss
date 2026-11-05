# Independent Agent 2 Review — R7-010 Executor Runtime

## Reviewed implementation

- Implementation SHA: `3711f2ab8ac45357b6dd6c993dc6e45be9d21884`
- Source file: `src/executor_runtime.cpp`
- Source SHA-256: `1682c206d93efa3b26b697a71283b634e16c04210cafaceb27a03792a006a984`
- Generated process-root probe: `tmp/review-3711/probe.rs`
- Probe SHA-256: `435ab82e57f23c64120116b2d42a43148466f477c3bb2af5b6b364eab3ecb5e0`
- Reviewer: Independent Team B Agent 2
- Verdict for scoped TLS borrowing and move-only `MossExecutor::start`: **GO**

## Findings

`with_current_worker_exec` holds an immutable TLS `RefCell` borrow only while its callback synchronously calls `solo_enter_worker` or `solo_leave_worker`. Neither callee retains the `&Arc`; the enter path releases `gx.inner` before spawning, and the helper returns before the caller proceeds to any blocking Solo operation. Nested immutable borrows are valid and are exercised by test 53. Tests also check no worker context off-worker and from an inline Root. The test suite passed **55/55**.

`MossExecutor::config(&self)` remains compatible with the documented runtime API. Private `into_config(self)` moves the builder fields into `MossExecutorConfig`, and `start(self)` uses that move path. A generated `Executor(...).start()` probe emits `.start()` and does not invoke `.config()`, so Moss startup no longer clones the source-derived affinity vector through the snapshot method.

## Generated clone inventory

The exact probe contains three direct `.clone()` callsites and eight qualified `Arc::clone` callsites. Source and generated Rust line pairs follow.

| Form | Source line | Generated Rust line | Type and reviewed role |
| --- | ---: | ---: | --- |
| `.clone()` | 497 | 2014 | `Option<Arc<MossGlobalExec>>`; active executor handle |
| `.clone()` | 841 | 2364 | `Option<Vec<usize>>`; public host config snapshot, eligible in principle as runtime support after the move-only `start` repair |
| `.clone()` | 1391 | 2914 | `MossBranchScope` owning an `Arc<MossBranchScopeState>` count |
| `Arc::clone` | 58 | 3188 | `Arc<MossFairNode>` lock-tail reference |
| `Arc::clone` | 152 | 3282 | `Arc<MossFairWaiter>` condition-waiter reference |
| `Arc::clone` | 438 | 1955 | `Arc<MossBranchScopeState>` ready-queue reference |
| `Arc::clone` | 643 | 2160 | `Arc<MossGlobalExec>` worker TLS reference |
| `Arc::clone` | 723 | 2240 | `Arc<MossGlobalExec>` spawned-worker capture |
| `Arc::clone` | 903 | 2426 | `Arc<MossGlobalExec>` process active-executor reference |
| `Arc::clone` | 1073 | 2596 | `Arc<(Mutex<Option<R>>, Condvar)>` reply-slot handle; `R` itself is written once and taken once |
| `Arc::clone` | 1100 | 2623 | `Arc<MossGlobalExec>` pins executor beyond ingress-lock release |

The former worker-guard clone at source line 728 is absent from the generated artifact. The former TLS lookup clone at line 1433 is also absent. The affinity clone at worker startup was removed earlier. The direct `config(&self)` snapshot clone remains because that host-facing API returns an owned snapshot.

The generated explicit-copy provenance probe reports one authorized copy and three unauthorized/unclassified direct clone occurrences. This matches the current direct `.clone()` scanner. It is not a complete clone count: that scanner does not map the eight `Arc::clone` occurrences.

## Provenance and authorization

The frozen runtime callsite registry remains empty. This review authorizes **no** clone site and does not approve any Moss semantic copy.

The host-API-only `MossExecutor::config(&self)` snapshot clone is eligible in principle for a `runtime_support` row in an independently reviewed successor registry. The successor evidence must bind the exact callsite, type, source digest/span, API role and generated context, and prove the Moss lowering path calls `start`/`into_config` rather than `config`. The unchanged frozen registry/certification bytes must not be edited in place.

All eight qualified `Arc::clone` occurrences need scanner and sidecar coverage before any provenance-completeness claim. A successor must enumerate each exact occurrence, bind it to checked runtime ownership/type evidence, and fail closed on missing, duplicated, forged, stale or type-mismatched rows. In particular, the reply-slot row must prove that `R` is moved into the slot once and taken once; it must not be used to authorize an `R` clone.

## Validation evidence

- Isolated compiler build: PASS.
- `python3 tests/tooling/check_phase20c_executor.py tmp/review-3711/build/compiler/moss-dev`: **55/55 PASS**.
- Real explicit-copy provenance probe: `UNAUTHORIZED`, 1 authorized, 3 unauthorized, 3 unclassified; this is the expected blocking result for the three direct runtime `.clone()` sites.
- Nested TLS borrow, off-worker/inline context, builder reuse, and Solo compensation regressions are included in the targeted executor tests.
- Full oracle and default/shadow suites were not part of this focused review.
