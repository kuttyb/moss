# Phase 28 Round 4 — General Ownership Inference Plan

**ID:** R04-T01-P01
**Status:** planning only; no Round 4 implementation is authorized
**Branch:** `phase28-r4-checker-plan`
**Reference baseline:** `origin/phase28-r3-team-b-effect-consumer` (Team B W06, including Team A's checked selector-effect handoff). This is the latest inspected Round 3 implementation branch. It is **not confirmed integrated** into `phase28-work`; no integration confirmation was found during planning.
**Contract:** the Nov 2 Phase 28 LOL proposal is the active ownership design. `MOSS_OWNERSHIP_S3.md` is explicitly historical and superseded; use it for rationale/probes only where it agrees.

## 1. Current implementation coverage

This assessment describes the inspected reference branch and source code, not an assertion that the branch is integrated or that every behavior is accepted by all consumers.

| Area | Status | Existing infrastructure and boundary |
| --- | --- | --- |
| Local READ/WRITE/CONSUME inference | **Partial** | `straight_line_alias_effect` in `src/semantic_checker.cpp` infers an alias demand from a bounded set of later statements and checked member-call requirements. The resulting `TypedLocalOwnershipFactV1` is EST/statement/binding/specialization keyed. Many expression forms, nested flow, and cross-function summary propagation are not covered by this path; established `OwnershipEnv` transfer checks remain active outside it. |
| Local rebinding and assignment identity | **Partial** | `OwnershipEnv` tracks binding identities and moved state; body typing has binding facts and branch predecessor identities. A whole-local reassignment is represented in some existing checker paths, but inferred ownership is not uniformly per-definition across rebindings, and the alias scan stops at the first rebinding instead of solving old/new definitions over the full function. |
| `if`/`else` and live joins | **Partial** | Body typing retains joined binding predecessor IDs and control-edge records. The ownership-fact path can recognize a constrained two-arm join and attach the shared join identity. It does not generally compute reachability/liveness and effect requirements across arbitrary nested branches or early exits. Existing moved-state merge logic is separate from inferred effect widening. |
| `match` branches | **Partial / mostly missing for general inference** | Ownership checking handles statement-level match paths and merges moved state, while the local alias scan explicitly declines `Match`/`Case`. No function-wide per-definition READ/WRITE/CONSUME widening or ordinary-match last-use borrow facts are published. `match consume` is intentionally outside the initial slice. |
| Loops and backedges | **Missing for general inference** | Vector loop-element READ/WRITE facts exist, keyed by the loop statement and iterable occurrence. Loop-carried local aliases are explicitly unsupported by the scanner; ownership checking currently rejects some consumed outer bindings on continuing backedges (`LOOP_OUTER_BINDING_CONSUMED`) rather than computing a general ownership fixed point. No blanket inference convergence solution exists for ownership. |
| Early returns | **Partial** | Ownership flow marks a path non-continuing after return and excludes it from some existing joins. The alias scan can recognize direct return of the alias as a consume sink in supported straight-line scans. There is no general live-use/reaching-definition fact contract across nested branches or mixed exits. |
| Typed exceptions / recover | **Partial** | Existing checker builds exceptional ownership exits, uses typed raise alternatives to select `recover` arms, and carries moved state at those edges. The ownership alias inference scan declines `try`/`recover`; per-definition effect widening from recover uses and full local ownership facts on exceptional edges are absent. Unknown/incomplete exceptional summaries must remain conservative. Panics abort, and typed raises do not roll back earlier effects. |
| Borrow lifetimes and source provenance | **Partial / missing for general aliases** | Local facts carry one `source_binding_identity`; take facts carry exact root identity and typed projection paths. No general borrowed-local fact records last-use boundary, a set of possible source origins through joins, or exclusivity interval. Field/index alias provenance and borrow-end facts are not published generally. |
| Function and method effects | **Partial** | The checker has concrete and open callable summaries, inferred parameter effects, member requirements, and specialization-specific effect computation. Straight-line alias inference consumes some already-checked member requirements. Integration of those summaries into every local definition and caller path is not complete; inferred local ownership must consume, not recompute, callee semantics. |
| Generic/specialized effects | **Partial** | Stable specialization keys and checker context mappings exist for current ownership/take records. Effect summaries are specialized and checked, but general local facts are not yet proven complete for every specialized body/context. Facts must be exact per specialization; no fallback or blanket catch is acceptable. |
| Package/imported effects | **Partial / open** | Typed/open summaries and imported `.mossi` contracts exist. Compiler contract still lists imported effects as open for ownership. Missing, incomplete, or contradictory imported summaries must not silently become READ or remove exceptional edges. |
| `take(place)` | **Working for bounded Round 3 slice** | Checker publishes exact take requirements/facts for supported root statement slots, eligible terminal types, projection paths, and replacements. Native/Fast Debug consumers use the facts on the inspected W06 branch. Composition and unsupported forms remain deliberately outside this plan's initial implementation. |

Round 3 established a fact-consumption pattern; it did not complete general ownership inference. The current `TypedLocalOwnershipFactV1` describes one local binding or Vector loop element and has an intentionally unsupported state. `TypedTakeFactV1` is a separate, narrower explicit-operation contract. Round 4 should evolve the local ownership path and reuse checker control-flow/exception facts; it should not infer semantics from consumer behavior or add an independent consumer classifier.

## 2. Proposed algorithms and rationale

### 2.1 One checker control-flow representation

Extend the checker’s existing typed statement occurrences, branch predecessor/control-edge identities, and exceptional exits into one checker-owned per-body control-flow representation. It must represent statement/expression occurrences, normal fallthrough, branch alternatives, loop entry/backedges/exit, return, typed raise edges to applicable recover arms, and non-returning exits. Reuse this representation for ownership and existing reachability/join consumers where practical. Do not build a second ownership-only CFG beside the checker’s typed control flow.

A conventional CFG is the right model because loops, multiple exits, and exceptional successors need explicit edges. A structured AST walk is still useful to construct or validate those edges. SSA is useful as a model for each assignment version and merge (phi-like join), but do not rewrite Moss IR into full SSA merely for ownership. Retain Moss binding identity separately from reaching definition identity; each whole-local assignment creates a definition, while projection writes remain accesses to the original place.

### 2.2 Backward liveness, then forward reaching definitions

Run backward liveness over this graph for local definitions. A definition is live at an edge only if some reachable use can observe it before a rebinding kills that local version. Return, raise, and other terminal exits have no normal successor; typed raises flow only to checker-identified recovery successors. This prevents dead branch-local definitions from forcing unrelated widening.

Run forward reaching-definitions over the same graph, keyed by binding identity and definition identity. A whole-local assignment kills the prior version for that name after successful RHS evaluation and creates a new version; a projection assignment does not rebind the base local. At a join, carry the set of definitions that reach each live use. This is standard reaching-definitions/dataflow, not runtime ownership tagging.

### 2.3 Effect demand and legal-source proof

For each live definition, collect direct uses and checked callee/member argument/receiver requirements. Map them to READ, WRITE, or CONSUME and combine through the finite ordered lattice READ < WRITE < CONSUME. Solve dependencies to a monotone fixed point. Apply the resulting effect at the source assignment, as the proposal requires. Do not defer moves to the sink.

At a live join, select the strongest effect required by reachable uses only if every reaching source can provide it. Borrowed/owned mixed provenance is an error under the frozen design, rather than a tagged union or hidden copy. A join of borrows retains the union of origin paths; WRITE is legal only if every possible source permits it and exclusivity holds. An owned source may satisfy a consume, but a surviving unified field or indexed element may not be implicitly consumed; eligible field extraction requires the separately checked `take` contract. No inferred `take` is introduced.

### 2.4 Loops and convergence

Treat a loop header as a join of its entry definitions and each reachable backedge definition. Iterate reaching definitions, liveness constraints, effect demand, moved/reinitialized state, and borrow origins until stable. Each effect coordinate can strengthen at most twice; finite identity/provenance sets are bounded by the finite body. Use a worklist and explicit convergence invariant. If a supposedly finite analysis fails to converge, produce an internal checker diagnostic with body/context provenance; never silently truncate at a fixed iteration count. Reinitialization on every continuing path makes a consumed outer `var` available for the next iteration only after the reinitializing definition. Zero iterations remain a real exit path.

### 2.5 Exits and exceptions

Return is a terminal normal edge; only fallthrough/reachable successors contribute to later joins. Typed raise edges use Phase 21 concrete raise sets and imported summaries at the exact operation point. A recover arm receives the ownership state at each matching possible raise point, and may strengthen definitions used there. Unhandled raises leave the function and do not flow to normal continuation. Panic is abort, not recover. Existing non-rollback behavior remains: mutations/consumes before a raise are retained on that exceptional path. Unknown or contradictory raise summaries cannot be treated as an absent edge.

### 2.6 Borrow provenance and lifetime

Represent borrow origin as one or more checker-issued source/place identities plus the path where the borrow begins and ends. Last-use is computed by liveness; rebind/drop/consume of a source is illegal while any reachable borrow remains live. Preserve projection path and storage-leaf identity needed by existing exclusivity and lock planning. Do not create stored/escaping references or per-index disjointness proofs: those are outside this ownership contract. Expression evaluation order and operation-level raises matter; edge state must reflect only effects that happen before that raise point.

### 2.7 What not to add

Do not add a runtime owned-or-borrowed representation, implicit clone, ownership-specific optimizer/emitter/Fast Debug analysis, full SSA conversion, interprocedural body rescans, or a second effect summarizer. Reuse checked function/method/generic/package summaries and the existing solver/fixed-point conventions. An ordinary CFG plus dataflow equations addresses the required behavior; novelty is in connecting Moss lifetime/source legality and typed exception summaries to those equations, not in inventing a new algorithm.

## 3. Checker-to-consumer fact/API contract

Facts below must be produced by the checker after the solution converges. Stable identities must be EST occurrence IDs and checker binding/definition/control-edge identities, with exact specialization identity and source provenance. Source spelling may be retained for diagnostics but is not identity.

| Fact family | Required checked contents | Existing / missing |
| --- | --- | --- |
| Local assignment/rebinding | Assignment statement and RHS EST occurrence; binding identity; new definition identity; prior definition killed (if any); source origin/place path; source type; inferred effect; success/raise sequencing; specialization; inference status and completeness | Existing local fact has RHS, source binding, definition, effect, statement, join, specialization and provenance. Missing predecessor/killed-definition relation, general projection origins, successor semantics, and full-body required occurrence coverage. |
| Branch joins | Join identity; each reaching definition and incoming control-edge identity; liveness at join; reachability; owned/borrowed class and all source origins; chosen common effect or checker diagnostic | Existing two-predecessor join records and branch edges are narrow infrastructure. Missing arbitrary multiway/nested/match/early-exit and full ownership/provenance result facts. |
| Loop-carried values | Loop/header identity; entry and backedge definition sets; zero-iteration exit; per-edge availability/moved/reinitialized state; converged effect/provenance; convergence status | No general ownership facts. Existing Vector element facts are not loop-carried local analysis. |
| Consuming calls | Exact call and argument/receiver EST occurrences; resolved target and specialization; checked callee parameter/receiver effect; definition(s) consumed at source; call raise edges | Existing general call/member summaries and selector facts exist; local aliases only read constrained member requirements. Missing one consistent local-definition linkage and coverage for all relevant callable forms/import summaries. |
| Early exits | Return/raise/abort kind and occurrence; reachable successor edges; ownership state at exit; no invented normal definition; per-edge borrow/moved state | Existing `can_continue` and `ExceptionalOwnershipExit` carry part of this privately. Missing complete typed contract for consumers and ownership fact requirements. |
| Borrow start/end/provenance | Borrowing definition, source/place origin set, projection path/storage leaf, READ/WRITE mode, first/last live points or edge set, exclusive overlap verdict, source lifetime bound | Take records describe explicit move projection only. General start/end/origin-set facts are missing. |
| Completeness/unsupported | Required occurrence inventory; authoritative fact or explicit intentionally unsupported reason; context/specialization mapping; duplicate/missing/contradictory detection | Existing required-local occurrence/fact API already distinguishes unsupported from unavailable, and exact specialization mapping exists. Extend its identity checks rather than weaken them. |

Existing consumers that opt into a new Round 4 fact must query by exact retained EST occurrence and checker-published context/specialization. Missing, duplicate, stale, or contradictory required facts must fail closed using the existing internal-invariant path; unsupported remains distinct from missing authority. For each new fact class and each consumer that reads it, add both drop and perturb/contradiction tests on real production paths. No consumer may derive ownership from generated Rust rules, `Expr` text, `Stmt` spelling, or a fallback scanner. This plan’s initial slices should not expand consumer semantics until the corresponding checker facts are complete; consumer migration is a later explicit dependency/handoff.

## 4. Bounded implementation slices and order

These are dependency slices, not parallel workstreams. Each slice must be accepted before the next broadens semantics. Exact filenames are expected; additional files require keeping the one-checker-authority rule.

### Slice A — Per-definition flow model and straight-line/rebinding coverage

- **Behavior:** replace name-centric bounded scanning for ordinary local aliases with exact definition/use edges across straight-line code, including multiple rebindings and source-use-after-last-borrow cases. Infer and apply each definition’s effect independently.
- **Likely files:** `src/semantic_checker.cpp`; ownership fact structures and typed lookup/validation in `src/typed_semantic_api.hpp`; focused `tests/phase28/team_a_ownership/` fixtures and `tests/tooling/check_phase28_local_ownership.py`.
- **Required facts:** assignment/RHS occurrence, binding and definition IDs, overwritten definition, source origin, call-use summary, effect/status, exact specialization, required-occurrence record.
- **Positive fixtures:** READ alias plus later source read; WRITE alias then safe source rebind after last use; old borrow ends before rebinding; consume sink applies at alias assignment; later independent rebinding does not retroactively strengthen old definition.
- **Negative fixtures:** source use while WRITE borrow live; source consumed while borrow live; use-after-consume; consume-demanding call with source still subsequently used; rebinding an open `FileIO` owner cannot bypass required close/raised-exit cleanup; missing/duplicate/perturbed facts.
- **Consumer dependencies:** none for checker acceptance; typed API contract tests. Any existing consumer remains on its current supported fact subset until it can validate the expanded facts.
- **Acceptance:** one authoritative definition per assignment; expected access observed in the exact production checker path; correct negative diagnostics; fact drop/contradiction tests fail closed; no scanner keyed by spelling/text; string-debt count does not grow.
- **Risks:** lexical scope/shadowing and RHS raise ordering can make rebinding semantics ambiguous if identities are assigned too late. Distinguish binding identity from definition identity from the start.

### Slice B — General normal-flow joins and early exits

- **Behavior:** compute live-reaching definitions for nested `if/else` and `match` ordinary branches, with return/raise exits excluded from normal joins. Apply widening only when the local is live after the join; track every predecessor origin. This slice does not make `match consume` general.
- **Likely files:** checker flow representation in `src/semantic_checker.cpp`, possibly shared occurrence/control-edge API in EST/typed semantic structures, same focused fixture harness.
- **Required facts:** CFG edge kinds/reachability, live-at-join, predecessor definition/edge IDs, origin set, selected effect or diagnostic, exit state.
- **Positive fixtures:** dead branch-local borrow/owned values do not widen; same-source borrow join; owned-source consume join; one branch returns while other continues; exhaustive ordinary match with common compatible borrow origins.
- **Negative fixtures:** live mixed borrowed/owned join; any origin unable to satisfy widened WRITE/CONSUME; later source use after a conditional consume; branch borrow that escapes its source; non-reaching unreachable predecessor omitted.
- **Consumer dependencies:** typed fact/API only; existing native/Fast Debug reads of the bounded fact should reject unknown new classes until migrated.
- **Acceptance:** exact reachable predecessor set; only live joins widen; no hidden copies or implicit takes; checker owns any failure; fact corruption cannot be silently repaired by consumer.
- **Risks:** Moss’s current definite-assignment and match exhaustiveness rules must remain authoritative; do not confuse type joins with ownership joins.

### Slice C — Loop-carried definitions and fixed-point convergence

- **Behavior:** solve `while`/`for` ownership over entry, backedges, reinitialization, and zero-iteration exits; remove blanket rejection only where every continuing path has a legal converged state.
- **Likely files:** checker’s existing loop branches and body flow structures in `src/semantic_checker.cpp`; typed fact/API; loop fixtures and convergence instrumentation/tests.
- **Required facts:** loop header and backedge identities, entry/backedge definition sets, iteration state, exit state, converged effects/origins, convergence result.
- **Positive fixtures:** owned vector/worklist reassignment; borrow whose last use precedes next iteration rebind; consume then reinitialize on every continuing path; loop-carried `var` with owned values; zero-iteration source remains available.
- **Negative fixtures:** possible backedge use after move; missing reinit on one continuing branch; borrow overlaps replacement; entry/backedge provenance lacks common legal effect; over-approximate non-reachable backedge does not corrupt result.
- **Consumer dependencies:** none beyond typed facts for checker gates; verify interpreted/native behavior after consumer adoption, not by guessed lowering.
- **Acceptance:** deterministic convergence on finite test graph; no fixed-round truncation; non-convergence is explicit failure; no blanket bans on legal nonprimitive loop state; no stale moved state leaks across exit paths.
- **Risks:** for-loops may have zero iterations and a collection borrow interval; avoid asserting that iterator element effects solve outer local backedges.

### Slice D — Typed exceptional edges, borrow provenance, and summary integration

- **Behavior:** connect each operation’s checked raises and callable summary to exact exceptional successors; carry per-edge ownership state into `recover`; close general source-origin and borrow lifetime checks through branch, loop and recovery flow. Reuse checked local/callee effects and imported summaries.
- **Likely files:** `src/semantic_checker.cpp`; typed/open summary and ownership API in `src/typed_semantic_api.hpp` and relevant EST semantic fact declarations; tests in `tests/phase28/` and tooling gates.
- **Required facts:** exact exceptional edge origin, concrete raised variant set and summary provenance, state-at-raise, recovery entry state, borrow origins/endpoints, callee argument effects, imported summary completeness, specialization identity.
- **Positive fixtures:** pre-raise mutation remains in recover state; one typed variant routes only to its compatible arm; recover use strengthens earlier definition; imported checked summary propagates WRITE/CONSUME; borrow ends before a recover-path mutation.
- **Negative fixtures:** missing/contradictory raise summary cannot erase a possible edge; consumed value used in matching recover; borrow source destroyed before edge use; one recovery origin cannot satisfy required effect; panic does not create recover edge.
- **Consumer dependencies:** full typed facts first; then separate consumer rollout with per-engine drop and perturbation hooks. The first Round 4 checker slices do not authorize independent consumer guesses.
- **Acceptance:** raises preserve exact program point and effects; imported and specialized cases have no context fallback; compiler contract status/debt is updated only to demonstrated coverage; all new consumed decisions have gates.
- **Risks:** typed exception sets and ownership fixed point can interact; keep raise alternatives finite, checker-issued, and distinguish panic/abort from typed raise.

Slices A–C are the minimum core needed to call this general function-flow inference. Slice D is required before claiming complete support for ownership across functions that recover typed errors, borrowing across those edges, or imported effect summaries. `take(place)` composition, collections, copy semantics, and consuming match remain deferred.

## 5. Team A / Team B ownership and handoff

- **Team A (checker/API):** sole owner of ownership semantics, per-body control-flow representation, fixed-point algorithm, source legality/provenance, typed facts/requirements, diagnostics, and checker fixtures. Team A publishes exact API shape and a minimal positive/negative corpus before Team B consumes a new decision class.
- **Team B (consumer/conformance):** owns native emitter, Fast Debug, optimizer consumer work and consumer-side drop/perturbation/parity gates after Team A’s checked API is stable. Team B may prepare fixture harnesses against a documented API but cannot decide semantics, add fallbacks, or modify checker-owned shared files without a coordinated lease.
- **Handoff boundary:** exact branch and SHA; exact typed fact contract; authority/completeness semantics; checker fixture outcomes; consumer query keys; drop/perturb results; unsupported scope; no claim of `phase28-work` integration without Team B/L7 confirmation. Use serialized asynchronous handoffs; no shared mutable build/test paths.
- **Reviews:** Agent 2 performs the requested one-time architectural review of this plan. It is not approval to implement. Future code follows the repository Team operating model and its automated gates; this document does not create new review cadence.

## 6. Adversarial test matrix

| Axis | Positive witness | Counterexample that must reject or route differently |
| --- | --- | --- |
| Definition identity | `x` borrow ends; later `x = fresh()` owns a new version | Old source incorrectly treated as source of the new definition; shadowing merges identities |
| Liveness | branch-local values die before join; no widening | dead predecessor incorrectly forces CONSUME on live definition |
| Join source legality | every reaching origin supports READ/WRITE/CONSUME | one field/element borrow cannot provide owned CONSUME; mixed borrowed/owned representation |
| Rebinding | replacement after final borrow; old owner remains valid | source rebind/consume while alias still live; RHS raises then phantom assignment is recorded |
| Scoped resources | closed resource may leave scope under existing rules | rebinding an open FileIO owner bypasses close or raised-exit cleanup |
| Loops | entry + legal reinitialized backedge; zero iterations | consume on one continuing path without reinit; zero-iteration path omitted; loop demands divergent effects |
| Returns | one branch returns; only fallthrough reaches later use | return path fabricated as a post-join definition or dead code widens a fact |
| Typed exceptions | matching recover sees edge-local state after prior operations | all recover arms start at try entry; impossible variant edge retained; possible unknown edge dropped |
| Borrow lifetime | last use ends borrow before owner mutation | borrow survives source overwrite/consume, or borrowed value escapes lexical owner |
| Callee summaries | exact generic/method specialization effect reaches definition | generic key/context mismatch; consumer reconstructs effect from function spelling |
| Package summaries | complete imported `.mossi` effect and raise summary are honored | missing, stale, open, or contradictory imported summary defaults to READ/no-raise |
| Facts/consumers | exact required fact selects checked answer | dropped, duplicated, stale, perturbed or contradictory fact is silently guessed around |
| Engine agreement | Fast Debug/native/optimized behavior agrees after fact consumption | hidden clone, deferred move, changed exception state, or optimizer reorders across borrow/raise boundary |

## 7. Explicit exclusions and deferred work

- `take(place)` expansion to nested expression contexts; `take(local)` remains absent.
- `copy(x)` implementation or API changes.
- General collection ownership operations, indexed extraction/removal APIs, collection views, or per-index/per-key disjoint borrow proofs.
- `match consume` generalization and new pattern forms; ordinary match branch flow is included only where needed for core function flow.
- New language lifetime classes, stored/escaping borrows, shared ownership, dynamic owned/borrowed tags, and implicit nonprimitive copying.
- Exception rollback/transactions. Earlier mutations remain visible after typed raise.
- Changes to language-level function/effect contracts, message input capability rules, or resource close rules; reuse current frozen decisions.
- Consumer semantic analysis or generated-Rust-based ownership inference.
- Reconciliation/integration into `phase28-work` or `main`; that requires separate authorization and confirmation.

## 8. GO / NO-GO recommendation

**GO to start Round 4 checker implementation after L7 authorization**, with Slice A as the first bounded change and the current inspected W06 branch retained as an unintegrated reference until Team B confirms integration. Before implementation, freeze the shared checker control-flow representation and fact identity schema sufficiently to prevent parallel analyzers and ambiguous consumer keys. Do not authorize all slices as one patch. The principal NO-GO conditions are: no exact source definition identity, no reachable edge model for loops/exceptions, or any proposal to make a consumer decide missing ownership semantics. No Round 4 code is included in this plan.
