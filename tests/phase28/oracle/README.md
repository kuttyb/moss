# Phase 28 Independent Semantic Oracle Suite

## Overview

This suite provides an independent semantic oracle for Moss Phase 28 ownership semantics.
Its revised expectation table and language addendum are **drafts pending L7 approval**:
`expectations.json` and `docs/phase28/P28_R7_005_LANGUAGE_ADDENDUM_DRAFT.md`.

The oracle is decoupled from Team A and Team B implementations and asserts expected values, mutations, rejection diagnostics, no unclassified clone occurrences, and multi-engine parity across:
1. **Semantic Checker** (`moss check --json`)
2. **Fast Debug Interpreter** (`moss run --interp`)
3. **Native Debug** (`moss <src> -o <out.rs> && rustc <out.rs>`)
4. **Native Optimized** (`moss -O <src> -o <out_opt.rs> && rustc -O <out_opt.rs>`)
5. **Clone Provenance Audit** (per-occurrence source provenance for every `.clone()` in debug and optimized Rust, including helpers before `main`)

## Required Coverage

The suite currently contains 56 fixtures covering:
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

Current status: proposed expectations are not frozen, the private reviewer
holdout is local-only, and the emitter does not yet provide the specified
occurrence-level clone provenance sidecar. Harness results must not be labeled
an L7-approved conformance verdict until these gates are resolved.

Run against the repository compiler:
```bash
python3 tests/phase28/oracle/oracle_harness.py --compiler ./moss
```

Run against a specific compiler binary:
```bash
python3 tests/phase28/oracle/oracle_harness.py --compiler build/compiler/moss-dev --json-out tmp/phase28_oracle/results.json --ledger-out tmp/phase28_oracle/ledger.md
```
