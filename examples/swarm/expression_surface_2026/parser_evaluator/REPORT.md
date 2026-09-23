# Expression parser/evaluator report

## Program and intent

This is a standalone Moss package (`Moss.toml`, `src/`, and `tests/`) that models
a tiny evaluator for a fixed parsed expression plan. `Expression` has `left`,
`right`, `opcode`, `enabled`, and `priority` fields. It supplies type methods for
direct binary evaluation; the demo's Fast-Debug-friendly evaluator uses typed
ordinary helpers that project those same fields. Opcodes select `+`, `-`, `*`, or
`/`. Disabled or non-positive results become zero, then a nested pipeline maps,
filters, doubles, and sums scalar results.

The demo also uses vector indexing (`values[0]`), `Map` indexed writes/reads,
`Map.get`, record projections, branches, calls, and nested expressions. The four
sample expressions produce `[26, 28]`, total `54`, and named-result calculation
`80`.

## Fresh discovery

Before inspecting swarm work or writing Moss, I read the repository instructions,
`moss-language`, and `moss-agent-workflow`; ran `./moss agent bootstrap --json`
from the repository root; and read the discovered practical guide sections for
types, collections, pipelines, and tests. Bootstrap reported language version
`moss-0.1`, compiler `0.1.0`, package source directory `src`, and the expected
Margo/Moss tooling split.

## First attempt and iterations

The complete first attempt used a readable multiline vector literal, record-method
pipeline, scalar filters, vector indexing, and a `Map`. Its first meaningful
`moss check --json` failed at the first element of the multiline literal with
`MOSS_COMPILE_ERROR: indentation jumps more than one block level`.

Six material iteration points followed:

1. **Parser/layout:** a multiline vector literal failed. The app-sized attempt and
   reduced form are preserved in `failed_attempts/01_multiline_vector_literal.*`.
   The source was flattened to one literal line; this is a frontend/layout
   observation, not a claimed language-design conclusion.
2. **Inference:** the untyped collection-returning pipeline helper failed with
   `TYPE_INFERENCE_FAILED`. `failed_attempts/02_untyped_pipeline_return.*` records
   the diagnostic. Annotating it as `Vector[Expression] -> Vector[Int]` supplied
   the concrete context requested by the checker.
3. **Ownership/collection construction:** filtering `Vector[Expression]` failed:
   `filter over nontrivial element type 'Expression' cannot produce a new
   collection without an explicit deep copy`. The complete application had already
   demonstrated the need; `failed_attempts/03_filter_expression_values.*` is the
   minimized recheck. This is an explicit current ownership rule / missing-copy
   surface, so enabled-state selection moved inside the scalar-producing helper;
   filtering then operates on `Int` values.
4. **Fast Debug parity:** checked and native-valid `map(_.adjusted())` failed in
   `margo debug` with `unsupported or unresolved callable 'evaluate'` when
   `Expression.adjusted` made an unqualified sibling method call. This path is
   preserved in `failed_attempts/04_fast_debug_method_pipeline.*`.
5. **Inference while making the debug-friendly form:** the first ordinary projection
   helper was untyped and failed to infer its record parameter. The diagnostic and
   minimized source are in `failed_attempts/05_untyped_projection_helper.*`.
   Explicit `Expression` parameter annotations are the requested, supported form.
6. **Formatter behavior:** the first formatter pass on the multiline pipeline
   required a subsequent pass before `fmt --check` became clean; the representative
   layout and observation are in `failed_attempts/06_pipeline_formatting_convergence.*`.
   The final source passes canonical formatter checking.

The final reachable pipeline is deliberately `map(adjusted_expression)`, then
`filter(_ > 0)`, then `map(_ * 2)`. `Expression.evaluate`, `accepted`, and
`adjusted` remain implemented and are covered by native tests, while ordinary
typed helpers let Fast Debug execute the application entry path.

## Compiler facts and diagnostics

After the final successful check:

- `moss type expression_values --source src/main.moss --json` resolved the helper
  as `vector[int]`.
- `moss effects expression_values --source src/main.moss --json` reported a single
  `READ` access to `vector[Expression]`, no local/domain mutation or I/O, and a
  possible failure flag.
- `moss calls expression_values --source src/main.moss --json` resolved the named
  pipeline helper target and its three callers (`main`, `expression_total`, and
  `evaluate_sample`).
- `moss why expression_values --source src/main.moss --json` reports bottom-up
  ownership/effect inference over the static call graph.
- `moss inspect main --source src/main.moss --json` resolved its four direct calls
  and a closed, empty domain graph (this is intentionally an ordinary value
  evaluator).
- `moss impact expression_values --source src/main.moss --json` identified its
  dependent helpers and `main`; after the later change, `moss test --affected`
  selected all three local tests with precise dependency paths.

## Validation and parity

All commands below ran from this package directory, using the repository-local
tools via `../../../../moss` and `../../../../margo`.

| Command | Result |
| --- | --- |
| `moss fmt --json`, then `moss fmt --check --json` | Final check passed; source and tests unchanged. |
| `moss check src/main.moss --json` | Passed with no diagnostics. |
| `moss check tests/parser_evaluator_test.moss --json` | Passed with no diagnostics. |
| `moss test --affected --json` | 3/3 selected tests passed. |
| `margo build --json` | Native debug build passed with Rust 1.98.1. |
| `margo run` | Printed `26 28`, `54`, and `80` as described above. |
| `margo test --json` | 3/3 tests passed. |
| `margo debug` | Passed and printed the same demo output. |
| `margo debug --trace` | Passed; JSONL includes `FunctionEnter`, projection `LocalRead`, `BranchTaken`, `Return`, and pipeline result `[26, 28]`. |

Thus final checker, native execution, native tests, Fast Debug, and trace agree
on the main program's observable results. The original method-through-pipeline
form is the explicitly preserved Fast Debug exception, not part of the final
reachable demo.

## Suspected issues and reduced reproducers

`failed_attempts/07_fast_debug_pipeline_sibling_method.moss` checks but
`moss run --interp` fails at its unqualified `evaluate()` call inside a method
reached by `map(_.adjusted())`. The full application was native-valid before the
debug-specific rewrite, so this is not a native-lowering issue. It is a Fast Debug
pipeline-context/sibling-method dispatch gap or regression.

The multiline vector layout and formatter convergence observations are retained as
separate candidate frontend/tooling issues. The untyped return/projection failures
are documented inference boundaries, and filtering nontrivial records is classified
as the checker’s stated deep-copy/ownership rule, not a silent compiler failure.

## Post-completion swarm-ledger overlap

Only after independent completion and final native/Fast Debug validation, I read
`examples/swarm/FINDINGS.md`. The minimized `07_fast_debug_pipeline_sibling_method`
observation overlaps **SWARM-025** (unqualified sibling method dispatch in Fast
Debug) and its pipeline context resembles **SWARM-023**. Although SWARM-025 is
listed as fixed, this map-reached sibling-method path still reproduces the same
`unsupported or unresolved callable 'evaluate'` symptom; it should be recorded as
a regression/expanded reproducer rather than allocated as a duplicate new issue.

No matching ledger entry was found for the multiline vector-literal layout failure,
the explicit-deep-copy `filter` diagnostic, or the formatter’s multi-pass pipeline
convergence. They remain local observations pending coordinator synthesis.
