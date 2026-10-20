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

The supported node categories and evidence boundary come from the R0 surface
matrix and oracle. ABI-v9 source-free exceptions for raise/recover remain
visible as regressions to correct; they do not make the source form invalid.
Genuine invalid forms remain negative tests. Missing facts remain fail-closed
until their checker representation is added and tested.

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
