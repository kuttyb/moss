# Phase 28.6 — ECT check speed: implementation (L7 brief)

*Revision 2, Nov 6 2026: revised after the Phase 28.5 baseline. See the change log at the end.*

You are the **L7** for Phase 28.6. You run six L6 teams in sequence to completion without checking in with Bank or his L10. Bring back one final report. Escalate earlier only for the cases under *Escalation*, or at the one decision gate after Team 0.

Phase 28.5 built the benchmark suite (`benchmarks/ect_check/`: B1–B6, M1/M2, the W4 depth and diamond extensions, the Rust comparison, and the reference-vs-candidate correctness gate) and recorded today's baseline. Phase 28.6 implements the design those benchmarks were built to judge.

## What the 28.5 baseline showed (put a short version in every team message)

- **Walks aren't linear today.** Deep chains grow roughly quadratically (W4 depth 1000: 90.6 s); shared-callee diamonds and `var c = f(x)` chains grow exponentially (diamond D=12: 9.4 s); a deep DAG whose leaf argument lacks a method did not finish in 130 s; the checker segfaults past about depth 258 at an 8 MiB stack. Every walk this design keeps inherits these costs, so fixing them comes first.
- **Mossi Leak is real:** a consumer that defines no DAG functions takes 22.6 s (D50 W4 K32), linear in K.
- **Inference Ripple is total:** every edit rechecks all 81 functions in B3.
- **Nothing is shared across worktrees:** cost scales 16× at N=16.
- **Cold Tax is not process startup** (1.9 ms for a file). The fixed cost is about 45 ms of margo Python overhead, plus the full recheck.
- **Local signatures barely exist:** about 4 of 81 callables in the DAG fixtures have a complete one (M2).

## Goals

1. Make every walk linear in distinct (callable, types) pairs, on success and on failure.
2. Fix Mossi Leak: a consumer never walks into a dependency's body to check a call.
3. Fix Inference Ripple: an edit rechecks only declarations whose guards actually fail.
4. Cut CPU and memory across worktrees: cost is the shared base plus divergence, not N × everything.
5. Lower the fixed cost of a check.

Source syntax does not change, and nothing inside a package changes in a way users can see. **The existing `moss` commands stay as they are; no daemon is built in this phase.**

## Terms (use these in code, docs and reports)

- **Signature:** a declaration's types and effects.
  - **Inferred signature:** computed during `moss check` for declarations inside a package. It's a cache: it may be partial, and a missing or failed one falls back to the walk.
  - **Hardened signature:** sealed at package build for exported generics, bound to visible traits or `any`, and authoritative at the `.mossi` boundary.
- **Guard:** what a caller's check relied on about a callee, recorded per call edge and re-evaluated after an edit. A guard that holds means reuse; a guard that fails means re-walk the caller.
- **Shared store:** the per-machine, content-addressed on-disk store holding signatures, guards and cached ESTs.
- **Rename:** the existing inference convergence fingerprint (`inferred_checker_signature()`, from Phase 26.2) becomes the **convergence fingerprint**, so "signature" has one meaning.

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
- Everything runs on the work machine (16-CPU WSL2). No hosted CI.
- **Build with `moss-dev` for development; time with `moss-opt` only.** `moss-dev` is `-O0` and invalid for timing; the 28.5 runner refuses non-opt timing.
- `make check` must pass at every tag. The frozen `benchmarks/compiler_torture` 1.0.0 is never modified.

## Invariants every team must hold

1. **One semantic authority.** Every signature, guard and formula comes from the checker; no other component reconstructs semantics.
2. **Incremental equals full.** Verdicts and diagnostics must be byte-identical to a full recheck, enforced by the 28.5 correctness gate, **including on failing programs.**
3. **The call graph is a DAG.** No recursion and an acyclic domain graph mean one topological pass and no fixpoints.
4. **Equal key means equal answer.** Keys cover every input and nothing environmental: no paths, no caller locations, no timestamps.
5. **Stored facts are immutable.** Only per-worktree root tables change.
6. **The one-package rule.** A stale dependency `.mossi` is an error; the fix is `margo build` on the dependency. Nothing ever derives another package's signatures from its source.
7. **When in doubt, re-walk.** An unknown guard kind, a missing signature or a hash mismatch falls back to today's full walk. Never guess.

## Decisions already made (embed them; don't reopen)

- **Exported generics are hardened at package build.** Each untyped parameter binds to visible, absolute traits or to **`any`** (zero expectations: move, store, return, or pass to another `any` position). Hardening fails *only* when no visible trait covers the requirements, and the error suggests the trait to declare. **Effects never fail hardening:** a generic can't access fields of an untyped parameter and results come back owned, so effects always compose from per-method effect variables.
- **Traits are absolute.** Every trait method declares its parameter and result types; omitting the result is an error.
- **Cover selection** follows today's method-ambiguity policy: a unique exact cover, else a unique minimal superset, else an error listing the candidates. Never a tie-break.
- **Copy facts are type properties, not traits.** Primitives copy on plain assignment, every other type copies with `copy(x)`, and **NonCopyable** marks the closed set of exceptions (`FileIO`). A hardened signature records only "primitive" (the body reuses `x` after a plain assignment) or "not NonCopyable" (the body calls `copy(x)`).
- **Internal type variables** (`$0`, `$1`, …) exist only in `.mossi` files and the compiler. They link positions that must be the same type and carry element, key and value types.
- **The primitive restriction in `trait_conforms` is lifted for built-in traits only.** Self-returning methods stay rejected at exports. Drift reporting is informational.
- **No daemon in this phase.** The shared store gives ripple reuse, worktree reuse and warm runs through today's commands. A resident daemon (named **Peat**) is a later, separate decision, informed by Team 5's measurements.

## The teams, in order

**Team 0 — Linear walks.**
- Make every checker walk cost one computation per distinct (callable, concrete types) pair, independent of how many paths reach it. This covers diamonds and shared callees, `var c = f(x)` chains, and repeated effect and signature queries during a walk. **It covers the failure path too:** once a specialization has failed, it's recorded and never re-walked from another path.
- Remove the per-level cost growth behind the roughly quadratic W4 curve.
- Remove the stack-depth crash: walk depth must not be bounded by the native stack. Make the walk iterative, or give it an explicit work stack.
- Fix the nested inferred-generic call failure `bump(bump(x))` ("unresolved state layout") if it lies on the walk path; otherwise log it as a separate bug with its reproducer.
- *Done when:*
  - W4 depth 100, 200, 500 and 1000 show close to linear growth in time and counters;
  - diamond D=6…12 and the `var` chain grow polynomially, not exponentially;
  - the failing-leaf deep DAG finishes, with the same diagnostic as before, far under the old 130 s;
  - no segfault at depth 1000 on the default stack;
  - B1–B6 are re-measured and committed;
  - the gate passes on every fixture, including failing ones.

**Decision gate (you, after Team 0).** Re-run the full 28.5 suite with `moss-opt` and send Bank a one-page before-and-after. Then continue straight to Team 1 unless Bank stops you. Teams 1–4 are approved on the expectation that the firewall and the shared store still show large wins after linear walks. Bank uses this gate to confirm it.

**Team 1 — Identity, hashing and determinism.** No semantic change.
- Declaration identity via `entity-v1` (module, kind, name, owning type for methods).
- A canonical EST serialization per declaration, excluding whitespace, comments and positions, and its hash.
- Type content hashes, which include each type's method set.
- A compiler build ID, combined with the language/ABI version and semantic flags.
- **Position-independent diagnostics:** anchored to the declaration plus a relative position, mapped back to absolute lines at report time. The correctness gate compares **after** this mapping.
- **Deterministic diagnostic order:** by file, line, column, code.
- **The rename:** `inferred_checker_signature()` becomes the convergence fingerprint, everywhere.
- *Done when:* moving a declaration down by N lines, or editing a comment, changes no declaration hash; output is byte-identical to before on the whole corpus; the gate passes.

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
- Internal type variables in the signature model.
- Migrate the SDK and packages to absolute traits.
- *Done when:* the corpus and SDK pass, and every built-in operation in the checker maps to exactly one built-in trait. Agent 2 must try to find an operator or built-in that maps to none or to two.

**Team 3 — The `.mossi` firewall (hardened signatures).**
- **Derivation is checking.** At package build, specialize each exported generic against an **archetype**: a synthetic type per parameter carrying only the methods the body calls, with results as internal type variables, effects as **effect variables** (`acc(r, path)`, `raises(r)`, `obs(r)`), and no copy facts assumed. Every question the checker asks of the archetype becomes a requirement or a constraint. Extend v10's `OpenCallableSummaryFacts` and `effect_dependencies` from callbacks to method requirements, and from values to variables. The walk is Team 0's linear walk.
- **Publish the hardened signature:** trait covers or `any` per parameter; effect formulas (access = local ⊔ the requirements' access; raises = local ∪ the requirements' raises, minus recovered; observable = local ∨ the requirements' flags); and constraints as inequalities over effect variables. Use a new `.mossi` ABI version, keeping the semantic IR bodies, which execution and emission still need.
- **Consumer side,** at a call to a hardened export: conform each argument to its traits, cached per (trait, type); unify the type variables; substitute the concrete methods' effects; check the constraints. **No walk across the boundary.**
- **Fast Debug** resolves dispatch at runtime from the hardened signature plus the value's concrete type, not from per-specialization records, for hardened exports.
- **The native emitter** keeps its existing per-type walk for emission; that's unchanged in this phase.
- A **drift report** at package build: exported signatures widened, narrowed or changed since the last `.mossi`.
- **Migrate** SDK exports using the suggested-trait errors.
- *Done when:* **B1** consumer check time is flat in D, W and K; the gate passes; M1 is re-measured after migration; and Agent 2 has confirmed that no consumer path opens a provider body during checking.

**Team 4 — Shared store and incremental check (no daemon).**
- **Per declaration:** direct facts (local types, access per path, raises, observable flags, copy facts) and an **inferred signature** expressed as a formula over its callees' signatures.
- **Per call edge: guards,** the checks the caller's check evaluated about the callee, in these kinds:
  - type (exact type, conforms to a trait, primitive);
  - dispatch (method resolves to `T.m`);
  - access (`acc(callee, path) ≤ mode`);
  - borrow (the callee doesn't write the live paths);
  - raise (`raises ⊆ recovered set`, or folded into the formula);
  - observable (no `io`, message, panic or divergence in initializer contexts).
- **On every `moss check`:**
  1. Hash the reachable files, skipping any whose mtime and size are unchanged.
  2. Reparse only changed files; match declarations by identity and compare canonical hashes.
  3. Re-derive changed declarations.
  4. For each caller, evaluate its guards: if all hold, re-evaluate its signature formula only; if any fails, re-walk the caller.
  5. Continue upward in one topological pass.
  6. When a handler's footprint changes, rerun that domain's synchronization analysis from the handler signatures.
  7. Answer everything else from the store.
- **Opportunistic internal signatures:** archetype derivation for internal generics, with partial traits allowed and silent fallback.
- **The shared store:**
  - One per user per machine, at `~/.cache/moss/store/<build-id>/` (overridable by environment variable). Every worktree uses it, and nothing points at a specific worktree.
  - Keys hash the build ID, the canonical body, type content hashes, **the callee facts its guards reference**, and imported hardened signatures.
  - Values are immutable and checksummed, written temp-then-rename, verified on read, and memory-mapped lazily. Concurrent `moss` processes are safe without locks; two writers of the same key write identical bytes.
  - **Cached ESTs keyed by file hash,** so unchanged files skip parsing and Fast Debug gets bodies without reparsing.
  - **Root tables per worktree,** carrying a generation number, kept in the worktree's build directory.
  - **Garbage collection:** `moss store gc`, plus opportunistic collection during a check under a lock file. It marks everything reachable from live root tables plus the last few snapshots of each, then sweeps under a size budget. No git awareness anywhere.
- **The Phase 22 queries read the store**, and `moss effects++` adds direct facts, guards, and a **blast-radius query** ("if X changes to Y, which guards fail?"). Every answer is tagged with its generation and revalidates before answering.
- Today's in-process memo stays as the dedup inside any walk.
- *Done when:*
  - **B3** rechecks one declaration for edits (a) and (c), and exactly the affected path for (b), plus a new failing edit whose diagnostics match a full recheck;
  - **B2** shows close to one conformance check per new type wherever local signatures exist;
  - **B4** warm runs reuse the store across processes;
  - **B5** shows a new worktree's first check mostly hitting the store, and total CPU close to base plus divergence;
  - **B6** p50 and p95 improve on the post–Team 0 numbers;
  - M2 is re-measured;
  - the gate passes on every benchmark and on corpus replays.

**Team 5 — Fixed-cost floor and daemon assessment.**
- **Cut margo's check-path overhead** (about 45 ms, mostly Python): profile `margo --check build` and remove the overhead without changing what margo decides or produces. For example, avoid redundant manifest parsing and process spawns, or let `moss check` read Margo's package state directly when nothing needs building. Margo still drives builds (D8: `moss build` never triggers a package build).
- **Assess the daemon, without building it.** Measure what's left after Team 4: per-process startup, store open and root-table load, physical memory across N concurrent worktrees, and duplicate work from concurrent checks of the same key. Write a one-page recommendation on whether Peat is worth building, with numbers.
- *Done when:* the B4 package-check floor drops substantially from 46 ms with identical margo outputs, the gate passes, and the recommendation is written.

## Your gate between teams

Verify against **evidence, not the team's report.** Pull the tag, then run `make check`, the 28.5 correctness gate (including failing fixtures), and that team's benchmarks with `moss-opt`. On a failure, send the same team back with the specific failure. Don't start the next team.

## Escalation

Stop and report to Bank only if:
- an invariant can't be held as written;
- a decision listed above turns out to be wrong in practice (for example, absolute traits or hardening break a part of the SDK that can't be migrated);
- the thread budget can't be honored with the tooling available.

Everything else, decide and continue, defaulting to invariant 7 (when in doubt, re-walk).

## Final deliverable

1. All six teams tagged, with a full 28.5 benchmark run committed as `benchmarks/ect_check/PHASE_28_6_REPORT.md`, comparing the 28.5 baseline, the post–Team 0 numbers, and the final numbers, benchmark by benchmark.
2. A short final report to Bank:
   - before and after for B1–B6 and the W4 and diamond extensions;
   - M1 and M2 after migration;
   - the Rust `cargo check` comparison;
   - any export migration that needed judgment;
   - every place invariant 7's fallback fires in practice, with counts;
   - Team 5's daemon recommendation;
   - confirmation that the gate held everywhere and the two-thread budget held throughout.

## Change log

**Rev 2 (Nov 6 2026), after the 28.5 baseline:**
- Added **Team 0** (linear walks, including the failure path and the stack-depth crash), followed by a decision gate.
- **Removed the daemon** from this phase. Team 4 now delivers a per-machine shared store used by today's commands, with cached ESTs; the store path is `~/.cache/moss/store/`, and the command is `moss store gc`.
- **Replaced Team 5 (Peat)** with cutting the fixed-cost floor (margo overhead) plus a written daemon assessment.
- Adopted the terms *inferred signature*, *hardened signature* and *guard*, and renamed the convergence fingerprint.
- Timing uses `moss-opt` only.
- The correctness gate covers failing programs and compares after position mapping.
- Embedded decisions: effects never fail hardening; Copyable and Primitive are not traits; NonCopyable is the only marker.
