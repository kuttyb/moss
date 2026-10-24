# Phase 25.3 J1 — Overnight Mechanical Port Status Ledger

- **Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`
- **Port Branch:** `phase25-3-java-port`
- **Initial Baseline Commit:** `caf2c515` (benchmarks + baseline docs)
- **Active Phase Milestone:** J1-A (Bootstrapped Java Project) -> J1-B (Parser and Type System)
- **Current Active Agent Roster:**
  - Agent A: L6 Coordinator / Reviewer / Implementer (Driver, Types, Semantics, Integration)
  - Agent B: L5 Implementation Worker (Parser, Lexer, AST)
  - Agent C: Adversarial Architect (Independent Reviewer)

## Subsystem Parity Matrix

| Subsystem | Java Package | Owner | Parity Status | Evidence / Notes |
|---|---|---|---|---|
| Project & Build Scripts | `scripts/java/*` | Agent A | COMPILES | Standalone javac + persistent JDT dev build scripts |
| AST Representation | `org.mosslang.compiler.ast` | Agent B | IN PROGRESS | AstNode, Expr, Stmt, FunctionDecl, DomainDecl, Program |
| Lexer & Parser | `org.mosslang.compiler.parser` | Agent B | IN PROGRESS | MossLexer, MossParser porting from `parser.cpp` |
| Type System & TypeRef | `org.mosslang.compiler.types` | Agent A | IN PROGRESS | TypeRef, TypeKind, TypeContext, TypeVar |
| TypeSolver & Unifier | `org.mosslang.compiler.types` | Agent A | IN PROGRESS | TypeSolver, checkpoint/rollback transactions |
| Semantic Checker | `org.mosslang.compiler.semantics` | Agent A | NOT STARTED | TypeEnv, statement checking, effect inference |
| Optimizer | `org.mosslang.compiler.optimizer` | Agent A | NOT STARTED | Pipeline fusion and functional optimization |
| Native Generator | `org.mosslang.compiler.backend` | Agent A | NOT STARTED | Rust emission, domain locking, runtime hooks |
| Fast Debug Interpreter | `org.mosslang.compiler.interpreter` | Agent A | NOT STARTED | Deterministic tree-walking interpreter |
| ABI-v9 / ABI-v10 | `org.mosslang.compiler.abi` | Agent A | NOT STARTED | Interface codec, checked body codec |
| CLI & Driver | `org.mosslang.compiler.cli` | Agent A | IN PROGRESS | Main entry point, flags, diagnostics |

## Key Milestones & Blockers
- **Blockers:** None currently. Toolchain (JDK 25, Eclipse JDT, Eclipse Collections) verified operational on host.
- **Next Actions:**
  1. Complete J1-A Java project bootstrap: establish `compiler-java` directory, build scripts (`build.sh`, `dev-build.sh`, `run.sh`, `test.sh`), test with plain `javac` and JDT daemon.
  2. Implement J1-B AST and Parser with Agent B.
  3. Implement J1-B TypeRef and TypeSolver with Agent A.
  4. Run first architectural review checkpoint with Agent C.
