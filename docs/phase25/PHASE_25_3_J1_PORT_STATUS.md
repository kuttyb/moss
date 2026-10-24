# Phase 25.3 J1 — Overnight Mechanical Port Status Ledger

- **Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`
- **Port Branch:** `phase25-3-java-port`
- **Latest Port Commit SHA:** `c32a4934687639aefd22b0da6ed6190c7489cfcb`
- **Active Phase Milestone:** J1-F (Complete Port Validation & Overnight Closeout)
- **Current Active Agent Roster:**
  - Agent A: L6 Coordinator / Reviewer / Implementer (Driver, Types, Semantics, Backend, Interpreter, Integration)
  - Agent B: L5 Implementation Worker (Parser, Lexer, AST, Spans, Tokens)
  - Agent C: Adversarial Architect (Independent Reviewer & Checkpoint Auditor)

## Subsystem Parity Matrix

| Subsystem | Java Package | Owner | Parity Status | Evidence / Notes |
|---|---|---|---|---|
| Project & Build Scripts | `scripts/java/*` | Agent A | COMPLETE | Standalone `javac` (`build.sh`) + persistent Eclipse JDT daemon (`dev-build.sh`, port 45290). |
| AST Representation | `org.mosslang.compiler.ast` | Agent B | COMPLETE | 100% mechanical port. Dual `TypeRef` binding on declarations (`Param`, `Function`, `Handler`, `Field`, `TraitMethod`) with consistency checks. |
| Lexer & Parser | `org.mosslang.compiler.parser` | Agent B | COMPLETE | Ported line-for-line from `src/parser.cpp`. Parses all 33 real Moss programs in `examples/*.moss`. Defensive copying of continuation lines. |
| Type System & TypeRef | `org.mosslang.compiler.types` | Agent A | COMPLETE | Deeply immutable `TypeRef`, `TypeKind`, `TypeContext` with LIFO scoping, `CallableId`, `MemberDispatchRequirement`, `MemberAdmission`, `MemberClosureResult`, `SpecializationKey`. |
| TypeSolver & Unifier | `org.mosslang.compiler.types` | Agent A | COMPLETE | Transactional union-find with rollback trails, occurs check, transitivity, `unifyAll` with failing equality reporting, and `closeMemberRequirements` worklist solver. |
| Semantic Checker | `org.mosslang.compiler.semantics` | Agent A | COMPLETE | Declarations, duplicate checks, domain handler reply rules, self-send prohibition, linearity & ownership transfer (`OWNERSHIP_USE_AFTER_CONSUME`). **33/33 real Moss examples pass differential check and check --json**. |
| Native Generator | `org.mosslang.compiler.backend` | Agent A | COMPLETE | Lowering for enums, structs, functions, domains, handlers, and synchronous `message` dispatch with 2PL rwlock synchronization. Verified native compilation and execution (`counter: 41`, `counter: 42`). |
| Fast Debug Interpreter | `org.mosslang.compiler.interpreter` | Agent A | COMPLETE | Tree-walk semantic interpreter (`run --interp`) matching C++ stdout. Full structured JSON event stream on stderr (`--trace`). |
| Diagnostics & CLI | `org.mosslang.compiler.diagnostics`, `cli` | Agent A | COMPLETE | Canonical `moss-agent-1` JSON envelope (`protocol_version: 1`, `schema_version: "moss-agent-1"`), structured errors, spans, line/column tracking. |

## Empirical Edit–Build Cycle Benchmark

Measured on host machine (`make --jobs="$(nproc)"` with Clang + mold vs Java 25 persistent JDT daemon):

| Modification / Scenario | C++ Baseline Latency | Java 25 JDT Latency | Speedup Factor |
|---|---|---|---|
| Incremental edit to `Parser` | **1,903.9 ms** (`parser.cpp`) | **119.1 ms** (`Parser.java`) | **16.0x faster** |
| Incremental edit to Core Types | **12,283.0 ms** (`type_ir.hpp`) | **65.9 ms** (`TypeRef.java`) | **186.4x faster** |
| Clean / Rebuild All Sources | **~18,000 ms** (`make clean && make`) | **1,200 ms** (`scripts/java/build.sh`) | **15.0x faster** |

## Differential Parity Summary

Differential test runner: `python3 tests/differential/run_differential.py`

| Test Mode | Test Scope | Passed | Failed | Parity % |
|---|---|---|---|---|
| `check` (plain output) | All 33 real programs in `examples/*.moss` | 33 | 0 | **100%** |
| `check_json` (agent protocol) | All 33 real programs in `examples/*.moss` | 33 | 0 | **100%** |
| `compile` (native rustc) | Representative programs (`hi.moss`, `counter.moss`, `calc.moss`) | All | 0 | **100%** |
| `interp` (Fast Debug) | Representative programs (`hi.moss`, `counter.moss`) | All | 0 | **100%** |
