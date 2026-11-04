# P28-R7-005 — L7 Approval Request (Draft)

**Decision:** L7 approved the language semantics. Oracle freeze is not yet
approved. This revised package requests review of the expectation table and
corrected harness/provenance contract; it remains unfrozen.

**State:** `DRAFT_PENDING_L7_APPROVAL`. Semantic approval does not approve the
oracle freeze. The author is not the approver.

## Review package

- [Expectation table](../../tests/phase28/oracle/expectations.json): 56
  fixtures, specification-based accept/reject results, per-engine output or
  panic expectations, mutation assertions, and permanent fixture/defect IDs.
- [Diagnostic mapping](../../tests/phase28/oracle/diagnostic_mapping.json):
  oracle-owned mappings separate from Moss compiler diagnostic definitions.
- [Language addendum](P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md): approved
  collection behavior and CollectionError propagation.
- [Clone provenance contract](P28_R7_005_CLONE_PROVENANCE_CONTRACT.md): revised
  sidecar schema, evidence requirements, scanner limits and adversarial checks.
- [Draft report](P28_R7_005_DRAFT_REPORT.md): validation evidence and open gates.
- [Oracle README](../../tests/phase28/oracle/README.md): revised scope, gates,
  and current draft status.

## L7 disposition and remaining review

L7 approved the language semantics in items 1–5 below. This is recorded as
semantic approval only; it does not authorize freezing the table.

1. `copy` is reserved and user-defined `fn copy` is rejected.
2. `Map.get(k)` returns borrowed `V` and raises `CollectionError.MissingKey`
   when absent.
3. Out-of-range Vector indexing panics; missing Map reads and write-through
   raise `CollectionError.MissingKey`; indexed assignment inserts/overwrites.
4. Invalid Vector `remove`/`swap_remove`, empty Vector/Queue `pop`, and missing
   Map `remove` raise their specified `CollectionError` variants.
5. Inferred raises flow through imported `.mossi` interfaces and Root totality
   checks, including rejection of a Root without a required handler.

The requested review covers the corrected 56-fixture expectation table,
structured rejection and panic validation, the revised clone provenance
contract, and strengthened ORACLE-07/49/50/52 discriminators. No oracle freeze
approval is claimed.

The oracle SHA remains unfrozen until explicit approval of the complete revised
expectation table and harness contract. After approval, the oracle owner will
incorporate requested edits, request independent harness review against the
exact candidate, run the private reviewer-controlled holdout, and publish the
exact SHA.

