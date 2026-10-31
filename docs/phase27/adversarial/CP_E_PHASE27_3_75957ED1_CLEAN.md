# Independent CP-E review: frozen Phase 27.3 candidate

Candidate: `75957ed1ff052c3bfd88d021b4d229f0592783e3`
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`
Review branch: `review/CP_E_PHASE27_3_75957ED1_CLEAN`

CP-E verdict: GO-with-defects

## Finding

MIGRATION BUG: the frozen candidate over-rejects an expression that the baseline accepts. In `tests/phase27/est/repro/unclosed_vector_expression.moss`, the source is `echo [1, 2`:

- Baseline, off and shadow: accepted, no diagnostic.
- Candidate, off and shadow: rejected with `MOSS_COMPILE_ERROR`, “heterogeneous or unresolved collection element type” at line 2, column 1.

This is the only source-semantic defect found in the explicit regression and corpus comparison. The candidate's four named malformed-expression controls agree between off and shadow; three also match the baseline. The unclosed-vector/echo correction is broader than the baseline contract. This verdict applies only to the frozen candidate SHA; no compiler source or test source was changed in this review worktree.

## Regression and declaration-closure evidence

The independently written probe runner compared the four named new repros and thirteen named Phase 27.1 rejected-source controls directly with baseline and candidate compilers under `MOSS_EST=off` and `shadow`. Seventeen controls were checked in four configurations. No off/shadow divergence occurred. The one baseline/candidate difference is the echo case above. Full results are in [`named_regression_probes.json`](../../../tests/phase27/adversarial/cp-e-phase27-3-75957ed1-clean/named_regression_probes.json).

I explicitly enumerated 934 `.moss` files under `tests/` and `sdk/`, pruning adversarial-named directories before descending into them. All 3,736 checks completed with valid JSON. There were zero baseline off/shadow differences, zero candidate off/shadow differences, and exactly two baseline-to-candidate standalone diagnostic differences:

| Path | Baseline off/shadow | Candidate off/shadow | Classification |
| --- | --- | --- | --- |
| `tests/phase27/est/repro/unclosed_vector_expression.moss` | accepted | `MOSS_COMPILE_ERROR` at 2:1: heterogeneous or unresolved collection element type | MIGRATION BUG: the echo expression is rejected while baseline accepts it. |
| `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss` when checked standalone without module paths | `RECOVERY_PATTERN_REQUIRES_VARIANT` at 57:1: recovery patterns must name a qualified enum variant | `UNKNOWN_ERROR_TYPE` at 57:1: unknown error enum `ndarray.NdArrayError` | BENIGN: this standalone invocation has no imported package interfaces. The package-backed source-free gate is the oracle and passes on baseline/candidate in off/shadow. |

The focused aggregate harness was skipped because its implementation copies a restricted adversarial fixture. This is a review-method limitation, not a verdict input. Its non-adversarial EST cases were run directly against baseline and candidate in off/shadow: message, binary, numeric member, Phase 27.1b syntax, side facts, and tuple fixture. All checked successfully; shadow emitted comparisons and fact dumps, and expected tree node kinds were present. The explicitly named `package_open_collection` Margo check rejected with the same expected diagnostic in all four compiler/mode combinations.

The package-backed source-free gate `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` passed for baseline off, baseline shadow, candidate off, and candidate shadow. Each run built against 14 source-backed provider interfaces and executed the consumer successfully. This validates the complete checked package closure for this source-free path.

Detailed run evidence and the enumerated corpus manifest are in [`cp-e-phase27-3-75957ed1-clean`](../../../tests/phase27/adversarial/cp-e-phase27-3-75957ed1-clean/). The scripts used are included there; neither script accesses the restricted adversarial directories. No performance measurements were run.

## Expression-reader inventory and taint trace

The required startup discovery reported `language_version=moss-0.1`. The repository’s named pattern inventory records the legacy checker/emitter expression-reading surface as 454 checker and 124 emitter sites (578 total), grouped into these exact categories. Each reader was classified by its replacement: structural projection into EST nodes/child IDs, typed side facts, or the retained narrow syntactic name/pattern fields.

| Legacy reader pattern | Checker | Emitter | Classification / EST replacement |
| --- | ---: | ---: | --- |
| Pipeline normalization and detection | 12 | 2 | Pipeline input/stage IDs and `FunctionalNodeKind`; use retained occurrence and side facts. |
| Redundant grouping | 20 | 9 | `Parenthesized.inner`; preserve meaningful grouping, otherwise descend structurally. |
| Bare-name recognition | 77 | 10 | `Identifier.name`, `Member.name`, syntactic callee node; semantic identity from resolved-target facts. |
| Identifier occurrence scans | 0 | 7 | Recursive EST visitor; scope-sensitive checks use binding facts. |
| Trimmed source equality | 75 | 23 | Node kind/child IDs/names, literal class, or typed side facts. |
| Direct call shape | 54 | 17 | `Call.callee` and ordered arguments; resolved target/specialization facts. |
| Member call shape | 40 | 8 | `Call(Member(base,name),arguments)` and resolved target fact. |
| Bound method callable | 8 | 2 | Member callable node, receiver/name, resolved target. |
| Named argument shape | 36 | 6 | `NamedArgument.name/value` children and source order. |
| Binary operator/operand extraction | 56 | 15 | `Binary.op/left/right`; unary operators use `Unary`. |
| Index extraction | 25 | 10 | `Index.base/index` child IDs. |
| Vector literal extraction | 11 | 2 | Collection delimiter and ordered element IDs. |
| Delimited child-list splitting | 23 | 9 | Existing child vectors for calls, constructors, messages, collections, tuples, fields, and pipeline stages. Match-arm/recovery pattern splits remain separately justified exemptions. |
| Enum case extraction | 7 | 2 | Constructor type/case and ordered args; named payloads via `NamedArgument`; resolution from checker facts. |
| FileIO call/chunk extraction | 5 | 2 | Call/member/named-argument structure and pipeline child calls; semantics from node/fact data. |
| Executor-chain extraction | 2 | 0 | Nested call/member and message nodes; callable resolution via side facts. |
| Top-level assignment/colon scanning | 3 | 0 | NamedArgument or Call argument nodes; labels distinguished structurally. |
| **Total** | **454** | **124** | **578** |

The current widened-lint scanner on the frozen candidate reports zero expression-text reader calls in checker, emitter, reach walk, and Fast Debug. The scanner also reports zero `tree_expression_text(...)` reads in checker/emitter consumers, no raw punctuation/trimmed-prefix/first-character shape tests on expression-bearing `Stmt.a`, `.b`, or `.text`, and no unclassified dynamic `Stmt` comparisons. Candidate raw statement comparisons are limited to 40 audited NAME comparisons and 9 audited assignment/loop binding-name comparisons. Their per-name counts and reasons are in [`27_3_STRING_READER_EXEMPTIONS.md`](../27_3_STRING_READER_EXEMPTIONS.md); expression-shaped constructor/collection checks use EST queries.

Bounded taint trace: expression strings retained in `Stmt.a`, `.b`, `.text`, `.args`, or `result_expression` flow to a checker/emitter EST source adapter only where an occurrence handle is unavailable, then to EST node projections or structural equality. The remaining checker `tree_expressions_structurally_equal` helper parses each operand through `checker_est_view` and calls `moss::est::expressions_equal`; it does not compare source strings for semantic equality. Its call sites are classified in [`checker_structural_equality_sites.txt`](../../../tests/phase27/adversarial/cp-e-phase27-3-75957ed1-clean/checker_structural_equality_sites.txt): retained pipeline/constraint occurrence matching, checked call arguments, return/local binding correspondence, same-binding collection refinement, call occurrence matching, and open-operator child matching. Reach walk uses expression-root slots/visitor handles; Fast Debug’s source adapter dispatches to the tree evaluator, and its zero-reader guard passed. Source text remains for diagnostics/provenance and narrow syntactic names; no expression-shape decision remains on raw text in these consumers.

All three keyed `split_top_level` exemptions were tested independently: renaming each target function caused a missing-function failure; adding a second reader site caused the exact-one-site failure. The scanner’s full output is in [`reader_counts.json`](../../../tests/phase27/adversarial/cp-e-phase27-3-75957ed1-clean/reader_counts.json), and the fail-closed probes are summarized in [`exemption_fail_closed.txt`](../../../tests/phase27/adversarial/cp-e-phase27-3-75957ed1-clean/exemption_fail_closed.txt).

## Validation and limits

- Both worktrees were isolated at the specified commits; each compiler was built locally with `make --jobs="$(nproc)"`.
- Both bootstraps reported `moss-0.1`.
- `python3 tests/tooling/check_phase27_string_debt.py` passed on the candidate.
- The aggregate EST harness was not run for the restricted-fixture reason above. Its safe, explicitly named Moss cases were run directly.
- No performance work or source changes were part of this review.

Evidence checksums are in [`CHECKSUMS.sha256`](CP_E_PHASE27_3_75957ED1_CLEAN.sha256).
