# Expression-surface Moss dogfood swarm — coordinator report

Date: 2026-09-23

## Method

Three agents independently built complete, isolated Moss packages before reading
other swarm implementations or the findings ledger:

| Workload | Final validation | Development evidence |
| --- | --- | --- |
| [Parser/evaluator](parser_evaluator/) | formatter check; checker; native build/run/test (3/3); Fast Debug and trace all pass | 7 preserved failed/repro attempts |
| [Numerical tool](numerical_tool/) | formatter check; checker; native build/run/test (5/5); Fast Debug and trace all pass | 8 preserved attempts, including a native parity repro |
| [Validation engine](validation_engine/) | formatter check; checker; native build/run/test (3/3); Fast Debug and trace all pass | 17 preserved attempts |

All agents used the fresh Moss workflow: repository guidance, both local skills,
`moss agent bootstrap --json` (confirmed `moss-0.1`), the practical guide,
structured checking, semantic queries, Margo native commands, and Fast Debug.
No compiler source, global tests, language surface, or APIs were modified.

## What composed well

The final programs comfortably combined ordinary arithmetic, Boolean predicates,
nested helper calls, record projections, map indexing/default lookup, vector
indexing/mutation, constructors, branches, loops, and eager scalar pipelines.
All 11 final native tests passed, each native demo matched Fast Debug, and every
final trace completed. The checker, semantic queries, canonical formatter, native
execution, and Fast Debug are therefore strongly usable for the final expression
shapes chosen by each application.

## Repeated and classified observations

| Pattern | Independent evidence | Classification |
| --- | --- | --- |
| Indented multiline vector/constructor literals rejected with `indentation jumps more than one block level` | all 3 workloads | Undocumented/restrictive layout surface with an unhelpful parser diagnostic. Bootstrap documents one-line literals but does not promise multiline literals, so this swarm does not call it a compiler defect. |
| Untyped helpers or named pipeline callbacks over record collections did not infer concrete record context | parser, numerical, validation | Current inference boundary; explicit concrete parameter/return annotations are accepted and diagnostic-led. |
| Filtering/indexing nontrivial record elements requires ownership-aware restructuring | all 3 workloads | Intended ownership rule, not a parity defect. The checker named the consuming operation; final programs used scalar pipelines, one-pass aggregation, or separately owned inputs. |
| Parenthesized arithmetic or `return (...)` | validation | Existing open SWARM-031 native lowering and SWARM-032 formatter disagreement independently reproduced. |
| Unqualified sibling method called inside a type method reached through `map` | parser | Regression/expanded reproducer for fixed SWARM-025, with a nearby functional-pipeline context (SWARM-023). It is not a new ID. |

## New finding: native Boolean pipeline terminal lowering

The numerical application naturally used this predicate:

```moss
fn has_large_shift(energies: Vector[Int]):
  return energies |> filter(_ >= 100) |> any(_ > 0)
```

The complete application exposed it first. Its retained minimal package is
[`07_boolean_pipeline_native`](numerical_tool/failed_attempts/07_boolean_pipeline_native/).
Coordinator revalidation confirmed all three facts:

1. `moss check` succeeds with no diagnostics.
2. Fast Debug succeeds and prints `true`.
3. Native `margo build` fails with `BUILD_BACKEND_ERROR`; generated Rust has the
   invalid return type `_functional_result:has_large_shift`.

This is a distinct, confirmed native-lowering parity defect, not unsupported
Moss syntax. Allocated as **SWARM-065** in the canonical ledger
(`examples/swarm/FINDINGS.md`). The final numerical program uses
`filter |> sum` followed by an ordinary comparison as a bounded workaround.

## Answer to the swarm question

**Not yet without qualification.** Ordinary final-purpose code can compose richly
and run consistently once it is within the documented ownership/inference surface;
the three finished programs provide concrete positive evidence. But programmers
cannot yet trust all five stages to agree: a checker-accepted Boolean pipeline can
fail native lowering, an already-fixed sibling-method case still fails Fast Debug
when reached through `map`, and leading/parenthesized expression formatting/lowering
remains inconsistent. The repeated multiline-literal experience is also an
ergonomic/documentation gap, although not established as a promised language feature.

The individual reports retain every first attempt, exact diagnostics, natural
rewrite, semantic query result, execution result, and minimized repro:

- [Parser/evaluator report](parser_evaluator/REPORT.md)
- [Numerical tool report](numerical_tool/REPORT.md)
- [Validation engine report](validation_engine/REPORT.md)
