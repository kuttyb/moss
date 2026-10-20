# Phase 25-M2b.3d-D0: Checked Generic Collection Loops

## Decision and evidence

Implement `index_of` as the first loop shape. Reuse the checked-body statement
model, typed summary, normal specialization, AST checking and native lowering.
Add only typed collection `count` and `index` expression forms plus a bounded
multi-statement decoder/projector. Do not serialize source text or route through
`generic_stmt`.

The baseline is `phase25-integration` at
`32c06c8278a5648b6f78c87e8ecd91c6048c6cc9`. The SDK body is at
[`collections.moss:5`](/home/owner/daji/code/moss3/sdk/foundations/collections/src/collections.moss:5).
An `./moss inspect` probe of the equivalent generic loop reports
`open_summaries_complete: false`, with `missing_typed_facts` for `index_of`;
the concrete specialization also reports `may_panic: true`. This is evidence
that neither the loop's checker facts nor source-free execution are presently
established. The corpus already has the Int loop fixture, currently classified
as legacy-dependent: [`corpus.json:46`](/home/owner/daji/code/moss3/tests/phase25/source_free_corpus/corpus.json:46).

## Operation-to-pipeline map

| `index_of` operation | Existing representation/fact | Gap for checked-body import |
| --- | --- | --- |
| `var i = 0`, `n = ...`, updates | `checked_ir::StmtKind::{Var,Let,Assign}` encode local ordinals and typed expressions in `src/checked_body_ir.hpp:135-176,323-410`. Normal `Stmt` has `Var`/`Let`/`Assign` in `src/ast.hpp:192-205`. | The checker retention entry `Checker::retain_checked_body_ir()` at `src/moss.cpp:4650-4791` accepts only one return/result expression. It does not retain a typed statement tree, local mutability/type environment, or assignment facts for the loop body. `CheckedBodyExprV1` in `src/ast.hpp:166-190` is expression/return-only. |
| `while`, nested `if`, early `return` | Checked IR already serializes `If`, `While`, `Return` and nested blocks; normal compiler AST and codegen handle them (`src/checked_body_ir.hpp:135-176,323-410`; `src/moss.cpp:29327-29395`). | `H3BodyReader` only accepts a single return or narrow let/return body (`src/checked_body_ir.hpp:801-843`). The importer projector at `src/moss.cpp:3656` projects only the existing expression subset. Need bounded block decoding, binding validation, and structured projection. |
| `items |> count` | Checker recognizes functional pipeline stages and has count handling in expression/pipeline analysis (`src/moss.cpp:6127-6191`); existing compiler pipeline/count lowering is already used for ordinary source. | Checked IR has no stable count/intrinsic expression node or exact operation identity. Do not encode this as a source-named call: add a versioned intrinsic tag with receiver/result TypeRefs and validate it against the supported operation. |
| `items[i]` | Checker validates vector index type and element type in `src/moss.cpp:21258-21300`; generic constraint derivation adds `Indexable` (`src/moss.cpp:6255-6275`), and summary construction maps it to `RequirementKind::Index` / `builtin:index` (`src/moss.cpp:5447-5450`). Existing native expression lowering indexes and clones vector elements (`src/moss.cpp:26830-26869`). | `checked_ir::ExprKind` has no `Index` node (`src/checked_body_ir.hpp:66-82`); current H3 reader rejects it. Add a typed index node carrying receiver, index and result TypeRefs, with the vector/index builtin identity and any panic/effect fact needed for validation. |
| `items[i] == target` | Checker checks equality operands for compatible types at `src/moss.cpp:20983-21007`; constraint derivation traverses comparisons at `src/moss.cpp:6276-6295`. `RequirementKind::SameType` exists. | The checked summary path does not currently establish the required generic equality predicate. `abi_v10_checked_requirements()` only serializes `operator:*` and `operator:+` (`src/moss.cpp:40338-40403`). Confirm whether checker-owned equality admissibility can be represented as a same-type relation plus a closed supported-equality constraint, or whether an explicit `operator:==` requirement is required. Never infer equality support from a type name. |
| Parameters, result, specialization | `OpenCallableSummaryFacts` retains bound-variable count, parameter/result TypeRefs, requirements, parameter access and effects (`src/ast.hpp:115-129`). Import reconstruction is in `build_open_callable_summaries()` (`src/moss.cpp:4794+`); `close_open_result_summary()` substitutes bound variables with fresh solver variables at `src/moss.cpp:19465-19527`. | Retain/check a complete summary for this body: parameters `Vector[BoundVar(0)]`, `BoundVar(0)`, result `Int`; count/index/equality constraints tied to that variable; parameter accesses; effects. Do not infer these from legacy strings. |
| Access/effects and native execution | Parameter access/effects are already summarized and specialization rechecks projected bodies (`src/moss.cpp:19415-19441`). `gen_block` already emits while/if/assignment and `gen_function_instance` uses ordinary concrete specialization; indexed access has native lowering above. | Imported checked bodies are presently required pure (`src/moss.cpp:3693-3700,40405-40410`). Indexing may panic, as the compiler probe shows. Validate the exact effects implied by decoded operations, allow the matching summary effect, and retain the ordinary specialization's effect checks. Do not waive effects wholesale. |

## Minimum checked representation and wire contract

**Reuse:** `checked_ir::StmtKind` already covers `Var`, `Let`, `Assign`,
`Return`, `If`, and `While`; `ExprKind::LocalRead`, `IntegerLiteral`, and
`Binary` already carry typed nodes. The statement encoder already recursively
encodes control-flow blocks. Reuse its canonical hashing, TypeRef codec,
typed-v2 summary, callable provenance, dependency validation, ordinary AST
checker, generic substitution and native code generation.

**New representation:** add two closed expression operations to checked IR:

1. `CollectionCount(receiver)` with an exact versioned builtin operation ID,
   receiver TypeRef, and `Int` result TypeRef.
2. `Index(receiver, index)` with exact builtin operation ID, receiver/index/
   element TypeRefs, and the checked effect contract (including possible panic).

Do not represent either operation as `Call` to a guessed function. Reuse
`Binary` for `<`, `==`, `+`, and `-`, with operator-specific operand/result
type validation. Reuse existing statement nodes; no `For` support is needed.

`moss-checked-body-v1` and `checked-body-v1` currently identify a versioned
wire format (`src/checked_body_ir.hpp:425-457`,
`src/abi_v10_records.hpp:425-508`). The implementation should add a
`checked-body-v2` record/body schema for the new node set and block decoder,
while retaining the v1 reader unchanged. This makes old consumers reject the
new body deterministically instead of interpreting new semantics under the
old contract. The v2 encoding must be canonical: fixed node/op tags, explicit
TypeRefs and local ordinals, ordered child blocks, exact callable ID and
dependency hashes, canonical body hash, and no source spelling. Decoder limits
must bound depth, node/statement counts, local count and block size.

The checker must retain one authoritative typed tree from the checked body,
not parse statement strings to reconstruct types. Each local binding needs an
ordinal, exact TypeRef and mutable/immutable bit. Every read must refer to a
previously declared binding; assignment must target a mutable binding and
unify with its TypeRef. Require dense unique ordinals, Bool conditions,
Int loop/index operands, `Int` count, `T` index/equality operands, Bool
comparison result, and `Int` function result. Verify all return paths against
the typed-v2 result. Preserve Vector[T] and T as the same bound variable in the
summary. Parameter access is read-only; local mutation is not parameter write.

For requirements, retain checker-origin call/expression provenance and
serialize only the supported Index and equality facts. At concrete
specialization, require a `Vector[X]`, Int index/count, and equality accepted
by the compiler for the same concrete `X`. Validate summary/body callable IDs,
body hash, semantic dependency closure, schema version, TypeRef bounds,
operation IDs, local binding rules, access/effect agreement and callable
provenance before projecting any body. Missing, duplicate, malformed or
contradictory records reject with `SOURCE_FREE_CHECKED_BODY_INVALID` (or the
existing precise missing-record diagnostic); there is no legacy fallback.

## Consumer projection and reuse

Extend `project_imported_h3_checked_bodies()` at `src/moss.cpp:3656` to select
the v2 decoder from the validated body record version and decoded root shape.
Validate the whole function before changing its body. Project typed nodes into
the existing `Stmt` sequence: map parameter ordinals to imported signature
bindings, map local ordinals to deterministic hygienic compiler-local names,
then emit the ordinary `Var`/`Assign`/`While`/`If`/`Return` AST nodes and
expressions. Preserve nested blocks and early returns structurally. The AST
uses textual expression slots, so print only from already validated typed
nodes through a closed printer; never parse source or `generic_stmt` to create
that projection.

Mark each projected callable with the existing checked-body projection
evidence/trace. Then let `build_open_callable_summaries()`,
`close_open_result_summary()`, `register_static_specialization()`, ordinary
body checking, ownership/effect validation, `gen_block`, and
`gen_function_instance()` handle concrete specialization and native lowering.
Do not add another substitution or codegen engine. Preserve ordinary vector
literal compilation; this work adds no new source vector syntax.

## Ordered bounded implementation sequence

1. **Prove checker facts first:** extend checker-owned retention to capture
   `index_of`'s typed statements, binding ordinals/mutability, expression
   TypeRefs, open summary, parameter access, exact requirements and effects.
   Confirm how generic equality is represented before emission work proceeds.
2. **Add the wire subset:** add the v2 schema and typed Count/Index tags;
   serialize and hash the retained checked tree. Keep v1 emission/decoding
   intact. Add codec round-trip and malformed-record tests.
3. **Decode and validate:** extend `H3BodyReader` or its v2 counterpart for
   this bounded statement/expression subset, validating ordinals, types,
   requirements, operation IDs, effects, identities, hashes and provenance.
4. **Project and specialize:** add the narrow projector at
   `project_imported_h3_checked_bodies()` and invoke existing checking,
   substitution and native lowering. Reject all other body shapes.
5. **Prove source-free execution:** extend the corpus runner with Int and
   String provider/consumer cases, source deletion and poison of the exact
   callable's `generic_stmt`; assert checked-body projection for every
   specialization. Add negatives and unchanged ABI-v9 checks.

## Acceptance gate

Build and seal the provider under staged ABI-v10 with the temporary emitter
switch, then remove provider source before independently compiling consumers.
For `Vector[Int]`, require `index_of([8, 3, 8], 3) == 1` and missing result
`-1`. Repeat with `Vector[String]`, e.g. `index_of(["oak", "moss"], "moss")
== 1` and an absent string `-1`. In each specialization poison `generic_stmt`
for `index_of` (and any imported helper if the finalized dependency closure
contains one), assert the poisoner removed that callable's body, assert the
checked-body projection trace, and execute correct results. The provider must
be source-absent during consumer compilation. The same fixture under default
ABI-v9 must preserve current behavior and defaults; do not remove the v10
emission switch or compatibility records.

Negative cases must deterministically reject: missing typed summary/body;
hash, callable identity or dependency mismatch; invalid provenance; malformed
or unsupported schema/node/op; nonempty or non-Vector index receiver; wrong
index/count/equality/result TypeRef; unbound summary variable; duplicate,
forward, sparse or immutable-assignment local binding; invalid branch/return
shape; forged requirement/access/effects; ABI mode mismatch; and v10 switch
off. No case may succeed by reconstructing a legacy body.

## `from_range` is separate

`bytes.from_range` at
[`bytes.moss:63`](/home/owner/daji/code/moss3/sdk/foundations/bytes/src/bytes.moss:63)
requires generic `For` iteration, a checker-bound iteration variable, helper
call (`is_byte`), `assert`, vector mutation (`push`) and ownership/effect
validation. It is not needed for `index_of` and is not covered by the proposed
While/If subset. In addition, runtime `Range` is a scoped read-only borrow
([`ROOT_RUNTIME_ABI.md:13`](/home/owner/daji/code/moss3/docs/ROOT_RUNTIME_ABI.md:13));
the existing Phase 24 freeze defers concrete cross-crate Range ABI support.
Keep `from_range` deferred until those independent checked-body and borrowed
ABI questions are assigned. This design does not propose a Range ABI solution.

## Unresolved questions and blockers

- What exact checker-owned evidence expresses generic `==` admissibility?
  Existing constraint traversal is not proof that an equality requirement is
  emitted. Establish whether same-type plus a bounded equality capability is
  sufficient, or add a typed operator requirement at the checker boundary.
- Does the typed summary's effect schema encode the index panic condition
  precisely enough to validate it, or is an operation-derived effect fact
  needed? Current import purity checks reject `may_panic` bodies.
- Confirm the source-free importer retains enough callable parameter metadata
  to map checked parameter ordinals without legacy source reconstruction.
- Confirm the minimum private dependency closure for `count` is an intrinsic
  operation rather than a callable dependency; encode that choice in the
  schema and verifier.

These are bounded semantic-contract questions. They do not require a generic
compiler rewrite; do not proceed to emission until checker facts and equality
admissibility are demonstrated.
