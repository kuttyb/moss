# Phase 25-C2b.1 — Missing Checker Facts Audit

**Analysis baseline:** `1e9a61bace88806480482a1dee6c3f92e8dac825` (`phase25-integration`, current origin tip at investigation time)

## Verdict

The checker accepts and specializes the source-backed function, but it cannot publish a complete open checked summary for it. The typed semantic API reports **`missing_typed_facts`**, not unresolved effects. The first loss is that the arithmetic expression has no open `TypeRef` or structured operator requirements at declaration time. The concrete specialization rechecks the source body after substituting `x = Int`, so its arithmetic is validated there.

The existing in-memory `moss::Requirement` structure is sufficient to represent the missing obligations (subject, argument types, result type, access, and provenance). **YES:** existing typed requirement records can support this generic arithmetic body without a new semantic representation. However, the current ABI-v10 wire `abi_v10::Requirement` serializes only receiver, operation, access, parent, and provenance (`src/abi_v10_records.hpp:213-219`); it cannot currently carry the explicit argument/result type relation. That wire contract would need to preserve those existing fields before the same evidence is available to a source-free consumer.

## Reproduction evidence

The in-repository probe `tmp/c2b1_compute.moss` contains:

```moss
export fn compute(x):
  return x * 2 + 20

fn main():
  echo compute(5)
```

`./moss inspect fn:compute --source tmp/c2b1_compute.moss --json` reports no available open summary for `compute`; `unavailable_open_summaries` contains its callable ID with gap `missing_typed_facts`. Its specialization is present with an Int parameter/result type key and concrete effects with `unresolved: false`. `./moss run --interp tmp/c2b1_compute.moss` prints `30`.

## Checker path and first missing fact

1. In `check_function()`, an untyped parameter enters the declaration environment as `_generic:x` without a `TypeRef` (`src/moss.cpp:6394-6416`). Integer literals are inferred as primitive `int` (`src/moss.cpp:7259-7264`, `7325`).
2. Arithmetic inference enters the binary branch at `src/moss.cpp:7386`. For `x * 2`, the left side is open legacy `_generic:x` and the right side has an `int` term. The only open fallback returns the left expression when *both* sides have no term and the same open legacy spelling (`src/moss.cpp:7403-7410`), so it does not apply. The inner expression has no inferred `TypeRef`; consequently `(x * 2) + 20` also has no inferred `TypeRef`.
3. `BodyTypeState::known_expression()` resolves locals, indexes, and fields, then returns null for other forms; it has no binary-expression case (`src/moss.cpp:3693-3713`). This is the first point where the summary builder explicitly asks for a retained checked expression type and gets no fact: `build_open_callable_summaries()` calls it on the return expression at `src/moss.cpp:4162-4167`.
4. For result spelling `_generic:x`, the summary initially relates its result to parameter `x` (`src/moss.cpp:4145-4150`). It then asks `known_expression()` for the returned arithmetic. Because that is unresolved and is not a recognized method return, it sets `complete = false` (`src/moss.cpp:4151-4185`). No other completion failure is needed for this reproducer.
5. `project_typed_semantics_v1()` maps a missing or incomplete `open_summary` directly to `TypedOpenSummaryGapV1::MissingTypedFacts` (`src/typed_semantic_api.hpp:341-350`). The probe confirms the JSON name `missing_typed_facts`. Effects are not the blocker: the concrete specialization in the probe has `observable_effects.unresolved == false`.

Thus, the first absent semantic fact is the checked type/relationship for `x * 2`; the outer addition is consequently absent as well. The open summary is incomplete because it lacks a typed return-expression fact, not because effects or parameter access remain unresolved.

## Arithmetic obligations and where they are lost

For `x * 2 + 20`, the summary needs two linked operation facts:

- `x * 2`: operator `*`, subject type `T` (the parameter's open type), argument type `Int`, result type `T`.
- `(x * 2) + 20`: operator `+`, subject type `T`, argument type `Int`, result type `T`.

At specialization, `T` is concretely `Int` here. Both operations must satisfy the built-in numeric rule: operands are `Int` or `Float`, and the result is the promoted numeric type (here `Int`). The remainder operator has a separate Int-only rule, but it is not used here.

The existing discovery paths do not retain these obligations:

- `derive_expression_constraints()` recursively descends binary forms and propagates expected types, but its arithmetic handling only calls itself on each operand; it does not append an operator constraint (`src/moss.cpp:5141-5159`).
- The signature scanner adds `ConstraintKind::Operator` only for the special case where both binary operands are exactly the same parameter (`src/moss.cpp:10269-10311`). Neither `x * 2` nor the nested outer expression matches that case.
- The open-summary producer has a narrow special case that creates a structured `operator:+` requirement for a direct parameter-plus-Int single return (`src/moss.cpp:4214-4275`). For this body, it is bypassed: the `_generic:x` result relation has already populated `summary.result_type` at lines 4145-4150, while that special case is guarded by `!summary.result_type` at lines 4214-4217. It also only handles `+`, so it would not preserve the multiplication obligation.

The declaration checker deliberately defers legality for unresolved template operands: the arithmetic branch in `check_expression()` returns when either side is unresolved (`src/moss.cpp:19846-19861`). The comment says legality is checked in a concrete `TypeEnv`, but no structured operation evidence is retained for this expression beforehand.

## Source-backed specialization validation

For a concrete call, `check_function_call()` recognizes a concrete specialization and registers it (`src/moss.cpp:18782-18812, 18996-19003`). `register_static_specialization()` builds an environment from the canonical argument types and reruns `infer_statement_expressions()` and `check_stmts()` on the function body (`src/moss.cpp:18233-18251`). For `compute(5)`, `x` is `Int`; `inferred_expr_core()` therefore infers both arithmetic expressions as Int (`src/moss.cpp:7386-7401`), and `check_expression()` sees resolved operands and verifies the numeric operator rules (`src/moss.cpp:19846-19897`). The focused probe's `30` output confirms this path accepts the valid concrete specialization.

This is specialization-time validation. At generic declaration time, unresolved operand legality is deferred. In this implementation the concrete body is still available and is rechecked; the typed open summary cannot independently convey the obligations to a compiled-only consumer.

## Existing representation and C2a.1 decoder

`moss::Requirement` already carries a controlled operation identity, subject `TypeRef`, argument `TypeRef`s, optional result `TypeRef`, access modes, parent identity, and provenance (`src/type_ir.hpp:897-910`). It can represent both obligations above without inventing a second checker-side semantic type. The smallest checker-side work is to walk the checked arithmetic expression using its existing type terms and emit one such requirement per operator, retaining expression provenance and the operand/result relationships.

C2a.1's `H3BodyReader::arithmetic_types_match()` enforces structural identity: a concrete Int result requires Int operands; for a generic-parameter result, each operand must be either that exact generic TypeRef or Int, with at least one operand carrying the generic identity (`src/checked_body_ir.hpp:569-592`). `read_expr()` uses this for `Add` and `Binary`, and restricts `Binary` to integer-arithmetic operation tags (`src/checked_body_ir.hpp:636-658`). This rejects inconsistent generic identities, but it does **not** prove the operation is admissible for every instantiation of `T`; it is a shape/type-relation check, not evidence that `T * Int` and `T + Int` are supported. That proof must come from the checker’s requirements and be enforced when `T` is closed.

The current ABI-v10 wire requirement record is narrower than the in-memory typed record (`src/abi_v10_records.hpp:213-219, 272-286`). It records receiver and operation but not explicit argument or result types. Therefore the checker can use existing typed `Requirement`s, but ABI-v10 serialization must carry their type relationships (or an equivalent lossless encoding of those existing fields) before an imported summary can substantiate the same obligations source-free.

## Smallest next implementation task

Add a focused checker-facts test for this single-return body that asserts the complete open summary contains two operator requirements (`operator:*`, `operator:+`) with subject/result `T`, Int argument, and exact provenance. Implement only the expression-to-`Requirement` extraction using existing `TypeRef` and `Requirement` records. Then verify specialization against Int and Float and rejection of a nonnumeric concrete type. Before using the facts across ABI-v10 source-free imports, extend the current wire serializer/decoder to round-trip the existing argument/result relations and validate them at specialization. Keep the C2a.1 decoder’s structural checks distinct from this admissibility proof.
