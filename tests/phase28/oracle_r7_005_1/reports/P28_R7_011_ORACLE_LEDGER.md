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
- Independent normative review returned GO for candidate
  `ff306c27f58738bd7eb9c79b00eb165cead01209`; reviewer-controlled v2 holdouts
  passed. The frozen expectations digest is recorded in `certification.json`.
- The final frozen content SHA is the reviewed candidate's freeze commit; the
  final certification commit records it after the freeze commit is created.

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

## Corrected-output comparison on the combined Step 0 compiler

The first combined-compiler run showed both new READ fixtures checked and
executed successfully in Fast Debug, Native Debug, and Native Optimized, with
actual stdout exactly `3`. The draft expectation had encoded a trailing newline
(`3\n`), while the normal harness compares Moss output without that newline.
This was a representation mismatch, not a semantic outcome change. The
expected output is now the directive's exact `3` for all three modes.

The normal harness was rerun against the exact combined Step 0 source
`83e42eece65663b3de46e09a14227f9e17633513`; the `moss-dev` binary SHA-256 is
`1e83d1874b9cd22ce714ffca0bdfb824342a2fbddcd2ba94e9f73119894ad362`. Result:
19/62 passed every applicable gate; 43 failed at least one gate; zero semantic
failures; zero fixtures blocked by missing/invalid provenance; 264 generated
Rust clone occurrences were unverified. All 43 failures are clone-provenance
failures. ORACLE-15 passed checker rejection. ORACLE-15B and ORACLE-15C passed
checker and all three execution modes with stdout `3`; each remains a
provenance failure. No clone exception was added.

Machine results are in disposable `tmp/p28-r7-011-oracle-83e42-corrected-results.json`
(SHA-256 `ffea7b638f2becc592f7709d9f3ff147cc3ceff9433284675cb1b19b6acbc2ea`).
The generated machine ledger is `tmp/p28-r7-011-oracle-83e42-corrected-ledger.md`
(SHA-256 `6d1dc4a04093ddbce5f63b3ce66143a5eca9ab0572c03f3da03d91b1dfc72e53`).
The exact harness invocation was:

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler /home/owner/daji/code/moss/tmp/phase28-r7-011-integration/build/compiler/moss-dev \
  --json-out tmp/p28-r7-011-oracle-83e42-corrected-results.json \
  --ledger-out tmp/p28-r7-011-oracle-83e42-corrected-ledger.md
```

This remains an implementation comparison, not compiler-conformance approval.
The normative expectations and holdout integrity have a scoped independent GO;
the oracle certifies expected behavior only and approves no clone provenance
exceptions. Do not broaden the report to unrelated changes.

## Independent review and holdouts

Independent normative review returned **GO** for candidate
`ff306c27f58738bd7eb9c79b00eb165cead01209`, expectation digest
`ce0581abcecd1b476f9f552433375493b1ed0cd9a58c61236b959c2d453fbfec`. The
review confirmed the conditional Proposal §10 rule and the `3` stdout
representation. Reviewer-controlled v2 holdouts were frozen before execution:

- READ: `reports/holdouts/p28-r7-011-oracle-v2/holdout_read.moss` accepted and
  printed `4` in Fast Debug, Native Debug and Native Optimized; the source field
  remained readable.
- CONSUME: `reports/holdouts/p28-r7-011-oracle-v2/holdout_consume.moss`
  rejected with `OWNERSHIP_ILLEGAL_CONSUME` in checker, Fast Debug, Native Debug
  and Native Optimized; no Rust artifact was emitted.

The v1 holdout draft used reserved Moss identifier `crate` and produced invalid
Rust. It was abandoned before complete execution and is explicitly not used as
evidence. The v2 holdout plan, sources and independent report are preserved in
`reports/holdouts/p28-r7-011-oracle-v2/`; report SHA-256 is
`dd1afdcda55058517bc902378c08bc5d2919753948e0ac34cc99a25596683c62`, and plan
SHA-256 is `596bf6b29dc930a90eb26e0b250493f8fd80bd91554f0db1fddcdc3ae1f8b28d`.

This GO approves only the ORACLE-15/15B/15C normative expectations and holdout
integrity. It does not approve compiler conformance or clone provenance. The
certification metadata records the GO and PASS. Frozen expectation content SHA:
`55ec3d44980133d2dfe38e30d75b4df8eab78783`; final certification metadata is
committed separately afterward.
