# Phase 28.6 — ECT check speed: implementation (L7 brief)

You are the **L7** for Phase 28.6. You run five L6 teams in sequence to completion without checking in with Bank or his L10. Bring back one final report. Escalate earlier only for the cases under *Escalation*.

Phase 28.5 built the benchmark suite (`benchmarks/ect_check/`: B1–B6, M1/M2, the Rust comparison, and the reference-vs-candidate correctness gate) and recorded today's baseline. Phase 28.6 implements the design those benchmarks were built to judge.

## Hard rule: at most two threads

A **thread** is one live LLM session making model calls: a Claude Code or Codex CLI session, **including any subagent, background agent or parallel task it spawns.** At most **two** may be active at any moment, across you and every team. Shell processes (builds, benchmark runs) are not threads.

- **Teams run strictly one after another.** While a team runs, you make no model calls.
- **Inside a team,** Agent 1 (codes, reviews, coordinates; owns delivery) and Agent 2 (adversarial review only) may run at the same time.
- **No agent spawns subagents,** background agents or parallel tasks, in any CLI. Put this sentence in every team's message.
- Between teams you work alone. If you want an independent review of your own integration, use at most one reviewer.

## How you brief teams

Give each team **one self-contained message**: everything inline, outcome-level done-when criteria, no step plans, no "read these docs." Agents let context compact and keep working (no context-limit stop rules). They implement in bulk, run one bulk check, and triage failures together. **Don't re-measure performance after every change.** Measure with the 28.5 suite at the end of each team.

## Repository and machine rules

- Branch `phase28-6`, created off the merged Phase 28.5 benchmarks. Teams work in sequence on it; each ends by pushing and tagging `ect-tN-done`. No pull requests.
- Everything runs on the work machine (16-CPU WSL2). No hosted CI. Build with `moss-dev`.
- `make check` must pass at every tag. The frozen `benchmarks/compiler_torture` 1.0.0 is never modified.

## Goals (put a short version in every team message)

1. **Fix Mossi Leak:** a consumer never walks into a dependency's body to check a call.
2. **Fix Inference Ripple:** an edit rechecks only the declarations whose callers' recorded predicates actually fail.
3. **Cut CPU and memory across worktrees:** cost is base plus divergence, not N × everything.
4. **Remove Cold Tax:** a warm check pays almost nothing to start.

Source syntax does not change, and nothing changes inside a package that a user can see.

## Invariants every team must hold

1. **One semantic authority.** Every bound, fact, predicate and formula comes from the checker; no other component reconstructs semantics.
2. **Incremental equals full.** Verdicts and diagnostics must be byte-identical to a full recheck, enforced by the 28.5 correctness gate.
3. **The call graph is a DAG.** No recursion and an acyclic domain graph mean one topological pass and no fixpoints.
4. **Equal key means equal answer.** Keys cover every input and nothing environmental: no paths, no caller locations, no timestamps.
5. **Facts are immutable.** Only per-worktree root tables change.
6. **The one-package rule.** A stale dependency `.mossi` is an error; the fix is `margo build` on the dependency. Nothing ever derives another package's facts from its source.
7. **When in doubt, re-walk.** An unknown predicate kind, a missing fact or a hash mismatch falls back to today's full walk. Never guess.

## Decisions already made (embed them; don't reopen)

- **Exported generics are hardened at package build.** Each untyped parameter binds to visible, absolute traits or to **`any`** (zero expectations: move, store, return, or pass to another `any` position). Hardening fails *only* when no visible trait covers the requirements, and the error suggests the trait to declare. Effects never fail hardening.
- **Traits are absolute.** Every trait method declares its parameter and result types; omitting the result is an error.
- **Cover selection** follows today's method-ambiguity policy: a unique exact cover, else a unique minimal superset, else an error listing the candidates. Never a tie-break.
- **Copy facts are type properties, not traits.** Primitives copy on plain assignment, every other type copies with `copy(x)`, and **NonCopyable** marks the closed set of exceptions (`FileIO`). A bound records only "primitive" (the body reuses `x` after a plain assignment) or "not NonCopyable" (the body calls `copy(x)`).
- **Internal type variables** (`$0`, `$1`, …) exist only in `.mossi` files and the compiler. They link positions that must be the same type and carry element, key and value types.
- **The daemon is named Peat** (process name `mossd`).
- **Defaults Bank may override before launch:**
  - the primitive restriction in `trait_conforms` is lifted for built-in traits only;
  - Self-returning methods stay rejected at exports;
  - drift reporting is informational.

## The teams, in order

**Team 1 — Identity, hashing and determinism.** No semantic change.
- Declaration identity via `entity-v1` (module, kind, name, owning type for methods).
- A canonical EST serialization per declaration, excluding whitespace, comments and positions, and its hash.
- Type content hashes, which include each type's method set.
- A compiler build ID, combined with the language/ABI version and semantic flags.
- **Position-independent diagnostics:** anchored to the declaration plus a relative position, and mapped back to absolute lines at report time.
- **Deterministic diagnostic order:** by file, line, column, code.
- *Done when:* moving a declaration down by N lines, or editing a comment, changes no declaration hash; output is byte-identical to before on the whole corpus; the 28.5 gate passes.

**Team 2 — Trait foundations.**
- Enforce absolute trait signatures.
- The built-in trait catalogue:
  - **Add:** `+`, including `String`;
  - **Arithmetic:** `+ - * /`, with a result-type rule for `Int`→`Float` promotion over two type variables;
  - **Integral:** `%`, bitwise, shifts — `Int` only;
  - **Ordered;**
  - **Equatable/Key,** using today's `KeyProofId`;
  - **Iterable,** with `next() -> option[$n]`, closing Iterator's open element;
  - **Indexable:** container, key and value types;
  - **Callable,** with effect variables.
- Primitives conform to the built-in traits.
- The NonCopyable marker and the "primitive" category check.
- Internal type variables in the summary model.
- Migrate the SDK and packages to absolute traits.
- *Done when:* the corpus and SDK pass, and every built-in operation in the checker maps to exactly one built-in trait. Agent 2 must try to find an operator or built-in that maps to none or to two.

**Team 3 — The `.mossi` firewall.**
- **Derivation is checking.** At package build, specialize each exported generic against an **archetype**: a synthetic type per parameter carrying only the methods the body calls, with results as internal type variables, effects as **effect variables** (`acc(r, path)`, `raises(r)`, `obs(r)`), and no copy facts assumed. Every question the checker asks of the archetype becomes a requirement or a constraint. Extend v10's `OpenCallableSummaryFacts` and `effect_dependencies` from callbacks to method requirements, and from values to variables.
- **Publish:** trait covers or `any` per parameter; effect formulas (access = local ⊔ the requirements' access; raises = local ∪ the requirements' raises, minus recovered; observable = local ∨ the requirements' flags); and constraints as inequalities over effect variables. Use a new `.mossi` ABI version, keeping the semantic IR bodies, which execution and emission still need.
- **Consumer side,** at a call to a hardened export: conform each argument to its traits, cached per (trait, type); unify the type variables; substitute the concrete methods' effects; check the constraints. **No walk across the boundary.**
- **Fast Debug** resolves dispatch at runtime from the bound plus the value's concrete type, not from per-specialization records, for hardened exports.
- **The native emitter** keeps its existing per-type walk for emission; that's unchanged in this phase.
- A **drift report** at package build: exported bounds widened, narrowed or changed since the last `.mossi`.
- **Migrate** SDK exports using the suggested-trait errors.
- *Done when:* **B1** consumer check time is flat in D, W and K; the gate passes; M1 is re-measured after migration; and Agent 2 has confirmed that no consumer path opens a provider body during checking.

**Team 4 — Fact store and incremental check (in-process, persisted).**
- **Per declaration:** direct facts (local types, access per path, raises, observable flags, copy facts) and a **summary formula** over callees.
- **Per call edge: the predicates the caller's check evaluated,** in these kinds:
  - type (exact type, conforms to a trait, primitive);
  - dispatch (method resolves to `T.m`);
  - access (`acc(callee, path) ≤ mode`);
  - borrow (the callee doesn't write the live paths);
  - raise (`raises ⊆ recovered set`, or folded into the formula);
  - observable (no `io`, message, panic or divergence in initializer contexts).
- **On an edit:** reparse changed files; match declarations by identity and compare canonical hashes; re-derive changed declarations; for each caller, evaluate its predicates — if all hold, re-evaluate its formula only; if any fails, re-walk the caller. Continue upward in one topological pass. When a handler's footprint changes, rerun that domain's synchronization analysis from the handler summaries.
- **Opportunistic internal bounds:** archetype derivation for internal generics, with partial traits allowed and silent fallback.
- **The content-addressed store:** keys hash the build ID, the canonical body, type content hashes, the **callee facts its predicates reference**, and imported bounds. Values are immutable and checksummed.
  - On disk at `~/.cache/moss/peat/<build-id>/`, written temp-then-rename, verified on read, memory-mapped.
  - Root tables per worktree, carrying a generation number.
  - Garbage collection by reachability from live root tables plus the last few snapshots of each, under a size budget.
- Today's in-process memo stays as the dedup inside any walk.
- *Done when:* **B3** rechecks one declaration for edits (a) and (c), and exactly the affected path for (b); **B2** shows close to one conformance check per new type wherever local bounds exist; **B6** p50 and p95 improve on the baseline; and the gate passes on every benchmark and on corpus replays.

**Team 5 — Peat.**
- **One daemon per user per machine.** `moss check` auto-starts it. It talks over a user-only socket, exits when idle, and exchanges build IDs on connect: on a mismatch, start a separate Peat with its own store.
- **The client** sends reachable files' hashes (using an mtime-and-size cache), unsaved buffers, and the worktree root as the root table's label. **No file watching and no git dependency.**
- **Internals:** a worker pool; **in-flight dedup** across worktrees; a memory budget.
- **Commands:** `moss peat status | stop | gc`. `--no-peat` and `MOSS_PEAT=off` fall back to Team 4's in-process path, with identical results.
- **The Phase 22 queries become clients**, and `moss effects++` adds direct facts, edge predicates, and a **blast-radius query** ("if X changes to Y, which predicates fail?"). Every answer is tagged with its generation and revalidates before answering.
- Fast Debug's checking phase goes through Peat.
- A **background self-audit:** a sampled fraction of checks re-run in full, with any mismatch logged as a bug.
- *Done when:* **B4** warm no-edit is close to the client round trip, and a one-line edit costs only that declaration's work; **B5** memory is close to base plus divergence, and a new worktree's first check is mostly hits; the self-audit is clean; and the gate passes with Peat on and off.

## Your gate between teams

Verify against **evidence, not the team's report.** Pull the tag, then run `make check`, the 28.5 correctness gate, and that team's benchmarks. On a failure, send the same team back with the specific failure. Don't start the next team.

## Escalation

Stop and report to Bank only if:
- an invariant can't be held as written;
- a decision listed above turns out to be wrong in practice (for example, absolute traits or hardening break a part of the SDK that can't be migrated);
- the thread budget can't be honored with the tooling available.

Everything else, decide and continue, defaulting to invariant 7 (when in doubt, re-walk).

## Final deliverable

1. All five teams tagged, with a full 28.5 benchmark run committed as `benchmarks/ect_check/PHASE_28_6_REPORT.md`, comparing against the 28.5 baseline benchmark by benchmark.
2. A short final report to Bank:
   - before and after for B1–B6;
   - M1 and M2 after migration;
   - the Rust `cargo check` comparison;
   - any export migration that needed judgment;
   - every place invariant 7's fallback fires in practice, with counts;
   - confirmation that the gate held everywhere and the two-thread budget held throughout.
