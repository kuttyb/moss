# Phase 28 Independent Semantic Oracle Suite

## Overview

This suite provides an independent semantic oracle for Moss Phase 28 ownership semantics.
Its 62-row successor expectation table includes the P28-R7-011 ORACLE-15 correction;
the revised table has independent normative GO and reviewer-controlled holdouts PASS.
The previous 60-row version 2 table remains historically certified; certification records the
independent normative review, reviewer-controlled holdouts, implementation comparison, and digest pins. `certification.json` records the approved candidate, independent
review verdict, reviewer-controlled holdout result, and digest pins.

The oracle is decoupled from Team A and Team B implementations and asserts expected values, mutations, rejection diagnostics, no unclassified clone occurrences, and multi-engine parity across:
1. **Semantic Checker** (`moss check --json`)
2. **Fast Debug Interpreter** (`moss run --interp`)
3. **Native Debug** (`moss <src> -o <out.rs> && rustc <out.rs>`)
4. **Native Optimized** (`moss -O <src> -o <out_opt.rs> && rustc -O <out_opt.rs>`)
5. **Clone Provenance Audit** (per-occurrence source provenance for every `.clone()` in debug and optimized Rust, including helpers before `main`)

## Required Coverage

The suite currently contains 62 successor fixtures covering:
- **Core NO-GO Review Findings (Items 1–10):**
  1. Write-through borrowed Vector elements (`ORACLE-01-VEC-WRITE-THROUGH`): expected 9,9.
  2. Indexed field reads without hidden clones (`ORACLE-02-INDEXED-FIELD-READ-NO-CLONE`).
  3. Rejection of consuming borrowed collection elements (`ORACLE-03-COLL-ELEM-CONSUME-REJECT`).
  4. Write-through borrowed Map entries (`ORACLE-04-MAP-WRITE-THROUGH`): expected 9,9.
  5. One-argument `Map.get(key)` returns borrowed `V`; missing key raises (`ORACLE-05-MAP-GET-BORROW`).
  6. Native compilation and execution of `match consume` on options (`ORACLE-06-MATCH-CONSUME-OPT-NATIVE`).
  7. Typed errors for missing map keys, invalid removal indices and empty pops; indexing out of bounds panics (`ORACLE-07-CATCHABLE-COLL-ERRORS`, `ORACLE-47-VECTOR-INDEX-PANICS`).
  8. `copy` is reserved; a user-defined `fn copy` is rejected (`ORACLE-08-USER-COPY-RESOLUTION`).
  9. User-defined enum cases named `Some`/`None` (`ORACLE-09-USER-ENUM-SOME-NONE`).
  10. Explicit-copy-only duplication and rejection of `.clone()` (`ORACLE-10A-EXPLICIT-COPY-ONLY`, `ORACLE-10B-CLONE-ALIAS-REJECTED`).
- **Proposal §10 and §12 Comprehensive Coverage:**
  - Local assignment effects (READ, WRITE, CONSUME inference)
  - `take(place)` explicit moves and rejection of illegal places
  - Rebinding after borrow expiration vs rejection while borrowed
  - Loop-carried ownership inference and fixed points
  - Join widening, promoted consume, and mixed-join rejection
  - Pattern borrows, last-use borrow expiration, and `match consume`
  - Collection iteration, mutable loops, and wrapper delegation
  - Handler incoming parameter immutability and primitive reply rules
  - CollectionError propagation through imported `.mossi` and Root totality
  - Ownership state at recover, eager pipeline captures, borrowed-origin joins,
    and structural mutation during live Vector iteration

## Invocation

Current status: previous v2 expectations and diagnostic mappings were
`CERTIFIED_FROZEN`; the P28-R7-011 successor is also `CERTIFIED_FROZEN` after
independent normative GO and reviewer-controlled holdout PASS. The latest full
oracle comparison is on combined compiler source
`00b54a515364f9237ce758eb866fa621da5375b5`. It still has 43 provenance-only
failures; certification does not imply compiler conformance. See
`reports/P28_R7_011_ORACLE_LEDGER.md` for the fixture-defect record.
The v1 oracle remains independently certified and unchanged. The R7-008
implementation comparison records semantic mismatches and missing clone
provenance; oracle certification does not establish compiler compliance.

Run against the repository compiler:
```bash
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py --compiler ./moss
```

Run against a specific compiler binary:
```bash
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py --compiler build/compiler/moss-dev --json-out tmp/phase28_oracle/results.json --ledger-out tmp/phase28_oracle/ledger.md
```


This is the separately versioned P28-R7-005.1 successor. The certified v1 oracle remains unchanged at `tests/phase28/oracle/`; its expectations and runtime allowlist digests are recorded as parent pins in this successor. Compiler failures against expected ownership semantics are recorded as implementation findings; oracle certification does not assert compiler conformance.
