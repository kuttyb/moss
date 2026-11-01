# Phase 27.5.4 semantic conflict registry

This registry records unresolved differences between the prior checker behavior
and the retained expression-tree interpretation. The implementation preserves
the prior observable behavior for each entry while work continues.

## PIPELINE-COUNT-COMPARISON-001 — comparison before `|> count`

**Status:** Open; language syntax/canonical documentation has not been found to
decide this precedence conflict.

**Minimal reproducer:**

```moss
fn f(tasks: Vector[Int]):
  var index = 0
  while index < tasks |> count:
    index = index + 1
```

**Prior checker behavior:** Rejects at the condition with
`UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'count'`.

**Expression-tree interpretation:** `Pipeline(input = Binary(index < tasks),
stages = [Identifier(count)])`. When `tasks` has a known vector type, the
functional-pipeline path accepts the `count` stage. That changes the prior
verdict and diagnostic.

**Preserved behavior:** The checker recognizes this exact node shape and keeps
the prior diagnostic. It does not reparse the expression text. The five
pipeline-comparison controls in `tests/tooling/check_phase27_est.py` exercise
this conflict family.
