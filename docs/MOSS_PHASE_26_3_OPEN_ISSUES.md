# Moss: Phase 26.3 — open issues and the ownership invariant

Status: Oct 28 2026, round 3. **The ownership model is frozen.** Both peer reviewers gave GO; what remains is proof obligations, API semantics and implementation work, not reasons to reopen the model. Companion to `MOSS_PHASE_26_2_DECISIONS.md` and `MOSS_LAZY_EXPRESSION_TREE.md`.

**Bottom line:**
- **No open issue is Phase 26 fallout.** All of them predate Phase 26 code changes. Phase 26 work *surfaced* them: the 26.1.1 reachability audit found the borrow-checker rows, and the 26.3 `let` change found the optimizer's reliance on `let`. The one Phase 26 gap is in a gate: the corpus differential didn't compare `-O` output.
- **S3 is a language correction first and a compiler cleanup second.** The current spec requires duplication in several operations. Removing `.clone()` from the emitter can't establish S3 while those operations remain.
- **S3 is its own ownership workstream, not Phase 26 cleanup.** Order is in "Workstream order" below.

## Frozen decisions

- **S1. `let` is an alias for `var`.** Local mutability is inferred, never declared. The parser emits `Stmt::Kind::Var` for both keywords. Branch `phase26-3-let-alias` (`498d72f9`), being merged separately from S3.
- **S2. Assignment moves.** `b = a` on a non-`Copy` value consumes `a`, and any later use is `OWNERSHIP_USE_AFTER_CONSUME`. This is the existing spec (`LANGUAGE_SYNTAX.md` §Ownership and §Pipelines: "rejected rather than cloned implicitly").
- **S3. Zero implicit duplication of non-trivial values.**
  - Every access to an existing Moss value has exactly one ownership effect: READ, WRITE or CONSUME. There is no fourth, implicit "duplicate" effect.
  - Rust generation preserves the checker's ownership decision: READ and WRITE become borrows, CONSUME becomes a move. The emitter has no authority to copy.
  - **When the checker can't prove a lowering that preserves ownership, Moss rejects the program. It never quietly copies.**
  - **The goal:** there must never be two independently owned non-`Copy` Moss values merely because the compiler needed to satisfy its Rust backend.
  - **Not duplication:** a move (bytes may be transferred at machine level); building a new value (string concatenation allocates and fills a new string); compiler-internal handle sharing (`Arc::clone` on a domain route bumps a reference count).
  - The headline is "zero implicit duplication," not "zero-copy," because moves and new construction legitimately copy bytes.
- **D1. Explicit `.clone()` in source: kept.**
  - It READs the original and deliberately builds an independent value. It is not a fourth effect and not `Copy` behaviour.
  - Prohibited for domain handles, pinned capabilities and other resources that can't be duplicated.
  - **Clone eligibility must be defined per type.** Allowing `.clone()` doesn't make every user-defined type cloneable.
  - The compiler never introduces a clone without an explicit source operation or another specifically defined value-construction rule.
- **D2. `Copy` is primitives-only.**
  - Only designated built-in primitive types are implicitly `Copy`.
  - User-defined records are never `Copy`, even when every field is primitive. There is no user-defined `Copy` mechanism.
  - Adding or changing a field must never silently change a type's ownership semantics.
- **D3. Sequencing.**
  - The expression-tree foundation (per-occurrence identity and side tables) comes first. `MOSS_LAZY_EXPRESSION_TREE.md` already says new features such as the Graph DSL are built on the tree, so it is a prerequisite either way.
  - Then S3 enforcement and the Graph DSL run in parallel on top of it.
  - **Neither track defines ownership rules on its own.** Graph DSL lowering consumes the same checked ownership facts as every other Moss feature.
  - Graph construction is exactly the indexed-borrow workload (`nxt.push(head[u])` is how adjacency lists are built), which makes the Graph DSL a natural validation target for S3.
- **M1. Ownership at call boundaries follows the callee's inferred effect.** No boundary ever duplicates a value.

| Boundary | Callee effect on the parameter | Caller | Rust |
|---|---|---|---|
| synchronous `message` | READ | keeps ownership | `&T`, a bounded borrow |
| synchronous `message` | CONSUME | loses ownership; later use is `OWNERSHIP_USE_AFTER_CONSUME` | moves `T` |
| synchronous `message` | WRITE | rejected: incoming payloads are immutable | — |
| `executor.invoke` | any | **always CONSUME**, even if the Root body only reads: the caller returns immediately, so it can't lend | moves `T` (`Send + 'static`) |
| `reply` | — | an owned reply must be newly built or moved from an owned value | moves `T` |

  - A handler that replies with its payload gets CONSUME inferred on that parameter, so the value travels in and back out with no copy. With conditional replies, the effect joins to CONSUME if any path consumes.
  - `tests/payload_reply_record.moss` stays positive (the caller doesn't use `data` afterwards). Using the original after a consuming message becomes a negative test.
  - **Domain-state arguments:** passing a domain-state field to a handler that CONSUMEs it is rejected, because a value can't be moved out of locked state. It becomes legal through `replace` on that field (issue 7).
  - **Across packages:** the inferred mode for each message parameter must be visible in `.mossi`. Check handler ABI generation and source-free interfaces.

## Open issues

| # | Issue | Phase 26 fallout? | Next step |
|---|---|---|---|
| 1 | S3 enforcement (spec operations, check location, proof obligations) | No (pre-existing spec and emitter) | Workstream step 4–5 |
| 2 | Group A: E0502 through the field-accessor trait | No (fails identically on pre-26.1 main) | Parked by Bank; candidate fix recorded |
| 3 | T6: untyped `for` parameter inferred WRITE | No (found Sept 23 in the language-surface swarm) | Deferred |
| 4 | `mutate_loop_element`: E0596 | No (fixture added Oct 27; fails identically on pre-26.1 main) | Agents: diagnose |
| 5 | Conformance gates | Partly: the `-O` gap was in a Phase 26 gate | Workstream steps 2–3 |
| 6 | Optimizer trusted `let` for fusion | No (pre-existing); surfaced by 26.3 | Done on branch; merging |
| 7 | `swap` / `replace` built-ins | No (language gap) | Small follow-on; not a prerequisite for S3 |

### 1. S3 enforcement

**1a. Spec operations that change.**

| Operation | Spec today | Under S3 |
|---|---|---|
| message payload | "an immutable value-copy snapshot" | follows M1 |
| forwarding `message` | "a new copy boundary" | follows M1 |
| `reply payload` | "a new semantic value boundary … legal" | follows M1 (inferred CONSUME) |
| `keys()`, `values()` | "eager owned `Vector` snapshots" | borrowed views |
| `executor.invoke` | "crosses a by-value boundary" | CONSUME (M1) |

**1b. Collection access.**

| Operation | Ownership |
|---|---|
| `values[i]` in a READ context | borrow the element |
| `values[i]` in a WRITE context | mutable borrow of the element |
| explicit extraction | CONSUME the element |
| `map.get(key)` | READ of the stored value |
| `map.get(key, fallback)` | READ on `map` and `fallback`; result is a borrowed choice usable only in temporary READ contexts |
| `Map.delete(key, fallback, found)` | READ `key`, WRITE `map`, **CONSUME `fallback` unconditionally**, WRITE `found`. Returns an owned value: the removed entry on a hit (the moved-in `fallback` is dropped), or `fallback` on a miss |
| `keys()`, `values()` | borrowed view, not a snapshot |
| consuming callback over retained elements | reject |

- `Map.delete` consuming `fallback` on both paths is what the spec already says ("a nontrivial binding is consumed on either…"). Making it READ would force a hidden clone on a miss.
- **`map.get` with an inline fallback** (`map.get(k, Record(v = 0))`): the temporary must outlive the borrowed result. That must be a language rule about bindings and expressions, not an emitter convention. Keep the API small; no source-visible lifetime syntax.
- **Implementation choice:** extend Moss's existing temporary READ views, or restrict indexed elements to temporary READ/WRITE uses. Both satisfy S3; returning an owned duplicate does not.

**1c. Where the check lives.**
- **In the semantic IR, before code generation.** The checker attaches an ownership effect to every evaluated expression occurrence; the emitter only translates it. There is no `clone_if_needed`-style escape.
- **Ownership facts live on the lazy expression tree,** in side tables keyed by occurrence.
- **Facts are authoritative across checking, optimization and lowering.** An expression the optimizer creates must derive valid ownership facts for its new context. It must never blindly inherit facts from an occurrence whose context has changed, or fusion could bypass the source's checks.
- **Payoff:** today the emitter decides whether to clone from the shape of an expression (indexed access, object view, borrowed parameter, cross-crate representation, message boundary). With authoritative facts, all of these lower uniformly.
- **Second line of defence:** an audit of the generated Rust that flags any duplication of an existing non-`Copy` value not traced to a source operation, including copies hidden in helpers such as `__moss_value()`. It must be semantic; an emitter-side witness (a source clone ID required by the clone helper) is one way to implement it.

**1d. Proof obligations for synchronous messages.** The compiler must prove statically that:
- a borrowed argument stays live for the whole message;
- no reachable operation can mutate it while it is borrowed;
- locks are held for the required duration and respect the global acquisition order;
- **there is no path back into the sending domain at all, whatever the mode.** If A holds a write lock and B makes even a READ call back into A, `std::sync::RwLock` deadlocks.

Notes:
- This is a deadlock (liveness) rule, not memory safety. Generated Rust contains no `unsafe`, and domain state sits behind `RwLock`.
- Moss's acyclic `domainroutes` is the foundation of the proof, not a substitute for it. **Verify its assumptions cover every re-entry path, including alternate handles and host callbacks.**
- Moving the value out of domain state and restoring it is strictly worse than borrowing. At most a narrow optimization, never the default.

**1e. Still unverified:** whether the checker rejects `nxt.push(head[u])` when the element type is non-`Copy`. By S2 it must, because the line moves out of `head`.

### 2. Group A: E0502 through the field-accessor trait

- **Rows:** `push_self_field_read/single` (`nxt.push(head[u])`) and `field_self_assign_struct/single` (`pos = moved(pos, d)`).
- **Why the accessor trait exists:** `MossAccess_X` lets one method body run on both an owned object and a domain-state view whose fields sit under separate per-class locks. It is a real capability; keep it.
- **Why it fails:** Rust treats each trait method taking `&mut self` as borrowing all of `self`. Single-field getters therefore hide disjointness: `(*self.__moss_field_nxt_mut()).push((*self.__moss_field_head())[u])` is E0502. The direct `impl` path (`self.nxt.push(self.head[u])`) compiles.
- **Moss is right, the lowering is wrong:** "evaluate the arguments, then call" is already the semantics, and path-granular ownership already proved the accesses disjoint.
- **Candidate fix (recorded for Bank, not decided):** one projection method per type, `fn __moss_project(&mut self) -> X_View<'_>`, returning a generated struct of plain field references (`nxt: &'a mut Vec<T>`, `head: &'a Vec<T>`). Once projected, rustc tracks disjoint fields natively, without `unsafe` or temporaries, for owned objects and lock-guard views alike.
- Temporaries (A-normal form) only fix `Copy` or freshly built arguments; both current rows qualify, but a non-`Copy` borrow of one field passed to a mutation of another still fails without the projection.
- **Language impact:** none. This is lowering only.

### 3. T6: untyped `for` parameter inferred WRITE

- **What:** an untyped `for` parameter that is only read is inferred as WRITE. A second specialization then lowers it to `&mut Vec` on a `&self` field, and rustc rejects it with E0596.
- **Source:** `examples/swarm/Surface/Shapes`, `repro_untyped_for_write4–7`.
- **Nature:** effect inference isn't precise enough. S1 doesn't change this; inference was already load-bearing for every bare binding and parameter.
- **Status:** deferred. The gates catch new cases.

### 4. `mutate_loop_element`: E0596

- **What:** both variants are reached through `take_all`, and rustc rejects them with E0596.
- **Status:** not diagnosed. One hypothesis is `.iter()` emitted where `.iter_mut()` is required. Needs one investigation pass to classify as Group A, T6 or a separate lowering bug.

### 5. Conformance gates

Three separate gates, made permanent parts of `make check`:

| Gate | What it proves |
|---|---|
| Checker acceptance → rustc success | accepted Moss can be lowered |
| Fast Debug ↔ native ↔ `-O` native | execution semantics are equivalent |
| S3 provenance and structural checks | no forbidden duplication or extra computation |

- **Turn them on with a ratcheting allowlist.** Group A, T6 and `mutate_loop_element` fail on main today, so the gates are red on day one. A new failure fails the build, and so does a fixed fixture left on the allowlist.
- **Rustc gate scope:** it must cover native `margo test`, not only the corpus differential's accepted sources.
- **Behaviour comparison isn't enough on its own.** The double-evaluation bug from issue 6 printed identical output, because the callbacks are pure. The structural gate also checks:
  - virtualized means no materialized producer;
  - callback-invocation counts match across all three engines.
- **Count in the harness, not inside the callback.** An observable counter inside a pure callback would change which optimizations are legal.
- **The `-O` gap:** the corpus differential reported only `let`→`let mut` changes when `-O` output had also changed; only `shard_phase4_diff` caught it. The equivalence gate closes this.

### 6. The optimizer trusted `let` for cross-binding fusion (done, merging)

- **What was wrong:** fusion needs a binding assigned exactly once. The optimizer took that from `let` by trusting the declaration (`explicitly_immutable`), and from a bare binding by inference; `var` got neither.
- **Fix (branch `phase26-3-let-alias`, `498d72f9`):**
  - single-assignment inference for `var`, as for a bare binding; `explicitly_immutable` deleted;
  - the emitter's virtualized-producer skip covers `var` (without it, callbacks ran twice);
  - checker join and binding-ledger paths recognize `var`;
  - `phase45_single_assignment_var_virtualizes` replaces the inverted test; new negatives for a mutated and a reassigned `var` must materialize.
- **Gates:** `make check` 154/154; default corpus unchanged beyond `let`→`let mut`; `-O` corpus shows only the nine expected changes (five virtualizations, four reason comments).

### 7. `swap` and `replace` built-ins

- **Why:** under S2, `tmp = items[i]` with a non-`Copy` element consumes the whole of `items`, so Moss can't express a swap or an in-place sort over non-`Copy` elements. Nothing breaks today, because the SDK only has `sort_int`.
- **Operations:**
  - `swap(items, i, j)`: one WRITE on `items`. The checker checks that `i` and `j` don't alias `items`; it does **not** prove `i != j`. `slice::swap` with equal indices does nothing.
  - `old = replace(place, new_value)`: WRITE on the place, CONSUME on `new_value`; the displaced value goes to the caller. It applies to fields as well as indices, which is what makes passing domain state to a consuming handler possible (M1).
  - Lowered to `slice::swap` and `mem::replace`. No value is copied and nothing is left partly uninitialized.
- **Rejected:** emitter recognition of the `tmp` swap idiom. Adding a log line mid-swap would silently break ownership checking.
- **Status:** a small follow-on feature. S3 correctness doesn't depend on it.
- **Field:** mutable value semantics (Hylo, formerly Val; Mojo).

## Workstream order

1. Freeze S3, D1, D2 and M1. **Done (round 3).**
2. Finish the `let`/`var` branch (merging) and close the `-O` gap with the equivalence gate.
3. Build the accepts-vs-rustc gate, with the ratcheting allowlist.
4. Build the expression-tree foundation: per-occurrence identity and side tables.
5. In parallel, both consuming the same ownership facts:
   - S3 enforcement: checker first (reclassify or reject operations that need duplication), then remove emitter workarounds (collection, accessor, ABI, callback, message, return and recovery paths), with tests spanning source-backed and cross-package builds;
   - the Graph DSL.
6. Independent adversarial review: Rust borrow correctness, cross-domain locking and re-entry, ownership on exceptional paths, and duplication introduced by optimization.

## Implementation notes for steps 3–5 (not spec changes)

- **`replace` is a compiler intrinsic, not an ordinary `&mut` function.** Called as a function, `replace(&mut self.nxt[u], …)` takes the mutable borrow of the place first and reproduces Group A's E0502. Evaluate `new_value` and every index expression into temporaries before borrowing the place.
- **Inferred effects are part of the exported interface.** A handler refactor can flip a parameter from READ to CONSUME (for example, by starting to reply with it or `invoke` with it), which silently breaks callers in other packages.
  - This is true of every inferred effect, not just M1.
  - `.mossi` diffs must classify an effect strengthening (READ→WRITE, READ→CONSUME) as a breaking interface change and report it as such, rather than leaving consumers to find out from a checker error.
  - Field: Rust's auto-trait leakage through `impl Trait` (an inferred property becoming part of the public contract).
- **Projection views follow the lock footprint.** A mutable projection (`X_ViewMut`) is only for methods whose static footprint takes those fields for WRITE. Read-only methods use a shared projection (`X_View`) or `&self` getters, so read locks are never upgraded to write locks on multi-lock domain views.

## Field pointers

- Mutable value semantics: Hylo (formerly Val), Mojo.
- Reference capabilities for message passing: Pony (`iso`, `val`).
- Evaluation order made explicit: A-normal form.
- Disjoint borrows through abstractions: Rust "view types."

## Revision log

- Oct 28 2026 (round 3): model frozen; both reviewers GO. S3 renamed "zero implicit duplication." D1 kept with per-type clone eligibility; D2 primitives-only; D3 resolved with the expression-tree foundation first, then S3 and the Graph DSL in parallel. 1a and 1b merged into M1, with `invoke` always CONSUME. `Map.delete` consumes `fallback` unconditionally. Re-entry: no path back to the sender in any mode; verify routing assumptions (alternate handles, host callbacks). Optimizer-created occurrences derive their own ownership facts. Issue 2: why the accessor trait exists, and a candidate projection fix. `swap` doesn't prove `i != j`; `replace` works on fields. Gates: three permanent gates with a ratcheting allowlist, counted in the harness.
- Oct 28 2026 (round 2): S3 as a semantic invariant with "reject, never copy"; D1–D3 opened; static proof obligations; re-entrancy as a deadlock rule; reply ownership by inferred CONSUME; check moved to the semantic IR; `swap`/`replace` proposed.
- Oct 28 2026 (round 1): messages are zero-copy; issue 1 expanded into five spec-level categories.
- Oct 28 2026: first version.
