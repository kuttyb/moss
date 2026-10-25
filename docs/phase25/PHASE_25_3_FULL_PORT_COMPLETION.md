# Phase 25.3 — Full Java Compiler Port Completion Ledger

- **Directive:** L7 Autonomous Execution Directive — Continue Until Full Parity
- **Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`
- **Working Branch:** `phase25-3-java-j2`
- **Starting Point / Pinned Head SHA:** `466c4f175d51ad76670289f7106e6ede52680366`
- **Execution Model:** Continuous autonomous progression until full test/regression parity.

---

## 1. Subsystem Parity & Completion Matrix

| Subsystem | C++ Source of Truth | Java Implementation | Production Integration State | Applicable Regression Tests | Current Parity Status | Review Findings | Assigned Agent | Closure Commit |
|---|---|---|---|---|---|---|---|---|
| **Lexer & Tokens** | `src/lexer.cpp`, `src/tokens.hpp` | `compiler-java/.../parser/Lexer.java`, `Token.java` | **INTEGRATED** | `ParserTest.java`, differential tests, `examples/*.moss` | **FULL REGRESSION PARITY** | Tokens, spans, multiline continuation, comments verified across 33 example ASTs. | Agent B | `25bc920a` |
| **AST Hierarchy** | `src/ast.hpp`, `src/ast.cpp` | `compiler-java/.../ast/*` | **INTEGRATED** | `ParserTest.java`, `CheckedBodyCodecTest.java` | **FULL REGRESSION PARITY** | FastList AST representations, line tracking, structured nodes matching C++ AST. | Agent B | `25bc920a` |
| **Parser & Syntax** | `src/parser.cpp`, `src/parser.hpp` | `compiler-java/.../parser/Parser.java` | **INTEGRATED** | `ParserTest.java`, `check_retired_syntax.py`, `check_agent_skills.py` | **FULL REGRESSION PARITY** | Parses all 33 example programs and syntax fixtures with exact indentation/continuation rules. | Agent B | `25bc920a` |
| **TypeRef & TypeSolver Core** | `src/type_ir.cpp`, `src/type_ir.hpp` | `compiler-java/.../types/TypeRef.java`, `TypeSolver.java`, `TypeContext.java` | **INTEGRATED** | `TypeSolverTest.java`, `check_phase25_type_ir.py` | **FULL REGRESSION PARITY** | Complete unification, occurs check, substitution, fresh variables, nominal bindings, member closures. | Agent A | `3bf8b5da` |
| **Expression Typing & Semantics** | `src/semantic_checker.cpp#L10100-L10500`, `src/type_ir.cpp` | `compiler-java/.../semantics/ExpressionTyper.java`, `SemanticChecker.java` | **INTEGRATED** | `tests/differential/`, `check_agent_skills.py`, `check_borrowed_reads.py` | **DIFFERENTIAL PARITY** | Connected TypeSolver to AST checking; eliminated silent "int" default. Fail-closed type checking. | Agent A | `b2f2bd06` |
| **Domain Checking & Composition Root** | `src/semantic_checker.cpp#L8000-L9500` | `compiler-java/.../semantics/SemanticChecker.java` | **INTEGRATED** | `check_agent_skills.py`, `tests/differential/` | **FULL REGRESSION PARITY** | Enforces static composition in main prefix, tests prohibited from composing domains, pure state initializers. | Agent A | `613e91cf` |
| **Ownership, Effects & Immutability** | `src/semantic_checker.cpp#L3000-L5000` | `compiler-java/.../semantics/SemanticChecker.java` | **INTEGRATED** | `check_agent_skills.py`, `check_phase21_expression_ownership_edges.py` | **DIFFERENTIAL PARITY** | Move tracking, use-after-consume, immutable let protection, unsupported local annotations rejected. | Agent A | `613e91cf` |
| **Compiler-Derived 2PL Synchronization** | `src/native_generator.cpp#L300-L800` | `compiler-java/.../backend/NativeGenerator.java` | **INTEGRATED** | `check_handler_2pl.py`, `check_synchronization_plan.py` | **DIFFERENTIAL PARITY** | State partitioned into classes with `RwLock`s. Scoped state projections, ordered lock acquisition with telemetry. | Agent A | `613e91cf` |
| **Native Rust Code Generation** | `src/native_generator.cpp` | `compiler-java/.../backend/NativeGenerator.java` | **INTEGRATED** | `rustc -D warnings`, `check_agent_skills.py`, `examples/` | **DIFFERENTIAL PARITY** | Rust code generation, domain Ref wrappers, route forwarding, zero-warning native execution. | Agent A | `613e91cf` |
| **Debug Maps (`.mossmap`)** | `src/native_generator.cpp#L5000-L6000`, `src/moss.cpp` | `compiler-java/.../backend/DebugMapGenerator.java`, `Main.java` | **INTEGRATED** | `check_debug_map.py`, `tests/differential/` | **FULL REGRESSION PARITY** | Automatic `.mossmap` JSON generation with FNV-1a 64-bit hashing, regions, erasures, and dataflow spans. | Agent B | `d2703f0b` |
| **Functional Pipeline Optimizer** | `src/functional_optimizer.cpp`, `src/functional_ir.hpp` | `compiler-java/.../optimizer/FunctionalOptimizer.java` | **INTEGRATED** | `FunctionalOptimizerTest.java` | **FOCUSED TESTED** | Stage fusion (`compose-map`, `compose-filter`), terminal reductions. All unit tests passing. | Agent B | `04206a5e` |
| **ABI-v10 Checked-Body Codec & `.mossi`** | `src/checked_body_ir.cpp`, `src/checked_body_ir.hpp` | `compiler-java/.../abi/CheckedBodyCodec.java`, `H3BodyReader.java` | **INTEGRATED** | `CheckedBodyCodecTest.java`, `check_phase25_checked_body_ir.py` | **FULL REGRESSION PARITY** | Round-trip statement/expr serialization, SHA-256 semantic hashing, source-free tamper verification. | Agent B | `2f89cbeb` |
| **ABI-v9 Default Compatibility** | `src/checked_body_ir.cpp`, `src/abi_v9.cpp` | `compiler-java/.../project/SourceFreeAbi.java`, `ProjectBuilder.java` | **INTEGRATED** | `check_phase21_abi_v9_migration.py`, `check_phase21_source_free_cache_binding.py` | **FULL REGRESSION PARITY** | ABI-v9 metadata verification, v8 migration detection, rlib archive marker validation (`MOSS_ABI_V9_CONTRACT`). | Agent A | In progress |
| **Fast Debug Interpreter & Tracing** | `src/fast_interpreter.cpp`, `src/fast_interpreter.hpp` | `compiler-java/.../interpreter/FastInterpreter.java` | **INTEGRATED** | `moss run --interp [--trace]`, `check_agent_skills.py` | **DIFFERENTIAL PARITY** | Deterministic JSON event streaming on stderr, local/state reads/writes, domain execution. | Agent A | `25bc920a` |
| **Semantic Queries & Agent Protocol** | `src/moss.cpp#L8500-L9200`, `src/typed_semantic_api.cpp` | `compiler-java/.../cli/AgentBootstrap.java`, `query/SemanticQueryEngine.java` | **INTEGRATED** | `check_agent_skills.py`, `check_semantic_queries.py` | **FULL REGRESSION PARITY** | Canonical `bootstrap`, `capabilities`, `schema`, `session-report-template`, `inspect`, `type`, `effects`, `calls`. | Agent A | `613e91cf` |
| **Margo Integration & Project Driver** | `margo`, `src/moss.cpp#L21790-L22000` | `compiler-java/.../project/ProjectBuilder.java`, `scripts/java/margo` | **INTEGRATED** | `check_margo.py`, `examples/projects/ledger` | **FULL REGRESSION PARITY** | Full Margo support: path/Git DAG, lockfiles, topological sort, warm-cache reuse, run/test/clean workflows. | Agent A | `d2703f0b` |
| **Repository Hygiene & JDT Daemons** | `.gitignore`, `compiler-java/jdt/` | `compiler-java/jdt/` | **INTEGRATED** | `scripts/java/dev-build.sh`, `git status` | **FULL REGRESSION PARITY** | Zero untracked cache artifacts, ~700-1000ms incremental compile via persistent Eclipse JDT daemon. | Agent A | `3194d37f` |

---

## 2. Immediate Work Queue & Subsystem Assignments

### Workstream 1: Automatic `.mossmap` Generation (Agent B)
- **Problem:** `check_borrowed_reads.py` and debug tooling expect `<output>.mossmap` alongside `<output>.rs` on compilation.
- **Action:** Implement `DebugMapGenerator.java` in `org.mosslang.compiler.backend` emitting canonical `moss-debug-map` format (version 1, source file, rust file, semantic identity entries, line mappings).
- **Target Gate:** `python3 tests/tooling/check_borrowed_reads.py ./scripts/java/run.sh tmp/test_borrowed_reads`

### Workstream 2: Project Build & Margo Compatibility (Agent A)
- **Problem:** `margo build` runs `moss build [--release] --json` in project directory expecting JSON envelope with `artifacts.executable`, `artifacts.generated_rust`, `artifacts.module_interfaces`.
- **Action:** Support `moss build` command in `Main.java` reading `Moss.toml`, building package source, generating Rust/executables, and outputting canonical build JSON.
- **Target Gate:** `MOSS="$(pwd)/scripts/java/run.sh" ./margo build` in `examples/projects/ledger`

### Workstream 3: Regression Suite Execution & Incremental Gaps (Agents A & B)
- **Action:** Sequentially execute suites from `tests/run.sh` with `./scripts/java/run.sh`, recording exact failure modes in this ledger and closing gaps continuously.
- **Target Gate:** Systematic execution of test scripts in `tests/tooling/`.

---

## 3. Active Agent Roster
- **Agent A:** L6 Coordinator / Implementer / Reviewer (`384f4ec1-b043-4d24-99ac-74606569390d`)
- **Agent B:** L5 Implementation Worker (`69af64b4-321f-4b61-9dc2-1541b831b144`)
- **Agent C:** Independent Adversarial Architect (`13982d0f-62ae-4457-90dd-a654c1bb1ab6`)

## Current continuation note — source-free object access modes

The Java checker now retains receiver and parameter access modes for object
methods, and the existing source-free object method record serializes/restores
those modes. Ownership checking applies imported method `CONSUME` parameter
modes at call sites. Focused Java unit tests cover a field-writing method that
consumes an owned vector parameter and reject a subsequent use. This closes only
the exercised access-mode metadata and ownership path; effect inference remains
syntax-bounded, and ABI-v10 canonical method records plus source-free native
linkage are still unverified. The full port is not complete and the original
acceptance gates remain pending.

The source-free object loader also restores an explicit `unit` method result;
a negative consumer test confirms the call cannot satisfy an `Int` result. This
is focused semantic coverage, not native source-free integration evidence.
