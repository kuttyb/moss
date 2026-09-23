# Multi-Domain Swarm Torture-Testing

This directory contains the results and test suites from a 5-agent swarm campaign torture-testing the Moss v0.1 language across multi-domain architectures, synchronous message passing, 2PL synchronization plans, ownership semantics, and compiler boundaries.

## Architecture & Sub-Projects

### 1. `linear_pipeline/`
- **Topology**: 4-hop cascading linear pipeline: `IngestDomain -> ValidateDomain -> TransformDomain -> AggregateDomain`.
- **Features Tested**:
  - Deep synchronous `message` call cascading with return values rippling in reverse order.
  - Early-exit filtering (`reply 0 - 1`) in `ValidateDomain` bypassing downstream domains.
  - State mutations and payload transformations at every hop.
  - Topological domain ranking ($0 \to 1 \to 2 \to 3$) and fine-grained 2PL lock classes.
- **Verification**: `margo test` (5/5 passing), `margo run`, and Fast Debug with 1,138 JSON execution trace events.

### 2. `diamond_sync/`
- **Topology**: Diamond DAG: `CoordinatorDomain -> (WorkerAlphaDomain, WorkerBetaDomain) -> SharedStoreDomain`, plus direct observation route `CoordinatorDomain -> SharedStoreDomain`.
- **Features Tested**:
  - Multiple routes converging onto a single shared domain instance.
  - Interleaved task execution with in-flight store reads.
  - Compiler derivation of 4 distinct synchronization classes (`sync:0` to `sync:3`) for 4 state fields.
  - 10 non-conflicting handler pairs executing concurrently without exclusion.
- **Verification**: `margo test` (5/5 passing), `margo run` (14 assertions passing), and `margo debug [--trace]`.

### 3. `rich_payloads/`
- **Topology**: 3-stage domain pipeline: `ProducerDomain -> RegistryDomain -> ConsumerDomain`.
- **Features Tested**:
  - Custom structs (`ComplexPayload`, `Config`, `ProcessedReceipt`, `TaskQueue`) across domain boundaries.
  - Passing and returning collections (`Vector[Int]`, `Map[String, Int]`, `Queue[Int]`) across domains.
  - Snapshot isolation: sender mutating its state after message dispatch does not affect receiver.
  - Zero-copy borrowed lowering under Phase 15.1 (`moss cost` confirmed 0 materialized bytes).
- **Verification**: `margo test` (4/4 passing), `margo run`, and `margo debug [--trace]`.

### 4. `dispatcher_fanout/`
- **Topology**: Star Hub: `DispatcherDomain` routing to `WorkerAlpha`, `WorkerBeta`, `WorkerGamma`, and `AuditLogDomain`.
- **Features Tested**:
  - Dynamic routing with priority bypass (`priority >= 10 -> WorkerGamma`) and adaptive load shedding (`WorkerBeta -> WorkerAlpha`).
  - Interleaved state mutations before and after cross-domain calls.
  - Mixed messaging: value-returning synchronous handlers and no-value fire-and-forget audit messages.
- **Verification**: `margo test` (7/7 passing), `margo run`, and `margo debug [--trace]`.

### 5. `domain_boundaries/`
- **Features Tested**: 28 negative boundary probes + 1 positive control:
  - Route cycle detection (2-domain, 3-domain, self-route).
  - Self-send and same-domain handler chaining rejection.
  - Domain handle escaping (as function arguments, handler payloads, `reply` return, `Vector`, `Map`, or pipelines).
  - Dynamic domain construction outside `main`'s composition prefix (`while`, `for`, `if`, helpers, `test`).
  - Impure domain state initializers (messaging or impure helpers during initialization).
- **Verification**: `python3 run_suite.py` (29/29 passing). 100% frontend boundary enforcement with zero leaks to rustc.

## Running the Suites

From the repository root:

```bash
# Linear Pipeline
cd examples/swarm/domain_torture/linear_pipeline && ../../../margo test && ../../../margo run

# Diamond Synchronization
cd examples/swarm/domain_torture/diamond_sync && ../../../margo test && ../../../margo run

# Rich Payloads
cd examples/swarm/domain_torture/rich_payloads && ../../../margo test && ../../../margo run

# Dispatcher Fan-out
cd examples/swarm/domain_torture/dispatcher_fanout && ../../../margo test && ../../../margo run

# Boundary Probes
cd examples/swarm/domain_torture/domain_boundaries && python3 run_suite.py
```
