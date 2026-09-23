# Expression-surface dogfood report: numerical tool

## Intended program

This package analyzes paired integer sensor channels. A sample's energy is the
square of `abs(observed - baseline)`. `window_score` is the mean energy of
samples at or above 25, multiplied by 100. The demo additionally finds the
peak index, totals baselines for alerting samples, computes the integer mean,
applies a `Calibration` field projection, and identifies any energy at least
100. This was deliberately chosen to use arithmetic combinations, predicates,
nested helper calls, record-field projection, vector indexing, vector mutation,
and functional collection transformation.

Final source: `src/numerical_tool.moss`; tests:
`tests/numerical_tool_test.moss`.

## Fresh discovery

Before examining any existing swarm implementation, I read the repository
instructions and both Moss skills, ran `./moss agent bootstrap --json`, and
verified `result.language_version == "moss-0.1"`. The live source surface and
the Gentle Introduction supplied the current forms for typed empty vectors,
`count`, pipelines, tests, and package layout. The first meaningful check and
Fast Debug attempt were made against `failed_attempts/01_len_method.moss`.

## Iterations and preserved evidence

There were eight source attempts (four language/ownership rewrites, one native
backend workaround, and three syntax/type discoveries). Every failed source and
its available command output is retained under `failed_attempts/`.

| # | Natural attempt | Observed result and classification | Final response |
| --- | --- | --- | --- |
| 01 | Multi-line record vector plus `.len()` | `MOSS_COMPILE_ERROR`: indentation jumps more than one block level before type checking. Syntax discovery, not a feature conclusion. | Flattened the literal to expose the next independent error. |
| 02 | Untyped named map helper over `Sample` | `TYPE_INFERENCE_FAILED` for `sample`; `type` produced the same structured result. The map use did not infer this function parameter. | Annotated the mapper as `sample: Sample`. |
| 03 | Familiar `samples.len()` | `invalid collection operation 'len'`; the ownership query also returned that diagnostic. This is a documented API spelling omission, not a compiler gap: cardinality is `samples |> count`. | Used `|> count`. |
| 04 | `filter(is_alert)` over `Vector[Sample]` | Checker: filtering a nontrivial element cannot create a new collection without an explicit deep copy. Current collection/ownership restriction. | Moved alert aggregation to an indexed pass over primitive numeric channels. |
| 05 | Two primitive pipelines from the same vector | `OWNERSHIP_USE_AFTER_CONSUME`; `ownership window_score` was also requested. Intended ownership behavior: the first pipeline transfers its input. | Accumulated score and count in one pass. |
| 06 | Repeated indexing of `Vector[Sample]` in peak selection | `OWNERSHIP_USE_AFTER_CONSUME` after retrieving a nontrivial element. Current indexed collection ownership restriction. | Represented the numerical data as parallel `Vector[Int]` channels, whose indexed values can be reused. |
| 07 | `filter |> any` Boolean pipeline | Source check and Fast Debug passed, while native compilation failed. The isolated package reproduces the same generated-Rust parse failure. **Compiler native-lowering bug**; not a source-language absence. | Used the equivalent numeric terminal: bind `filter |> sum`, then compare the resulting total. |
| 08 | Compare directly after a pipeline terminal | `UNKNOWN_SYMBOL_OR_TYPE: unknown local function '100'`; the comparison was parsed as a pipeline stage. Syntax/precedence behavior. | Bound the pipeline result before comparing it. |

The original full-program native failure is retained in `margo_build.json` from
the pre-workaround run; the minimal reproducer has its own source, check, Fast
Debug, and native-build output in
`failed_attempts/07_boolean_pipeline_native/`.

## Natural successes in the final source

- `magnitude`, `deviation`, and `squared_energy` compose arithmetic and nested
  helper calls.
- The `Calibration` record and `calibrated_observed` exercise field projection.
- `window_score`, `peak_minute`, `alert_baseline_total`, `mean_observed`, and
  `energy_series` use mutable locals, predicates, and indexed primitive vectors.
- `energy_series` uses `Vector[Int]()` plus `push`; `has_large_shift` uses the
  final eager `filter |> sum` pipeline and a Boolean comparison.
- `type window_score` resolved to `int`; `effects window_score` reports both
  vectors as `READ`; `calls window_score` resolves `squared_energy`; `cost
  has_large_shift` reports zero materialized functional intermediates.

## Validation and tool agreement

All final validation was run from this package directory with the repository
tools:

```sh
../../../../moss fmt src/numerical_tool.moss --json
../../../../moss fmt --check src/numerical_tool.moss --json
../../../../moss check src/numerical_tool.moss --json
../../../../moss type window_score --source src/numerical_tool.moss --json
../../../../moss effects window_score --source src/numerical_tool.moss --json
../../../../moss calls window_score --source src/numerical_tool.moss --json
../../../../moss why has_large_shift --source src/numerical_tool.moss --json
../../../../moss cost has_large_shift --source src/numerical_tool.moss --json
../../../../moss impact window_score --source src/numerical_tool.moss --json
../../../../moss test --affected --json
../../../../margo build --json
../../../../margo run
../../../../margo test --json
../../../../margo debug
../../../../margo debug --trace
```

Results:

- Both formatter commands succeeded and reported no source changes.
- Final `moss check --json` succeeded with no diagnostics.
- `margo build --json` produced the debug executable successfully.
- Native run and Fast Debug both printed `6800, 3, 20, 13, 11, true` (one value
  per line).
- `margo test --json` passed all 5 tests.
- The trace succeeded and records function entry/exit, local reads/writes,
  loop iterations, helper calls, and returns in NDJSON.
- `moss test --affected --json` selected no tests because the semantic snapshot
  was unchanged at that point; the full native test run is the authoritative
  result.

Validation JSON/text and trace artifacts are stored alongside this report.
Generated build directories were removed with `margo clean` after successful
validation; retained logs remain the durable evidence.

## Diagnostic quality

The checker diagnostics were specific enough to separate parse, type inference,
collection API, ownership, and backend failures. The ownership errors included
the consumed binding and transfer line. Semantic query commands correctly refused
to proceed when a prior semantic error remained and returned useful positive
facts after the final check. The backend failure was reported as
`BUILD_BACKEND_ERROR` and included the native compiler's exact invalid return
type span, enabling a minimal repro without treating it as a source limitation.

One operational note: running bare `moss fmt --json` at this package root
attempted to validate retained intentionally-invalid `failed_attempts/*.moss`
and failed. Targeted canonical formatting of `src/numerical_tool.moss` succeeds
and is the appropriate final formatting result for this evidence-bearing package.

## Suspected issue / minimal reproducer

`failed_attempts/07_boolean_pipeline_native/` is a self-contained package:

```moss
fn has_large_shift(energies: Vector[Int]):
  return energies |> filter(_ >= 100) |> any(_ > 0)
```

`moss check` and `margo debug` return success and print `true`; `margo build`
fails with `BUILD_BACKEND_ERROR` because generated Rust contains the invalid
return type `_functional_result:has_large_shift`. This is a confirmed native
lowering defect for this valid checked/interpretive program, at compiler 0.1.0.

## Existing-SWARM overlap

No existing swarm implementation, finding list, or prior swarm status was read
before or during this independent work, as required. Overlap comparison is
therefore intentionally deferred to the coordinator after independent completion.
