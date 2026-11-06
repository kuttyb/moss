# P28-R7-010 Fixture Deltas — Combined SHA 3417bedc

Previous comparison source: Team C exact staging run `1d3be42d` (results SHA-256 `d82183c4424cc4182a531759a35dbb35d63df9188d92e1b8e76c50fa2f7bbec5`).
Current source: `3417bedcd523dbeff187014de18d4a699ab53b10` (results SHA-256 `ead9cad35b758fa624fdb15df72ef4b919c5cfef9637e8f164417e808de81f1e`).

`ALL-GATES / SEMANTIC / PROVENANCE` uses `N/A` where a checker-rejected fixture has no generated provenance gate. All 60 fixtures have identical verdicts and per-mode outcomes. Timing values differ in all 60 rows. In addition, path-derived copy fact/variant IDs and Rust context hashes differ in ORACLE-10A; Root-ingress fact IDs and context hashes differ in ORACLE-43 and ORACLE-45; ORACLE-54 serializes its worktree-specific absolute source path in checker stdout. These raw-field differences do not change diagnostics, expectations, semantic results, engine outputs, or provenance dispositions.

| Fixture | Previous | Current | Raw JSON delta |
| --- | --- | --- | --- |
| `ORACLE-01-VEC-WRITE-THROUGH` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-02-INDEXED-FIELD-READ-NO-CLONE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-03-COLL-ELEM-CONSUME-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-04-MAP-WRITE-THROUGH` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-05-MAP-GET-BORROW` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-06-MATCH-CONSUME-OPT-NATIVE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-07-CATCHABLE-COLL-ERRORS` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-08-USER-COPY-RESOLUTION` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-09-USER-ENUM-SOME-NONE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-10A-EXPLICIT-COPY-ONLY` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timings; path-derived copy fact/variant IDs and Rust context hashes |
| `ORACLE-10B-CLONE-ALIAS-REJECTED` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-11-ASSIGN-READ-BORROW` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-12-ASSIGN-WRITE-BORROW` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-13-ASSIGN-CONSUME-RETURN` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-16-TAKE-FRONTIER-ARCHIVE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-17-TAKE-OPTION-FIELD` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-18-TAKE-QUEUE-FIELD` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-19-TAKE-NESTED-VEC-OPTION` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-20-TAKE-NESTED-MAP-VECTOR` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-21-TAKE-LOCAL-REJECTED` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-22-TAKE-UNIFIED-FIELD-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-23-TAKE-VEC-ELEM-REJECTED` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-24-TAKE-CUSTOM-SLOT-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-25-TAKE-BEFORE-RAISE-MUTATION` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-26-VEC-REMOVE-API` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-27-REBIND-AFTER-BORROWS` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-28-REBIND-WHILE-BORROWED-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-29-LOOP-CARRIED-OWNERSHIP` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-30-JOIN-BORROW-FRESH-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-31-JOIN-PROMOTED-CONSUME` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-32-MATCH-PAYLOAD-WRITE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-33-MATCH-LAST-USE-OVERWRITE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-36-MATCH-CONSUME-QUEUE-POP` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-38-MAP-VALUES-BORROWED-VIEW` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-39-VEC-FOR-READ` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-40-VEC-FOR-WRITE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-41-VEC-FOR-CONSUME-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-42-CUSTOM-WRAPPER-REMOVE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-43-HANDLER-PRIMITIVE-REPLY` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timings; path-derived Root-ingress fact IDs and Rust context hashes |
| `ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timings; path-derived Root-ingress fact IDs and Rust context hashes |
| `ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-47-VECTOR-INDEX-PANICS` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-48-ROOT-MISSING-COLLECTION-ERROR` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-49-OWNERSHIP-AT-RECOVER` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-50-EAGER-PIPELINE-CAPTURES` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-52-MUTATE-DURING-ITERATION-REJECTED` | PASS/PASS/N/A | PASS/PASS/N/A | timing values only |
| `ORACLE-53-MAP-REMOVE-MISSING` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-54-IMPORTED-MOSSI-EFFECTS` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timings; worktree-specific absolute source path in checker stdout |
| `ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-57-MAP-GET-WRITE-THROUGH` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
| `ORACLE-59-SHADOWED-BINDING-IDENTITY` | FAIL/PASS/FAIL | FAIL/PASS/FAIL | timing values only |
