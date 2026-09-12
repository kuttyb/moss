# Moss implementation checkpoint

Prepared 2026-09-11 for handoff to another engineer or ChatGPT session.

## Repository state

- Branch: `main`.
- HEAD while this checkpoint was prepared: `cd65d8cd2dfcd2e3152b7366069b0f8cb3556c24` (`Latest change`).
- Previous known checkpoint/base: `3d4c772f3441b93c3d34bf7ee1c7aceacc64aab8` (`Refresh Moss handoff status`).
- Accumulated implementation commits after that base: `5d8c086` (shared-memory transport and clustering), `34fba80` (generated Rust source/backend annotations), `5da34cd` (example build targets), and `cd65d8c` (unchecked-await backend flag).
- `origin` is `git@github.com:kuttyb/moss.git`; it pointed at `cd65d8c` before the checkpoint commit.
- Build outputs under `build/` and generated `examples/*.rs` are ignored. The checkpoint commit must contain source, tests, documentation, and this file only.

## What the accumulated work accomplishes

Moss remains a source-level message-passing language. The C++ compiler checks Moss programs, plans backend placement and transport statically, and emits standalone Rust. Rust has no `std::sync::mpsc` transport: cross-thread Moss messages and replies use generated lock-backed shared-memory queues and one-shot reply cells. Eligible awaited request/reply domains may use a direct `Arc<Mutex<DomainState>>` lowering. Explicitly clustered domain types share one worker thread; intra-cluster calls are statically emitted as direct local calls and external traffic uses the shared ingress queue.

The latest backend flag, `--no-await-error-handling`, is opt-in. It removes per-await `unwrap_or_else` diagnostics and emits unchecked extraction for a future supervision-tree runtime. The checked diagnostic path remains the default and is still the behavior described by ordinary Moss semantics.

## Language and semantic changes

- Existing source syntax remains the v0.2 slice: object types, domains, handlers, state, `proc main`, one-way sends, typed replies, `reply value`, and await as a complete local initializer.
- A domain owns mutable state and executes one handler at a time to completion; handlers are non-reentrant and messages from one sender to one receiving domain retain FIFO order.
- A reply-capable handler must contain a syntactic `reply`; the checker does not prove every control-flow path replies. The default runtime reports a missing reply to an awaiter.
- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is rejected. Hidden deep copies and copy-on-write are not inserted.
- An existing owned non-primitive local, parameter, state value, or nested projection cannot cross a domain boundary in a message or reply. This also applies from `main` and between clustered domains.
- A fresh value constructed directly as a message/reply payload may cross. Primitive snapshots and domain-reference capabilities may cross; passing a domain reference does not transfer its mutable state.
- A queued self-message remains within one domain and may transfer a local. Ordinary synchronous intra-domain procedures and `deepCopy()` are not implemented.
- `--cluster=A,B` and `--no-await-error-handling` are compiler/backend flags, not Moss syntax and not new language semantics.

## Compiler, frontend, type, and ownership analysis

- `Parser` preserves source line numbers in `Field`, `Stmt`, `Handler`, `Domain`, `ObjectType`, and `MainProc` records. `lex_lines` strips Moss comments while retaining line diagnostics.
- `Checker::check_stmts` validates receivers, handlers, arity, reply contexts/types, await restrictions, spawning restrictions, and source-level control flow.
- `Checker::check_ownership`, `check_ownership_block`, `require_available`, and `require_cross_domain_value` track direct assignment transfer and reject bound non-primitive values at cross-domain send/await/reply boundaries. The checker recursively handles object constructors, projections, state aliases, and nested values within its lightweight analysis.
- `transfer_type` keeps domain references as capabilities instead of treating them as moved domain-private data.
- `OptimizationPlan` records direct shared-memory candidates and requested clusters. `MessageTransportOptimizer::run` scans the visible call graph, rejects unsupported cluster layouts and statically visible intra-cluster await cycles, and only promotes a domain when all handlers reply, all known calls are awaited, and the supported sendability analysis succeeds.
- `sendable_type` and `domain_is_direct_candidate` conservatively gate Rust `Send` crossings. Incomplete/raw/indirect call analysis disables direct promotion rather than guessing.

## Rust lowering, backend, and runtime

- `Generator::gen_shared_channel` emits `MossChannel<T>` with `Arc`, `Mutex`, `VecDeque`, and `Condvar`, plus sender/receiver close and drop handling. This is the shared-memory implementation of Moss message semantics, not `std::sync::mpsc`.
- Cross-thread request/reply calls use a lock-backed one-shot channel. Ignored replies allocate the same cell and drop the receiver.
- `Generator::gen_domain` emits the message/mailbox version: a Rust message enum, `_shared` enqueue methods, a worker thread, and serialized handler dispatch.
- `Generator::gen_direct_domain` emits the direct shared-memory version: `Arc<Mutex<DomainState>>`, lock-taking `_shared` methods, direct handler execution, and `Send` assertions for state, parameters, and replies.
- `Generator::gen_cluster` emits one shared ingress enum/mailbox, zero-sized member `LocalRef` capabilities, a `RefCell` runtime for member states, a plain local `VecDeque`, direct `_local` handler methods, local queue dispatch, and target-specific FIFO flushing before local awaits.
- Cluster members have no mutex, atomic, condition variable, or thread-safe channel operation on the local path. Calls leaving a cluster use the shared path. The generated selection between `_shared` and `_local` is static; there is no runtime placement test.
- `Generator::gen_block` annotates generated user-code regions with `// Moss line N: ...` and labels the selected `MESSAGE/MAILBOX`, `SHARED-MEMORY DIRECT`, or `CLUSTER-LOCAL` lowering. Declarations, handlers, fields, `spawn`, sends, awaits, replies, control flow, and `main` receive source correlation comments where applicable.
- Generated Rust includes a completion tracker so `main` can wait for enqueued work. General failure propagation, cancellation, timeout, and supervision are not implemented.
- With `--no-await-error-handling`, `gen_block` uses `unsafe { ...unwrap_unchecked() }` for direct/local `Option` replies and mailbox `Result` replies. This removes the per-await check/diagnostic closure but assumes successful reply delivery.

## Optimization and placement behavior

- `-Oshared-memory`, `-O`, and `--optimize-shared-memory` enable the awaited-only direct state-lock promotion. `-O0` disables that promotion while retaining lock-backed shared-memory mailboxes.
- Direct promotion is whole-program, domain-wide, conservative, and currently has no profile or cost model. A one-way handler, ignored reply, unresolved/indirect call, non-sendable state/payload, or asynchronous caller keeps the mailbox/thread implementation.
- `--cluster=A,B` places the named domain types together on one generated worker. Each member must be spawned exactly once and unconditionally at main scope. A proposed cluster with a statically visible await cycle is rejected.
- `make examples` and `make examples-optimized` generate and compile all valid files in `examples/` under `build/examples/optimized`; the intentional `use_after_transfer.moss` negative example is skipped.

## Tests and examples added or changed

- Added `examples/shared_memory.moss`, which demonstrates Moss `Counter` messages and awaits, default lock-backed transport, and optional direct `Arc<Mutex>` promotion.
- Updated `examples/object_pipeline.moss` so cross-domain traffic sends a primitive snapshot rather than an existing owned object.
- Added `tests/shared_memory_contention.moss` for mailbox/direct-lock contention and repeated ordering checks.
- Added `tests/fresh_reply_payload.moss` and negative ownership cases: `await_object_transfer.moss`, `object_reply_transfer.moss`, `nested_object_transfer.moss`, and `string_transfer.moss`.
- Added cluster cases: `cluster_mixed_fifo.moss`, `cluster_domain_ref.moss`, `cluster_external_ref.moss`, and `cluster_await_cycle.moss`.
- `tests/run.sh` rejects generated `std::sync::mpsc`, compiles generated Rust with `rustc -D warnings`, checks backend comments and static local lowering, exercises direct/shared/cluster behavior, validates FIFO and non-reentrancy, rejects invalid ownership and cluster configurations, and verifies `--no-await-error-handling` removes `unwrap_or_else` while preserving successful execution.
- `Makefile` now has `examples`/`examples-optimized` targets and cleans their ignored output.

## Invariants that must remain true

- Moss source must not expose Rust locks, channels, `Send`, `Arc`, `Rc`, `RefCell`, atomics, cluster placement, or supervision bookkeeping.
- Generated Rust must not use `std::sync::mpsc` or a true OS/IPC message transport. Cross-thread Moss traffic remains shared-memory lock/condition-variable transport or a statically proven direct state-lock call.
- Source-level domain serialization, run-to-completion, non-reentrancy, FIFO, await blocking, ignored-reply behavior, and self-await rejection must not change under backend optimization.
- No existing domain-private non-primitive ownership may cross a domain boundary. Fresh payload construction, primitives, and capability references retain their documented rules.
- Direct promotion may not apply to asynchronous/ignored-reply traffic or a clustered domain. Cluster-local dispatch must remain statically selected and free of synchronization primitives on the local path.
- `--no-await-error-handling` must remain explicit and backend-only; checked reply diagnostics remain the default until supervision guarantees are implemented.
- Generated source-line comments are diagnostic documentation and must not be mistaken for Moss semantics or used to introduce runtime checks.

## Deliberately undecided or deferred semantics

- Supervision trees, restart/stop policies, failure propagation, cancellation, timeouts, and mailbox cleanup policy are future design work. The unchecked-await flag anticipates this runtime but does not define it.
- Whether the unchecked-await mode should eventually be safe through static total-reply proofs, a supervised reply primitive, or a different runtime contract remains open.
- Ordinary synchronous intra-domain procedure syntax and parameter/alias behavior are deferred; `self.Message(...)` remains queued.
- `deepCopy()` syntax, cost warnings, immutable sharing, arenas, `ref object` identity, persistent versions, and copy-on-write are deferred.
- General automatic cluster selection, multiple instances of one domain type, per-spawn identity, load balancing, and cluster lifecycle are deferred.

## Known bugs, limitations, hacks, and incomplete implementations

- The ownership checker is intentionally lightweight around arbitrary raw expressions, indirect aliases, and complete control-flow dataflow. Rust may still report type/ownership errors when Moss inference lacks information.
- Reply completeness is syntactic only. A fallthrough reply handler is valid to check and fails at runtime by default; using `--no-await-error-handling` with such a program can invoke undefined behavior through `unwrap_unchecked`.
- The unchecked-await mode also assumes reply channels remain valid if a worker panics or exits. Current v0.2 has no supervision or general failure propagation, so that flag is not a safe production mode yet.
- `MessageTransportOptimizer` is conservative and only understands statically visible calls. It does not use profiles or a cost model.
- Cluster placement is type-wide and requires one unconditional main-scope spawn per member. General await cycles outside proposed clusters can still deadlock.
- The generator emits shared channel/tracker helper definitions globally even when a particular program's optimized paths do not use every helper; unused generated items are allowed rather than removed.
- `catch_unwind`/failure-tracker scaffolding exists in generated support code, but workers do not yet provide complete supervision or failure propagation semantics.
- The compiler remains a single C++17 source file and implements only the documented v0.2 language slice.

## Design/documentation discrepancies called out explicitly

- `.codex/MOSS_DESIGN.md` describes missing replies as a runtime failure. That remains the default source-observable behavior. The implemented `--no-await-error-handling` flag is an explicit backend-only exception that bypasses the diagnostic and assumes a future supervision runtime; it must not be read as a change to Moss's normal semantics.
- `.codex/MOSS_DECISIONS.md` contains earlier historical alternatives mentioning MPSC and detached cross-domain transfer. Those entries remain append-only history; the later decisions supersede them. Current implementation uses generated lock-backed queues and rejects existing owned non-primitive cross-domain values.
- `.codex/MOSS_AWAIT_HANDOVER.md` is historical. Its old standard-channel lowering instructions are superseded by the current shared-memory transport decision.
- The old status handoff named `3d4c772` as the latest base and called the work uncommitted; `.codex/CURRENT_STATUS.md` has been updated to identify `cd65d8c` as the accumulated implementation tip before this checkpoint.

## Exact files and major functions for future work

- `src/moss.cpp`: `Parser`, `lex_lines`, `Checker::check_stmts`, `Checker::check_ownership`, `Checker::require_cross_domain_value`, `OptimizationPlan`, `MessageTransportOptimizer::run`, `validate_clusters`, `sendable_type`, `domain_is_direct_candidate`, `scan_calls`, `Generator::generate`, `gen_shared_channel`, `gen_domain`, `gen_direct_domain`, `gen_cluster`, `gen_block`, and CLI `main`/`usage`.
- `.codex/MOSS_DESIGN.md`: authoritative settled source semantics and current backend description.
- `.codex/MOSS_DECISIONS.md`: append-only rationale, including the shared-memory/clustering and supervision-owned await extraction decisions.
- `.codex/CURRENT_STATUS.md`: concise implementation handoff; historical `.codex/MOSS_AWAIT_HANDOVER.md` is not current authority.
- `tests/run.sh`: end-to-end generated-Rust, ownership, transport, optimization, cluster, and failure-mode regression harness.
- `Makefile`: compiler, test, clean, and all-valid-example optimized build targets.
- `examples/shared_memory.moss`, `examples/checkout.moss`, and `examples/object_pipeline.moss`: primary backend demonstrations.

## Commands run for this checkpoint

The following commands were run during checkpoint preparation; results are recorded exactly:

- `make clean` — passed; removed ignored compiler, generated Rust, binary, test, and example-build outputs.
- `make check` — passed: all baseline, shared-memory, direct-lock, cluster, ownership, FIFO, non-reentrancy, fallthrough, annotation, and unchecked-await cases reported `all Moss v0.2 tests passed`.
- `make examples-optimized` and `make examples` — both passed: checkout, counter, object pipeline, and shared-memory examples generated and compiled; `use_after_transfer.moss` was explicitly skipped as intentional negative input.
- `./moss -Oshared-memory --no-await-error-handling examples/shared_memory.moss -o /tmp/moss-checkpoint-direct.rs`, `rustc -D warnings /tmp/moss-checkpoint-direct.rs -o /tmp/moss-checkpoint-direct`, `/tmp/moss-checkpoint-direct`, and the check that the generated file contains `unwrap_unchecked` but no `unwrap_or_else` — passed; output `shared total: 10 42`.
- `./moss --no-await-error-handling examples/shared_memory.moss -o /tmp/moss-checkpoint-mailbox.rs`, `rustc -D warnings /tmp/moss-checkpoint-mailbox.rs -o /tmp/moss-checkpoint-mailbox`, `/tmp/moss-checkpoint-mailbox` — passed; output `shared total: 10 42`; mailbox awaits used `unwrap_unchecked`.
- `./moss --no-await-error-handling --cluster=Counter,Client examples/shared_memory.moss -o /tmp/moss-checkpoint-cluster.rs`, `rustc -D warnings /tmp/moss-checkpoint-cluster.rs -o /tmp/moss-checkpoint-cluster`, `/tmp/moss-checkpoint-cluster` — passed; output `shared total: 10 42`; local awaits used `unwrap_unchecked`.
- `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o /tmp/moss-werror-checkpoint` — passed.
- `sh -n tests/run.sh`, `git diff --check`, and `git diff --cached --check` — passed.
- No command in the final validation pass failed. Negative tests intentionally exercised compiler rejection and passed by detecting the expected diagnostics.

## Recommended next implementation steps

These are recommendations only; they are not part of the completed checkpoint:

1. Design and implement supervision trees before making unchecked await extraction a normal mode. Define worker failure reporting, restart strategy, pending-reply resolution, mailbox cleanup, and domain lifecycle.
2. Replace `unwrap_unchecked` with a supervision-aware reply primitive or prove total reply delivery statically before allowing branch-free extraction in production builds.
3. Add focused code-generation tests for helper elision and measure optimized Rust (`rustc -O`) rather than the warning-only smoke binaries.
4. Extend cluster planning to multiple domain instances and per-spawn identities, then evaluate automatic placement and load balancing.
5. Expand ownership and call-graph analysis to complete control flow, aliases, and richer expressions without weakening domain isolation.
6. Revisit `deepCopy()`, ordinary procedures, retention-safe sharing, arenas, and persistent values with the Moss designers before implementing source syntax.
