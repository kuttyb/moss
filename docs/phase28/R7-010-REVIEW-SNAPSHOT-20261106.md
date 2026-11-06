# Phase 28 R7-010 review snapshot — 2026-11-06

Branch: `phase28-r7-010-review-snapshot-20261106`  
Snapshot implementation tip before this index update: `b61614cd`
Vertical integration base: `54eeb2c188ec3bc030a9e26a05e8a5ef52811dc0`
Fresh Root-ingress implementation: `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`
Explicit-copy Root-ingress implementation: `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`

This branch gathers the current isolated vertical integration and the newly reviewed Root-ingress fresh-payload repair in one place for review. It is a review snapshot, not authorization to merge to `phase28-work` or `main`. Full Phase 28 remains NO-GO.

## Included implementation

The vertical integration contains the previously reviewed CollectionView repair, imported generic ReturnValue authority, and declared-`None` method ReturnValue authority. The overloaded-method identity implementation is also present in the integrated source tree; its original scoped review explicitly closed the overload identity issue and isolated Phase 26 `-Werror` requirement. Its review is [R7-010-team-a-overload-fix-3dc803a2.md](agent2-reviews/R7-010-team-a-overload-fix-3dc803a2.md).

The added Root-ingress repair issues checker authority only for a fresh direct constructor or literal argument to a main-body Root message. It transfers the already-owned value and grants no Moss copy permission. Exact checker and consumer corruption checks are registered in `tests/tooling/check_phase28_root_ingress_capture_authority.py`.

The next frozen slice, `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`, retains the existing checker `TypedCopyFact` through an Echo-wrapped message expression and resolves it at the exact Root message argument in native lowering. The legacy generic payload, Phase 26 snapshot, and materialized Vector fixtures now request `copy(...)` explicitly. This adds no message-argument Consume rule. The 190-scenario Root gate and optimizer parity gate pass in Default/Shadow, but **both independent reviews returned NO-GO**: Fast Debug still deep-copies every Root message argument through `independent(...)` without consuming the exact checker fact. It duplicates explicit copies and lets a non-copy local pass Fast Debug while native fails closed. Reports: [Agent 2](agent2-reviews/R7-010-root-local-transfer-3f79d7f0.md), [Team C](team-c/R7-010-root-local-transfer-3f79d7f.md). A follow-up is underway; no full suite or oracle ran on `3f79d7f0`.

## Root-ingress scoped review evidence

Both independent reviewers returned scoped PASS for exact candidate `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`:

- [Agent 2 adversarial review](agent2-reviews/R7-010-root-ingress-payload-97c2573d.md)
- [Team C verification](team-c/R7-010-root-ingress-payload-97c2573d.md)

The registered gate passed 138 scenarios in Default and EST-shadow. Constructor and string-literal native sources compiled with warnings denied and ran with expected output in both modes. Identifier and materialized-local payloads remain unsupported and fail closed. No payload copy/clone authorization was introduced.

These are focused results. No full suite, frozen ownership oracle, or clone-provenance audit ran on the Root-ingress candidate or this snapshot.

The explicit-copy slice is main-only and has no `.mossi` transport path. Fast Debug's existing Root handler preparation still calls `independent(...)`; this slice does not change or authorize that runtime copy path. Runtime clone provenance remains independently unverified.

## Phase-wide status and measurement boundaries

The latest full Default and Shadow suites were run on source SHA `9b440e9376873d8480a52d87b2bc43b1fdd5d7c7`: both were 99/167 PASS, 67 FAIL, 1 SKIP. The fresh oracle on that same SHA was 60/60 semantic PASS, zero engine disagreements, 19/60 all-gate PASS, and 41 provenance-only failures. These results do not validate this snapshot SHA.

The corrected earlier recovery figures (90/167 suites against the 166/167 baseline; oracle 60/60 semantic, zero engine disagreements, 19/60 all-gate and 41 provenance-only; 252 unclassified direct `.clone()` occurrences and 588 qualified `Arc::clone` occurrences) belong to earlier tested SHAs. They are not measurements of this branch. No runtime clone exception is approved.

Outstanding phase acceptance includes recovering Default and EST-shadow suites to the established baseline, rerunning the frozen ownership oracle and engine parity on the final source, resolving provenance-only failures, and independently verifying runtime clone provenance without unapproved exceptions. Full `.mossi` and native-consumer work remains subject to the exact checker-authority contract and its own gates.
