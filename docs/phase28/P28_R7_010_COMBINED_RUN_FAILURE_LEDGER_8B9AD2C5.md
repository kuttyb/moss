# P28 R7-010 Combined Oracle Failure Ledger — `8b9ad2c5`

**Implementation SHA:** `8b9ad2c578e2beb60bfcaf5a7ee7f4a8f2b72f72`  
**Staging branch:** `phase28-r7-010-l7-copy-authority`  
**Oracle:** certified v2, 60 fixtures; expectations and mappings unchanged  
**Fresh outputs:** `tmp/phase28-r7-010-combined-8b9ad2c5.json` and `.md`  
**Fresh artifacts:** `tmp/l7-oracle-artifacts-8b9ad2c5/`

## Summary

| Result | Count |
| --- | ---: |
| All applicable gates pass | 19 / 60 |
| At least one gate fails | 41 / 60 |
| Semantic/execution failures | 4 |
| Clone provenance failures | 37 fixtures, 448 unauthorized clone rows |
| Missing/invalid-sidecar blocks | 0 |
| Harness/infrastructure failures | 0 |

The categories are disjoint for this run: the four semantic failures have no
native clone-provenance result, and the 37 provenance failures pass all
semantic/execution modes. The 448 rows count generated clone occurrences, not
distinct emitter sites or confirmed Moss-level illegal copies. Team B's separate
generation-site audit remains in progress; no runtime clone is authorized.

## Semantic and engine failures

| Fixture | Checker / Fast Debug | Native Debug / Optimized | Classification |
| --- | --- | --- | --- |
| `ORACLE-42-CUSTOM-WRAPPER-REMOVE` | PASS / PASS | `EMIT_FAIL` / `EMIT_FAIL` | Indexed nonprimitive read consumer cannot resolve exact checked READ/WRITE, Copy, or projected-borrow authority. |
| `ORACLE-54-IMPORTED-MOSSI-EFFECTS` | Provider `.mossi` build fails | Not reached | Imported provider build hits `MOSS_INTERNAL_INVARIANT checked return-value identity unavailable`; being separated into checker producer versus consumer lookup. |
| `ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE` | PASS / PASS | `EMIT_FAIL` / `EMIT_FAIL` | Native conditional Map read cannot resolve its checked CollectionError fact. |
| `ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION` | PASS / Fast Debug crashes | `EMIT_FAIL` / `EMIT_FAIL` | ReturnValue fact type does not match exact EST occurrence across colliding specializations. |

These are the only semantic failures in this 60-fixture run. Native/Fast Debug
disagreements are included above: ORACLE-42 and ORACLE-55 pass Fast Debug but
fail native emission; ORACLE-58 fails Fast Debug and native emission; ORACLE-54
fails while building the imported provider.

## Clone provenance failures

Thirty-seven accepted fixtures fail the certified clone-provenance gate. The
runner recorded 448 unauthorized generated clone occurrences across those
artifacts. This is occurrence-level accounting; it must not be read as 448
distinct causes. The earlier Team C generation-site report and Team B's pinned
67-artifact / 84-site inventory use different artifact scopes and must not be
combined arithmetically without a same-corpus reconciliation.

## Infrastructure and integrity

- The oracle harness completed all 60 fixture rows and wrote both fresh output
  files; no harness or infrastructure error occurred.
- The certified v2 expectations and diagnostic mapping files were not edited.
- The earlier `13/60` result belongs to implementation SHA
  `422d08a4bf3e5d2a34b3bbf34ef3487b9af0131b` and is not reused as this run's
  result.
- The implementation source at this run already includes reviewed consumer
  integration through `0fd269d2`; the oracle is pinned to the later checkpoint
  `8b9ad2c5`. A new full oracle is required after the four failures above are
  repaired.
