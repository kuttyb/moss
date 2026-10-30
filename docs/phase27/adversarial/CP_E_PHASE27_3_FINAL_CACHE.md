# CP-E exhaustive checker review — final-cache candidate

NO-GO

Candidate source: `2bdc2ea16cff57882dcfb45f99b5c2df39d6d98f`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Evidence commit: `3283b5c7a286cc8e0f25e593b1d0a3882c3d7dfb` on `cp-e-phase27-3-final-cache-review`; it contains the unchanged probes, corpus TSVs, closure census, and inventories linked below.

This was a fresh isolated worktree at the frozen candidate SHA. No `src/` files were edited. No compiler performance work was run. `make --jobs="$(nproc)" moss-dev` built both baseline and candidate compilers. Each of the four exact lint gates (`checker`, `emitter`, `reach_walk`, `fast_debug`) returned exit 0, and the candidate reports 40 audited raw NAME comparisons plus nine binding-name comparisons. The checker and emitter widened reader lint reports zero sites, and `tree_expression_text` has zero uses in checker/emitter source. The exhaustive field and bounded taint audits below show that the lint still misses semantic readers after expression text is copied into helpers and locals.

## Exhaustive field inventory and lint audit

`field_inventory.tsv` lists 1,153 checker/emitter `Stmt` field-use occurrences across `.a`, `.b`, `.text`, `.args`, and `.result_expression`: 1,062 other/transport/metadata uses, 50 occurrences participating in NAME/binding comparisons, two structural AST-equality operands, and 39 arguments to EST expression queries. The raw-comparison lint has 40 literal NAME/keyword comparisons and nine dynamic binding-name comparisons; all match the documented, function/API-keyed audit in `27_3_STRING_READER_EXEMPTIONS.md`. These are method, handler, keyword, collection-operation, assertion, `pass`, or binding names. No expression-shaped comparison remains in that set.

The direct-field shape rules found no punctuation search, first-character dispatch, or trimmed-prefix test. The bounded taint audit in `indirect_taint_audit.tsv` follows expression-bearing slots into helpers and locals and found remaining semantic reads:

- The checker scans EST-projected child spellings for digits, identifier-looking character runs, dots, and brackets when proving loop bounds, mutation roots, and Range invalidation. These checks decide divergence and static bounds facts.
- File-chunk lowering calls that root scanner on assignment places and `replace` arguments to decide whether a mapper mutates its Range parameter.
- The emitter uses trimmed whole-expression spellings as keys in domain-instance, type, borrowed-parameter, view-parameter, and object-type maps. Those checks affect domain-capability propagation and clone/view lowering.
- The checker uses a trimmed return expression as a `local_sources` key to propagate result constraints, and FileIO range-origin tracking uses trimmed expression spellings as binding keys.
- Pattern projections also remain partially bypassed: checker and emitter code still selects enum cases, recovery types, and bindings from raw `arm.a`/`arm.args` even where an EST Pattern node exists. These are pattern-node migrations, not exemptions.

Every item in that table is a **MIGRATION BUG**. The checker/emitter lint result is therefore not a valid zero-reader proof.

## Corpus and diagnostics

The baseline corpus has 2,118 tracked Moss sources: 1,394 accepted and 724 rejected. Candidate has 2,141 sources: 1,406 accepted and 735 rejected. The 23 candidate-only sources are committed adversarial probes already present at the frozen SHA (12 accepted, 11 rejected). On the 2,118 common paths, there are three diagnostic deltas:

| Source | Baseline | Candidate off and shadow | Classification |
| --- | --- | --- | --- |
| `tests/negative/functional_placeholder_noncopy_field.moss` | `FUNCTIONAL_SEMANTIC_ERROR:9` — placeholder map aliases nontrivial source storage | `ECHO_DISPLAY_UNSUPPORTED:10` | **MIGRATION BUG**; `tests/tooling/check_phase25_sb_check_display.py` explicitly pins the earlier functional diagnostic. |
| `tests/negative/functional_placeholder_noncopy_identity.moss` | `FUNCTIONAL_SEMANTIC_ERROR:6` — placeholder map aliases nontrivial source storage | `ECHO_DISPLAY_UNSUPPORTED:7` | **MIGRATION BUG**; the same harness pins the earlier functional diagnostic. |
| `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss` | `RECOVERY_PATTERN_REQUIRES_VARIANT:57` | `UNKNOWN_ERROR_TYPE:57` for unavailable `ndarray.NdArrayError` interface | **UNEXPLAINED** in this isolated standalone corpus. The same source rejects on both paths, but this review did not execute the source-free package oracle, so the diagnostic delta blocks a GO verdict. |

Candidate shadow versus candidate off has zero verdict/diagnostic changes across all 2,141 frozen-candidate sources. Full TSV outputs are committed as `corpus-baseline.tsv`, `corpus-candidate-off.tsv`, and `corpus-candidate-shadow.tsv`. `rejected_sources.tsv` enumerates every baseline/candidate rejected path and its diagnostic key; accepted declaration closure excludes those sources.

## Checked-declaration closure

The corpus check accepts 1,394 common paths in both baseline and candidate. For each accepted source context, `moss symbols --source ... --json` enumerated source declarations; all 1,394 common context inventories are equivalent (zero changed declaration contexts). The baseline census reports 8,125 source declarations, and candidate reports 8,173; the 48 additional declarations are in the 12 candidate-only accepted probe files.

Eleven source contexts per revision were excluded from the declaration census because `symbols` switches to the project-aware `src`/`tests` context while `corpus_verdicts.py` checks each physical file standalone. Those 11 queries fail with the same diagnostic on both revisions and are enumerated, with codes, in `closure-summary.json`. All corpus paths rejected by `moss check` are separately enumerated in `rejected_sources.tsv`.

The independent accepted closure probe verifies the declaration boundary directly: `symbols` returns `main`, `reachable`, and an unreachable helper on baseline, candidate off, and candidate shadow; `inspect` reports `checked_declaration` for `main` and the unreachable helper; `calls main` reaches only `reachable`. The unreachable-invalid probe rejects with identical `UNKNOWN_SYMBOL_OR_TYPE:2` in all three paths. The incomplete-expression probe rejects with identical `MOSS_COMPILE_ERROR:2` in all three paths. Probe sources, exact JSON results, and the rerunnable script are under `tests/phase27/adversarial/cp-e-phase27-3-final-cache/`.

## Verdict

NO-GO. The bounded taint audit finds checker/emitter string readers after local/helper propagation, pattern-node consumers still read pattern spelling, and one corpus diagnostic delta remains UNEXPLAINED. Fix the migration readers and rerun CP-E at a new frozen candidate before CP-F or phase gates.
