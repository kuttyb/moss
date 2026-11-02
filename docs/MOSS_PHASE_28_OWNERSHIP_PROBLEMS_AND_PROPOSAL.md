# Phase 28: Ownership — Current Problems and Proposed Solutions

**Status:** Peer-review proposal, 2026-11-02. Design choices below reflect the agreed Phase 28 discussion; implementation has not been delivered.  
**Target branch:** `phase28-work`. **Compiler baseline:** `main` at `9b840e8ae8cc99e37915fb15367ca31c47b160f8` (Phase 27 closed at `phase27-5-5-m1`).  
**Companion detail:** [`MOSS_OWNERSHIP_S3.md`](MOSS_OWNERSHIP_S3.md) contains the original probes, Map decisions, and complete ownership taxonomy. **If accepted, this proposal replaces that document's rev-10 §0e and review follow-ups #13–#14**, which still allow non-primitive rebinding and reject all borrowed/owned joins. Avoid keeping two contradictory specifications at implementation time.

## 1. Purpose and scope

Moss's current checker eagerly transfers many non-primitive local values; the emitter sometimes silently clones values (especially indexed reads), and Fast Debug can disagree with native execution. Ownership and parameter-effect facts are not yet the sole authority at every consumer boundary. Phase 28 establishes a consistent language rule, then implements and tests it across checker, Fast Debug, native/`-O` emission, package interfaces and SDK usage.

**Principles (agreed):**
- Exactly one semantic owner of each non-primitive value. READ and WRITE are temporary borrows; CONSUME transfers ownership. Backend layout may optimize representation but may not create observable copies.
- Designated primitives alone are implicitly Copy. Records, Strings, Vector, Map and `option[T]` are non-primitive, even when they contain primitives.
- The **checker** chooses access effects and ownership facts. Fast Debug, optimizer and emitter consume those facts; none independently decides Moss semantics.
- No silent non-primitive `.clone()`. Explicit `copy(x)` (source name provisional) creates an independent eligible value. Nonduplicable resources cannot be copied.
- Do not combine this phase with the checker string-parser or driver rewrite cleanup left by Phase 27.

## 2. New rule: path-wise single assignment for non-primitive locals

**One assignment to each non-primitive local binding on any dynamic execution path through that binding's lifetime.** This is a binding rule, independent of inferred READ/WRITE/CONSUME effects.

- A second **whole-local assignment** on the same path is rejected, even if the prior borrow is dead or the local was consumed.
- Whole-local assignment and projection mutation are different: `b.cost = 9`, `v.push(x)`, and `m[k] = x` mutate an owned value or legal WRITE borrow; they do **not** rebind the local.
- Assignments on **mutually exclusive paths** may establish the same logical local; this is a control-flow join, not sequential rebinding. The checker must use binding identity and definite assignment, not mere textual name matching.
- A fresh binding declared inside a loop body is fresh each iteration. Assigning again to an outer non-primitive binding on a loop backedge is rejected.
- Primitive locals may be reassigned normally; existing immutable/`let` rules still prohibit assignments that were previously forbidden. Lexical shadowing creates a distinct binding and must not be used to bypass binding-identity checks.

Illustrative pseudo-Moss (branch-introduced local scope/visibility must be checked against parser rules):

```moss
a = Edge(cost: 7)
b = a
b.cost = 9                 # legal WRITE borrow; a.cost becomes 9
b = Edge(cost: 10)        # ERROR: second assignment of non-primitive b

if condition:
    result = left         # first assignment on this path
else:
    result = right        # first assignment on the other path
echo result.cost          # legal only after definite assignment and join checks

total = 0
for x in ints:
    total = total + x    # legal primitive rebinding
```

## 3. Effects across uses, branches, and calls

An assignment has **one statically inferred effect**, not a runtime READ/WRITE/CONSUME choice. Read all uses of that binding within its lexical scope and transitive callee parameter effects. The required effect is the conservative maximum **READ < WRITE < CONSUME**.

### 3.1 One definition, multiple conditional uses

```moss
b = job.result
if condition:
    echo b
else:
    archive(b)            # consumes
```

The single assignment infers CONSUME even if the READ branch executes. Therefore a consumable `option` field `job.result` is taken and becomes `None` **at the assignment on every path reaching it**. Likewise, consuming a whole Vector/Map field empties it. This is intentional, observable state; the checker should make the consume source visible in diagnostics or an informational compiler query.

### 3.2 Mutually exclusive definitions with no live join

When the differently assigned local has **no use after the branches**, infer each branch independently; do not promote an effect just because another branch constructs an owned value.

```moss
if condition:
    b = edges[0]           # READ borrow
    echo b.cost
else:
    b = Edge(cost: 10)     # owned
    echo b.cost
```

### 3.3 Mutually exclusive definitions with a live join

When different definitions of **the same logical local** reach a use after a merge, join their ownership requirements to the strongest effect. Every reaching assignment must **legally satisfy the joined requirement**, without hidden copies or a runtime tagged owned/borrowed representation.

| Reaching paths | Required joined effect | Result |
| --- | --- | --- |
| READ + READ | READ | Accept if both provenance/lifetime paths are valid |
| READ + WRITE | WRITE | Each source must legally supply WRITE |
| WRITE + WRITE | WRITE | Accept with correct possible-source provenance and exclusivity |
| Borrowed + owned | CONSUME / owned | Borrowed source must be consumable; else reject |
| Owned + owned | owned | Accept if both transfers/constructions are valid |

```moss
if condition:
    b = a                   # promoted to CONSUME
else:
    b = Edge(cost: 10)     # already owned
echo b.cost                # accepted if a can be moved

if condition:
    r = edges[0]           # cannot consume individual element
else:
    r = Edge(cost: 10)
echo r.cost                # ERROR: owned join cannot be supplied from edges[0]
```

Transfers occur **only on the executed path**. After the first example, an unconditional use of `a` is rejected because one path may have consumed it. This conservative rejection is an accepted language tradeoff, even if the post-join operation only READs.

### 3.4 Definite assignment, exceptions, and loops

Apply the same principles to `if`/`else`, `match`, `try`/`recover`, early returns, and loop backedges. A post-merge use requires a value on **every reachable continuing path**. A non-continuing branch (`return`, `raise`) does not contribute a reaching definition. Loop binding identities and iterations must be explicit; do not confuse a new per-iteration local with a second assignment to a live outer local. Borrow lifetime/exclusivity still applies even when the joined effect is READ.

These are **implementation proof obligations**, not permission to silently restrict other agreed Moss constructs.

## 4. Current problems and Phase 28 solutions

| ID | Current problem/evidence | Agreed solution |
| --- | --- | --- |
| P1 | Non-primitive `b = a` usually moves `a` even if `b` only reads | Scoped inference chooses READ, WRITE or CONSUME from uses and callee summaries |
| P2 | Records of primitive fields and `option[primitive]` can be treated inconsistently with Copy | Only primitive value types are implicitly Copy; aggregate borrowing is legal, implicit duplication is not |
| P3 | Indexed read `v[i]` may emit silent `.clone()`; unified-field extraction has inconsistent move behavior | Borrow indexed elements and plain fields; forbid individual element/unified-field CONSUME; use explicit `copy`, `remove`, `pop`, `swap_remove` for independent ownership |
| P4 | `Map.keys()`/`values()` return cloned snapshots | Temporary borrowed views with checked lifetimes; structural mutation while live is forbidden |
| P5 | Map access/removal ownership is inconsistent | `m[k]` and `m.get(k)` borrow and raise on absent key; `m.remove(k)` transfers removed value and raises if absent; `m[k] = value` consumes replacement; borrowed `for(k,v)` iteration |
| P6 | Emitter inserts non-primitive `.clone()` at many call, return, sort, constructor and read sites | Every duplicate must trace to explicit eligible source `copy`; otherwise borrow, move or report a checker error |
| P7 | General `swap`/`replace` could extract unified fields | Not in Phase 28; permit `Vector.swap_remove` only as a collection-owned removal operation |
| P8 | Inferred effects must survive package boundaries | Use existing `.mossi` concrete READ/WRITE/CONSUME and `parameter_leaf_effects`; preserve checked hashes/fail-closed validation |
| P9 | `option[T]` is not yet a general field type | General `option[T]`; consuming option **field** leaves `None`; consuming whole Vector/Map **field** leaves empty collection; consuming owner local makes it dead |
| P10 | Source message/reply rules permit some transfers not wanted in immutable snapshots | Incoming synchronous payloads READ/forward only; no WRITE/CONSUME/storage/rebinding or `reply payload` even for primitives, including alias bypass. Explicit eligible `copy(payload)` or independently computed reply is permitted |
| P11 | Local reassignment and branch merges can change ownership provenance | Path-wise single assignment; strongest legal effect at live joins, branch-local effects when no join use |
| P12 | Some consumers still independently choose copy/move/borrow or disregard checked facts | Checker-owned keyed ownership facts; consumers mechanically lower or fail closed; perturb/drop tests |

Details for Vector/Queue, Map error semantics, field lifetime taxonomy, borrowed collection views and SDK migration are in [MOSS_OWNERSHIP_S3.md](MOSS_OWNERSHIP_S3.md); this proposal does not weaken them.

## 5. Reuse of Phase 27 and inference implementation

Phase 27 established retained expression handles/IDs, per-specialization checked facts, stable call/specialization facts, and consumer fail-closed type authority. Do **not** invent a second ownership-analysis authority in the emitter or Fast Debug.

**Today:** `semantic_checker.cpp::infer_effects()` initializes function/method parameter effects to READ and iterates bodies and callee effects until unchanged, with at most `4 * (functions + methods) + 4` rounds. The three-round `inference_rounds_cap_reached` belongs to a **different type/signature inference loop**, not to effect inference. Source-free `.mossi` effects are retained. Concrete generic effects can require another specialized body analysis; detailed leaf-effect capture sometimes rewalks callee source.

**Phase 28:**
1. Add binding identity, per-path single-assignment/definite-assignment facts, reaching-use effect demand, provenance and lifetime to the **checker**. Existing `Stmt::joined_types_by_context`, EST occurrence IDs and `SemanticFacts` are starting points, not automatically sufficient ownership records.
2. Reuse cached checked formal parameter/receiver effects and concrete specialization summaries for local alias chains and calls. Preserve `.mossi` effect and leaf records; recheck callers when provider summaries change.
3. Extend the existing monotone effect solution with local-assignment demands. Add an **effect-specific convergence** gate that fails if the bound is hit while effects are still changing; do not confuse this with the signature inference counter. Missing/contradictory facts fail closed.
4. Eliminate repeated callee-body rewalks **per local alias**. Where detailed leaf effects currently rewalk bodies, preserve correct provenance and improve or cache summaries before broadening this work; do not hide cost or assume the generic cache covers all leaf work.
5. Record expression/context-specific ownership access facts and source location/provenance so the emitter and Fast Debug cannot independently reinterpret assignment, borrowing, calls or merges. Preserve domain lock-leaf provenance and existing 2PL ordering.

## 6. Known failures and how to classify them

The Phase 27 closeout on `main` recorded **162/165** passes for both default and shadow; the same three jobs failed, with no new Phase 27 regression. These are **test jobs**, not three independent semantic defects.

| Evidence | Classification | Phase 28 handling |
| --- | --- | --- |
| `tests/swarm_035_nested_mutating_calls.moss`: Rust E0499 in `put(take())` / `b.put(b.take())` (two jobs) | Ownership-sensitive Rust lowering and evaluation order | Add sequential evaluation/borrow-lowering regression; do not reject valid Moss solely to satisfy rustc |
| Group A E0502, field projections/self assignment | Ownership-sensitive Rust lowering; some historical source forms may become illegal under P3 | Re-probe with Phase 28 legal forms; fix remaining valid cases |
| `mutate_loop_element` E0596 | WRITE iteration/borrowing mismatch | Enforce legal WRITE borrow and correct native lowering |
| `repro_push_loop_reference` E0308 | Borrowed String supplied to owned Vector push | Checker rejects implicit consume; explicit copy or removal is required, never hidden clone |
| 10 `ownership` perturb/drop ignore entries in `phase27_5_4_fact_hook_debt.json` | Checker-authority implementation debt | Retire all ten by making all affected consumers follow facts or fail closed |
| `compiled_only_custom_key` Cargo jobserver FD 3 | Build/test infrastructure | Track separately; not an ownership language failure |
| Rust keyword `box`, `assert` call lowering, unrelated C1 probes | General emitter/accepted-Rust debt | Track separately; do not claim Phase 28 fixes them merely by ownership changes |

**Known semantic parity fixture:** `ed = edges[0]; ed.cost = 9; echo ed.cost; echo edges[0].cost` must output `9`, `9` in Fast Debug, native and `-O` (no invisible indexed clone).

**Important:** the existing accepted→rustc allowlist is broader than ownership. A zero-ownership-debt gate does not automatically mean the global accepted→rustc invariant is satisfied.

## 7. Proposed delivery sequence and gates

**M0 — Baseline and rule reconciliation.** Start implementation from Phase 27-closed `main`, not the stale Phase 28 work-branch source. Preserve the peer-reviewed ownership decision history. Resolve contradictory §0e prose in `MOSS_OWNERSHIP_S3.md` upon approval. Add focused positive/negative fixtures for the exact new rules before changing the checker.

**M1 — Checker local semantics.** Binding identities, non-primitive path-wise single assignment, definite assignment, branch/no-live-join behavior, owned live joins and path-sensitive move-validity diagnostics. Cover `if`, `match`, exceptions and loops, including non-continuing branches.

**M2 — Interprocedural closure and provenance.** Local effect demand through aliases and generic/imported callees, effect-specific fixed-point convergence/fail-closed summary reads, source-leaf provenance, borrow exclusivity, read/write locking facts.

**M3 — Aggregate/message semantics.** Implement unified/option/collection place rules, Map/Vector borrowed and consuming APIs, explicit copying, borrowed views/iteration, and immutable incoming message/reply restrictions.

**M4 — Consumers and native Rust validity.** Fast Debug, native/`-O`, optimizer and `.mossi` consume checker facts. Remove inferred clones, correct evaluation ordering and guard lifetimes, and fix ownership-relevant accepted→rustc cases.

**M5 — Migration and release gates.** Update `LANGUAGE_SYNTAX.md`, `MODULE_ABI.md`, Gentle Introduction and relevant compiler documentation; migrate SDK/tests/examples without compatibility shims. Run targeted checks during implementation and complete `make check` and `make check-shadow` at the merge gate; record unchanged unrelated baseline failures explicitly.

**Acceptance evidence:**
- Positive/negative tests for each ownership decision and each path-wise binding/join case; at least one `try`/`recover`, early return, loop backedge and imported generic case.
- Checker accepted → generated Rust compiles for targeted ownership cases; native, Fast Debug and `-O` agree in behavior.
- New ownership facts are checker-authored and mandatory; perturb/drop tests for each consumer boundary; **zero ignored ownership cases** in the existing 10-entry baseline.
- Source `copy` to any generated nonprimitive clone has auditable provenance; no implicit semantic clone.
- Effect-specific convergence verified, existing compiler debt counters do not grow, and no extra Phase 27 type/target/specialization debt appears.
- Distinguish baseline failure remediation from Phase 28 language acceptance; log actual suite counts instead of promising an unmeasured clean 165/165.

## 8. Peer-review checklist: challenge the proof, not the agreed syntax preferences

Reviewers should test these specific failure modes:
1. Can path-wise single assignment be enforced by **binding identity** across `var`/`let`, branches, loop iterations, shadowing and exceptions without falsely rejecting independent per-iteration bindings?
2. Does ownership promotion at a **live** join correctly require a legal transfer on each reaching path, while leaving dead branch-local borrows alone? Does the flow model exclude returning/raising paths?
3. Is a single-definition READ/CONSUME join still interpreted as a **consume at its original assignment** even when the actual runtime use takes the READ branch? Are observable option/container field resets precisely documented?
4. Do transitive parameter/receiver/specialization/`.mossi` effects propagate to local definitions with reliable convergence and without per-alias reanalysis of the same callee bodies?
5. Are borrowed sources/whole-collection conflict leaves preserved through aliases and calls, and are WRITE/move restrictions enforced without hidden clones?
6. Do all affected consumers follow checked ownership and fail closed on dropped/contradictory facts? Can valid `swarm_035`-style sequential mutation compile as Rust?
7. Which existing language examples or SDK patterns genuinely become impossible (e.g. repeatedly building a String without an in-place builder), and are they acceptable explicit costs of non-primitive single assignment?

**Not open as part of this peer review:** implicit nonprimitive copying, general `replace`/`swap`, per-index collection conflict proofs, or runtime tagged mixed-owned/borrowed join values. A reviewer may raise a concrete counterexample, but should label it as a proposed design change rather than silently alter these decisions.

## 9. Related source of truth

- [Phase 28 original inventory and detailed decisions](MOSS_OWNERSHIP_S3.md)
- [Phase 27 compiler contract](https://github.com/kuttyb/moss/blob/main/docs/MOSS_COMPILER_CONTRACT.md) — authoritative invariant/debt tracking on `main`; branch implementation must merge/reconcile it
- [Phase 27 architecture](https://github.com/kuttyb/moss/blob/main/docs/COMPILER_ARCHITECTURE.md) — stage boundaries and existing checked fact handoff on `main`
- [Phase 27 closeout report](https://github.com/kuttyb/moss/blob/main/docs/phase27/27_CLOSEOUT_REPORT.md)
- [Phase 27.5.5 report](https://github.com/kuttyb/moss/blob/main/docs/phase27/27_5_5_REPORT.md)
