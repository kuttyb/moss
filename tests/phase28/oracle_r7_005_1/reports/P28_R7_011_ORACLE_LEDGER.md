# P28-R7-011 Oracle ledger: ORACLE-15 fixture correction

## Finding

The previously certified `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` fixture was a
fixture defect. Its `archive(items)` function had a `pass` body, so the call
`archive(b)` did not consume `b`. The oracle expected rejection even though the
normative Proposal §10 row says to reject only **if archive consumes**. This
fixture therefore tested the READ case while asserting the CONSUME verdict.

## Corrected cases

- **ORACLE-15** now makes `archive` return its `Vector[Int]` argument and binds
  `kept = archive(b)`. This is the actual consuming case and remains REJECT.
- **ORACLE-15B** retains the pass-body `archive` call, then reads
  `s.frontier`; it is ACCEPT and prints `3`.
- **ORACLE-15C** is the minimal plain-field assignment/read case from the
  directive; it is ACCEPT and prints `3`.

The expectations follow Proposal §10's conditional rejection and §3's
per-definition use-effect inference. This oracle correction does not itself
claim implementation conformance.

## Review and certification state

- Baseline source `150ab36b` predates `tests/phase28/oracle_r7_005_1/`; the
  update is based on current W5 source `45b586c2`.
- The expectation digest and candidate commit SHA are recorded after review.
- Independent normative review and full oracle recertification are pending.
- Do not mark the revised table `CERTIFIED_FROZEN` until the independent
  reviewer approves the exact candidate and the normal certification checks
  complete.

## Executable comparison on the current W5 compiler

The normal oracle harness ran against the compiler built from source
`45b586c2de3a405f96f0be8268947a1abb98d3c1` (`moss-dev` binary SHA-256
`dae345d0143ee273aa601ee40e17a955225f06e4ad9158df35944c362513f97d`). The
oracle table was in draft status and contained all 62 fixtures. Result: 19/62
passed every applicable gate; 43 failed at least one gate; 2 had semantic
failures; 0 were blocked by missing or invalid provenance; 252 generated Rust
clone occurrences were unverified. This is an implementation comparison, not
oracle certification.

The corrected consuming **ORACLE-15** passed its expected checker rejection.
**ORACLE-15B** and **ORACLE-15C** both failed with
`OWNERSHIP_ILLEGAL_CONSUME`, confirming the known field-read regression on this
compiler. The remaining 41 failures are provenance-only. Full machine results
were written to disposable `tmp/p28-r7-011-oracle-28e3-results.json` (SHA-256
`f6eb0da555ea059f61e3238be26b1d2aaaa1e6e40333c1f65e5219e2257b0d21`). The
comparison command was:

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler build/compiler/moss-dev \
  --json-out tmp/p28-r7-011-oracle-28e3-results.json \
  --ledger-out tmp/p28-r7-011-oracle-28e3-ledger.md
```
