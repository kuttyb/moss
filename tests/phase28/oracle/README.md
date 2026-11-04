# Phase 28 Independent Semantic Oracle Suite

## Overview

This suite provides an independent semantic oracle for Moss Phase 28 ownership semantics.
It tests the compiler against the frozen ownership proposal specification:
`docs/MOSS_PHASE_28_LOL_OWNERSHIP_PEER_REVIEW_PROPOSAL.md` (§10 and §12).

The oracle is decoupled from Team A and Team B implementations and asserts expected values, mutations, rejection diagnostics, zero hidden clones, and multi-engine parity across:
1. **Semantic Checker** (`moss check --json`)
2. **Fast Debug Interpreter** (`moss run --interp`)
3. **Native Debug** (`moss <src> -o <out.rs> && rustc <out.rs>`)
4. **Native Optimized** (`moss -O <src> -o <out_opt.rs> && rustc -O <out_opt.rs>`)
5. **Clone Budget Audit** (strictly zero unauthorized nonprimitive `.clone()` calls)

## Required Coverage

The suite contains 47 executable fixtures covering:
- **Core NO-GO Review Findings (Items 1–10):**
  1. Write-through borrowed Vector elements (`ORACLE-01-VEC-WRITE-THROUGH`): expected 9,9.
  2. Indexed field reads without hidden clones (`ORACLE-02-INDEXED-FIELD-READ-NO-CLONE`).
  3. Rejection of consuming borrowed collection elements (`ORACLE-03-COLL-ELEM-CONSUME-REJECT`).
  4. Write-through borrowed Map entries (`ORACLE-04-MAP-WRITE-THROUGH`): expected 9,9.
  5. Borrowed `Map.get(key)` semantics (`ORACLE-05-MAP-GET-BORROW`).
  6. Native compilation and execution of `match consume` on options (`ORACLE-06-MATCH-CONSUME-OPT-NATIVE`).
  7. Catchable errors for missing map keys and invalid removals (`ORACLE-07-CATCHABLE-COLL-ERRORS`).
  8. Correct resolution of user-defined `copy` functions versus built-in (`ORACLE-08-USER-COPY-RESOLUTION`).
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

## Invocation

Run against the repository compiler:
```bash
python3 tests/phase28/oracle/oracle_harness.py --compiler ./moss
```

Run against a specific compiler binary:
```bash
python3 tests/phase28/oracle/oracle_harness.py --compiler build/compiler/moss-dev --json-out tmp/phase28_oracle/results.json --ledger-out tmp/phase28_oracle/ledger.md
```
