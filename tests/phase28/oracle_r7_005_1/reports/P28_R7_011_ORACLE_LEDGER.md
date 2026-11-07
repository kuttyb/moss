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
