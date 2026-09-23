# Multi-Domain Swarm Torture-Testing

This directory contains the results and test suites from a 5-agent swarm campaign torture-testing the Moss v0.1 language across multi-domain architectures, synchronous message passing, 2PL synchronization plans, ownership semantics, and compiler boundaries.

## Architecture & Sub-Projects

### 1. `linear_pipeline/`
- **Topology**: 4-hop cascading linear pipeline: `IngestDomain -> ValidateDomain -> TransformDomain -> AggregateDomain`.
- **Features Tested**:
  - Deep synchronous `message` call cascading with return values rippling back in reverse order.
  - Early-exit filtering (`reply 0 - 1`) in `ValidateDomain` bypassing downstream domains.
  - State mutations and payload transformations at every hop.
  - Topological domain ranking ($0 \to 1 \to 2 \to 3$) and fine-grained 2PL lock classes.
- **Verification**: 5 unit tests pass natively (`margo test`) and in Fast Debug (`moss test --interp tests/test_pipeline.moss`). End-to-end `main` verified natively (`margo run`) and in Fast Debug with 1,138 JSON execution trace events (`moss run --interp --trace src/main.moss` and `margo debug [--trace]`).

### 2. `diamond_sync/`
- **Topology**: Diamond DAG: `CoordinatorDomain -> (WorkerAlphaDomain, WorkerBetaDomain) -> SharedStoreDomain`, plus direct observation route `CoordinatorDomain -> SharedStoreDomain`.
- **Features Tested**:
  - Multiple routes converging onto a single shared domain instance.
  - Interleaved task execution with in-flight store reads.
  - Compiler derivation of 4 distinct synchronization classes (`sync:0` to `sync:3`) for 4 state fields.
  - 10 handler pairs were classified as non-conflicting and therefore need not mutually exclude under concurrent invocation. (The committed integration scenario exercises sequential and interleaved calls without claiming runtime thread concurrency.)
- **Verification**: 5 unit tests pass natively (`margo test`) and in Fast Debug (`moss test --interp tests/test_diamond.moss`). End-to-end `main` (14 assertions) passes natively (`margo run`) and in Fast Debug (`moss run --interp`).

### 3. `rich_payloads/`
- **Topology**: 3-stage domain pipeline: `ProducerDomain -> RegistryDomain -> ConsumerDomain`.
- **Features Tested**:
  - Custom structs (`ComplexPayload`, `Config`, `ProcessedReceipt`, `TaskQueue`) across domain boundaries.
  - Passing and returning collections (`Vector[Int]`, `Map[String, Int]`, `Queue[Int]`) across domains.
  - Borrowed synchronous lowering preserves observable by-value message semantics; sender mutation after the call cannot retroactively change the completed receiver computation.
  - `moss cost` confirmed zero materialized bytes for internal synchronous message payloads under Phase 15.1 borrowed lowering.
- **Verification**: 4 unit tests pass natively (`margo test`) and in Fast Debug (`moss test --interp tests/test_payloads.moss`). End-to-end `main` verified natively (`margo run`) and in Fast Debug (`margo debug [--trace]`).

### 4. `dispatcher_fanout/`
- **Topology**: Star Hub: `DispatcherDomain` routing to `WorkerAlpha`, `WorkerBeta`, `WorkerGamma`, and `AuditLogDomain`.
- **Features Tested**:
  - Dynamic routing with priority bypass (`priority >= 10 -> WorkerGamma`) and adaptive load shedding (`WorkerBeta -> WorkerAlpha`).
  - Interleaved state mutations before and after cross-domain calls.
  - Mixed messaging: value-returning synchronous handlers (`worker_res = message worker.Execute(...)`) and no-value synchronous messages (`message audit.LogStart(...)`), where the caller synchronously waits for the target handler to complete without capturing a value.
- **Verification**: 7 unit tests pass natively (`margo test`) and in Fast Debug (`moss test --interp tests/test_dispatcher.moss`). End-to-end `main` verified natively (`margo run`) and in Fast Debug (`margo debug [--trace]`).

### 5. `domain_boundaries/`
- **Features Tested**: 27 negative boundary probes + 2 positive cases (29 total):
  - Negative probes covering route cycle detection (2-domain, 3-domain, self-route), self-send and same-domain handler chaining rejection, domain handle escaping (as function parameters, handler payloads, `reply` return, `Vector`, `Map`, or pipeline capture), dynamic domain construction outside `main`'s composition prefix (`while`, `for`, `if`, helpers, `test`), and impure domain state initializers.
  - 2 positive cases: pure-helper state initialization (`probe5c_pure_helper.moss`) and an idiomatic multi-domain DAG control (`positive_control.moss`).
- **Verification**: `python3 examples/swarm/domain_torture/domain_boundaries/run_suite.py` (29/29 passing). 100% frontend boundary enforcement with zero leaks to rustc, and 100% output parity between Fast Debug and native compilation for the positive control.

## Running the Suites

From the repository root:

```bash
# Linear Pipeline
(cd examples/swarm/domain_torture/linear_pipeline && ../../../../margo test && ../../../../margo run && ../../../../moss test --interp tests/test_pipeline.moss)

# Diamond Synchronization
(cd examples/swarm/domain_torture/diamond_sync && ../../../../margo test && ../../../../margo run && ../../../../moss test --interp tests/test_diamond.moss)

# Rich Payloads
(cd examples/swarm/domain_torture/rich_payloads && ../../../../margo test && ../../../../margo run && ../../../../moss test --interp tests/test_payloads.moss)

# Dispatcher Fan-out
(cd examples/swarm/domain_torture/dispatcher_fanout && ../../../../margo test && ../../../../margo run && ../../../../moss test --interp tests/test_dispatcher.moss)

# Boundary Probes & Positive Control
python3 examples/swarm/domain_torture/domain_boundaries/run_suite.py
```
