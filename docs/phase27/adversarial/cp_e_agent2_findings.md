# CP-E independent adversarial findings

## Verdict

**BENIGN** — no checker-side expression-text interpretation found in this pass caused a verdict or diagnostic difference from `phase27-2-m1` (`d4446b72`, peeled commit `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`). The candidate cases below either use EST-aware expression equality or preserved legacy constructor/mode spellings, and the tested outcomes match the baseline.

## Scope and method

The checkout was frozen at `8ab1524f` in an isolated worktree. The repository compiler reported `language_version: moss-0.1`. I searched checker and semantic-helper paths for direct source-expression string splitting, exact textual sentinels, and `tree_expression_text` comparisons. The remaining candidates reviewed included `Map()` recognition/refinement, empty-vector checks, FileIO mode tokens, and legacy expression walkers in `semantic_checker.cpp` / `fileio_semantics.inc`. No source files were changed.

Two standalone Moss probes were checked with both the frozen compiler and baseline compiler:

| Probe | Frozen `8ab1524f` | Baseline `d4446b72` |
|---|---|---|
| `cpe_map_parentheses.moss`: `(Map())`, then indexed assignment and read | `ok: true`, no diagnostics | `ok: true`, no diagnostics |
| `cpe_map_get_parentheses.moss`: `(Map()).get("x", 1)` | `TYPE_AMBIGUOUS_OPEN_RESULT` at line 2 | `TYPE_AMBIGUOUS_OPEN_RESULT` at line 2 |

The first exercises a legacy exact `prior.b == "Map()"` refinement path with parenthesized source while confirming the checker still closes the map type. The second exercises the AST-aware `tree_expression_text(receiver) == "Map()"` branch; its open-map rejection is unchanged from baseline. Thus neither is a migration regression or an independently established misparse.

## Artifacts

- `tests/phase27/adversarial/cpe_map_parentheses.moss`
- `tests/phase27/adversarial/cpe_map_get_parentheses.moss`

No escalation. This is a bounded independent pass, not a claim that every checker expression path was exhaustively enumerated.
