# Phase 25.3 J2 — Compiler Parity Gap Ledger

**Directive:** L7 Directive on Semantic Fidelity and Mechanical Port Completion  
**Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`  
**Working Branch:** `phase25-3-java-j2`  
**Audit Date:** 2026-10-24  

---

## 1. Subsystem Parity Gap Matrix

| Subsystem | C++ Authoritative Implementation | Existing Java Implementation | Missing Invariants / Identified Gaps | Severity | Assigned Agent | Status | Required Parity Tests |
|---|---|---|---|---|---|---|---|
| **Expression Typing & TypeSolver Integration** | `src/semantic_checker.cpp#L10100-L10500`, `src/type_ir.cpp#L1-L180`, `src/type_ir.hpp` | `compiler-java/.../semantics/SemanticChecker.java`, `ExpressionTyper.java` | Previously used string heuristics defaulting to `"int"`. Now fully integrated with `TypeSolver`, `TypeRef`, and `ExpressionTyper`. Fail-closed validation for all expressions, calls, operations, and collections. | **P1** | Agent A | **PARITY VERIFIED** | Negative type mismatch tests, call argument validation, return type checking, recursive expression typing. |
| **Compiler-Derived 2PL Synchronization Plan** | `src/native_generator.cpp#L300-L800`, `src/native_generator.cpp#L3800-L4500` | `compiler-java/.../backend/NativeGenerator.java` | Lowered domain state into fine-grained `struct <Domain>Class<i>` with individual `RwLock`s. Handlers analyze READ/WRITE class sets, acquire locks in strict `(domain_rank, class_rank)` 2PL order, emit telemetry, and access state via scoped projections. | **P1** | Agent A | **PARITY VERIFIED** | Concurrency locking tests, multi-domain message dispatch, read/write contention hook events. |
| **Differential Test Harness & Negative Corpus** | `tests/run.sh`, `tests/*.moss`, `tests/phase25/` | `tests/differential/run_differential.py`, `tests/differential/corpus/` | Expanded harness to support directory trees, negative corpus expectations (`# EXPECT: ok=false`, `error.code=...`), structured JSON diagnostics comparison, and `--oracle-only`. | **P1** | Agent B | **PARITY VERIFIED** | Negative semantic tests (type mismatches, unclosed types, undefined variables), exit code and diagnostic matching. 39/39 passing. |
| **Functional Pipeline Optimizer** | `src/functional_optimizer.cpp#L1-L1398`, `src/functional_ir.hpp` | `compiler-java/.../optimizer/` (Milestone J2-A) | Implemented pipeline stage recognition, fusion (`compose-map`, `compose-filter`), and terminal reductions (`count`, `sum`, `any`, `all`). | **P2** | Agent B | **PARITY VERIFIED** | Adjacent stage fusion tests, filter composition, terminal reduction evaluation. All optimizer unit tests passing. |
| **ABI-v10 Checked-Body & `.mossi` Codec** | `src/checked_body_ir.cpp#L1-L541`, `src/checked_body_ir.hpp#L1-L1797` | `compiler-java/.../abi/` (`CheckedBodyCodec.java`, `H3BodyReader.java`, `MossiInterface.java`) | Round-trip checked body encoding/decoding, deterministic SHA-256 semantic hashing, `.mossi` interface generation, and source-free verification. | **P2** | Agent B | **PARITY VERIFIED** | Checked body serialization round-trip, hash determinism, `.mossi` header validation, source-free tamper detection. |
| **Semantic Queries & Agent Protocol** | `src/moss.cpp#L8500-L9200`, `src/typed_semantic_api.cpp` | `compiler-java/.../cli/AgentBootstrap.java`, `query/SemanticQueryEngine.java` | Implemented `agent bootstrap --json`, `inspect`, `type`, `effects`, `ownership`, `calls`, `why`, `fmt` matching canonical `moss-agent-1` JSON protocol. | **P3** | Agent A | **PARITY VERIFIED** | Differential query tests against C++ `./moss`. |
| **Eclipse Cache Artifact Tracking** | `.gitignore` | `compiler-java/jdt/configuration/` | Cleaned up and untracked all ephemeral Equinox/JDT cache files (`.fileTable.*`, `.tmp*.instance`, `daemon.log`, `cache.timestamps`). | **P3** | Agent A | **PARITY VERIFIED** | Clean `git status` with zero untracked runtime artifacts. |

---

## 2. Identified Uncalled Helper Classes in J1

The initial audit revealed that while several foundational subsystems were mechanically ported and tested via unit tests in J1, their integration into the primary compilation pipeline was decoupled:

1. **`org.mosslang.compiler.types.TypeSolver`**: Tested in `TypeSolverTest.java` (unification, occurs check, rollback, `unifyAll`), but `SemanticChecker.java` maintained its own string-based `TypeEnv` rather than solving AST expression types with `TypeSolver`.
2. **`org.mosslang.compiler.types.MemberClosure`**: Tested in `TypeSolverTest.java`, but not invoked during expression member dispatch in `SemanticChecker.java`.
3. **`org.mosslang.compiler.types.CallableId`** & **`SpecializationKey`**: Implemented in `org.mosslang.compiler.types`, but not yet stored onto statement AST nodes during semantic analysis.

---

## 3. Immediate Action Plan

1. **J2-A (Agent A — P1 Priority): Authoritative TypeSolver Integration**
   - Replace string-based expression guessing in `SemanticChecker` with structured AST expression typing.
   - Build `ExpressionTyper` using `TypeSolver` and `TypeRef`:
     - Disallow defaulting to `"int"`. Unrecognized or incompatible expressions must fail with targeted `CompileError` (`TYPE_MISMATCH`, `UNKNOWN_SYMBOL_OR_TYPE`).
     - Unify function call arguments with parameter types.
     - Type check binary expressions (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `and`, `or`, `not`).
     - Resolve nominal object constructors and field access.

2. **J2-B (Agent B — P1 Priority): Comprehensive Negative Differential Harness**
   - Expand `tests/differential/` with negative test cases exercising rejected programs.
   - Verify that Java compiler rejects invalid programs with matching diagnostic codes and error envelopes.

3. **J2-C (Agent A — P1 Priority): Compiler-Derived 2PL Synchronization Plan**
   - Lower domain state into typed synchronization classes with read/write set analysis.
   - Generate fine-grained class locks and 2PL lock ordering in `NativeGenerator.java`.

## Phase 25.3 continuation — ABI-driven source-free callable availability

The Java source-free CLI no longer special-cases `Provider.outer` or
`Provider.relay` by source text. Imported ABI-v10 functions record whether the
loader projected a supported checked body; a consumer call to an imported
function without one now receives the general
`SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` diagnostic. The v9 record writer also
preserves inferred open built-in return kinds rather than substituting `unit`.
A real CLI test uses a generated provider interface and artifact seal: valid
`Provider.outer` passes source-free checking, while unsupported open-result
`Provider.relay` fails closed through the general path. This does not establish
source-free execution or general private helper closure.

Validation: clean Java build and all ten Java unit-test classes pass in default
ABI-v9 and staged ABI-v10. Native Rust execution, Margo provider-consumer
integration, SDK gates, and full release acceptance remain unverified.

## Phase 25.3 continuation — ABI-v10 inferred empty-vector body

The Java ABI-v10 producer now emits a canonical typed checked body and generic
summary for the restricted zero-argument `return []` callable shape. The
source-free loader validates and projects that exact body shape; the metadata
regression removes legacy generic-body rows and checks consumer specialization
as both `Vector[Int]` and `Vector[Bool]`. This is a bounded source-free profile,
not general collection-body or helper-call support. Clean Java build and all
ten Java unit-test classes pass in ABI-v9 and staged ABI-v10 modes. Native,
Margo, SDK, and full acceptance gates remain unrun.

## Phase 25.3 continuation — closed typed empty-vector return

The restricted source-free ABI-v10 empty-vector body profile now accepts both
an inferred `Vector[T]` result with one bound type variable and a closed
`Vector[T]` result with no bound variables. Loader validation still requires an
empty checked VectorLiteral, zero parameters, and a summary with no unsupported
requirements, dependencies, or effects. Java producer-consumer tests cover
closed `Vector[Int]` plus inferred Int and Bool specializations. Native/Margo
execution and broader collection body profiles remain open.

## Phase 25.3 continuation — mixed numeric comparison semantics and Fast Debug

A focused differential Fast Debug probe exposed Float relational comparisons
being evaluated through integer conversion in Java; e.g. C++ returned `true`
for `1.9 > 1.1` while Java returned `false`. Java Fast Debug now uses floating
comparison when either operand is Float. `ExpressionTyper` also admits the
language-defined Int/Float mixed comparison, while an incompatible Bool/Float
pair remains a `TYPE_MISMATCH`. A focused producer test and direct C++/Java
interpreter probe cover the corrected semantics. All eleven Java unit-test
classes pass in ABI-v9 and staged ABI-v10. Broader interpreter parity and the
native/Margo/SDK acceptance gates remain open.

## Phase 25.3 continuation — numeric equality in Java Fast Debug

A focused C++/Java interpreter probe found Java using wrapper equality for
numeric values: mixed `Int == Float` disagreed, signed zero compared unequal,
and NaN compared equal to itself. The Java interpreter now converts mixed
Int/Float values for numeric equality and uses IEEE Float equality semantics.
Coverage exercises `==` and `!=` for mixed values, signed zero, and NaN; the
six outputs match C++ Fast Debug. Clean build and all eleven Java unit tests
pass in ABI-v9. Broader Fast Debug and native/Margo/SDK gates remain open.

## Phase 25.3 continuation — structural Fast Debug equality

A C++/Java probe showed separately allocated equal Vector values compared
unequal in Java because `Objects.equals` compared `Value` element identities.
Java Fast Debug now compares Vector and Queue elements recursively, Map values
by matching keys, object fields with nominal type identity, and enum payloads
structurally. Nested Int and Float Vector comparisons now match C++. Clean Java
build and all eleven unit-test classes pass in ABI-v9. Broader collection/map
and full interpreter/native parity remain unverified.

## Phase 25.3 continuation — Fast Debug while-loop execution

Inspection found that the Java Fast Debug statement dispatcher did not handle
`Stmt.Kind.While`, so a body could be visited once without reevaluating its
condition. The interpreter now executes the checked body repeatedly under the
condition. A checked counter-loop emits `0`, `1`, `2` and matches the C++
interpreter. Clean build and all eleven Java unit-test classes pass in ABI-v9;
staged ABI-v10, native, Margo, and SDK gates remain open.

## Phase 25.3 continuation — Fast Debug local tracing and string escapes

The C++/Java trace probe exposed missing Java `LocalWrite` events and incomplete
`LocalRead` event fields. It also showed that escaped quotes remained escaped
in Java runtime strings. Java now emits local read/write trace events with
source location and JSON escaping, and decodes the Moss string escapes accepted
by the parser. The focused probe's local events and stdout match C++. Clean
build and all eleven Java unit-test classes pass in ABI-v9 and staged ABI-v10.
General control/event-stack, state/message trace parity, and acceptance gates
remain open.

## Phase 25.3 continuation — Fast Debug lexical local scopes

A checked C++/Java probe found Java nested blocks shared the parent locals map,
so `let` and `for` bindings overwrote outer variables. Java now evaluates
if/else, while, for, try/recover, and match blocks in scoped local maps, merging
updates to preexisting bindings while discarding block declarations and loop
variables. The probe prints `2, 1, 3, 4, 1` in both interpreters. Clean build
and all eleven Java unit-test classes pass in ABI-v9 and staged ABI-v10;
complete interpreter parity and release gates remain open.

### Recovery payload binder follow-up

The lexical-scope fix exposed that recovery binders were inserted into the
parent map and received the full enum value. Java now extracts the named
payload field (or the single payload) and restores any shadowed outer local
when leaving the recovery body. A checked `Failure.Bad(code: 2)` probe prints
`2` in recovery and `1` after it in both interpreters. All eleven Java test
classes pass in ABI-v9 and staged ABI-v10.

## Phase 25.3 continuation — Java Fast Debug while-loop tracing

The focused loop trace exposed missing `LoopIteration` events, incorrect body
depth and control-parent links, missing reassignment `before` values, and stale
condition source lines in Java. Java now records each while iteration, nests
body local events under the iteration, includes write before/after values, and
restores the loop header context before reevaluating the condition. A small
while trace compares equal to C++ event-for-event and stdout-for-stdout. All
eleven Java test classes pass in ABI-v9 and staged ABI-v10. Other branch/for/
message/state trace paths remain open.

### Range-for tracing follow-up

Java range-for loops now emit numbered LoopIteration events and trace nested
body reads/writes with matching depth and control-event links. A full `range(0,
2)` trace and stdout compare equal to C++. All eleven Java unit-test classes
pass in ABI-v9 and staged ABI-v10. Vector/Map iteration traces and the remaining
trace constructs are not yet covered.

### Branch tracing follow-up

Java now emits `BranchTaken` for selected then/else arms and nests selected
branch body events under that control event with incremented depth. A probe
covering both choices compares exactly with C++ event-for-event and stdout.
All eleven Java unit-test classes pass in ABI-v9 and staged ABI-v10; other
control, message, and state tracing remain open.

## Phase 25.3 continuation — v10-only callable declaration seeding

At pushed source checkpoint `391bfb8ae9835343b53f8fcf4d645cc3b242edcc`,
Java source-free loading can seed callable declarations from ABI-v10 summaries
when both legacy declaration copies are absent. The bounded profiles cover
closed scalar and collection returns, nested `Vector[Vector[Int]]` literals,
escaped `Vector[String]`, direct generic parameter pass-through for `(T) -> T`
and `(T, U) -> U`, and zero-argument empty `Vector[T]` specialized as Int or
Bool. Checked bodies and summaries are retained; requirements, dependencies,
and effects must be absent for seeded profiles. Generic synthetic parameters
remain unannotated so each direct pass-through call infers independently, while
summary access modes are preserved.

The source-free metadata regression removes corresponding legacy rows from both
`binary_contract` and `semantic_contract`, then semantically checks and runs the
consumer in Fast Debug. The latest test checkpoint is
`150641efd7ec178c7ec26933f7da126bd80e96c1`. All eleven Java unit-test classes
pass in staged ABI-v10 and default ABI-v9; the standalone JAR and launcher
bootstrap were also checked. Native Rust provider-consumer linkage, SDK
integration, complete generic relations, method-body import, and complete
logical ABI-v10 parity remain open. This does not establish full parity or
release readiness.

## Phase 25.3 continuation — refreshed Fast Debug capture trace sample

At `fc499c1741f74f89c1f060b21152bbd199231b66`, a fresh interpreter comparison
of all eleven positive `phase21_capture_*` fixtures produced identical stdout
and complete JSON traces under C++ and Java. This supersedes earlier localized
trace differences for selected fixtures; it is a focused sample and not full
Fast Debug trace parity. No native compilation or execution was run.

## Phase 25.3 continuation — `for` binder scope exit

A minimal source with `for item in [1]: pass` followed by `return item` was
incorrectly accepted by Java. C++ rejected the post-loop read with
`UNKNOWN_SYMBOL_OR_TYPE`. Java loop restoration handled only a pre-existing
shadowed name, leaving fresh loop binders in the environment. The restoration
snapshot now tracks binding presence and removes fresh loop names from type,
ownership, immutability, and source-binding metadata after the loop. Existing
bindings still restore after shadowing.

`./scripts/java/test.sh` passes all eleven Java test classes, including the
existing object-method loop argument regression and a new positive loop-body
method-call check. The minimized post-loop probe now receives
`UNKNOWN_SYMBOL_OR_TYPE` from both compilers. No native or project gate ran.

## Phase 25.3 continuation — assertion failure trace and source provenance

`tests/phase224_debug_domain.moss` fails `assertEqual(result, 6)` after a
synchronous domain reply. C++ emitted an `AssertionFailure` trace event and a
line-qualified interpreter error; Java previously emitted no failure event and
reported an internal error. Its synthesized main tail-expression statement
also lacked the source filename, leaving assertion argument reads unlocated.
The Java parser now retains the main function source on that statement, and the
Fast Debug assertion path emits the failure event and surfaces a line-qualified
`ExecutionError`.

The complete trace and failure text now match C++ for the selected fixture.
`./scripts/java/test.sh` passes all eleven classes, including a focused
`assertEqual` failure regression. Native compilation/execution was not run.

## Phase 25.3 continuation — `run --json` command contract

A focused `run --interp --json` assertion probe showed C++ rejects this
combination with `moss: run does not yet support --json`, while Java returned a
JSON error labeled `compile`. Java now matches the unsupported-option response
and exit status before reading source. `BootstrapTest` pins this CLI behavior.
All eleven Java unit-test classes pass in ABI-v9. Check/compile JSON behavior is
unchanged.

## Phase 25.3 continuation — JDT daemon identity handshake

The JDT client considered any process listening on port 45290 to be its daemon,
so status and shutdown could target a foreign local service. Liveness now sends
`PING` and accepts only `OK MOSS_JDT 1`; the repository's headless JDT
application replies with that identity. Other listeners and old protocol
servers select the existing javac fallback.

Six `scripts/java/test_jdt_client.py` lifecycle tests pass, including positive
and negative local socket handshake cases. Python byte-compilation and
`git diff --check` pass. No Eclipse executable is available in this workspace,
so live daemon startup/build/stop is still unverified.

## Phase 25.3 continuation — remove name-based `may_panic` query result

Java effects queries treated any target name containing `panic` as evidence
that the callable may panic. A harmless `panic_free() -> Int` returned
`may_panic: true` solely because of its name. That shortcut is removed. The
current source/body scan reports the test's `assert(false)` callable as
potentially panicking while `panic_free` reports false. All eleven Java unit
test classes pass in ABI-v9. Broader checked-operation and transitive panic
summary derivation remains incomplete.

## Phase 25.3 continuation — source-derived direct raise facts in effects queries

The Java query returned an empty `raise_set` for a generic function name even
when its body directly raised a typed enum case. It now collects direct
`Stmt.Raise` variants from queried functions, methods, and handlers and emits
those facts before legacy fallback handling. The minimized arbitrary-name
`calculate()` query reports `enum:Trouble:Bad`, matching the C++ result's
`observable_effects.raise_set`.

All eleven Java unit-test classes pass under ABI-v9. This verifies direct typed
raises only; transitive, recovered, callback, and complete other effects remain
open. The full C++ and Java JSON response objects still differ in shape and
availability of retained facts.

## Phase 25.3 continuation — subtract caught direct raises

Follow-up validation of source-derived direct raise effects added the matching
try/recover rule: a directly raised variant is omitted from the escaping set
when an immediately attached recover arm catches that variant or catches all.
The new `handled()` query test reports an empty raise set; the unhandled direct
raise test retains `enum:Trouble:Bad`.

All eleven Java unit-test classes pass in default ABI-v9 and staged ABI-v10.
Bare reraises, indirect/transitive calls, and complete checker-owned effect
summaries remain open.

## Phase 25.3 continuation — ordinary call-chain raise query

The Java effects query now follows ordinary statically named function calls to
collect escaping direct raises, with cycle protection. It applies the caller's
lexical recovery arms at each call site. Tests cover an arbitrary `calculate()`
forwarding to a direct-raising helper and `handled_call()` catching that
helper's variant; unhandled and directly recovered controls also remain.

All eleven Java unit-test classes pass in default ABI-v9 and staged ABI-v10.
This remains a bounded source walk, not complete method, callback, bare-reraise,
or cross-module effect closure.

## Phase 25.3 continuation — same-owner method raise query

Effects-query call traversal now follows same-owner object method calls,
including method tail-result expressions. The focused `Box.calculate()` to
`self.source_fault()` case reports `enum:Trouble:Bad` under both C++ and Java.
All eleven Java unit tests pass in ABI-v9. This does not close effects for
arbitrary receivers, generic/cross-module methods, or callbacks.

## Phase 25.3 continuation — explicit bare-reraise effect query

A bare `raise` inside `recover Trouble.Bad` now contributes that explicit
variant to the selected callable's escaping raise set, subject to any outer
matching recovery. The minimized `reraiser()` query matches C++ on
`enum:Trouble:Bad`; helper and handled-call controls also match.

All eleven Java unit tests pass in ABI-v9. Catch-all reraises and generalized
multiple-variant/cross-module effect closure remain open.

## Phase 25.3 continuation — catch-all bare-reraise effects

Bare reraises under `recover:` now recover explicit variants from the paired
`try` body, including variants propagated by ordinary helper calls, and report
them as escaping unless an outer arm handles them. The minimized catch-all
`reraiser()` reports `enum:Trouble:Bad`, matching C++.

All eleven Java unit classes pass in ABI-v9. This remains bounded to explicit
raised variants and ordinary helper calls; generic effects and broader failure
arm reasoning remain incomplete.

## Phase 25.3 continuation — grouped recovery effect precision

Bare reraises from a grouped recover arm are now intersected with variants
actually derived from the paired try body. The C++/Java query for
`regroup_a` in `phase21_addendum_grouped_a.moss` reports only
`enum:Failure:A`, despite the arm also listing `Failure.B`. All eleven Java
unit tests pass in ABI-v9. Other indirect/callback and foreign effect sources
remain open.

## Phase 25.3 continuation — resolve local receiver type for method effects

Function effect queries now follow object method calls through local receivers
when the checked local binding identifies the receiver's nominal type. The
arbitrary `calculate()` caller that constructs `Box` and invokes
`box.source_fault()` reports `enum:Trouble:Bad` under both C++ and Java.

All eleven Java unit tests pass in ABI-v9 and staged ABI-v10. Field-chain
receiver typing, generic method effects, and cross-module closure remain open.

## Phase 25.3 continuation — root typed-raise closure across direct, function, and method calls

A focused comparison found that Java accepted uncaught typed raises from
`main` and missed some raise propagation through ordinary helpers and object
methods. The root checker now rejects direct raises unless the exact enclosing
recover arm (or catch-all) handles them; an unrelated try does not mask a later
raise. It follows ordinary function call chains, checked local object receiver
paths through declared fields, straight-line method aliases, nested self-field
method calls, and ordinary helper calls made by methods. Recovery inside a
function or method is applied at that call site before propagating the variant.

Minimal C++/Java checks agree for direct main raises, direct field-chain method
raises, `main → relay → fail`, `main → Box.relay → Sink.fail`,
`main → invoke_method → Box.relay → Sink.fail`, straight-line aliases, and
`main → Box.relay → fail_helper`. Variant-specific and catch-all recovery
controls remain accepted. All eleven Java unit-test classes pass under ABI-v9
and staged ABI-v10; `check_agent_feedback.py` and `git diff --check` pass.

This remains bounded root-closure evidence. Branch-sensitive or loop aliases,
generic substitutions, imported/source-free raise summaries, and complete
checker-owned effect closure remain open. Commits `cc8aad57` through `8416f932`
are pushed to `phase25-3-java-j2`; no native, SDK, multi-crate, or full release
gate ran.


## Phase 25.3 continuation — propagate sibling method receiver writes and preserve unit codegen

A valid `Box.run` method calling `self.bump`, where `bump` writes
`self.value`, exposed a concrete Java/C++ difference: C++ reported `self` as
WRITE and generated `run(&mut self) -> ()`; Java reported READ and emitted
`run(&self) -> i64` even though its checked query type was unit. Java now counts
`self.field` assignments as receiver writes, propagates WRITE through
same-owner sibling calls, uses the checked receiver mode in effects queries,
and honors the checked unit result in native generation.

The focused query and generated-signature comparison now agree. All eleven
Java unit classes pass under ABI-v9 and staged ABI-v10; feedback validation and
`git diff --check` pass. Generated Rust was inspected but not compiled. This
closes only the shown receiver/write and unit-return profile, not general
method effect or native backend parity.


### Follow-up — nested field receiver write closure

The sibling-method receiver-write slice also covers calls through nested
object fields. `Box.relay → self.inner.sink.bump()` is accepted by both
compilers and reports `self: WRITE` in the method effects query. The Java
checker resolves the statically declared self-field path to the mutating method
and closes receiver writes through it. All eleven Java unit classes pass under
ABI-v9 and staged ABI-v10. Alias, generic substitution, and cross-module
closures remain open.


### Follow-up — canonical nominal names in effects ownership rows

Effects ownership query serialization now preserves nominal spelling (for
example `Box`) instead of lowercasing it, while canonicalizing built-in and
collection names recursively. The nested-field receiver query now returns the
same `self: Box, WRITE` row under C++ and Java. All eleven Java unit-test
classes pass under ABI-v9; broader query schema/data parity remains open.


### Follow-up — method receiver consumption after moving an owned field

`Box.take_sink` moves `self.sink` into an owned local and calls a mutating
method on that local. The C++ checker reports the receiver as `CONSUME` and
rejects a later use of the caller's `box`; Java previously reported `READ` and
accepted the use. Java now infers receiver consumption from a direct owned
field move, propagates the mode through sibling and nested field method calls,
consumes the call receiver, serializes `CONSUME` in effects queries, and emits
the method receiver by value.

Focused C++/Java checks agree on the use-after-consume diagnostic, and their
effects queries both report `self: Box, CONSUME`. The Java codegen unit test
checks the by-value receiver. All eleven Java test classes pass under ABI-v9
and staged ABI-v10. No Rust compilation or native execution was run. Nested
consuming receiver expressions outside the direct receiver path,
control-flow-sensitive field moves, and generic/cross-module summaries remain
open.


### Follow-up — propagate consumption through nested method receivers

For `consume(box) → box.sink.take_payload()`, C++ rejected a later use of
`box`, while Java previously missed the nested receiver path and accepted it.
Java now resolves declared field types along the receiver expression, infers
the function parameter's CONSUME mode from the called method, and consumes the
root local at the nested method call. The focused effects query reports
`box: CONSUME` in Java and C++.

The semantic regression now covers this nested path alongside the direct
receiver case. All eleven Java test classes pass under ABI-v9 and staged
ABI-v10. Generic field substitutions and branch-sensitive receiver inference
remain open.


### Follow-up — detect owned moves from nested self fields

For `target = self.sink.payload`, C++ rejected a later use of the caller's
`box`; Java previously treated the method as READ because it only matched a
one-level `self.field` value. Java now walks the declared field path and uses
the final field's transfer type to infer a consuming method receiver.

The semantic test covers this nested extraction. The C++/Java focused
diagnostic agrees, and all eleven Java test classes pass under ABI-v9 and
staged ABI-v10. Generic substitutions, indexed paths, and branch-sensitive
moves remain open.


### Follow-up — infer receiver consumption from explicit field returns

For a method `return self.payload`, C++ rejected later use of the owning
caller while Java previously accepted it. Java now inspects explicit Return
expressions during receiver move inference and checks the full declared field
path's transfer type.

The semantic test covers the return form. C++/Java focused diagnostics agree,
and all eleven Java test classes pass under ABI-v9 and staged ABI-v10. Generic
substitutions and branch-sensitive moves remain open.


### Follow-up — propagate receiver-field moves through consuming helpers

For `return take(self.payload)`, where the helper returns its owned argument,
C++ inferred receiver CONSUME and rejected a later use of the caller while
Java previously accepted it. Java now checks ordinary function call arguments
against the helper's inferred parameter access modes and the receiver field's
transfer type.

The semantic test covers the helper call. C++/Java focused diagnostics agree,
and all eleven Java test classes pass under ABI-v9 and staged ABI-v10.
Indirect helper closure, generic field substitutions, and branch-sensitive
moves remain open.


### Follow-up — close ordinary function consume summaries transitively

For `Box.take_payload → relay → take`, where `relay(value)` forwards its
owned parameter to a consuming helper, C++ rejected a later use of the caller
while Java previously accepted it. Java now iterates function parameter
consume modes to a fixed point and uses the resulting summaries before
receiver mode inference.

The semantic test covers the two-helper chain. Focused C++/Java diagnostics
agree, and all eleven Java test classes pass under ABI-v9 and staged ABI-v10.
Generic/imported function summaries and branch-sensitive aliases remain open.
The summary scan respects lexical `case` binders so a shadowed local does not
count as a consume of a same-named parameter.
The regression declares the forwarding helper before its consuming callee to
verify the fixed-point result is independent of function declaration order.


### Follow-up — summarize consuming method arguments

For `relay(value, box) -> box.accept(value)`, C++ inferred that `relay`
consumes its first parameter when `Box.accept` consumes and returns the
argument. Java previously left the parameter in READ mode. Function consume
summaries now resolve typed method receivers and inspect checked method
parameter modes before closing ordinary function summaries.

The focused Java regression and C++/Java diagnostic pair cover the case.
All eleven Java test classes pass under ABI-v9 and staged ABI-v10. Generic
receiver substitutions and imported method summaries remain open.


### Follow-up — summarize object constructor argument consumption

For `wrap(value) -> Box(payload=value)`, C++ inferred that `wrap` consumes
the owned parameter while Java previously left it in READ mode. Java consume
summary inference now recognizes constructor arguments, strips named argument
labels, and marks a transferred parameter CONSUME.

The focused Java regression and C++/Java diagnostic pair cover caller reuse.
All eleven Java test classes pass under ABI-v9 and staged ABI-v10. Generic
object construction and branch-sensitive aliases remain open.


### Follow-up — source-free ABI-v10 consuming receiver metadata

The source-free object test now encodes and imports an ABI-v10 method template
with receiver `CONSUME`, checks that mode survives loading, and verifies a
consumer cannot use the receiver after the call. The focused
`SourceFreeObjectMetadataTest` passes. This covers metadata and semantic call
handling only; native source-free linkage remains a final acceptance gate.


### Follow-up — propagate function WRITE summaries through helper calls

For `relay(values) -> push_one(values)`, C++ reported `values: WRITE` while
Java reported READ. The Java access-mode closure now propagates WRITE through
ordinary function arguments, including when the forwarding function is
declared before the mutating callee.

The focused Java regression checks the resulting parameter mode, and the
C++/Java effects queries agree. All eleven Java test classes pass under
ABI-v9 and staged ABI-v10. Imported helper and nested field write summaries
remain open.


### Follow-up — summarize nested method receiver writes

For `relay(box) -> box.sink.bump()`, C++ reported `box: WRITE` while Java
reported READ. Java function parameter inference now resolves declared field
paths and propagates a nested method receiver's WRITE mode to the root
parameter.

The semantic test covers the nested call, and focused C++/Java effects queries
agree. All eleven Java test classes pass under ABI-v9 and staged ABI-v10.
Local alias and generic/cross-module receiver summaries remain open.


### Follow-up — use checked types for inferred function consume summaries

For inferred `take(value) -> value` and `relay(value) -> take(value)`, C++
rejected later reuse of a concrete owned `Payload` while Java accepted it.
Java now uses the retained checked parameter TypeRef when the source has no
annotation, allowing the function consume summary to propagate through the
generic helper chain.

The semantic test covers owned `Payload` rejection and confirms an inferred
generic identity with an `Int` remains reusable. All eleven Java test classes
pass under ABI-v9 and staged ABI-v10. Generic cross-module summaries remain
open.
