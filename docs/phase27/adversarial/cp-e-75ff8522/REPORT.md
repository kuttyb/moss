# CP-E blind review — frozen source 75ff8522

Source under review: \`75ff852287f9f845af89175607b512af38f8fdc0\`  
Comparison baseline: \`04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4\`  
Review checkout: \`review/cp-e-75ff8522\`  
Scope: Phase 27.3 checker and Rust-emitter expression-tree migration.

## Verdict

GO

No checker/emitter expression-text reader or expression-shape decision remained in the audited source. No off/shadow verdict or diagnostic mismatch was found in the 42-file Phase 27 EST corpus, checked-declaration closure, focused rejected-source corpus, or package checks.

## Reader and taint audit

The Phase 27.3 inventory counts 454 checker and 124 emitter legacy reader sites at baseline. The widened lint reports zero remaining legacy reader calls for both consumers. \`tree_expression_text(...)\` has zero call sites anywhere under \`src/\`.

All 42 remaining literal comparisons on statement fields are the enumerated NAME/keyword cases from \`27_3_STRING_READER_EXEMPTIONS.md\` (collection and executor method names, effect keywords, builtins, FileIO methods, \`pass\`, and \`_\`). The nine dynamic checker \`.a\` comparisons are binding-name slots. There are no unclassified raw literal comparisons, expression-shape predicates, or extra expression-text helpers. \`reader-inventory.json\` lists the individual NAME sites, counts, and the residual calls that construct temporary EST views for tree projections.

The bounded copy/helper trace followed expression-bearing \`Stmt.a\`, \`.b\`, \`.text\`, and argument strings through checker caches/projection helpers and emitter expression lowering. Where a consumer needs expression structure, it calls an EST projection or lowers the expression tree; no traced path classifies expression shape using raw spelling. The remaining temporary-view adapters preserve a tree projection interface for copied, trimmed, or synthetic text. No \`tree_expression_text\` wrapper is used.

The three type-syntax \`split_top_level\` exemptions were tested fail-closed in memory: removing each named function was rejected, and adding a second reader site to each function was rejected. Evidence is in \`exemption-fail-closed.json\`.

## Behavioral coverage

- All 42 \`.moss\` sources under \`tests/phase27/est/\`, including every repro, were checked in both \`MOSS_EST=off\` and \`MOSS_EST=shadow\`. All accepted/rejected verdicts and structured diagnostics matched; see \`off-shadow-corpus.json\`.
- For all 13 accepted corpus files, \`MOSS_EST_CONTRACT_TEST=1\` passed in both modes (26 runs), exercising checked expression-root closure; see \`checked-closure.json\`.
- Focused EST checks passed: retained syntax dumps and IDs, side facts, mismatch injection, pipeline structural cases, raw assignment-place roots, source staleness guard, and rejected-source diagnostic controls.
- Package checks passed in both modes: a valid package accepted; the invalid arithmetic package and open-collection package rejected with their expected diagnostic messages. Raw results are in \`package-valid-oracle.json\` and \`package-oracles.json\`.
- \`.mossi\` imported declaration parity passed for library and app contexts. The three-engine gate passed with identical stdout. Pipeline functional-tree and deferred-return checks passed. Accepts-vs-rustc passed for 236 standalone roots and completed its native \`margo test\`.

## Gate limitations

The scrubbed checkout intentionally lacked \`docs/phase27/STRING_DEBT_BASELINE.md\`, so the stock widened-lint invocation could not load that file. I generated an ephemeral baseline under \`tmp/\` from the explicitly supplied baseline commit using the lint module, then ran its checker and emitter \`--require-zero\` paths; both returned zero and reported the expected 42 NAME and nine binding comparisons. The ephemeral baseline is not committed.

The focused EST harness referenced a scrubbed-away package fixture from an earlier adversarial directory. I ran the harness unchanged in memory except for redirecting that one path to the equivalent new incomplete-expression package fixture committed with this evidence. The remaining harness and package-open-collection oracle ran as authored. No source under \`src/\` was modified.

## Evidence

Raw artifacts are under \`tests/phase27/adversarial/cp-e-75ff8522/\`: paired corpus payloads, checked-closure results, reader inventory, exemption mutation results, and package oracle inputs/results. This review report is the only document under the matching \`docs/phase27/adversarial/cp-e-75ff8522/\` directory.

