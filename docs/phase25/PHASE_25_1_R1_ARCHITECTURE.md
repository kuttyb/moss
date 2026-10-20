# Phase 25.1 R1 checked-body consolidation architecture

Status: design in review. ABI-v9 remains the default; ABI-v10 remains opt-in.
R0 is approved at `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4`. This note is
the R1 design checkpoint before broad compiler changes.

## Current paths and duplication

The current wire has a useful canonical recursive representation in
`checked_ir::Expr`, `checked_ir::Stmt`, and `encode_checked_body_v1` in
`src/checked_body_ir.hpp`. It already preserves TypeRefs, parameter/local
ordinals, literals, operators, fields, vector/map literals, calls, nominal
constructors, collection Count/Index, assignment, branches, loops, match and
reply. The checked-body-v1 tag values already published by this branch are
stable and will not be renumbered or reinterpreted.

The path around that representation is still shape-specific:

| Responsibility | Current paths | R1 consolidation |
|---|---|---|
| Provider body retention | `checked_parameter_return_body`, `checked_scalar_return_body`, `checked_arithmetic_body`, `checked_private_chain_body`, and `retain_checked_collection_loop_ir` in `src/moss.cpp` | One recursive lowerer over the ordinary checked function body plus checker-retained expression, binding, operation, requirement, effect and dependency facts. It returns unavailable if any required fact is missing. Remove statement-count, indentation, local-name and source-spelling admission as semantic evidence. |
| ABI-v10 emission | `append_abi_v10_direct_checked_records`, `append_abi_v10_direct_scalar_records`, `append_abi_v10_arithmetic_records`, `append_abi_v10_collection_loop_records`, and the empty-vector record path in `src/moss.cpp` | One callable-record builder consumes a complete `OpenCallableSummaryFacts` and canonical checked body, validates them together, and emits the same summary/body/provenance/dependency records. Per-shape emitters become unnecessary. |
| Body import | `H3BodyReader` with `allow_empty_vector_return` / `allow_collection_loop_body`, plus `decode_h3_checked_body_v1`, `decode_collection_loop_checked_body_v1`, and `decode_empty_vector_return_checked_body_v1` in `src/checked_body_ir.hpp` | One bounded `decode_checked_body_v1` validates the complete supported checked-body grammar, canonical re-encoding, lexical scopes, dense identities, type substitutions and control flow. No caller selects a narrower decoder based on summary shape. |
| Record validation and projection | ABI-v10 import around `import_checked_body` in `src/moss.cpp`, including summary-specific decoder branches and several checked-body projection sites | One validate-then-project route: validate callable summary, body/hash, provenance, dependency closure, effects and access; project into the existing Moss AST; run the existing checker/specializer/native path. Preserve normal diagnostics and ABI-v9 behavior. |
| Legacy AST representation | `moss::CheckedBodyExprV1` / `CheckedBodyStmtV1` in `src/ast.hpp`, plus conversion wrappers in `checked_body_ir.hpp` | Keep only as read/compatibility adapters while existing focused tests need them; new producer/importer use `checked_ir` directly. Remove adapters only after the focused regression matrix proves no consumer depends on them. |

## One semantic source of truth

The checker remains the sole authority for generic parameter/result TypeRefs,
operation and member requirements, argument access, observable effects,
nominal identity and private callable dependencies. `BodyTypeState` already
retains solver-owned locals, statement binding identities/mutability,
assignment TypeRefs, open collection operations and some operator
requirements. R1 will extend this retained checked state at the point where
ordinary checker visitors resolve expressions and statements; the lowerer may
not infer a missing TypeRef, method, operation or effect from an exported
legacy string, a generated Rust name or a second type checker.

The lowering traversal is structural and recursive. It consumes the existing
parsed function/statement tree and stable expression-site identities produced
while the checker visits those sites. Each supported expression must have its
resolved TypeRef, selected callable/member/operation identity, ordered child
sites and access facts. Each statement must have lexical binding identity,
mutability, scope, control-flow children, and relevant effects. Summary
requirements and body operations are cross-checked against these nodes; any
unrepresented requirement, effect or dependency makes the callable
unavailable rather than complete. Pipeline stages lower from their checked
functional plan, not by independently re-inferring placeholder text.

The R0 matrix is evidence, not the definition of the supported syntax universe:
it has residual combinations and explicitly unprobed forms. The compiler's
current ordinary-function AST variants and the generic semantic constructs in
the Moss v0.1 contract define that universe. Maintain this totality ledger in
the compatibility matrix and link every row to focused source-backed and
source-free tests. A pending row is work, not an exclusion or a capability.

| Source category | Checker-retained occurrence facts required | Checked-body mapping / evidence status |
|---|---|---|
| `Stmt::Raw`, implicit assignment, `Let`, `Var`, rebinding, `Pass`, `Echo`, expression `Call` | Statement occurrence and ordered expression roots; binding identity/introduction/mutability; selected callable and effects | `Let`, `Var`, `Assign`, `ExprStmt`, `Return` as applicable; `Pass` needs a no-op/flow representation or proven normalization. R1 common traversal; validate against ordinary functions. |
| `Message`, `Reply`, `Return` | Handler/call identity, argument and result TypeRefs, access, terminal/continuation state | Existing `Message`, `Reply`, `Return`; preserve current domain restrictions and validate ordinary generic forms where legal. |
| `If`/`Else`, `While`, `For` | Condition/iterator expression occurrences, ordered child scopes, branch joins, loop binding identity/type, return reachability and effects | Existing `If`, `While`, `For`; expand from current bounded loop grammar. |
| `Match`/`Case` | Scrutinee occurrence, exact enum/variant identity, payload bindings/types, exhaustiveness and branch scopes | Existing `Match`/`CaseBranch`; typed match is R0-valid and pending v10. |
| `Try`, `Recover`, `Raise`; `assert` | Error/value occurrence links, active error scope, selected raise/handler identity, effect bits and ordering, assert condition and failure effect | No current checked-body node mapping; add append-only checked-body-v1 tags if same-version fail-closed compatibility holds. R0 source-backed valid raise/recover remains a v9 regression to fix, not an exclusion. |
| Literals, parameter/local reads, unary/binary, field/index, vector/map construction, named calls, nominal construction, Count/Index | One typed occurrence per evaluated expression with resolved TypeRef, exact ordered child IDs, binding/member/call/operation ID, access and effects | Existing tags cover many cases; complete operator, literal, collection, identity and scope validation in unified codec. |
| Queue construction and operations; constrained member dispatch | Receiver/argument/result TypeRefs, exact selected member and nominal identity, operation/requirement/access/effect facts | Queue construction is unprobed. R1 must run source-backed and ABI-v9 probes; valid forms map through existing call/nominal nodes or append-only tags. Open Queue `.push` currently has an invalid R0 probe; do not generalize it to construction or concrete calls. |
| Functional pipelines and supported collection methods | Checked functional plan with ordered stage identities, resolved operation/member IDs, input/output TypeRefs and effects; no stage inferred from placeholder spelling | Normalize the checked plan into canonical expression/operation nodes. Pipeline is source-backed/v9-valid in R0 and pending v10. |

Every evaluated expression occurrence receives a unique checker-local ID while
the ordinary checker resolves it, and stores an ordered child-ID list. Every
statement occurrence receives an ID, parent/branch identity, scope entry and
exit, and expression-root IDs. The existing checker routines are the
authority that create these facts; the lowerer cannot rediscover tree shape
from `Stmt::text`, `a`, `b`, `args`, provider source, generated Rust, or a
second expression parser/type checker. Where current string-based checker
helpers recurse, resolution must record child facts at the same decision
point. If that cannot be done without duplicating semantics, consolidate that
checker path instead of adding a parallel frontend.

Before a generic summary may be marked complete, an exhaustive verifier walks
the checked statement/control-flow tree and requires one-to-one accounting of
all accepted source occurrences: each statement and expression ID appears
exactly once as a tree node or an explicitly documented non-evaluating syntax
marker; all child/root links resolve, are ordered, and are acyclic; every
binding, operation, requirement, effect and dependency fact is consumed or
explicitly attached to a node. Unknown kinds, missing/duplicate IDs,
unattached aggregate facts, and unsupported valid constructs make the callable
unavailable and keep the ledger row open. R2/R3 must close all valid rows
before R4/R5 can claim readiness; per-callable fail-closed admission is not
evidence of complete language coverage.

R0 invalid probes remain negative controls; they do not imply adjacent valid
forms are unsupported. The specific Queue construction probe is an R1
prerequisite to classifying that row. Missing facts remain fail-closed only
while their totality-ledger item is actively tracked to a later gate.

## Wire and importer invariants

- Preserve the checked-body-v1 header, existing enum values, canonical field
  encoding, hash namespace, and existing summary/provenance contracts.
- Add only append-only tags with explicit bounds and old-reader rejection tests
  if R1 requires nodes not currently represented. Never change the meaning of
  an existing tag or accept an artifact an old consumer previously rejected.
- If a needed change requires a new body version, changes ABI-v10 contract
  meaning, or cannot be made append-only and fail-closed, stop before landing it
  and escalate under the autonomous hard-stop rule.
- Bound bytes, field sizes, expression/statement node counts, recursive depth,
  type depth, local count and branch/match counts before allocation. Reject
  duplicate or non-dense local IDs, out-of-scope reads, immutable writes,
  invalid case payloads, unknown semantic IDs, inconsistent TypeRefs, extra
  unreferenced requirements, bad dependency hashes and noncanonical bytes.
- Projection is only into existing Moss AST/checker structures. The ordinary
  checker verifies projected semantics before specialization and native code
  generation. ABI-v10 never executes `generic_stmt`, `generic_source`, or a
  legacy source fallback.
- Do not change ABI-v9 output/default or add a second specialization engine,
  runtime, or user-visible generic syntax.

## R1 implementation sequence and proof

1. Identify the parser/checker expression-site hooks and extend checker-owned
   retention without changing language semantics.
2. Implement and unit-test the structural checked-body lowerer and unified
   bounded reader with deterministic round trips and adversarial malformed
   cases; keep per-shape producers wired until the shared result agrees.
3. Route one existing source-free callable through the unified record builder
   and unified importer in a minimal cross-crate provider/consumer fixture.
   Physically remove provider source, poison/remove selected legacy records,
   require the checked projection trace, and compare Debug/Release output.
4. Switch all already admitted H3/M2b.3/D1-D4/checked-arithmetic paths to the
   shared record and reader code. Delete the superseded per-shape code only
   after each old acceptance/mutation test passes on the unified route.
5. Run R1 acceptance: focused round-trip and hostile-artifact suites, all
   admitted source-free fixtures, ABI-v9 controls, feedback validation, then
   the appropriate full compiler gate. Record exact outputs and the pushed
   SHA; request independent architectural review before R2.

Acceptance requires one lowerer, one record builder and one reader for all
admitted checked bodies, a minimal cross-crate source-free generic round trip,
existing H3/M2b.3/D1-D4/checked-arithmetic evidence green, and no ABI-v9 drift.
This is not a v10 readiness claim; R2/R3/R4 close the remaining R0-valid forms
and SDK parity.
