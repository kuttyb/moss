# Phase 21 coordination register

Updated: 2026-10-10 (America/Los_Angeles)

## Phase 21.0 approval and release

- Approved checkpoint: `f27b2629efa2c321bef953e62ca73d230e56f96f`
- Review disposition: independent architectural review approved Phase 21.0;
  native/provider ABI v8 and all associated shared contracts are frozen.
- Release disposition: Agents 21.A–D are authorized to implement from this exact
  checkpoint. No workstream may independently alter shared representations,
  `.mossi` records, tagged-outcome layouts, or native/provider ABI signatures.
- Integration disposition: workstreams remain isolated and use checkpoint commits;
  integration stops for human approval at the Phase 21 checkpoint before any merge
  to `main`.
- Continuity recovery: the coordinator resumed the existing worktrees after an
  interrupted session. A–C's in-progress, uncommitted changes were preserved and
  their native subagents relaunched in place; D remains queued for the first
  available native worker slot. No worktree was reset or rebased.

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
| 21.0 shared contracts | `phase-21-0-effects-abi` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-0` | `ed0b3d2` | ABI v8 checkpoint `f27b2629`; independently approved and frozen |
| 21.A recovery | `phase-21-a-recovery` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-a` | `f27b2629` + shared FileIO-origin checkpoint `f7c769c` | checkpoints `29d13c4`, `cd9b26c`, `299fdf3`; focused semantics/trailer/bare-raise tests pass; generated outcome, imported bridge and Root cleanup remain open |
| 21.B FileIO | `phase-21-b-fileio-errors` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-b` | `f27b2629` + `f7c769c` | checkpoints `9c3a16c`, `a47e639`; independent interior-NUL finding fixed as `IO`; focused FileIO and Phase 20 runtime gates pass; generated Root fixture awaits A wrapper |
| 21.C Branch errors | `phase-21-c-branch-errors` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-c` | `f27b2629` + B dependency checkpoint | checkpoint `c0b395f`; focused test passes; B's independent review found typed mapper/fold and non-handler propagation blockers plus production Rust test gap; remediation active |
| 21.D language parity | `phase-21-d-language-parity` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-d` | `f27b2629` | active; settled Float IEEE parity underway; numeric grammar/tie-break left unresolved for explicit decision |
| Integration | `phase-21-integration` | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-integration` | `f27b2629` | created clean; accepts reviewed checkpoints only |

A–D must all branch from the exact approved commit
`f27b2629efa2c321bef953e62ca73d230e56f96f`.
All four worktrees were verified clean at that SHA before launch. The native
subagent runtime permits three workers alongside the coordinator. Following two
usage-limit exits, A and C resumed in place, B checkpointed and released its
slot, and D launched when that slot opened. B's shared-origin checkpoint is an
explicit compiler/fileio dependency; implementation edits remain worktree-local.

## Shared interface ownership map

21.0 owns the shared contracts and focused tests only. Its expected implementation surface is:

- `src/functional_ir.hpp`: replace the coarse `may_fail` bit with `may_panic`, normalized variant-precise `raise_set`, merge/removal semantics, unchanged ordinary `fusion_safe()`, and separate `chunk_speculation_safe()` contract.
- `src/ast.hpp` and semantic records in `src/moss.cpp`: carry concrete raise alternatives, tagged outcomes, failure-arm metadata, captures, reply contract, and exceptional-CFG records without implementing the full Phase 21 grammar.
- `.mossi` parsing/emission and semantic identity in `src/moss.cpp`: ABI v8 fail-closed schema, deterministic serialization/hashing, provider body/failure-body records, imported frame bridges, and missing-metadata rejection.
- `src/handler_lowering.inc`, `src/handler_runtime.hpp`, and synchronization interfaces: concrete application-owned nested/Root wrapper signatures and ordered failure transition contract; no broad recovery semantics implementation in 21.0.
- Focused tooling fixtures/tests: concrete native signature assertions, ABI v6 rejection/rebuild behavior, and a source-free provider whose source is removed before the consumer build.
- Contract documentation: explicit C++/Rust signatures, ownership rules, error identity, provider-private payload/capture handling, exceptional CFG consumers, and the corrected class-repartition rule from the independent handoff.

The approved implementation sets `kNativeAbiVersion` from
`kErrorHandlingAbiVersion = 8`; its interface entry points remain
`load_module_interface`, `interface_effects`, `interface_effects_text`, and
`write_module_interfaces` in `src/moss.cpp`. Existing source-free handler/body
precedents live in `tests/tooling/check_phase106f1.py`,
`check_handler_2pl.py`, `check_synchronization_plan.py`, and
`check_phase15_9_cross_package_specialization.py`.

## Phase 21.0 acceptance checklist

- [x] Exact `ObservableEffects` contract with `may_panic` and normalized `raise_set`
- [x] `fusion_safe()` stays a typed-raise ordering barrier
- [x] Separate context-specific `chunk_speculation_safe()` contract
- [x] Concrete tagged normal/raised Rust representation and owned payload ABI
- [x] Stable multi-error identity without a source-level cross-enum union
- [x] Separate exported handler and `on_fail` bodies with capture/reply metadata
- [x] Application-generated nested and Root wrapper signatures
- [x] Root order: join/quiesce, close Root-local FileIO, drop old guards, acquire fresh cleanup plan, dispatch arm, complete reply
- [x] Exceptional CFG schema and named consumers: D7, ownership, guard cancellation, early acquisition/2PL, cleanup, codegen
- [x] ABI v8 serialization, semantic hash identity, pre-v8 rejection and rebuild rule
- [x] Source-free provider fixture with provider source removed
- [x] Focused tests pass
- [x] Full `make check` and `make examples` pass or any unrelated blocker is documented
- [x] Worker checkpoint commit includes title and concise body
- [x] Coordinator independent review completed; worker fixes review defects in `21-0`

## Agent checkpoints

| Agent | Initial report | Checkpoint SHA | Review | Tests / blockers |
|---|---|---|---|---|
| 21.0 | `/home/kuttybanerjee/daji/moss/tmp/moss-worktrees/21-0`; `phase-21-0-effects-abi`; corrective base `d33e3be`; clean at review | `f27b2629efa2c321bef953e62ca73d230e56f96f` | complete; independent architectural review approved ABI v8 and froze shared contracts | Focused ABI/source-free strict PASS; real two-crate/source-removed bridge PASS; `make check` PASS; `make examples` PASS; strict `-Werror` build PASS |
| 21.A | `tmp/moss-worktrees/21-a`; `phase-21-a-recovery`; exact base `f27b2629`; clean at launch | `29d13c4`, `cd9b26c`, `299fdf3` | independent peer review pending | Focused checker tests pass; native outcome lowering, actual exceptional-CFG consumers, Root sequencing and imported specialization bridge remain required |
| 21.B | `tmp/moss-worktrees/21-b`; `phase-21-b-fileio-errors`; exact base `f27b2629`; clean at launch | `9c3a16c`, follow-up `a47e639` | independent review by C found/fixed NUL-path mapping; C review of Branch workstream complete | FileIO error/fault gates pass; Root cleanup runner is genuine but pending A syntax/lowering |
| 21.C | `tmp/moss-worktrees/21-c`; `phase-21-c-branch-errors`; exact base `f27b2629`; clean at launch | `c0b395f` | independent review by B: two high findings, one medium production-test gap | Read path join/order assertions pass; mapper/fold typed raises, non-handler errors, real generated Rust execution remain blockers under remediation |
| 21.D | `tmp/moss-worktrees/21-d`; `phase-21-d-language-parity`; exact base `f27b2629`; clean and verified | pending | independent peer review pending | Settled IEEE Float work started; unresolved numeric spelling/tie choice is escalated, not inferred |

## Decisions and blockers

- The independent handoff clarifies that adding `h_fail` may repartition synchronization classes and change normal-handler lock counts, including exclusive acquisitions, while it does not add new normal-path per-leaf WRITE requirements. 21.0 must encode this corrected contract rather than the older shorthand in Part II §3/P10.
- Numeric token grammar and Float tie-breaking are Stage 21.D contract decisions and are outside 21.0.
- Phase 21.0 checkpoint `f27b2629efa2c321bef953e62ca73d230e56f96f` is approved;
  ABI v8 is frozen and A–D are implementing from that exact base. Open gates are
  listed in each workstream row above and must pass before integration. The
  integrated Phase 21 checkpoint still requires human approval before any merge
  to `main`.
