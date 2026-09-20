# Moss current status

Updated: 2026-09-14

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
- The post-Phase-2.5 await-DAG correctness checkpoint makes local type environments
  branch-aware, rejects divergent concrete domain references at joins, validates
  bounded await targets in all executable code, and reports per-edge source sites in
  cycle witnesses. It does not change Moss concurrency semantics.
- Phase 4 functional/dataflow compilation is implemented at core checkpoint `829b1a9`.
  Pipelines now have typed, provenance-carrying compiler IR; observable callable effects
  are inferred separately from ownership; `-O0` preserves eager stage semantics; and
  `-O` fuses proven-safe chains into explicit loops without a Rust iterator runtime.
- The current Phase 4 hardening work carries exact pipeline plan IDs from checked AST
  occurrences into Rust lowering, includes initializer effects in `Reduce`, rejects
  non-Copy placeholder projections at Moss level, preserves real result-expression
  lines with source-derived semantic identities, and propagates capture mutation through
  ordinary helper calls.
- Phase 4.5 extends the authoritative Phase 4 IR with explicit materialization plans,
  exact-count/dead-map simplification, effect-safe `any`/`all` short-circuit plans,
  single-use immutable cross-binding fusion, and shared-source terminal DAGs. It adds no
  source syntax and keeps `-O0` as the complete eager reference traversal.
- Phase 4.6 adds a bounded Moss-to-Moss semantic rewrite sequence over the same IR.
  Optimized plans explicitly compose adjacent maps/filters, perform identity-proven
  predicate pushdown, eliminate safe trailing maps before `count`, propagate terminal
  facts, preserve single-use virtual bindings, and fold inert literal cardinality. It
  adds no syntax, lazy runtime, MLIR, hardware optimizer, or domain/backend policy.
- Phase 5 tooling is frozen at closeout checkpoint `92d0953` without changing the
  frozen semantic phases. The
  compiler emits a deterministic shared `.mossmap`; debug generation retains stable
  native symbols; `editors/emacs/moss-mode.el` provides editing, compilation, map-based
  navigation, `dape`/`lldb-dap` launch support, and objdump integration; optional native
  tools remain capability-gated.
- Phase 6A is frozen. It exposes those already-computed facts through the deterministic
  `moss-agent-1` JSON protocol. Bootstrap/schema/capability discovery, structured checks,
  and `inspect`, `type`, `effects`, `ownership`, `calls`, `awaits`, and `why` queries are
  read-only views over the ordinary compiler pipeline. Statement/binding queries do not
  borrow the enclosing callable's effect summary: exact effects remain null when no
  precise target summary exists, with the callable summary exposed separately as context.
- The remaining AI-native Phase 6 work is implemented and frozen. `entity-v1` durable
  semantic identities carry deterministic implementation/interface hashes; project
  snapshots power `impact` and conservative `test --affected` selection with dependency
  paths and reuse counters. `fmt` is canonical and idempotent; exact semantic rename,
  binding-expression, and call-argument edits validate and format before writing.
  Structured diagnostics expose stable `fixes` and `legal_alternatives`, while `cost`
  reports factual materialization, traversal, copy, specialization, and backend facts.
  Bootstrap advertises the complete workflow and a deterministic session-report template.
  Phase 5 `.mossmap` source identities remain build/source-layout provenance and are not
  silently redefined as durable edit identities. MCP, a daemon, package management, new
  syntax, and AI telemetry remain deferred.
- Phase 7 is frozen. It adds a conventional `moss.toml` project layer and the `moss build`,
  `moss clean`, `moss test`, and `moss bench` workflows. Debug/release builds,
  native test/benchmark harnesses, stable project/source IDs, filtered discovery,
  benchmark sampling/baselines, and all structured results reuse the ordinary
  compiler pipeline and `moss-agent-1` envelope. It adds no package resolver,
  module system, or new optimizer semantics. Native cache reuse
  includes the resolved Rust compiler, verbose backend version, profile, and flags;
  benchmark baselines use the same fingerprint for compatibility.

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

- A minimal project manifest names the project/version and configured source directory
  or file. Commands locate the nearest project root, use deterministic profile artifact
  paths, hide direct Rust invocation, compile with warnings denied, and reuse unchanged
  generated Rust/debug maps/native artifacts only under the same backend-toolchain
  fingerprint. `moss clean` removes build products while
  retaining saved benchmark evidence.
- Top-level `test "name":` declarations support statically checked `assert(bool)` and
  `assertEqual(actual, expected)`. Project source and recursive `tests/*.moss` discovery,
  substring filtering, per-test panic containment, stable
  `test:<relative-source>:<name>` identities, Moss source diagnostics, human summaries,
  and versioned JSON output are implemented.
- Top-level `bench "name":` declarations compile only through the release/highest-
  optimization profile. The safe generated harness black-boxes every discarded
  value-producing expression (calls, arithmetic, and functional pipelines), uses warmup,
  31 repeated samples, 1,000 invocations per sample, and median/p25/p75 reporting.
  Stable-ID JSON baselines retain compiler/profile/platform/backend-toolchain/time/
  methodology/sample data. Incompatible backend fingerprints suppress both comparison
  and optional regression-threshold enforcement; compatible comparison and filtering are
  implemented without fragile thresholds in the compiler regression suite.
- Build, test, and benchmark generation share the parser, checker, ownership/effect
  passes, functional/dataflow optimizer, backend plan, generator, `.mossmap` contract,
  profiles, artifact directories, and Phase 6A JSON envelope. Test and benchmark code is
  absent from ordinary application artifacts.
- `moss agent bootstrap|capabilities|schema --json` provides vendor-independent protocol
  discovery and workflow/safety guidance. `moss check --json` uses stable diagnostic
  categories and a uniform source/span/identity/details shape. Semantic queries accept
  exact source locations or retained semantic identities and reuse the compiler's static
  call/await graphs, ownership/effect summaries, and optimization explanations. A
  statement with no precise retained effect summary reports null rather than inheriting
  all effects from its enclosing callable.
- Successful project checks/builds/tests/benchmarks persist a compact semantic snapshot
  under `.moss/semantic-cache-v1/`. `moss impact <target> --json` distinguishes unchanged,
  implementation-only, and semantic-interface changes and reports direct/transitive
  dependents, affected tests/benchmarks/domains/await edges, specializations, and reuse
  counts. `moss test --affected` uses that cone during iteration and conservatively runs
  all tests when no baseline exists.
- `moss fmt` and `moss fmt --check` provide canonical two-space Moss formatting after
  parse/check validation. `moss edit` applies only exact durable semantic targets for
  initial rename/replace-expression/change-argument operations; stale or ambiguous
  targets are rejected. `moss cost <target> --json` exposes compiler-known cost facts,
  and every structured diagnostic includes `fixes` plus `legal_alternatives`.

- A representative Phase 6 agent session used bootstrap, structured checks and
  semantic queries, exact edits, formatting, impact analysis, and affected tests.
  The structured repair for a missing block colon and the dependency path reported
  by `impact` avoided guesswork and an unrelated test run; ownership intent remains
  deliberately agent-selected from compiler-provided alternatives.

- Every Rust emission has an adjacent versioned JSON `.mossmap` with absolute source and
  output paths, real one-based source spans, deterministic source/provenance identities,
  generated ranges, line mappings, generated/native symbols, and provenance. Transient
  functional IR IDs are excluded. Fused functional nodes deliberately share one
  generated range while retaining all contributing origins.
- `--debug` selects the unoptimized eager/mailbox reference lowering and adds stable
  function boundaries. `tools/moss-build-debug` compiles that Rust with DWARF, frame
  pointers, no stripping, and warnings denied. Concrete functions, methods, handlers,
  and main have deterministic readable exported symbol names; mailbox transport and
  handler implementation are separated so handler bodies also have a toolable symbol.
- The dependency-light Emacs `moss-mode` includes comment/string syntax, centralized
  font-lock definitions, tab-free two-space indentation, `else` dedenting, Imenu and
  defun navigation, compilation-mode check/build/run commands, read-only artifact views,
  and bidirectional Moss/generated-Rust navigation through the shared map.
- Optional debugging uses Emacs `dape` with `lldb-dap`. Pending Moss source breakpoints
  translate deterministically after LLDB creates the native target;
  `tools/moss_lldb.py` also supplies standalone `moss-map-load`, `moss-break`,
  `moss-where`, and filtered `moss-stack` commands. Ordinary variable inspection stays
  in LLDB/DWARF rather than a custom debugger runtime.
- Emacs disassembly commands resolve a concrete symbol through `.mossmap`, prefer
  `llvm-objdump`, fall back to GNU `objdump`, use `asm-mode`, and display all fused
  provenance origins. Compiler operation never depends on editor/debugger/disassembler
  availability.

- The compiler now has extracted `src/ast.hpp`, `src/constraints.hpp`,
  `src/diagnostics.hpp`, and `src/functional_ir.hpp` modules. `moss.cpp` still contains
  most parser, checker, ownership, optimizer, and Rust-generator implementation; the
  extraction is incremental rather than a completed module split.
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
- Julia-like `type Name:` blocks, inferred object fields, expression/block-bodied `fn`
  functions, typed functional pipeline expressions, explicit `message`, and
  assignment-style `await`.
- Phase 4 recognizes `map`, `filter`, `reduce(initial, fn)`, `sum`, `count`, `any`, and
  `all` as functional/dataflow operations rather than opaque nested calls. Static type
  propagation verifies element, predicate, accumulator, and terminal result types before
  Rust generation, including defined empty-input behavior. A nontrivial reduce initializer
  transfers into the terminal result rather than being copied.
- Functional stages accept named functions, bound READ-only instance methods,
  `_` placeholder expressions with immutable captures, and statically resolved method
  placeholders. Untyped higher-order callable
  parameters are closed at each call site, recorded as specialization dependencies, and
  erased from generated Rust; unbounded callable identity is rejected.
- `ObservableEffects` independently records local capture reads/mutation, domain
  reads/writes, message, await, I/O/external effects, unresolved effects, and possible
  failure. Transitive summaries conservatively govern fusion without changing
  READ/WRITE/CONSUME ownership inference.
- The functional IR distinguishes dense compilation-local pipeline/node handles from
  source-derived semantic identities. The checked AST carries the exact plan handle for
  each expression and static specialization, so Rust emission never re-matches plans by
  expression text/type/callable. Nodes retain real function/method result lines and stage,
  concrete input/output types, callable identity, captures, ownership/effect summaries,
  logical materialization, and stable semantic provenance. It also records element
  independence, determinism, and reduction compatibility for future planners.
- `-O0` emits explicit eager stage loops and logical intermediate collections. `-O`
  fuses safe map/map, map/filter/map, and terminal-reduction chains into one explicit
  loop, preserving order and wrapping integer accumulation while eliminating intermediate
  vectors. Effectful or possibly failing stages retain eager lowering.
- Phase 4.5 separates traversal fusion from physical materialization. Transformation
  nodes record virtual/materialized state plus escape, multiple-consumer, and barrier
  reasons. Exact count uses collection length; preceding pure/non-failing/non-divergent
  maps are dead when only cardinality is observed. Safe optimized `any`/`all` stop their
  consumer at a decisive element, while eager/effectful/failing/divergent callbacks visit
  the full source.
- Callable summaries carry `may_diverge` independently of ordinary effects and failure.
  Any function/method/handler containing `while`, plus callers reached through the
  acyclic local call graph, is conservatively marked. Work-eliminating plans require
  non-divergence; invocation-preserving ordinary fusion remains legal.
- A new local binding or `let` transformation with one adjacent immutable consumer can
  remain virtual across statements. Mutable/reassigned bindings, later uses, multiple
  consumers, intervening effects/statements, source effects, and control-flow ambiguity
  retain a concrete collection.
- `FunctionalTraversalGroup` represents one stable local source with multiple independent
  terminal consumer edges. Adjacent pure/non-failing sums, counts, filters/reductions,
  and `any`/`all` can share one explicit loop; dependent results and source mutation are
  conservative barriers. Group plans and generated comments retain every consumer's
  semantic provenance.
- `FunctionalPipeline::semantic_steps` is the Phase 4.6 post-analysis Moss semantic
  computation consumed by exact-plan codegen. A composed step references every original
  node; eliminated work retains its source identity and provenance. The bounded pass
  sequence does not reconstruct types/effects or use a generic rewrite framework.
- Phase 4.6 composes adjacent safe maps and left-to-right short-circuit filters. Its only
  predicate pushdown is through trivial `map(_)`, which the existing IR proves to be
  identity. Terminal `count` removes safe trailing maps even after a cardinality-changing
  filter, and inert scalar literal counts become constants. Effects, failure,
  `may_diverge`, ownership, multiple uses, and escapes remain conservative barriers.
- Deterministic `--dump-functional-ir` and `--explain-fusion` output exposes stage types,
  effects, spans, materialization decisions, retained provenance, fusion barriers, the
  optimized semantic-stage sequence, and successful `semantic-opt` rewrites.
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
- One indentation-aware type-environment walker now serves ordinary statement checking,
  domain-reference inference, local-call discovery, and await-target resolution. It
  forks `if`/`else` environments, merges only types present on every relevant path, and
  rejects conflicting concrete types. Definite same-type branch creation is carried to
  Rust lowering through explicit join metadata.
- Static bounded-target validation covers handlers, methods, all local functions, and
  `main`, including helpers reached only from `main`. Whole-program await-dependency
  checking separately constructs source-domain edges, follows statically resolved
  functional callbacks, visits every branch
  conservatively, and rejects every possible cycle reached through ordinary local
  functions. Asynchronous sends do not create await edges.
- Await-cycle witnesses identify the source line used for every dependency edge;
  repeated logical edges retain their actual source sites rather than relying on one
  arbitrary map insertion.
- Direct and mutual recursion are rejected. Compiler-internal READ/WRITE/CONSUME summaries drive local call lowering, and conflicting aliases at a call site are Moss compile-time errors.
- Copyable field projections retain a read effect; moving a nontrivial field consumes its containing value. Consuming method receivers lower by value rather than as shared receiver references.
- Generated Rust compilation with warnings denied in the test suite.
- Executable showcases cover method-based duck typing, two concrete named-trait
  implementations, inferred Vector/Map/Queue use, an executable optimized functional
  dataflow pipeline, and a small static job-scheduler application with domains, messages,
  and awaits.
- The focused functional showcases additionally cover named and placeholder stages,
  immutable captures and source reuse, every reduction terminal and empty-input identity,
  bound methods and static higher-order specialization, observable eager effect order,
  wrapping reduction arithmetic, read-only pipelines over user-defined values, and an
  awaited domain callback that remains an eager fusion barrier.
- Four Phase 4.5 showcases cover terminal simplification, a virtual cross-binding
  intermediate, a shared terminal traversal, and multiple-consumer materialization.
- The Phase 4.6 showcase covers semantic map/filter composition, a virtual named
  intermediate, dead-map removal, known literal cardinality, and safe short circuiting.

## Partially implemented or unimplemented

- Phase 7 deliberately has no package, dependency, or module/import system. Project
  targets use a temporary uber-module: build links `src/**/*.moss`, test links src plus
  `tests/**/*.moss`, and bench links src plus `benches/**/*.moss` as one global unit.
  Assertion panics are isolated per test in one process, but process aborts and failures
  on spawned threads can still terminate that unit. Benchmark warmup/sample counts are a
  fixed conservative first methodology rather than adaptive statistical calibration.
- Phase 5 does not invent one-to-one stepping for fused code. Several Moss nodes may
  resolve to one Rust/native location, and reverse assembly navigation is currently
  symbol/provenance-oriented rather than an exact address-level UI. Generic Rust
  implementations without a concrete Moss specialization have no stable native symbol.
- Emacs, Python, LLDB/`lldb-dap`, `dape`, and objdump are optional integrations. The
  corresponding regression runs only when each capability is installed; there is no
  VS Code extension, LSP, performance model, or Rust interoperability in Phase 5.

- Ownership analysis remains conservative around arbitrary raw expressions and indirect aliases outside the statically represented projection/call slice.
- `deepCopy()` and its cost warnings are approved but not implemented; no implicit copy is inserted.
- The future ordinary `proc` parameter model is not implemented; top-level `fn` local functions are implemented. `self.Message(...)` remains queued communication.
- Reply-path completeness is checked only syntactically; fallthrough is diagnosed at runtime.
- Backend choices use deliberately small whole-program heuristics rather than profiles
  or a cost model. `-O0` remains the unoptimized lock-backed mailbox reference.
- Cluster configuration is type-wide and currently requires exactly one unconditional `main` spawn for every member.
- Await-cycle checking is global and placement-independent; cluster planning relies on the language-level result.
- The global await DAG also prevents cyclic nested domain-lock acquisition in direct
  shared-memory lowering. A direct handler may hold its source state lock while its
  nested await acquires a target lock.
- Functional collection ownership is intentionally conservative. A callback that would
  WRITE or CONSUME a nontrivial element, a mutable capture, or a nontrivial `filter`
  result that would require an implicit copy is rejected rather than cloned or made
  unsafe.
- Functional fusion uses correctness-first effect rules rather than profitability data.
  Phase 4.5 scope reconstruction is deliberately lexical and adjacent rather than a
  general SSA optimizer. Automatic SIMD, threading, GPU lowering, generic stage
  reordering/algebraic predicate pushdown beyond Phase 4.6's proven identity case,
  layout/storage reuse, general lambdas, runtime callable values, user effect
  annotations, initializer-free reduction, and sophisticated profitability modeling
  are not implemented.

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
- A direct-shared domain that awaits a mailbox-backed domain currently retains its
  state lock for the target's full request/reply latency. This preserves logical
  occupation/non-reentrancy but can increase lock-hold latency; changing it requires a
  future equivalence proof.
- Most compiler implementation remains in one C++17 translation unit with a deliberately
  compact semantic/type checker and extracted data headers.

## Tests run and results

`make check` passes with Phase 4.6 on the frozen Phase 2.5 foundation. The suite compiles ordinary,
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
heterogeneous collections, and naked cross-domain calls. The showcase suite also executes
under the regression harness: static duck typing, named traits, inferred collections and
methods, the `map |> filter |> map |> sum` dataflow shape, the domain-backed mini
application, and the focused functional examples through Phase 4.6.

Await regressions additionally accept same-concrete-domain branch joins, reject
different-domain joins and one-branch-only targets, validate an await helper reached
only from `main`, retain unreachable-branch dependencies, and cover direct, transitive,
helper-hidden, and repeated-edge cycles. Cycle assertions require the correct Moss line
for every witness edge. All of these cases run alongside the unchanged Phase 2
ownership and Phase 2.5 backend suites.

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

Phase 4 regressions run representative functional programs through both `-O0` and `-O`,
compile both generated Rust programs with `rustc -D warnings`, compare their exact output,
and cover named, bound-method, and placeholder `map`, `filter`, map/map, map/filter/map,
every terminal, immutable capture, statically specialized higher-order helpers, empty
input, object-method placeholders, eager callback output order, and wrapping reduction.
Generated-code checks require one explicit loop and no intermediate vector or Rust iterator chain for fusible
pipelines. Separate I/O, possible-failure, domain-state, `message`, and `await` cases must
retain eager lowering. Terminal scalars can use message/reply boundaries directly, while
transformation collections must first materialize. Deterministic IR/explanation checks
cover types, spans, materialization, provenance, and barrier reasons. Negative tests require Moss diagnostics for invalid
predicates/callables/reductions, consuming elements, mutable capture, unbounded callable
identity, pipeline domain-boundary escape, higher-order recursion, and callback alias
violations.
They additionally distinguish identical pipeline text in pure-local and domain-effect
contexts, prove that an I/O-producing `reduce` initializer is recorded on the `Reduce`
node and preserves differential evaluation order, verify real method-result provenance,
reject non-Copy `_`/field-projection maps before Rust generation, and reject captured
mutation propagated through nested ordinary helpers.
The focused functional examples are likewise differential tests: each is compiled with
both `-O0` and `-O`, compiled by `rustc -D warnings`, executed, and compared against one
expected output.

Phase 4.5 differentials cover exact and mapped count, empty/immediate/late/no-match
`any`/`all`, complete eager effect ordering, possible-failure barriers, bare and explicit
immutable cross-binding fusion (including a function result), later-use/mutable/effect
materialization barriers, multiple consumers, sum/count/filter-count and any/all/count
shared DAGs, result-dependency and source-mutation barriers, and wrapping shared sums.
Direct/transitive `while` regressions verify that mapped-count elimination and standalone,
cross-binding, and shared-DAG `any`/`all` skipping are disabled by `may_diverge`, while
ordinary invocation-preserving fusion remains enabled.
Generated-Rust checks require eliminated callbacks/loops, optimized-only short-circuit
control flow, absent virtual collections, present required collections, and one shared
source loop. Deterministic IR checks assert materialization reasons, DAG edges, stable
group identity, and combined provenance.

Phase 4.6 differentials cover adjacent map and filter composition, identity-only legal
predicate pushdown plus non-identity/effectful rejection, safe and divergent/effectful
dead-map cases, filter/map/count terminal simplification, safe and divergence-blocked
`any`/`all`, known literal cardinality, runtime-empty terminal identities, single-use
cross-binding collapse, downstream dead work, and multiple-use materialization. The
suite inspects the semantic plan and generated Rust, including composed predicate
short circuiting, eliminated callbacks/literal allocation, retained barrier callbacks,
deterministic rewrite output, and exact `-O0`/`-O` result parity.

Phase 5 regressions build the tooling fixture twice at identical paths and require
byte-identical maps; compare `-O0`, optimized, and explicit debug maps; verify deterministic
source/provenance identities, real function/method/handler lines, readable native symbols,
bidirectional exact line resolution, and many-to-one fused provenance; and compile and
run both generated programs with warnings denied. Python map/LLDB helper checks pass,
all 19 batch ERT tests pass under Emacs 30.1, and `llvm-objdump` resolves both an ordinary
debug function and an optimized fused-pipeline symbol. Live LLDB 19.1.7 and
`lldb-dap-19` regressions load the shared helper/map, resolve exact breakpoints for main,
a method, ordinary functions, and a handler, step between two exact Moss lines, map user
and runtime stack frames, inspect integer/boolean/user-type/raw String/raw Vector locals,
and terminate cleanly. Debian's LLDB build warns that Rust-specific value presentation is
limited, so collection/string inspection remains structural. Phase 5 provenance IDs are
repeatable for an unchanged source layout but are not the durable cross-edit semantic IDs
planned for Phase 6. No compiler functionality depends on optional debugger tools.

`make examples` passed, compiling every valid example (including the executable static
trait, dataflow, reduction, callable, effect-order, and object-pipeline showcases) with
`-Oshared-memory`; the intentional `use_after_transfer.moss` negative example was skipped. A strict
`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic` build and `sh -n tests/run.sh` also
passed for this checkpoint.

Phase 7 validation additionally builds the example project in debug and release modes,
checks byte-stable map/artifact reuse under one Rust toolchain, forces rebuilds for a
changed/fake Rust identity, executes and filters passing/failing native tests,
inspects Moss assertion values/locations, executes and filters release benchmarks,
validates call/arithmetic/pipeline black-box and sampling structure, saves and compares
a toolchain-identified metadata/sample baseline, suppresses incompatible baseline
threshold enforcement, checks structured project errors, and runs `moss clean`.
Phase 6A validation also proves target-local statement effects remain unknown rather
than inheriting callable-wide I/O while the enclosing, callable, and pipeline summaries
remain available. The full `make check` suite,
`make examples`, strict C++17 warnings-as-errors build, generated Rust `-D warnings`,
Phase 4/4.5/4.6 differentials, Phase 5 map/Emacs/objdump/LLDB/DAP integrations, Phase 6A
API regressions, 19 ERT tests, `sh -n tests/run.sh`, and `git diff --check` all pass.

Two local smoke samples of the contention program completed 20 baseline runs in approximately 0.20–0.25 seconds and 20 direct shared-memory runs in approximately 0.02–0.03 seconds. This is evidence that transport elimination works for the intended request/reply shape, not a general performance claim.

## Immediate next tasks

Phase 2, Phase 2.5, Phase 4, Phase 4.5, Phase 4.6, Phase 5, Phase 6A, and
Phase 7 are frozen at their respective checkpoints; none of the following is implicitly
authorized by this status record.

1. Benchmark mailboxes, batching, Mutex/RwLock, atomics, and clusters on
   representative workloads.
2. Design modules/imports and package dependencies before allowing independently
   discovered test/benchmark files to import application declarations.
3. Evaluate later profitability, SIMD, threading, and GPU plans using the preserved
   independence, reduction, capture, effect, materialization, and DAG facts without
   changing eager semantics.
4. Extend cluster placement from one instance per domain type to a static per-spawn
   identity plan.
5. Implement explicit `deepCopy()` with type checking, deep lowering, and approved cost diagnostics.
6. Expand ownership analysis beyond the Phase 2 call/projection/control-flow slice when future reference facilities are designed.

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

## 2026-09-14 temporary multi-file project linkage

Project targets now use a temporary uber-module model until Moss gains explicit
modules/imports. `moss build` compiles every `src/**/*.moss`; `moss test` adds
`tests/**/*.moss`; and `moss bench` adds `benches/**/*.moss`. Each target is one
global compilation unit with deterministic path discovery, order-independent
declaration resolution, duplicate-symbol diagnostics, and original physical
source provenance in diagnostics, semantic queries, generated maps, tests, and
benchmarks. Native artifact fingerprints include the complete source set.

Semantic query/edit context hardening: Phase 6A queries and edits now consume the
same temporary uber-module source sets as project build/test/bench. `src` selectors
analyze all application files, test selectors use `src + tests`, and benchmark
selectors use `src + benches`; standalone files remain single-file. Cross-file
renames are transactional within the selected context, and expression/argument edits
are validated against the complete logical program before writing. Physical source
paths remain in query results and diagnostics.
## Implicit domain specialization hardening

Each declared instance of an implicit-state domain now has one monotonic concrete
specialization. Compatible repeated calls remain valid; an incompatible state,
handler-parameter, or reply constraint is rejected with both constraint lines rather
than overwriting the layout. Separate instances of the same source domain continue to
specialize independently, and exact-instance await identities are unchanged.

Handler parameter constraints are indexed by handler and parameter slot. Multi-parameter
handlers can therefore specialize as tuples such as `Set[0] -> Int, Set[1] -> Float`; only
an incompatible constraint for the same slot is rejected.

Typed domain-handle parameters remain specialized by their declared nominal domain type;
the exact spawned instance is retained only for await-graph substitution (and, where
needed, backend instance plumbing). Generated code uses a uniform nominal handle wrapper
to route calls to independently materialized instance layouts. Thus one handler may receive
multiple instances of the same domain type without splitting its source-level specialization.

## Phase 8

Modules/imports/exports are supported with qualified namespace resolution.
Typed exports are materialized contracts; untyped exports are compile-time Moss
generics represented in versioned `.mossi` interfaces. Await graph identities
are exact declared domain instances (`module::binding`), with composition-time
DFS over those edges. Each explicit module now emits its own generated Rust
crate/rlib in import-DAG order; consumers link dependency rlibs with `--extern`.
Generic semantic IR and private helper closure are re-instantiable without
provider source, and final-build specializations are canonical in a dedicated
Rust crate. Legacy implicit-module projects remain supported.

## Phase 4.7 — Static Iteration and `for` Loops

**Phase 4.7 — Static Iteration and For Loops: complete and frozen.**

Phase 4.7 is implemented additively on the frozen Phase 4–4.6 semantics. `for binding
in source:` has scoped bindings and participates in the existing type, effect, and
ownership walkers. `range(start, end)` is half-open; vectors use READ traversal; and
custom traversal resolves statically through the structural `Iterator` contract
(`next() -> Option[element]`). Collection mutation during an active READ traversal is
rejected. Effect analysis reuses the canonical iterator-element resolver for loop
bindings, so method and ownership/effect summaries agree with ordinary type analysis.
No runtime iterator dispatch or vtable is emitted.

## Phase 2.6 — Immutable Message Payloads and Shared-Memory Copy Elision

Incoming handler arguments are immutable READ-only value snapshots regardless of
concrete type. The normal ownership/effect checker rejects mutation,
reassignment, consumption, moving a payload into domain state, and replying
with the original payload, including through transitive helper calls. Explicit
forwarding with `message` remains legal because it is a new copy boundary;
derived replies remain legal. `Copy` is only a backend/property distinction and
does not weaken these Moss restrictions.
Mailbox lowering retains owned payload copies. Synchronous DirectMutex,
DirectRwLock, DirectAtomic, and cluster-local lowering pass nontrivial READ-only
payloads by temporary immutable reference when safe, while primitive Copy values
remain by value. This is a backend optimization preserving identical Moss
semantics.

## Phase 10.1A — Fast Debug interpreter checkpoint

The checked Moss AST now has a direct Fast Debug execution backend for ordinary
functions. `moss run --interp` and standalone `moss test --interp` execute
literals, arithmetic, locals, assignments, conditionals, while loops,
function calls, returns, structs, fields, methods, and assertions without
invoking Rust tooling. `--trace` emits deterministic newline-delimited JSON
semantic events. Domain scheduling, messages, and await/reply remain deferred
to the next Phase 10 checkpoint; unsupported operations report an interpreter
diagnostic rather than falling back to generated Rust.

Fast Debug project entry points now reuse the project compiler's checked source
loading. `moss debug app` (or a project directory/entry source) interprets the
transitive source-module closure for explicit-module projects, and the complete
legacy `src/**/*.moss` uber-module otherwise. All selected Moss code executes
in one interpreter; compiled Moss interfaces/native modules are rejected rather
than mixed into the run. Project closure regressions cover cross-file calls and
verify that no Rust compiler is needed.

## Phase 10.5 — Semantic convergence and AI introspection

Phase 10.5 is a non-semantic tooling/documentation checkpoint. The existing
static callable specialization, first-order effect summaries, entity-v1/source
provenance, semantic query API, impact graph, and Fast Debug trace are now
documented together. Query JSON exposes the existing source identity,
specialization identity when an exact record exists, resolved call kind, and
caller edges without creating a second analysis. Fast Debug ordinary execution
trace events retain source file and semantic identity and include local reads,
returns, branches, loops, writes, and assertion failures.

`moss agent schema --json` reserves (but does not fabricate)
`synchronization_diagnostics` fields for future class/root/handler and
conflict/collapse reporting. A complete Account `R(h)`, `W(h)`, `W*`, and
ProtectedRead derivation is recorded in `docs/SEMANTIC_CONVERGENCE.md` as
design vocabulary only.

Legacy mailbox/worker/await transport descriptions remain for implemented
backends and historical checkpoints. No new self-send or same-domain handler
chaining semantics, WRITE-parameter assignment rule, domain-handle
representation, synchronization rank, effect ABI, or lock trace semantics
were selected here; those items remain **BLOCKED BY OPEN DESIGN**.
