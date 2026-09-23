# Validation Engine Dogfood Report

## Intent and first attempt

This package models a small validation/normalization engine. `Rule` owns a keyed numeric value and allowed range; `Evaluation` contains a normalized score, penalty, and acceptance decision. The demo builds a `Map[String, Int]` of per-key limits, evaluates a rule through projections and nested conditionals, maps a rule collection to contributions, and filters accepted Boolean values before counting them.

The first draft used natural multiline constructors and vectors, direct nested arithmetic such as `(value - minimum) * 100 / (maximum - minimum)`, untyped reusable helpers, and a named pipeline callback. It was checked before any inspection of the other swarm implementations. Its first compiler result was `MOSS_COMPILE_ERROR: indentation jumps more than one block level` at the first indented constructor field. The untouched initial program and exact structured result are in `failed_attempts/01_multiline_constructor.*`.

## Iterations and classification

Seventeen evidence-preserving iterations were recorded.

| Category | Attempts | Result |
| --- | ---: | --- |
| Multiline parser surface | 01, 02, 15 | Indented constructor/vector literals fail with `indentation jumps more than one block level`; one-line literals work. Test combined-unit provenance pointed at `src/main.moss` for a test failure. |
| Type/callback inference | 03–08 | Annotating concrete `Rule`, `Vector[Rule]`, `Map[String, Int]`, and return boundaries resolved inference. A typed named callback used as `map(callback)` still received `_generic:rule`; a placeholder method call worked. |
| Intended ownership | 09–12 | Reusing a value-consuming method receiver and repeatedly extracting non-Copy Rules by index correctly triggered `OWNERSHIP_USE_AFTER_CONSUME`. The final code consumes each Rule once and constructs separate rule values for distinct ownership paths. |
| Native lowering | 13 | Checked parenthesized divisors emitted Rust with redundant parentheses and failed under `-D warnings`; binding `span` and `delta` locally avoided it. |
| Formatter/native disagreement | 16–17 | `moss fmt` rejected valid `return (...)` arithmetic, then its accepted rewrite `return(...) * n` lowered as `self.return(...)` in Rust. The unused method containing that form was removed; equivalent evaluation logic remains exercised. |
| Test maintenance | 14 | The tests initially retained the pre-rewrite `evaluate(rule, limits)` signature; this was local fixture drift, not a compiler issue. |

Every workaround has its preceding natural failed source fragment and tool result in `failed_attempts/`. The structured checker/type/ownership follow-ups were used before changing the affected shape. The only ownership rewrites are explicit, intended moves; they are not reported as language gaps.

## Successfully exercised expression surface

- Predicate composition: `value >= minimum and value <= maximum`, nested range branches, and acceptance against a looked-up limit.
- Arithmetic: range spans, deltas, multiplication/division normalization, and below/above-range penalties.
- Calls and projections: `rule.evaluate_at(...)`, `rule.contribution()`, `first.accepted`, and `first.key`.
- Collections: `limits["latency"]`, `limits.get("latency", 0)`, vector indexing, `map(_.contribution()) |> sum`, and `[Bool, Bool, Bool] |> filter(_) |> count`.
- Nested constructor arguments: `Evaluation(normalized: delta * 100 / span, ...)`.

The surprising but correct rewrite was ownership-oriented: a `Vector[Rule]` cannot be indexed repeatedly when extraction moves an owned Rule, so validation accepts independently owned Rules while score calculation consumes a dedicated vector. The surprising non-semantic rewrites were local `span`/`delta` bindings to avoid the native and formatter bugs.

## Tool agreement and final validation

All final commands ran from this package directory using the repository-local tools.

| Command | Result |
| --- | --- |
| `moss fmt --json`, then `moss fmt --check --json` | Canonical and idempotent after the workaround. |
| `moss check src/main.moss --json` | Pass, no diagnostics. |
| `margo build --json` | Pass. |
| `margo run` | `rule latency normalized 68 accepted true`; `health 192 accepted 2`. |
| `margo test --json` | Pass: 3/3 tests. |
| `margo debug` | Same demo output as native run. |
| `margo debug --trace` | Pass; trace contains method entry/exit, branches, field/local reads, pipeline callback calls, and final count `2`. |
| `moss type/calls/effects/why health_score --source src/main.moss --json` | `health_score: Int`; static call to `Rule.contribution`; `rules` inferred `READ`; no domain/message/I/O effects. |
| `moss inspect main --source src/main.moss --json` | Resolved calls to `evaluate`, `health_score`, and `accepted_count`; closed empty domain graph as expected for this pure program. |
| `moss impact health_score --source src/main.moss --json` and `moss test --affected --json` | No semantic change from the recorded baseline; no affected tests selected. Full Margo tests were run separately. |

Diagnostics were strong for explicit ownership and concrete parameter inference, including useful suggested alternatives. The named-callback and untyped-receiver diagnostics were less direct (`_generic:rule` / `_method_value`) and required a minimal application-preserving rewrite. Native diagnostics clearly exposed generated Rust and warning-as-error failure, while the formatter bug was especially misleading because check accepted the same source.

## Suspected findings and post-run overlap

After completing the independent check/native/test/debug run, I inspected `examples/swarm/FINDINGS.md` only for overlap.

- `failed_attempts/13_parenthesized_divisor_native.*` overlaps **SWARM-031** (open): parenthesized arithmetic lowers to redundant Rust parentheses under `-D unused-parens`. This run independently reproduced it in division denominators.
- `failed_attempts/16_formatter_parenthesized_return.*` and `17_formatter_return_grouping_native.*` overlap **SWARM-032** (open): formatter/checker disagreement around `return (` expressions. This run additionally observed that formatter output can native-lower as `self.return(...)`.
- `failed_attempts/01`, `02`, and `15` are a separate candidate for the coordinator to compare: documented-looking multiline constructor/vector literals fail parsing, with the test path also reporting combined-unit provenance poorly. No matching finding was found by the post-run targeted search.

No compiler sources, global tests, existing swarm findings, or coordinator status files were modified.
