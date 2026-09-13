# Moss current status

Updated: 2026-09-13

## Version and commits

- Compiler version: Moss v0.2
- Static duck-typed methods and named-trait specialization are complete on top of
  `df73771` (`Complete concrete method semantics`). This checkpoint closes the initial
  static method/trait foundation without adding runtime dispatch.
- The showcase/documentation checkpoint is built on `eecac49` and demonstrates the
  stabilized static duck-typing, named-trait, collection, pipeline, and domain syntax.
  It adds no ownership, borrow, move, optimizer, or runtime-dispatch semantics.
- Phase 2 ownership/effect safety is present in the current history (`c5396c6` and
  `58772f3`). Phase 2.5 is frozen and complete on top of `d76449f`. It adds
  backend-only batching, explicit domain-lowering plans, direct lock regions,
  RwLock specialization, and whole-domain atomics without changing Moss source
  semantics. The freeze checkpoint closes the final `-O0`/atomic equivalence gap
  by defining and explicitly lowering wrapping `i64` arithmetic.

## Approved semantics

- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is an error.
- `message`, awaited request payloads, and `reply` are explicit value-copy boundaries. Existing non-primitive locals, parameters, state, and projections may cross while the sender retains an independent value.
- Primitive values and domain references remain usable after sending.
- Hidden deep copies and copy-on-write are prohibited. Independent duplication is the explicit future `deepCopy()` operation.
- Future ordinary procedures read parameters temporarily by default; `var` permits temporary caller-visible mutation without transfer.
- Immutable sharing, arenas, `ref object` identity, and persistent `revise` versions are deferred because retention and leak behavior is unresolved.
- Moss `Int` is currently signed 64-bit two's-complement. Overflowing integer
  arithmetic wraps modulo 2^64 in every backend; Rust overflow-check settings are
  not observable Moss semantics.

## Implemented features

- The compiler now has extracted `src/ast.hpp`, `src/constraints.hpp`, and
  `src/diagnostics.hpp` modules. `moss.cpp` still contains the parser, checker,
  ownership, optimizer, and Rust generator; the extraction is an incremental
  architectural checkpoint rather than a completed module split.
- Type declarations retain method bodies as children of their method nodes in the
  AST, including nested control-flow blocks.
- Concrete methods are checked with receiver fields and typed parameters in scope;
  result types are inferred across return paths and lowered through the ordinary
  statement generator to executable Rust inherent methods. Same-receiver calls,
  locals, conditionals, loops, and early returns are supported.
- Concrete method parameters must have resolved static types; unresolved parameters
  are compile errors rather than defaulting to an integer backend type.
- Untyped-function operation metadata now flows through structured `Constraint`
  records; the former `generic_ops` field has been removed.
- `ConstraintKind::Method` records the receiver relationship, method name, arity,
  argument relationships, and method-result relationships. Every concrete
  duck-typed call site is checked by the same resolver used for direct concrete method
  calls, including distinct diagnostics for missing methods, arity mismatches,
  incompatible arguments, ambiguity, and incompatible results.
- Named traits are structural compile-time contracts. Conformance resolves every
  declared method against the concrete type's inherent methods and checks annotated
  parameter and result types; there is no separate trait dispatch path.
- Functions with duck-typed method requirements or trait-typed parameters are
  specialized into concrete function instances discovered during checking. Generated
  calls target those instances directly, so multiple conforming object types execute
  through ordinary inherent calls without `dyn Trait`, vtables, runtime method search,
  implicit `Any`, or source-level generic type parameters.
- The Rust emitter lowers the existing `sum` builtin for inferred `Vector`/`seq`
  element types, including the concrete result annotation needed by strict Rust.

- Indentation-aware parser for object types, domains, handlers, local `fn` functions, and both `fn main()` and compatibility `proc main()`.
- Julia-like `type Name:` blocks, inferred object fields, expression/block-bodied `fn` functions, pipeline expressions, explicit `message`, and assignment-style `await`.
- Julia-like domain state bindings (`value = initializer`) with optional `value: Type`
  constraints, statically inferred handler reply types, and `=`-named object constructors.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Domain spawning from `main`; one OS thread and serialized lock-backed shared-memory mailbox per unclustered queued domain in generated Rust.
- Generated `Mutex<VecDeque<_>>`/`Condvar` request and reply transport with explicit Rust `Send` assertions and no `std::sync::mpsc` use.
- `--cluster=A,B` static placement for single-instance domain types, with one shared worker and ingress mailbox per cluster.
- Statically selected `_shared` cross-thread calls and `_local` same-cluster calls. Cluster-member capabilities use zero-sized local reference types; local awaits dispatch directly and local one-way calls use a single-threaded queue without synchronization.
- `-Oshared-memory` / `-O` builds an explicit whole-program plan with `Mailbox`,
  `DirectMutex`, `DirectRwLock`, `DirectAtomic`, and `ClusterLocal` domain
  classifications plus separate batched-send and coalesced-lock region facts.
- Adjacent proven-total, side-effect-free asynchronous sends to the same receiver
  lower to one `send_batch` queue acquisition. `MossTracker::begin_n/end_n`
  coalesces completion accounting and restores the full count after an all-or-none
  enqueue failure.
- Direct lock-backed handlers have separate `_locked` implementations and `_shared`
  wrappers. A single unconditional `main` spawn with no alias, escape, return, nested
  capability, or alternate caller can form a one-guard consecutive-await region.
- Direct domains with useful pure state readers use `Arc<RwLock<State>>`; READ handlers
  use shared guards, while WRITE handlers use exclusive guards. External effects,
  uncertain analysis, stateless handlers, and effectively write-only domains retain
  `Mutex`.
- Entirely compatible integer/boolean domains lower to `AtomicI64`/`AtomicBool` with
  `Ordering::SeqCst`. Loads, stores, add/subtract, boolean toggle, and scalar swap are
  supported. Both one-way and awaited calls execute directly, and a fully atomic
  program emits no mailbox, condition variable, mutex, or worker thread.
- Ordinary integer `+`, `-`, `*`, `/`, integer `sum`, and generated state-update
  equivalents lower through explicit `i64` wrapping operations. Atomic fetch-add/
  fetch-sub reply reconstruction uses the same rule, keeping optimized execution
  equivalent to the mailbox reference at `i64::MIN` and `i64::MAX`.
- One-way asynchronous messages, typed/inferred reply handlers, `reply value`, and assignment-style `await` (with compatible `let`/`var` initializers).
- Domain-owned mutable state, serialized run-to-completion handlers, local `let`/`var`, control flow, `echo`, and bare `return`.
- Ownership checks for direct local assignment, existing owned cross-domain payloads, nested non-primitive projections, domain state, and non-primitive replies.
- Whole-program await-dependency checking rejects every possible cycle, including dependencies reached through ordinary local functions. Asynchronous sends do not create await edges.
- Direct and mutual recursion are rejected. Compiler-internal READ/WRITE/CONSUME summaries drive local call lowering, and conflicting aliases at a call site are Moss compile-time errors.
- Copyable field projections retain a read effect; moving a nontrivial field consumes its containing value. Consuming method receivers lower by value rather than as shared receiver references.
- Generated Rust compilation with warnings denied in the test suite.
- Executable showcases cover method-based duck typing, two concrete named-trait
  implementations, inferred Vector/Map/Queue use, the future dataflow pipeline shape,
  and a small static job-scheduler application with domains, messages, and awaits.

## Partially implemented or unimplemented

- Ownership analysis remains conservative around arbitrary raw expressions and indirect aliases outside the statically represented projection/call slice.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- The future ordinary `proc` parameter model is not implemented; top-level `fn` local functions are implemented. `self.Message(...)` remains queued communication.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.
- Backend choices use deliberately small whole-program heuristics rather than profiles
  or a cost model. `-O0` remains the unoptimized lock-backed mailbox reference.
- Cluster configuration is type-wide and currently requires exactly one unconditional `main` spawn for every member.
- Await-cycle checking is global and placement-independent; cluster planning relies on the language-level result.

## Known bugs and limitations

- Batching currently accepts mailbox payload forms proven total without user calls.
  Broader interprocedural purity proofs are future optimizer work; ignored replies use
  their ordinary generated one-shot channels inside the batch.
- Lock coalescing is intentionally limited to adjacent awaits in `main` and a strict
  single-spawn/no-escape/no-other-caller proof. General region ownership and
  interprocedural exclusivity are not implemented.
- Atomic lowering intentionally excludes floats, CAS loops, arbitrary expressions,
  multi-action handlers, and cross-field invariants. One incompatible handler falls
  the entire domain back to a coherent lock/mailbox representation; there is no hybrid
  atomic/mailbox domain.

- Static specialization currently covers method-constrained and trait-typed local
  functions in the implemented expression/call slice. Associated types, trait
  inheritance, default trait methods, runtime trait objects, and source-level generic
  declarations remain intentionally unsupported.

- Rust type errors may still surface when Moss inference lacks enough source information; normal Phase 2 ownership and conflicting-call-access errors are diagnosed by Moss.
- General await expressions, spawning from handlers, cancellation, timeouts, and failure propagation are not implemented.
- The compiler remains a single C++17 source file with a deliberately small type checker.

## Tests run and results

`make check` passes for the frozen Phase 2.5 checkpoint. The suite compiles ordinary,
Mutex/RwLock/atomic optimized, batched, coalesced, and clustered Rust with
`rustc -D warnings`; rejects any generated
`std::sync::mpsc` use; checks local implementations for the expected synchronization
boundary; compares behavior for checkout, object isolation, ignored replies, local and
shared domain-reference messages, FIFO, and non-reentrancy; rejects invalid cluster
layouts, cycles, naked domain calls, invalid awaits, unresolved fields/state, conflicting
reply types, state annotation mismatches, and value-returning `main`; verifies source/
backend annotations; verifies `--no-await-error-handling`; and repeatedly exercises both
the lock-backed mailbox and direct state-lock paths under contention. Dedicated positive
cases cover inferred state, optional state annotations, inferred replies, and `=`
constructors; the former one-way-reply negative fixture is now a positive compatibility
case because reply presence infers request/reply capability. Dedicated method/trait
cases execute one duck-typed function and one trait-typed function with two distinct
user-defined types, assert that two concrete specializations are emitted, and reject
runtime Rust trait machinery. Negative coverage includes missing methods, wrong arity,
incompatible method arguments, missing trait methods, incompatible trait signatures,
conflicting method-result expectations, unresolved collection element types,
heterogeneous collections, and naked cross-domain calls. The five showcase programs
also execute under the regression harness: static duck typing, named traits, inferred
collections and methods, the `map |> filter |> map |> sum` dataflow shape, and the
domain-backed mini application.

Phase 2.5 regressions additionally prove same-receiver batching and sender order,
different-receiver/effect barriers, batched tracker accounting/rollback, exclusive
one-guard regions, multi-caller rejection, shared READ versus exclusive WRITE guards,
integer and boolean atomics, mixed awaited/one-way mailbox elimination, multi-field
SeqCst use, atomic request/reply copy boundaries, locking fallbacks for invariants and
external effects, fully atomic runtime removal, atomic and read-heavy contention, and
representative `-O0`/`-O` output parity.

The overflow differential additionally runs increment-at-`i64::MAX`, decrement-at-
`i64::MIN`, positive addition overflow, negative-direction subtraction overflow,
and awaited new-value replies through both `-O0` mailboxes and `DirectAtomic`.
Both generated programs are compiled with `rustc -C overflow-checks=yes -D warnings`
and must match. It also checks ordinary wrapping multiplication and division,
integer `sum`, and a concretely specialized duck-typed integer operation.

`make examples` passed, compiling every valid example (including the executable static
trait and dataflow showcases in `examples/traits.moss` and
`examples/functional_dataflow.moss`) with `-Oshared-memory`; the intentional
`use_after_transfer.moss` negative example was skipped. A strict
`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build and `sh -n tests/run.sh` also
passed for this checkpoint.

Two local smoke samples of the contention program completed 20 baseline runs in approximately 0.20–0.25 seconds and 20 direct shared-memory runs in approximately 0.02–0.03 seconds. This is evidence that transport elimination works for the intended request/reply shape, not a general performance claim.

## Immediate next tasks

Phase 2.5 is frozen; none of the following work belongs in this checkpoint.

1. Benchmark mailboxes, batching, Mutex/RwLock, atomics, and clusters on
   representative workloads.
2. Extend cluster placement from one instance per domain type to a static per-spawn
   identity plan.
3. Implement explicit `deepCopy()` with type checking, deep lowering, and approved cost diagnostics.
4. Expand ownership analysis beyond the Phase 2 call/projection/control-flow slice when future reference facilities are designed.

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
