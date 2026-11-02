# Moss Phase 28 — LOL, Ownership Inference, and Explicit Moves

**Status:** Consolidated language-design proposal for final adversarial peer review — **decisions below proposed for freeze, not implemented**  
**Date:** 2026-11-02  
**Scope:** Ownership semantics and the checker/consumer contract. Existing source programs, SDK APIs, tests, and the language documentation will require migration if adopted.  
**Terminology:** *READ*, *WRITE*, *CONSUME* are Moss semantic access effects. `take(place)` is the proposed explicit **move/extraction operator** for an eligible surviving field; ordinary local moves are inferred. Code examples are semantic specifications unless explicitly identified as existing syntax. Current parser and APIs may require migration.

## Executive summary

**The thesis:** Moss can have one simple ownership model even across locals, functions, fields, collections, control flow, and domain messages. The model starts with **LOL — Lifetime, Ownership, Layout**:

- **Lifetime** is a language property. A contained value either lives with its owner (**unified**), may exist zero or one times (**disjoint 0..1**, `option[T]`), or is one of zero or many removable elements (**disjoint 0..n**, Vector/Map).
- **Ownership** is singular. Every non-primitive value has one semantic owner. READ and WRITE are scoped borrows; CONSUME transfers ownership. There are no escaping/stored borrows or user-visible shared ownership.
- **Layout** is a backend choice. The implementation may use references, locks, `Arc`, heap/inline representations, and copy elision without changing Moss's observable semantics.

Four rules do most of the work:

1. **LOL governs containment and extraction.** Plain fields remain present and are never partially moved; optional/collection contents can leave only via their owning mechanisms.
2. **Infer each local definition's READ/WRITE/CONSUME effect from all its uses**, including checked callee effects. Whole-local rebinding is legal, including across loop iterations, provided ownership and borrow lifetimes remain valid.
3. **Widen only at a live control-flow join.** Choose the strongest effect that *every reaching source can legally provide*; do not invent runtime owned-or-borrowed values or implicit copies. A dead branch-local definition does not force another branch to change ownership.
4. **Destructive extraction from a surviving place is explicit.** `take(place)` is the *move operator* for eligible `option`, Vector, Map, or Queue **fields**, including eligible fields nested under writable projections. It leaves the field in its defined empty state; collection removal transfers individual elements. `take(local)` is not provided because local moves are already inferred. Ordinary `match` borrows with READ/WRITE inferred until each pattern binding's last use; `match consume` explicitly disassembles an already-owned enum value.

**Baseline invariants:** No hidden non-primitive copies; no drop while borrowed; no borrowed value escaping its source; the checker is the sole authority for ownership decisions; Fast Debug, native emission, and optimization only execute/lower those decisions.

**Why explicit `take` matters:** It removes surprising field resets caused by inferred CONSUME at an assignment or widened join and eliminates the need for a path-sensitive "must-use inferred take" restriction. Typed `raise` **does not** roll back state in Moss; an *explicit* take before a raise remains an observable, programmer-authorized state change. **Exception safety and automatic rollback are a separate Phase 21 problem, not a blocker for freezing Phase 28 ownership.**

**Peer-review goal:** Find the first counterexample that breaks these four rules, not merely a Rust lowering problem. Stress loop-carried data, exception edges, last-use match borrows, nested `take`, pipelines, resources, and cross-package effects. The deliberate expressiveness and failure-safety limits are identified in §9, rather than claimed solved.

---

## 1. Rule 1 — LOL defines what can exist and what can leave

### 1.1 The lifetime classes

Classify a contained value from the **owner's point of view**:

| Class | Typical place | Always present? | Legal way out | What remains |
| --- | --- | --- | --- | --- |
| **Unified** | `job.report: Report` | Yes, for the lifetime of `job` | No partial move; borrow, mutate through, or overwrite instead | A valid `Report` field |
| **Disjoint 0..1** | `job.pending: option[Report]` | No; zero or one value | Explicit `take(job.pending)` | `None` |
| **Disjoint 0..n** | `state.frontier: Vector[Node]`, `m: Map[K,V]`, `queue: Queue[T]` | Container yes; elements no | `take(state.frontier)` for the **whole field**, or the type-specific `pop`/`remove`/`swap_remove` for an element | Empty whole field or collection with the element removed |

A plain field can be **overwritten** through authorized WRITE access, but cannot be left uninitialized by extracting its contents. A Vector/Map/Queue *field* remains a valid empty collection when taken; an indexed collection element cannot be consumed by indexing. The terminal field's **own type** determines eligibility, not whether the access path starts in a local, vector element, map entry, or another field. This is about semantic lifetime, not memory layout.

**General-purpose `option[T]` fields are proposed new language work.** Existing iterator use of `option[T]` does not imply current support for arbitrary option-valued state fields.

### 1.2 One owner, temporary borrows

A Moss non-primitive value has one owner. A local may temporarily refer to it with a READ or WRITE borrow; that local does not become a second owner. Multiple READ borrows are legal; overlapping READ/WRITE or WRITE/WRITE against a conflicting location are not. For Vector/Map, Phase 28 conservatively treats the whole collection as a conflict leaf rather than proving disjoint indices/keys.

Only designated primitive value types are implicitly Copy. A record made entirely of primitives is still a non-primitive record. Strings, aggregates, collections, and `option[T]` are non-Copy. Independently duplicating an eligible non-primitive value requires the **frozen source spelling `copy(x)`**. `copy(x)` READs its argument and produces an independently owned duplicate when the type is eligible; it is not an additional ownership effect and does not authorize implicit copies. **No source-level `.clone()` alias is provided.** Backend Rust `.clone()` may implement a checked, explicit `copy(x)` (or clone an internal runtime handle without duplicating a Moss value), but must never be inserted to repair an otherwise illegal Moss operation. Nonduplicable resources such as FileIO and domain handles cannot be copied.

### 1.3 Layout cannot change ownership

A Rust emitter may borrow, move, use temporary guards, or select a physical representation. It may **not** silently insert a semantic clone because a particular lowering is inconvenient. Fast Debug is a straightforward interpreter of the checker's chosen effects; it does not independently decide whether an indexed read cloned or aliased its source.

## 2. The broad problems this proposal solves

The current historical baseline exposes different symptoms of one missing ownership account:

| Problem family | Representative failure or inconsistency | Resolution |
| --- | --- | --- |
| **Eager local transfers** | `b = a; echo a` rejects even when `b` only reads | Per-definition READ/WRITE/CONSUME inference |
| **Hidden copies** | `v[i]`, field reads, calls, constructors, return and sort-key lowering can emit `.clone()` | Borrow/move or reject; explicit `copy` is the only semantic duplication |
| **Place inconsistency** | Plain field historically moves/invalidates owner while index silently clones | LOL defines both as borrows on ordinary access; extraction is explicit and class-specific |
| **Control-flow confusion** | Different definitions feed `if`, `match`, `recover`, or a loop header | Live-only widening, legal-source proof, possible-source provenance |
| **Loop-carried state** | Banning non-primitive reassignment blocks BFS frontiers, refinement and worklists (recursion is prohibited) | Rebinding is legal; infer over loop backedges to a fixed point |
| **Silent field loss** | Widening an ordinary assignment to CONSUME silently resets an option/collection field, possibly on unused/raising paths | **No implicit destructive extraction from surviving fields**; require `take` |
| **Pattern matching** | READ-only patterns cannot update state; implicit consume would unexpectedly dismantle enums | `match` READ/WRITE borrows; `match consume` explicitly dismantles owned enums |
| **Collection interfaces** | `Map.keys()/values()` clone snapshots; `get`/`delete` fallback semantics obscure ownership | Borrowed views/lookup, explicit removal, no hidden element copies |
| **Cross-package effects** | Callers may see a different READ/WRITE/CONSUME requirement after a library change | Reuse checked concrete `.mossi` effect/leaf summaries and fail closed |
| **Engine disagreement** | `ed = edges[0]; ed.cost = 9` historically prints different source element values in Fast Debug and native | Checker-owned borrow/move decisions, three-engine parity |
| **Rust borrow errors** | Valid sequential Moss mutation can emit overlapping Rust borrows (e.g. nested mutating calls) | Preserve Moss evaluation order and emit legal Rust; do not redefine Moss to fit bad lowering |

The proposal intentionally does **not** add a hidden `clone`, general `replace`/`swap`, first-class references, per-index borrow proofs, dynamic dispatch, or runtime owned-or-borrowed unions.

## 3. Rule 2 — local effects are inferred **per definition**, not per name

A whole-local assignment creates a new *definition* of its binding. Analyze all uses reached by that definition and the effects of any callees those uses invoke:

| Required use | Effect at source assignment | Meaning |
| --- | --- | --- |
| Inspect only | **READ** | Borrow; source remains owned |
| Mutate referent, without transferring it | **WRITE** | Exclusive borrow; mutation reaches the owner's value |
| Return/store as owned, pass to a consuming parameter, or other owned sink | **CONSUME** | Ownership transfer at **the assignment** if the source legally supports moving |

Examples (schematic):

```moss
origin = Edge(cost = 7)
view = origin
echo view.cost             # READ: origin still owns the Edge
```

```moss
origin = Edge(cost = 7)
view = origin
view.cost = 9               # WRITE: mutates origin's Edge
```

```moss
fn handoff(a: Report) -> Report:
  b = a                     # CONSUME is inferred from return
  return b
```

**Each assignment has one statically chosen effect.** If a definition reaches a READ use on one path but a consuming use on another, the assignment has CONSUME on *both* runtime paths. For a normal owned local this can be legal; for a surviving field it is **not** an implicit `take` and must be rejected if ownership is required.

### 3.1 Rebinding is legal

Whole-local `b = new_value` rebinds `b` to a new definition; it **does not write through** a previous `b` borrow. Projection assignment `b.field = x`, by contrast, writes through `b` and requires WRITE permission. A new definition does not resurrect a previously consumed source.

```moss
frontier = [start]
while (frontier |> count) > 0:
  frontier = expand(frontier)    # legal when RHS effects/lifetimes permit
```

Reassignment in loops requires a worklist/fixed-point solution for effects at loop headers and backedges. The effect domain is finite (`READ < WRITE < CONSUME`); each coordinate can strengthen at most twice. A practical worklist iteration should converge, or the checker must **diagnose non-convergence** rather than stop silently. General recursive function calls remain prohibited by the language; this is a dataflow fixed point, not execution recursion.

Loop-introduced bindings and already-existing outer bindings obey Moss's **existing** scoping rule: names first introduced inside a `for`/`while` are not visible after the loop; reassignments to outer names remain visible; names introduced in both `if` and `else` may be visible after their join. The implementation must track **binding identity and reaching definition identity** separately. A fresh per-iteration local is not an erroneous outer-name reassignment.

### 3.2 Rebinding safely ends or drops the old binding

- If the old definition **borrowed** a source, rebinding releases that borrow when no longer live; it does not destroy the source.
- If the old definition **owned** a value, rebinding drops the old value **after successful evaluation of the replacement**, provided no live borrower of the old value remains.
- If a replacement expression itself consumes the old value, apply its checker-proven ownership semantics first; a raised RHS does not count as a successful assignment. Preserve the state reached at the precise raise point.
- Resource types retain their stronger lifetime/close discipline. For example, rebinding cannot silently discard an open FileIO owner and bypass `FILEIO_MUST_CLOSE` or the Phase 21 raised-exit cleanup obligations.

```moss
b = make()
c = b                     # c READ-borrows b's value
b = make2()               # ERROR if c's borrow is still live
echo c                     # c proves the old value is needed
```

We **do not** add a generic "must use an owned local before rebinding" rule. Dropping an unused ordinary owned local is normal; unnecessary implicit destruction of an owner-surviving *field* is addressed instead by the explicit extraction rule.

## 4. Rule 3 — widen ownership **only at a live control-flow join**

At a post-merge use of the *same logical local*, all reaching definitions must admit one common ownership category, with requirements conservatively strengthened under **READ < WRITE < CONSUME**. A newly constructed owned value is owned; mixed borrowed/owned reaching paths therefore demand owned representation. A source that cannot legally supply ownership causes a **Moss checker error**, not a hidden copy, a dynamic discriminant, or backend magic.

If separate branch-local definitions are never used after convergence, **do not widen them together**.

```moss
if condition:
  b = edges[0]              # borrowed READ, used here only
  echo b.cost
else:
  b = Edge(cost = 10)       # new owned value, used here only
  echo b.cost               # legal: no post-join use
```

At a **live** join:

```moss
a = Edge(cost = 7)
if condition:
  b = a                     # widened to CONSUME from owned a
else:
  b = Edge(cost = 10)       # already owned
echo b.cost                 # legal if a can move
```

An unconditional later use of `a` is rejected if any continuing path consumed it, even though only the true runtime path executes that move.

```moss
if condition:
  b = edges[0]              # element cannot be consumed by indexing
else:
  b = Edge(cost = 10)
echo b.cost                  # ERROR: no legal ownership promotion
```

The same rejection applies to a borrowed option/collection **field** when the other branch creates an owned value. The user must explicitly choose a source operation such as `take(job.pending)`, `take(state.frontier)`, `copy(...)` if eligible, or restructure the branch-local uses.

### 4.1 Joins of borrows: provenance is a set

```moss
if condition:
  b = left[0]
else:
  b = right[0]
echo b.cost
```

This is legal when both paths can provide READ and lifetime/exclusivity checks succeed. The checker retains **both possible origins**, not an invented single origin. For a later WRITE, every possible source must support WRITE. A domain lock plan statically includes the **union** of relevant source leaves; this can conservatively lock more than the executed path accesses. Existing approved lock-acquisition/deferral rules are unchanged. The join is not permission to dynamically acquire a new conflicting lock after beginning the protected interval.

### 4.2 Backedges, early exits, and exception paths

Apply the same joining rule to `if`/`else`, `match`, `try`/`recover`, and loop headers/backedges. Reachability and definite assignment matter: a `return` or escaping `raise` does **not** contribute a normally continuing definition at a later join.

A loop header has an entry definition and zero or more backedge definitions. Inference iterates their ownership requirements to a fixed point. No blanket ban on non-primitive loop-carried state.

For a typed `raise` inside `try`, use **Phase 21's inferred concrete raise sets** to add precisely the exceptional edges possible from an operation/call to applicable `recover` arms (including imported summaries). A `recover` sees ownership as it stood at each possible raise point: a source already consumed remains consumed; a use in a recovery arm may strengthen the effect demand on earlier definitions. Unknown or contradictory raise summaries must not silently drop exceptional edges. Panics abort and **do not** enter `recover`.

**No rollback assumption:** Moss Phase 21 explicitly permits partial state changes to survive typed raises. An explicit `take(state.frontier)` before a fallible call is therefore an intentional mutation that can remain visible even if recovery runs.

## 5. Rule 4 — `take` is the explicit *move* operator for eligible surviving places

This is the central change from the earlier Phase 28 proposal.

An ordinary assignment from a field or indexed element can infer **READ or WRITE**, but **must not infer destructive extraction** from a place that survives the operation. If downstream use requires CONSUME, reject with a diagnostic suggesting a legal explicit extraction or an eligible copy.

### 5.1 Proposed `take(place)` semantics: a closed set of movable fields

`take(place)` evaluates an eligible **field place** once, transfers its current owned contents to the result, and installs the **language-defined empty state** at that field. It is a **semantic MOVE**, not a copy. The operation requires exclusive mutable access through the complete projection path and contributes the resulting WRITE/storage-leaf requirements to the checker's synchronization plan. An eligible nested terminal field is permitted even when its owner is an element of another collection.

**Closed set of fields whose whole values can be taken:** `option[T]` (replaced by `None`), `Vector[T]` (empty Vector), `Map[K,V]` (empty Map), and `Queue[T]` (empty Queue). This is a language-defined list, not a generic default/zero-value trait. A user-defined record or ordinary enum field is **unified** and cannot be taken, even if its fields individually have empty states.

| Expression | Result | State after operation | Allowed? |
| --- | --- | --- | --- |
| `take(job.pending)` where `pending: option[Report]` | **`option[Report]`** (possibly `None`) | `job.pending == None` | Yes, when writable |
| `take(state.frontier)` where `frontier: Vector[Node]` | `Vector[Node]` | `state.frontier == []` | Yes, when writable |
| `take(state.index)` where `index: Map[K,V]` | `Map[K,V]` | Empty Map | Yes, when writable |
| `take(state.queue)` where `queue: Queue[T]` | `Queue[T]` | Empty Queue | Yes, when writable |
| `take(v[i].pending)` where `pending: option[Report]` | `option[Report]` | `v[i].pending == None` | **Yes:** final field is eligible and projection is exclusively writable |
| `take(m[k].items)` where `items: Vector[T]` | `Vector[T]` | Empty `m[k].items` | **Yes:** final field is eligible and projection is exclusively writable |
| `take(job.report)` for a normal unified `Report` field | — | — | **No:** cannot leave field absent |
| `take(v[i])` or `take(m[k])` | — | — | **No:** taking an entire element is not permitted; use a collection removal API |
| `take(local)` | — | — | **No:** moving ordinary locals already uses inferred CONSUME; keep `take` specific to surviving storage |

**Important:** taking an *option field* moves the entire `option[T]`, not implicitly the `T` inside it. Destructure the resulting owned option with `match consume` if an owned `T` is needed. Do not let `take` quietly unbox or invent a success value from `None`. A nested take such as `take(v[i].pending)` changes **only that terminal field** (and entails a WRITE conflict on the enclosing collection), not the element slot `v[i]` itself. A conflicting live borrow makes it illegal.

```moss
state = State(frontier = [1, 2, 3])
old = take(state.frontier)       # old owns [1,2,3]; field becomes []
process(old)
```

```moss
pending = take(job.pending)     # pending: option[Report]; job.pending becomes None
match consume pending:
  case Some(report):
    archive(report)              # archive may consume its argument
  case None:
    pass
```

At a live branch join, an owned `take(...)` result may meet a fresh owned value; the required transfer is now **visible in source** and happens only when that branch executes.

### 5.2 Explicit moves remove the dead-take rule

Earlier drafts contemplated prohibiting an inferred CONSUME when it was killed unused on any path, including `return`, `raise`, scope exit, or rebinding. **That rule is withdrawn.** It arose because an inferred CONSUME could silently empty an option/collection field at a distant assignment or join.

With explicit extraction, a programmer writes `take(...)` at the destructive point. If its value is later discarded or a typed raise interrupts the flow, the field stays empty because that was an explicit operation. Ordinary owned locals can also be dropped when no longer needed; no new path-sensitive "must-use" restriction applies.

This does **not** change ordinary assignment into an implicit move-from-field, or permit extracting an entire unified field or indexed element.

### 5.3 Collection ownership APIs

- `Vector.pop()`, `Vector.remove(i)` and `Vector.swap_remove(i)` transfer the removed element and shrink the Vector. `swap_remove` may change order; the compiler cannot substitute it automatically.
- `Queue.pop()` transfers an element.
- `Map.remove(key)` transfers the removed value. Missing-key behavior is the ordinary typed error chosen in the detailed S3 design; no implicit fallback ownership transfer.
- `v[i]` and `m[k]` borrow READ/WRITE; `nxt.push(v[i])` is rejected when push needs ownership. Use `copy(v[i])` if legal, or removal.
- **Vector `for` bindings:** `for item in items` borrows the Vector's elements with an effect inferred from the body: READ for inspection, WRITE for permitted in-place mutation, **never CONSUME**. The loop binding is a borrow, not an extracted owned element. The collection remains conservatively one conflict leaf; a conflicting structural mutation during a live iterator borrow is rejected. The checker records the effect, and native lowering chooses read versus mutable iteration (`iter()` versus `iter_mut()`) from that checked fact—not from emitter guesses.
- `Map.get(key)` is a borrowed lookup (single argument, missing key raises); `Map.keys()`/`values()` and borrowed iteration yield temporary views rather than eagerly cloned owned snapshots. Views may be locally bound while valid but cannot escape into owned state or outlive their source. Structural mutation while a conflicting iterator/view is live is rejected.
- No general `replace` or `swap` primitive is added by Phase 28. This is an **intentional change** from legacy language docs that describe a general `replace` operation.

## 6. Applying Rule 4 to enums — `match` borrows; `match consume` disassembles an **owned** enum

Pattern matching has two deliberately distinct forms. These are not different ownership models: both follow LOL and the explicit-extraction principle.

### 6.1 Ordinary `match`: infer READ or WRITE for payload bindings, never CONSUME

```moss
match job.state:
  case Running(stats):
    stats.done = stats.done + 1     # WRITE borrow of payload inside job.state
  case Idle:
    pass
```

A pattern binding borrows its portion of the scrutinee; mutation is allowed only when the scrutinee's source supports WRITE. The enum retains ownership and remains present. A READ-only source (such as an immutable incoming message payload) cannot be mutated. Ordinary match **never** implicitly dismantles its scrutinee, even if it is an owned local.

**Borrow lifetime is last use, not the whole `match` arm or statement.** After a pattern binding's final use, its borrow may end *inside the same arm*, so overwriting the matched field is legal if there is no other live borrow and the replacement expression is ownership-safe. The source-language rule is last-use-based regardless of how the emitter structures Rust match temporaries.

```moss
match job.state:
  case Running(stats):
    if stats.done == n:
      job.state = Idle       # legal if stats is dead before overwrite
  case Idle:
    pass
```

If `stats` is used after the overwrite, or another alias still borrows the old state, the overwrite is rejected. Last-use inference must also be honored by Fast Debug and native emission.

A pattern payload needed as an owned argument is rejected under ordinary `match`; use an explicitly owned scrutinee with `match consume`, or a legal explicit copy. **This revises the existing READ-only pattern wording in `LANGUAGE_SYNTAX.md`.**

### 6.2 `match consume`: transfer payloads from an owned enum

```moss
cmd = queue.pop()                # owned enum
match consume cmd:
  case Write(buf):
    pending.push(buf)             # buffer ownership passes to pending
  case Stop:
    pass
```

`match consume` consumes the **entire owned enum**, then gives the selected arm ownership of its non-primitive payloads. The original enum is unavailable afterward. This is the consuming pattern operation for owned `option[T]` values as well.

It is **not allowed** on a unified field (`match consume job.state`), on an indexed element (`match consume queue[0]`), or on a temporary borrowed view. First obtain an owned value through a legitimate operation (`pop`, `remove`, `take` of an eligible field, a consuming owned local, or fresh construction).

This prevents having to make every enum variant payload an `option[T]` solely to move it out. `match consume` is the existing explicit extraction mechanism and is retained despite the desire for a simpler `match` surface.

**Pattern limitations remain:** the current Moss syntax requires exhaustive named enum cases; it does **not** support `case other:` as a consuming catch-all or nested patterns such as `case Some(Loading(buf)):`. Phase 28 does not silently invent either feature. Nested enums/options require nested `match consume` statements, and restoring unmatched variants requires explicit arms that reconstruct or put back their values. The practical cost is documented in §9.

## 7. Interactions with functions, exceptions, pipelines, domains and messages

### 7.1 Calls and imported effects

Moss already infers function/method parameter and receiver READ/WRITE/CONSUME effects interprocedurally; checked `.mossi` interfaces carry parameter and leaf-effect data. Phase 28 adds **local-definition effects** that consume these summaries. It must not rewalk a callee body for each alias use or reconstruct effects downstream.

The existing `infer_effects()` algorithm iterates but can exhaust its bound without failing. Replace that silent cap behavior with **an explicit, checked fixed point**. The bound should relate to the actual number of mutable effect coordinates (parameter/receiver/leaf summaries), each of which can rise only finitely, rather than just function count. The separately named three-round type/signature inference cap is a different issue.

Effects can strengthen after a library update without changing a source signature; callers must recheck and reject newly illegal CONSUME from borrowed places. Explicit `take` remains a visible source operation across package boundaries.

### 7.2 Exception edges are effect-precise

Phase 21's typed raise sets provide the potential exceptional successors of an expression or call. The ownership checker propagates states to `recover` **from each operation that may raise a matching typed variant**, after any source effects that occur before the raise. Recovery arms do not automatically start from the pre-`try` state; moved/maybe-moved values must be checked at their actual possible entry states.

A helper returning normally can READ a value while a raised path reaches recovery after a prior `take`. A `recover` use may raise the inferred effect at an earlier local definition, but cannot make an illegal source move legal. The checker must preserve the Phase 21 ordering/exception topology. A `panic` is not a recoverable typed raise.

### 7.3 Eager pipelines and captures

Moss `map`/`filter` pipelines are logically eager, with ordered stage semantics. They are **not lazy views**. General first-class lambdas/closures are not in the v0.1 language; single-expression placeholders and statically bound method stages have restricted capture behavior.

```moss
b = v[0]
ys = xs |> map(_ + b.cost)       # illustrative placeholder capture: READ within evaluation
v.push(Edge(cost = 1))          # legal if the pipeline was evaluated and b has no later use
```

A capture counts as a use while its eager expression/stage is evaluated; it cannot escape or be retained in `ys` as a closure. A named callee contributes its checked READ/WRITE/CONSUME requirements; existing Moss capture restrictions (including READ-only surrounding capture rules) remain unless deliberately revised. Optimized fusion cannot move accesses, fallible effects, or borrow lifetimes across a source boundary in a way that changes observable behavior or the checker-proven ownership plan.

### 7.4 Borrow joins and synchronization provenance

At a join of two borrowed definitions, track the **set of possible source storage leaves**. Lifetimes, alias conflicts, and domain effect footprints are checked conservatively over that set. Static lock planning includes the union of relevant leaves, potentially over-locking relative to the runtime-selected path; global lock ordering, existing handler 2PL, and approved lock deferral are unaffected. No per-element lock partitioning is introduced here.

### 7.5 Message inputs: primitive replies permitted

Incoming handler parameters remain immutable semantic by-value snapshots **for every type, including primitive Copy types**. They may be **READ or synchronously forwarded** but not WRITE, CONSUME, rebound as the original input binding, or moved into owned state. Thus `n = n + 1` is illegal when `n` is the incoming handler parameter, even if `n: Int`. `result = n; result = result + 1; reply result` is legal: the primitive value is copied into an ordinary mutable local. A nested call or local alias cannot launder a non-primitive incoming payload into an owned reply, because the required CONSUME is forbidden. Explicit `copy(payload)` of an eligible value produces a separate owned replyable value.

**Primitive Copy input replies are allowed**, including direct `reply n`, a primitive local copy, and an ordinary function that returns a copied primitive. No special primitive return-origin summary is needed in `.mossi`; banning `reply n` while allowing `reply n + 0` would create an arbitrary provenance restriction. This reverses the earlier Phase 28 proposal to forbid even primitive replies and must be recorded explicitly when updating language/ABI documentation.

For **non-primitive** input values, ordinary `reply payload` remains illegal under the new ownership contract because the reply needs a separately owned value. A helper's existing CONSUME summary propagates that illegality through ordinary calls. READ-only helpers and synchronous forwarding remain legal.

A message is a semantic by-value boundary, but internal synchronous forwarding may lower to immutable views while source/guard lifetimes remain valid. No physical boundary authorizes a hidden non-primitive clone.

## 8. Observable implications and deliberate tradeoffs

1. **Safer, more ergonomic loops:** BFS frontiers, fixed-point refinement, worklists, and builders may rebind non-primitive locals. The checker must solve loop-carried effects and prohibit drop-while-borrowed; it can no longer rely on a single acyclic backward scan.
2. **Destructive field changes become visible:** `take(state.frontier)` explicitly empties the field; a plain `b = state.frontier` does not. Old implicit field-move code will get new compile errors with actionable diagnostics.
3. **Explicit take is not transactional:** If later code raises, a previous take remains effective. Moss's typed raises do not roll back domain state. A state-machine field may remain empty after a partially completed transition; this is a genuine application-invariant/exception-safety issue, **separately owned by Phase 21 rather than a blocker for freezing LOL**.
4. **All legal owned/borrowed joins have one static category:** A join may conservatively reject a program that is safe at runtime, particularly an owned-vs-element-borrow case. Restructure branches or explicitly copy/remove as appropriate.
5. **Borrowed state machines become mutable:** `match` payloads can WRITE with permission, and their borrows end at last use even inside an arm; this permits inspecting a variant then overwriting its field. This changes the existing READ-only/whole-match-lifetime wording.
6. **Owned enums remain usable:** `match consume` handles command/event dispatch and iterator `Some(value)` extraction without adding optional states to every variant payload.
7. **Map/Vector/Queue SDK changes:** Snapshot-oriented code using `keys()/values()`, implicit indexed copies, and old `get/delete` fallbacks must be rewritten; explicit `copy` is available for eligible values when independent snapshots are genuinely needed. Whole Queue fields also support `take`.
8. **Resource destruction has timing:** Rebinding an owned value drops its predecessor at assignment completion, subject to explicit close/destroy obligations. A borrow-only rebind releases a borrow without destroying the source.
9. **Synchronization can become more conservative:** A merged borrowed value can refer to either source; domain footprints include both. This is a predictable performance cost, not a semantic hole.
10. **Compiler scope is substantial but bounded:** The new machinery is CFG reaching definitions/liveness, loop effect fixed points, legal-source/provenance proofs, nested checked `take`, match last-use effects, and consumer adherence—not a new runtime ownership mode.
11. **Message inputs simplify:** Primitive Copy values may be replied, but non-primitive input values remain non-consumable; no extra return-origin ABI facts are required.

## 9. Deliberate limitations and problems Phase 28 does **not** solve

These are not hidden ownership promises. They should be challenged with realistic programs, recorded as limitations, and assigned to the appropriate future phase rather than expanding the Phase 28 ownership contract.

| Limitation | Consequence | Disposition |
| --- | --- | --- |
| **Exception safety / transactional domain updates** | `take(job.state)` may leave `None` across a typed raise before a replacement is installed; ordinary multi-step writes have the same problem | **Phase 21 follow-up**, not a Phase 28 freeze blocker; no automatic rollback or transactional `take` |
| **Moving payloads out of unified enum fields** | `match consume job.state` stays illegal; state transitions that need owned payloads must use an eligible `option[State]` field, a different representation, or an explicitly non-destructive operation | **Accepted LOL expressiveness cost**; do not invent general `replace`/`swap` here |
| **Missing consuming catch-all / nested patterns** | Current `case other:` and `case Some(Loading(buf)):` shapes are unsupported; restoring values after an option take requires exhaustive, possibly nested consuming matches | **Separate pattern-syntax ergonomics**, not a new ownership rule |
| **Conservative owned/borrowed joins** | An owned fresh value cannot merge with an indexed borrowed value at a live use, even if the use only reads | **Intentional Phase 28 rejection**; restructure branches or make explicit copy/removal where legal |
| **Collection-granularity borrow/lock proofs** | Two independent Vector/Map indices can conflict conservatively; merged borrows may lock the union of possible leaves | **Performance/expressiveness tradeoff**; no per-index proof in Phase 28 |
| **Fully custom owning collections** | A user-defined array may wrap Vector/Map/Queue or represent removable slots with `option[T]`, but no generic `remove`/`take` hook allows extracting arbitrary elements from unified user-defined storage. `take` remains a closed set of builtin terminal *field* types. | **Accepted Phase 28 expressiveness limit**; test a custom collection backed by built-ins and one unsupported all-custom representation; revisit only with a concrete SDK need |
| **No stored/escaping references or user-level shared ownership** | Persistent graphs, cycles, and shared references cannot be modeled by stored borrows; domain handles and FileIO remain special resources | **Outside LOL v0.1 scope**; continue to use explicit identifiers, arenas, message routes and owned values |

### State-machine transition example: why rollback is separate

Consider a `Job` whose `state: option[State]` contains an owned buffer:

```moss
old = take(job.state)            # field becomes None immediately
match consume old:
  case Some(state):
    match consume state:         # nested match: no nested pattern syntax
      case Loading(buf):
        validate(buf)            # assumed READ; typed raise can leave job.state == None
        job.state = Some(Ready(buf))
      case Ready(buf):
        job.state = Some(Ready(buf))   # explicit put-back of other variant
  case None:
    job.state = None             # explicit handling; no `case other` catch-all
```

This is **ownership-safe** yet may violate a domain's application invariant on a typed raise. That is the same exception-safety hole as `job.done = job.done + 1; publish(job)` when publish raises: Phase 21 does not promise rollback. **Do not make Phase 28 transactional to mask it.** An implementation can obtain a *strong exception guarantee by hand* where possible: perform every fallible validation and compute needed results while the field is still intact, then execute a non-raising extraction/reconstruction/commit sequence. That is possible only when the transition's required work can be separated in that way.

If a valid state-machine design cannot tolerate a visible temporary `None`, the language currently provides **no automatic invariant-preserving consuming transition**. Record this as a deliberate limitation and investigate rollback/commit-or-undo, failure supervision, or related alternatives in a distinct Phase 21 exception-safety discussion. Do not claim that 2PL alone supplies rollback: it only constrains concurrent observation while a handler is executing, not post-failure state.

**This illustrative code assumes the proposed general `option[T]` type and `take` semantics.** The ordinary enum cases shown are schematic if concrete `Some`/`None` case spellings differ in the parser; unlike a nonexistent catch-all or nested pattern, the *semantic* requirement for explicit exhaustive restoration is intentional.

## 10. Minimal semantic examples for peer review

| Scenario | Expected checker outcome | Reason |
| --- | --- | --- |
| `b = a; echo b; echo a` | Accept if no conflicting overlapping use | `b` is READ borrow |
| `b = a; b.field = 1` | Accept if source writable | `b` is WRITE borrow |
| `b = a; return b` | Accept for owned movable `a`; reject borrowed non-movable place | CONSUME demand at definition |
| `b = state.frontier; archive(b)` | Reject if `archive` consumes | Plain field access cannot implicitly empty field |
| `b = take(state.frontier); archive(b)` | Accept subject to exclusivity | Explicit whole-field move, field becomes empty |
| `b = take(job.pending)` | Accept if field writable | Whole `option[T]` moves, field becomes `None` |
| `take(state.queue)` | Accept if Queue field writable | Moves whole Queue, leaves empty Queue |
| `take(v[i].pending)` | Accept if projection exclusively writable | Takes eligible terminal option field; element `v[i]` stays in place |
| `take(m[k].items)` | Accept if projection exclusively writable | Takes eligible terminal Vector field; entry `m[k]` stays in place |
| `take(local)` | Reject | `take` is for surviving eligible fields; local moves are inferred |
| `take(job.report)` | Reject | Unified field cannot be extracted |
| `take(v[0])` | Reject | Collection element requires `remove`/`pop` |
| `b = v[0]; b.cost = 9` | Accept with WRITE permission | Writes through borrowed element |
| `nxt.push(head[0])` | Reject for non-Copy element | Push needs ownership, indexed access borrows |
| `nxt.push(head.remove(0))` | Accept | Collection API transfers element |
| `b = a; b = new_value` | Accept only after outstanding borrows/use obligations end | Rebinding is not prohibited by itself |
| `c = b; b = make2(); echo c` with owned `b` | Reject | Drop/rebind while `c` borrows old value |
| `frontier = expand(frontier)` in a loop | Accept if effects and exceptional states are sound | Loop-carried ownership handled by fixed point |
| `if c: b = left[0] else: b = Fresh(); echo b` | Reject | Borrowed element cannot promote to owned live join |
| `if c: b = left else: b = Fresh(); echo b` | Accept if `left` movable | Promoted CONSUME from owned local |
| `match job.state` and mutate a payload | Accept if writable | Pattern WRITE borrow |
| `match job.state` then overwrite state after last pattern use in same arm | Accept if no other live borrow | Last-use/NLL semantics, not whole-arm borrow |
| Overwrite matched state while its payload is still borrowed | Reject | Cannot destroy value with live borrower |
| `match job.state` and transfer payload | Reject | Ordinary match cannot consume |
| `match consume queue.pop()` | Accept | Freshly owned enum dismantled explicitly |
| `match consume job.state` | Reject | Unified field cannot be taken implicitly |
| `b = take(state.frontier); may_raise(); use(b)` | Accept ownership-wise, while recording partial-state-on-raise consequences | Explicit destructive operation; no rollback |
| `Map.values()` bound temporarily then source mutated after last use | Accept if borrow ended | Eager borrowed view, not cloned snapshot |
| `for item in items: echo item` for a Vector | Accept with READ | Borrowed element loop, no unnecessary mutable iteration |
| `for item in items: item.bump()` for writable Vector elements | Accept with WRITE | Mutates the original element through the loop borrow; emitter must use checked mutable iteration |
| `for item in items: consume(item)` | Reject if `consume` requires ownership | Vector iteration never extracts an element; use `remove`/`pop` outside conflicting iteration |
| `copy(v[0])` for an eligible value | Accept | Explicit source-level duplication; backend may use a Rust clone to implement it |
| `v[0].clone()` as a source-language duplication alias | Reject | `copy(x)` is the only explicit copy spelling in Phase 28 |
| User-defined wrapper `fn remove_at(a, i): return a.elements.remove(i)` | Accept if backing Vector removal is legal | Custom APIs can delegate ownership transfers to built-in storage |
| `take(my_array.slots[i])` for a unified custom element | Reject | Custom slots are not added to the builtin take/remove capability list |
| `reply n` where `n: Int` is an incoming handler parameter | Accept | Primitive Copy input reply is permitted |
| `n = n + 1` where `n: Int` is an incoming handler parameter | Reject | Incoming bindings are immutable even when primitive |
| `result = n; result = result + 1; reply result` for primitive input `n` | Accept | Rebinding an independent primitive local does not rebind the incoming parameter |
| `reply payload` where `payload: Report` is an incoming handler parameter | Reject | Non-primitive input cannot supply owned reply without explicit copy |

These are **specification expectations**. Every positive case must be demonstrated with actual Moss syntax before the design is marked implemented.

## 11. Implementation outline: reuse the checker, avoid parallel semantic authorities

**Phase A — freeze semantics by examples.** Record the four rules and the **closed `take` target types**, **no `take(local)`**, **primitive input replies permitted**, **last-use match WRITE borrows**, and explicit owned `match consume`. Reconcile the current specifications that permit general `replace`, READ-only/whole-match-lifetime pattern borrows, inferred field moves, and non-primitive input replies.

**Phase B — checker flow facts.** Introduce explicit binding identity, per-assignment/reaching definition identity, liveness, may/must-moved state, possible-source provenance, definite assignment, loop backedges, and precise exceptional successors. Use the retained expression/statement occurrence facts from Phase 27; do not use text fingerprints as semantic identity.

**Phase C — effect fixed point and package interfaces.** Extend existing function/method/specialization inference with local demands; use checked `.mossi` parameter/leaf effects, do not repeatedly rewalk each callee for every alias, and fail closed if effects have not converged. **No primitive return-origin ABI summary is needed** under the adopted primitive-reply rule.

**Phase D — LOL operators.** Implement checked explicit `take` from eligible `option`/Vector/Map/Queue **fields**, including nested terminal fields with exclusive writable projection paths; general `option[T]`, collection removal and borrowed-view APIs; explicit **`copy(x)` only (no source `.clone()` alias)**; last-use READ/WRITE match payload borrows; and owned `match consume` payload moves. Record precise effects and source-leaf provenance for all.

**Phase E — consumers and Rust lowering.** Emitter, Fast Debug and `-O` follow checker-issued facts only. Preserve RHS-before-rebind evaluation, guard/borrow lifetime, proper drop semantics, no hidden nonprimitive `.clone()`, correct domain lock footprints, and sequential evaluation of nested mutable calls. Implement negative tests for missing/contradictory decisions. **Do not count any of the following existing defects as solved merely because ownership rules changed; each requires its named fixture to pass or a checker-supported rejection with an explicit rationale.**

- **E1 — Group A (Rust E0502), field accessor lowering:** re-probe `push_self_field_read/single` and `field_self_assign_struct/single`; validate legal disjoint-field READ/WRITE access through the generated `MossAccess_*` view/projection. Under Phase 28, `nxt.push(head[u])` with a non-Copy element becomes a source-level ownership error; test the *legal* `nxt.push(copy(head[u]))` or removal variant to expose any remaining accessor-alias lowering bug. Distinguish an intentional checker rejection from a Rust E0502 failure.
- **E2 — `swarm_035_nested_mutating_calls.moss` (Rust E0499):** isolate its nested mutable-call evaluation order and overlapping generated borrows. A checker-accepted sequential expression must lower without duplicate mutable Rust borrows; a genuinely conflicting Moss access must be rejected by the checker, not by rustc. Require accepted-to-rustc and Fast Debug/native/`-O` parity.
- **E3 — T6 (Rust E0596), untyped read-only `for` effect inference:** re-run the `repro_untyped_for_write4–7` / `examples/swarm/Surface/Shapes` probes. A READ-only Vector iteration cannot spuriously strengthen an untyped parameter to WRITE or request `&mut` in a read-only specialization. Repair checker inference and verify native lowering uses the READ fact.
- **E4 — `mutate_loop_element` (Rust E0596):** re-run both known variants reached via `take_all`. `for item in items: item.bump()` must infer WRITE when legal and lower to mutable element iteration, never a cloned element or a guessed READ-only `iter()`. If a fixture truly violates exclusivity, the checker must explain it before rustc.

These are separate, named Phase E acceptance gates, not proof that the LOL semantics alone repair historical emitter or inference defects.

**Phase F — migration/release gate.** Update `LANGUAGE_SYNTAX.md`, `MOSS_V0_1_LANGUAGE_DESIGN.md`, `MODULE_ABI.md`, the Gentle Introduction, the Phase 28/S3 historical document, SDK/examples/tests. Explicitly record **exception-safety/rollback as a Phase 21 follow-up**, not a Phase 28 acceptance criterion. Run targeted tests during work; require `make check` and `make check-shadow`, accepted-to-rustc checks and Fast Debug/native/`-O` parity at integration. Separate pre-existing unrelated failing jobs from Phase 28 regressions.

**Existing debt guardrails:** Phase 27's compiler contract records ten frozen ownership/parameter-effect perturb/drop exceptions; the goal is zero. The Phase 27 closeout recorded three failing suite *jobs* in each 165-job mode (including duplicated `swarm_035` E0499 coverage and a Cargo jobserver issue), not three unique ownership defects. Maintain the compiler's other declared debt baselines without enlarging Phase 28 to rewrite checker string parsing or driver tree rebuilding.

## 12. Required adversarial test matrix

At minimum, test the following **per decision class × consumer boundary**, plus positive/negative behavior:

- **Loops:** fresh loop-local vs outer rebind; READ-only frontier expansion vs consuming expansion; nested loops; zero-iteration exits; rebind after a borrow; owned field-backed worklist; backedge effects strengthened only on a later iteration.
- **Joins:** READ/WRITE borrow joins; borrowed/owned join where local promotion is legal; forbidden field/element promotion; branches with no post-join use; paths that return/raise; liveness and maybe-moved cases; joined source-leaf union.
- **`take`:** option `Some` and `None`; empty/nonempty Vector, Map and Queue fields; unified record rejection; entire indexed element rejection versus **legal nested eligible terminal field** (`take(v[i].pending)`, `take(m[k].items)`); `take(local)` rejection; conflicting WRITE borrows; duplicate source aliases; explicit take followed by typed raise and recover; field restoration/invariant limitations; write lock provenance; absent/dropped checker fact corruption.
- **Rebinding:** borrowed rebind releases access, owned rebind drops predecessor, open FileIO rebind rejected without proper close, RHS raise before assignment, RHS consume of previous value, live borrower carried into next iteration.
- **`match`:** READ from owned and borrowed scrutinees; permitted WRITE of payload; **last-use** borrow ends within arm before legal whole-field overwrite; illegal overwrite before a subsequent payload use; forbidden WRITE of immutable payload; forbidden implicit owned payload extraction; `match consume` from owned local, `take` result and `pop`; reject `match consume` on unified field or element; option `Some` owned extraction; exhaustive nested matches and explicit non-selected-variant restoration; no nonexistent `case other` or nested patterns.
- **Exceptions:** exact raised variants entering each `recover`; imported raise summary; earlier consume reaching recovery; catch-all; sibling recover not catching a new raise from another arm; no panic-to-recover edge; lock footprint across recovery.
- **Pipelines:** eager capture end at expression boundary, no capture escape, `-O` fusion respects last use and potential raises, later collection mutation legal only once borrow dead, named helper summaries.
- **Vector iteration and historic defects:** READ-only Vector `for`, legal WRITE element loop, illegal element consume, structural mutation during live loop borrow, `iter()`/`iter_mut()` checker-fact selection; the four independently tracked E1–E4 fixtures (Group A E0502, `swarm_035` E0499, T6 E0596, `mutate_loop_element` E0596).
- **Messages:** handler parameter immutability **including primitive binding rebinding rejection** and synchronous forwarding; independent primitive local rebinding accepted; prohibition on non-primitive consume/store; independently computed replies; explicit eligible `copy(x)`; primitive direct/alias/helper returns **accepted** without return-origin summaries; nonprimitive direct/alias/helper attempts to produce owned replies **rejected** by effect checking.
- **Consumers/interfaces:** drop/perturb every new ownership fact in Fast Debug/native/optimizer and imported interfaces; no fallback inference; generated clone audit; identical behavior across all three execution modes.

## 13. Questions for the peer reviewers (and residual proof obligations)

**Challenge the design, in this order:**

1. **Core counterexample:** Is there any Moss operation that LOL's unified / 0..1 / 0..n classes cannot express without a general `replace`? Pay special attention to transitions that need an owned enum payload, exhaustive put-back arms, and iterators. Distinguish an expressiveness limitation from an exception-safety/rollback requirement.
2. **Ownership soundness:** Can per-definition effect inference plus live-only widening incorrectly approve an alias that outlives its source, an owned-vs-borrowed merge, or a loop-backedge use-after-move? Construct the smallest trace.
3. **Explicit move correctness:** Does `take` of exactly `option`, Vector, Map or Queue **fields** always have a unique, well-typed empty replacement? Verify nested eligible terminal targets, collection WRITE conflicts, unified fields, and rejection of `take(local)`.
4. **Control-flow precision:** Are Phase 21's concrete typed raise sets sufficient to make correct exceptional edges, including effects that execute before a raised exit? Does a fallible RHS rebind preserve the prior local on all paths where it should?
5. **Pipeline and pattern semantics:** Does inferred WRITE on borrowed match payloads, with borrows ending at **last use inside an arm**, interact safely with alias exclusion and matched-field replacement? Is `match consume` the only necessary explicit enum extraction mechanism? Can a pipeline optimizer violate last-use capture boundaries?
6. **Locking:** Does conservative possible-origin union preserve existing ranked 2PL with domain routes, synchronized helpers, and imported leaf summaries without unsound under-coverage?
7. **Usability/performance:** Which real SDK workloads are impossible or substantially worse under explicit `take`, whole-collection conflicts, no hidden clones, or no general `replace`? Measure an actual BFS/worklist and state-machine command loop, not only synthetic fixtures.

**Decisions adopted for this review draft (not implementation claims):** incoming handler parameters are immutable and non-rebindable for all types, including primitives; primitive incoming-handler Copy replies are legal; explicit copying is spelled `copy(x)` with **no** source `.clone()` alias; Vector `for` elements are inferred READ/WRITE borrows, never CONSUME; no `take(local)`; `take` has exactly four eligible terminal field type families (`option`, Vector, Map, Queue), including nested fields; normal pattern borrows end at last use, even inside an arm; consuming matches require owned scrutinees; exception safety remains a Phase 21 issue.

**Residual questions for review, not reasons to silently expand the ownership model:**

- **Concrete source syntax and ergonomics:** Verify branch-joined local, collection count, `Some`/`None`, and built-in spellings against the parser. Demonstrate a complete enum state transition without pretending `case other` or nested patterns exist.
- **Optional diagnostics:** Should a checker warn when an *explicit* take is followed by potentially raising work before re-establishing a state-machine invariant? It must **not** reject the program under Phase 28 solely for absence of rollback.
- **Practical expressiveness:** Measure how often needing an `option[State]` and exhaustive put-back arms makes real SDK state machines awkward. Test user-defined array removal implemented through a built-in Vector or option slots, and demonstrate the limitation for fully custom non-optional slot storage. These are accepted limitations until a separate proposal changes them.

**Not re-opened without a concrete counterexample:** implicit nonprimitive cloning, stored references or shared ownership, general `replace`/`swap`, runtime mixed-owned/borrowed join representations, and per-index disjointness proof. **Do not add rollback or a transactional `take` to Phase 28.** The Phase 28 checker remains the single semantic authority.

---

## Source documents consulted / precedence

This is a **new design proposal**, not a statement that all rules already compile. It builds on the following existing Moss specifications and previously documented defects:

- [MOSS_OWNERSHIP_S3.md — earlier Phase 28 ownership decisions and probes](https://github.com/kuttyb/moss/blob/phase28-work/docs/MOSS_OWNERSHIP_S3.md). Its rev-10 mixed-join rejection, implicit option/collection field consume, READ-only match assumptions, primitive-reply prohibition and provisional `.clone()` spelling are **superseded** by this consolidated proposal. Its per-definition rebinding insight is retained. The S3 document is preserved as a historical record, not the current Phase 28 contract.
- [MOSS_PHASE_28_OWNERSHIP_PROBLEMS_AND_PROPOSAL.md — earlier draft on `phase28-work`](https://github.com/kuttyb/moss/blob/phase28-work/docs/MOSS_PHASE_28_OWNERSHIP_PROBLEMS_AND_PROPOSAL.md). Its non-rebinding rule is **not retained here**; this earlier draft is superseded for peer review.
- [LANGUAGE_SYNTAX.md on `main`](https://github.com/kuttyb/moss/blob/main/docs/LANGUAGE_SYNTAX.md): loop/branch scoping, eager pipelines, `match` and `match consume`, currently READ-only payload wording, existing general `replace` and collection APIs, and current incoming-payload reply semantics. Where different, this document explicitly proposes a change.
- [MOSS_PHASE_21_ERROR_HANDLING.md on `main`](https://github.com/kuttyb/moss/blob/main/docs/MOSS_PHASE_21_ERROR_HANDLING.md): variant-precise raise effects, `try`/`recover` control flow, partial mutations on typed raises, and no transactional rollback.
- [MOSS_COMPILER_CONTRACT.md on `main`](https://github.com/kuttyb/moss/blob/main/docs/MOSS_COMPILER_CONTRACT.md): checker-owned decision authority, perturb/drop tests, ownership debt, and accepted-to-rustc constraints.

**Review instruction:** Please mark each objection as (a) a language counterexample or accepted expressiveness limit, (b) an implementation/proof gap, (c) a documentation/API migration issue, or (d) a Phase 21 exception-safety concern. Prefer small executable Moss examples over speculative Rust lowering arguments. Do **not** implement or merge this proposal before the ownership-language decisions are reviewed.