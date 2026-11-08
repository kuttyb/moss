# Phase 28 Task F1 — Field-alias attack plan

## Submission and gate

- **Plan author:** Agent 1 (Coordinator / implementer)
- **Requested reviewer:** Agent 2 (independent adversarial reviewer)
- **Known rejected implementation:** `019451b884c4a3941d5ddbf8948333f813e918b9`
- **Current implementation action:** none; wait for Agent 2 plan approval before editing code.
- **Branch/baseline:** `phase28-f1-field`, accepted baseline `00b54a51` plus replayed field-effect repair.
- **Plan verdict:** pending Agent 2.

## Attack objective and constraints

Close the NO-GO findings without changing Moss semantics outside checker field
and alias logic. Preserve the 24-loss corpus baseline, all required engine
results, the exact `field_consume` diagnostic, and the expected Step 2
CollectionView classifications. Do not change fixtures' expected diagnostics,
oracles, diagnostic text, Fast Debug, native lowering, optimizer, or unrelated
compiler paths. A missing or ambiguous checked identity/type/occurrence must
fail closed; it must never be repaired by identifier spelling or expression
text inspection.

Agent 2's prior attack found two concrete holes at `019451b8`: the field-alias
READ shape with two alias edges fails at the field alias in both engines, and
the helper falls back to identifier text scanning on missing identity while
pipeline classification falls back to alias-name text and source slicing.

## Proposed checker approach

### Whole-chain effect inference

Treat aliases as a graph of exact definitions, not as independent one-hop
uses. Each graph node is keyed by `(statement occurrence, LHS ExprId, lexical
binding identity, definition identity)`. Each alias edge also stores the exact
RHS ExprId and the reaching source definition selected at that occurrence.
Starting at the field-projection definition, add an edge only when the
checker-resolved RHS binding and reaching definition identify a prior alias
node. Branch edges use the checker's reachable predecessor/reaching-definition
facts. A reassignment ends the old definition's live edge and creates a new
node; it cannot inherit the previous definition by spelling. Loops, cycles,
duplicate source occurrences, or conflicting reaching definitions decline
closed.

For every node, enumerate all reachable terminal uses of that exact definition
and combine their checker-issued effects with the order `READ < WRITE <
CONSUME`. The originating field alias is classified READ only if the graph is
complete (every relevant RHS/use has one exact binding and reaching definition)
and every terminal sink is READ; a complete unused chain is READ because it
has no transfer sink. WRITE stays WRITE and CONSUME stays CONSUME. Any missing,
ambiguous, or contradictory fact is an `Unsupported`/declined result, never an
effect: the ownership rule then reports its existing fail-closed rejection at
the original field-alias definition. It is not converted to READ or to an
invented CONSUME.

The traversal is a monotone worklist over those existing checker definition
identities. Each node's summary is the join of all reachable terminal sinks
and successor summaries; it stabilizes when no summary changes. The summary
flows back over each exact edge to the originating projection, so `b = field;
c = b; d = c; count(d)` is one READ chain and any escape/consume at `d` reaches
the original `b = field` definition.

- READ sinks (count, read-only pipeline, or READ call argument) keep the field
  projection READ, including when the alias passes through multiple locals.
- WRITE sinks propagate WRITE, and CONSUME sinks propagate CONSUME, back
  through every exact alias edge. A field alias ending in a consume/escape is
  rejected at the original field-alias definition.
- Rebinding terminates the old definition edge and starts a new definition.
  Branch joins follow the checker's reaching definitions; a READ on every
  reachable branch remains READ, while any reachable consuming sink makes the
  source alias reject.
- Effects are combined over all reachable terminal uses, not just the first
  alias hop. Cycles or unsupported control-flow/identity shapes decline closed.

### Remove the identifier-text fallback

Delete the `tree_expression_mentions_identifier` fallback from alias use
classification. The replacement is an exact EST-occurrence scan using the
checker-resolved binding/definition identity on every relevant identifier
occurrence. The expected identity comes from the exact reaching definition at
that occurrence, not from its token name. When an occurrence lacks that
resolution, is duplicated, or contradicts the expected definition, return the
existing unsupported/declined classification so the normal ownership path
fails closed at the originating field alias. Do not infer usage from matching
token spelling.

### Remove alias-name and source-text pipeline fallbacks

Delete the `alias_binding_identity.empty() && input_name == alias` branch and
the path that slices statement source to rediscover pipeline stages/source.
Classify a pipeline only from checker-issued pipeline facts attached to the
exact statement/ExprId occurrence: stage operation kind, source ExprId, source
binding and reaching-definition identity, and the source/result TypeRefs. The
consumer of this alias summary must not reconstruct stage meaning from
`functional_operations()` name matching or source spelling. If the checker
does not currently publish one of those facts, extend the checker producer
within this field/alias slice; if exact facts still cannot be established,
decline closed at the originating field alias. No reparsing or text-based
recovery is allowed.

### Early parameter-effect inference

The early parameter-effect pass may continue to record that a plain projected
field initializer is a READ of the parameter projection, but it must not
decide downstream alias effects before exact definitions exist. It must carry
an unresolved/deferred alias summary until the normal checker pass has the RHS
TypeRef, exact EST occurrences, binding identities, and reaching definitions.
That later complete chain summary supplies the parameter effect. If any
required fact is unavailable, the early pass leaves the summary unresolved;
it may not guess READ or CONSUME and may not search identifier text. This path
must preserve `field_parameter`, `field_parameter_caller_reuse`, and
`size(s)` caller reuse as READ in both engines.

## Test plan and expected outcomes

Run every positive result in Fast Debug and Native (`moss run --interp`; emit
Rust with `moss F -o F.rs`, compile with `rustc`, then execute). Run every
negative result in both engines and verify rejection at the stated source
definition. The existing nine probes are:

| Probe | Expected Fast Debug | Expected Native |
|---|---|---|
| `b = s.frontier`; count `b` | prints `3` | prints `3` |
| Count `b`, archive `b` into a no-op function, then count `s.frontier` | prints `3` | prints `3` |
| Return `b` from `archive`, bound to `kept` | reject at `b = s.frontier` | reject at `b = s.frontier` |
| Nested `s.inner.frontier`; count alias and original | prints `3 3` | prints `3 3` |
| `v = [State(...)]`; alias `v[0].frontier`; count | prints `3` | prints `3` |
| Rebind `b` from `[9]` to `s.frontier`; count | prints `3` | prints `3` |
| Branch join `b = s.frontier` / `t.frontier`; count | prints `3` | prints `3` |
| Function `size(s)` aliases and counts field; caller reuses field | prints `3 3` | prints `3 3` |
| `b = s.frontier`; `c = b`; return `c` from `keep` | reject at `b = s.frontier` | reject at `b = s.frontier` |

Agent 2's original independent READ shape must be added unchanged as a
regression and pass in both engines (print `3`, then show the original field
still counts `3`). It has two alias edges, not three. Add the other chain
variants, each in Fast Debug and Native:

| Chain variant | Expected result |
|---|---|
| Two hops: field → `b` → `c`; count `c`, then original | `3`, `3` |
| Three hops: field → `b` → `c` → `d`; count `d`, then original | `3`, `3` |
| Field → `b` → `c`, pass `c` to a READ helper that counts it; then original | `3`, `3` |
| Field → `b`, branch-join into `c` from two field aliases; count `c`, then original | `3`, `3` |
| Field → `b` → `c`, pass `c` to a helper that returns it; reject at field alias | `OWNERSHIP_ILLEGAL_CONSUME` at `b = s.frontier` |
| Direct writable field control: `var s`; write `s.frontier[0] = 9`; read field twice and count | preserve current direct WRITE behavior; expected `9`, `9`, `3` in both engines; otherwise stop before alias-write implementation |
| Field → `b` → `c`; write `c[0] = 9`; read `c[0]` and `s.frontier[0]`, then count | preserve exact WRITE effect; expected `9`, `9`, `3` in both engines, matching the direct control |
| Field → `b`; branch: one arm returns/escapes `b`, the other counts `b` | reject at the original field alias because reachable CONSUME joins with READ |
| Field → `b` → `c`; rebind `b = [9]`; count `c` and original field | `3`, `3`; `c` remains attached to the pre-rebind source definition |

The exact branch-join negative source shape is:

```moss
type State:
  frontier: Vector[Int]

fn keep(items: Vector[Int]) -> Vector[Int]:
  return items

fn probe(s: State, flag: Bool):
  b = s.frontier
  if flag:
    escaped = keep(b)
  else:
    echo (b |> count)

fn main():
  probe(State(frontier = [1, 2, 3]), true)
```

It must reject at `b = s.frontier` in both engines because both branch
predecessors are reachable to the checker and the `keep(b)` arm escapes the
alias. Do not constant-fold the supplied `true` to weaken the static branch
join.

The write case is conditional on the direct-field control immediately above,
using a writable binding (`var s = State(...)`) and `s.frontier[0] = 9`. The
control reads the field twice and then counts it, producing `9\n9\n3`. After
plan approval but before implementation, check this direct-field control on
accepted `00b54a51`. If it passes, the alias-chain write must produce the same
output while checking both `c[0]` and `s.frontier[0]`. If the direct control
does not pass as specified, stop and revise the plan before implementing or
testing alias writes; do not add a new write-through-borrow rule as part of F1.

Agent 2's previously failing probe is preserved verbatim below. It has two
alias edges (`field → b`, `b → c`) and must pass in both engines, printing `3`
for `c` and `3` for the original field:

```moss
type State:
  frontier: Vector[Int]

fn main():
  s = State(frontier = [1, 2, 3])
  b = s.frontier
  c = b
  echo (c |> count)
  echo (s.frontier |> count)
```

Keep the existing field consume rejection and READ controls. Run the corpus
with `python3 tests/tooling/check_phase28_f1_corpus.py <rebuilt-150ab36>
<candidate>` on the direct 238-source corpus; report exact loss names and
require the 24-name baseline with no additions.

For diagnostics, retain combined stdout/stderr transcripts for every candidate
Fast Debug and Native invocation from the corpus, all field probes/gate, and
both completed suite runs. Count literal occurrences with `rg -F -o` over
those transcripts for `Native statement EST handle missing/invalid` and
`Root-ingress capture lacks exact message edge`; require a total of zero for
each string. This exercises check, Fast Debug, native generation/compilation,
and suite paths rather than only searching source files.

The suite comparison baseline is the 170-job Default and Shadow failure-name
sets produced by the same `make check` / `make check-shadow` and
`tests/run_parallel.py` runner on exact source commit `00b54a515364f9237ce758eb866fa621da5375b5`.
Before comparing the candidate, rebuild that commit in an isolated worktree
and save its complete outputs as
`tmp/phase28-f1-baseline-default.log` and
`tmp/phase28-f1-baseline-shadow.log`; derive and freeze the exact failed job
names from those logs. Save candidate logs as
`tmp/phase28-f1-candidate-default.log` and
`tmp/phase28-f1-candidate-shadow.log`. Require at least 117/170 in each mode
and no candidate failure name absent from the corresponding frozen baseline
set.

## Scope and line budget

- Expected implementation delta: roughly 60–120 changed lines, confined to
  checker field/alias inference. This is an estimate, not a hard limit.
- Expected regression delta: about 150–240 lines across the field-control
  harness and purpose-built Moss fixtures.
- No consumer/backend, optimizer, diagnostic-string, oracle, or existing
  expected-result edits.
- If the implementation needs to materially exceed the estimate, stop before
  expanding scope and report which exact fact/edge requires the added code.
  Do not compress logic or weaken fail-closed handling to hit the estimate. If
  retained EST/checker facts cannot represent an alias edge or terminal use
  without text recovery, stop and report the missing fact rather than adding a
  fallback.
- Stop immediately on any of: Agent 2 plan NO-GO; a positive probe failing in
  either engine; a negative probe not rejecting at the field alias; any
  fallback/reparse required; corpus loss beyond baseline or a new loss name;
  either diagnostic count nonzero; field gate failure; suite below 117/170 or
  any new failing job; Agent 2 final NO-GO.

## Requested Agent 2 review

Please attack whether the chain propagation is definition-exact across
reassignment and branch joins, whether every missing lookup fails closed, and
whether pipeline classification can use retained EST nodes and checked
identities without spelling or source-slice fallback. Challenge the probe
expectations, stop point, and line budget. Return plan GO/NO-GO before any code
change begins.

## Amendment 1: field-alias chains through branch joins (Agent 2 GO)

When a downstream alias is read or consumed after a branch join, combine its
effect over every reachable reaching definition at the join using the existing
`READ < WRITE < CONSUME` order. Propagate that joined demand backward over each
exact definition edge, including every incoming field-alias edge, so each
originating field read receives the strongest demand of its complete alias
chain. Match edges with checked binding/definition identities and CFG
predecessor facts; a missing or ambiguous predecessor/source edge fails closed.
Do not choose one branch by display name or expression text. Add/retain the
branch-join chain probe (`field → b`, branch assigns `b` from either field,
`c = b`, then count `c` and the original field); both engines must print `3`,
`3`. The mixed READ/consume branch probe continues to reject at the original
field alias. This amendment changes no other scope or budget.

## L8 dispositions

- **F1-R:** “Fast Debug writes through on whole-local rebind; §3.1 requires a
  new binding. Expected 3 3.” The probe remains in the gate and is expected to
  reject at line 8 with `OWNERSHIP_SOURCE_BORROWED` while another alias of the
  same borrow remains live, matching the accepted baseline. This prevents the
  Fast Debug wrong output from shipping.
- **F1-J:** “Join of two field borrows followed by an alias chain is rejected;
  §4.1 expects accept, output 3 3.” The probe remains in the gate as a known,
  classified failure with the baseline `OWNERSHIP_ILLEGAL_CONSUME` at line 7.
  It is not skipped and is not counted as a new failing job.
- The post-join propagation attempt did not change F1-J's result and is
  reverted. Do not extend this repair in F1 absent a new approved plan.
