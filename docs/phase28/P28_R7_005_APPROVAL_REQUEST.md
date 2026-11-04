# P28-R7-005 — L7 Approval Request (Draft)

**Requested decision:** Review and approve the complete revised oracle
expectation table and language addendum before the oracle is frozen.

**State:** Submitted for approval is pending. The author is not the approver.
No freeze or L7 approval is claimed.

## Review package

- [Expectation table](../../tests/phase28/oracle/expectations.json): 55
  fixtures, specification-based accept/reject results, per-engine output or
  panic expectations, mutation assertions, and permanent fixture/defect IDs.
- [Diagnostic mapping](../../tests/phase28/oracle/diagnostic_mapping.json):
  oracle-owned mappings separate from Moss compiler diagnostic definitions.
- [Language addendum](P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md): collection
  behavior, CollectionError propagation and the occurrence-level clone
  provenance artifact contract.
- [Oracle README](../../tests/phase28/oracle/README.md): revised scope, gates,
  and current draft status.

## Decisions requested

1. Confirm `copy` is reserved and `fn copy` is rejected.
2. Confirm `Map.get(k)` returns borrowed `V` and raises
   `CollectionError.MissingKey` when absent.
3. Confirm out-of-range Vector indexing panics; strict missing Map reads and
   write-through raise `CollectionError.MissingKey`; indexed assignment
   inserts/overwrites without a missing-key error.
4. Confirm invalid Vector `remove`/`swap_remove`, empty Vector/Queue `pop`, and
   missing Map `remove` raise the specified `CollectionError` variants.
5. Confirm inferred raises flow through imported `.mossi` interfaces and Root
   totality checks, including rejection of the Root fixture without a handler.
6. Approve or amend the occurrence-level Rust clone provenance schema and its
   source-span validation rules.
7. Approve the five additional coverage cases: imported effects, ownership at
   recover, eager pipeline captures, borrowed-origin joins, and live-iteration
   structural mutation.

The implementation SHA must remain unfrozen until this approval is recorded.
After approval, the oracle owner will incorporate requested edits, freeze the
table, obtain independent harness review against the exact candidate, run the
private reviewer-controlled holdout, and publish the exact SHA.

