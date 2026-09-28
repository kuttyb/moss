# Phase 15.15 EXPRESS-005 review: closed enums and exhaustive match

## Corrective closeout (2026-09-28)

The table below supersedes the historical peer-review verdict and open-question
text retained later in this document. The 125-probe registry remains
[`check_phase15_15_express005_followups.py`](../tests/tooling/check_phase15_15_express005_followups.py);
fixed F and decided D cases are permanent `control` cases. Run it with
`--expect-fixed` to require all cases to pass through the checker and the
applicable native, Fast Debug, Margo, effects, or synchronization path.

| Finding | Regression | Final disposition |
| --- | --- | --- |
| F1 | F1a–F1c | Fixed: payload enum construction has pure observable effects. |
| F2 | F2 | Fixed: inferred domain state types reach handler match checking. |
| F3 | F3 | Fixed: inferred Queue element types reach `pop()` and match. |
| F4 | F4a–F4e | Fixed: READ-view ownership checks decompose expressions; derived independent values are legal. |
| F5 | F5 | Fixed: `_` pattern binding is rejected. |
| F6 | F6 | Fixed: payload pattern bindings are immutable. |
| F7 | F7 | Fixed: enum Map keys are rejected. |
| F8 | F8 | Fixed: enum field projection is rejected; use match. |
| F9 | F9 | Fixed: trait-typed enum payload storage is rejected. |
| F10 | F10 | Fixed: payload construction requires `field: value`. |
| F11 | F11a–F11d | Fixed: loop back edges reject outer values consumed without definite restoration. |
| F12 | F12a–F12b | Fixed: generated Rust allows `unused_assignments` under `-D warnings`. |
| F13 | F13 | Fixed: unknown bare identifier statements are rejected; `pass` is supported. |

| Decision | Final rule |
| --- | --- |
| D1 | Patterns bind exact declared field names in declaration order; positional renaming is rejected. |
| D2 | `match consume` accepts owned enum bindings and rvalues; interior places still cannot be partially moved. |
| D3 | A READ match borrows its source for the whole match statement. |
| D4 | `replace(place, replacement)` installs an equal-typed value in writable storage and returns the old whole value as owned. |
| D5 | READ enum views may cross `message` and `reply` semantic value boundaries. |
| D6 | Untyped ordinary parameters participate in static enum match specialization; each concrete call is checked. |
| D7 | A consumed `var` may be reinitialized by whole-value assignment; joins and loop back edges require definite initialization. |
| D8 | Enum payload field types remain explicit. |
| D9 | Lowercase legacy `option[T]` remains separate from matchable enums; convergence is a Phase 21 input. |
| D10 | No `Option[T]` alias is added; the iterator contract retains `option[T]`. |
| D11 | `pass` is a reserved, effect-free no-op statement in normal blocks. |
| D12 | Tag-only declaration, construction, and pattern use bare `Tag`; `Tag()` is rejected. |
| D13 | Enums remain functions-only; enum methods receive a targeted diagnostic. |

`replace` evaluates its replacement under ordinary ownership/effect rules.
A nontrivial replacement binding is consumed; copy/trivial replacement bindings
remain usable under ordinary Moss copy semantics. It installs the replacement
without an observable uninitialized state and returns the old whole value. It does not
permit partial moves, shared ownership, `take`, or `swap`. A domain state
machine can use `old = replace(phase, Phase.Idle)` followed by
`match consume old:` to transfer a payload into its next state. A normal READ
match still holds its borrow for the entire match. An owned-rvalue consuming
match consumes the complete value before branch selection.

Historical findings and open questions below record the initial peer review;
their “actual” and “open” statements are superseded by this section.

**Reviewed:** `badceae` (closed enums and exhaustive matching) and `2e96565`
(READ matches on expressions), `main` at `2e96565`, 2026-09-27.
**Method:** 125 minimal probes, plus the existing 32-case rejection matrix, run
through `moss check --json`, native lowering compiled with `rustc -D warnings`
(the flags `moss build` uses), Fast Debug, and `moss effects` / `inspect` /
`type`. Every finding and open question below is reproduced by
[`tests/tooling/check_phase15_15_express005_followups.py`](../tests/tooling/check_phase15_15_express005_followups.py).

## Verdict

Historical peer verdict at the reviewed commits: design approved, with the
corrective blockers listed below. The corrective disposition above supersedes it.

The documented surface works, and native and Fast Debug agree on it:
construction, exhaustive checking, READ and consuming matches, nesting, messages,
modules, source-free `.mossi`, and `-O` / `-Oshared-memory`. The whole-enum
ownership rule (no partial moves) is the right simplification for v0.1.

Four problems block ordinary use:

1. **Domain state machines**, the headline use case, hit an effects bug (F1)
   and an inference bug (F2).
2. **The READ-view check is textual** (F4). It rejects ordinary code such as
   `return name.length()`.
3. **The checker accepts programs that rustc or Fast Debug reject** (F5–F11,
   F13). The docs treat a rustc failure on checked Moss as a compiler defect,
   so each of these is one. F11 is an ownership hole that a non-Rust backend
   would inherit.
4. **Valid programs fail the native build** under `-D warnings` (F12). Exhaustive
   matches make the triggering pattern common.

Thirteen design questions (D1–D13) were listed for the author at review time;
the corrective closeout above records their decisions.

## Running the cases

```sh
python3 tests/tooling/check_phase15_15_express005_followups.py
python3 tests/tooling/check_phase15_15_express005_followups.py --expect-fixed
python3 tests/tooling/check_phase15_15_express005_followups.py --only F4a,C2
python3 tests/tooling/check_phase15_15_express005_followups.py --list
```

| Kind | Meaning in the default mode |
|---|---|
| `control` (F*, D*, C*) | Corrected findings, decided rules, and documented idioms that must keep working. |
| `bug` / `open` | Reserved by the harness for any future unresolved finding. |

`--expect-fixed` requires every case to be correct; use it as the closeout gate.
The script runs inside `make check` (`tests/run.sh`) with `--expect-fixed`.
Each case's source is kept under `tmp/express005_followups/<ID>/` after a run.

## Historical findings at the reviewed commit

Severity: **High** blocks documented use or breaks a stated invariant.
**Medium** is a false acceptance or an engine divergence on an edge. **Low** is
narrow.

| ID | Sev | Finding | Cases |
|---|---|---|---|
| F1 | High | Payload constructors are an `unresolved` effect | F1a–F1c |
| F2 | High | Unannotated enum domain state cannot be matched | F2 |
| F3 | Low | Enum popped from an inferred `Queue` cannot be matched | F3 |
| F4 | High | READ-view escape check is textual | F4a–F4e |
| F5 | Medium | `_` pattern binding diverges across engines | F5 |
| F6 | Medium | Payload binding mutability is undefined | F6 |
| F7 | Medium | Enum-typed `Map` key accepted | F7 |
| F8 | Medium | `value.field` on an enum accepted | F8 |
| F9 | Medium | Trait-typed payload field accepted | F9 |
| F10 | Medium | `Enum.Case(field = value)` diverges across engines | F10 |
| F11 | High | Moves in loop bodies are not checked | F11a, F11b |
| F12 | High | `unused_assignments` fails valid native builds | F12a, F12b |
| F13 | Low | Bare unknown identifier statement accepted | F13 |

### F1 — High: payload constructors are an `unresolved` effect

- **Expected:** constructing a value is pure, as object construction is.
- **Actual:**
  - `moss effects` reports `unresolved: true` for any function that returns
    `E.Case(...)`; tag-only construction is clean.
  - `.mossi` exports `unresolved=1`.
  - A payload constructor is rejected as a domain state default or a `main`
    constructor argument ("state initializers must be side-effect-free"). A
    domain therefore cannot start in a payload-carrying state.
- **Cause:** `observable_expression_effects` (`src/moss.cpp:6228`) has no
  branch for `Enum.Case(args)`. The dotted form falls through the method-call
  path to `effects.unresolved = true` (line 6423).
- **Fix direction:** treat `Enum.Case(...)` and `module.Enum.Case(...)` like
  `objects_.count(callee)`: merge the argument effects and nothing else. Provider
  interface hashes change.

### F2 — High: unannotated enum domain state cannot be matched

- **Expected:** "An initializer normally supplies the type" (domain state,
  `LANGUAGE_SYNTAX.md`).
- **Actual:** with `phase = Phase.Idle`, `match phase` fails with
  `MATCH_REQUIRES_ENUM`, while `moss type Machine.phase` answers `Phase`. The
  checker and the semantic query disagree about one fact. An annotated field
  works.
- **Cause:** the handler-body environment walked by
  `walk_type_environment_block` (match check at `src/moss.cpp:3513`) lacks the
  initializer-inferred enum type.
- **Fix direction:** seed the environment from the same state-type resolution
  that `moss type` uses.

### F3 — Low: enum popped from an inferred `Queue` cannot be matched

After `q.push(E.Case(...))`, `x = q.pop()`, `match x` fails with
`MATCH_REQUIRES_ENUM`. `Vector.pop` and indexing work. The inferred
`queue[...]` element type does not reach the match check.

### F4 — High: the READ-view escape check is textual

- **Expected:** a READ view cannot be mutated, consumed, returned, stored, or
  sent. A value computed from it is an ordinary value.
- **Actual:** these are rejected whenever the view's name appears anywhere in
  the expression:
  - `return name.length()` (an `Int`);
  - `return name + "!"`;
  - `reply text.length()`;
  - `reply values |> sum`;
  - `E.Case(f: helper(view))`.

  The same computation through an intermediate `let` passes (C2). The
  diagnostic then suggests `match consume x:`, which is impossible when `x` is
  domain state or an incoming payload.
- **Cause:** `check_ownership_expression` (`src/moss.cpp:7440`) and
  `require_cross_domain_value` (`:7716`) run `expression_uses(value, view)` on
  the undecomposed expression, before operators and calls are split by callee
  effects.
- **Fix direction:**
  - Reject only when the expression's storage location is rooted at the view,
    after decomposition, as other borrows are handled.
  - Suggest `match consume` only for a consumable scrutinee.

### F5 — Medium: `_` pattern binding diverges across engines

`case Range(_, hi):` passes the checker, which treats `_` as a name. Native
emits `let _ = *_;`, which is invalid Rust. Fast Debug binds `_`. Two `_` in
one arm give `DUPLICATE_PATTERN_BINDING`. Pick one rule and apply it in all
three: reject, since the docs rule out wildcards, or make field-position `_`
an ignored binding.

### F6 — Medium: payload binding mutability is undefined

`case Num(value): value = value + 1` passes the checker, which drops the name
from `immutable_locals` (`src/moss.cpp:7789`). Native emits an immutable
`let value = *value;`, and rustc rejects the assignment. Fast Debug allows it.
The docs say scalar fields "follow ordinary copy rules", which does not settle
it. Decide, then align the checker and both engines.

### F7 — Medium: enum-typed `Map` key accepted

`counts[Color.Red] = 1` passes the checker. rustc fails because the enum lacks
`Eq`/`Hash`, while Fast Debug hashes it. The docs say enums have no hashing.
Reject enum key types in the Map key checks (around `src/moss.cpp:8808`).

### F8 — Medium: `value.field` on an enum accepted

`echo b.value` passes the checker. rustc reports no such field, and Fast Debug
returns the field. Reject projection on enum-typed receivers; the rule is to
use `match`.

### F9 — Medium: trait-typed payload field accepted

`Has(shape: Shape)` passes the checker. rustc cannot find the type, and Fast
Debug runs it. Traits are not storage types. Apply the
`TRAIT_COLLECTION_ELEMENT_UNSUPPORTED` rule at the enum payload check that
already rejects domain handles (`src/moss.cpp:2097`).

### F10 — Medium: `Enum.Case(field = value)` diverges across engines

The checker and native accept the `=` spelling. Fast Debug fails with "enum
constructor requires named fields". Object construction accepts both `=` and
`:`, so either accept `=` in the interpreter or reject it in the checker.

### F11 — High: moves in loop bodies are not checked

`match consume m` inside `while`/`for`, with `m` bound outside the loop, passes
the checker. rustc reports E0382 (moved in a previous iteration). Fast Debug
fails at run time with "unknown local". `let t = s` in a loop shows the same
general gap; enums inherit it.

This is High because the Moss checker is the documented semantic authority and
Fast Debug executes what it accepts. rustc is currently the only backstop, and
a Tile IR backend (Phase 24) would not have one.

**Fix direction:** treat the loop back edge as a use. A binding declared
outside the loop and consumed inside it must be reinitialized before the next
iteration.

### F12 — High: `unused_assignments` fails valid native builds

`var x = a`, followed by an assignment on every branch or arm, is valid Moss.
`moss build` and `margo run` fail with `value assigned ... is never read`,
because generated Rust compiles with `-D warnings` (`src/moss.cpp:17405`). The
prelude allows `dead_code`, `unused_mut`, and `unused_variables`, but not
`unused_assignments` (`:10654`). Exhaustive matches make this pattern routine.

**Fix direction:** add `#![allow(unused_assignments)]`. Binding the name in
every arm, as the documented idiom does (C1), avoids the problem today.

### F13 — Low: bare unknown identifier statement accepted

An arm body of `pass` (or any unknown name) passes the checker, and rustc
fails. This is general, but enums invite it: see D11.

## Historical diagnostics and tooling observations

These observations describe the reviewed commit. The corrective pass updated
the diagnostics, bootstrap, `.mossmap`, and standalone source-free test.

- **`BORROWED_ENUM_PAYLOAD_*`:** the guidance recommends `match consume x:`
  even when `x` cannot be consumed.
- **`MATCH_READ_BORROW_ACTIVE`:** the message names the borrowed place, not the
  write target. Writing `jobs[1]` while matching `jobs[0]` reports "cannot
  mutate or consume 'jobs[0]'". The aggregate-level borrow itself is by design.
- **Enum methods:** they report "enum case fields must be named and typed"
  (D13).
- **`match` on an `Int`:** it reports "must have a known enum type" rather than
  naming `Int`.
- **Bootstrap:** `source_surface.enums.consume_match` says
  `match consume value:`, while `control_flow` says `match consume binding:`.
  The rule is an owning named binding.
- **`.mossmap`:** construct kinds include `function`, `main`, and `type`, but
  not enums.
- **`check_enum_source_free.py`:** it needs `margo run` to have built the
  fixture first. That holds in `run.sh`, but the script fails standalone.

## Historical design questions (all resolved above)

The options below are the peer's original questions. The corrective closeout
table records the binding decision for each D case.

**D1. Named construction, positional patterns.** `case Range(hi, lo)` binds
`hi` to the `lo` field silently; the case prints −8. Construction requires
names precisely to prevent this. Options:

- (a) Pattern names must equal field names, with `field: local` to rename. This
  matches construction and Rust/Python struct patterns.
- (b) Keep positional binding, but diagnose a name that equals a different
  field of the same case.
- (c) Status quo.

**D2. `match consume` of a temporary.** `match consume make():` is rejected,
although an owned temporary carries no partial-move risk. Options: accept any
owned rvalue, or keep binding-only for readability.

**D3. The READ borrow spans every arm.** Assigning the scrutinee in a tag-only
arm is rejected even though no view is live; Rust's non-lexical lifetimes
accept the equivalent. Options: per-arm borrows limited to the live range of
that arm's views, or keep the whole-match borrow and teach the C1 idiom.

**D4. Domain transitions cannot move owned payloads.** State cannot be
consumed, and views cannot be moved. `Running(name) → Done(name)` must
therefore rebuild `name` from the view (for example `name + ""`). This is the
concrete ownership use case that the EXPRESS-002 close-out asked for. Options:

- a narrow state `replace` / `take` that consumes state while installing a
  fallback (compare `Map.delete`'s fallback);
- a consuming match on state that must reassign on every path;
- keep rebuilding.

**D5. Views across `message`.** A READ view cannot be a message argument,
though a READ parameter (C4) and borrowed domain state can. Design §9 already
lends payloads for the extent of the call. Options: treat a view like any READ
value at the boundary, or keep the restriction and document why views differ.

**D6. `match` on an untyped parameter.** `match` does not take part in
call-site specialization, which departs from "infer all the way". Options:
specialize the match per concrete enum, or keep "annotate to match"
(currently documented).

**D7. Re-initializing a consumed `var`.** After `match consume phase`,
assigning `phase = ...` in an arm or any later statement is rejected, while
`x = f(x)` in one statement works. This general ownership rule blocks the
natural `var`-based state machine. Rust allows re-initialization after a move.

**D8. Payload field types are required.** `type` fields may be inferred, but
enum payload fields may not. Options: infer them as implicit type parameters,
shipping generic IR per the "one inference layer" invariant, or keep them
required for a closed ABI.

**D9 / D10. Two sum types.** The iterator contract requires the legacy
`option[T]` (`Some` / `None`). It is not a `match` scrutinee, and the
`Option[...]` spelling the docs used is rejected, because `canonical_type_name`
does not map `Option`. The ledger (EXPRESS-004, EXPRESS-009) says "no interim
Option type". Aliasing `Option` to `option` would break user enums named
`Option`, which work today (C6 shows user `Some` / `None` enums). Options:

- make `option[T]` a built-in closed enum that can be matched;
- move the iterator contract to an enum shape;
- keep it as is, which the docs now describe.

**D11. No no-op arm.** Exhaustive matching forces arms for don't-care cases.
`EMPTY_MATCH_CASE` requires a statement, and there is no no-op statement
(`pass` is F13). Options: a no-op statement, or allow an empty arm.

**D12. `Tag()` spelling.** `Nothing()` is accepted in a declaration and a
pattern but rejected in construction (`2e96565`). Options: one spelling
everywhere, or accept it everywhere.

**D13. Enum methods.** `type` can declare methods; `enum` cannot. Options:
allow methods, or keep functions-only. Fix the diagnostic either way.

## Verified behavior (no findings)

- **Exhaustiveness and patterns:** missing, duplicate, unknown, foreign-case,
  and arity errors; duplicate bindings; `match`, `case`, and `enum` colons are
  required.
- **Construction errors:** unknown, duplicate, or missing fields; type
  mismatches; positional arguments; called tag-only cases; direct and indirect
  recursive layouts.
- **READ matches:** on locals, parameters, aggregate fields, indexed elements,
  call results, and `message` results. Disjoint field writes are allowed;
  overlapping writes and structural mutation are rejected.
- **Consuming matches:** on named bindings, including loop-local ones. Use of
  a consumed scrutinee is rejected, and so is consuming an incoming payload.
- **Composition:** nested enums with nested READ and consuming matches.
  Shadowing of outer names and of the scrutinee behaves correctly in both
  engines. The contextual `consume` does not break identifiers or functions
  named `consume`.
- **Domains:**
  - enum payloads cross `message` and `reply`;
  - enum domain state can be replied and sent;
  - domain handles in payloads are rejected;
  - the lock plan treats a `match` on state as a SHARED read and writes as
    EXCLUSIVE (C5).
- **Pipelines and flags:** enum callbacks work in pipelines; `-O` and
  `-Oshared-memory` match the default build.
- **Modules:** `export enum`, qualified tag and payload construction (C3), and
  source-free `.mossi` consumers.
- **Tooling:**
  - Fast Debug traces the chosen arm as `BranchTaken` with the case name;
  - the formatter is idempotent on enum source;
  - `moss type` and `moss inspect` resolve enums;
  - user `Some` / `None` enums and an enum named `Option` work.

## Documentation corrected in the same series

The earlier peer patch proposed aligning the canonical docs, the language
skill, and the issue ledger with implemented enums. The corrective pass retained
its valid corrections and replaced positional binding with exact-name patterns.
The docs now cover the whole-match READ borrow and the transition idiom, no enum
equality/`echo`/`Map` keys/methods/projection, required payload types, and
`export enum`. It also fixed stale claims:

- EXPRESS-005 was still marked open;
- Fast Debug was said to be unable to run pipelines;
- the iterator type was spelled `Option[element]`;
- V0_1 said Phase 15 had not begun.

The object construction spelling (`Quote(x = 1)` preferred in
`LANGUAGE_SYNTAX.md` and the README, `:` everywhere else) is left for the
author.
