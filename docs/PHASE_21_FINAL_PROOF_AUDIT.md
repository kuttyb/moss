# Phase 21 implementation proof audit

**Engineering audit base:** `7775049ab70951b749dfd25cd3902fa4291e187a`

**Provisional integration retest:** `53f11363e2f973e30b1e9a869407b6ecb6eef166`
with Rust 1.98.1 and GCC 13.3. This is not the final candidate SHA.

**Status at this audit base:** open. The counterexamples below are accepted by
`moss check` at this SHA or reveal a false D7 rejection. Post-base engineering
repairs are identified explicitly. Every gate must be rerun on the exact final
implementation SHA before this document can serve as a closeout. This document
is an implementation argument and an adversarial audit; behavioral fixtures
alone do not prove its quantified claims.

## Claim boundary and proof method

The authoritative statements are Phase 21 Part III obligations O1, O4 and O8
and v0.1 §§13, 27 and 32. The Phase 15.3 optimizer implements a bounded
leading-conditional placement only. It does not implement early release of a
**touched** guard. Here, an “execution” is one handler's own-domain lock
interval. A nested synchronous message starts a separate callee execution.
A Root `on_fail` starts a second interval after the attempt's resources and
guards are released.

The proof has two layers. The checker must establish sound effects, ownership
and control-flow facts for every admitted source form and imported callable.
The lowering must preserve those facts in native execution. Fast Debug is an
independent behavioral oracle where its documented source surface permits it;
it does not prove physical lock behavior. Native Rust type checking is a final
guard against certain borrow errors, but a Moss program accepted by `check`
that fails during lowering or rustc is an engineering defect, not a successful
Moss diagnostic.

| Area | Implementation invariant needed | Engineering audit status |
| --- | --- | --- |
| O1 | Every executed effect and possible typed variant is included in the concrete handler/callable summary; panic remains separate. | **FAIL at base SHA** for implicit iterator calls. Direct, factory, generic and panic iterator gates pass on provisional integration. Source-removed imported `next()` retains its typed effect and fails closed. The external-provider warm build cache still bypasses ABI-v9 artifact validation after provider mutation; the public build guarantee remains **OPEN**. |
| O4 | Every possible raise site has a selected-variant edge to its first matching recovery arm or the handler exit, carrying the ownership state at that site. | **FAIL at base SHA** for reinitialization and expression order. Selected-exit, early condition, expression order and capture gates pass on provisional integration. The general claim remains conditional on O1 completeness, ABI-v9 validity and final-SHA transfer coverage. |
| O8 | Every body-to-trailer capture is an owned, initialized value with one binding identity on every selected exceptional edge; its type contains no capability or protected view. | Checked local and source-removed method-capture cases pass on provisional integration. The external-provider warm cache is an open source-free artifact-authenticity defect; the general source-free claim remains **OPEN**. |
| Early release | Every touched guard lasts until completion; only a statically untouched guard can cancel, and all later acquisitions remain ranked and precede the first externally observable action. | Bounded leading-conditional plan and raising-condition fallback pass on provisional integration. The proof covers the implemented bounded placement and conservative full-entry fallback, conditional on complete effects and final-SHA code review. |

## O1: effect completeness

### Implementation argument

`observable_expression_effects` in `src/moss.cpp` recursively visits grouped
expressions, vector elements, unary `not`, binary operands, indexed operands,
field-producing bases, enum constructor payloads, calls, methods and eager
pipeline stages. It keeps a `RaiseSet` of concrete identities and independent
`may_panic`, I/O, message, state and unresolved flags. The reachable source
call graph is finite because ordinary recursion is rejected and dispatch is
statically closed. `infer_observable_effects` iterates summaries through the
declaration graph; `concrete_function_observable_effects` rechecks static
specializations with concrete types. Imported summaries must come from a
validated provider contract and must never be re-inferred as empty from
synthetic interface bodies. These facts jointly give a structural induction
only if every expression and implicit call has a sound transfer rule and an
unknown target fails closed.

The two object-iteration desugarings are missing from that induction at the
audit base.
`for item in source` can call an object `iter()` factory and repeatedly call
`next()`. `phase21_raise_set_block` records raises from the source expression,
then descends into the loop body, but omits raises from either implicit method.
`observable_body_effects` likewise does not merge these method effects into
the enclosing handler. The native loop in `gen_block` calls `iter()`/`next()`
as though both returned ordinary values. This is an actual accepted-program
failure, not a speculative proof gap:

| Reproducer | Base-SHA check/effects | Native result |
| --- | --- | --- |
| `phase21_iterator_next_raise.moss` | Check accepts with `ON_FAIL_UNREACHABLE_VARIANT`; `Run.raise_set=[]` although `next()` raises `Failure.Bad`. | rustc E0308: `Result<Option<Int>, Error>` matched as `Option<Int>`. |
| `phase21_iterator_factory_raise.moss` | Same false unreachable warning and empty handler raise set for raising `iter()`. | rustc E0599: `.next()` called on `Result<Counter, Error>`. |
| `phase21_iterator_generic_raise.moss` | A duck-typed `consume(items)` helper specializes to a raising `next()` object; check still claims the Root arm is unreachable and its raise set is empty. | The specialization has the same checked-lowering obligation as the direct loop; it must not escape a concrete checker restriction. |
| `phase21_iterator_next_panic.moss` | Check accepts, but `Run.may_panic=false` even though implicit `next()` performs an out-of-bounds index. | A fatal panic remains distinct from typed recovery; the enclosing summary must still report `may_panic=true`. |

The permanent gate `check_phase21_raising_iterators.py` requires one of two
sound outcomes: exact `Failure.Bad` propagation and native O0/O3 output `7`,
or an explicit `RAISING_ITERATOR_UNSUPPORTED` Moss checker diagnostic, including
for a concrete generic specialization. A
source-free object method must obey the same condition after ABI v9; unknown
imported effects cannot count as empty. Fast Debug currently reports that
`for` execution is unsupported, so this gate relies on Moss checking,
semantic effects and native execution.

Post-base integration fixes the accepted-contract gap conservatively: it
merges implicit `iter()` and `next()` method summaries into observable body
effects and reports a `RAISING_ITERATOR_UNSUPPORTED` Moss diagnostic when a
concrete custom iterator method has a typed raise, including after generic
specialization. This is a supported-language restriction until checked
iterator lowering exists. A custom iterator whose `next()` can panic remains
accepted; its enclosing handler must report `may_panic=true` and an empty
typed `raise_set`. The coordinator reports that both parts pass on the current
integration compiler; the exact final-SHA rerun is still required.
The source-free equivalent is a required ABI-v9 gate: build a provider object
with a typed-raising `next()`, remove its Moss source, and check a consumer
`for`. The consumer must receive the same fail-closed diagnostic before rustc.
The provider's method metadata must be bound to its selected artifact so the
consumer cannot downgrade the raise set by editing `.mossi`.

`check_phase21_source_free_proof_edges.py` executes that source-free gate:
after provider source removal, the imported `Counter.next()` still leads to
`RAISING_ITERATOR_UNSUPPORTED`. It also compiles and runs an imported Root
whose early argument raise carries an owned String into `on_fail` for a
`length()` method call. These are particular source-free paths, not evidence
that every artifact selection or cache entry is valid.

The ABI-v9 checker rejects a `.mossi` whose digest was recomputed without
rebuilding the provider `.rlib`. An adversarial warm-build gate found a
separate entrypoint bypass at provisional integration `b4a0087`: for an
external provider selected through `MOSS_MODULE_PATH`,
`compile_native_artifact` hashes local build-directory `.mossi` files but
omits the external provider from its early cache key. A warm `moss build`
can return a prior executable with `units_analyzed=0` after the external
`.mossi` is forged or the `.rlib` is deleted. `moss check` rejects both
states correctly. `check_phase21_source_free_cache_binding.py` is a permanent
red gate for this defect. The O1 imported-contract premise is not established
for every build entrypoint until that gate passes. The defect is in cache
invalidation, not a change to the ABI-v9 semantic contract.

Other existing adversarial gates cover previously repaired parenthesized,
vector-element, unary, field-projection, helper, generic and pipeline effects,
variant-precise recovery, Root totality, and speculative mapper eligibility.
Those are transfer-rule witnesses. They do not imply an exhaustive induction
over a future language-surface addition. The final audit must inventory the
accepted expression/statement and implicit-call grammar against
`observable_expression_effects`, `phase21_raise_set_block` and lowering, with a
fail-closed branch for any unhandled form. ABI v9 must prove imported effects
come from the selected provider artifact.

## O4: exceptional CFG, definite initialization and moves

### Required graph invariant

For each concrete statement or expression site `s` that may raise variant
`v`, the checker must retain an edge `(s, v, d)` to the first matching lexical
`recover` arm or the handler raise exit. An arm's own raised outcome targets
the next outer scope. The edge carries the definite initialization, move,
binding-identity and type state *after exactly the operations that precede the
raise*. A recovery arm may read an outer binding only when all incoming edges
for its selected variants agree that the binding is the same available value.
The handler's escaping set is the union of all edges that reach its exit.

`phase21_raise_set_block` annotates statements with concrete
`exceptional_alternatives`; `check_ownership_block` stores
`ExceptionalOwnershipExit` snapshots and routes a `try`'s attempt exits by
variant. `populate_phase21_source_exceptional_cfg` serializes individual
raising nodes, scopes, destinations and variant edges for provider interfaces
instead of coalescing sites solely by error variant. The native lowering uses
tagged outcomes. This architecture is sound if the analysis and lowering
agree on all implicit raises, if each captured snapshot is taken at the
correct evaluation point, and if recovery joins selected snapshots rather
than unrelated entry states.

At the base SHA, recovery starts its ownership state from the pre-`try`
`incoming` state and unions moved facts from selected exceptional exits.
It never clears an incoming moved fact that a guaranteed reinitialization
cleared before the raise. `phase21_recover_reinit_positive.moss` consumes a
String, reinitializes the same `var` inside `try`, then raises. The matching
arm reads the new String. `moss check` incorrectly rejects the read with
`OWNERSHIP_USE_AFTER_CONSUME`. The negative sibling fixture makes
reinitialization conditional, and its rejection is correct. The exact merge
for a reachable arm is: preserve the outer scope/type environment, then derive
availability from the **union of moved or shadowed facts on only selected
incoming exceptional exits**. A binding is available if every selected edge
contains the same binding identity and none marks it moved. An unreachable
arm may retain the conservative incoming state. The final implementation must
also verify that types and any borrowed-view facts join consistently; the
move-only repair cannot be taken as a full O4 proof.

The base SHA has a second ownership error at the expression boundary.
`check_ownership_expression` walks call arguments in execution order, but
`check_ownership_block` originally recorded one exceptional snapshot only
after the entire expression. In `consume_after(fail(), data)`, `fail()` raises
before the String argument can be consumed, yet the recover arm's read of
`data` was rejected. The inverse `consume_before(data, fail())` must remain
rejected. A third case gives both the early argument and the callee the same
`Failure.Bad` variant: the selected arm must join both pre-consume and
post-consume states, rather than deduplicate them by variant.

The post-base repair `e1793e8` makes the ownership expression walker record
each recursively visited raising child at its actual evaluation point. It
separately records the direct function/method/FileIO/builtin call outcome after
arguments. A frame tracks child variants so an enclosing grouped, binary,
vector, indexed or enum constructor does not repeat a child variant at its
later end state. Pipeline sources and reduce initializers are visited in
order, while callbacks get an explicit stage snapshot. Direct `raise` and
message-handler outcomes retain their statement-level snapshots. This is a
transfer-function argument for accepted expression forms, conditional on the
observable-effect analysis identifying every typed raise and on native
lowering retaining the same left-to-right order. The new
`check_phase21_expression_ownership_edges.py` gate exercises the argument,
same-variant callee, grouped binary, vector element, and pipeline
source/callback cases. It passes check,
native O0/O3, and Fast Debug for the supported positive paths in the proof
worktree; negative paths retain `OWNERSHIP_USE_AFTER_CONSUME`.

The direct-source enumeration matters when an argument and callee raise the
same variant: subtracting a child's variant from the enclosing expression's
set would otherwise erase the callee's later edge. The observable transfer
walk has typed direct sources at a message outcome, a pipeline callback, a
FileIO operation, a resolved method/function, or a checked builtin call.
The ownership walk records the first two at the statement or pipeline stage,
and explicitly re-adds the other direct call summaries after their arguments.
Grouping, unary/binary operators, vector elements, indexing, field projection,
enum construction and `replace` contribute only their children's typed raises.
Every statement expression analyzed by D7 enters this wrapper: conditions
and match scrutinees, loop sources, declaration/assignment operands, message
receivers and arguments, `echo` arguments, ordinary calls, `reply`, `return`,
`raise`, and raw expression statements. This argument assumes concrete
unresolved calls are rejected by O1 and that the ABI-v9 method summary equals
the selected provider artifact's method behavior.

The raising leading-condition case exposes a separate checker/lowering
disagreement. At the base SHA, `phase21_early_condition_raise.moss` is accepted
with exact `Failure.Bad` effect and a leading-conditional split plan, but
native lowering fails with `checked raising function call lacks a Phase 21
outcome context`. Inserting an effect-free `pass` before the `if` forces the
ordinary full-entry wrapper; then rustc and native/Fast Debug execution print
the expected handled result. The split condition must either propagate a
tagged outcome with correct guard cleanup or conservatively use full entry
when its `raise_set` is nonempty. The final gate requires O0/O3 and Fast Debug
output `1\n4\n2\n`.

The provisional integration compiler takes the conservative full-entry path
for a leading condition with a typed raise, panic or unresolved effect.
`check_phase21_early_condition_raise.py` now passes Moss checking, native
O0/O3 and Fast Debug. `check_phase21_recover_reinit.py` likewise passes its
positive reinitialization and negative missing-reinitialization cases. The
selected-exit merge in `check_ownership_block` starts a reachable recovery
arm's unavailable set from the union of moved or shadowed facts on the
**matching exceptional exits**, not the pre-`try` moved set. The
expression-edge recorder captures children in left-to-right order and a
direct callee outcome after its arguments. These transfer rules close the
identified counterexamples, subject to O1 finding every implicit raise.

Existing D7 gates exercise preinitialization, conditional joins, loop
reinitialization, loop consumption, shadowed bindings, nested recovery,
re-raise and variant-specific ownership. They remain behavioral witnesses.
An implementation-level closure requires a transfer-function audit for
`if`, `while`, `for`, `match`, `try`, `recover`, `return`, `reply`, `raise`,
messages and expression-originated errors, including the implicit iterator
calls above. When a site cannot supply a sound ownership snapshot, Moss must
reject it before native lowering.

## O8: failure-frame ownership

### Implementation argument

`check_domain` computes candidate handler-local capture names used by each
`on_fail` arm, then checks every exceptional exit whose variant selects that
arm. It requires presence, a concrete compatible type, an unmoved binding,
and one binding identity across those exits. It rejects borrowed enum payload
views, any type recursively containing FileIO/Range/RangeBatch, and domain
capabilities. `fileio_contains_scoped_type` descends through object fields and
collection type arguments. `phase21_bounded_error_type` restricts raised
payloads to owned bounded scalar/enum values. Native lowering transports
captures in typed `Option<T>` slots of a failure frame, materializes the frame
before returning a tagged error, and starts `on_fail` after the original
handler wrapper releases its guards. Fast Debug uses a fresh failure frame.

The proof obligation is stronger than Rust compilation alone. A provider
interface with a forged capture or arm contract can still be internally
well-formed while disagreeing with its compiled provider. ABI v9 must bind
the interface to the selected provider artifact and validate the recursive
owned type shape and exceptional-edge contract. Only after that binding can
the source-backed capture argument be lifted across source-free imports.
It also depends on O1/O4: a missing raise edge can omit a capture-path check.

`check_phase21_on_fail_capture.py` covers owned String/message/enum payload
positives, path joins, consumption and missing initialization negatives,
FileIO and borrowed enum view negatives, native Rust and Fast Debug parity.
The added `phase21_capture_state_borrow.moss` attempts to put a protected
domain String into a local consumed by `on_fail`; Moss rejects the move before
the local can cross the guard-release boundary. These tests are specific
counterexamples to common leaks, not a proof that all future type constructors
preserve capability exclusion.

The audit also found a valid local capture that the synchronization planner
could not analyze: `on_fail Failure.Bad: reply data.length()` where `data` is
a checked handler-local String. The checker previously emitted an internal
`unresolved concrete method effect target` error, because both the instance
failure plan and provider interface state-effect export traversed the trailer
body without the capture's type. Repair `6e65560` seeds each arm's validated
capture types, selected error binding and payload-pattern types into those
effect passes. The method-local and early-expression-raise capture fixtures
pass Moss checking, native O0/O3 and Fast Debug. The latter confirms that
`Option<T>` failure-frame lowering can transport a String left available by an
early raising argument. `check_phase21_source_free_proof_edges.py` removes
the provider Moss source, rebuilds the consumer, and observes `3\n3\n` from
a Root trailer calling `data.length()` after an early expression raise.
The provider `.rlib` and `.mossi` are present and match in this case. The
warm-cache counterexample above keeps the quantified source-free premise
open, and the final-SHA gate must rerun after the ABI cache repair.

## General early-release guarantee

### Bounded implementation argument

The ordinary wrapper acquires the complete own-domain ClassSet in rank order
before running the body, then keeps touched guards through result
materialization and normal or typed-raise completion. `derive_path_placement`
has one optimization case: an eligible first top-level `if`. It computes the
condition, both continuations and shared suffix with the checked leaf-effect
walker, including nested recovery arms. It assigns universally required
classes to entry, hoists every branch class whose rank would violate order
after an entry class, and defers only remaining higher-ranked branch classes.
It cancels a rank-forced entry class on a selected path only when that class
is absent from both the condition and the complete selected continuation.
`emit_path_sensitive_wrapper` evaluates the condition with its entry guards,
then cancels those statically untouched classes before any later branch
acquisition. Every class actually touched stays held until its continuation
returns. A raised outcome uses the wrapper's ordinary release path; the Root
failure phase subsequently starts from no old guard.

The v0.1 §32 rule distinguishes *cancelling an untouched guard* from releasing
a touched guard. An untouched guard has no access in the realized logical
execution, so its cancellation does not start the shrinking phase. Later
ranked acquisition is permissible. There is no implemented general early
release of touched guards. The condition may write protected state: that
state class is in the condition footprint and cannot be cancelled. State
WRITE itself is not the separate non-domain `O(h)` of v0.1 §13. When a helper
called by the condition performs `echo`, the helper's `external_io` effect
forces full entry; a message, FileIO or unresolved condition also forces it.
An externally observable action therefore cannot precede a deferred lock
that could determine the execution's serialization position.

The permanent three-class witness in `check_phase21_early_placement_proof.py`
uses a condition method that writes `b_condition.value`. The plan reports
entry classes for `a_low` and `b_condition.value`, cancellation of only
untouched `a_low` on the else path, and later acquisition of `c_high`.
Adding `echo` to the condition method reports full-entry fallback. Both
cases pass native and Fast Debug output. This is evidence that the bounded
path algorithm follows the stated rule on that shape; it does not by itself
establish the general effect or branch-remaining-path analyses. In
particular, the raising-condition counterexample must be fixed before the
path argument covers typed exceptional exits.

The O2 clarification is material to this proof: adding an `on_fail` column
does not add per-leaf WRITE requirements to the original handler, but it may
repartition classes and change the number of normal-path exclusive locks.
The rank and untouched-cancellation argument uses the **final** partition
and the actual per-path class modes. No assumption of a fixed class count is
made.

## Exact regression ledger at audit base

| Gate | Base-SHA result | What the final SHA must show |
| --- | --- | --- |
| `check_phase21_early_condition_raise.py` | Red: check/effects accept; native lowering internal error. | Exact variant, successful O0/O3 native and Fast Debug. |
| `check_phase21_recover_reinit.py` | Red: positive and negative both rejected as consumed. | Positive passes O0/O3 native and Fast Debug; negative remains rejected. |
| `check_phase21_expression_ownership_edges.py` | Red: early argument raise before later String consume is falsely rejected; second-raise and same-variant negative siblings reject correctly. | Early argument and pipeline-source paths pass O0/O3 native and Fast Debug; consume-before-raise, shared-variant callee, and shared-variant pipeline callback remain D7 rejects. |
| `check_phase21_raising_iterators.py` | Red: false unreachable warnings/empty effects; rustc E0308/E0599; panic iterator incorrectly has `may_panic=false`. | Explicit `RAISING_ITERATOR_UNSUPPORTED` for direct, factory and generic typed raises; accepted panic iterator has `may_panic=true`, `raise_set=[]`. |
| `check_phase21_early_placement_proof.py` | Green: plan assertions, native and Fast Debug. | Rerun after all O1/O4/ABI changes. |
| `check_phase21_on_fail_capture.py` | Base cases green, protected state borrow negative; added method-local case was red with an internal planner invariant. | Method-local and early-raise capture paths pass O0/O3 native and Fast Debug; rerun after selected-exit and ABI changes, including source-free. |
| `check_phase21_source_free_proof_edges.py` | Added after ABI-v9 method metadata; no base-SHA result. | Source-removed imported `next()` rejects with `RAISING_ITERATOR_UNSUPPORTED`; source-removed owned String capture and method call run and print `3\n3\n`. Provisional integration passes. |
| `check_phase21_source_free_cache_binding.py` | Added after ABI-v9 provider binding; no base-SHA result. | Warm consumer build must reject forged metadata with recomputed digest and deleted provider rlib before cache reuse. Provisional integration fails both warm-build cases, although `moss check` rejects them. |

The final engineering closeout must record the exact implementation SHA,
compiler and Rust toolchain versions, ABI-v9 source-free executions and
falsification tests, complete `make check`/`make examples` results, and the
independent Architect L6 assessment separately. Until the red gates turn
green and imported contracts are bound to artifacts, the quantified O1/O4/O8
claims remain **open** regardless of other passing suites.

### Audit-worktree validation before integration

The strict C++17 `make` build, expression-edge gate, on-fail capture gate,
recovery-semantics gate and peer-effects gate passed after `e1793e8`.
After `6e65560`, the capture gate, expression-edge gate, recovery-warning and
method-warning gates passed again; the new O8 method captures ran native O0/O3
and Fast Debug.
`make check` advanced through those suites and then stopped at the previously
identified leading-condition lowering gate on this worktree's base compiler.
That historical run does not establish a green integration baseline.

### Provisional integration retest

At integration `53f1136`, using Rust 1.98.1 and GCC 13.3, the focused
raising-condition, recovery-reinitialization, expression-order, bounded
placement, on-fail-capture and iterator gates all passed. The new
source-removed provider method/capture gate also passed. These gates include
native O0/O3 and Fast Debug where each gate supports them. The warm external
provider cache-binding gate failed as described above; thus the entire
`make check` suite has **not** passed on this SHA. An ABI-v9 cache repair and
exact-final-SHA rerun are required before engineering can claim O1/O4/O8
closure. Independent Architect L6 acceptance is separate.
