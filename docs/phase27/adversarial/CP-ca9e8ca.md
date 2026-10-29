# Adversarial review: ca9e8ca pipeline Shape normalization

**Verdict: NO-GO**  
**Frozen candidate:** `d25155ea4a1f4eef90ecaa1c1e26038867a7b32a`  
**Target commit:** `ca9e8ca7c0d39ac1d799475385fc574c84a18ce8`  
**Defects:** 1

## Finding 1: nested stage pipelines are flattened too

`shape_of` flattens a pipeline returned for either `node.input` or any `node.stages` child. The former handles a left-associated chain. Flattening a pipeline-valued stage also removes a real tree boundary: `outer |> (inner |> map(...))` becomes the same `pipeline` child sequence as `outer |> inner |> map(...)`. These forms encode different stage structure; the inner pipeline is a single outer stage in the first expression.

The seeded pair `nested-stage.moss` / `stage-flat.moss` exercises nested versus flattened stage structures. The nested probe confirms the AST/string comparator detects the nested form as a structural divergence, while the flattened form passes the shadow comparison and proceeds to semantic checking. The error prints the parsed tree with adjacent `outer`, `inner`, and `map` children, but the string-side tree preserves the nested pipeline under `outer`. This is not a false negative in the current shadow comparator because its string-side normalizer does not flatten this nesting; it does prove that the changed `shape_of` normalization erases a non-associative Shape distinction and is broader than left-association normalization.

## Probes

- `stage-a.moss` and `stage-b.moss` are valid pipelines whose map stages differ (`_ + 1` versus `_ + 2`). Each passes shadow comparison and evaluates to 5 versus 7, respectively.
- `assoc-flat.moss` and `assoc-grouped.moss` encode the same left-associated chain, with and without parentheses around the left pipeline. Both pass shadow comparison and evaluate to 3.
- `nested-stage.moss` parses the non-left-associated pipeline-valued stage. Shadow comparison exits 1 with `MOSS_EST shadow structural divergence`; normal interpreter execution reaches a type ambiguity for the open result. The flat counterpart reaches an unknown-local-function error for its intentionally ill-typed `inner` stage. These are expression-tree comparator probes, not runnable semantic examples.

Commands were run from this isolated worktree:

```sh
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/stage-a.moss
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/stage-b.moss
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/assoc-flat.moss
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/assoc-grouped.moss
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/nested-stage.moss
MOSS_EST=shadow ./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/stage-flat.moss
./moss run --interp tests/phase27/adversarial/cp-ca9e8ca/nested-stage.moss
```

Captured outputs are in disposable `tmp/ca9e8ca-review/{stage-a,stage-b,assoc-flat,assoc-grouped,nested-stage-shadow,nested-stage-runtime,stage-flat}.log`.

## Limits

The compiler's shadow comparison is per-source, so distinct valid stage probes establish that changed stage trees remain distinguishable in the source/behavior probes, but do not invoke a public API that compares two separately supplied ASTs. The nesting probe establishes the concrete over-normalization directly from the comparator's printed trees.
