# Moss ECT Check Speed — Complete Proposal

*Phase 28.5 · Nov 5, 2026 · builds on `docs/phase28.5/moss-ect-check-principles.md`*

## Abstract

Moss's ECT check should cost close to zero per edit, and many agents working in parallel worktrees should cost base plus divergence, not N times everything. This proposal turns the agreed principles into a complete design, built from four components:

- **the `.mossi` firewall** — hardened, authoritative bounds on exported generics. Fixes Mossi Leak.
- **an incremental fact store** — per-declaration facts plus *predicates* on each call edge. Fixes Inference Ripple.
- **a content-addressed store** — shared across worktrees by key, never by path. Cuts resources.
- **one resident daemon per machine** — fixes Cold Tax.

Today's in-process memo stays as the dedup inside any walk. Source syntax doesn't change. The checker remains the single semantic authority: every fact is something the checker derived, never something reconstructed elsewhere.

---

## 1. Goals, non-goals, success

**Goals**
1. Fix Mossi Leak.
2. Fix Inference Ripple.
3. Cut CPU and memory for `moss check` across worktrees.
4. Remove Cold Tax.

**Non-goals**
- Native build speed (rustc).
- Fast Debug execution speed.
- New generic-type features. Template types (types with untyped fields) already exist and cross exports; see §4.8.
- Changes to source syntax.

**Success** is measured on the Phase 28.5 benchmarks:

| Benchmark | Target |
|---|---|
| B1 | Consumer check time flat in D, W and K |
| B2 | Marginal cost of a new type close to one conformance check, wherever local summaries are complete |
| B3 | Edits (a) and (c) recheck one declaration; (b) rechecks exactly the affected path |
| B4 | A warm no-edit check close to the client's round trip |
| B5 | Memory close to base plus Σ divergence; a new worktree's first check is mostly hits |
| B6 | p95 latency per edit inside the agent loop's budget |
| Gate | Every result identical to a full recheck |

---

## 2. What the current principles leave open

1. **No language for effect bounds.** "Effects as a formula" is stated, but its grammar and the legality constraints it carries are not.
2. **No mechanism for derivation.** "The checker derives, checking against a stand-in" needs the checker to specialize against an abstract type with *symbolic* effects. Today it only specializes concrete types.
3. **Edge records are defined as data, not as checks.** Storing "the consumed slice" is underspecified. What's actually needed is the *questions* the caller asked about the callee.
4. **Summaries are values, not formulas.** Without direct facts plus composition, a changed callee forces a caller re-walk even when the caller's legality holds.
5. **No equivalence for internal generics.** B2's case is unsolved inside a package.
6. **Incomplete cache keys.** Compiler build, language/ABI version and flags are missing, so facts could be shared across different compilers.
7. **No compute dedup across worktrees.** Two worktrees asking for the same key at the same moment would both compute it.
8. **No daemon lifecycle, protocol or fallback.** Who starts it, how it learns of edits (file watching is unreliable on WSL2), what happens when it's absent, and how versions are matched.
9. **No persistence or garbage collection design.** On-disk format, crash safety, eviction, budgets.
10. **Fast Debug dispatch.** Fast Debug consumes checker-recorded per-call decisions. Without a defined source for those under the firewall, equivalence before the emitter doesn't hold for Fast Debug.
11. **Determinism of diagnostics.** An incremental check must emit the same diagnostics, in the same order, as a full check.
12. **Edits that span packages in one worktree.** These still hit the stale-`.mossi` cliff. Resolved by keeping the one-package rule (§5.6).
13. **Interface drift and migration.** How bound changes are surfaced, and how the SDK gets from today's exports to hardened ones.
14. **A correction to an earlier claim.** Plain `moss check` skips the semantic snapshot pass. The double pass applies to other check-only commands, so the "near-free 2×" is narrower than first stated.

Sections 3–8 close these gaps.

---

## 3. Architecture

```
 moss check / queries / Fast Debug  ── client (computes file hashes, sends overlays)
                │  local socket (per user)
                ▼
 ┌────────────────────── daemon (one per user per machine) ──────────────────────┐
 │  root tables: worktree → {declaration → fact key}                             │
 │  incremental engine: re-derive, evaluate edge predicates, upward DAG pass     │
 │  in-process memo (dedup inside any walk)                                      │
 │  content-addressed store: EST by file hash · facts by key · .mossi bounds     │
 └──────────────────────────────── persisted on disk ────────────────────────────┘
```

Three layers stop work:
- the firewall at package boundaries
- the fact store at unchanged call edges
- the memo within a walk

---

## 4. Component A — The `.mossi` firewall

### 4.1 Rule

At package build, every exported generic is **hardened**. Each untyped parameter binds to visible, absolute traits or to `any`. Its effects bind to a formula over the effects of its required methods. If either can't be done, it's a build error. Source stays untyped.

### 4.2 The bound language

**Types:**
- **`any`:** zero expectations. The parameter may only be moved, stored, returned, or passed on to another `any` position.
- **Traits:** user traits, which are absolute (every method declares parameter and result types), and built-in traits (§4.4). A parameter's bound is a conjunction of traits.
- **Internal type variables** `$0`, `$1`, … link positions that must be the same type (`first: (Vector[$0]) -> $0`) and carry element, key and value types. They are allowed by the spec inside the compiler, and never appear in source.
- **Copy facts are type properties, not traits.** Primitives are primitives: plain assignment copies them. Every other type is copyable with `copy(x)`. The only exception is **NonCopyable**, a marker on a small closed set of types such as `FileIO`. A parameter's bound records at most one of two facts:
  - **"not NonCopyable"**, if the body calls `copy(x)`;
  - **"primitive"**, a type-category check, if the body uses `x` again after a plain assignment.

**Effects.** For each requirement *r* (a method call on a bound parameter, or a built-in operation), the summary introduces **effect variables**: `acc(r, path)` ∈ {READ, WRITE, CONSUME}, `raises(r)`, and `obs(r)`, the observable flags. The export's effects are formulas:

- **Access** per parameter path = local access ⊔ the `acc(r, path)` of the requirements that touch it.
- **Raises** = local raises ∪ ⋃ `raises(r)`, minus the raises the body recovers.
- **Observable** = local flags ∨ ⋁ `obs(r)`.

**Constraints** are the legality conditions the body depends on, written as inequalities over effect variables. For example, `acc(x.reset, x) ≤ READ` holds when a borrow of `x` is live across that call, and `obs(r) ∌ io` holds in a context that must be free of side effects.

### 4.3 Derivation: checking against an archetype

Derivation *is* checking. The checker specializes the exported generic against an **archetype**:
- a synthetic type per parameter, carrying only the methods the body calls;
- method signatures taken from the matched traits, with results as internal type variables until resolved;
- effects left as effect variables;
- no copy facts assumed: neither primitive nor known to be copyable. Each copy fact the body depends on gets recorded.

Every question the checker asks of the archetype becomes either a requirement or a constraint. When the walk finishes, the requirements are covered by traits (§4.4), and the summary is the archetype's requirements, the effect formulas and the constraints.

**Why this is sound:** the checker only consulted the archetype, so any type that satisfies the bound gives the same answers. Sufficiency holds by construction. No separate extractor exists, so there is one semantic authority.

**What's new for the checker:** specializing with abstract receivers whose method results are type variables and whose effects are variables. The v10 open-summary machinery (`OpenCallableSummaryFacts`, `effect_dependencies`) is the base. This extends it from callbacks to method requirements, and from values to variables.

**Hardening fails only when no visible trait covers an export's requirements.** The error suggests the trait to declare. Self-returning methods (rejected since v0.1) and user trait methods whose result depends on an element type are instances of this same error, since no trait can express them.

**Effects never fail hardening.** A generic can't access fields of an untyped parameter, and results come back owned, so the body interacts with a parameter only through whole-value method calls. Each call's effect is a per-method variable, and the export's effects are their composition. Built-in collection operations carry effects declared by the built-in traits.

### 4.4 Covers and built-in traits

**Cover selection** follows today's policy for method ambiguity:
1. a unique exact cover by visible traits — traits with identical method sets count as one;
2. otherwise, a unique minimal superset;
3. otherwise, an error that lists the candidate covers and the exact trait that would have matched.

**Absolute traits are enforced:** omitting a trait method's result type becomes an error.

**The built-in trait catalogue** is closed and versioned with the language:

| Trait | Covers |
|---|---|
| Add | `+`, including `String` concatenation |
| Arithmetic | `+ - * /` with `Int`→`Float` promotion, via a result-type rule over two type variables |
| Integral | `%`, bitwise, shifts — `Int` only |
| Ordered | comparisons |
| Equatable / Key | map and set keys, using today's `KeyProofId` |
| Iterable | element type `$n`; Iterator's `next() -> option[$n]` |
| Indexable | container, key and value types |
| Callable | argument and result types, with effect variables |
| NonCopyable | a marker, not a capability: the closed set of types `copy(x)` rejects, such as `FileIO` |

Primitives conform to the built-in traits. Today's `trait_conforms` rejects primitives for method traits, and that rule is lifted for built-ins only.

### 4.5 The consumer side

At a call to a hardened export:
1. Conform each argument type to its traits, cached per (trait, type) and shared by every generic.
2. Unify the internal type variables to get the result type.
3. Substitute the concrete methods' effects into the formulas.
4. Check the constraints.

There is no walk. Errors land at the call site and name the trait or constraint that failed.

### 4.6 Before the emitter, types are equivalent

All types that satisfy a bound are equivalent for checking, Fast Debug and queries.
- **Fast Debug** resolves method dispatch at runtime from the bound's requirements and the value's concrete type, instead of from per-specialization records. That's a lookup table per type and method name.
- **Queries** compute specialization identity lazily, only for the call site asked about.
- **The native emitter** still needs per-type facts: names, concrete effects for the Result ABI, selected methods, and the specialization graph. It runs a **facts-only** walk per type, with no ownership re-check and no diagnostics, persisted in the store (§6).

### 4.7 Format, drift and migration

- **`.mossi` gets a new ABI version** carrying bounds, internal type variables, effect formulas and constraints, along with the semantic IR bodies it already holds (still needed for execution and emission).
- **Drift is reported, not blocked.** Each build diffs exported bounds against the previous `.mossi` and prints the changes: widened, narrowed or changed. That makes interface changes visible.
- **Migration.** M1 (the share of exports that close today) sizes the work. The compiler's suggested-trait errors make migration mechanical, and the SDK swarm can apply them.


### 4.8 Known gap: exported template types

A type with untyped fields is a template. An exported function that takes or returns one (`export fn relay(b: Box)`) is recorded as generic and re-walked by the consumer per instance, even though its parameter is annotated. This phase does not harden those functions: no package in `sdk/` or `packages/` exports a template type today, and the gap costs check time, not correctness. Until it is closed, the firewall's guarantee is "hardened exports never leak", not "no Mossi Leak". Closing it means treating a template's untyped fields as generic parameters for hardening, including trait cover for field requirements such as `b.value + 1`.

---

## 5. Component B — Incremental check (Inference Ripple)

### 5.1 Facts per declaration

For every declaration, generic or not, the store holds:
- **Direct facts:** local types, local access per path, local raises and observable flags, and copy facts ("primitive" or "not NonCopyable"). Only the declaration's own body contributes.
- **The summary as a formula:** direct facts composed with its callees' summaries, using the same operators as §4.2.

A caller's summary is updated by **re-evaluating its formula** when a callee's summary changes. That needs no body walk. Formulas, not values, are what let a changed summary propagate cheaply.

### 5.2 Edge records are predicates

For each call edge, the store keeps the **set of predicates the caller's check evaluated about the callee**, not a slice of data:

| Kind | Example predicate |
|---|---|
| Type | `result is Int`, `result conforms Ordered`, `result is primitive` |
| Dispatch | `method size on result resolves to Foo.size` |
| Access | `acc(callee, arg0.items) ≤ READ` |
| Borrow | `callee does not WRITE paths {self.buf}` (a borrow is live across the call) |
| Raise | `raises(callee) ⊆ {ParseError}` (recovered in the caller), or "propagated" (folded into the caller's formula, no predicate) |
| Observable | `obs(callee) ∩ {io, message, panic, diverge} = ∅` (caller is a domain initializer) |

**Validation is predicate evaluation** against the callee's new summary. That's cheap and exact. Edges whose predicates all still hold are reused untouched, even if the callee's summary changed.

### 5.3 What happens on an edit

1. The client sends changed file hashes. The daemon reparses those files and matches declarations by `entity-v1` ID.
2. Each edited declaration is re-derived by walking its body (deduplicated by the memo), producing new direct facts and a new summary.
3. For each caller: evaluate its edge predicates.
   - All hold: re-evaluate its summary formula only.
   - One fails: re-walk that caller's body, which creates new predicates and facts.
4. Repeat upward in **one topological pass**. The call graph is a DAG, since v0.1 rejects recursion and keeps the domain graph acyclic, so there is no fixpoint.
5. **Domain re-analysis:** if a handler's footprint changed, rerun that domain's synchronization and conflict analysis from the handlers' summaries. That needs no body walks, and it's bounded to one domain, because domains live in one package.

**Inside a package, a failed predicate is a cache miss, not an error.** Inference is allowed to shift types and effects. An error exists only if the re-walk finds the program inconsistent.

### 5.4 Equivalence for internal generics

Internal generics get **opportunistic bounds**: the same archetype derivation as §4.3, except partial traits are allowed and failure falls back silently. A new internal instantiation is checked against the bound: if it satisfies the bound, it's reused; if not, or if no bound exists, it walks. That gives B2 its equivalence inside a package without new rules. M2 measures how complete today's local summaries are.

### 5.5 Granularity

- **Now: the declaration.** Keyed by `entity-v1`, this works with today's checker internals.
- **Later: the expression**, so a changed result type re-resolves only the expressions that use it. This needs stable `ExprId`s across edits, so it depends on removing the driver's text rewrites and the checker's text-keyed caches. It's an upgrade, not a prerequisite.

### 5.6 Edits that span packages in one worktree

**Decided: the one-package rule stands.** A stale dependency `.mossi` stays an error for `moss check`, per the Oct 26 rule. After editing a dependency, the agent runs `margo build` on it. There is no workspace or monorepo mode: the daemon never derives another package's bounds from source.

### 5.7 Correctness

- **Permanent differential gate:** incremental results must equal a full recheck — verdicts and diagnostics, byte for byte — on every benchmark and the corpus.
- **The daemon audits itself:** it re-runs a sampled fraction of checks in full, in the background, and logs any mismatch as a bug.
- **Diagnostics are ordered deterministically:** by file, line, column and code, regardless of which declarations were rechecked.

### 5.8 Queries (`moss effects++`)

Phase 22's commands become clients of the daemon, gaining:
- direct facts per declaration;
- edge predicates;
- a **blast-radius query**: "if X changes to Y, which predicates fail?" This is answered by evaluating the predicates, before any edit is made.

Every answer is tagged with its worktree's generation and revalidates the slice it touches before answering, so a query never serves a fact the next check would contradict.

---

## 6. Component C — Content-addressed store (worktrees)

### 6.1 Keys

A key is a hash over:
- the compiler build ID, the language/ABI version, and the semantic flags;
- the declaration's canonical EST body, a serialization that is stable and independent of paths;
- the content hashes of the types it references, never their names;
- for each callee edge, the hash of **the callee facts its predicates reference**;
- for imports, the hash of the provider's `.mossi` bound for each export it uses.

**Excluded:** absolute paths, caller file and line, and timestamps. Equal key means equal inputs means equal answer. A callee edit that no predicate references leaves the caller's key unchanged.

### 6.2 Values and sharing

- **Values are immutable records:** facts, predicates, summary formulas, and native per-type facts. Each is canonically serialized and checksummed.
- **EST sharing:** trees are keyed by file content hash, so identical files across worktrees parse once.
- **Root tables:** one per worktree, mapping declaration ID to key, plus a generation counter. These are the only mutable state, and each is written only by its own worktree's requests.

### 6.3 Concurrency

- Reads are lock-free over immutable entries.
- **In-flight dedup:** a table of keys currently being computed. A second request for the same key waits on the first instead of computing it again, across worktrees.
- Daemon worker threads parallelize independent requests.

### 6.4 Persistence and garbage collection

- **On disk:** a content-addressed directory per compiler build (for example `~/.cache/moss/store/<build-id>/`). Writes go to a temporary file and are renamed into place. Checksums are verified on read; a corrupt entry is a miss. Entries are memory-mapped when loaded.
- **Garbage collection** marks everything reachable from live root tables plus pinned baselines (recent commits), then sweeps the rest under a size budget, least-recently-used first. A store built by an old compiler is dropped whole.

### 6.5 What memory costs

Memory ≈ shared base + Σ(per-worktree divergence) + the in-flight working set. Budgets are configurable, and eviction applies only to unreachable entries.

---

## 7. Component D — Daemon (Cold Tax)

- **Topology:** one daemon per user per machine, serving every worktree. One per worktree would forfeit the sharing.
- **Lifecycle:** `moss check` auto-starts it on first use and connects over a local socket readable only by the user. It exits after an idle timeout. On connect, client and daemon exchange compiler build IDs; on a mismatch the client starts a fresh daemon for its own build. Both can coexist, with separate stores.
- **Learning of edits:** the client walks the files reachable from its entry point, hashes them (with an mtime-and-size cache to skip unchanged files), and sends the hashes plus any overlays for unsaved buffers. **There's no file watching**, which is unreliable on WSL2 mounts.
- **Fallback:** with no daemon, the client runs in-process against the on-disk store. It's slower to start but still warm, and results are identical by the gate.
- **Clients:** `moss check`, the Phase 22 queries, and Fast Debug's checking phase. Fast Debug requests checked facts from the daemon, then interprets locally; execution stays out of scope.
- **Limits:** a cap on worker threads and a memory budget. A crash or restart loses nothing but warm state.

---

## 8. Invariants

1. **The call graph is a DAG,** so propagation is a single pass and keys can't form cycles.
2. **One semantic authority:** every fact, bound, predicate and formula comes from the checker.
3. **Equal key means equal answer.** Keys cover every input and nothing environmental.
4. **Incremental equals full,** in verdicts and diagnostics, enforced by the gate and the daemon's audit.
5. **Exported generics are bounded.** The `.mossi` is authoritative for types and effects.
6. **Facts are immutable.** Only root tables change.
7. **No change to source syntax.**

---

## 9. Decisions needed

1. Lifting `trait_conforms`'s primitive restriction for built-in traits only (§4.4).
2. Whether Self-returning methods stay rejected at exports (§4.3).
3. Whether drift reporting stays informational or can be made strict per package (§4.7).

*Decided:* edits that span packages keep the one-package rule; there is no workspace mode (§5.6).

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| Archetype checking is a larger checker change than expected | Start from v10's open-summary machinery; declaration-level only; fall back to the walk where derivation fails |
| Edge predicates are incomplete, so a stale fact gets reused | The differential gate, the daemon's self-audit, and conservative defaults (an unknown predicate kind forces a re-walk) |
| Many SDK exports fail hardening | Measure with M1; suggested-trait errors; migration by the swarm |
| Store growth | Garbage collection by reachability, budgets, a separate store per compiler build |
| Daemon complexity | The in-process fallback has the same semantics; the daemon is only an accelerator |
| Summary churn is high, so the fast path rarely wins | Measure with B6; the formula and predicate design keeps even churned edits local wherever predicates hold |

---

## 11. Validation

The Phase 28.5 benchmarks B1–B6, the W4 depth extensions and the W4 × W2 cross, the Rust `cargo check` comparison, and measurements M1 and M2, all against the targets in §1. Every speed number counts only alongside a passing differential gate.
