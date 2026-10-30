# CP-E Phase 27.3 review — frozen candidate 4c2242de

**CP-E verdict: NO-GO**

This is the completed review of frozen candidate `4c2242defdb856ba187c893c34443c0dd079b058` (`phase27-3-work`) against baseline `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`. Review branch: `review/cp-e-phase27-3-4c2242de`. The worktree was isolated at `tmp/cpe-4c2242de`; no `src/` files were changed. Candidate dev binary SHA-256: `cf7968540fb727c6921b7f10a360e91bc9a976aa8fddd74a1a450c1113bac078`. Baseline optimized binary SHA-256: `a7014f21f024ffba26801d377474d346eeecfc4c536e604d7e0816d4c7b211f0`.

## Lint and explicit exemptions

`python3 tests/tooling/check_phase27_string_debt.py --require-zero checker` and `... emitter` both pass at the frozen candidate. `tree_expression_text(...)` has zero uses in `src/`; therefore no rendered-tree equality/prefix comparison remains in checker or emitter. The lint prints exactly 40 raw Stmt NAME comparisons and 9 binding comparisons. All 49 sites are covered by the audited per-consumer table in `docs/phase27/27_3_STRING_READER_EXEMPTIONS.md`: the 40 NAME values are method/handler/builtin/effect/keyword names or the assignment placeholder, and the 9 dynamic comparisons are binding names in binding slots. No expression-shape literal comparison is exempted.

There are exactly three actual widened-lint exemptions, each audited here and keyed by function plus API (not line):

- `phase20_is_nontransferable_capability | split_top_level`: canonical type arguments.
- `builtin_equality_type | split_top_level`: canonical type arguments.
- `contains_float_type | split_top_level`: canonical type arguments.

The lint confirms each target function exists and contains exactly one site. There are no stale or line-keyed exemptions.

## Bounded taint audit: remaining semantic readers

The following expression-bearing values reach character-inspecting helpers not counted by the current widened lint. They are MIGRATION BUG findings under L7's taint rule; each should be replaced by EST node/child projections or semantic literal facts. No changes were made in this review.

| Reader/helper | Tainted call path and current decision | Expected structural EST replacement |
| --- | --- | --- |
| `is_single_string_literal` / `is_single_char_literal` (`src/moss.cpp:1049,1061`) | Checker `obvious_expr_type` (`semantic_checker.cpp:10355-10356`) and type/literal fallback sites (`semantic_checker.cpp:27991-27993`); emitter `generated_expr_type` (`native_generator.cpp:1845-1848`) and `expr` (`native_generator.cpp:3467-3469`). Helpers inspect quotes, escapes, and boundaries to decide literal kind. | Root `Literal` with explicit string/char subtype; retain decoded spelling/value as a literal fact.
| `decimal_float_literal` (`src/moss.cpp:144`) | Checker `obvious_expr_type` (`semantic_checker.cpp:10363`) and further inferred-type/literal fallback sites (`semantic_checker.cpp:4441, 27993`); emitter `generated_expr_type` (`native_generator.cpp:1867`) and `expr` (`native_generator.cpp:3222`). It scans sign, digits, decimal point, and exponent. | Root numeric `Literal` subtype/value fact; float/int kind must not be inferred by scanning source text.
| `generated_integer_literal` (`native_generator.cpp:1580`) | `generated_expr_type` (`:1865`), `expr` (`:3222`), generated binary/comparison conversions (`:3303-3311, :3421-3440`). It scans sign/digits; `generated_integer_literal_as_float` then calls `decode_numeric_literal_expression` (`:1591`). | EST numeric `Literal` with decoded integer payload; signed/parenthesized forms via `Unary`/`Parenthesized` child IDs.
| `typed_empty_vector_constructor` (`src/moss.cpp:1029`) | Checker call paths include `semantic_checker.cpp:11703, 20702, 22119, 27488, 27496, 27583`; emitter paths include `native_generator.cpp:1864, 3227`. The helper reparses text with legacy `parse_simple_call`, splits the callee, and checks canonical type spelling/prefix/suffix plus zero args. | Constructor/call node with zero value arguments and a built-in `Vector` type application; type arguments from type syntax nodes/facts.
| `decode_numeric_literal_expression` | Checker literal integer facts (`semantic_checker.cpp:5752, 20726`); emitter integer-to-float literal lowering (`native_generator.cpp:1593`). Both decode expression lexemes after identifying literal shape. | The EST numeric literal node should carry the checked decoded numeric value/kind so these paths consume the fact.
| `proven_safe_integer_divisor` (`semantic_checker.cpp:11382`) | Called for a `Binary.right` fragment at `semantic_checker.cpp:20486`; strips parentheses/signs and scans digit characters to decide whether division/remainder can panic. | Project `Binary.right`; allow only an integer `Literal` under `Parenthesized`/unary-sign nodes, then use its decoded value (exclude zero and -1).
| `is_integer_literal_at_least` (`semantic_checker.cpp:20712`) | Loop progress analysis calls it through `is_positive_integer_literal` / `is_nonnegative_integer_literal` (`:20731-20736`) on EST-derived step expressions. It checks `Literal` shape, then decodes source text. | Same numeric literal value fact as above; tree shape alone is insufficient for the threshold comparison.
| `obvious_expr_type` raw text checks (`semantic_checker.cpp:10351-10363`) | Expression values are compared to `None`, `true`, and `false`, then scanned as signed digits / decimal float before returning semantic types. | Literal node kind/subtype, plus the existing inferred type fact for identifiers.
| `root.find/substr` in observable effects (frozen source `semantic_checker.cpp:20988-20993, 21006`) | `statement.a` is copied to `root`, then `.`/`[` character positions decide the root binding and whether writes mutate domain/parameter state. | `tree_root_identifier(statement.a)` plus EST place/member/index node shape. This specific finding was fixed on a later branch commit after the frozen SHA; it remains a finding for this review SHA.

The audit also followed string values through EST projections (`tree_parse_*`, `tree_identifier_name`, `tree_expressions_equal`) and classified those as structural projections, not character readers. Whitespace-only `trim` before an EST projection is normalization, not a semantic classifier. Diagnostic rendering and generated Rust spelling are output uses, not readers.

## Frozen shadow fragment finding

The candidate shadow corpus aborts at `src/semantic_checker.cpp:10756`, asserting `tree == legacy` in `expression_uses`. Reproducer: `examples/swarm/expression_surface_2026/validation_engine/failed_attempts/09_reused_method_receiver.moss` (tracked, unchanged). Exact expression fragment passed to `expression_uses`: `key: rule.key`; queried name: `rule`.

`MOSS_EST_DUMP=1` on candidate prints owner 2, slot `ReturnValue`, as:

```text
constructor#25 (identifier#8 "Evaluation")
  (named-argument#11 "key" (member#10 (identifier#9 "rule") "key"))
  (named-argument#15 "normalized" ...)
  (named-argument#19 "penalty" ...)
  (named-argument#24 "accepted" ...)
```

Thus the whole source is a constructor call and `rule.key` is the `NamedArgument.value`; the helper receives the incomplete label fragment `key: rule.key`. Immediate call site is `expression_uses(value, parameter.name)` at `semantic_checker.cpp:18266` in `Checker::analyze_effect_expression`'s conservative fallback. Producer chain: `tree_parse_simple_call` yields `call_arguments`; because unresolved `Evaluation` is not known as an object/function, the generic-call fallback recursively sends each raw argument fragment into `analyze_effect_expression`, which reaches the fallback above. This is a MIGRATION BUG in fragment hook placement: recurse on `NamedArgument.value` and preserve the parent cursor association. Candidate OFF and baseline OFF both return `UNKNOWN_SYMBOL_OR_TYPE:13`; candidate SHADOW aborts before returning the diagnostic. The parent reports this producer fix at `11350b2c`, after this frozen SHA.

## Corpus diagnostics and project context

Corpus commands and raw TSV/log artifacts are under `tests/phase27/adversarial/cp-e-phase27-3-4c2242de/`. Baseline OFF, candidate OFF, and candidate SHADOW each checked all 2,146 tracked `.moss` entrypoints (1,409 accepted, 737 rejected); there were zero corpus timeouts.

- Baseline OFF vs candidate OFF: one diagnostic difference, `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss`, `RECOVERY_PATTERN_REQUIRES_VARIANT:57` vs `UNKNOWN_ERROR_TYPE:57`.
- Baseline OFF vs candidate SHADOW: the same standalone source-free difference plus the NOJSON shadow abort above.
- Exact project-context exclusion: none from the raw tracked-source corpus. The standalone source-free consumer is separately adjudicated by its required package context. Both compilers pass sequentially: `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug`, each using all 14 `.mossi` providers (5 Team A modules). See `logs/source-free-{baseline,candidate}.log`. Classification: **BENIGN project-context diagnostic difference**. The standalone corpus invocation omits the `.mossi` provider interfaces this source-free consumer requires; exact package-mode reruns pass for both compilers, so this is not a package verdict difference.

## Checked-declaration closure

`checked-declaration-closure.json` contains all 2,146 entrypoints, accepted and rejected, both checked declaration sets, trace completeness, verdicts, and the exact exclusion list (empty). The frozen comparison used baseline compiler at the exact baseline revision and candidate shadow mode. Result: 4 legacy-only checked declaration rows and 0 tree-only rows. The four rows are duplicate contexts for one identity, `method:heap__Entry.key_of@9`, from `examples/swarm/modules/algo_chain/heapkit/src/order.moss`, its test entrypoint, and the matching `natural_chain` source/test copies. In each context both sides reject with `INVALID_BARE_IDENTIFIER_STATEMENT:35`. This is an unresolved checked-closure MIGRATION BUG for the gate; no project-context exclusion applies to these tracked sources.

The reach trace script additionally reported 162 entrypoints with edge-label pairs (1,546 legacy labels missing and 1,546 tree labels added) such as `Math__answer` → `answer` and `compute_part_01` → `mg1_multi_file__compute_part_01`. I independently normalized each target by removing its final `__` prefix and compared the per-entry, per-owner, per-kind target multisets: 0 missing and 0 added remain. Classification: **BENIGN trace target-label normalization**; these are not checked-declaration differences. Full raw pairs remain in `checked-closure.json`. The checked-declaration comparison above is independent of those edge labels.

## Verdict basis

The lint's apparent zero is insufficient: taint follow-through found uncovered literal/classifier readers; the frozen shadow reproducer aborts on an incomplete named-argument fragment; and checked-declaration closure has one legacy-only declaration identity in four contexts. Therefore the exact verdict is **CP-E verdict: NO-GO**. No findings are left unreported as BENIGN or UNEXPLAINED. This review did not edit source or rerun after the post-freeze fixes.
