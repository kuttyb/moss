# Phase 25.3 J2 — Advanced Compiler Subsystems & Integration Status Ledger

- **Pinned C++ Baseline SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289`
- **Port Branch:** `phase25-3-java-port`
- **J1 Closeout SHA:** `07c19f0a514d86c8f6f69c5e3d7bb411516f4aa5`
- **Active Phase Milestone:** J2-A (Functional Pipeline Optimizer) & J2-B (ABI-v10 Checked-Body Codec)
- **Current Active Agent Roster:**
  - Agent A: L6 Coordinator / Implementer (ABI-v10, Semantic Queries, CLI, Integration)
  - Agent B: L5 Implementation Worker (Functional Pipeline Optimizer)
  - Agent C: Adversarial Architect (Independent Reviewer)

## Subsystem Parity Matrix (Phase 25.3 J2)

| Subsystem | Java Package | Owner | Parity Status | Evidence / Notes |
|---|---|---|---|---|
| Functional Pipeline Optimizer | `org.mosslang.compiler.optimizer` | Agent B | IN PROGRESS | Pipeline recognition, stage fusion (`map \|> filter \|> reduce`), terminal optimizations from `src/functional_optimizer.cpp`. |
| ABI-v10 Checked-Body Codec | `org.mosslang.compiler.abi` | Agent A | IN PROGRESS | `.mossi` checked body binary serialization, hashing, and source-free verification from `src/checked_body_ir.cpp`. |
| Semantic Queries & Agent CLI | `org.mosslang.compiler.cli`, `query` | Agent A | IN PROGRESS | Full `moss agent bootstrap --json`, `moss inspect`, `moss type`, `moss effects`, `moss calls`, `moss why`. |
| Margo & Integration Harness | `scripts/java/*`, `tests/*` | Agent A | IN PROGRESS | Support package-level workflows through `./margo` using `MOSS=./scripts/java/run.sh`. |

## Key Milestones
- [x] J1: Complete mechanical compiler port (Parser, TypeSolver, Semantics, Native Generator, Fast Debug, JDT daemon).
- [ ] J2-A: Functional Pipeline Optimizer port (`org.mosslang.compiler.optimizer`).
- [ ] J2-B: ABI-v10 Checked-Body IR & Codec (`org.mosslang.compiler.abi`).
- [ ] J2-C: Semantic Queries and Full Agent Bootstrap JSON (`org.mosslang.compiler.cli`).
- [ ] J2-D: Margo Package Driver & Regression Harness Integration.
- [ ] J2-E: Adversarial Architectural Audit (Agent C) & Final Sign-Off.
