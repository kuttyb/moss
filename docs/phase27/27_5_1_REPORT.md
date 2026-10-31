# Phase 27.5.1 checked call edges and Fast Debug joins/patterns

## W1 — checked call-edge specialization

- Added shared `checked_call_edge_specialization(program, context, callee, line)`. It selects from checked semantic call edges by target and line and maps recorded argument types to callee specializations; it never accepts consumer-computed argument types.
- Native emission and Fast Debug use the shared lookup. Unique selections use the checked specialization. Ambiguous emitter selections retain the previous fallback and assert the selected specialization belongs to the checked candidate set; ambiguous selections are counted with `MOSS_TEST_COUNT_FACT=g1`. Fast Debug preserves the prior none/ambiguous fallback and G4 counter.
- Context mapping covers source checker edges that use unqualified handler names or a generic function context while the consumer uses a module-qualified or specialized context.
- One combined corpus/examples/packages pass checked 1,821 source files and built/debugged 337 package manifests (286.2 seconds). Before the explicit context mapping, it found 1,246 unique selections, 40 ambiguous selections, and six natural `None` results. Targeted re-emission after mapping adds ten unique selections. Final measured/targeted totals: unique 1,256, ambiguous 40, natural `None` 3. The remaining `None` paths retain the pre-existing fallback per phase rule. The dropped-edge test hook still produces `MOSS_INTERNAL_INVARIANT missing checked call edge` on the fixture.
- All six natural misses are **FACT-PRESENT-UNUSED**: three module-qualified handler edges, two generic function call edges, and the `bump` edge recorded at `fn:forward` line 5 although an emitter helper reached `call_specialization` without context or source line. Counts: FACT-PRESENT-UNUSED 6; FACT-ABSENT 0. The six source samples are `tests/implicit_domain_parameter_specialization.moss`, `tests/implicit_domain_specialization.moss`, `tests/implicit_domain_specialization_message.moss`, `tests/phase106d_parameter_write.moss`, `tests/swarm_050_join_specialization.moss`, and `tests/swarm_050_nested_specialization.moss`.
- Generated Rust had no differences for pre-existing corpus/examples. The only two diff paths were newly added W2/W3 regression fixtures: `join_recover_binding.moss` exercises the corrected checked join assignment, and `default_char.moss` exercises the explicit NUL Char default.

## W2 — joins and patterns

- Fast Debug reads `joined_types_by_context[frame.functional_context]`, falling back to `joined_types`. Removed `block_binders`, `joined_binders`, and their cache.
- Match and recovery execution consume checker-populated EST pattern nodes. Checker resolution now stores canonical enum names in qualified recovery patterns, including imported `pkg__Err` cases. Native recovery lowering assigns a joined recovery binding into the hoisted checked join local rather than shadowing it.
- Native/Fast Debug parity passes for if/else, match, try, recover-binding, local payload, parenthesized recovery, and imported enum recovery fixtures. J3's return-branch example and P4 named payload pattern are checker-rejected and remain unchanged.

## W3 — Char

- Added explicit NUL defaults for Char in Fast Debug domain state and native Rust lowering. The accepted default-state fixture previously failed in Fast Debug and emitted NUL natively; it now has parity.
- The scalar Char raise probe is rejected by the checker with `RAISE_PAYLOAD_NOT_OWNED_BOUNDED`; the raise identity site is unchanged to preserve checker acceptance.

## W4 — hook and report hygiene

- Updated the Phase 27.5 report title from “in progress” to “delivered”.
- Test hook gating is skipped: `./moss` is byte-identical to `build/compiler/moss-dev`, which `make check` uses, while `moss-release` is copied from `moss-opt`, also used by `make check`. The suite and release entrypoints therefore share hook-bearing compiler binaries.

## Validation state

- `MOSS_EST=off make --jobs="$(nproc)" check` passed all regression jobs. `MOSS_EST=shadow make --jobs="$(nproc)" check` passed all regression jobs. Both include the Phase 27.5.1 fixture driver and three-engine check.
- The driver passed against both `moss-dev` and `moss-opt`. After the full suites, a test-only follow-up made `MOSS_TEST_DISABLE_GUARD_HOOKS=1` prevent fact injection; the normal driver passed and the hook-off removal check failed as expected. Compiler source was unchanged after the full suites.
- Performance against first parent `1c9489aaac40a4dc9386c4e1257fc02474fbf433` (candidate `047d1dc04f99036cc2c07e6fe3bad4287ac71937`), 77 fixtures, 3 interleaved repetitions: aggregate median 5.572 s → 5.650 s (+1.4%). No fixture crossed the report's beyond-noise threshold. Full report: `tmp/compiler-perf/20261031-141231/LOCAL_REPORT.md`.
- Agent 2 review, merge, and tag remain pending.
