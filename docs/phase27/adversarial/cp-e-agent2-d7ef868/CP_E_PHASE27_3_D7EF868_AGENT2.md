# CP-E Phase 27.3 blind review: d7ef868d

Candidate: `d7ef868d7206bc72bf03a209edff613fafb00932` (`origin/phase27-3-work`).
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` (phase27-2-m1).
Both compiler bootstraps reported `moss-0.1`.

CP-E verdict: GO-with-defects

## Findings

- **MIGRATION BUG — incomplete member diagnostic changed.** The fresh probe
  `tests/phase27/adversarial/cp-e-agent2-d7ef868/incomplete_member.moss`
  leaves an empty vector's element type unresolved and ends a member expression
  at `values.`. The baseline emits `TYPE_AMBIGUOUS_OPEN_RESULT` with message
  `unbound open result type for variable 'result'; heterogeneous or unresolved
  collection element type`. Candidate off and shadow emit `MOSS_COMPILE_ERROR`
  with `heterogeneous or unresolved collection element type`. This is a
  frozen-baseline diagnostic delta in an incomplete-source path.
- **LEGACY MISPARSE — standalone external recovery case.** The baseline corpus
  differential found one difference in `sdk/advanced-data-acceptance/fixtures/
  source_free_consumer.moss` at line 57 when checked as an isolated source
  without package module paths. Baseline reports
  `RECOVERY_PATTERN_REQUIRES_VARIANT`; candidate off and shadow report
  `UNKNOWN_ERROR_TYPE` for `ndarray.NdArrayError`. The source-free package gate
  resolves all provider interfaces and accepts/runs this consumer on both the
  baseline and candidate, so the isolated baseline recovery diagnostic is a
  legacy misparse of a qualified external type without its interface.

No `SEMANTIC` classification is used.

## Differential and probes

The fixed baseline revision's 2,118 tracked `.moss` files were checked with
baseline/off, candidate/off, and candidate/shadow: 6,354 checks total. Candidate
off and shadow agree on every file. There is one baseline/off diagnostic
mismatch, described above; details are in `corpus-differential.json`.

Fresh collection probes cover empty Vector and Map bindings, unresolved index
and pop results, an unresolved empty-vector argument, heterogeneous collections,
incomplete binary/member expressions, and exact diagnostics from all three
modes. Raw JSON results are in
`tests/phase27/adversarial/cp-e-agent2-d7ef868/targeted-diagnostics.json`.

## Reader audit

`python3 tests/tooling/check_phase27_string_debt.py` passes. Its inventory
reports 40 raw literal NAME comparisons and 9 binding-name comparisons; no raw
expression-shape probes; no `tree_expression_text` occurrences; and no legacy
expression-reader API call sites in the checker/emitter. The three remaining
`split_top_level` exemptions are keyed by source path, C++ function, and API;
reasons are nonempty, function bodies resolve uniquely, each site is matched
exactly once, and stale/duplicate/unmatched exemptions fail closed.
`reader-inventory.json` contains the keyed inventory, while
`raw-stmt-field-hits.txt` preserves the full raw scan of checker, emitter, and
included `.inc` files for manual classification.

## Verification

- Candidate and baseline `make --jobs="$(nproc)"` succeeded; both bootstrapped as `moss-0.1`.
- `tests/tooling/check_phase27_est.py ./moss` passed.
- `tests/tooling/check_phase25_checked_body_ir.py ./moss` passed, including
  checked-body IR hash and dependency-closure cases.
- `sdk/tables` release native builds passed in `MOSS_EST=off` and `shadow`,
  run sequentially with a clean between modes.
- `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug`
  passed candidate off/shadow and baseline off.
- An additional ABI-10 release source-free attempt failed twice while the
  script's concurrent provider builds wrote the shared runtime artifact tree
  (first a missing object, then a UTF-8 decode error). These are accurately
  recorded as failed attempts; ABI-10 release is not claimed green.

Compiler binary SHA-256 values:

- Candidate: `3a932cac088849ac325a0d840d86c38032c5d146a5d56007cafae5ce586aced4`
- Baseline: `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`
