# Phase 27.5.3 — C1 stop-loss checkpoint

## Disposition

Phase 27.5.3 stopped at C1 under the specified stop-loss rule. The passing
remainder is merged as a **blocker checkpoint**, not as completion of the
checker-authority migration. C2 was not started.

- Work branch: `phase27-5-3-work`
- Reviewed implementation: `d3052c4dc1000b4a5a39327b321f209ab8b927b0`
- Merge: `c96bca786caefa5744000af9918be50a65b41037`
- First parent: `b4670ca1e21dbcf0cf3e7a5413898fa1fd7f0bac` (`origin/main` at fetch)
- Tag: `phase27-5-3-c1`
- Agent 2: GO for merging/tagging only as a C1 blocker checkpoint. The audit
  confirms that C1 is unmet and that the missing-handle failures were
  substantiated.

The compiler migration could not meet C1. Removing Fast Debug's expression
tree parser fallback made accepted recovery, join, enum, and domain programs
fail closed because the checker had not retained handles for every expression
consumer. The attempted default suite passed 111/165 jobs and failed 54 with
`MOSS_INTERNAL_INVARIANT missing checked expression handle`. Representative
sources were `tests/phase106e_domains.moss`,
`tests/tooling/fixtures/phase21_addendum_grouped_root.moss`, and
`tests/tooling/fixtures/phase21_capture_owned_string.moss`. That fallback
removal was reverted to preserve existing execution behavior.

## C1 exit results

| Gate | Result |
| --- | --- |
| K1 declared authority | Classes 1–9 are listed in `docs/COMPILER_STATE.md`; the exact producers still need perturbation proof. |
| K2 perturbation | FAIL / not implemented. No `MOSS_TEST_PERTURB=<class>` suite exists for classes 1–4, 7, or 8. |
| K3 fact removal | FAIL / not implemented. Class 1–4 drop hooks and removal tests are absent; dropped-fact success counts were not measured. |
| K4 fallbacks | Not met. Phase 27.5 baseline counts remain ambiguous call edges 40, none 3, Fast Debug pipeline type fallback 12, Fast Debug specialization fallback 2. No new bulk count was run. |
| K5 downstream parsing | FAIL. The failed Fast Debug fallback removal was reverted; current parser boundaries are not enforced by the build. The Phase 27.5.2 corpus baseline was 4,929,605 re-parses. |
| K6 fact coverage | FAIL / not remeasured. The Phase 27.5.2 baseline was 23,452 PRESENT / 282,668 questions (8.3%). |
| K7 engines and artifacts | Existing `make check` passed in default and shadow, 165/165 each. The registered focused fixtures and existing three-engine and `.mossi` checks passed. Full baseline parity for the migration was not run. |
| K8 conflict registry | 0 semantic conflicts recorded; 0 L7 questions. Registry: `docs/phase27/27_5_3_CONFLICTS.md`. |
| K9 cost ceiling | No timing/RSS suite run. Both optimized compiler binaries have identical SHA256 `98c9cd8769b4a120c6154e6973f9f59dcf70a228fe9334f4b484ac366fb331f7`; compiler source is identical to `origin/main`, so no compiler performance change is present in this checkpoint. |
| K10 rule and test registration | `AGENTS.md` rule updated: YES. All K2/K3 tests registered: NO; those tests were not implemented. |

The final restored-candidate `make --jobs="$(nproc)" check` passed default
165/165 and shadow 165/165, with zero failures or skips. The focused parity
gate covers `examples/functional_basics.moss`,
`tests/phase27/est/specialized_call_facts.moss`, and
`tests/swarm_001_parenthesized_pipeline.moss`. It checks native, Fast Debug,
and optimized Rust output agreement; it does not claim that all Fast Debug
reparses have been removed.

No bulk performance suite was run. The saved phase27-5-2 baseline and current
optimized compiler binary are identical, and `git diff origin/main -- src/`
is empty. Peak RSS was not measured.
