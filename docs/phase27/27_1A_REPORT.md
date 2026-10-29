# Phase 27.1a report — stopped after item B

## 1. Corpus count

The prior statement “2,061 of 2,064” is based on a wrong denominator for the requested baseline. At `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`, there are 2,061 tracked `.moss` files, and the off/shadow corpus TSVs contain each one exactly once. No source was omitted; therefore there are no three omitted paths to name. Compared with the separate 2,064-source tree at `d109b8f1`, the required tree has 7 fewer paths and 4 additions (net −3), rather than three omissions:

- Older-only: `tests/negative/swarm_013_immutable_let_{direct,index,member,receiver}.moss`, `tests/phase45_mutable_binding_materializes.moss`, `tests/tooling/fixtures/builtin_generic_typing/map_delete_immutable.moss`, `tests/tooling/fixtures/moss_language_surface_immutable_let.moss`.
- Required-tree-only: `sdk/cbor/tests/support/support.moss`, `tests/phase45_single_assignment_var_virtualizes.moss`, `tests/phase45_var_mutation_materializes.moss`, `tests/phase45_var_reassignment_materializes.moss`.

Evidence: `tmp/phase27-a-baseline.txt`, `tmp/phase27-a-old.txt`, `tmp/phase27-a-current.txt`, `tmp/phase27-a-ran.txt`.

## 2. Shadow failure triage

| Form | Label | Count | Reproducer |
|---|---|---:|---|
| Pipelines | DEFERRED-27.1b | 9,919 | `tests/phase27/est/repro/pipeline.moss` |
| Collection literals | DEFERRED-27.1b | 7,447 | `tests/phase27/est/repro/collection_literal.moss` |
| Collection constructors | DEFERRED-27.1b | 30 | `tests/phase27/est/repro/collection_constructor.moss` |
| Record/enum constructors and argument fragments | DEFERRED-27.1b | 22,712 | `tests/phase27/est/repro/constructor.moss` |
| Message expressions | NEEDS BANK | 1,645 | `tests/phase27/est/repro/message_expression.moss` |
| Bitwise and shift operators | NEEDS BANK | 402 | `tests/phase27/est/repro/bitwise_shift.moss` |
| Unsupported `**` and `in` | NEEDS BANK | 4 | `tests/phase27/est/repro/unsupported_exponent.moss`; `tests/phase27/est/repro/membership_operator.moss` |
| Symbolic `&&` and `||` | NEEDS BANK | 4 | `tests/phase27/est/repro/symbolic_boolean.moss` |
| Incomplete expression | NEEDS BANK | 2 | `tests/phase27/est/repro/incomplete_expression.moss` |
| Standalone multiline fragments | NEEDS BANK | 7 | `tests/phase27/est/repro/split_fragment.moss` |
| Numeric literal member access | EST BUG | 4 | `tests/phase27/est/repro/numeric_literal_member.moss` |

No `.mossi`-loaded expression failure was observed; the runner enumerates tracked `.moss` sources. Group details, slot counts, language syntax references, and string-path outcomes are in `docs/phase27/EST_DIVERGENCES.md`.

## 3. Per-slot classified counts

These are computed from the raw shadow trace, not the C counter implementation, which was not started.

| Slot | Built | Deferred 27.1b | Remaining failures |
|---|---:|---:|---:|
| Condition | 32,664 | 685 | 25 |
| Initializer | 20,370 | 2,940 | 80 |
| ReturnValue | 47,840 | 7,849 | 204 |
| Iterable | 1,190 | 50 | 0 |
| Receiver | 48,494 | 1 | 0 |
| Argument | 82,077 | 4,811 | 1,585 |
| LhsPlace | 74,281 | 34 | 0 |
| RhsValue | 52,206 | 21,940 | 169 |
| Scrutinee | 284 | 4 | 0 |
| Pattern | 0 | 0 | 0 |
| RaisedValue | 1,808 | 1,775 | 0 |
| Expression | 46 | 19 | 5 |

## 4. Items D–F

Not started. The stop rule fired after item B produced NEEDS BANK groups. No D–F behavior or tests are claimed.

## 5. String-debt baseline

Not measured for the required `4d6d0c8` baseline; item G was not started. Existing baseline data elsewhere in the worktree is not substituted.

## 6. Gates G1–G9

All not run. Work stopped before C under the explicit NEEDS BANK stop rule; no gate failure or pass is claimed. There are no gate logs. Required baseline hash: `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`.

- G1 `make check`, off: NOT RUN. Log: none (not run).
- G2 `make check`, shadow: NOT RUN. Log: none (not run).
- G3 corpus off vs baseline binary: NOT RUN. Log: none (not run).
- G4 corpus off vs shadow: NOT RUN. Log: none (not run).
- G5 generated Rust parity: NOT RUN. Log: none (not run).
- G6 Fast Debug parity: NOT RUN. Log: none (not run).
- G7 `.mossi` Section 1/2 parity: NOT RUN. Log: none (not run).
- G8 compiler-perf, off: NOT RUN. Log: none (not run).
- G9 compiler-perf, shadow: NOT RUN. Log: none (not run).

## 7. Branch head and CP-A

The final branch head is the commit containing this report. It is not an authorized CP-A start; CP-A has not begun. The branch head SHA is reported in the session closeout.

## 8. Decisions

**Self-review:** item A's denominator was checked against both Git trees and exact TSV membership. Item B used one fresh 2,061-source shadow run; categorized totals reconcile to 42,176 raw failed constructions. Deferred groups were not counted as built. The NEEDS BANK stop rule was applied before C. No splitter, consumer, language semantic, ABI, or `.mossi` behavior was changed. The only implementation addition is opt-in failure tracing under `src/est`; reproducers and evidence are under repository-local paths.
