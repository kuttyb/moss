# CP-E Phase 27.3 independent final blind review

**Verdict: NO-GO**

Candidate source freeze: `2fac00c821dd7c7f7f9c2eafea1349c64685bd53`
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`
Review branch: `review/cp-e-27-3-final-blind`

## Blocking finding: shadow mode drops a checked method declaration

The repository’s checked-declaration closure invariant is that EST checked declarations must contain every declaration checked by the legacy path. That invariant fails for `method:heap__Entry.key_of@9`.

The declaration is at `examples/swarm/modules/algo_chain/heapkit/src/heap.moss:9`, and the exported wrapper calls it at line 101. On the same candidate source, candidate `MOSS_EST=off` records the method, while candidate `MOSS_EST=shadow` omits it. The comparison was repeated from both the ordinary heapkit `src/order.moss` and `tests/order_test.moss` contexts, and the natural-chain repro’s corresponding `src/order.moss` and `tests/order_test.moss` contexts: four legacy-only checked declaration records, all for this method.

Both modes return the same `INVALID_BARE_IDENTIFIER_STATEMENT` at `heap.moss:35` (`break`). The diagnostic remains stable, but that does not satisfy the closure gate: the shadow checker omits a declaration the off checker checks. The full filtered differential found no tree-only checked declarations. Edge labels changed in 160 source entries; all 1,545 removed/added edge pairs match after normalizing module qualification, so those label changes do not explain the missing method.

This violates the standing closure invariant and blocks a GO verdict. The focused trace and all differential rows are in [checked-declaration-closure.json](evidence/checked-declaration-closure.json).

## Other findings and controls

- **Checker/emitter reader lint:** zero expression-text reader call sites remain in checker and emitter consumers, including the compiled `.inc` consumer set. The recursive local include audit found only the three keyed `split_top_level` type-syntax exemptions; emitter has no remaining reader calls. Raw `Stmt` name comparisons and nine checker binding-name comparisons exactly match the keyed audit, with no raw shape scans or `tree_expression_text` reads. Evidence: [reader-audit.json](evidence/reader-audit.json).
- **Full corpus diagnostics:** 16,468 checks covered the baseline’s 2,048 and candidate’s 2,069 tracked non-adversarial `.moss` paths, each with baseline/candidate compiler and `MOSS_EST=off`/`shadow`. All returned JSON. Off/shadow diagnostics were identical for each compiler. One baseline/candidate diagnostic refinement occurred in `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`: the old reader reported `RECOVERY_PATTERN_REQUIRES_VARIANT`; EST retains the full qualified enum name and reports `UNKNOWN_ERROR_TYPE` when the provider is absent. The source remains rejected. The package-aware source-free oracle below succeeds with provider artifacts present. Full results: [corpus-diagnostics.jsonl](evidence/corpus-diagnostics.jsonl), summary and exact delta: [corpus-summary.json](evidence/corpus-summary.json).
- **Phase 27.1 rejected-source sweep:** 39 allowed source/probe cases produced 156 baseline/candidate × off/shadow checks. All matched the exact expected diagnostics, including numeric-member fixtures, pipeline-precedence controls, rejected fragments, and generated `||`/trailing-operator probes. No adversarial fixture was read or run. Evidence: [phase27-1-rejected-sweep.json](evidence/phase27-1-rejected-sweep.json).
- **Source-free package oracle:** `sdk/advanced-data-acceptance/check_source_free.py` passed in all four baseline/candidate × off/shadow combinations. It built 14 provider interfaces, checked and built the consumer without provider source visibility, and ran the executable successfully. Evidence: [source-free-package-oracle.json](evidence/source-free-package-oracle.json).
- **Imported `.mossi` EST oracle:** `tests/tooling/check_phase27_est_mossi.py` passed on baseline and candidate. It verified equal raw `.mossi` bytes between off/shadow for each compiler and observed EST dumps for imported generic and Section 2 execution bodies. Evidence: [mossi-est-oracle.json](evidence/mossi-est-oracle.json).
- **Package-open-collection rejection:** Margo rejected the normal `package_open_collection` fixture with `MOSS_COMPILE_ERROR` and the expected unresolved collection message in all four compiler/mode combinations. Evidence: [package-open-collection.json](evidence/package-open-collection.json).

The bootstrap contract is `moss-0.1`; both compilers built successfully. No performance runs were made. No source files were modified. Corpus and closure scans excluded every tracked path with an `adversarial` path component; the counts excluded are recorded in the corpus summary.

## Evidence bundle

The `evidence/` directory contains the sanitized bootstrap contract, source-reader audit, complete corpus diagnostics, checked-declaration closure differential, package oracles, and rejected-source sweep. Compiler SHA-256 identities are included in the corpus summary.
