# R7-010 Runtime Clone Gate Follow-up

## Scope and status

This follow-up fixes evidence-gate correctness on top of the published `57f932b9f8facbad4c428ead1398434bcf2f2eb6` candidate. It does not change runtime clone source markers, sidecar schema, runtime semantics, or authorization rows. The runtime callsite allowlist remains empty. Team C and Agent 2 review must be repeated on the frozen follow-up SHA.

Worktree: `tmp/phase28-r7-010-runtime-clone-evidence-followup-57f932`
Branch: `phase28-r7-010-runtime-clone-evidence-followup-57f932`
Parent: `57f932b9f8facbad4c428ead1398434bcf2f2eb6`
Frozen implementation commit: `26f4a301356920b9c89f4d9ff6dd775eda626d0e`. The follow-up source/test tree is frozen at this commit.

## Six Team C unit failures

All six `INVALID_PROVENANCE` failures shared one fixture omission: the synthetic v3 sidecar builder did not emit the required `runtime_clone_site_observations` array. These synthetic artifacts contain no runtime clone-site observations, so the fixture now serializes an explicit empty array. No production schema or consumer was changed in this follow-up.

1. `test_exact_source_digest_and_byte_spans_preserve_crlf_and_utf8` — **PASS** after the fixture emits the empty array; exact UTF-8/CRLF spans and digest behavior is again reached.
2. `test_missing_checker_authority_is_blocked` — **PASS**; it now reaches the missing-authority check instead of failing early on a malformed v3 manifest.
3. `test_template_fact_sort_position_does_not_change_provenance_result` (first fact-order subcase) — **PASS**; it reaches and preserves the provenance result.
4. `test_template_fact_sort_position_does_not_change_provenance_result` (second fact-order subcase) — **PASS**; reversing template fact order produces the same provenance result.
5. `test_two_lexical_copy_sites_share_each_specialization_context` — **PASS**; the lexical-site/context mapping check now runs.
6. `test_two_specialization_facts_share_one_lexical_copy_occurrence` — **PASS**; shared lexical occurrence and specialization fact mapping now run.

Reproduction/verification command:

```sh
MOSS_PHASE28_QUERY_COMPILER=build/compiler/moss-dev \
  python3 -m unittest -v \
  tests.phase28.oracle_r7_005_1.test_oracle_harness \
  tests.tooling.test_phase28_r7_010_clone_inventory
```

Result: **46 tests passed, 0 failures**. The six formerly failing tests are listed above; the other 40 passed as well.

## Hash gate correction

The raw marker-stripped generated Rust digest changed across checkout paths because the generated helper symbol embeds the first 16 hex digits of the `TypedCopyFact` identity, whose identity includes the absolute Moss source path. The gate now:

1. Requires exactly one checked fact identity and requires every generated `__moss_copy_value_<id>` helper to match that fact's first 16 hex digits.
2. Normalizes only the helper ID and corresponding `typed-copy-fact-v1:<id>` comment before the structural Rust hash.
3. Rebuilds the identical fixture bytes from a second checkout-like source path, confirms the raw fact identity changes, confirms the normalized structural digest stays equal, and checks the full provenance audit returns the same counts for both artifacts.

This keeps exact checker-fact matching intact while removing the path-only difference from a structural output digest. A direct two-path run produced:

| Source path | Helper suffix | Full TypedCopyFact ID | Normalized structural digest | Provenance audit |
|---|---|---|---|---|
| `tmp/path-checkout-a/tests/phase28/oracle_r7_005_1/fixtures/source.moss` | `16a2becba49efba8` | `16a2becba49efba81865f8489dd540a8a9ed3fb7ba78285a325a7b35b5e2c569` | `cf0bd5e74c782d1e860d77345d1908e3a0cf6eb881d16f27139a81bdddd34329` | `UNAUTHORIZED`, 1 authorized / 3 unauthorized / 3 unclassified |
| `tmp/path-checkout-b/tests/phase28/oracle_r7_005_1/fixtures/source.moss` | `f70aa3057504e0c4` | `f70aa3057504e0c4d8308f307f7e48c8afcf1af74e2e8100cd05aae164c29120` | `cf0bd5e74c782d1e860d77345d1908e3a0cf6eb881d16f27139a81bdddd34329` | `UNAUTHORIZED`, 1 authorized / 3 unauthorized / 3 unclassified |

Both full fact IDs begin with the exact helper suffix recorded for that artifact. Each artifact was audited against authority queried from its own source path. Focused Default and EST-shadow runs of the official evidence script both pass after this correction.

## Mutation validator correction

The test-only `audit_runtime_sidecar_once` helper assumed byte offsets on `rust_span`, but the production v3 Rust span schema contains only `{start, end}` line/column positions. Its `KeyError` was caught as a successful mutation rejection, producing false-positive tests. The helper was removed. Missing, duplicate, source-hash, span, type, operator, FileChunk drop, and marker-relocation mutations now run through `harness.validate_runtime_clone_site_observations`, the production validator, and pass only when it rejects with `ValueError`.

## Remaining review findings

This checkpoint does not resolve the other Team C / Agent 2 concerns from `57f932`: separate site 01/08/09 evidence, dedicated FileIO lifetime evidence, runtime-02's Moss-callable config surface, and the allowlist's `CERTIFIED_FROZEN` metadata despite an empty site list. Per latest L7 steering, this follow-up is restricted to gate correctness. No clone was authorized; no runtime-source or allowlist metadata change was made.

No full suite or oracle result is claimed for this follow-up.
