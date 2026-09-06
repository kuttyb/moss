# Moss current status

Updated: 2026-09-05

## Version and commits

- Compiler version: Moss v0.2
- Repository HEAD reviewed in this session: `c75e861892fac6813821a0de9831d851d4164ed7`
- Latest compiler-behavior commit: `f440856ff59f8ea208d06bae4d11738ccfe880fc`
- Await/reply implementation commit: `4ee1de7e0f33564782e800e84500b945b45268b7`

## Implemented features

- Two-space-indented parser for object types, domains, handlers, state, and `proc main()`.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Domain spawning from `main`.
- One OS thread and one serialized message queue per spawned domain in the Rust backend.
- One-way asynchronous handler messages with value-style payload delivery.
- Domain-owned mutable state and serialized run-to-completion handlers.
- Local `let` and `var`, state and field mutation, `if`/`else`, `while`, `echo`, and bare `return`.
- Typed reply handlers using `on Name(...) -> Type` and `reply value`.
- Await as the complete initializer of a local `let` or `var`.
- Await from `main` and from a handler, ignored replies, self-await rejection, arity checks, and missing-reply runtime diagnostics.
- Non-reentrant await behavior: messages queued for an awaiting domain remain queued until its current handler resumes and completes.
- Generated Rust compilation is exercised with warnings denied by the test suite.

## Partially implemented or provisional features

- The compiler has a lightweight ownership pass for direct local-to-local assignments of types it recognizes as nontrivial. It reports later uses of the source as moved. This is provisional backend-driven behavior, not approved Moss semantics.
- The ownership pass handles the direct `let moved = original` case and some branch merging, but it is not a complete dataflow, alias, or lifetime analysis.
- Reply handlers are required to contain a syntactic `reply`, but the checker does not prove that all paths reply.
- `examples/object_pipeline.moss` demonstrates object flow through queued self-messages. It does not demonstrate ordinary synchronous intra-domain procedures, which do not yet exist.

## Known bugs and limitations

- Nontrivial local assignment semantics are unresolved. The current ownership diagnostic is not an approved source-language rule; README now labels it provisional.
- The provisional ownership checker is intentionally incomplete and may miss indirect moves or behave conservatively around complex shadowing and control flow.
- Rust ownership or type errors can still leak through when the lightweight Moss checker cannot infer enough source information.
- Ordinary synchronous intra-domain procedure declarations and calls are not implemented.
- General await expressions are not implemented; await is limited to a complete local initializer.
- Cross-domain await cycles may deadlock and are not detected.
- Cancellation, timeouts, structured failure propagation, and domain-thread panic propagation are not implemented.
- An awaited reply handler can syntactically contain `reply` while still falling through on some path; this is detected only at runtime.
- Spawning domains inside handlers is not supported.
- The compiler remains a single C++17 source file with a deliberately lightweight type checker.

## Tests run and results

Most recent full command before this status update:

```sh
make check
```

Result: passed.

The suite built the C++17 compiler, checked and generated all positive examples, compiled generated Rust with `-D warnings`, ran exact-output checks, verified await from `main`, sequential awaits, ignored replies, missing-reply failure, all required negative await/reply diagnostics, and non-reentrant ordering over 20 runs. It also verified the currently provisional use-after-move diagnostic.

Focused checks performed in the same session:

- `examples/use_after_move.moss` produced the Moss diagnostic at line 12 and cited the transfer at line 10.
- `examples/object_pipeline.moss`, `examples/checkout.moss`, and `examples/counter.moss` passed Moss checking.
- Generated `object_pipeline.rs` compiled with `rustc -D warnings` and produced the expected output.

## Immediate next tasks

1. Keep the nontrivial local assignment question open; do not change the provisional ownership checker until Kutty revisits it.
2. When the question is reopened, choose the observable assignment rule and then retain, revise, or remove the checker.
3. Add positive and negative tests for the approved assignment rule, including aliasing or copy-observability cases where applicable.
4. Obtain a separate design decision for ordinary synchronous intra-domain procedures before adding them or describing self-messages as a substitute.
5. Commit the reconciled implementation, tests, and `.codex` design records together after those decisions.

## Open design questions requiring Kutty's decision

### Nontrivial local assignment

For the following source, what should assignment mean?

```moss
let moved = original
```

Open choices:

- Ownership transfer: `original` becomes unavailable.
- Eager value copy: both bindings are independently usable.
- Alias: both bindings refer to the same logical object and mutations may be shared.
- Value assignment with compiler-selected copy-on-write or another representation that preserves value behavior without requiring the programmer to choose a storage strategy.

This must be decided from Moss's observable source semantics, not Rust's default assignment behavior.

### Ordinary intra-domain procedures

Should domains support synchronous local procedures distinct from message handlers? If so, their syntax, access to domain state, parameter behavior, mutation visibility, return behavior, and interaction with handler serialization require an explicit decision.

## Handoff summary

### What changed in this session

- Added the three required design and handoff documents.
- Separated approved Moss semantics from current Rust lowering.
- Recorded the await/reply and serialized-domain decisions with rationale and rejected alternatives.
- Marked local ownership transfer and intra-domain procedure semantics as unresolved.

### Decisions Kutty explicitly made

- Moss design context must live in the repository and preserve decision history.
- Semantic changes require explicit approval unless already authoritative in `MOSS_DESIGN.md`.
- Rust backend mechanisms do not implicitly define Moss source semantics.
- The current ownership-transfer implementation is not a settled assignment rule.
- Nontrivial local assignment semantics are intentionally deferred; the current implementation must remain unchanged for now.
- A self-message is queued domain communication and is not automatically a substitute for a synchronous function call.

### Assumptions Codex made

- The await/reply contract in `.codex/MOSS_AWAIT_HANDOVER.md` and commit `4ee1de7e0f33564782e800e84500b945b45268b7` reflects an approved prior decision.
- The serialized run-to-completion domain model described by the handover and existing compiler is approved current behavior.
- The provisional ownership checker should remain in place temporarily, clearly labeled, until Kutty chooses assignment semantics; no further semantic code change was made in this session.

### Tests actually executed

- `make check` - passed.
- Focused Moss checks for `use_after_move`, `object_pipeline`, `checkout`, and `counter` - behaved as described above.
- Generated Rust compile and execution for `object_pipeline` with warnings denied - passed with exact output.

### Tests that could not be executed and why

- None.

### Semantic questions still unresolved

- The observable meaning of nontrivial local assignment.
- The design of ordinary synchronous intra-domain procedures.

### Exact commit hash containing the work

The current compiler experiment is in `f440856ff59f8ea208d06bae4d11738ccfe880fc`; the latest reviewed repository commit is `c75e861892fac6813821a0de9831d851d4164ed7`. The documentation created in this session records the explicit deferral; no implementation reconciliation was performed.
