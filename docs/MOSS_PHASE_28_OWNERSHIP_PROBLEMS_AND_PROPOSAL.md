# Phase 28 — LOL and local ownership inference: problems and proposed rules

**Status:** Peer-review proposal, 2026-11-02. Language rules below are the current agreed design, **not implemented behavior**. The proposals are intended to be challenged with concrete counterexamples before implementation.  
**Baseline:** Phase 27-closed `main` at `9b840e8` (`phase27-5-5-m1`). **Work branch:** `phase28-work`.  
**Companion inventory:** [MOSS_OWNERSHIP_S3.md](MOSS_OWNERSHIP_S3.md) retains the full history, probes, Map/API decisions and earlier alternatives. **This proposal supersedes its rev-10 §0e and review follow-ups #13–#14:** non-primitive rebinding on the same path is now forbidden; mixed owned/borrowed paths are widened rather than categorically rejected.

## Executive summary

**LOL (Lifetime, Ownership, Layout) is the organizing idea.** Moss always has **one semantic owner** of a non-primitive value. **Lifetime** determines whether a value is permanently part of its owner (a normal field), optionally present (an `option` field), or one of many removable elements (a Vector/Map). **Layout**—inline, heap, borrowing representation, or other implementation technique—belongs to the backend and cannot change Moss behavior.

Four rules resolve the bulk of the ownership ambiguity:

1. **LOL determines what may leave a place.** Unified fields are never extracted; option contents and collection elements leave through their owning APIs; whole eligible values can move.
2. **Infer each non-primitive local assignment's effect from its uses:** READ borrow, WRITE borrow or CONSUME. Calls contribute their checked parameter effects. A CONSUME transfers ownership **at the assignment**, not at its later use.
3. **No non-primitive local rebinding on the same execution path.** Mutually exclusive first assignments are permitted. Writing `b.field` or mutating a collection is not rebinding `b`. Primitives retain ordinary reassignment.
4. **Widen ownership across paths only when a binding is used after the merge.** Use the strongest requirement, **READ < WRITE < CONSUME**, and demand that every reaching source legally supply it. With no post-merge use, branches retain independent effects. No runtime mixed-owner/borrow representation and no implicit copy.

**Consequence:** Moss may reject a theoretically safe borrow-versus-new-object join. This is a deliberate simplicity tradeoff: a single statically determined ownership category after a live merge. Transfers occur only on the path actually executed.

**Compiler strategy:** Extend Phase 27's checker-owned facts and existing interprocedural effect summaries; do not add competing inference in the emitter or Fast Debug. Remove hidden non-primitive clones, preserve domain-leaf provenance, and bring the remaining ten ownership fact-hook exceptions to zero. The Phase 27 suite's three failing jobs are not three ownership-language defects; see §7.

---

## 1. LOL: start with the lifetime of the contained value

The fundamental question is not “Should Moss clone, borrow or move this expression?” It is **“What lifetime relationship does this value have to the place that owns it?”** Once that relationship is known, ownership access can be checked with a few rules.

**L — Lifetime.** From the *containing owner's point of view*, is the contained value always present, present zero or one times, or present zero or many times?

| Lifetime class | Moss construction | What remains true | How a contained value can leave |
| --- | --- | --- | --- |
| **Unified** | Ordinary record/object field | The field exists for the full life of its owner | It **cannot** be moved out; it may be read, written through, or overwritten with another value |
| **Disjoint 0..1** | `option[T]` | The slot can hold one value or `None` | Take via the option's operation; the field becomes `None` |
| **Disjoint 0..n** | `Vector[T]`, `Map[K,V]` (and corresponding collection operations) | The collection exists, though its elements can come and go | `pop`/`remove`/`swap_remove` transfer elements and update the collection |

**O — Ownership.** Every non-primitive value has exactly one semantic owner. READ and WRITE are **temporary access to that owned value**, not a second owner. CONSUME transfers ownership, where the lifetime class permits. There is no user-visible shared/RC ownership and no stored borrow as a data type.

**L — Layout.** Moss does not prescribe inline versus heap representation. Rust `&T`, `&mut T`, temporary guards, backend `Arc` and copy elision are implementation details; the emitter cannot make a hidden semantic copy or invent an ownership decision.

The classes explain a formerly irregular family of operations:

```moss
job.report          # unified field: borrowing is possible, taking is not
job.pending         # option field: can be taken; becomes None
queue[0]            # indexed element: borrow, do not silently clone
queue.pop()         # container transfers an element and removes it
```

Consuming a **whole** Vector/Map *field* is a separate legal operation: the owner retains an **empty collection** in that field. Consuming an `option` *field* leaves `None`. The representation of either result is a backend choice.

General-purpose `option[T]` fields are **new Phase 28 language work**; today's iterator `option` support does not establish that general feature.

### Why this is a useful simplification

There is no special field-versus-element “copy by default” rule. A read of a value inside a containing owner borrows it. If the program requires ownership, the lifetime class tells the checker whether it can transfer, must use a removal API, or must reject.

In particular, a plain unified field is **never partially moved**, even at the owner's last use. A collection element cannot move via `v[i]`; `v.remove(i)` is the explicit ownership operation. A general `replace`/`swap` escape hatch is not in Phase 28.

## 2. The broad problems the current compiler and language leave open

The problems are connected. Local assignment, passing to functions, field reads, collection access, package boundaries and native lowering currently do not reliably express **one** ownership story.

| Problem family | The troublesome shape | Why it matters |
| --- | --- | --- |
| **Eager local moves** | `b = a; echo a` | Today's non-primitive local assignment may consume `a` even when `b` only reads it |
| **Silent copies** | `x = v[0]`, `echo v[0]`, constructor/call/return lowering | Native code may insert `.clone()` without a source request; cost and identity differ from intended borrowing |
| **Inconsistent contained lifetimes** | Ordinary field versus collection index; taking a whole collection or option field | No single rule explains when taking a value is legal or what remains in its owner |
| **Opaque local aliases** | `b = a; helper(b)` | Whether the assignment borrows, mutates or transfers must depend on the *helper's inferred effect*, not a fixed rule attached to `=` |
| **Branch and loop uncertainty** | A local is introduced in different branches or used differently along paths | Ownership becomes complicated if rebinding is unrestricted or if the merged value is sometimes borrowed and sometimes owned |
| **Collection snapshots and access** | `Map.keys()`, `values()`, `get`, iteration, `push` | Borrowed inspection, structural mutation, and ownership removal need clearly different operations |
| **Cross-engine inconsistency** | `ed = edges[0]; ed.cost = 9` | Fast Debug has historically mutated `edges[0]` while native emission silently cloned it |
| **Split semantic authority** | Emitter/optimizer/Fast Debug handling of parameter effects | Phase 27 records ten explicit ownership perturb/drop exceptions; some consumers still ignore or reconstruct decisions |
| **Accepted code rejected by Rust** | Nested mutable calls, object-field borrowing, mutable iteration | Lowering can introduce overlapping Rust borrows even when Moss-level operations can execute sequentially |

The design goal is **not** to add separate borrowing instructions for every example. It is to make each example a consequence of LOL plus the three local-flow rules.

## 3. Rule 1: a local assignment infers READ, WRITE or CONSUME

For a non-primitive assignment `b = source`, the checker examines **all uses of that definition**, including inferred effects of reachable function and method calls:

| Uses of `b` | Effect at `b = source` | Result for the source |
| --- | --- | --- |
| Inspect only | **READ** | Owner keeps the value; `b` temporarily borrows it |
| Mutate without transferring | **WRITE** | Owner keeps the value; mutation is visible through the source |
| Return as owned, store in owned state, or pass to a consuming callee | **CONSUME** | Ownership transfers immediately at `b = source`, if legal |

```moss
a = Edge(cost: 7)
b = a
echo b.cost              # READ: a still owns the Edge
```

```moss
a = Edge(cost: 7)
b = a
b.cost = 9              # WRITE: modifies the Edge owned by a
```

```moss
fn take(a: Edge) -> Edge:
    b = a               # inferred CONSUME because b is returned as owned
    return b
```

There is **one static effect at the assignment**, not a dynamic read-or-move choice. If one later branch merely reads `b` and another stores `b` as an owned value, the original assignment is CONSUME on **either** runtime branch. That is intentional and can visibly reset a consumable source field:

```moss
b = job.pending         # option field: CONSUME; job.pending becomes None here
if should_archive:
    archive(b)          # consuming callee
else:
    echo b
```

For source places, LOL decides whether that inferred effect is legal. A unified field or a Vector element cannot supply CONSUME; an owned local can; an option field can and becomes `None`. An illegal source produces a Moss compile error, **not** an implicit copy.

Borrowed locals cannot outlive their source, escape as borrowed return values, or be stored in an owned field. READ/WRITE aliases still obey exclusivity; overlapping READ+READ is allowed, but overlapping READ+WRITE or WRITE+WRITE on the same collection is forbidden even for different indices. Ordinary record-field leaf distinctions remain.

## 4. Rule 2: no rebinding of a non-primitive local on the same path

The new binding restriction removes the need to model arbitrary chains of changing local provenance.

**A non-primitive local can be assigned at most once along any execution path through that logical binding's lifetime.** This applies whether its assignment inferred READ, WRITE or CONSUME.

```moss
b = a
b = Edge(cost: 10)      # ERROR: second assignment of b on one path
```

```moss
b = a
b.cost = 10             # legal if b has a WRITE borrow
```

The latter changes the **referent**, not what local `b` denotes. Likewise `v.push(x)` and `m[k] = x` are collection mutations, not local rebinding. Replacing a non-primitive *field* value is governed by the field's lifetime and WRITE permissions, not the ban on *local* reassignment.

**Mutually exclusive first assignments are legal:**

```moss
if condition:
    b = left
else:
    b = right
echo b.cost
```

On any executed path `b` is assigned once. The checker must identify a single logical joined binding and prove it is definitely assigned before use. This illustrates the intended *language rule*: current parser/lexical-scope behavior for branch-introduced names must be verified and implemented consistently, rather than assuming this example already compiles.

**Loops:** `for item in items` creates a fresh per-iteration binding; it does not repeatedly rebind the same live local. Updating an outer non-primitive local on subsequent iterations is forbidden. Normal primitive accumulators/counters remain legal:

```moss
total = 0
for item in numbers:
    total = total + item    # legal: Int is primitive Copy
```

A `let`/immutable local does not gain reassignment rights merely because its value is primitive. A consumed non-primitive binding cannot be “reinitialized” by assigning to the same binding; introduce a genuinely new binding instead. Lexical shadowing is a *new identity* and must be distinguished from rebinding, not guessed from a name.

**Tradeoff:** repeatedly rebuilding a non-primitive String or Vector by reassigning the same local is no longer permitted. In-place mutation APIs, explicit builders or different scope structure are the intended alternatives. A peer reviewer should test this cost against SDK code before the rule is frozen.

## 5. Rule 3: widen across control-flow paths, but only at a live join

A *path widening* is needed when mutually exclusive definitions of the **same logical non-primitive local** feed a use after control-flow convergence.

The checker computes the strongest needed effect, **READ < WRITE < CONSUME**, and requires **every reaching assignment** to be able to provide it. READ/WRITE imply borrowing; CONSUME means the joined value is owned. A branch that constructs a fresh owned value contributes an owned result.

### A. No use after the branches: no widening

```moss
if condition:
    b = edges[0]         # independent READ borrow
    echo b.cost
else:
    b = Edge(cost: 10)   # independently owned
    echo b.cost
```

Neither definition reaches a shared use after the branches. They need not agree on ownership category; the checker can keep their effects branch-local.

### B. Live join with legal promotion: allowed

```moss
a = Edge(cost: 7)
if condition:
    b = a                # widened to CONSUME: moves a on this path
else:
    b = Edge(cost: 10)   # newly owned
echo b.cost              # b is uniformly owned
```

The join is owned, because one path produces an owned value. The source `a` can legally transfer, so both paths can satisfy the join.

**Only the selected path performs the transfer.** If `condition` is false, `a` was not moved. Nevertheless an **unconditional later use** of `a` must be rejected because it is invalid along the true path.

### C. Live join with impossible promotion: rejected

```moss
if condition:
    b = edges[0]         # element can only be borrowed
else:
    b = Edge(cost: 10)   # owned
echo b.cost              # ERROR: uniform ownership is impossible
```

The final `echo` only READs. We still reject because the joined binding has one ownership category, and `edges[0]` cannot supply CONSUME. The programmer may use an explicit eligible `copy(edges[0])` or restructure the control flow.

This is a **deliberate restriction**, not a backend limitation. We do not add a tagged “sometimes owned, sometimes borrowed” runtime value.

### D. Two borrowed sources: allowed when legal

```moss
if condition:
    b = left[0]
else:
    b = right[0]
echo b.cost
```

Both paths can supply READ. The checker retains the possible source provenance for lifetime and exclusivity checks. If a later use writes through `b`, each source must legally supply WRITE. A same-collection overlapping access remains a conflict even if indices differ.

### E. Control-flow edges that matter

Definite assignment is required on **all reachable continuing paths**. `return` and `raise` paths do not contribute a reaching definition after a merge. Apply the same logic to `if`/`else`, `match`, `try`/`recover`, loops and backedges. A second assignment to an outer non-primitive local on a loop backedge violates Rule 2; a fresh per-iteration binding does not.

**These rules are about logical bindings and reachability, not matching source text.** Phase 28 needs an explicit, tested binding-identity and control-flow model.

## 6. How the rules resolve the rest of the ownership surface

These are applications of LOL and local inference, rather than more independent ownership models.

| Surface | Consequence |
| --- | --- |
| **Ordinary field `p.payload`** | Borrow READ/WRITE as legal; cannot move it out, even at owner's last use. Overwriting the field is permitted when WRITE is legal |
| **`option[T]` field** | Borrow to inspect/mutate; a legal CONSUME takes it and leaves `None`. General field-capable `option[T]` is new work |
| **Vector/Queue element** | Index borrows, never silently clones or transfers. `pop`, `remove`, `swap_remove` transfer via the collection's API |
| **Map element** | `m[k]` and `m.get(k)` borrow and raise when absent; `m.remove(k)` transfers; `m[k] = value` stores a consuming replacement |
| **Map views and iteration** | `keys()`, `values()`, and iteration provide temporary borrows, not eager cloned snapshots; exclusivity applies for the whole collection |
| **Whole Vector/Map field** | Can be consumed where legal, leaving an empty collection field rather than a missing field |
| **Explicit independent copy** | `copy(x)` (name provisional) creates an independently owned eligible value; no user-defined or implicit non-primitive copy; resources such as FileIO are nonduplicable |
| **Message payload** | Separate immutable capability rule: incoming handler data can be READ/forwarded but not written, consumed, rebound, stored as owned, or replied directly, even when primitive. Explicit `copy(payload)` or a newly calculated reply is allowed |
| **Domain state** | Borrow aliases retain source field/leaf provenance; READ/WRITE effects contribute to the existing synchronization plan; no new lock order or per-index proof |

**One native/Fast Debug proof case** must print `9`, then `9` in all engines:

```moss
edges = [Edge(cost: 7)]
ed = edges[0]            # WRITE borrow inferred
ed.cost = 9
echo ed.cost
echo edges[0].cost       # 9, not an old cloned value
```

The prior native behavior silently cloned `edges[0]` while Fast Debug aliased it. Under LOL + inference, Fast Debug's observable result is the intended behavior.

## 7. What Phase 27 gives us and what Phase 28 must still implement

**Reuse rather than redesign:** Phase 27 delivered retained expression occurrences/IDs, checked type and call facts, specialization contexts, control-flow type joins, and a checker-authority contract. The consumer stage must lower checked decisions, not infer its own clone/move/borrow semantics.

The current checker also already has **interprocedural parameter-effect inference**. In `src/semantic_checker.cpp`, `infer_effects()` initializes source function/method effects to READ, analyzes bodies and callee effects, and iterates until unchanged or at most `4 × (functions + methods) + 4` rounds. This is **not** the separate three-round type/signature inference limit. Existing `.mossi` records carry parameter and leaf effects, while generic specializations and detailed leaf capture can still reanalyze concrete source bodies.

**New Phase 28 work** is primarily:
- Flow-sensitive local **binding identity, definite assignment, per-path assignment count, liveness, source provenance and effect demands**.
- Widening at post-merge uses, with a proof that every path can supply the joined effect and no ownership violation is hidden on a side path.
- Integrating local effects with the **existing** interprocedural/specialized/`.mossi` summaries. Make effect inference explicitly fail if convergence is not reached; do **not** mistake the type inference counter for an effect-convergence gate.
- Recording per-occurrence/per-context **checked ownership decisions** that Fast Debug, emitter and optimizer must follow, with no redundant callee-body walk *per alias* and no source-derived fallback at consumer boundaries.
- Enforcing LOL field/collection operations, general `option[T]`, explicit copy and message-capability rules consistently.

### Current test and implementation debt

The Phase 27 closeout recorded **162/165** jobs passing in default and shadow. Both Rust E0499 failures are the **same** nested-mutation source appearing in two jobs (`swarm_035_nested_mutating_calls.moss`); the other failure is a Cargo jobserver FD problem. Those results are not evidence against the proposed ownership language rules.

Ownership-related validation also includes Group A E0502 projection borrowing, E0596 mutable iteration, and the case where a borrowed String is pushed into an owned Vector (E0308). Some historical source shapes will correctly become illegal without explicit copy/removal, while **valid sequential mutations must compile**; Phase 28 should not reject them just to placate rustc.

The Phase 27 [compiler contract](https://github.com/kuttyb/moss/blob/main/docs/MOSS_COMPILER_CONTRACT.md) records **ten frozen ownership perturb/drop exceptions**. These are consumer-authority test exceptions, *not* ten separate full-suite failures. The target is **zero**. The accepted→rustc allowlist also includes unrelated emitter bugs (`box` keyword, `assert` lowering); ownership cannot be credited with fixing those.

Checker-internal string reparses and driver text-to-tree rewrites remain explicitly recorded **unscheduled** Phase 27 debt. Avoid growing their counts, but do not turn Phase 28 into a general parser rewrite.

## 8. Delivery proposal and acceptance criteria

| Step | Delivery | Evidence |
| --- | --- | --- |
| **A — Freeze language model** | Peer review LOL + inferred assignment + non-rebinding + live path widening; reconcile conflicting old S3 §0e | Accepted and rejected examples, including ergonomic SDK counterexamples |
| **B — Checker facts** | Binding identities, liveness, path/loop assignment rules, local effect demands, provenance and legal joins | Positive/negative `if`, `match`, early return, exception, loop, and alias fixtures |
| **C — Interprocedural effects** | Use existing method/function/generic/`.mossi` summaries, reliable convergence and state-leaf tracking | Deep call chains, imported effects, contradiction/missing-summary tests, no reanalysis explosion |
| **D — LOL operations** | Field, general option, collection, Map/view, explicit-copy and message rules | Tests showing exact source/owner postconditions and no hidden non-primitive clone |
| **E — Consumers and Rust lowering** | Fast Debug, native/`-O`, optimizer and packages obey checked ownership | Perturb/drop tests with zero ownership exceptions; native compile and output parity |
| **F — Migration and final gate** | Update `LANGUAGE_SYNTAX.md`, `MODULE_ABI.md`, Gentle Introduction, SDK and examples | `make check` and `make check-shadow`, accepted→rustc ratchet and documented unrelated baselines |

Key acceptance outcomes:

1. A checker-accepted Phase 28 ownership program emits Rust that compiles, and Fast Debug/native/`-O` agree on output, error and state mutations.
2. Every non-primitive clone is justified by an explicit eligible source `copy`; generated-Rust audit checks its provenance.
3. Ownership perturbation and fact-removal tests cover every consumer boundary; all **ten** legacy ownership exceptions are retired without replacing them with unchecked defaults.
4. Effects and local assignment decisions converge or fail closed with a diagnostic; recursion/generic specialization/import cases do not silently infer READ in place of missing evidence.
5. Domain/collection borrow exclusivity and lock-footprint provenance remain sound. Unrelated accepted→rustc baseline debt is reported accurately rather than counted as Phase 28 completion.

## 9. Focused peer-review questions

Reviewers should challenge the **four rules**, not start with emitter code. Specifically:

1. Is LOL's unified/0..1/0..n classification sufficient for every field and collection extraction in the current language? Identify a legal operation it cannot express.
2. Does local inferred CONSUME at the *assignment point* cause unacceptable observable `None`/empty-field resets? Find a concrete code path that cannot be rewritten safely.
3. Can path-wise non-rebinding be defined using **binding identity** without banning a fresh loop-local or a mutually exclusive definition? What real String/Vector accumulation pattern becomes unreasonably awkward?
4. Is **live-only widening** enough to avoid mixed ownership representations while still handling READ/WRITE borrowed merges, early exits, exceptions and loop joins? Can any reachable path hide a use-after-move or illegal source extraction?
5. Can the checker reuse existing interprocedural effect summaries with bounded, verified convergence and per-leaf provenance, instead of walking callee bodies for every local alias?
6. Do native, Fast Debug, optimizer and imported packages consume **exactly the same checked ownership decisions**? Which test would detect a silent clone or Rust borrow-lowering regression?

**Intentional tradeoffs, not open feature requests:** no hidden non-primitive copy; no runtime owned-or-borrowed union at a live join; no general `swap`/`replace`; no disjoint-index lock/borrow reasoning in Phase 28. Counterexamples may motivate a deliberate later revision, but implementations must not silently relax these rules.

## References and document precedence

- [MOSS_OWNERSHIP_S3.md](MOSS_OWNERSHIP_S3.md) — detailed historical problem inventory and the previously agreed LOL/Map/message decisions. Rev-10 §0e and #13–#14 are **superseded by this proposal** pending reconciliation after review.
- [Phase 27 compiler architecture](https://github.com/kuttyb/moss/blob/main/docs/COMPILER_ARCHITECTURE.md) — implemented checker/fact/consumer boundaries.
- [Phase 27 compiler contract](https://github.com/kuttyb/moss/blob/main/docs/MOSS_COMPILER_CONTRACT.md) — authoritative current invariant/gate and declared-debt list.
- [Phase 27 closeout](https://github.com/kuttyb/moss/blob/main/docs/phase27/27_CLOSEOUT_REPORT.md) and [27.5.5 report](https://github.com/kuttyb/moss/blob/main/docs/phase27/27_5_5_REPORT.md) — suite counts, ownership debt and Phase 27 evidence.
