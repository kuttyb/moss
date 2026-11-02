# Phase 28 — Two-Team Execution Map and Round 1 Instructions

**Status:** Round 1 kickoff instructions, Nov 2, 2026. **Branch:** `phase28-work`. **Do not merge to `main` without L7 approval.**
**Language design authority:** [MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md](MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md).
**Compiler architecture authority:** [MOSS_COMPILER_CONTRACT.md](MOSS_COMPILER_CONTRACT.md) and [AGENTS.md](../AGENTS.md). Follow [AGENT_TEAM_OPERATING_MODEL.md](AGENT_TEAM_OPERATING_MODEL.md) except for the user-approved Phase 28 team shape and staged manual-test cadence described below.

## Team model and source ownership

Exactly **two implementation teams, each with two agents**:

| Team | Agent 1 | Agent 2 | Long-term owner |
| --- | --- | --- | --- |
| **A — Checker and semantics** | L6 Coordinator + coder + explicitly labelled self-reviewer | Independent adversarial reviewer, invoked only at frozen milestones | Checker decisions, EST-backed occurrence/definition identities, CFG/liveness/provenance, inferred effects, language operators, checked interfaces |
| **B — Consumers and execution** | L6 Coordinator + coder + explicitly labelled self-reviewer | Independent adversarial reviewer, invoked only at frozen milestones | Native emitter, Fast Debug, optimizer fact consumption, generated Rust audits, parity fixtures, migration |

L7 coordinates cross-team integration and resolves language decisions; L7 is not a third implementation team. Agent 2 does **not** code production fixes, participate continuously, or block unrelated work. There are no additional L5 implementers in this phase's user-selected two-agent team model.

Both teams must read `MOSS_COMPILER_CONTRACT.md` **before** touching compiler code. Also follow the mandatory `AGENTS.md` session startup and discovery, including repository skills and `./moss agent bootstrap --json`.

### Non-negotiable compiler rules

1. **Checker decides once.** The checker alone issues types, READ/WRITE/CONSUME effects, moves, borrow lifetimes, source/alias provenance, join decisions, patterns, lock effects, and specialization-aware summaries. Consumers lower/execute checked decisions; no duplicated semantic analysis in emitter, Fast Debug or optimizer.
2. **EST only.** New ownership analysis is keyed by retained EST expression occurrences and statement/definition identity. Do not parse expression strings, split them, key by source text/fingerprint, or introduce any new consumer-side text reconstruction. Do not increase existing checker string debt; migrating that legacy debt is **not** a Round 1 mission.
3. **Fail closed.** Missing or contradictory required checked ownership facts are not permission to guess or clone. Ship decision authority perturb/drop tests together with new *consumed* decision classes.
4. **One writer per file.** Team A leases `semantic_checker.*`, checker-focused AST/EST fact plumbing, and checked semantic API declarations. Team B leases `native_generator.*`, emitter include files, `interpreter*`, `functional_optimizer*`, and independent test/harness files. Shared headers and `src/moss.cpp` require explicit L7 lease before either team edits them. Do not modify the other's leased files; coordinate an API handoff instead.
5. **No scope creep.** No Graph DSL, transactional rollback, general `replace`/`swap`, stored references, novel runtime owned/borrowed union, or broad parser/driver rewrite. Treat the proposal as a proposed semantic contract; don't assume all its source syntax currently parses.

## Execution map — preserve this cadence

```text
SYNC 0   Agree on checked-fact shape, EST identity, file leases, tiny fixtures
  |
  +-------------------------------+
  | TEAM A (independent)          | TEAM B (independent)
  | 28.1 local ownership          | consumer / Rust audit, targeted probes
  | 28.2 CFG + effects            | existing independent lowering diagnosis
  +-------------------------------+
               ASYNC HANDOFF 1 — A publishes checked local/CFG facts and SHA
  +-------------------------------+
  | TEAM A (independent)          | TEAM B (independent)
  | 28.3 take/copy/collections    | integrate available local/CFG facts
  | 28.4 match/iteration/messages | integrate verified operators as delivered
  +-------------------------------+
               ASYNC HANDOFF 2 — A publishes checked operator/pattern facts
               SYNC 1 — combined ownership/consumer architecture gate
  +-------------------------------+
  | TEAM A (independent)          | TEAM B (independent)
  | effects, checker repairs,     | native/optimizer/SDK debt and migration
  | spec correction               |
  +-------------------------------+
               SYNC 2 — 28.7 full suite, parity, SDK and final review
```

**Round 1 covers SYNC 0 and the first independent work columns only.** An async handoff includes branch, commit SHA, changed files, exact fact schema/contract, representative executable source probes, expected diagnostics, and known gaps. The recipient integrates the handoff when ready; the producer continues its next independent work. A reviewer evaluates a frozen SHA; other work does not stop.

Reviewer milestones: **CP1** checker local/CFG soundness after 28.1–28.2, **CP2** end-to-end operator/consumer authority, **CP3** final integrated parity. Each team's Agent 2 independently reviews only the checkpoints relevant to its team's contract; no standing adversarial-review loop.

### Tests and PR policy

During Round 1: **code inspection, a handful of tiny positive/negative source fixtures, focused native/Fast Debug probes where relevant.** Do not manually run `make check`, `make check-shadow`, whole-corpus campaigns, or complete cross-engine/SDK suites. Reserve the heavy tests for later integration, especially 28.7. A Round 1 claim must not represent targeted tests as full green parity.

Repository PR policy still requires automated checks before a PR can become **ready/approved**; this document does **not** silently waive it. Use draft PRs/branch handoffs and report CI honestly. If normal PR automation launches larger mandatory checks, let required CI run; do not duplicate those full runs manually. A genuinely targeted-only PR workflow would require an explicitly approved workflow/policy adjustment before replacing mandatory checks. No merge to `main`.

## ROUND 1 — Team A instructions (Agent 1: L6 Coordinator/Coder/Self-reviewer)

**Mission:** Establish the smallest *real, checker-owned* ownership fact path for local assignments using EST. Do not attempt all Phase 28 semantics this round.

**Start:** Branch `phase28-r1-team-a` from the current, recorded `phase28-work` SHA; use a dedicated worktree. Read design, compiler contract, AGENTS, team model and the existing checker/EST code before changing it. Complete mandatory session bootstrap.

**Round 1 objectives:**

1. **Inspect and reuse:** identify where existing local binding identity, specialization context, READ/WRITE/CONSUME inference, EST occurrence IDs, and checked semantic facts live. Produce a concise map of actual paths and known gaps. Favor a small extension of existing APIs over a parallel `OwnershipAnalyzer`.
2. **Specify the minimum typed checker-to-consumer fact contract:** what identifies the **assignment definition**, the EST expression occurrence, specialization, ownership effect, legal source category/provenance and explicit missing fact. Identify which facts already exist. Share this minimal contract asynchronously with Team B at the earliest stable point (SYNC 0); do not hold work for a meeting. Team A owns the interface.
3. **Implement one bounded straight-line vertical slice:** for non-primitive **local-to-local** binding, record a real per-definition, EST-keyed checked READ/WRITE/CONSUME effect (reusing existing analysis) and demonstrate it on a production checker path. Add focused accepted/rejected programs for READ-only alias, legal writable alias, consuming local transfer, illegal use after move, and ownership demand from a surviving field. Keep old unrelated behavior stable; do not patch consumers with guessed fallback behavior.
4. **Inspect, don't implement yet:** document what CFG join, loop fixed point, typed raise, and imported summary semantics this first slice cannot handle. No `take`, `copy`, pattern or Vector iteration implementation in this round.
5. **Self-review** the code against compiler-contract invariants; demonstrate the fact is populated by real checking, not just a struct/unit test. Record any new decision's perturb/drop gate plan and implement targeted fault tests if a production consumer uses that fact in Round 1.

**Stop only the impacted change** on a frozen language contradiction, required shared-file lease conflict, absent EST identity, or genuine missing checker authority. Record a minimal reproducer and escalate to L7; continue independent tasks. Do not invent fallback semantic logic.

**Round 1 handoff:** push branch with exact SHA, changed files, inspected entrypoints, checked fact schema, small fixture outcomes and diagnostics, limitations, debt baseline changes (if any), and the contract for Team B. Do **not** call 28.1/28.2 complete unless their acceptance scope is genuinely proven. Agent 2 is ordinarily not invoked until CP1.

## ROUND 1 — Team B instructions (Agent 1: L6 Coordinator/Coder/Self-reviewer)

**Mission:** Build the consumer/conformance runway without waiting for Team A and without independently defining ownership semantics.

**Start:** Branch `phase28-r1-team-b` from the same recorded `phase28-work` baseline (or a documented later nonconflicting base); use a separate worktree. Read design, compiler contract, AGENTS, team model, and existing emitter/Fast Debug/optimizer code. Complete mandatory session bootstrap.

**Round 1 objectives:**

1. **Inventory decision duplication:** inspect the native emitter, Fast Debug and optimizer for clone insertion, move-vs-borrow decisions, effect reconstruction, text parsing, text-keyed EST lookups, and inferred iteration modes. Give concrete file/function locations and classify each path as (a) checked fact already consumed, (b) known debt, or (c) uncovered risk. Do not merely produce a grep count.
2. **Inventory four historical defects** from Phase 28 §11: **E1 Group A E0502**, **E2 `swarm_035` E0499**, **E3 T6 E0596**, **E4 `mutate_loop_element` E0596**. Run **only minimal existing probes**, isolating checker acceptance versus generated-Rust failure. Note that T6 is primarily checker effect inference and belongs to Team A; E4 spans checker and lowering. Do not change semantic expectations to hide defects.
3. **Build a tiny consumer test runway:** add narrowly scoped fixtures/scripts for generated Rust ownership, accepted-to-rustc behavior and checked-fact absence/contradiction using the existing fact-hook machinery where practical. Keep test files disjoint from Team A. A fixture which cannot run until Team A's checked fact exists may be committed as explicitly pending, never marked passing.
4. **Prepare fact-consumer adapter locations:** identify where the new typed ownership facts will be read for native generation, Fast Debug and optimization. Document the minimum fields Team B needs; send a short asynchronous feedback note to Team A. **Do not** implement a second classifier, parse source expression text or wire a fallback to the old string heuristic.
5. **Optional narrow fix:** only an independently provable emission-order bug whose correctness can be established from *existing checked facts*, without touching shared checker/API files, and only with a tiny accepted-to-rustc fixture. Otherwise stop at characterization and test groundwork.

**Round 1 handoff:** push branch with exact SHA, concrete duplication/clone/EST-debt census, E1–E4 fixture outcomes, tests and gaps, proposed consumer adaptation points and checked-fact requirements. Self-review findings against the compiler contract. No complete consumer migration, large suite, SDK rewrite or routine adversarial review in Round 1. Agent 2 is ordinarily invoked at CP2.

## Round 1 combined exit / first handoff

Round 1 is ready to advance when:

- **A** demonstrates a real, EST-backed local ownership fact path on a few positive/negative programs and publishes the typed fact contract.
- **B** independently delivers its census, targeted fixtures and consumer integration map; B may have no production semantic changes.
- Both publish exact SHAs and review/diagnostic evidence, identify file leases and avoid duplicated semantic authorities.
- L7 verifies the typed interface is sufficient to unblock B's next implementation slice. **Do not wait for a large test run or both teams to finish unrelated work** before this asynchronous handoff.
- CP1 remains scheduled **after** 28.1–28.2 foundation, not at every Round 1 commit.
