# Phase 21 coordination register

Updated: 2026-10-09 (America/Los_Angeles)

## Frozen preparation baseline

- Base commit: `ed0b3d23596e189dec7c259319dd3d570a2120b0`
- Base branch: `main`
- Base status: clean and synchronized with `origin/main`
- Language contract: `moss-0.1` from `./moss agent bootstrap --json`
- Design: `docs/MOSS_PHASE_21_ERROR_HANDLING.md`, consolidated v4.5.2
- Handoff: `docs/MOSS_PHASE_21_IMPLEMENTATION_HANDOFF.md`
- Baseline `make check`: PASS, all Moss v0.1 tests
- Baseline `make examples`: PASS, all positive examples; intentional negative skipped

## Worktree and branch register

| Workstream | Branch | Absolute worktree | Base | State |
|---|---|---|---|---|
| Coordinator | `phase-21-planning` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-planning` | `ed0b3d2` | active, clean before this register |
| 21.0 shared contracts | `phase-21-0-effects-abi` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-0` | `ed0b3d2` | ready, clean |
| 21.A recovery | `phase-21-a-recovery` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-a` | pending approved 21.0 SHA | not created |
| 21.B FileIO | `phase-21-b-fileio-errors` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-b` | pending approved 21.0 SHA | not created |
| 21.C Branch errors | `phase-21-c-branch-errors` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-c` | pending approved 21.0 SHA | not created |
| 21.D language parity | `phase-21-d-language-parity` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-d` | pending approved 21.0 SHA | not created |
| Integration | `phase-21-integration` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-integration` | pending approved integration base | not created |

A-D remain uncreated until user approval of the reviewed 21.0 commit. They must all branch from that same approved commit.

## Shared interface ownership map

21.0 owns the shared contracts and focused tests only. Its expected implementation surface is:

- `src/functional_ir.hpp`: replace the coarse `may_fail` bit with `may_panic`, normalized variant-precise `raise_set`, merge/removal semantics, unchanged ordinary `fusion_safe()`, and separate `chunk_speculation_safe()` contract.
- `src/ast.hpp` and semantic records in `src/moss.cpp`: carry concrete raise alternatives, tagged outcomes, failure-arm metadata, captures, reply contract, and exceptional-CFG records without implementing the full Phase 21 grammar.
- `.mossi` parsing/emission and semantic identity in `src/moss.cpp`: ABI v7 fail-closed schema, deterministic serialization/hashing, provider body/failure-body records, and missing-metadata rejection.
- `src/handler_lowering.inc`, `src/handler_runtime.hpp`, and synchronization interfaces: concrete application-owned nested/Root wrapper signatures and ordered failure transition contract; no broad recovery semantics implementation in 21.0.
- Focused tooling fixtures/tests: concrete native signature assertions, ABI v6 rejection/rebuild behavior, and a source-free provider whose source is removed before the consumer build.
- Contract documentation: explicit C++/Rust signatures, ownership rules, error identity, provider-private payload/capture handling, exceptional CFG consumers, and the corrected class-repartition rule from the independent handoff.

The current ABI entry points are `kNativeAbiVersion = 6`, `load_module_interface`, `interface_effects`, `interface_effects_text`, and `write_module_interfaces` in `src/moss.cpp`. Existing source-free handler/body precedents live in `tests/tooling/check_phase106f1.py`, `check_handler_2pl.py`, `check_synchronization_plan.py`, and `check_phase15_9_cross_package_specialization.py`.

## Phase 21.0 acceptance checklist

- [ ] Exact `ObservableEffects` contract with `may_panic` and normalized `raise_set`
- [ ] `fusion_safe()` stays a typed-raise ordering barrier
- [ ] Separate context-specific `chunk_speculation_safe()` contract
- [ ] Concrete tagged normal/raised Rust representation and owned payload ABI
- [ ] Stable multi-error identity without a source-level cross-enum union
- [ ] Separate exported handler and `on_fail` bodies with capture/reply metadata
- [ ] Application-generated nested and Root wrapper signatures
- [ ] Root order: join/quiesce, close Root-local FileIO, drop old guards, acquire fresh cleanup plan, dispatch arm, complete reply
- [ ] Exceptional CFG schema and named consumers: D7, ownership, guard cancellation, early acquisition/2PL, cleanup, codegen
- [ ] ABI v7 serialization, semantic hash identity, v6 rejection and rebuild rule
- [ ] Source-free provider fixture with provider source removed
- [ ] Focused tests pass
- [ ] Full `make check` and `make examples` pass or any unrelated blocker is documented
- [ ] Worker checkpoint commit includes title and concise body
- [ ] Coordinator independent review completed; worker fixes review defects in `21-0`

## Agent checkpoints

| Agent | Initial report | Checkpoint SHA | Review | Tests / blockers |
|---|---|---|---|---|
| 21.0 | pending | pending | pending | pending |

## Decisions and blockers

- The independent handoff clarifies that adding `h_fail` may repartition synchronization classes and change normal-handler lock counts, including exclusive acquisitions, while it does not add new normal-path per-leaf WRITE requirements. 21.0 must encode this corrected contract rather than the older shorthand in Part II §3/P10.
- Numeric token grammar and Float tie-breaking are Stage 21.D contract decisions and are outside 21.0.
- No unresolved preparation blocker. Native Codex subagent messaging is available.
