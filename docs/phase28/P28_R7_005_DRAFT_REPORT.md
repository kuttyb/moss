# P28-R7-005 — Oracle Hardening Draft Report

**State: Awaiting L7 approval. Not frozen, not independently reviewed, and not
published.** Work is on `phase28-r7-005`, branched from
`4755ed2fcdf8b1654bb6b36d23bb44cd857838ff` (`phase28-r7-oracle`). No compiler
source files were changed.

## Draft contents

- 56 specification-based fixture expectations, stored independently from the
  harness in `tests/phase28/oracle/expectations.json`.
- Separate oracle-owned diagnostic normalization in
  `tests/phase28/oracle/diagnostic_mapping.json`.
- Corrected ORACLE-05/07/08 plus new tests for collection errors, an unhandled
  Root, imported `.mossi` effects, inferred raise sets, ownership at recover,
  eager pipeline capture, borrowed-origin joins, and live-iteration mutation.
- An occurrence-level clone provenance contract covering every generated Rust
  `.clone()` occurrence in debug and optimized outputs. The harness checks the
  entire generated artifact, including helpers before `main`, and validates
  explicit-copy source spans independently. The current emitter supplies no
  `.mossclone.json` artifact, so clone occurrences fail closed as unclassified.
- A reviewer-only holdout file in local `.git` metadata. It is not in the
  branch or available to Teams A/B through the remote.

## Validation

Built `build/compiler/moss-dev` from the task baseline, then ran the revised
oracle harness against it. Results: **11 PASS / 45 FAIL** across 56 fixtures.
These are draft conformance results for the requested expected behavior, not an
L7 verdict. The 298 observed generated Rust `.clone()` occurrences across debug
and optimized output remain **unverified**: without occurrence provenance they
are not established as hidden Moss-value clones or as harmless runtime clones.
They remain unclassified because the compiler does not provide the specified
provenance artifact.

Evidence from the baseline includes:

- ORACLE-05: one-argument `Map.get` is rejected with `TYPE_MISMATCH` because the
  baseline requires two arguments.
- ORACLE-07 and other typed collection fixtures: `CollectionError` is rejected
  as `UNKNOWN_ERROR_TYPE`.
- ORACLE-08: a user-defined `fn copy` is accepted, contrary to the requested
  reserved-name decision.
- The no-handler Root fixture is accepted on the baseline because its Map index
  operation carries no `CollectionError` raise effect there.
- ORACLE-47 panics in Fast Debug, native debug, and optimized native execution;
  its typed recovery arm is unreachable.
- The live Vector structural-mutation fixture is rejected with the message
  “cannot structurally mutate collection … during an active READ traversal”; the
  oracle-owned diagnostic map normalizes that current diagnostic.
- The eager pipeline capture fixture passes checker and execution output parity.
  The joined-borrow fixture passes Fast Debug but native output does not reflect
  the expected write-through mutation.
- The imported `.mossi` provider build currently fails because the baseline
  rejects one-argument `Map.get`.

Also completed: `python3 -m py_compile tests/phase28/oracle/oracle_harness.py`,
`python3 tests/tooling/check_agent_feedback.py`, and `git diff --check`.
No full repository suite was run. No commit, push, merge, freeze, or exact-SHA
publication is claimed.

## Remaining gates

1. L7 approval of the expectation table and language addendum.
2. Independent review of the harness and expectation/diagnostic separation.
3. Emitter support for the stated occurrence-level clone provenance interface
   is outside this task's no-compiler-source scope; without it, clone-audit
   acceptance remains blocked.
4. After the required approvals, freeze the exact table and publish its SHA.

