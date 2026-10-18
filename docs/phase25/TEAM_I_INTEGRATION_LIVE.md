# Team I live integration record

L7 authorized execution on 2026-10-18. Target `phase25-integration`; initial
SHA `fbd2f729caf1a96c3ffe184068e88ba6b09288cc`. No main merge or ABI production
cutover. Coordinator also serves as L6 PR Reviewer; independent milestone
adversarial review remains separate.

## Intake and dependency DAG

Verified after `git fetch origin`: H/T `2d457245061c75f3a4652a8851134322f1fa8445`,
G `2938d257bf10bbb11bd221fa14237ddcd1e0702b`,
S `53d04fbf48c007d912a47836959c488bcbb6c450`,
B `755f13172c37447bca981d1fefbb885ec46f3a59` match the execution plan.
H/T intake first, followed by distinct G, S, B deliveries on fresh branches
from the reviewed target. Historical prep rehearsal is not current evidence.
H1/H2/H3 and all release gates remain open until executable integrated proof.

## File leases

| Owner | Exclusive write scope | Branch/worktree |
| --- | --- | --- |
| L5 compiler | `src/moss.cpp`, `src/ast.hpp`, `src/type_ir.hpp`; compiler intake and associated tests | `phase25-i-h-intake`, `tmp/phase25-i-compiler` |
| L5 ABI/harness | ABI headers, runtime, `tests/phase25/integration/**`; initially audit and independently justified harness fixes | `phase25-i-abi-preparation`, `tmp/phase25-i-abi` |
| L6 Coordinator | root status, organizational guidance, this record, integration/review provenance | root checkout |

Whole-history H intake includes ABI files; runtime/harness worker must avoid
editing those files until their baseline interface is handed off. No concurrent
compiler writers. Each worker owns its feedback workstream. Two actual L5
agents instantiated; no optional third worker.

## Evidence and gates

Startup bootstrap returned `moss-0.1`. GitHub connector repository access
confirmed; `gh` CLI absent, connector supports draft PR and workflow inspection.
AGENTS already explicitly permits authorized normal push on the fetched branch;
no prohibiting rule remains. Push must preserve published history.

No PR review, integration, regression PASS or final acceptance is claimed yet.
Required CI runs four isolated groups; semantic gates missing before intake are
blocked, never green. Final strict 62-gate release is distinct from PR checks.

## Initial live findings

- H/T merge reports no compiler conflict; documentation conflicts are current
  status and three architecture documents. These still require deliberate
  reconciliation and regression evidence before integration.
- ABI worker identified the strict CLI bypassing `validate_gate_evidence`; a
  bounded harness integrity repair is in progress. Cross-crate source-removal
  coverage requires additional production-path work; existing status is not E3.

## Contract interpretation

The execution plan assigns typed ABI payloads, runtime linkage, Gate 10 and
source-free specialization to Team I. Historical D3/D4 ownership questions do
not authorize deferral and do not require a new language-design decision.
ABI-v10 production default cutover awaits L7 final acceptance; staging must
exercise the actual v10 emitter/importer/linker with source removal. Fail-closed
rejection is required for negative cases but is not positive H3/E3 evidence.

## Open integration issues

| ID | First evidence / owner | Acceptance consequence |
| --- | --- | --- |
| I-001 | L5 ABI audit: strict CLI bypasses evidence validator | Repair + CLI fault regression; no narrative-only strict acceptance |
| I-002 | L5 ABI audit: cross-crate scenarios 2–5 remain source-backed, 6 lacks execution, 7 checks name substring | Replace with real source-removal, link/run and exact provenance evidence before E3 |
| H1–H3 | Inherited L7 execution-plan findings | Team I owns structured requirements, stale AST projection and true generic source-free proof |

These are open implementation/validation defects, not architectural escalation.
