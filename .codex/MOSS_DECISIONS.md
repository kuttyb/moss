# Moss design decisions

This is an append-only decision history. New decisions and revisions are added as dated entries; earlier entries remain present even when superseded.

## 2026-09-05 - Serialized domain message execution

**Question:** What concurrency and ordering guarantees should domain message handlers expose?

**Final decision:** A domain owns its mutable state and processes one queued message handler at a time. Handlers are run-to-completion and non-reentrant. Messages from one sender to one receiving domain preserve FIFO order. `self.Message(...)` is a queued message, not a synchronous call.

**Reason:** Serialized ownership makes domain state race-free at the language level and gives programmers a predictable ordering model without locks in ordinary Moss code.

**Alternatives considered and rejected:** Shared mutable state with programmer-managed locking; reentrant processing of another queued message while a handler is suspended; treating a self-message as an ordinary procedure call.

**Programmer-facing consequences:** Domain state is never concurrently mutated by two handlers. A self-message runs only after the current handler completes. Queued messages wait while a handler is blocked in an await.

**Supersedes:** No earlier recorded rule.

## 2026-09-05 - Typed request/reply and non-reentrant await for v0.2

**Question:** How should a handler return a value to a caller, and what should happen to the caller's domain while it waits?

**Final decision:** A handler may declare `-> ReplyType` and use `reply value`. For v0.2, `await` is permitted only as the complete initializer of a local `let` or `var`. The awaiting domain stays logically occupied and does not process other messages. Self-await is a static error. A reply handler may be called without await, in which case the reply is ignored. Cross-domain await-cycle detection, cancellation, timeouts, and general failure propagation are out of scope.

**Reason:** This adds direct request/reply syntax while preserving the existing serialized domain model and avoiding reentrant event-loop behavior.

**Alternatives considered and rejected:** A separate response-message language mechanism; reentrant message processing during await; unrestricted nested await expressions in the first milestone; allowing self-await; requiring every reply-capable send to be awaited.

**Programmer-facing consequences:** Awaited results have the declared reply type. A domain does not become reentrant at an await. Missing replies produce a clear runtime failure. Cross-domain await cycles may deadlock.

**Supersedes:** The v0.1 restriction that cross-domain messages have no direct request/reply form.

## 2026-09-05 - Design authority and backend separation

**Question:** How should Moss design intent be preserved, and may Rust implementation constraints define Moss semantics implicitly?

**Final decision:** `.codex/MOSS_DESIGN.md` is the current authoritative semantics document, this file is the append-only decision history, and `.codex/CURRENT_STATUS.md` is the implementation handoff. A semantic change requires Kutty's explicit approval unless already documented as authoritative. Rust moves, borrows, clones, lifetimes, channels, locks, arenas, and reference counting are backend mechanisms and do not become Moss rules merely because the backend uses them.

**Reason:** Moss should remain easy to use without leaking backend bookkeeping into source code, and future engineers or agents need the rationale behind behavior rather than only the emitted implementation.

**Alternatives considered and rejected:** Inferring source semantics from the current Rust lowering; recording only the latest outcome while silently rewriting earlier decisions; implementing tentative ideas before approval.

**Programmer-facing consequences:** New semantic behavior is discussed and documented before implementation. Tests demonstrate approved observable rules. Tentative experiments are labeled as open questions rather than presented as language guarantees.

**Supersedes:** The prior informal process in which implementation behavior and README descriptions could be treated as design authority without a separate recorded decision.

## 2026-09-05 - Keep nontrivial assignment semantics open

**Question:** What should `let moved = original` mean when the value is a nontrivial local object?

**Final decision:** Defer the source-language decision. Keep the current ownership-transfer checker implementation unchanged as a provisional experiment, but do not describe move, copy, alias, or copy-on-write behavior as authoritative Moss semantics and do not extend that implementation until Kutty revisits the question.

**Reason:** Moss should not inherit Rust's move behavior merely because Rust is the current backend. The source rule must be chosen from observable Moss intent and usability.

**Alternatives considered and rejected for now:** Choosing ownership transfer, eager copy, shared aliasing, or value assignment with compiler-selected copy-on-write before the language tradeoffs are explicitly settled.

**Programmer-facing consequences:** The current `use_after_move.moss` diagnostic remains a documented provisional experiment. New code must not rely on it as a stable Moss rule. No source or backend behavior changes as a result of this decision.

**Supersedes:** No earlier decision; this entry confirms and defers the open question recorded below.

## 2026-09-05 - Explicit ownership transfer and deferred duplication/sharing

**Question:** How should Moss assign nontrivial values, pass detached values in messages, and expose duplication, procedure parameters, and sharing?

**Final decision:** Assignment of a nontrivial uniquely owned local transfers ownership. A later source use is a compile-time error. Moss must not silently deep-copy or use copy-on-write; independent duplication is the explicit future `deepCopy()` operation. A detached local may transfer through a message, but domain state and aliases into domain state may never transfer. Domain references remain usable after being passed. Future ordinary procedures read parameters temporarily by default and use `var` parameters for temporary caller-visible mutation without transfer. Immutable sharing, arenas, `ref object` identity, and persistent versions or `revise` are deferred.

**Reason:** Moss prioritizes predictable performance and must not hide potentially large copies. Transfer keeps ownership costs visible and prevents aliases into mutable domain state. Sharing and retention-heavy facilities need a reclamation and memory-leak model first.

**Alternatives considered and rejected:** Implicit deep copies, copy-on-write, Rust-style borrow syntax, reference-counted source semantics, arena handles, and implementing persistent structural sharing or `revise` before retention behavior is understood.

**Programmer-facing consequences:** Diagnostics say “transferred value” rather than Rust “moved value.” `worker.Process(payload)` makes a detached nontrivial `payload` unavailable afterward; `worker.Process(dataset)` is an error when `dataset` is domain state. `worker.Process(dataset.deepCopy())` is the intended future explicit duplication form, but `deepCopy()` is not implemented in v0.2. A normal future procedure call will not consume a read-only argument, and `var` will request temporary mutation without transfer.

**Supersedes:** The 2026-09-05 deferral of nontrivial assignment semantics. It does not supersede the earlier serialized-domain or await/reply decisions.

## Open questions and experiments - not decisions

### Ordinary intra-domain procedures

The `object_pipeline.moss` experiment passes an object through `self.Message(...)` handlers because ordinary domain-local procedures are not implemented. This demonstrates queued message flow only. It does not decide that self-messages substitute for synchronous intra-domain procedures. The declaration, call, mutation, and return semantics of ordinary domain-local procedures remain open.
