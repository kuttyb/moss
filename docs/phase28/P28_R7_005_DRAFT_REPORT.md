# P28-R7-005 — Oracle Hardening Revised Draft Report

**State: Oracle expectations certified frozen after independent GO.**
Independent Agent 2 reviewed candidate `d37ea08fa853c994b69c16bd90671ffcf34c72ea`
and returned GO after the reviewer-controlled private holdout passed. The exact
freeze commit is `544506774211fbb5b9d4d3e8116525b389faa3dd`, recorded in
`tests/phase28/oracle/certification.json`. No compiler source was changed.

## Approval package

- [Complete 56-fixture expectation table](../../tests/phase28/oracle/expectations.json)
- [Oracle-owned structured diagnostic mapping](../../tests/phase28/oracle/diagnostic_mapping.json)
- [Runtime callsite allowlist](../../tests/phase28/oracle/runtime_callsite_allowlist.json): empty; runtime labels authorize no clone
- [Normative language addendum](P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md)
- [Clone provenance contract](P28_R7_005_CLONE_PROVENANCE_CONTRACT.md)
- [Certified per-fixture, per-engine results](../../tests/phase28/oracle/reports/P28_R7_005_CERTIFIED_RESULTS.json)
- [Certified failure ledger](../../tests/phase28/oracle/reports/P28_R7_005_CERTIFIED_FAILURE_LEDGER.md)
- [This draft report](P28_R7_005_DRAFT_REPORT.md)
- [Certification record](../../tests/phase28/oracle/certification.json)

Permanent fixture IDs and defect IDs are unchanged. Reviewer-owned holdout tests
remain private and are not part of this branch.

## Corrections and discriminating fixtures

- The package contains 56 fixtures, not 55.
- ORACLE-07 covers negative indices for both `remove` and `swap_remove`; its mutation assertion now points to stdout line 11.
- ORACLE-49 removes an element successfully, then raises on a later invalid
  removal; both the recovery and post-recovery observations expect the surviving
  Vector value `8`.
- ORACLE-50 changes the captured `offset` after constructing the pipeline and
  expects the materialized values to retain the eager results.
- ORACLE-10A mutates a copied record and verifies the original Vector record remains `original`.
- The Moss `copy(...)` scanner now enumerates nested calls such as `copy(copy(x))` with distinct source-span identities.
- ORACLE-52 mutates the iterated Vector with a fresh independent record, then
  uses the borrowed loop element. The rejection must come from the active
  iteration borrow, not consuming that element.
- Rejection checks parse structured JSON and verify severity, accepted stable
  diagnostic code, message pattern, source file, and a source line identifying
  the rejected construct. ORACLE-08 requires `RESERVED_KEYWORD`; generic
  `MOSS_COMPILE_ERROR` is not accepted for `fn copy`; ORACLE-44/52 generic-code mappings require the exact approved message and source construct.
- ORACLE-47 accepts only a non-timeout runtime failure with explicit indexing
  panic evidence and the expected stdout. A compiler failure, unrelated crash,
  or timeout cannot satisfy the panic result.

## Validation and draft results

The oracle data validator passed for all 56 fixtures. Harness self-tests passed
15/15, covering unrelated crashes, mismatched panic evidence, timeouts, structured
diagnostic code/message/location mismatches, unrelated ORACLE-44/52 generic
errors, missing provenance, forged runtime labels and keyword-bearing unrelated
evidence, incorrect source spans, stale manifests, nested copy identities,
mutation-line consistency, and copied-record independence.

The full oracle completed against the repository compiler at candidate
`d37ea08fa853c994b69c16bd90671ffcf34c72ea`; no compiler source was changed.
It reported 8 fixtures passing all applicable gates, 34 semantic failures
excluding provenance, and 19 fixtures blocked by missing or invalid clone
provenance. These categories overlap. Per-fixture and per-engine results are in
the linked JSON report; the failure ledger records each observed engine outcome.

ORACLE-47 produced indexing-panic evidence in Fast Debug, Native Debug and
Native Optimized. ORACLE-50 passed checker and execution-output parity across
all three engines, while its clone audit remained blocked by absent provenance.
ORACLE-52 was rejected with a structured diagnostic at the structural mutation
line. The approved collection behavior remains nonconforming on this baseline;
for example, one-argument `Map.get` is rejected as expecting two arguments,
and `CollectionError` is unknown to the checker. ORACLE-48's missing-handler
Root is accepted on the baseline.

The harness observed 298 generated Rust `.clone()` occurrences across debug
and optimized output. They remain **unverified**, not proven hidden Moss-value
clones or harmless runtime clones. The compiler does not emit the required v2
sidecar. The revised contract also states that `.clone()` text scanning alone
cannot prove absence of duplication through alternate Rust mechanisms; the
independent AST/MIR duplication audit remains open.

The independent holdout also found and verified a repair for a missing-sidecar
fail-open case: clone-free Rust artifacts now fail the provenance gate without
their v2 sidecar. Holdout controls for explicit-copy links, tampered context
digests, unlinking, duplicate rows, unknown origins and missing source-copy rows
passed. Certification does not close the separate generated-Rust duplication
audit or establish compiler compliance.

No full repository suite was run. The oracle was independently certified; no
compiler compliance claim is made.

## Checkpoint

Certification handoff complete. No merge to `main`.
