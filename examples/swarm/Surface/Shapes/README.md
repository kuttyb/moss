# Shapes — 2-D geometry over Float in Moss v0.1

## Purpose

`Point`, `Circle`, `Rect` and `Polygon` (a `Vector[Point]`), each with `area`,
`perimeter`, `bbox`, `centroid` and `translate`. A structural `Shape` trait plus
generic helpers written once and specialized per concrete collection:
`total_area`, `total_perimeter`, `largest_index`, and a higher-order `apply_all`
that takes a function argument. The program uses Float throughout and needs
sqrt, abs and rounding.

The natural Python version I started from is kept at `tmp/surface_shapes/intent.py`
(scratch, not part of the project).

## Design decisions

- **Methods return new values.** `translate` returns a fresh shape and never
  mutates the receiver. This matches the Python intent and avoids WRITE effects.
- **The mixed collection is `Scene`.** Moss has no runtime trait objects. The
  literal `[Circle(..), Rect(..)]` is rejected with a clear message
  (`heterogeneous or unresolved collection element type`). `Scene` keeps one typed
  vector per kind. `Scene.largest()` returns a `Pick(kind, index, area)` record
  instead of the shape itself.
- **Generic helpers use untyped parameters and `for`.** An untyped Vector parameter
  cannot be indexed, and pipelines over it either fail inference or crash the
  checker (T4/T5). A `for` loop over an untyped parameter works natively when the
  argument is a local in `main` or in a test.
- **`Scene` methods use typed pipelines** (`circles |> map(_.area()) |> sum`)
  rather than calling the generic helpers on `self` fields, because that call
  fails native lowering (T6).
- **Float helpers are hand-written.** `fabs`, `fmin`, `fmax`, `floor_nonneg` and
  `round_to` exist because only `sqrt` is available and Float→Int conversion does
  not exist. `floor_nonneg` greedily sums descending powers of two.
  `pi()` is a function because top-level constants are not allowed.
- **Polygon code uses `while` plus `get_x()`/`get_y()` accessors.** This works
  around native lowering of `points[i].x` (T3), and it keeps Polygon runnable in
  Fast Debug, which cannot execute `for`.

## Tension log (sorted by cost)

| # | WANTED | HAD TO | WHY | CLASS | COST |
|---|---|---|---|---|---|
| T5 | `fn total_area(shapes): return shapes \|> map(_.area()) \|> sum`, or index `shapes[i]` in a while loop | an untyped `for` loop (native only) | `TYPE_INFERENCE_FAILED` for the placeholder; `value is not an indexable container` for `shapes[i]`; `map(area_of)` with an untyped or trait-typed `area_of` **aborts the checker** (`add_functional_pipeline_ir` assertion) | frontend bug (crash) + missing surface | 4 |
| T6 | `Scene.total_area()` calling `total_area(circles) + total_area(rects) + ...` | typed pipelines per field in `Scene` | an untyped `for` param is inferred WRITE (`moss ownership`). A second specialization lowers to `&mut Vec` → rustc E0596 on a `&self` field. A READ-typed free function passing `sc.squares` fails the same way. `check` accepts all of these. | native-lowering bug (plus over-conservative WRITE inference) | 4 |
| T2 | `abs(x)`, `round(x, 2)`, `floor`, `min`/`max`, `float(n)` | hand-written `fabs`, `fmin`, `fmax`, `floor_nonneg`, `round_to` | `UNKNOWN_SYMBOL_OR_TYPE`. Only `sqrt(x)` exists, and neither `source_surface` nor the Gentle Introduction lists it. `%` is Int-only. | missing surface + docs gap | 4 |
| T3 | `points[i].x` in a loop | `points[i].get_x()` accessor methods | native: `[Point] cannot be indexed by i64`. `points[0].x` inside a method → `f64 cannot be dereferenced`. check and interp are both fine. | native-lowering bug | 3 |
| T4 | `apply_all(polygons, shift_polygon)` where `Polygon.translate` builds a Vector with `push` | an explicit while loop in `Scene.shifted` for polygons | the checker aborts with the same assertion when a map stage transitively pushes into a local Vector or contains `for` | frontend bug (crash) | 3 |
| T7 | `shapes = [Circle(..), Rect(..), Polygon(..)]` or `Vector[Shape]()` | `Scene` with three typed vectors | the literal is rejected clearly (intended). `Vector[Shape]()` + `push` of different types **passes check and runs in interp** but native fails (`cannot find type Shape`). A `Vector[Shape]` param rejects `Vector[Circle]` (intended). | intended rule + frontend bug | 2 |
| T9 | run the whole program and all tests in Fast Debug | a scratch harness; 5/8 tests run in Fast Debug | interp does not run `for`. A function passed as an argument fails (`unknown local 'shift_circle'`). A map placeholder inside a method fails (`missing checked map output type`). Test blocks are not runnable (SWARM-039). `--trace` emits **no events** when the run later hits an interpreter error. | Fast Debug gap | 2 |
| T10 | `assertEqual(Point(..).dist(Point(..)), 5.0)` | bind the receiver to a local, then call | `TEST_ASSERTION_CONFIGURATION_ERROR: operands have types 'Point' and 'float'`. Binding `d = Point(..).dist(..)` in the test still types `d` as Point. Fine in `main`. | frontend bug | 2 |
| T14 | trust `moss check` to reject `x.abs()` / unknown names | probe with interp and native as well | check accepts `echo zzz`, `x.abs()` and `x.frobnicate()` on a Float. interp fails at runtime and native fails in rustc. It misled my first math probe. | frontend bug | 2 |
| T1 | `PI = 3.14159...` at top level | `fn pi() -> Float` | `expected 'module', 'import', ... or 'proc main()'` (the message mentions `proc main()`, which appears nowhere in the docs) | intended rule / docs gap | 1 |
| T11 | `return (circles \|> count) + (rects \|> count) + ...` | three locals, then sum | fmt: `FORMAT_PARSE_ERROR unknown local function 'return'`. Native: rustc `unused_parens`. Matches SWARM-031/032. | formatter bug + native-lowering bug | 1 |
| T12 | `p = points[i]; q = points[j]` then `p.x * q.y` | inline indexed reads | `OWNERSHIP_USE_AFTER_CONSUME: value 'self' was transferred` (binding an element moves it out) | intended rule (diagnostic points at `self`, no fix) | 1 |
| T13 | `n * 1.5` or `float(n)` for averages | avoided it (the shoelace centroid needs no count) | check accepts Int*Float and interp prints 4.5; native: `cannot multiply i64 by {float}`. There is no conversion builtin. | frontend bug + missing surface | 1 |
| T15 | `sqrt(2.0)` on a literal | `sqrt` only on typed Float values | native: `ambiguous numeric type {float}` | native-lowering bug | 0 |
| T16 | `largest(shapes)` returning the shape | returns an index / `Pick` | I assumed that moving an element out of a borrowed vector is not allowed (T12). I never checked this directly. | agent choice (unverified) | 0 |

## Defect reproducers

All saved in `tmp/surface_shapes/`. The check/fmt/interp/native matrix was produced by
`tmp/surface_shapes/matrix.sh <file>`.

| Reproducer | check | fmt | interp | native | SWARM |
|---|---|---|---|---|---|
| `repro_pipeline_untyped_method.moss` | CRASH (assert) | CRASH | CRASH | CRASH | new |
| `repro_pipeline_push_stage.moss`, `repro_pipeline_for_stage.moss` | CRASH | CRASH | CRASH | CRASH | new (same assertion) |
| `repro_untyped_for_write4.moss` (+5,6,7) | ok | ok | for unsupported | E0596 &mut on &self | new |
| `repro_index_field.moss` (`ps[i].x`) | ok | ok | 2 | E0277 index by i64 | new (cf. fixed SWARM-027) |
| `repro_field_vec_literal_index.moss` (`pts[0].x` in method) | ok | ok | 1.5 | E0614 deref f64 | new |
| `repro_trait_vector.moss` (`Vector[Shape]()`) | ok | ok | 2 | E0425 no type Shape | new |
| `repro_ctor_method.moss` | TEST_ASSERTION_CONFIGURATION_ERROR | same error | same | same | new |
| `repro_unknown_ident.moss` | ok | ok | unknown local | E0425 | new (related SWARM-036) |
| `repro_int_float_mix.moss` | ok | ok | 4.5 | E0277 i64*float | new |
| `repro_sqrt_literal.moss` | ok | ok | 1.414… | E0689 ambiguous float | new |
| `repro_fmt_return_paren.moss` | ok | FORMAT_PARSE_ERROR | 3 | unused_parens | SWARM-031 + SWARM-032 |
| `repro_interp_fn_arg.moss` | ok | ok | unknown local 'twice' | 2 | new (Fast Debug) |
| `repro_interp_map_in_method.moss` | ok | ok | missing checked map output type | 2 | new (Fast Debug) |
| `repro_trace_lost_on_error.moss` | ok | ok | prints 3, then error; `--trace` emits 0 events | ok | new (Fast Debug) |
| `repro_top_const.moss` | parse error | parse error | parse error | parse error | intended |

## Validation results (final source)

Run from this directory. The log is in `tmp/surface_shapes/validation.log`.

- `moss fmt`: exit 0; `moss fmt --check` exit 0 (idempotent).
- `moss check src/main.moss --json`: `ok: true`.
- `margo test`: **8 passed, 0 failed** (native).
- `margo build`: exit 0.
- `margo run`: all 15 output lines, matching hand calculations. The scene total is
  1.25π + 6 + 6 + 1 = 16.93.
- `moss run --interp src/main.moss` / `margo debug`: the first 8 lines match native.
  The run then stops at `Scene.shifted` (`missing checked map output type`), which
  is a Fast Debug gap.
- Fast Debug tests via the scratch harness (`tmp/surface_shapes/it_N.moss`,
  test bodies rewritten as functions): **5/8 pass**. The other 3 are unsupported:
  `for`, a function passed as an argument, and a map placeholder in a method.
- `--trace`: `moss run --interp --trace tmp/surface_shapes/it_3.moss` gave 544
  NDJSON events (LocalRead 330, LocalWrite 53, MethodEnter/Exit 45, Return 49,
  LoopIteration 12, …). A trace of full `main` produced no events, because the
  run aborts later.

Other examples under `examples/` were not browsed. After the first passing
`moss check`, I read only `examples/swarm/FINDINGS.md`, to match SWARM IDs.
