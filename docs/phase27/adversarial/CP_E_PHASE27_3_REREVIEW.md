# CP-E independent re-review — Phase 27.3

## Frozen inputs

- Candidate branch: `phase27-3-work`
- Candidate: `ea7bf9f47cca36f5815481a683bbda3d7e766631`
- Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`
- Review branch: `review/cp-e-phase27-3-rereview`
- Live bootstrap: `language_version` is `moss-0.1`.
- No full gates or compiler-perf run was started. No tracked or untracked `src/` file was written by this review.

## Expression-slot inventory and alias audit

The independent locator records 1,066 direct reads of `Stmt`-shaped `.a`, `.b`, `.text`, and `.args` slots across checker/emitter implementation files. It preserves every occurrence with file, line, receiver, source line, and a syntactic classification in `cp-e-phase27-3-rereview-slot-inventory.jsonl`. The companion summary records the classification counts. All direct literal and variable comparisons are separately enumerated in `cp-e-phase27-3-rereview-name-exemptions.json`.

There are 40 literal comparisons. They are NAME/keyword/builtin slots selected by statement kind: checker/emitter method and builtin names, `FileIO`, the `pass` statement sentinel, and `_` as a recovery-pattern marker. There are 9 variable comparisons, each comparing an assignment/name slot with an already selected binding or root name. The per-site list is the authority for exact names, file locations, and source lines.

The bounded alias inventory finds 22 local `string` initializers fed from statement fields. It preserves each initializer and same-file identifier-use locations (overinclusive by design; it does not claim general interprocedural dataflow). The fixed loop-bounds path is specifically:

`Stmt::For.b` → `iter_expr = trim(statement.b)` → `tree_parse_simple_call` / `tree_parse_vector_literal` / `inferred_expr_type` and effect queries. These are EST projections or checked semantic APIs. There is no raw substring classifier in this path. The unchanged `for index in range(lower, upper)` reproducer is accepted by both baseline and candidate.

Exact source search finds zero `tree_expression_text(...)` calls and zero `expression_text(...)` calls in checker/emitter implementation files. The reader linter's remaining 3 checker API matches are `split_top_level` calls on canonical type strings at `semantic_checker.cpp:102`, `:8248`, and `:8368`; none read a `Stmt` expression slot. Its emitter API count is zero. Therefore checker and emitter expression-text readers are both zero.

Synthetic linter checks show function/API exemptions fail closed: a valid exact function/API entry is accepted; missing function, stale/wrong API, duplicate entry, and an exemption whose function gains a second matching read are rejected. Exact outcomes are preserved in `cp-e-phase27-3-rereview-lint-exemption-checks.json`.

### Lint limitations

The repository lint is lexical and allowlist based. Its API matcher only recognizes listed spellings; a new or renamed helper can evade that list. The direct-slot comparison and shape patterns do not propagate aliases through arbitrary assignments, helper parameters, multiline expressions, or computed member access. The linter itself does not prove semantic binding identity. This review added a bounded direct-string-alias scan and source-context inspection; it is not whole-program C++ dataflow analysis. The full occurrence locator is retained so later reviewers can reclassify any syntactic bucket without rescanning the source.

## Verdict and structured-diagnostic parity

The complete current `tests/**/*.moss` set contains 934 files after adding the unchanged reproducers. Baseline and candidate verdicts and complete structured JSON payloads match for **934/934**, with **0 mismatches**. Raw per-source pairs are in `cp-e-phase27-3-rereview-corpus.jsonl`; summary: `cp-e-phase27-3-rereview-corpus-summary.json`.

Seven unchanged adversarial sources were checked on both compilers: for-loop bounds alias, parenthesized enum constructor, malformed expression, mismatched enum pattern field, mismatched recovery field, unreachable invalid function, and a source with invalid reachable and unreachable functions. Each source has identical baseline/candidate JSON. The malformed source rejects with `MOSS_COMPILE_ERROR`; the two enum field mismatches reject with their specific field-name diagnostics; an unreachable invalid function rejects with `UNKNOWN_SYMBOL_OR_TYPE`. The valid parenthesized constructor and for-loop alias are accepted. Raw results are in `cp-e-phase27-3-rereview-probes.json` and reproducers are under `tests/phase27/adversarial/`.

## Package-mode local dependency

On `examples/swarm/modules/data_pipeline/analytics`, whose manifest has a local path dependency on `records`, `margo build --json` succeeds on both baseline and candidate. `margo test --json` returns the same `MODULE_PROVIDER_NOT_FOUND` for imported module `report` on both. This test command is therefore not a green parity gate; the shared fixture limitation and both raw outputs are preserved in `cp-e-phase27-3-rereview-package-parity.json`.

## Checked-declaration closure census

For each of the 934 source files, the census records check status, structured rejection code, compiler-visible symbols when available, and the source-level declaration fallback. On the 537 accepted files, `moss symbols --source ... --json` enumerates declarations; compiler `calls` roots (`main`, tests, benchmarks) classify callable declarations as reachable or unreachable. There are 1,322 reachable and 163 unreachable callable declarations. The checker runs `check_function` over every `p_.functions` entry and `check_domain` over every `p_.domains` entry without a reachability filter. The unreachable-invalid probes confirm an unreachable function body still produces a checker diagnostic.

The other 397 sources reject and are explicitly excluded from the closed checked-program census. Their diagnostic codes and source-level declaration fallback are preserved per file; the summary records the rejection-code distribution and declaration-kind counts. The fallback captures root declarations, nested callable declarations, enum cases, and type/domain fields. No rejected source is silently treated as checked.

- Complete per-file census: `cp-e-phase27-3-rereview-complete-declaration-census.jsonl`
- Counts and rejection distribution: `cp-e-phase27-3-rereview-complete-declaration-summary.json`

Closure status: **complete for all accepted checked sources, with 397 rejected-source exclusions explicitly enumerated**.
