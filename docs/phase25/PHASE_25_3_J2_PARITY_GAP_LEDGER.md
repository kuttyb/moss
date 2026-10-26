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


### Follow-up — summarize method WRITE arguments

For `relay(values, box) -> box.append_to(values)`, C++ reported
`values: WRITE` while Java reported READ, even though the method mutates the
parameter. Java function summaries now resolve the static method receiver and
inspect its checked argument access modes, propagating WRITE to the forwarded
parameter.

The semantic test covers the call and the focused C++/Java effects queries
agree. All eleven Java test classes pass under ABI-v9 and staged ABI-v10.
Imported and generic method argument summaries remain open.


### Follow-up — summarize nested field moves from function parameters

For `relay(box)` assigning `target = box.sink` and mutating `target`, C++
rejected later use of the caller's `box`, while Java accepted it. Java function
consume inference now resolves nested field paths in assignment, return, and
implicit result expressions, then checks the moved value's transfer type.

The semantic regression covers this owned-field move and confirms reading a
Copy `Int` field does not consume its owner. Focused C++/Java diagnostics
agree. All eleven Java test classes pass under ABI-v9 and staged ABI-v10;
alias and branch-sensitive field moves remain open.


### Follow-up — retain WRITE for inferred method receivers

For `fn relay(counter): counter.bump()` with a concrete `Counter` callsite,
C++ reported `counter: WRITE`, while Java reported READ because its checked
parameter type remained a placeholder. Java now preserves WRITE for a direct
inferred-parameter receiver call when a matching object method requires WRITE.
The focused C++/Java effects queries agree. All eleven Java test classes pass
under ABI-v9 and staged ABI-v10. Ambiguous same-name methods across structural
receiver types remain an area for focused coverage. C++'s standalone `effects`
query still labels the unresolved `relay(box)` parameter READ even though its
caller-use check rejects reuse after `box.take()`; the diagnostic behavior
agrees, but the query discrepancy remains open.


### Follow-up — specialize inferred CONSUME at callsites

Generic receiver methods sharing a name can have different access modes.
Java now checks the actual argument TypeRef for each call: a `Mover` whose
method consumes is consumed, while a same-name read-only `Reader` remains
reusable. The focused caller diagnostics match C++ for both cases. All eleven
Java test classes pass under ABI-v9 and staged ABI-v10. Cross-module method
resolution remains open; the C++ standalone `effects` query's READ summary
discrepancy remains recorded above.


### Follow-up — preserve source-free access modes

An imported ABI-v10 function retained CONSUME when loaded, but semantic
checking reset its parameter mode to READ and allowed caller reuse. The Java
checker now preserves source-free parameter modes and applies ABI metadata
WRITE/CONSUME at calls. Tests cover ABI-v9 and ABI-v10 caller rejection after
consumption, propagation through a local `forward` helper, and an ABI-v10
WRITE/READ alias conflict. All eleven Java
test classes pass under ABI-v9 and staged ABI-v10; native source-free linkage
remains a final acceptance gate.


### Follow-up — rebuild standalone package after compiler changes

Rebuilt `compiler-java/dist/moss.jar` and the launcher from the current Java
sources. The packaged bootstrap reports `language_version: moss-0.1`; a valid
inferred-WRITE example checks successfully, and the consuming-receiver reuse
example is rejected with `OWNERSHIP_USE_AFTER_CONSUME`. This is a packaging
smoke check, not native Rust or Margo integration evidence.

### Phase 25.3 continuation — preserve source-free typed raise metadata

An ABI-v9 imported function or object method with
`raise_set=enum:Fault:Bad` was accepted in `main` without recovery because
there was no body from which to infer raises. Java now retains source-free
enum variants for both callables and includes them in root raise closure.
Regressions reject unhandled direct and helper-forwarded calls and accept exact
`recover Fault.Bad` arms.
All eleven Java test classes pass under ABI-v9 and staged ABI-v10. Multi-variant
ABI encoding and source-free native execution remain open.


### Follow-up — source-free method consume through inferred helper parameters

Added ABI-v9 and ABI-v10 consumer regressions where an imported method consumes
an untyped helper parameter and a caller attempts to reuse that value after the
helper returns. Both source-free method metadata forms preserve the CONSUME
mode through the inferred helper summary, and Java rejects the caller reuse with
`OWNERSHIP_USE_AFTER_CONSUME`. This narrows the generic/imported method-summary
gap for concrete imported receivers; generic receiver substitutions and native
source-free linkage remain open.

The focused `SourceFreeObjectMetadataTest` passes under the staged ABI-v10
environment. Full Java test suites under both ABI modes were last run before
this test-only addition.


### Follow-up — specialize method argument ownership at generic callsites

A generic helper `relay(value, item) -> item.accept(value)` exposed a false
acceptance: Java accepted caller reuse for a concrete `Mover.accept` that
consumes its argument, while C++ rejected it. Java now resolves the generic
helper's method receiver against the actual receiver argument at each callsite
before applying the selected method's argument access modes. A same-name
read-only `Reader.accept` specialization remains reusable, so the fix does not
widen the helper's summary to CONSUME when candidates disagree.

The regressions cover both receiver specializations. The minimized C++/Java
checker pair agrees, all 12 Java test classes pass under ABI-v9 and staged
ABI-v10, and the rebuilt standalone JAR reproduces both outcomes. Generic
receiver substitution beyond this bounded callsite path and native acceptance
remain open.


### Follow-up — carry inferred receiver identity through local aliases

A helper-local alias (`alias = item; alias.accept(value)`) hid the receiver
parameter from callsite ownership specialization. Java previously accepted a
read after a concrete consuming Mover call, while C++ rejected it. The caller
resolver now follows straight-line local bindings back to their helper
parameter and applies the concrete receiver's method access mode. Matching
Mover and Reader controls agree across C++ and Java; branch-dependent and
nested-field alias flows remain open.

All 12 Java test classes pass under ABI-v9 and staged ABI-v10. The rebuilt
standalone compiler rejects the Mover alias callsite and accepts the Reader
alias control. This is checker/package evidence only; native execution and
broader ownership acceptance remain open.


### Open discrepancy — C++ effect closure for a branch-assigned receiver alias

A minimized generic helper assigns `alias = item` in both sides of an `if`,
then calls `alias.accept(value)`. With only the consuming `Mover.accept`
implementation, C++ accepts subsequent reuse of `value`; its `effects relay`
query reports `value: READ` and `unresolved: true`. Java rejects the reuse with
`OWNERSHIP_USE_AFTER_CONSUME`, using the selected receiver method's CONSUME
mode. The Java result follows the concrete selected method, but the C++ baseline
verdict/effect closure disagrees. No generated Rust compilation or native run
was performed, so this remains an unresolved acceptance discrepancy rather
than a parity claim.


### Follow-up — vector iteration trace structure

A focused `for value in values` vector probe produced the same stdout and
structured trace events in C++ and Java: each body read is nested under its
corresponding `LoopIteration`. Java's `FastInterpreterTest` now asserts this
structure for a two-element vector. The focused test passes. Queue iteration is
rejected by both checkers as not statically iterable; vector and range iteration
are the accepted forms covered here. Other collection, message, state, and
cross-project Fast Debug paths remain open.


### Follow-up — custom iterator Fast Debug trace

The accepted `Countdown.next() -> option[Int]` custom iterator also executes in
Java Fast Debug, with two yielded values and each body read nested under its
`LoopIteration`. The existing custom iterator unit regression now checks both
output and per-iteration trace context. The focused `FastInterpreterTest` passes.
The C++ Fast Debug probe for the same Moss program reports `Fast Debug cannot
iterate this checked source`, so this confirms Java execution coverage rather than
parity with the C++ interpreter.


### Follow-up — READ-only branch-assigned receiver control

The branch-assigned generic receiver alias was rechecked with a concrete
`Reader.accept` implementation whose argument mode is READ. Both C++ and Java
accept the later payload read. `SemanticCheckerTest` now retains this positive
control alongside the consuming Mover negative. This narrows the recorded
C++/Java discrepancy to the selected CONSUME method path; it does not resolve
the C++ unresolved-effect result. The focused semantic test passes.


### Follow-up — multi-variant raise query recovery

A focused Moss source raises two enum variants in separate branches. C++ and
Java `effects` queries agree that the source function raises both variants, a
wrapper recovering one variant retains only the other, and a wrapper recovering
both reports an empty raise set. `SemanticQueryEngineTest` now covers all three
outcomes. The focused test and both compilers' minimized probes pass. Generic,
callback, and cross-module effect closure remain open.


### Correction — raises through named pipeline callbacks

A Moss function using `map(risky)` where `risky` raises a typed enum case was
accepted by both checkers, but C++'s effects query reported the callback's
escaping raise while Java returned an empty set. Java effects traversal now
recognizes named function callbacks in `map`, `filter`, `any`, `all`, and the
callback position of `reduce`. Regression coverage checks all five forms and a
wrapper recovering the callback's raise. Focused Java tests and minimized
C++/Java query probes agree. Bound-method callbacks, generic callback
specialization, and cross-module callback effects remain open. This corrects
query reporting only; native execution and root raise checking for callback
invocation have not been validated here.


### Correction — enforce root recovery for callback and value-returning raises

A raising function referenced as `map(risky)` produced an effects-query delta,
then exposed a checker defect: Java allowed the call from `main` to escape
unhandled, while C++ rejected it. Root function-raise collection now follows
named callbacks in all recognized pipeline stages. It also checks raises from
value-returning functions; the previous return-type filter allowed such failures
to escape. Focused semantic tests cover rejection without recovery and acceptance
with a matching `recover`. An existing return-carrier test now recovers its
raised variant, matching the C++ root contract. Focused Java semantic tests pass.
Rust compilation and execution remain deferred.

Validation refresh: after the root callback raise correction and shared pipeline
callback parser, `scripts/java/test.sh` and
`MOSS_STAGED_ABI=v10 scripts/java/test.sh` each pass all 12 Java test classes.
No Rust compilation/native execution or SDK gate was run.


### Follow-up — bound-method pipeline callback raises

The raise traversal was extended to callbacks such as `map(box.risky)`. It uses
the caller's checked receiver type to select the method, propagates its raise
through helper functions and direct `main` pipelines, and applies matching
recovery. Semantic query traversal now includes the same bound-method callback
path. Tests cover unhandled and recovered direct/helper calls plus query output.
The focused semantic/query tests pass. With recovery in place, minimized C++
and Java effects queries agree for the method callback and its recovered
wrapper. This remains bounded to locally resolved receivers; generic and
cross-module callback methods remain open.

Validation refresh after bound-method callback handling: all 12 Java test
classes pass under ABI-v9 and staged ABI-v10; the added direct-main positive
and negative controls pass in `SemanticCheckerTest`.


### Follow-up — require concrete receiver type for method callback references

A generic helper used `map(box.risky)` with an untyped `box`. C++ rejects this
callback because the receiver type is unresolved; Java had accepted it and
emitted a generic macro body. Java now fails closed for an unresolved bound
method callback receiver while retaining the concrete `Box` control. Both
checkers reject the untyped form and accept the concrete source. A same-named
global function and method also exposed raise-set overcounting: dotted method
callbacks were matching global function suffixes. Function callback matching
now requires a bare or `::`-qualified function name, and method callback tests
verify only the selected method's raise set. All 12 Java test classes pass under
ABI-v9 and staged ABI-v10.


### Correction — method callback raises at domain root ingress

A `Worker.Run` handler used `values |> map(box.risky)` where the method raises
`Failure.Bad`. C++ rejected a root message to that handler without `on_fail`;
Java accepted it. Root ingress validation now resolves callback receiver types
from checked handler locals and includes the selected method's raise variants.
Function calls at a root also use the richer function raise closure, which
accounts for supported callback/method paths and local recovery. Tests reject
the unhandled method callback and accept an exhaustive `on_fail` arm. C++ and
Java accept the handled minimized source. All 12 Java test classes pass under
ABI-v9 and staged ABI-v10.


### Follow-up — ordinary method raises at domain root ingress

The same root validation gap also affected ordinary handler expressions such as
`value = box.risky(1)`, independently of pipelines. A C++/Java probe showed C++
rejecting the unhandled root while Java accepted it. Root ingress validation now
scans direct object-method calls, resolves their receiver from checked handler
bindings, and includes the selected method's escaping raises. The regression
checks rejection without `on_fail` and acceptance with `Failure.Bad`. All 12
Java test classes pass under ABI-v9 and staged ABI-v10.


### Follow-up — contextual method raises through generic helpers

A handler calling `invoke(box)`, where `invoke` has an untyped parameter and
calls `box.risky(1)`, exposed another Java root-ingress false acceptance:
C++ rejected the unhandled `Failure.Bad`, Java accepted it. Root raise collection
now resolves helper parameter receiver types from the checked handler callsite
and scans the helper body for direct and pipeline method calls. The focused
`SemanticCheckerTest` passes. Minimized C++ and Java probes both reject the
unhandled form and accept the matching `on_fail` control. This does not yet
cover transitive generic helper chains or provide native execution evidence.


### Correction — transitive generic helper method raises

A follow-up source chained `invoke(box) -> forward(box) -> box.risky(1)` and
showed that the direct-helper correction still left Java accepting an
unhandled root rejected by C++. Root validation now propagates contextual
receiver types recursively through directly called helpers, with recursion
deduplicated by helper and contextual type bindings. The focused semantic test
passes. Minimized C++/Java checks agree on rejection without recovery and
acceptance with `on_fail`. This does not establish parity for unrelated
contextual effects or native execution.


### Correction — local aliases through generic helper chains

The transitive helper path still missed a straight-line alias in the first
helper: `alias = box; forward(alias)`. C++ rejected the unhandled root while
Java accepted it. The contextual raise walker now carries simple top-level
local aliases before following nested helper calls. `SemanticCheckerTest`
passes; minimized C++/Java checks now reject the unhandled alias chain and
accept the matching `on_fail` control. Branch-dependent aliases and native
execution remain open.


### Correction — same-type branch aliases through helper chains

The helper-chain receiver was assigned `alias = box` in both arms of an
`if/else`; Java still accepted the unhandled root while C++ rejected it. Root
raise traversal now joins aliases only when both top-level arms resolve the
same alias name to the same concrete receiver type, then forwards that type to
subsequent helpers. `SemanticCheckerTest` passes; C++ and Java reject the
unhandled case and accept its `on_fail` control. Different-type and nested
branch joins remain open.


### Correction — indexed string-key Fast Debug trace detail

`examples/collections.moss` produced identical output in C++ and Java Fast
Debug, but the Java `LocalRead` trace rendered `prices[NVDA]` instead of
retaining the source expression `prices["NVDA"]`. Java now records the source
index expression in local collection-read details while using the evaluated
index for lookup and state paths. `FastInterpreterTest` passes, and the full
stdout and trace now compare byte-for-byte for the example. Traces for
`phase224_debug_nested.moss`, `sequential_messages.moss`, and
`state_payload_snapshot.moss` also matched in focused sampling; broad trace and
native acceptance remain open.


### Validation — Java Margo test and benchmark execution

The Java Margo launcher ran `examples/projects/phase7_demo`: both declared
tests passed, and all four declared benchmark bodies returned measured results
from five warmups and 31 samples each. The `phase7_failing_test` project
returned exit 1 with one failing assertion and one passing test. This confirms
actual interpreter-backed project test and benchmark execution rather than
placeholder success for these small projects. Native project builds,
multi-crate integration, and SDK validation remain deferred.


### Correction — unresolved generic method receiver effects queries

For `fn invoke(box): result = box.risky(1)`, C++ effects output reports the
unannotated parameter type as unknown and marks the summary
`conservative_unresolved`; Java had fabricated `Int` and reported certainty
`resolved`. Java ownership query serialization now preserves unknown parameter
types as `null`, and effect-query traversal marks an object-method receiver
unresolved when its nominal type cannot be established. `SemanticQueryEngineTest`
passes, and minimized C++/Java queries agree on these fields. This is not full
effects-query schema or dimension parity.


### Validation — packaged compiler and optional JDT daemon

After the semantic-query correction, `scripts/java/package.sh` rebuilt the
standalone JAR and launcher. The packaged compiler bootstrap reports
`moss-0.1`, and its effects query agrees with C++ on the unresolved generic
method receiver fields. All six `scripts/java/test_jdt_client.py` protocol
lifecycle tests pass. A live JDT daemon could not be started because no Eclipse
executable is installed at the checked standard paths or on PATH. This leaves
live daemon startup/build/stop unverified; native and SDK acceptance remain
deferred.


### Correction — repeated semantic call query candidates

The existing semantic-query tool failed because one syntactic call at a source
line appeared three times among Java's resolution candidates; two same-target
calls on one line appeared six times in Java call queries. Repeated type-check
inference had accumulated duplicate internal call edges. Query projection now
caps same-source/target/line edges at the number of syntactic calls on that
line, masking comments and string literals. Genuine repeated calls remain
distinct. `SemanticQueryEngineTest` and
`tests/tooling/check_semantic_queries.py scripts/java/run.sh` pass.
When combined project programs do not retain `sourceLines`, the query derives
the call count from the matching source callable's original statement text; a
unit regression clears `Program.sourceLines` to cover that fallback.


### Correction — stable warning for large owned message payloads

Java previously omitted `MESSAGE_PAYLOAD_COPY_LARGE` for the existing large
payload fixture. The semantic checker now estimates fixed-size payload size
through nominal object fields and `option`, and emits the warning at exported
domain message boundaries when the known payload exceeds 1024 bytes. All 12
Java test classes pass. The agent API checker reached its later statement
effects assertions after passing the payload-warning assertion, then failed
because the statement target lacked `construct_kind`; this separate API-query
gap remains open. Generated Rust and native behavior are not validated by this
warning-only correction.


### Correction — source location effects and concrete domain graph queries

The agent API regression showed that `effects line:50` fell through callable
resolution and returned a fabricated function target. Java now resolves
binding locations for effects queries, reports the binding target and keeps
statement effects separate from enclosing-callable effects. Callable effect
results explicitly include `enclosing_callable_effects: null`. Inspection no
longer reports a hardcoded empty concrete graph: the graph is derived from
main's concrete domain constructors and route arguments. On
`tests/phase6_agent_api.moss`, Java and C++ graph JSON match exactly, including
graph identity, instance ranks, and the route edge. All 12 Java test classes
pass. The full agent API script progresses through topology and currently fails
at optimized `why main@42:expression:0 -O`, which exits 1 without output. The
query implementation remains incomplete beyond this covered graph profile.


### Correction — advertised semantic query regression paths

The Phase 6A agent API regression now passes end to end, as does
`tests/tooling/check_semantic_queries.py`. The fixes cover source-location
binding effects and types, pipeline-local effects and optimizer explanations,
continuation-line callable references, generic specialization location
selection, structured missing-source errors, and callable ownership alias
fields. Java now derives concrete domain graph instances and route edges; on the
agent API fixture, its full graph JSON matches C++ including identity and rank
values. Pipeline effects use callback summaries and the existing functional
optimizer decision logic. This demonstrates the tested query paths, not complete
semantic-query schema or language parity. The Java suite passes all 12 test
classes against these changes; native/backend and SDK acceptance remain
deferred.


### Clarification — branch-joined receiver ownership baseline delta

The branch-alias case with one `Mover.accept(value: Payload) -> Payload`
implementation still has a C++/Java acceptance difference. Current C++ accepts
later reuse and reports the helper parameter as READ with an unresolved effect
summary. Java reports CONSUME and rejects the later use as
`OWNERSHIP_USE_AFTER_CONSUME`. An unused `Reader.accept` implementation changes
overload resolution and both compilers accept that control. The selected
Mover method takes an owned Payload by value, so the Java result is consistent
with consuming semantics; the C++ unresolved summary suggests its result may be
an under-inference. Keep this as an unresolved baseline delta until native
behavior is checked at the frozen-candidate gate; do not weaken Java's ownership
check based solely on this C++ acceptance.


### Correction — enforce branch-joined inferred parameter constraints at calls

A generic helper assigned `alias = left` and `alias = right` on opposite branch
arms. Its checked parameter types shared one inference variable, but Java did
not enforce that equality at concrete calls. C++ rejected distinct `A`/`B`
arguments with `TYPE_MISMATCH`; Java accepted them. This also occurred when a
domain handler called the helper before function-body inference had completed.
Java now checks shared inferred parameter identities at call inference and
revalidates retained semantic call edges after all function constraints are
known. Both compilers reject the minimal and domain-handler mismatch, while a
same-type call remains accepted. This is targeted callsite constraint
preservation, not general specialized-body rechecking.

Validation: all 12 Java test classes pass under ABI-v9 and staged ABI-v10;
`tests/differential/run_differential.py --mode check_json
tests/differential/corpus` passes all 8 files; Phase 22.3 semantic-query and
Phase 6A agent API checks pass. Native and SDK acceptance remain deferred.

### Follow-up — statically closed handles for inferred domain specializations

The accepted `specialized_domain_routes.moss` case exposed a native codegen gap:
Java materialized per-instance inferred domain layouts but left route fields
pointing at the unspecialized `WorkerRef`, while the specialized instance had a
distinct ref type. Java now records the source declaration for each
materialized specialization and emits a closed `WorkerHandle` enum/struct for a
nominal routed domain with specialized variants. Constructor route values are
converted into that handle, and compatible handler root calls dispatch across
the statically known variants. The focused regression covers an Int base
instance and Float specialization routed through one coordinator.

Validation: all 12 Java unit classes pass under ABI-v9 and staged ABI-v10.
Generated Rust has the expected typed route fields, conversions, and compatible
handler dispatch. Rust compilation/native execution remain deferred; the
frozen-candidate gate is still required.

### Follow-up — synchronous root ingress through active Java Executor

Checked main-level message statements now lower through `runtime_invoke`,
including imported ABI-v9 source-free handlers via their generated message
bridge. The runtime queues an active root and synchronously returns its reply;
when no Executor is active it runs inline. FIFO tickets serialize synchronous
ingress, startup coordinates with an inline root, queue admissions are reserved
against `join()`, and ingress arriving during draining waits for the post-join
inline state. Inline panics fail closed. Regressions require an active root
message between `Executor.start()` and `Executor.join()`, including source-free
bridge dispatch.

Validation: all 12 Java test classes pass in ABI-v9 and staged ABI-v10.
`scripts/java/package.sh` and the packaged launcher pass a focused generation
check; all six mocked JDT client lifecycle tests pass. A live Eclipse daemon is
unavailable in this environment. Generated Rust has not been compiled or run.
Physical worker compensation, runtime Root identity/context, and native
Executor behavior remain open; this is not Executor parity evidence.

### Correction — Java Fast Debug Executor schedule

Running `tests/phase20_executor_multi_root.moss` through both interpreters
initially produced C++ output `10` and Java output `0`: Java ignored the
Executor construction, `invoke`, and `join` statements. Java Fast Debug now
evaluates Executor configuration arguments at construction, snapshots each
domain target and handler argument at submission, drains queued roots in order
at `join`, and emits `executor_start`, `root_submit`, and `executor_join`
events. The existing Phase 20 fixture produces output `10`, and its structured
trace compares byte-for-byte with C++ including state events. All 12 Java test
classes pass under ABI-v9 and staged ABI-v10. This is deterministic
interpreter behavior only; native Executor execution remains unverified.

A focused second probe changes a local after its first submission. Both
interpreters preserve the first value, return `18`, and emit identical
stdout/trace files, confirming submit-time argument snapshots as well as
submission-order execution.

### Correction — direct `moss debug` project-module targets

The existing Phase 10 Fast Debug project-closure check failed for Java at
`moss debug app`: Java treated `app` as a source filename and returned
`cannot open app`, while the command contract resolves a project module target.
Java now resolves a missing debug target against the current package's declared
Moss sources by module name or source stem, then uses the same checked source
closure and interpreter path. `check_phase10_fast_debug_project.py` passes
with the Java launcher, covering both `debug app` and `run --interp` across a
three-module closure. The actual `phase7_demo` Margo debug command also prints
`42`. All 12 Java test classes pass under ABI-v9 and staged ABI-v10. Cross-
package and source-free Fast Debug constraints remain as documented.

### Correction — canonical imported function names in Fast Debug traces

A package-resolved Fast Debug probe showed Java executing the imported function
correctly but tracing its loader-qualified `graphpkg.double` name, while C++
traced the canonical callable symbol `graphpkg__double`. Java now canonicalizes
qualified function names for FunctionEnter/Exit, body events, and semantic
identities. The unit test models the loader's qualified function record instead
of merging an unqualified parsed provider function.

Validation: all 12 Java test classes pass under ABI-v9 and staged ABI-v10. The
`phase15_8/pkg_main` cross-package Fast Debug run returns the same stdout and
byte-identical full trace in C++ and Java, with `RUSTC` pointed at a nonexistent
path to confirm this stayed on the interpreter path. `git diff --check` passes.
This closes the tested imported-function trace naming case only; broad Fast
Debug and native/backend parity remain open.

### Correction — apply Executor affinity and priority hints

The Java generated Executor runtime retained validated `affinity` and
`priority` settings but did not apply them to worker threads. It now applies
the C++ runtime's best-effort Linux CPU affinity and scheduling-priority hints
at worker startup, warns when Linux refuses them, and warns that they are
unsupported on non-Linux targets. The hints are cloned into each worker, so
every worker receives the configured policy.

Validation: all 12 Java test classes pass under ABI-v9 and staged ABI-v10; the
Executor generation regression checks configuration capture, worker startup,
Linux calls, and fallback warning code. `git diff --check` passes. Generated
Rust has not been compiled or executed, so platform-specific behavior remains
unverified until the frozen-candidate backend gate. Blocking compensation and
Root identity/context gaps are still open.

### Correction — establish Java runtime Root identity for ingress

The Java-emitted runtime now allocates process-wide monotonic Root IDs and
installs/restores a thread-local current Root around Executor jobs and both
active and inline `runtime_invoke` work. Executor enqueue and host ingress now
fail closed before admission when called from an already active Root, matching
the root-ingress invariant in `ROOT_RUNTIME_ABI.md`.

Validation: all 12 Java test classes pass under ABI-v9 and staged ABI-v10. The
generated-runtime regression checks ID allocation, TLS context, wrapping for
each ingress mode, and both re-entry guards. No generated Rust compilation or
execution was run. This does not add branch context, Root telemetry, or bounded
blocking compensation; those remain open runtime gaps for final acceptance.

### Correction — RangeBatch index and iteration semantic coverage

The positive Phase 20 FileIO integration fixture was accepted by C++ but Java
rejected `RangeBatch` indexing and then `for` iteration. Java now requires an
Int batch index and types `batch[index]` as `Range`; `for part in batch` binds
`part` as `Range`. A focused regression also rejects a Bool index. After a
fresh Java build, both C++ and Java `--check` accept
`tests/tooling/fixtures/phase20_fileio_runtime_integration.moss`.

This closes only the checker surface exercised by that fixture. Generated
Java Rust still emits a length-only `__MossFile`/`__MossRange` stub: it drops
the FileIO open mode, does not implement writes or batch reads, and does not
emit the production `moss_fileio`/`moss_root_runtime` modules emitted by C++.
The compiler therefore remains blocked on real native FileIO support; no Rust
compilation or execution was run.
