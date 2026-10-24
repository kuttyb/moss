# Phase 25.3 J2 — Compiler Parity Gap Ledger

**Directive:** L7 Directive on Semantic Fidelity and Mechanical Port Completion  
**Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`  
**Working Branch:** `phase25-3-java-j2`  
**Audit Date:** 2026-10-24  

---

## 1. Subsystem Parity Gap Matrix

| Subsystem | C++ Authoritative Implementation | Existing Java Implementation | Missing Invariants / Identified Gaps | Severity | Assigned Agent | Status | Required Parity Tests |
|---|---|---|---|---|---|---|---|
| **Expression Typing & TypeSolver Integration** | `src/semantic_checker.cpp#L10100-L10500`, `src/type_ir.cpp#L1-L180`, `src/type_ir.hpp` | `compiler-java/.../semantics/SemanticChecker.java` | `SemanticChecker.inferExprType()` used ad-hoc string heuristics and silently defaulted unrecognized expressions to `"int"`. `TypeSolver`, `TypeRef`, and `MemberClosure` exist in `org.mosslang.compiler.types` but were NOT connected to AST statement/expression checking. | **P1** | Agent A | IN PROGRESS | Negative type mismatch tests, call argument validation, return type checking, recursive expression typing. |
| **Compiler-Derived 2PL Synchronization Plan** | `src/native_generator.cpp#L300-L800`, `src/native_generator.cpp#L3800-L4500` | `compiler-java/.../backend/NativeGenerator.java` | Java backend used a single coarse `RwLock` around the entire domain state rather than compiler-derived synchronization classes (`(domain_rank, class_rank)` lock ordering, 2PL handler entry/exit acquisition, read vs write splitting). | **P1** | Agent A | IN PROGRESS | Concurrency locking tests, multi-domain message dispatch, read/write contention hook events. |
| **Differential Test Harness & Negative Corpus** | `tests/run.sh`, `tests/*.moss`, `tests/phase25/` | `tests/differential/run_differential.py` | Differential harness only exercised 33 programs in `examples/*.moss` (mostly positive). Lacks systematic validation of compile error codes, negative type rejections, syntax errors, and runtime artifacts. | **P1** | Agent B | IN PROGRESS | Negative semantic tests (type mismatches, unclosed types, undefined variables), exit code and diagnostic matching. |
| **Functional Pipeline Optimizer** | `src/functional_optimizer.cpp#L1-L1398`, `src/functional_ir.hpp` | `compiler-java/.../optimizer/` (Milestone J2-A) | J1 lacked pipeline stage recognition and fusion (`compose-map`, `compose-filter`, terminal reductions `count`, `sum`, `any`, `all`). | **P2** | Agent B | TRANSLATED | Adjacent stage fusion tests, filter composition, terminal reduction evaluation. |
| **ABI-v10 Checked-Body & `.mossi` Codec** | `src/checked_body_ir.cpp#L1-L541`, `src/checked_body_ir.hpp#L1-L1797` | `compiler-java/.../abi/CheckedBodyCodec.java` | Encoder and SHA-256 hash generator implemented; needs deserialization reader (`H3BodyReader`), `.mossi` file I/O, and cross-module interface verification. | **P2** | Agent B | TRANSLATED | Checked body serialization round-trip, hash determinism, `.mossi` header validation. |
| **Semantic Queries & Agent Protocol** | `src/moss.cpp#L8500-L9200`, `src/typed_semantic_api.cpp` | `compiler-java/.../cli/AgentBootstrap.java`, `query/SemanticQueryEngine.java` | Implemented `agent bootstrap --json`, `inspect`, `type`, `effects`, `ownership`, `calls`, `why`, `fmt` matching canonical `moss-agent-1` JSON protocol. | **P3** | Agent A | TESTED | Differential query tests against C++ `./moss`. |
| **Eclipse Cache Artifact Tracking** | `.gitignore` | `compiler-java/jdt/configuration/` | Ephemeral runtime files (`.tmp*.instance`, `.fileTable.*`, `framework.info.*`) were tracked in git. | **P3** | Agent A | PARITY VERIFIED | Clean `git status` with zero untracked runtime artifacts. |

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
