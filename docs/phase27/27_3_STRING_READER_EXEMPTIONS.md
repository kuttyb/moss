# Phase 27.3 widened-lint exemptions

Each entry is keyed by source path, enclosing function, and reader API. The lint fails closed if the function disappears or if it contains more than one site for that API.

- `src/semantic_checker.cpp | phase20_is_nontransferable_capability | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.
- `src/semantic_checker.cpp | builtin_equality_type | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.
- `src/semantic_checker.cpp | contains_float_type | split_top_level` — Splits canonical type argument spellings; this is type syntax, not expression or pattern text.

## Audited raw Stmt NAME comparisons

The widened checker enforces these exact per-consumer counts. Every hit reads a
`Stmt` slot in a call/message shape where the slot is a method, handler,
collection operation, effect keyword, assertion name, or `pass` keyword. The
`_` emitter hit is the assignment binding placeholder. Expression-shaped
values (`Map()`, `Queue()`, `Set()`, `[]`) were replaced by EST constructor or
collection-node checks. Any new literal comparison or count change fails the
lint until reviewed and recorded here.

| Consumer | NAME value | Sites | Reason |
| --- | --- | ---: | --- |
| checker | `push` | 5 | Collection method name |
| checker | `insert` | 2 | Collection method name |
| checker | `invoke` | 4 | Executor method name |
| checker | `join` | 3 | Executor method name |
| checker | `consume` | 2 | Effect keyword |
| checker | `pop` | 3 | Collection method name |
| checker | `pop_front` | 3 | Collection method name |
| checker | `assert` | 2 | Builtin call name |
| checker | `sort` | 1 | Builtin call name |
| checker | `pass` | 1 | No-op keyword |
| emitter | `consume` | 3 | Effect keyword |
| emitter | `_` | 1 | Binding placeholder |
| emitter | `delete` | 1 | Map method name |
| emitter | `assert` | 1 | Builtin call name |
| emitter | `assertEqual` | 1 | Builtin call name |
| emitter | `sort` | 1 | Builtin call name |
| emitter | `pop` | 1 | Collection method name |
| emitter | `write` | 1 | FileIO method name |
| emitter | `open` | 1 | FileIO method name |
| emitter | `sync` | 1 | FileIO method name |
| emitter | `close` | 1 | FileIO method name |

All checker/emitter `tree_expression_text(...)` comparisons were removed. The
remaining expression-to-expression equivalence checks call
`tree_expressions_structurally_equal`; name, placeholder, constructor, and
collection checks use EST queries.

The widened lint also rejects punctuation searches, first-character dispatch,
and trimmed-prefix tests applied directly to `Stmt.a`, `Stmt.b`, or `Stmt.text`.
The assignment-place index/member classifiers, vector-literal prefix checks,
and source-free member-call origin search found by fresh CP-E were converted
to EST projections. No raw expression-shape exemption remains.

The checker has nine additional raw `Stmt.a` comparisons against binding
names (`name`, `base`, `statement.a`, or `selected_binding`). In each site the
statement kind makes `a` the assignment/loop binding slot. The lint counts
these separately and rejects comparisons from `.b` or `.text` against a
dynamic string.
