# Phase 21 implementation proof audit

**Engineering audit base:** `7775049ab70951b749dfd25cd3902fa4291e187a`

**Provisional integration retest:** `53f11363e2f973e30b1e9a869407b6ecb6eef166`
with Rust 1.98.1 and GCC 13.3. This is not the final candidate SHA.

**Reconstructed peer-addendum A/C repair:** `b7a7ead`, `5b2cbf7` and a
subsequent ownership-join correction on `phase-21-a-c-fixes` (base `6a08830`).
The source fixes and focused gate pass;
the exact combined integration SHA and full-suite rerun remain pending.

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
| O1 | Every executed effect and possible typed variant is included in the concrete handler/callable summary; panic remains separate. | **FAIL at base SHA** for implicit iterator calls and a later reconstructed final-expression method call. Iterator and source-free gates pass on provisional integration. The local/generic trailing-method and reachable grouped-recovery gates pass on the repair branch. ABI-v9 warm-cache repair passes its targeted gate. Exact final-SHA proof review remains **OPEN**. |
| O4 | Every possible raise site has a selected-variant edge to its first matching recovery arm or the handler exit, carrying the ownership state at that site. | **FAIL at base SHA** for reinitialization and expression order. Selected-exit, early condition, expression order and capture gates pass on provisional integration. The standard WRITE argument-order bridge passes its targeted gate after repair. A/C Root and native gates pass on the repair branch. Exact final-SHA proof review remains **OPEN**. |
| O8 | Every body-to-trailer capture is an owned, initialized value with one binding identity on every selected exceptional edge; its type contains no capability or protected view. | Checked local and source-removed method-capture cases pass on provisional integration. ABI-v9 warm-cache and source-free capture gates pass after repair. Final artifact-bound contract and exact-SHA review remain **OPEN**. |
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

A separate imported-method audit found that the ABI-v9 `.mossi` record can
contain an exact `WRITE` parameter effect while `moss effects` in the
source-removed consumer reports `READ`. `infer_effects` resets every object
method's parameter effects before traversing its body; an imported method
has only a synthetic empty body. The consumer then accepts a cross-crate
`std.ParseError` WRITE call and native compilation fails with rustc E0308
because the compiled provider expects a mutable argument. This directly
violates O1's READ/WRITE/CONSUME effect completeness, even though the typed
`raise_set` remains exact. The permanent
`check_phase21_source_free_standard_write_recovery.py` gate requires the
provider metadata's `WRITE` and `Failure.Bad` effects to survive source
removal. It also requires a consumer Root recovery arm to observe the
provider's mutation after that typed raise. A correct cross-crate bridge
must copy the mutated standard enum back before propagating the checked
error. The approved Phase 20 Range deferral does not cover this case.

Other existing adversarial gates cover previously repaired parenthesized,
vector-element, unary, field-projection, helper, generic and pipeline effects,
variant-precise recovery, Root totality, and speculative mapper eligibility.
Those are transfer-rule witnesses. They do not imply an exhaustive induction
over a future language-surface addition. The final audit must inventory the
accepted expression/statement and implicit-call grammar against
`observable_expression_effects`, `phase21_raise_set_block` and lowering, with a
fail-closed branch for any unhandled form. ABI v9 must prove imported effects
come from the selected provider artifact.

### Reconstructed peer addendum A/C: final expressions and reachable arms

The original `p21_review_repros.zip` was unavailable. The permanent
`check_phase21_addendum_a_c.py` gate reconstructs the written shape; its
results are not an exact archive retest. In A, `via_local()` constructs a
`Box`, binds it to `item`, and ends with `item.poke()`, whose method raises
`Failure.Bad`. At `7775049`, `moss check` accepted an unhandled call from
`main`, `moss effects fn:via_local` had an empty raise set with
`unresolved=true`, and native lowering failed with `checked raising method
call lacks a Phase 21 outcome context`. A generic `via_generic(value)`
sibling specializes to the same `Box` through a local alias. With repair
`b7a7ead`, both unhandled main calls fail at Moss checking; the handled
program reports the exact variant and prints `7\n8\n` under native Rust O0,
O3 and Fast Debug.

The root cause was a scope mismatch. `observable_body_effects` advanced the
local type environment through ordinary body statements, but
`infer_observable_effects` and `concrete_function_observable_effects` analyzed
the final expression with the original parameter-only environment. The
repair uses the same post-body type-environment transfer for the final
expression; parameter and receiver-effect inference does likewise. This
argument assumes `walk_type_environment` conservatively joins predecessor
types and concrete unresolved calls fail closed.

In C, `recover Failure.A, Failure.B: raise` may name two alternatives when
only one reaches that arm. Previously the re-raise analysis used the full
named set, so A-only and B-only helpers each reported both variants. The
repair passes the intersection of named and remaining variants into arm
analysis and separately tracks lexical recovery context, leaving a bare
`raise` in an unreachable arm legal with an empty effect. Follow-up
`5b2cbf7` still checks the body of an entirely unreachable arm but omits its
explicit raised effects from the enclosing callable summary. Its fixture
shows `recover B: raise C` after an A-only attempt cannot make C escape.

A-only, B-only, both-variant, and unreachable-arm fixtures pass
`moss check --json`, exact `moss effects --json`, Rust O0/O3 execution and
Fast Debug. The A-only output is literally `outer A`. A Root version with
A-only `on_fail` passes totality, executes its failure reply, and exposes
separate normal `left` and failure `right` lock plans. This proves these
reconstructed paths; exact final-SHA verification and independent Architect
review remain pending.

The effect-level reachable-arm repair exposed a separate O4 join error:
ownership checking still merged the normal successor of an entirely
unreachable recovery arm. If that arm consumed a String, a valid later read
was rejected with `OWNERSHIP_USE_AFTER_CONSUME`. The follow-up repair keeps
checking the arm's body, then excludes its normal and exceptional ownership
successors when its selected incoming set is empty. The permanent gate has
an unreachable B arm that consumes `data` but allows a later read after an
A-only attempt; a sibling whose B arm is reachable must still reject the
same read. The positive runs under native O0/O3 and Fast Debug. This aligns
the ownership CFG's successor set with the variant-reachability rule used by
the effect analysis.

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

The later standard-error cross-crate WRITE bridge adds another O4 lowering
premise. At integration `c2793ce`, a provider method with a later WRITE
`std.ParseError` argument emits its mutable borrow and copy-in **before**
an earlier argument expression is evaluated. A consumer call
`carrier.Flip(set_overflow(error), error)` is accepted by `moss check`, while
native Rust fails E0499 because the bridge borrows `error` before
`set_overflow` writes it. The equivalent same-module program prints `1`
under native O0/O3 and Fast Debug: the first argument changes the enum to
`Overflow`, then `Flip` observes that value and writes `Invalid`. The
permanent `check_phase21_standard_write_order.py` gate requires that result
through source-backed and source-removed provider builds. A bridge may stage
earlier argument values in source order before constructing the later WRITE
temporary, or reject a shape it cannot lower safely at Moss checking. The
general expression-order proof cannot close while this gate is red.

The subsequent bridge repair stages earlier argument expressions before
constructing a later WRITE temporary. The permanent order gate passes on
the combined ABI/standard-error implementation, including source-backed and
source-removed calls, native O0/O3 and Fast Debug. It remains a required
exact final-SHA regression.

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

ABI repair `4711a47` made the warm-cache falsification gate pass with a fresh
strict C++17 compiler build. The final implementation SHA must still rerun
the gate before lifting the quantified source-free premise.

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
| `check_phase21_source_free_cache_binding.py` | Added after ABI-v9 provider binding; no base-SHA result. | Warm consumer build must reject forged metadata with recomputed digest and deleted provider rlib before cache reuse. Provisional `b4a0087` fails; post-repair `4711a47` passes with a fresh compiler build. |
| `check_phase21_source_free_standard_write_recovery.py` | Added after ABI-v9 standard errors; no base-SHA result. | Source-backed and source-removed provider method must retain `WRITE` and exact typed variant; Root recovery must see the mutated standard enum. Provisional integration rejects the valid source-backed call, and source-removed metadata lost `WRITE` then failed rustc E0308. ABI `4711a47` restores the imported effect; the bridge remains under repair. |
| `check_phase21_standard_write_order.py` | Added after cross-crate WRITE bridge; no base-SHA result. | Same-module and cross-crate argument order must agree. At `c2793ce`, same-module O0/O3 and Fast Debug print `1`; cross-crate check accepts but native build fails rustc E0499 because the later WRITE borrow is hoisted before an earlier mutating argument. |
| `check_phase21_addendum_a_c.py` | Reconstructed from written peer addendum because the archive was unavailable. At `7775049`, final local method effect is omitted, and grouped bare re-raise is overapproximated. | On the repair branch: unhandled ordinary/generic callers reject; handled A output, A-only/B-only/both reachable variants, unreachable explicit arm, unreachable-arm ownership positive and reachable-arm D7 negative, Root totality and separate lock plans pass check/effects, native O0/O3 and Fast Debug. Rerun on exact final SHA. |

The final engineering closeout must record the exact implementation SHA,
compiler and Rust toolchain versions, ABI-v9 source-free executions and
falsification tests, complete `make check`/`make examples` results, and the
independent Architect L6 assessment separately. The targeted repairs above
are green in their stated worktrees, but the quantified O1/O4/O8 claims
remain **open** until exact combined-SHA validation and independent review.

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

### Later A/C repair-branch validation

At `5b2cbf7` on `phase-21-a-c-fixes`, strict GCC C++17 compilation and the
expanded reconstructed A/C gate pass, including check/effects JSON, Rust
O0/O3 and Fast Debug. Peer effects, recovery semantics, Root totality,
recovery reinitialization, expression ownership, early condition and warning
gates pass in focused runs. The branch-local `make check` run with the earlier
`b7a7ead` compiler reached the late Phase 10.6C synchronization-plan suite
then stopped at `tests/tooling/check_synchronization_plan.py:202`: after a
deliberate `.mossi` edit, ABI v9 correctly reports
`SOURCE_FREE_CONTRACT_MISMATCH`, whereas this older test expects the word
`rebuild`. This is a test expectation migration, not an A/C compiler failure.
The exact combined-SHA full run, proof review and independent Architect
assessment remain outstanding.
