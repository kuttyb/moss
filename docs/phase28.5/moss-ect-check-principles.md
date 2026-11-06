# Moss ECT Check Speed — Starting Principles

*Snapshot of agreements, Nov 5, 2026. Built on `phase28-work` (pre-v0.9).*

## Abstract

Moss wants an edit–compile–test (ECT) loop that beats Rust and stays cheap when many agents work in parallel git worktrees. The check part of ECT should cost close to zero per edit, and CPU and memory should grow with how far worktrees diverge, not with how many there are. We found four problems in the way of that and agreed a direction for each:

- **Mossi Leak** is fixed by hardening exported generics at package build.
- **Inference Ripple** is contained by `moss effects++`: per-edge records of what each caller relied on.
- **Cold Tax** is removed by a resident daemon.
- **Resources across worktrees** are shared through a content-addressed fact store.

None of this changes Moss source syntax.

## Goals

1. Make the check part of ECT approach zero per edit, so the ECT story beats Rust's build-time pain.
2. Serve a multi-agent world: N worktrees cost base + divergence, not N × full.
3. Keep Moss looking the way it does: untyped and inferred in source.

## Scope

- **In scope:** the check part of ECT (`moss check`, Fast Debug's checking).
- **Out of scope:** native build speed, Fast Debug interpreter speed, and generic user-defined types.
- **Source and victim:** the package being edited is the source of change, and its own code is the victim. Downstream packages that use its public interface are out of scope.
- **Domains:** a domain lives in one package and one module. To be enforced as a checker error.

## The four problems

| Name | What it is |
|---|---|
| **Mossi Leak** | Instantiating a dependency's untyped exported generic walks bodies through every package beneath it. The `.mossi` is not a firewall |
| **Inference Ripple** | Inside a package, a body edit can change an inferred summary (types, effects), so callers recheck, possibly up to the roots |
| **Cold Tax** | Every check pays process startup, parsing and `.mossi` loading before any real work, times N agents |
| **Resources across worktrees** | N worktrees each hold and recompute nearly identical state |

---

## 1. Mossi Leak — fixed by export hardening

**Claim:** bounding every exported generic, in both types *and* effects, at package build eliminates Mossi Leak. This is a fix, not an optimization: consumers never walk into a dependency's body.

### Rule

*Every exported generic parameter is bound at package build to a visible trait, or to `any`.*

- **Source stays untyped.** The bound is derived by the compiler; nobody writes it.
- **Hardening:** during package build, the checker walks each exported generic's call tree, within the provider's visibility, and records what it requires of each parameter.
  - If something is required, it must resolve to **visible, absolute traits** (built-in or user). A partial (anonymous) trait at an export is a build error.
  - If nothing is required, the parameter is published as **`any`**: static, opaque, zero expectations.
- **Effects** are published as a formula over the required methods' effects: access (read/write/consume), raises, and the legality-relevant observable effects.
- **The checker is the single deriver.** It records every requirement and every condition it relied on, so the bound is complete by construction. No second extractor, which keeps one semantic authority.
- **Errors are communicated by the build**, not by syntax. For example: "export `f` requires `{done, stop}` of `x`, a partial trait — declare a trait with these methods."

### Supporting rules agreed

- **Traits are absolute.** Every trait method declares its parameter and result types. Today's parser lets the result be omitted, so this has to be enforced.
- **Superset covers are allowed**, following today's method-resolution policy: ambiguity is an error, never a tie-break. Use a unique exact cover, else a unique minimal superset, else an error.
- **Partial traits remain legal everywhere except the export boundary.**
- **Copy classes:** primitive (implicit copy on `=`), aggregate (needs `copy(x)`), uncopyable (`FileIO`). A parameter records the weakest class its body accepts.

### What we give up

Unbounded ("free-running") exported generics. A caller can no longer satisfy an export with traits the provider can't see.

### Hand-wavy parts, to settle

| Item | Status |
|---|---|
| **Built-in trait list** | Needed for operators, indexing, iteration, callables and the copy classes. Draft: Copyable, Primitive, Equatable/key, Ordered, Iterable, Indexable, Callable, plus the operator traits below. Needs one sweep of the checker to confirm |
| **Operator traits: split Numeric** | One "Numeric" trait is wrong. Per the spec: `+` also concatenates `String`; `+ - * /` and comparisons promote `Int` to `Float` when the operands are mixed; `%`, bitwise (`& \| ^ ~`) and shifts (`<< >>`) are `Int`-only. So at least: **Concatenable/Add** (`+`, including `String`), **Arithmetic** (`+ - * /` with promotion), **Integral** (`%`, bitwise, shifts). Promotion relates two operand types, so the bound carries a result-type rule, expressed with internal type variables (below) |
| **Primitives satisfying built-in traits** | Today `trait_conforms` rejects primitives ("only concrete object types can satisfy method traits") |
| **Internal type variables in `.mossi`** | **Direction agreed.** The syntax spec allows generic and template variables *inside* the compiler while forbidding them in source. The `.mossi` uses internal variables (`$0`, `$1`, …) to: link `any` positions that must be the same type (`first: (Vector[$0]) -> $0`); give Iterator a declared element (`next() -> option[$0]`), closing the open `option[_]` gap; and express indexing over container, key and value types (SWARM-044). Never surfaces as a source-level `E` |
| **Effect formulas** | Extend v10's `effect_dependencies`, today limited to callbacks, to method requirements |
| **Self-returning methods** | Rejected in v0.1. Exports that need them fail |
| **User traits whose methods mention an element type** | Can't be absolute. Exports over user-defined containers fail |
| **Fast Debug** | Must resolve method selection from the summary, not a per-specialization record |
| **Generic-type syntax** | Docs and SDK say there is none. To be verified |

---

## 2. Inference Ripple — `moss effects++`

**Principle:** inside a package, stored inferred facts are a **cache, not a contract.** Inference may change types and effects freely; that is the point of the language. A broken fact means invalidation, not an error.

### Two outcomes per edit

| After an edit | Meaning | Cost |
|---|---|---|
| **Consistent** with the stored facts | Nothing a caller relied on changed | Fast: stop |
| **Violates** a stored fact | The cached inference is stale | Slow: walk and re-infer from that point, recursively, stopping at the first level that is consistent again |

An error exists only if the re-walk finds the package truly inconsistent.

### Invariant: the call graph is a DAG

Moss v0.1 rejects recursion (`RECURSION_CYCLE`) and keeps the domain graph acyclic. So call edges, message edges and generic instantiation chains form a strict DAG. Re-inference after an edit is **one topological pass** upward from the edit, with no fixpoint iteration, and it always terminates.

### Store what each caller consumed, not the callee's summary

Facts are stored **per call edge**: what the caller relied on, not the callee's whole summary. Two callers of the same function can rely on different things, so a callee change can force one to recheck and let the other skip. Storing only the callee's summary would force every caller to recheck on any change.

### `moss effects++` = Phase 22 plus three augmentations

Phase 22 already built the query surface: `resolve | inspect | type | effects | ownership | calls | why | cost`, durable `entity-v1` IDs, per-parameter read/write/consume, transitive `ObservableEffects`, and call edges. It recomputes everything in a fresh process per query. The augmentations:

1. **Direct (local) facts** per function, alongside the transitive ones. Phase 22 marks direct effects `not_retained`.
2. **What each caller consumed, per edge,** recorded by the checker as it checks.
3. **Kept resident** (see Cold Tax).

That gives a **blast-radius query**, "if I make this a write, which callers break?", answerable before an edit.

### Benefit for agents

Source stays terse for humans. The checker's inferred types and effects become a queryable side table, so agents get the verbose, hand-annotated view on demand, and it can never go stale the way hand-written annotations do.

### Hand-wavy parts, to settle

- **Exact contents of an edge record:** what "consumed" covers for types, access paths, raises and copy class. Also `may_panic` and `may_diverge`, because domain initializers must be free of side effects, which is a checker error (both bits already live in `ObservableEffects`).
- **Freshness:** a query must never serve a fact the next check would contradict. Revalidate the slice it touches, or tag answers with an edit generation.
- **Summaries as formulas over callees,** so a caller's summary updates without a body walk.

---

## 3. Cold Tax — resident daemon

A resident process holds the EST, the facts and the edge records. Startup becomes zero. Phase 22.5's own measurement supports this: about 7 ms median per query, with process startup "a material share" of it.

---

## 4. Resources across worktrees — content-addressed fact store

**Principle:** don't version the EST; **content-address the facts.**

- **Key** each declaration's facts by a hash of their inputs: its own body, plus the hash of **only the slice of each callee's facts that it consumed**, Merkle-style. A callee edit that no caller consumed leaves upstream keys unchanged, so other worktrees' cached entries stay valid. Equal key means equal inputs means equal answer, so reuse across worktrees is correct by construction.
- **Keys are acyclic.** The call graph is a DAG (see the invariant in section 2), so Merkle keys can't depend on themselves and need no fixpoint.
- **Each worktree is a root table** mapping declaration ID (`entity-v1`) to fact hash.
- **Identical declarations are stored once.** Memory ≈ shared base + Σ(divergence).
- **Entries are immutable,** so worktrees read and add concurrently without locking. Only each worktree's own root table is mutable.
- **The baseline** comes from a clean build at a commit and is shared by every worktree branched from it.

### Hand-wavy parts, to settle

- **Garbage collection** of entries no root table references any more.
- **Cache keys must hash type contents, not type names.** A persisted cache keyed on names goes stale when an agent edits its own type.
- **Strip caller-local data** (source file and line in requirement origins) from shared entries.
- **A hit must be free.** Today's hit path recomputes concrete effects (`spec_cache_hit_recheck`).

---

## Open questions

1. Built-in trait list (including the operator split), and primitives satisfying it.
2. Format of internal type variables in the `.mossi` (direction agreed).
3. Contents of an edge record.
4. Freshness rule for queries.
5. Garbage-collection policy for the fact store.
6. Invalidation unit inside a body (file vs declaration vs expression, via the EST).
7. Edits that span two packages in one loop: they hit a stale `.mossi` and fall off the fast path.

## Measurements before building

1. **SDK exports reaching `scheme_complete` today:** a first estimate of how many exports would fail hardening.
2. **Share of internal callees without a complete summary:** today's slow-path rate.
3. **Summary churn from git history:** how often the fast path would win.
