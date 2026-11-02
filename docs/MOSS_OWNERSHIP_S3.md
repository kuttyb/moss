# Moss ownership (S3): the problems, simply

Status: design in progress, Nov 1 2026. **Implemented in Phase 28** (Phase 27 closes when 27.5.4 C2 completes). (rev 7: collections and plain fields decided — `=` borrows, consume is the error; `remove`/`pop`/`swap_remove`; rev 6: option[T] closed; rev 5: copy primitive agreed; rev 4: ownership taxonomy agreed; rev 3: P3 decided — option 3, explicit extraction; rev 2: current behavior probed on main `d8f64910`). Replaces the problem framing in `MOSS_PHASE_26_3_OPEN_ISSUES.md`; that doc's frozen decisions S1, S2, D1, D2 stand, M1 is withdrawn (see section 1). Examples are pseudo-Moss unless marked as a probe; probes are real programs checked with `moss check`, run in Fast Debug (`moss run --interp`) and compiled natively (`moss -o` + `rustc -O`).

## 0. Agreed principles

1. **The checker decides** read, write or consume for every access to a value. That is language semantics.
2. **The emitter and backend decide how** to implement it: borrow, move, lock, `Arc`, copy elision, fusion. Lowering and optimization live there, not in the checker.
3. **No implicit clones of non-primitive values.** If a program would need a hidden copy, it is rejected with a clear error. It is never quietly copied.
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

## 0c. Copy vs move, and the copy primitive (agreed Nov 1 2026)

- With single ownership and backend-owned layout, the only observable question after `b = a` is whether `a` is still usable. Primitives: yes. Everything else: no. Bytes copied or pointer moved is the backend's business.
- `option[T]` is not a primitive, so assigning an option always moves, whatever `T` is.
- **Assignment never copies a non-primitive value.** The one place a non-primitive source stays usable is a message send, by definition (pass-by-value), not by assignment.
- **A copy primitive will be added to the language** (name to be decided). It expresses intent only: "give me an independent value with the same contents." Deep, shallow, lazy, copy-on-write or anything else is the backend's choice, invisible to the program. The only guarantee: changes to either value are never visible through the other.
- **No user-defined copy per type.** Programmers don't implement their own copies; that would bypass the backend's control of layout.
- The checker rejects the primitive on non-duplicable values (domain handles, FileIO, scoped values). Fast Debug implements it as a plain deep copy.
- Today: there is no explicit copy at all (`a.clone()` on a vector is rejected with `invalid builtin operation 'vector[int].clone'`). New language work.

## 0d. `option[T]` (closed Nov 1 2026)

```text
type Job:
  result: option[Report]

x = job.result        # x: option[Report]; job.result is now None
```

- `option[T]` becomes a general type, usable as a field (disjoint lifetime, 0..1).
- **Two things take an option field:** the assignment operator and a consuming function argument. `x = job.result` moves the whole option into `x` (type `option[T]`, possibly `None`) and leaves `None` behind; `f(job.result)`, where `f` is inferred to consume its parameter, does the same.
- **Parameters follow normal inferred effects.** READ borrows, WRITE mutates the option in place, CONSUME takes (as above). Nothing else is special about options.
- **Looking doesn't take.** A plain `match job.result:` (or an `if` testing for `Some`) borrows and leaves the field intact. `match consume` is the moving form.
- **Locals stay ordinary moves.** `x = opt` on a local consumes `opt`; later use is a compile error. Only fields leave `None` behind.
- **Taking is a write.** `x = job.result` (or a consuming call) modifies `job`, so it is rejected wherever `job` is read-only (an incoming message payload, a parameter inferred read-only).
- **Writing in needs no new rule.** `job.result = Some(r)` or `job.result = None` overwrites; the old value is dropped.
- Options always move, whatever `T` is (an option is not a primitive).

## 1. Settled: messages are pass-by-value (not a problem)

```text
e = Entry(text = "boot")
message log.Append(e)
echo e.text          # legal: the sender keeps its value
```

- Messages carry values. Incoming payloads are immutable: the handler may read, forward or reply with them, but not write, consume or store them. This is already the spec (`LANGUAGE_SYNTAX.md`, Domain communication).
- Because the sender is blocked and the receiver can't mutate, a copy and a borrow are indistinguishable. The backend borrows or moves when nobody can tell, and copies only when the language requires it (for example, a reply carrying the payload while the sender still uses it).
- **M1 (consume at message boundaries) is withdrawn.** It was proposed, never implemented.
- Kept as a checker rule: synchronous message graphs have no cycles and no re-entry into the sender (a deadlock rule for any implementation).

Everything below is about values **inside** a domain or function.

## 2. The problems

### P1. Assignment must move, not copy — **already enforced by the checker**

```text
a = [1, 2, 3]
b = a
echo a              # OWNERSHIP_USE_AFTER_CONSUME
```

**Today:** the checker rejects it (probe, for both a vector and a String): `value 'a' was transferred to 'b' at line 3`. The error comes from `moss check`, so it is the checker, not the emitter. Existing negative tests include `examples/use_after_transfer.moss`, `tests/negative/nontrivial_field_move.moss`, `tests/negative/branch_join_consume.moss`, `tests/negative/functional_reduce_initial_reuse.moss`; 33 corpus sources are rejected with this diagnostic. No action needed beyond keeping these tests.

### P2. What counts as Copy — **records already move**

```text
type Point:
  x: Int
  y: Int

p = Point(x: 1, y: 2)
q = p
echo p.x            # OWNERSHIP_USE_AFTER_CONSUME
```

**Today:** the checker rejects it (probe): an all-Int record is moved, not copied. Also covered by `tests/tooling/fixtures/builtin_generic_typing/vector_literal_reuse.moss` (`Payload(value: Int)` moved into a vector literal). Resolved: options always move, whatever `T` is (section 0d); `copy_type_name` treating `Option[Copy]` as Copy is a bug to fix.

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
- The checker must track local borrow bindings (the largest implementation item).

**One rule across the three lifetime classes:** `=` from a place borrows. What differs is consumption:
- unified field: consume is a compile error.
- option field (0..1): consume takes it and leaves `None`.
- vector/map element (0..n): consume is a compile error; `remove`/`pop` move it out.
- whole vector/map field: consume takes it and leaves an empty container.

### P4. `keys()` and `values()` copy everything — **agreed: become borrowed views**

```text
vs = data.values()
```

**Today:** the spec says `keys()` and `values()` return "eager owned `Vector` snapshots" (`LANGUAGE_SYNTAX.md`, Built-in collections). The probe emits `data.values().cloned().collect()`: every value is copied. `tests/swarm_021_map_value_semantics.moss` relies on mutating such a snapshot without affecting the map; under borrowed views that program must be rejected or rewritten.

### P5. Map fallbacks — **agreed: `get` borrows**

```text
v = data.get("a", Vector[Int]())
v.push(3)                         # does not change the map
w = data.get("x", fb)
echo fb |> count                  # fb still usable
```

**Today (probe):** `get` returns an owned copy of the stored value and clones the fallback: `data.get(k).cloned().unwrap_or(fb.clone())`. The fallback binding is not consumed. `tests/swarm_021_map_value_semantics.moss` relies on the returned copy being independent. `delete` consumes its fallback on both paths (already the spec). An inline fallback must outlive a borrowed result: that needs a language rule, not an emitter convention.

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

### P8. Inferred effects cross package boundaries silently

```text
# library v1
fn total(xs):
  xs |> sum            # xs inferred READ

# library v2
fn total(xs):
  keep(xs)             # xs now inferred CONSUME
```

Every caller in another package breaks. Inferred effects must be recorded in `.mossi`, and READ to CONSUME must be flagged as a breaking change.

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

## 5. Open questions (updated Nov 1 2026)

Resolved and removed from this list: `Option[Int]` Copy (options always move), taking an element out (`remove`/`pop`/`swap_remove` only), `replace`/`swap` (rejected for now).

Lean in italics; **bold** marks the three real design calls.

**Maps (next):**
1. `map.remove(key)` on a missing key: ordinary error, replacing `delete(key, fallback, found)`? *Lean: yes to both.*
2. `m[key]` is a borrow like `v[i]` (write-through changes the entry, consume is an error, missing key is an error)? *Lean: yes.*
3. `m[key] = value` consumes `value` and silently drops any old value? *Lean: yes; `remove` first to keep it.*
4. **Does `get(key, default)` stay at all?** With strict `m[key]` plus error handling it may be redundant. If it stays, what keeps an inline default alive (P5)? *Lean: open.*
5. `keys()`/`values()` views are temporary borrows: not stored, not returned, map not mutated while in use (P4)? *Lean: yes.*
6. Keys are only ever read-borrowed (writing a key would break lookup)? *Lean: yes.*
7. `for (k, v) in m`: `k` read, `v` as the body needs; consuming the map hands entries out by value? *Lean: yes.*

**Beyond maps:**
8. **Two borrows into one container where one writes:** `x = v[0]; y = v[1]; x.cost = y.cost`. Allowed only when the checker proves the indices differ, or always rejected (today's overlap rule is READ + READ only; Rust always rejects)? *Lean: open.*
9. **Extracting a field from a value consumed anyway** (`fn f(p: Packet) = p.payload` at its last use): reject like any field consume, or allow a whole-value destructure (`match consume p`)? *Lean: open.*
10. A reply carrying the payload while the sender still uses its value: a defined copy (section 1)? *Lean: yes.*
11. P8: record inferred effects in `.mossi` and flag READ → CONSUME as a breaking change? *Lean: yes.*
12. Migration: rewrite programs that rely on today's copies (`swarm_021`, indexed reads), or a deprecation period? *Lean: rewrite; no outside users.*

**Not design, but scheduled:** review the known bugs (section 3) once before S3 starts; the checker must now track local borrow bindings (a borrow must end before its container is changed or consumed), which is the largest implementation item.
