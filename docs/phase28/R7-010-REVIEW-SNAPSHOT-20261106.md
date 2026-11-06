# Phase 28 R7-010 review snapshot — 2026-11-06

Branch: `phase28-r7-010-review-snapshot-20261106`  
Latest implementation included: `7e4a56b3461ffd949f89542ad50bc7ccd4557241`
Vertical integration base: `54eeb2c188ec3bc030a9e26a05e8a5ef52811dc0`
Fresh Root-ingress implementation: `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`
Explicit-copy native Root-ingress implementation: `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`
Fast Debug Root-ingress authority follow-up: `7e4a56b3461ffd949f89542ad50bc7ccd4557241`

This branch gathers the current isolated vertical integration and the newly reviewed Root-ingress fresh-payload repair in one place for review. It is a review snapshot, not authorization to merge to `phase28-work` or `main`. Full Phase 28 remains NO-GO.

## Included implementation

The vertical integration contains the previously reviewed CollectionView repair, imported generic ReturnValue authority, and declared-`None` method ReturnValue authority. The overloaded-method identity implementation is also present in the integrated source tree; its original scoped review explicitly closed the overload identity issue and isolated Phase 26 `-Werror` requirement. Its review is [R7-010-team-a-overload-fix-3dc803a2.md](agent2-reviews/R7-010-team-a-overload-fix-3dc803a2.md).

The added Root-ingress repair issues checker authority only for a fresh direct constructor or literal argument to a main-body Root message. It transfers the already-owned value and grants no Moss copy permission. Exact checker and consumer corruption checks are registered in `tests/tooling/check_phase28_root_ingress_capture_authority.py`.

The explicit-copy native slice, `3f79d7f0206b6c9aa1e38dbfd506838f95bdc79a`, retains checker `TypedCopyFact` through Echo-wrapped messages and resolves it at the exact Root argument. It migrated three legacy silent-copy fixtures to explicit `copy(...)` and passed its 190-scenario Default/Shadow gate, but **both independent reviews returned NO-GO** because Fast Debug still deep-copied all Root arguments without checked authority and accepted a no-copy local. Reports: [Agent 2](agent2-reviews/R7-010-root-local-transfer-3f79d7f0.md), [Team C](team-c/R7-010-root-local-transfer-3f79d7f.md).

The follow-up at `7e4a56b3461ffd949f89542ad50bc7ccd4557241` adds exact Fast Debug Copy/Fresh/primitive authority checks, rejects unsupported nonprimitive locals, and moves values into the handler without an extra Root `independent()` copy. Its registered gate passes 214 scenarios in Default and EST-shadow; the explicit-copy trace records one authorized copy and zero boundary copies, while fresh and primitive cases record zero copies. Agent 2 and Team C both returned scoped GO ([Agent 2](agent2-reviews/R7-010-root-interpreter-authority-7e4a56b3.md), [Team C](team-c/R7-010-root-interpreter-authority-7e4a56b3.md)). No full suite or oracle ran on `7e4a56b3`.

## Root-ingress scoped review evidence

Both independent reviewers returned scoped PASS for exact candidate `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`:

- [Agent 2 adversarial review](agent2-reviews/R7-010-root-ingress-payload-97c2573d.md)
- [Team C verification](team-c/R7-010-root-ingress-payload-97c2573d.md)

The registered gate passed 138 scenarios in Default and EST-shadow. Constructor and string-literal native sources compiled with warnings denied and ran with expected output in both modes. Identifier and materialized-local payloads remain unsupported and fail closed. No payload copy/clone authorization was introduced.

These are focused results. No full suite, frozen ownership oracle, or clone-provenance audit ran on the Root-ingress candidate or this snapshot.

These Root message facts are main-only and have no `.mossi` transport path. Candidate `7e4a56b3` removes the extra Fast Debug Root-boundary deep copy and checks exact Copy/Fresh/primitive authority before dispatch. Runtime clone provenance for other interpreter/native sites remains independently unverified; no runtime clone exception is approved.

## Phase-wide status and measurement boundaries

The latest full Default and Shadow suites were run on source SHA `9b440e9376873d8480a52d87b2bc43b1fdd5d7c7`: both were 99/167 PASS, 67 FAIL, 1 SKIP. The fresh oracle on that same SHA was 60/60 semantic PASS, zero engine disagreements, 19/60 all-gate PASS, and 41 provenance-only failures. These results do not validate this snapshot SHA.

The corrected earlier recovery figures (90/167 suites against the 166/167 baseline; oracle 60/60 semantic, zero engine disagreements, 19/60 all-gate and 41 provenance-only; 252 unclassified direct `.clone()` occurrences and 588 qualified `Arc::clone` occurrences) belong to earlier tested SHAs. They are not measurements of this branch. No runtime clone exception is approved.

Outstanding phase acceptance includes recovering Default and EST-shadow suites to the established baseline, rerunning the frozen ownership oracle and engine parity on the final source, resolving provenance-only failures, and independently verifying runtime clone provenance without unapproved exceptions. Full `.mossi` and native-consumer work remains subject to the exact checker-authority contract and its own gates.
