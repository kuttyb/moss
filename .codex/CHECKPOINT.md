# Moss implementation checkpoint

Prepared 2026-09-11 for reconstruction from the public repository.

## Repository state

- Branch: `main`.
- HEAD before the final frontend checkpoint commit: `c130ff2`
  (`Checkpoint language direction before syntax migration`). This is the clean
  pre-migration compiler checkpoint plus the requested syntax-direction document.
  The exact SHA of the commit containing this file is reported by `git rev-parse
  HEAD` after the final commit and in the handoff response.
- Previous known compiler checkpoint/base: `acebe2c1adad1d04d9d3f0e1ac506084417af741`.
  The shared-memory, generated-comment, example-build, clustering, and unchecked-await
  backend work was already accumulated through `cd65d8c` before this migration.
- `origin` is `git@github.com:kuttyb/moss.git`. The language-direction checkpoint
  `c130ff2` was pushed before source migration. The frontend checkpoint is ready to
  push after it is committed.
- Build outputs under `build/` and generated binaries/Rust files are ignored. Only
  source, tests, examples, documentation, and checkpoint records belong in the commit.

## Session outcome

The C++17 frontend now accepts an incremental Julia-like syntax while retaining the
existing Moss domain/message semantics and Rust backend. Moss remains statically typed:
omitted annotations are inference requests, and an unresolved type or call is a Moss
compile-time error. The AST distinguishes local calls, explicit asynchronous messages,
and awaited request/reply operations. Existing `on`, `proc main()`, `let`/`var`, and
legacy object declarations remain available where they do not obscure the communication
boundary.

No runtime/domain behavior was changed to make parsing easier. The deliberate source
behavior change is the one required by the new communication syntax: a naked dotted
call whose receiver is a domain reference is now diagnosed and must use `message` or
`await`; all migrated examples use the explicit form.

## Language and semantic changes

- Top-level `fn name(...) = expression` and indentation-oriented `fn name(...):` block
  functions are parsed and lowered as ordinary local functions. A block's final raw
  expression is its result; explicit `return value` is supported for local functions.
- `fn main():` is accepted as the preferred entry-point spelling. `proc main()` and
  `proc main():` remain compatibility spellings. A value-returning `main` is rejected.
- `type Name:` is accepted alongside `type Name = object`. Fields may be written as
  `field: type` or as bare `field` when whole-program constructor/use constraints infer
  one concrete type. Unresolved or unknown field types and duplicate fields fail.
- Domain members may use either `on Handler(...)` or `fn Handler(...)`; both are Moss
  message handlers. A trailing `:` is accepted on domain and handler headers. Handler
  parameter types may be inferred from whole-program message calls; unresolved message
  contracts fail. Reply annotations remain explicit handler contracts in this slice.
- `message receiver.Handler(args...)` is an asynchronous/fire-and-forget Moss send.
  `value = await receiver.Handler(args...)` is a Moss request/reply wait. Compatible
  `let value = await ...` and `var value = await ...` declarations remain accepted.
  Standalone `await` and nested/general await expressions are rejected.
- A plain `foo(args...)` is an ordinary local call. A naked `domain.Handler(args...)`
  call is retained as a distinct `Call` AST node so semantic checking can reject it;
  it is never silently reclassified as a message.
- `|>` pipeline syntax is accepted on continued indented lines and is normalized to
  nested local calls before type checking and Rust lowering. This is a frontend
  normalization only; fusion, SIMD, and GPU planning remain future backend work.
- Primitive names are canonicalized case-insensitively for the documented spellings
  `Int`, `Float`, `Bool`, and `String`; Rust lowering still uses the existing Moss types.
- The following source rules remain in force: serialized run-to-completion handlers,
  non-reentrant awaits, sender/receiver FIFO, explicit replies, ownership checks at
  domain boundaries, self-await rejection, and no hidden deep copy.

## Compiler, frontend, type, and ownership analysis

- `lex_lines` keeps source line numbers, strips comments outside strings, rejects tabs,
  joins `|>` continuation lines, and computes a consistent indentation unit. The
  documented two-space style remains canonical; consistently indented wider units are
  accepted during migration.
- `Parser` records source headers and lines in `Function`, `Handler`, `Domain`,
  `ObjectType`, `MainProc`, and `Stmt`. `Stmt::Kind` now includes `Assign`, `Call`,
  `Message`, and `AwaitMessage` in addition to the existing control-flow and reply
  forms. Domain handler `fn` and top-level local `fn` are represented separately by
  their containing AST records.
- `Parser::parse_object`, `parse_domain`, `parse_handler`, `parse_function`,
  `parse_main`, and `parse_stmt` implement the new spellings while preserving legacy
  forms. `parse_stmt` recognizes explicit communication before ordinary calls and
  preserves assignment targets for both declarations and reassignments.
- `canonical_type_name`, `parse_simple_call`, `parse_member_call`,
  `top_level_assignment`, and `normalize_pipeline` provide the small expression parser
  used by the incremental frontend. It intentionally does not attempt a complete
  expression grammar.
- `Checker::infer_object_fields` and `infer_function_signatures` propagate literal,
  constructor, field, operator, local-function, message-handler, and await-result
  constraints to a fixed point. Unresolved function parameters, value returns, object
  fields, and calls produce Moss diagnostics rather than dynamic fallback values.
- `Checker::check_objects`, `check_function`, `check_stmts`, and `check_expression`
  validate object fields, local call arity/types, domain receiver/handler/arity, reply
  contexts, return contexts, await restrictions, and domain spawning restrictions.
  Reply and local return expressions must have statically inferable types.
- `Checker::check_ownership`, `check_ownership_block`, `require_available`, and
  `require_cross_domain_value` retain the previous direct ownership analysis. Existing
  non-primitive locals, parameters, state, and nested projections cannot cross a domain
  boundary. Fresh constructors, primitive snapshots, domain references, and queued
  same-domain transfers retain their documented rules.
- `MessageTransportOptimizer::run` now also scans local function bodies. A function call
  or expression result that this lightweight pass cannot follow is a conservative call
  graph barrier, preventing unsafe direct transport promotion.

## Rust lowering, backend, and runtime

- The existing generated Rust runtime remains lock-backed shared memory. `gen_shared_channel`
  emits `MossChannel<T>` with `Mutex`, `VecDeque`, and `Condvar`; generated code contains
  no `std::sync::mpsc` transport. Request/reply cells and ignored replies retain their
  existing behavior.
- `Generator::gen_function` emits inferred Rust signatures and ordinary local calls.
  Its generated comments identify the source `fn` and label the implementation as a
  local call with no Moss mailbox or domain scheduling.
- `gen_domain` and `gen_direct_domain` retain the message/mailbox and awaited-only
  direct shared-memory implementations. `-Oshared-memory` remains a backend choice;
  Moss source still describes a message and keeps its source ordering and blocking
  semantics.
- `gen_cluster` retains static `_shared` versus `_local` implementations, zero-sized
  clustered capabilities, a lock-backed shared ingress queue, and a plain local queue.
  Local awaited calls flush older queued work for the same target before direct dispatch.
- `gen_block`, `source_comment`, and `backend_comment` emit `// Moss line N: ...` source
  correlations and explicit `Moss backend` labels for message/mailbox, direct shared,
  and cluster-local lowering. These comments document backend choices and do not add
  Moss semantics.
- The generated `Send` assertions, tracker, default missing-reply diagnostics, and
  explicit `--no-await-error-handling` unchecked mode are unchanged from the preceding
  backend checkpoint. The unchecked mode remains unsafe without future supervision-tree
  guarantees.

## Optimization and planning changes

- Direct shared-memory promotion remains whole-program, opt-in, awaited-only, reply-only,
  sendability-gated, and disabled for clustered domains. No source syntax or Moss type
  was added for this optimization.
- The optimizer marks asynchronous sends from local functions, and treats unmodeled
  local calls/function result expressions conservatively. It therefore cannot turn a
  hidden asynchronous call into a direct state-lock call.
- Pipelines are currently normalized to nested calls before planning. No fusion or
  performance promise is made by the pipeline syntax.
- Domain clustering continues to be selected by the backend-only `--cluster=A,B` input;
  there is no runtime placement test and no Moss placement syntax.

## Tests and examples added or changed

- Added `examples/frontend_syntax.moss`, covering `type Name:`, bare inferred fields,
  expression and block `fn`, explicit messages, handler `fn`, optional header colons,
  assignment-await, a pipeline, and inferred function/handler signatures.
- Updated `examples/shared_memory.moss` to use `fn main():`, assignment-style spawn and
  await bindings, and explicit `message`. Updated the other valid examples and all
  domain-message tests from implicit dotted sends to explicit `message` sends.
- Added negative tests for `naked_cross_domain_call`, standalone `invalid_await`,
  `unresolved_field`, and `main_return_value`. Existing ownership, await, cluster,
  FIFO, and non-reentrancy negatives now use explicit message syntax.
- `tests/run.sh` checks the modern generated source comments and inferred signature,
  pipeline lowering, mailbox/direct/cluster output, source/backend separation, and the
  new diagnostics. It still compiles generated Rust with `rustc -D warnings` and rejects
  generated `std::sync::mpsc`.
- `Makefile`'s existing `examples`/`examples-optimized` target now builds the new
  frontend example along with every valid example and skips only the intentional
  `use_after_transfer.moss` negative input.

## Invariants that must remain true

- Moss source must not expose Rust locks, channels, `Arc`, `Mutex`, `Rc`, `RefCell`,
  `Send`, `Sync`, atomics, cluster placement, or supervision bookkeeping.
- Domain messages and awaits must remain distinct in the AST/semantic checker. A naked
  cross-domain call must never be silently lowered as an asynchronous message.
- Inference is static and complete for every accepted construct. No dynamic `Any`,
  runtime dispatch fallback, or hidden deep copy may be introduced when inference fails.
- Source-level domain serialization, run-to-completion, non-reentrancy, FIFO, await
  blocking, reply behavior, and ownership restrictions must remain invariant under
  Rust backend selection and optimization.
- Generated Rust must continue to use shared-memory lock/condition-variable transport
  for cross-thread messages and must not reintroduce `std::sync::mpsc`.
- Direct state-lock promotion may not apply to asynchronous/ignored-reply traffic or
  clustered domains. Cluster-local calls must be statically selected and free of
  synchronization primitives on their local path.
- `--no-await-error-handling` must remain explicit and backend-only until supervision
  trees define failure ownership. Checked missing-reply diagnostics remain the default.
- `Moss line` and `Moss backend` comments are documentation for inspection, not source
  semantics or runtime checks.

## Semantic decisions deliberately not made

- No supervision-tree, restart/stop, cancellation, timeout, failure-propagation, or
  mailbox-cleanup semantics were designed or implemented in this frontend pass.
- No automatic cluster selection, multiple instances per domain type, per-spawn identity,
  load balancing, or cluster lifecycle model was chosen.
- No ordinary synchronous `proc` parameter/alias model was implemented. Top-level `fn`
  is local computation; `message self.Handler(...)` remains queued communication and is
  not a synchronous procedure substitute.
- No `deepCopy()`, immutable sharing, arenas, `ref object` identity, persistent versions,
  generics/traits, or hidden copy-on-write behavior was added.
- General expression grammar, nested function declarations, method values, full alias
  ownership dataflow, and complete reply-path proofs remain future work.

## Known bugs, limitations, hacks, and incomplete implementation

- The frontend uses a deliberately small expression parser. Domain receivers currently
  need identifier-like bindings (`worker`, `self`); arbitrary domain expressions and
  general method values are not parsed.
- Bare object fields can be inferred from currently recognized constructor/use
  constraints. An expression whose constraints leave a field or function parameter
  ambiguous is rejected; the checker does not infer every possible Julia-style program.
- Ownership and call-graph analysis remains lightweight around arbitrary raw expressions,
  indirect aliases, and complete control-flow dataflow. Generated Rust may still expose
  a backend error when a source expression lies outside this analysis.
- Reply completeness is syntactic only. A handler that falls through without `reply`
  fails at runtime by default; unchecked await extraction can be undefined behavior in
  that case or if a worker/channel closes.
- Local function calls are not yet modeled interprocedurally for optimization; the
  optimizer disables promotion conservatively when it cannot see through them.
- Cluster placement remains type-wide and requires exactly one unconditional main-scope
  spawn per member. General cross-domain await cycles can still deadlock.
- The compiler remains a single C++17 source file and emits support helpers globally,
  including helpers not used by every optimized program.

## Design and documentation discrepancies

- `.codex/MOSS_DESIGN.md` and `docs/LANGUAGE_SYNTAX.md` describe two-space indentation
  as the canonical style. The implementation also accepts a consistently indented
  wider unit (for example four spaces) by computing an indentation gcd; this is a
  compatibility superset, not a new source semantic.
- The source direction names `<domain-expression>` receivers. The current parser accepts
  identifier-like receivers and `self`; richer receiver expressions are a parser
  limitation recorded here, not a deliberate language restriction.
- The design documents allow omitted handler annotations when whole-program calls infer
  one contract. That rule is implemented. An uncalled or otherwise ambiguous handler
  parameter still fails with a Moss diagnostic.
- `.codex/MOSS_DESIGN.md` keeps the approved default missing-reply runtime failure. The
  explicit `--no-await-error-handling` flag bypasses that check for a future supervision
  runtime and is not a change to normal Moss semantics.
- Historical `.codex/MOSS_DECISIONS.md` and `.codex/MOSS_AWAIT_HANDOVER.md` contain
  earlier alternatives involving MPSC or detached cross-domain transfer. They remain
  append-only history; the current implementation follows the later shared-memory and
  ownership-boundary decisions.

## Exact files and major functions for future work

- `src/moss.cpp`: `lex_lines`; `Parser::parse_object`, `parse_domain`, `parse_handler`,
  `parse_function`, `parse_stmt`; `Checker::infer_object_fields`,
  `infer_function_signatures`, `check_objects`, `check_function`, `check_expression`,
  `check_stmts`, `check_ownership`, and `require_cross_domain_value`;
  `MessageTransportOptimizer::run`, `scan_calls`, and `validate_clusters`;
  `Generator::gen_function`, `gen_shared_channel`, `gen_domain`, `gen_direct_domain`,
  `gen_cluster`, `gen_block`, and CLI `main`/`usage`.
- `docs/LANGUAGE_SYNTAX.md`: source-level syntax and inference direction, with migration
  limitations called out separately from backend choices.
- `.codex/MOSS_DESIGN.md`: authoritative settled source semantics and implemented backend
  contract. `.codex/CURRENT_STATUS.md` is the concise operational handoff.
- `tests/run.sh`: end-to-end Moss diagnostics, generated Rust checks, backend annotations,
  runtime behavior, optimization, clustering, and contention regressions.
- `Makefile`: compiler, full test, clean, and valid-example optimized-build targets.
- `examples/frontend_syntax.moss`, `examples/shared_memory.moss`,
  `examples/checkout.moss`, and `examples/object_pipeline.moss`: primary syntax/backend
  demonstrations.

## Commands run and results

Final validation commands:

- `make clean` — passed; removed ignored compiler, generated Rust, binaries, and test
  outputs.
- `make check` — passed; the complete Moss v0.2 harness reported `all Moss v0.2 tests
  passed`, including ordinary mailbox, direct shared-memory, clustered, ownership,
  FIFO, non-reentrancy, fallthrough, source-comment, and unchecked-await cases.
- `make examples` — passed; all valid examples generated and compiled with
  `-Oshared-memory`; the intentional negative example was skipped.
- `g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic src/moss.cpp -o /tmp/moss-syntax-werror`
  — passed.
- `sh -n tests/run.sh` — passed.
- `git diff --check` and `git diff --cached --check` — passed during validation before
  the final staging/commit pass.

Intermediate failures were not hidden:

- The first WIP `tests/run.sh` pass still expected the old `client.Run` generated source
  comment. The test expectation was updated to the explicit `message` source line, and
  the rerun passed.
- One `make check` pass after stricter reply-expression inference reported the expected
  ownership test's earlier diagnostic (`cannot infer the type of this reply expression`)
  instead of its ownership diagnostic. Local binding inference was corrected; the final
  `make check` passed with the intended ownership error.
- A compile pass briefly failed after making the checker domain map mutable because its
  constructor still stored `const Domain*`; the map/constructor were corrected and the
  subsequent full suite passed.

Negative cases intentionally fail compilation and are considered passing when their
line-numbered Moss diagnostics match the harness expectations. No unresolved test or
build failure remains in the final validation pass.

## Recommended next implementation steps

These are future tasks, separated from the completed migration:

1. Decide whether richer domain-expression receivers and nested/method-valued functions
   belong in the next grammar increment, then extend the AST without collapsing the
   local/message/await distinction.
2. Build a typed interprocedural call graph so optimization can see through pure/local
   functions without losing conservative safety.
3. Add complete expression typing and control-flow/reply-path analysis before expanding
   the inference surface.
4. Design supervision trees and failure ownership before making unchecked await extraction
   safe or default.
5. Extend clusters to multiple instances/per-spawn identities only after the source
   semantics and runtime lifecycle are agreed.
6. Revisit `deepCopy()`, ordinary procedures, retention-safe sharing, arenas, and
   persistent values with Moss designers before adding their syntax.
