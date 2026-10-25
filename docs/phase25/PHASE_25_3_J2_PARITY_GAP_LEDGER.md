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
