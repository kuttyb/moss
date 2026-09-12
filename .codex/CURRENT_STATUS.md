# Moss current status

Updated: 2026-09-11

## Version and commits

- Compiler version: Moss v0.2
- Repository was synchronized with `origin/main`; the previous frontend checkpoint is
  `d2cf9aa` before inferred state/reply syntax work is committed.
- Previous known compiler checkpoint/base: `acebe2c1adad1d04d9d3f0e1ac506084417af741` (with the subsequent shared-memory and clustering implementation on `cd65d8c` in the prior handoff).
- The current working tree adds inferred domain state, inferred handler replies, and
  `=`-named constructors on top of the shared-memory, ownership, clustering, and prior
  Julia-like frontend implementation.

## Approved semantics

- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is an error.
- Existing owned non-primitive locals, parameters, state, and projections cannot cross a domain boundary through a message or reply. Fresh message construction, primitive snapshots, domain references, and queued same-domain transfers remain allowed.
- Primitive values and domain references remain usable after sending.
- Hidden deep copies and copy-on-write are prohibited. Independent duplication is the explicit future `deepCopy()` operation.
- Future ordinary procedures read parameters temporarily by default; `var` permits temporary caller-visible mutation without transfer.
- Immutable sharing, arenas, `ref object` identity, and persistent `revise` versions are deferred because retention and leak behavior is unresolved.

## Implemented features

- Indentation-aware parser for object types, domains, handlers, local `fn` functions, and both `fn main()` and compatibility `proc main()`.
- Julia-like `type Name:` blocks, inferred object fields, expression/block-bodied `fn` functions, pipeline expressions, explicit `message`, and assignment-style `await`.
- Julia-like domain state bindings (`value = initializer`) with optional `value: Type`
  constraints, statically inferred handler reply types, and `=`-named object constructors.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Domain spawning from `main`; one OS thread and serialized lock-backed shared-memory mailbox per unclustered queued domain in generated Rust.
- Generated `Mutex<VecDeque<_>>`/`Condvar` request and reply transport with explicit Rust `Send` assertions and no `std::sync::mpsc` use.
- `--cluster=A,B` static placement for single-instance domain types, with one shared worker and ingress mailbox per cluster.
- Statically selected `_shared` cross-thread calls and `_local` same-cluster calls. Cluster-member capabilities use zero-sized local reference types; local awaits dispatch directly and local one-way calls use a single-threaded queue without synchronization.
- Optional `-Oshared-memory` / `-O` whole-program planning that promotes awaited-only, reply-only domains to direct `Arc<Mutex<DomainState>>` dispatch while generated Rust assertions retain the `Send` boundary.
- One-way asynchronous messages, typed/inferred reply handlers, `reply value`, and assignment-style `await` (with compatible `let`/`var` initializers).
- Domain-owned mutable state, serialized run-to-completion handlers, local `let`/`var`, control flow, `echo`, and bare `return`.
- Ownership checks for direct local assignment, existing owned cross-domain payloads, nested non-primitive projections, domain state, and non-primitive replies.
- Generated Rust compilation with warnings denied in the test suite.

## Partially implemented or unimplemented

- Ownership analysis remains lightweight around arbitrary raw expressions, indirect aliases, and complete control-flow dataflow.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- The future ordinary `proc` parameter model is not implemented; top-level `fn` local functions are implemented. `self.Message(...)` remains queued communication.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.
- The additional awaited-only state-lock optimization is domain-wide and opt-in. It does not yet use profiles or a cost model.
- Cluster configuration is type-wide and currently requires exactly one unconditional `main` spawn for every member.
- Statically visible await cycles among proposed cluster members are rejected; general cycle handling remains unimplemented.

## Known bugs and limitations

- Rust type/ownership errors may still surface when Moss inference lacks enough source information.
- General await expressions, spawning from handlers, cancellation, timeouts, failure propagation, and cycle detection are not implemented.
- The compiler remains a single C++17 source file with a deliberately small type checker.

## Tests run and results

`make check` passed after the inferred state/reply migration. The suite compiles ordinary,
direct-lock optimized, and clustered Rust with `rustc -D warnings`; rejects any generated
`std::sync::mpsc` use; checks local implementations for the expected synchronization
boundary; compares behavior for checkout, object isolation, ignored replies, local and
shared domain-reference messages, FIFO, and non-reentrancy; rejects invalid cluster
layouts, cycles, naked domain calls, invalid awaits, unresolved fields/state, conflicting
reply types, state annotation mismatches, and value-returning `main`; verifies source/
backend annotations; verifies `--no-await-error-handling`; and repeatedly exercises both
the lock-backed mailbox and direct state-lock paths under contention. Dedicated positive
cases cover inferred state, optional state annotations, inferred replies, and `=`
constructors; the former one-way-reply negative fixture is now a positive compatibility
case because reply presence infers request/reply capability.

`make examples` passed, compiling every valid example (including `frontend_syntax.moss`)
with `-Oshared-memory`; the intentional `use_after_transfer.moss` negative example was
skipped. A strict `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build and `sh -n
tests/run.sh` also passed after the migration.

Two local smoke samples of the contention program completed 20 baseline runs in approximately 0.20–0.25 seconds and 20 direct shared-memory runs in approximately 0.02–0.03 seconds. This is evidence that transport elimination works for the intended request/reply shape, not a general performance claim.

## Immediate next tasks

1. Extend cluster placement from one instance per domain type to a static per-spawn identity plan.
2. Benchmark lock-backed mailboxes, direct state locking, and clusters on representative workloads.
3. Implement explicit `deepCopy()` with type checking, deep lowering, and approved cost diagnostics.
4. Expand ownership-boundary analysis to arbitrary aliases and complete control flow.

## Open design questions requiring Kutty's decision

- Exact `deepCopy()` warning wording and fixed-size estimates.
- Syntax and domain-state interaction for ordinary synchronous procedures.
- Future retention-safe designs for immutable sharing, arenas, and persistent versions.
- How future automatic cluster selection should balance locality, blocking awaits, and load distribution.
- Supervision, failure propagation, transactional rollback, and restart semantics remain
  unresolved and were deliberately not changed by this frontend migration.

## Previous 2026-09-11 shared-memory and clustering handoff

### What changed in this session

- Replaced every Rust standard channel with generated lock-backed shared-memory queues and one-shot reply cells.
- Kept the optional awaited-only `Arc<Mutex<DomainState>>` direct optimization for unclustered domains.
- Added repeatable `--cluster=A,B` backend placement. A generated cluster runtime owns one thread, one shared ingress queue, all member states, and a plain local queue.
- Generated distinct `_shared` and `_local` call implementations and selects between them statically. Cluster-member capabilities lower to zero-sized local references. Local awaits are ordinary handler calls; one-way calls retain queued semantics without synchronized operations.
- Added `--no-await-error-handling` as an explicit backend opt-in for supervision-owned failures. It removes per-await `unwrap_or_else` diagnostics and emits unchecked reply extraction; the checked path remains the default until supervision trees exist.
- Added target-specific local flushing before direct awaits to preserve FIFO when an earlier local one-way call targets the same domain.
- Prohibited cross-domain ownership transfer of bound non-primitive data, including nested projections and replies, while permitting fresh message construction and same-domain queued transfer.
- Updated the object pipeline to send a primitive snapshot across domains and added transport, clustering, contention, FIFO, and ownership regressions.

### Decisions Kutty explicitly made

- All Rust message transport must use shared memory with locks and Rust's `Send` boundary; there is no true Rust message-passing fallback.
- Moss must continue to expose and behave as message passing; the backend representation must not change the language.
- Domain-private non-primitive values cannot transfer ownership across domains.
- Configured clusters share one execution thread, and calls among their members remove synchronization through statically selected generated implementations.

### Assumptions Codex made

- `--cluster` is a compiler-side placement input that generates the requested runtime startup call, because runtime branching would conflict with static call selection and Moss must not gain placement syntax.
- Fresh values built directly as payloads are message-owned rather than transferred from a domain. Queued self-messages remain within one ownership domain.
- The initial cluster implementation is intentionally limited to one unconditional spawn per member type.

### Tests actually executed

- `make check` — passed with baseline and shared-memory generated Rust compiled under `rustc -D warnings`.

### Exact commit hash containing the backend work

`cd65d8c` — Backend predecessor before the frontend migration. The syntax-direction
checkpoint is `c130ff2`; the completed frontend checkpoint is the commit that follows.

## 2026-09-05 transfer-semantics handoff (partly superseded)

### What changed in this session

- Made approved transfer semantics authoritative in the design records.
- Renamed user-facing move terminology to transfer terminology.
- Added the earlier source checks for detached message transfers and direct domain-state transfer rejection. The later 2026-09-11 boundary rule now rejects the cross-domain transfer case.
- Updated the earlier message lowering to avoid hidden deep copies; current lowering accepts fresh payloads and rejects existing owned non-primitive bindings at cross-domain boundaries.
- Added positive and negative examples and regression tests.

### Decisions Kutty explicitly made

- Nontrivial assignment transfers ownership; hidden deep copies and COW are prohibited.
- `deepCopy()` is explicit future syntax and is not implemented.
- Detached-local cross-domain transfer was the decision at that time and is superseded by the 2026-09-11 prohibition. Same-domain queued transfer remains allowed.
- Ordinary read-only and `var` procedure parameters do not transfer.
- Immutable sharing, arenas, and `revise` remain deferred due memory-retention and leak concerns.

### Assumptions Codex made

- Existing await/reply and serialized-domain behavior remains approved.
- The lightweight checker is extended only for direct, statically recognizable transfer cases in this increment.

### Tests actually executed

- `make clean && make check` — passed; generated Rust was compiled with `-D warnings`.

### Tests that could not be executed and why

- None.

### Semantic questions still unresolved

- `deepCopy()` warning details and ordinary procedure design, as listed above.

### Exact commit hash containing the work

`0adac33` — Implement approved Moss transfer semantics.
