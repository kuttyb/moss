# Normative review of reviewer-controlled holdouts

Holdout source SHA: clean R7-008 compiler `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`.

**Oracle-spec verdict: GO. Holdout integrity: PASS.** The expected results were recorded before execution and are supported by the approved contract. The two observed R7-008 failures remain implementation findings; they do not change the expected oracle results.

## Contract assessment

1. `01_primitive_owner.moss`: ACCEPT is normatively correct for a primitive Copy element passed to `Int -> Int`, followed by another read and a Vector loop that pushes the primitive scalar. The R7-009 requested contract and reference patch explicitly classify concrete primitive indexed and Vector iteration element consumes as scalar reads. The paired `01b_nonprimitive_control.moss` expectation is the opposite for nonprimitive elements and correctly expects `OWNERSHIP_COLLECTION_ELEMENT_CONSUME` under the Phase 28 proposal §5.3 and frozen ORACLE-03.
2. `02_map_get_write_through.moss`: ACCEPT with output `new\nnew` is normatively correct. The frozen R7-005 addendum defines `map.get(key)` as borrowing `V`; the Phase 28 per-definition rule permits a WRITE borrow and says writes through a borrowed alias mutate the owner. The request for a Map.get write-through fixture confirms this contract application. The observed `new\nold` is an R7-008 behavior defect, not an oracle reason to change expectations.
3. `03_specialization_collision.moss`: ACCEPT with `41\nready` is normatively correct. The same generic callable is applied to two distinct concrete record specializations; checker facts must remain exact-context and exact-specialization, and neither returned value may acquire the other type's facts.
4. `04_shadowed_function_name.moss`: ACCEPT with `99\n7\n7\n7` is normatively correct. A loop-local and branch-local shadow must not replace the sibling function's binding after scope exit; this is the established lexical binding behavior used in prior Phase 15 conformance coverage.
5. The supplementary primitive indexed-only probe is also ACCEPT with `8\n8\n9`; it isolates scalar indexed behavior from the Vector iteration failure.

## Implementation findings kept separate

- R7-008 rejects primitive Vector loop-element use at the consuming `copied.push(value)` with `OWNERSHIP_VECTOR_ELEMENT_CONSUME`; expected behavior is acceptance under the R7-009 primitive Copy contract. Repeated primitive indexed use passes.
- R7-008 accepts the Map.get write-through source but leaves the stored value unchanged (`new\nold`) in Fast Debug, native debug, and native `-O`; expected behavior is `new\nnew`.
- The nonprimitive rejection control passes, as do distinct specialization and shadowed-name checks across check, Fast Debug, native debug, and native `-O`.

No expected data, compiler output, or checked-in Team B file was changed. Full invocation details and source digests remain in `final_summary.json` and `FINAL_REPORT.md`.
