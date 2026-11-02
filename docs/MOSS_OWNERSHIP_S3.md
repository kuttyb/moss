# Moss ownership (S3): the problems, simply

Status: Phase 28 ownership design contract, Nov 1 2026. **Rev 9: all twelve issues closed; scoped local borrow inference and message reply restrictions frozen.** Implementation and migration remain pending. Rev 8 closed seven Map decisions. Historical probes below describe old behavior and are not the adopted rules. This supersedes the older unconditional-local-move decision S2 and withdrawn message-consume proposal M1 in `MOSS_PHASE_26_3_OPEN_ISSUES.md`.

## 0. Agreed principles

1. **The checker decides** READ, WRITE or CONSUME for every access, including locally inferred assignments and effects propagated through reachable calls.
2. **The emitter and backend decide how** to implement it: borrow, move, lock, `Arc`, copy elision, fusion. Lowering and optimization live there, not in the checker.
3. **No implicit clones of non-primitive values.** Independently owned duplicates require explicit eligible copying or fresh construction; emitters never invent a clone.
4. **Fast Debug executes the checker's decisions naively.** Single-threaded, no locks, no emitter or pipeline optimizations, eager evaluation. It reads the checker's decisions; it does not re-derive them.
5. **Known bugs** get one more review before S3 starts.

## 0b. Ownership taxonomy (agreed Nov 1 2026)

**LOL: Lifetime, Ownership, Layout.** Layout belongs to the backend. Ownership is not a choice: always a single owner. So the only language question is lifetime, with exactly three classes: unified, disjoint 0..1, disjoint 0..n.

Classified from the owner's point of view: does a contained value live exactly as long as its owner (unified), or can it come and go (disjoint)?

- **Unified lifetime:** plain fields. A field exists from the owner's construction to its destruction and is never absent. It can be read, mutated in place (through its own methods) or overwritten, but never moved out.
- **Disjoint lifetime, 0..1:** `option[T]`. The value may be absent or present at any point; moving it in and out goes through the option's own methods.
- **Disjoint lifetime, 0..n:** `Vector` and `Map`. Elements come and go only through the collection's own methods (`push`, `remove`, `pop`, `swap_remove`; see P3). The collection itself always exists (it is never absent; only its element count is 0..n); its elements are disjoint.
- **Layout is the backend's choice.** Inline or heap, the language does not say (Moss has no recursive types, so inline is always possible).
- **Sharing is excluded.** Every value has exactly one owner. No reference-counted or shared ownership at the language level (the backend may use `Arc` internally, invisibly).
- **Borrowing stays temporary** (views, message payloads); never stored in a type.
- **No `replace` built-in for now.** Known cost: in-place enum transitions that move an owned payload (`Loading(buffer)` → `Ready(buffer)`) need the field to be an `option` and a take-then-put, or a later narrow built-in.

**Gap today:** `option[T]` exists only for the iterator protocol (`next() -> option[T]`). The Gentle Introduction says `Map.get` "does not introduce a new `Option` type." Making `option[T]` a general type usable as a field, with take/put-style methods, is new language work.

## 0c. Copy, borrow, move and explicit copy (revised Nov 1 2026)

- Only designated primitives are implicitly Copy. Records, Strings, collections and `option[T]` are nonprimitive even when their fields are primitive.
- Nonprimitive local `b = a` now infers READ borrow, WRITE borrow or CONSUME from all uses of `b` within its lexical local scope and reachable callees (§0e). It is **not** an unconditional move.
- READ/WRITE means `a` remains owned, subject to borrow exclusivity. CONSUME transfers ownership **at `b = a`**, making `a` unusable. An ownership sink such as `return b`, storing `b` as an owned value or passing `b` to a consuming parameter demands this CONSUME effect.
- Add an explicit `copy(x)` primitive (final name TBD) to deliberately create an independently owned value. Backend layout/deep vs copy-on-write implementation is invisible; independent mutation is guaranteed. No user-defined copies. Reject nonduplicable types such as domain handles, FileIO and scoped resources; Fast Debug can deep-copy eligible values.
- Historical behavior: unconditional nonprimitive local moves, and current rejection of Vector `.clone()`, do not establish Phase 28 semantics.

## 0d. `option[T]` (revised Nov 1 2026)

```text
type Job:
  result: option[Report]

x = job.result       # READ/WRITE if x only borrows
return x             # forces CONSUME at assignment; field becomes None
```

- General-purpose `option[T]` is disjoint lifetime 0..1, usable as a field; this extends today's limited iterator-protocol support.
- `x = opt` (owned local) and `x = job.result` (field) follow the same inferred READ/WRITE/CONSUME rule. Only CONSUME takes the option. Consuming an owned local invalidates it; consuming an option **field** resets that field to `None`. A consuming function parameter behaves the same.
- Inspection and ordinary match borrow; `match consume` consumes when legal. Taking an option field counts as WRITE of its owner and is illegal for incoming READ-only payloads. `Some(r)` or `None` assignment overwrites the old value and drops it.
- An option is never implicitly Copy even if T is primitive; fix the old Option[Copy] exemption.


## 0e. Scoped local borrow inference (closed Nov 1 2026)

```text
b = a
echo b                  # READ borrow; a remains owned
```
```text
b = a
b.cost = 9              # WRITE borrow; changes a.cost
```
```text
fn take(a):
    b = a               # CONSUME happens at this assignment
    return b            # returns owned b, not a borrow
```

**One inferred effect at the assignment**, not a runtime choice: READ for inspection, WRITE for mutation without an ownership sink, CONSUME if any downstream path requires ownership (return, owned storage or consuming call). Statically join the effects across branches. Ordinary flow-sensitive borrow and post-move validity checks remain in force.

**Borrow inference is local only.** A temporary local alias cannot escape its lexical scope, outlive its source, be returned as a borrowed alias, be stored as an owned value or be retained after a synchronous helper returns. The alias borrow may end at last use. If the programmer returns the local, CONSUME is inferred back at the **original assignment** if the source may legally be consumed; otherwise reject. A moved value is never restored at scope exit. No source-level lifetime annotations or globally escaping borrow types.

**Transitive call closure:** while checking that local scope, follow all reachable synchronous calls/specializations that pass the borrowed value, including existing imported `.mossi` formal READ/WRITE/CONSUME effects. Downstream WRITE or CONSUME propagates to the initiating assignment; callees may not hide retention or alias escape.

**Lock correctness:** borrowed local aliases of domain state retain their source field/leaf provenance through calls. Required WRITEs must reach the checker's synchronization plan; existing static 2PL, global lock order, per-record-leaf partitioning and whole-Vector/Map leaves do not need redesign.

## 1. Messages: immutable by-value snapshots; no input reply (revised Nov 1 2026)

```text
e = Entry(text = "boot")
message log.Append(e)
echo e.text               # legal: sender retains e
```

- Message parameters are semantic **pass-by-value** and immutable, irrespective of their primitive/nonprimitive type. The handler may **READ** or **FORWARD** an incoming parameter through a synchronous message, but may not WRITE, CONSUME, rebind, or move it into owned state. Local aliases and helpers cannot bypass the restriction.
- **New Phase 28 rule: `reply payload` is forbidden, even indirectly through an alias, and even for primitives.** Existing `LANGUAGE_SYNTAX.md` and `MODULE_ABI.md` allow input reply and must be reconciled when implementing Phase 28. Replying with an independently computed value (`reply payload.size()`) or explicitly copied eligible value (`reply copy(payload)`) is allowed.
- For internal synchronous messages the emitter may use `&T` / typed immutable views throughout forwarding; the source and any supporting domain guard remain live until the nested call completes. A physical boundary cannot justify a hidden nonprimitive clone.
- The old M1 proposal (message CONSUME) is withdrawn. Message graph acyclicity and no reentry into the sender remain required for lock safety.

## 2. The problems

### P1. Old eager local transfers — **superseded by inferred effects**

```text
a = [1, 2, 3]
b = a
echo a               # Phase 28: allowed when b is READ-only
```

**Historical probe:** today's checker rejects this, reporting transfer from `a` to `b`. Existing use-after-transfer negatives include `examples/use_after_transfer.moss`, `tests/negative/nontrivial_field_move.moss`, `tests/negative/branch_join_consume.moss` and `tests/negative/functional_reduce_initial_reuse.moss`. Phase 28 distinguishes legal local borrowing from actual CONSUME. Keep genuinely consuming negative tests and migrate assumptions of automatic local moves (§0e).

### P2. Primitives-only Copy — **records can borrow but never implicitly copy**

```text
type Point:
  x: Int
  y: Int

p = Point(x: 1, y: 2)
q = p
echo p.x             # Phase 28: allowed if q READ-borrows p
```

**Historical probe:** today's checker rejects later use of `p` because it always moves into `q`. A record containing only Int fields is still non-Copy; this remains true in Phase 28. The example becomes a borrow, **not** a duplicate. A separately owned `q` needs legal CONSUME or explicit `copy(p)`. `option[T]` is non-Copy regardless of T.

### P3. Moving an aggregate out of a place — **DECIDED: borrow on `=`, error on consume (revises option 3)**

```text
names = ["ada", "alan"]
first = names[0]
echo names[0]        # accepted; prints "ada" twice
```

```text
nxt.push(head[0])    # accepted; head is unchanged
```

**Today (probes):** all accepted. The emitter clones the element in every case:
- `first = names[0]` → `names[0].clone()`
- `nxt.push(head[0])` → `head[0].clone()`
- even read-only uses clone: `echo names[0]` → `names[0].clone()`; `echo edges[0].cost` clones the whole record to read one field.

The spec does not state an ownership rule for indexing. Existing tests rely on the copy (`tests/swarm_021_map_value_semantics.moss`: `current = snapshot[0]` then `current.push(3)`).

**How other languages do it:**
- **C++** `std::vector`: `auto x = v[0]` copies; `auto& x = v[0]` borrows; `std::move(v[0])` moves out and leaves a valid-but-unspecified element. No checking of use-after-move.
- **Rust** `Vec<T>`: `let x = v[0]` is a compile error for non-Copy `T` (E0507). The programmer must choose: borrow `&v[0]`, explicit `v[0].clone()`, move-and-shrink `v.remove(i)` / `v.swap_remove(i)`, move-and-replace `mem::replace(&mut v[i], new)` / `mem::take`, or `v.swap(i, j)`. Reading through a place (`v[0].field`) is always fine.

**The design question.** A place is a field (`p.payload`) or an element (`v[i]`) holding an aggregate (non-primitive) value. Reading through a place is easy (`p.payload.value`, `v[i].cost`). The question is whether `=` may move the aggregate out:

```text
type Packet:
  payload: Payload
  code: Int

p = Packet(payload: Payload(value: 7), code: 42)
x = p.payload        # ?
y = v[0]             # ?
```

Today the two places disagree: a field moves and consumes the whole owner (`LANGUAGE_SYNTAX.md`: "moving a non-Copy field out of a value consumes the whole binding"; `tests/negative/nontrivial_field_move.moss`), while an element is silently copied.

| Option | `x = p.payload` | `y = v[i]` |
|---|---|---|
| 1. Move, consume the owner (today's field rule) | `p` is gone | `v` is gone |
| 2. Partial move (Rust's field rule) | only `p.payload` is gone | rejected (can't track per index) |
| **3. No assignment out of a place** | rejected | rejected |
| 4. Copy | — | violates principle 3 |

**Decision history (Nov 1 2026).** Option 3 was chosen first, then revised the same day: the assignment is not the problem, the intent to consume is. Option 3's `replace` and `swap` were dropped (P7).

**Decided rules:**
- **`=` from a place borrows.** `x = v[i]` or `r = job.report` with a local on the left makes it a borrow, exactly like passing the place to a function. Read or write borrow follows from how it is used (inferred effects, as for parameters). The borrow must end before the container or owner is changed or consumed.
- **A write through the borrow changes the place.** `ed = edges[0]; ed.cost = 9` leaves `edges[0].cost == 9`; `r = job.report; r.score = 9` leaves `job.report.score == 9`.
- **Consuming a unified field or an individual element is a compile error** (storing the borrow in a field, sending it, passing it to a consuming parameter). Today's "moving a non-Copy field consumes the whole binding" rule goes away (it had no practical meaning); `tests/negative/nontrivial_field_move.moss` stays negative with a new diagnostic.
- **`nxt.push(head[u])` is a compile error** (push consumes its argument). Write `nxt.push(copy(head[u]))` or `nxt.push(head.remove(u))`. Today it compiles with a silent clone.
- **Two ways to get an element out:** `copy(v[i])` (explicit intent; the backend chooses how) or removal.
- **Removal is the only way an element leaves a container**, for vectors and maps. `remove` is not just a delete: it returns the removed element, moved out to the caller.
  - `Vector.pop()` and `Queue.pop()` are `remove` at an end: move out and shrink.
  - `Vector.swap_remove(i)`: moves element `i` out, moves the last element into its slot, shrinks. O(1), changes order. The programmer chooses it because order is observable; the backend can't substitute it.
  - Popping an empty container is a run-time error handled by the normal error mechanism (Phase 21: the caller catches it). No option-returning variant.
  - No `v.swap(i, j)` and no general `swap` for now: a general swap between any two places is `replace` under another name (`swap(job.report, tmp)` moves a value out of a unified field).
- **The whole container may be consumed** (lifetime class 0..n); every element goes with it. A consumed local is gone (later use is a compile error). A consumed container *field* leaves a new, empty container behind, never `None`: only an option can be absent under LOL, so no program ever null-checks a container. (Rust backend equivalent: `mem::take`.)

Consequences:
- The emitter's indexed-read clones (`echo names[0]`, `edges[0].cost`) become borrows.
- The Fast Debug/native bug in section 3 is decided in Fast Debug's favour (expected output 9, 9).
- The checker must track scoped local borrow bindings and transitive callee effects (§0e; the largest implementation item).

**One inference rule across lifetime classes:** `=` from a place READ/WRITE borrows unless downstream ownership requires CONSUME; source legality then governs:
- unified field: CONSUME is forbidden, even if its owner is about to die;
- option field (0..1): legal CONSUME takes it, leaving `None`;
- vector/map element (0..n): CONSUME forbidden; use `remove`/`pop`/`swap_remove`;
- whole vector/map field: legal CONSUME takes it, leaving an empty container;
- owned local: CONSUME invalidates the source permanently.

### P4. `keys()` and `values()` copy everything — **decided: borrowed views**

```text
vs = data.values()             # borrowed view; no cloned values
```

**Today:** the spec says `keys()` and `values()` return "eager owned `Vector` snapshots" (`LANGUAGE_SYNTAX.md`, Built-in collections). The probe emits `data.values().cloned().collect()`: every value is copied. `tests/swarm_021_map_value_semantics.moss` relies on mutating such a snapshot without affecting the map; that program must be rejected or rewritten.

**Phase 28:** views are temporary borrows of the Map, not owned snapshots. They cannot be stored or returned; their borrow must end before structurally modifying or consuming the Map. Map keys are read-only. Values may be read or written through a permitted WRITE borrow, but cannot be consumed individually.

### P5. Map API and ownership — **seven decisions closed (Nov 1 2026)**

**Today (legacy probe):** `get(key, fallback)` returns an owned copy of the stored value or fallback, emitting `data.get(k).cloned().unwrap_or(fb.clone())`. The fallback binding remains usable. `tests/swarm_021_map_value_semantics.moss` relies on returned copies being independent. The old `delete(key, fallback, found)` also has fallback-consuming behavior. These describe current implementation, **not** Phase 28 semantics.

**Phase 28 decisions:**

1. **`m.remove(key)`:** removes the entry and transfers ownership of its value to the caller, without copying. Missing key raises an ordinary catchable error. This replaces the legacy fallback/`found` `delete` API.
2. **`m[key]`:** borrows the stored value with inferred READ or WRITE effect. WRITE modifies the value in place; trying to consume an individual Map element is rejected. Missing key raises an ordinary catchable error.
3. **`m[key] = value`:** inserts or overwrites, consuming the incoming non-primitive value into the Map. If there was a previous value, it is dropped, not returned. Use `remove` first when the old value must be retained. Normal borrow exclusivity applies.
4. **`m.get(key)`:** exactly **one argument**, no fallback. Returns a READ/WRITE borrow of the entry, just like `m[key]`; missing key raises an ordinary catchable error. `get(key, fallback)` is removed. `get(key)` and indexing are intentionally equivalent in Phase 28; this is no longer an ownership-design question.
5. **`keys()` / `values()`:** borrowed views, not owned copies. They cannot be stored or returned and cannot remain live across structural Map mutation or consumption (P4).
6. **Map keys:** read-borrowed only while stored in the Map. No in-place key mutation, which would invalidate lookup.
7. **`for (k, v) in m`:** borrowing iteration only; `k` is READ and `v` is READ/WRITE as inferred from the body. Neither can be consumed from the Map. Iteration does **not** implicitly consume the entire Map to transfer entries. To transfer an entry, explicitly call `remove(key)` and then pass the owned result to a consuming operation, outside any live conflicting iteration borrow. Structural mutation of the iterated Map while its iterator is live is rejected.

```text
m = {"a": [1, 2]}
x = m.get("a")                # borrow; missing key raises
y = m.remove("a")             # owned value; entry removed
archive.put("a", y)           # consumes y

m["b"] = [3, 4]               # Map owns new value
```

**No fallback-lifetime problem remains:** the two-argument `get` API is removed. No Map read, view, or iteration introduces an implicit non-primitive copy. Map elements remain one synchronization leaf for locking; Phase 28 does not introduce per-key locks.

### P6. Hidden clones in the emitter — **agreed: remove**

The emitter decides on its own, in about 24 places, to insert `.clone()`: sort keys, constructor arguments, `Some(x)`, return values, assignment right-hand sides, cross-package calls, map fallbacks, and (per the P3 probes) read-only indexed reads such as `echo names[0]`.

```text
people |> sort_by(p -> p.name)   # today each element is cloned to compute its key
```

Under S3 each of these either borrows, moves, or is rejected. None copies.

### P7. `swap` and `replace` — **both rejected for now (Nov 1); only `Vector.swap_remove` is provided**

```text
swap(names, 0, 1)                  # one write on names
old = replace(names[0], "grace")   # write the slot, consume "grace", return the old value
```

**Decided (Nov 1 2026):** neither is provided for now. `replace` would extend a value's lifetime past its unified place; a general `swap` between any two places is `replace` under another name. Vectors get `swap_remove(i)` as a container method instead (P3). Known cost: in-place enum transitions that move an owned payload need an `option` field (section 0b).

### P8. Cross-package inferred effects — **already in .mossi (#11)**

```text
# same library v1
fn total(xs):
  xs |> sum           # READ
# same library v2
fn total(xs):
  keep(xs)            # CONSUME when keep consumes
```

A source signature may remain unchanged while inferred effects strengthen. **Existing implementation:** `MODULE_ABI.md` specifies the concrete parameter READ/WRITE/CONSUME modes, `parameter_leaf_effects`, semantic interface hashing and fail-closed missing-effect validation in `.mossi`. Importing callers must respect those recorded facts, including through local alias inference.

A friendly cross-version READ→CONSUME breaking-change diagnostic is optional tooling, **not** a missing Phase 28 language or serialization feature. Callers must still be checked against changed contracts.

## 3. Known bugs

### Programs the checker accepts but Rust rejects

| Bug | Smallest shape | What goes wrong |
|---|---|---|
| Group A (E0502) | `nxt.push(head[u])` inside a method using two fields | Field accessors borrow all of `self`; candidate fix is a per-type projection view. Under P3 the shape itself becomes a compile error; the underlying E0502 can still occur with `copy(head[u])` or `head.remove(u)` and must be re-probed |
| T6 (E0596) | `fn f(xs): for x in xs: echo x` with untyped `xs` | A read-only loop parameter is inferred as WRITE |
| `mutate_loop_element` (E0596) | `for item in items: item.bump()` | Emitted with read-only iteration where mutation is needed |

### New: Fast Debug and native disagree on an indexed element (found Nov 1)

```text
type Edge:
  cost: Int

fn main():
  edges = [Edge(cost: 7)]
  ed = edges[0]
  ed.cost = 9
  echo ed.cost
  echo edges[0].cost
```

- **Native:** prints `9`, then `7` (the emitter cloned the element; the vector is unchanged).
- **Fast Debug:** prints `9`, then `9` (it aliased the element; the write changed the vector).

The debugger disagrees with the binary. **Decided (Nov 1 2026):** `ed` is a write borrow of `edges[0]` (its last use is the write), so the correct output is `9`, then `9`. Fast Debug is right; native is the bug (the emitter's silent `.clone()` on an indexed read). This becomes a regression fixture with expected output 9, 9.

All known bugs to be reviewed once more before S3 starts.

## 4. How we'll know it's fixed

- **Perturbation and drop tests** for the ownership decisions (read, write or consume per access; clone, move or borrow), as for the other decision classes in `docs/MOSS_COMPILER_CONTRACT.md`.
- **Checker accepts → Rust compiles**, with a ratcheting allowlist.
- **A generated-Rust audit:** every `.clone()` of a non-primitive value must trace to an explicit source operation.
- **Engines agree:** Fast Debug, native and `-O` produce the same behavior (the indexed-element probe above is the first fixture).

## 5. Questions #8–#12: all closed (Nov 1 2026)

Map questions 1–7 are recorded in P4–P5. The remaining five choices:

**8 — Conservative collection borrowing.** Overlapping READ+READ borrows are legal. Overlapping READ+WRITE or WRITE+WRITE into the **same collection** are rejected even for different statically evident indices/keys. No index-disjointness proof in Phase 28. Sequential borrowing after the earlier borrow ends remains legal; this is not a change to independent ordinary record-field leaves.

```text
x = v[0]             # WRITE downstream
y = v[1]             # READ downstream
x.cost = y.cost      # ERROR while those borrows overlap
```

**9 — Unified fields never partially move.** Even at an owner's last use, `return p.payload` cannot consume a normal unified field; no whole-record-destructuring escape hatch. Consuming the **whole** owner is allowed. Existing separately specified `match consume` semantics for enum variants remain unchanged.

**10 — No reply of input payload.** READ and synchronous forwarding only; `reply payload` (including aliased or primitive payload) is forbidden. Explicit `copy(payload)` when allowed or an independently computed owned reply remains valid. This is a language change from the current docs (§1).

**11 — .mossi parameter effects already exist.** Retain and enforce existing READ/WRITE/CONSUME / leaf records; do not build redundant serialization. Enhanced cross-version diagnostic is optional tooling (P8).

**12 — Rewrite, no backward-compatibility period.** Rewrite SDK code, examples and tests that relied on silent clones or old Map snapshots/APIs. Retain genuine negative consume tests. After Graph DSL lands, run another **SDK swarm** with realistic graph/collection workloads to expose ergonomic gaps and implementation defects.

### Execution requirements (not unresolved design)

- New language work: scoped local inference, explicit `copy`, general `option[T]`, collection and Map ownership APIs, and input-reply prohibition.
- Reused infrastructure: checker effect authority, existing `.mossi` effect records, immutable synchronous messages, domain leaf provenance, and lock plans; no emitter locking redesign.
- Checker: join effects across branches and reachable calls, ensure alias provenance and borrow exclusivity, reject illegal escape, retained-element/unified-field transfer and input reply.
- Emitter/Fast Debug: follow checked decisions without hidden nonprimitive clones; preserve borrowed guard lifetimes and synchronize valid writes.
- Migration: reconcile `LANGUAGE_SYNTAX.md`, `MODULE_ABI.md`, Gentle Introduction, SDK/examples/fixtures; no compat shim.
- Tests: acceptance→rustc, Fast Debug/native/`-O` parity, checker decision perturbation/drop, generated clone provenance, known bugs in §3.
- Post-Graph DSL: separate SDK swarm for realism; does not defer Phase 28 correctness.
