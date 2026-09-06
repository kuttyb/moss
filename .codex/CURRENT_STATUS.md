# Moss current status

Updated: 2026-09-05

## Version and commits

- Compiler version: Moss v0.2
- Repository was synchronized with `origin/main` before this session; it was already up to date.
- Latest relevant implementation commit: `0adac33`.

## Approved semantics

- Assignment of a uniquely owned nontrivial local transfers ownership; later source use is an error.
- Message sends transfer detached nontrivial locals. Domain state and aliases into it cannot be sent.
- Primitive values and domain references remain usable after sending.
- Hidden deep copies and copy-on-write are prohibited. Independent duplication is the explicit future `deepCopy()` operation.
- Future ordinary procedures read parameters temporarily by default; `var` permits temporary caller-visible mutation without transfer.
- Immutable sharing, arenas, `ref object` identity, and persistent `revise` versions are deferred because retention and leak behavior is unresolved.

## Implemented features

- Two-space-indented parser for object types, domains, handlers, state, and `proc main()`.
- Primitive, object, domain-reference, `seq`, `option`, and `table` types in the implemented slice.
- Domain spawning from `main`; one OS thread and serialized message queue per domain in generated Rust.
- One-way asynchronous messages, typed reply handlers, `reply value`, and `await` as a complete local initializer.
- Domain-owned mutable state, serialized run-to-completion handlers, local `let`/`var`, control flow, `echo`, and bare `return`.
- Lightweight ownership checking for direct local assignment, detached message arguments, and direct domain-state message rejection.
- Generated Rust compilation with warnings denied in the test suite.

## Partially implemented or unimplemented

- Ownership analysis is intentionally lightweight: indirect aliases, complex expressions, and complete control-flow dataflow remain incomplete.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- Ordinary synchronous intra-domain procedures are not implemented; `self.Message(...)` remains queued communication.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.

## Known bugs and limitations

- Rust type/ownership errors may still surface when Moss inference lacks enough source information.
- General await expressions, spawning from handlers, cancellation, timeouts, failure propagation, and cycle detection are not implemented.
- The compiler remains a single C++17 source file with a deliberately small type checker.

## Tests run and results

`make clean && make check` passed. The suite compiled the compiler, checked and generated every positive example, compiled generated Rust with `rustc -D warnings`, ran exact-output checks, exercised await/reply and non-reentrancy, verified primitive and domain-reference message arguments, fresh payload sends, and rejected local transfer, message-transfer use-after-transfer, domain-state message transfer, and Rust ownership-term leakage in Moss diagnostics.

## Immediate next tasks

1. Implement explicit `deepCopy()` with type checking, deep lowering, and approved cost diagnostics.
2. Expand transfer analysis to aliases and complete control flow.
3. Obtain a separate design decision for ordinary synchronous procedures before implementing them.

## Open design questions requiring Kutty's decision

- Exact `deepCopy()` warning wording and fixed-size estimates.
- Syntax and domain-state interaction for ordinary synchronous procedures.
- Future retention-safe designs for immutable sharing, arenas, and persistent versions.

## Handoff summary

### What changed in this session

- Made approved transfer semantics authoritative in the design records.
- Renamed user-facing move terminology to transfer terminology.
- Added source checks for detached message transfers and direct domain-state transfer rejection.
- Updated message lowering to avoid hidden deep copies for transferred nontrivial payloads.
- Added positive and negative examples and regression tests.

### Decisions Kutty explicitly made

- Nontrivial assignment transfers ownership; hidden deep copies and COW are prohibited.
- `deepCopy()` is explicit future syntax and is not implemented.
- Detached locals may transfer through messages; domain state may not.
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
