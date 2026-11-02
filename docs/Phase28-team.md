# Phase 28 — LOL Ownership Inference and Consumer Migration: Team Execution File

Date: Nov 2, 2026. Base: `phase28-work`.
Governing documents:
- `AGENTS.md`
- `docs/MOSS_COMPILER_CONTRACT.md`
- `docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md`

## Mission

Migrate compiler consumers (native emitter, Fast Debug, functional optimizer) to read checked ownership facts from the checker. Eliminate duplicated ownership decisions, hidden non-primitive clones, and remaining expression-string dependencies. Resolve known compiler defects E1–E4 (Group A E0502, `swarm_035` E0499, T6 E0596, `mutate_loop_element` E0596) with zero heuristic fallbacks.

## Team Organization

Phase 28 is executed by two complementary teams with non-overlapping file ownership:

| Team | Scope | Primary Files |
| --- | --- | --- |
| **Team A** | Checker Ownership Facts: CFG reaching definitions, loop-carried effect fixed points, LOL operators (`take`, `copy`), `for`-loop element effects, side table facts | `src/semantic_checker.cpp`, `src/type_ir.*`, `src/checked_body_ir.*`, `src/est/*` |
| **Team B** | Consumer Migration: Native generator, Fast Debug interpreter, functional optimizer, conformance test harnesses, defect reproducers | `src/native_generator.*`, `src/interpreter.*`, `src/functional_optimizer.*`, `tests/phase28/*` |

### Team B Roles

- **Agent 1 — L6 Coordinator, Implementer and Self-Reviewer**:
  Sole implementation agent for Team B. Owns consumer migration, test harness, targeted reproducers, and self-review reports.
  Branch: `phase28-r1-team-b` branched from `phase28-work`.
- **Agent 2 — Independent Adversarial Reviewer**:
  Ordinarily invoked at CP2 after consumer integration to audit invariant conformance and probe fail-closed behavior.

## Core Rules & Invariants

1. **Semantic Authority Invariant**: Only the checker decides Moss semantics. Consumers (native generator, Fast Debug, optimizer) execute or lower checked decisions; they never make or re-infer them.
2. **Fail-Closed on Missing Facts**: Consumers must fail closed (`MOSS_INTERNAL_INVARIANT`) if a required checked fact is missing or perturbed. No fallback guessing or silent heuristic inference.
3. **No Hidden Clones**: No `.clone()` in emitted Rust unless tracing to an explicit source `copy(x)` or internal runtime handle.
4. **EST Native Navigation**: No new expression-string parsing in consumers; navigate retained EST handles.
5. **Independent Execution**: Work asynchronously and report exact SHAs, test results, and checker-fact requirements without blocking on Team A.
