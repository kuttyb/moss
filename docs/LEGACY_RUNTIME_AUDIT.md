# Phase 10.6E retirement audit

Base: `e70943c`; production 2PL was checked in at `9f49e64` and borrowed
READs at `b3f8a64`. This record describes the Phase 10.6E change, not Phase
10.6F performance results.

## Changed-file inventory

- Compiler/runtime integration: `src/moss.cpp`, `src/ast.hpp`,
  `src/functional_ir.hpp`, `src/handler_lowering.inc`, `src/borrowed_views.inc`,
  `src/interpreter.hpp`, and new `src/interpreter_domains.inc`.
- Tests/tooling: `tests/run.sh`, `tests/phase106e_domains.moss`,
  `tests/tooling/check_phase106e_domains.py`, `check_handler_2pl.py`,
  `check_borrowed_reads.py`, `check_agent_api.py`, and both Emacs mode/test files.
- Documentation: `README.md`; docs `AGENT_API`, `AI_NATIVE_DEVELOPMENT`,
  `BUILD_SYSTEM`, `FAST_DEBUG`, `FUNCTIONAL_DATAFLOW`, `LANGUAGE_SYNTAX`,
  `MODULE_ABI`, `SEMANTIC_CONVERGENCE`, `SYNCHRONIZATION_PLAN`, `TESTING`,
  `TOOLING`, and this audit; `.codex/CURRENT_STATUS.md`, `CHECKPOINT.md`,
  `MOSS_AWAIT_HANDOVER.md`, `MOSS_DECISIONS.md`, and `MOSS_DESIGN.md`.
- Example comments: `examples/shared_memory.moss` and
  `examples/errors/await_cycle.moss`.
- Preserved tests renamed: `message_main.moss`, `main_helper_message.moss`,
  `sequential_messages.moss`, `implicit_domain_specialization_message.moss`,
  `phase4_message_result_barrier.moss`, `phase26_message_payload.moss`,
  `phase26_repeated_payload.moss`, `message_object_copy.moss`, and
  `negative/cluster_await_retired.moss`. Deletions are listed below.

## Historical 10.6E production snapshot (superseded by 10.6F.1)

This section records the implementation state at the 10.6E checkpoint. At that
point, `construct_<domain>` created per-instance class storage and direct
synchronized entries, while the generic `MossClassRuntime` and frame-bounded
borrowed views were still the active physical implementation. Phase 10.6F.1
superseded that design: production now emits typed class-owned `RwLock` fields,
direct acquisitions, and typed borrowed views; the generic runtime is no longer
used on the execution path. Plan validation, exact SHARED/EXCLUSIVE acquisition,
full-handler retention, nested rank ordering, reply release, and panic/poison
abort behavior remain authoritative.

Removed from `src/moss.cpp` and checked metadata:

- `DomainLowering`, `BackendOptimizer`, legacy handler/atomic plans, batch and
  coalesced-region descriptors, and backend-selection maps/configuration.
- Mailbox channels, pending-message envelopes, worker loops, tracker/completion
  plumbing, queued dispatch, and cluster-local alternate entries.
- Whole-domain Mutex/RwLock and atomic-domain generators, domain batching,
  coalescing, clustering, and their CLI options.
- Await IR, boundary/site/edge records, await-target/cycle analysis, effect bits,
  agent query/schema fields, and serialized await metadata.

No independently useful ordinary Queue collection, compiler graph algorithm,
functional optimization, or test-harness thread primitive was removed. No separate
source file existed solely for the old runtime; its implementation was inside
`src/moss.cpp`.

The native `.mossi` ABI is now **4**; incompatible older providers must be rebuilt.
Semantic impact snapshots are **v2** and use checked concrete route edges. Physical
lock layout and borrow/view representation remain absent from provider ABI.

## Fast Debug

`src/interpreter_domains.inc` adds interpreter-owned logical instances keyed by
checked concrete identity, with exact specialization, semantic state, and immutable
route bindings. Composition initializes those instances before ordinary execution.
Synchronous messages execute nested handler frames; replies immediately terminate
the handler. Recursive independent payload/reply values preserve value boundaries.
Helper and method READ/WRITE locations refer to caller storage, including primitive
and nested state writes; checked CONSUME behavior uses the ordinary interpreter
ownership path. The checker still rejects illegal domain-state consumption.

Domain traces add `domain_instance`, `message_call`, `handler_enter`, `state_read`,
`state_write`, `state_consume`, `reply`, `handler_exit`, and `message_return`.
Stable source/semantic/concrete/specialization/handler identities and bounded state
summaries support future trace slicing. There are no locks, schedules, addresses,
or physical class events in interpreter execution.

Transitive Moss source modules remain interpreted as one checked closure.
Source-free Moss dependencies still require production execution. Existing
unsupported interpreter functional pipelines and `for` traversal have precise
construct diagnostics. No Phase 20 foreign-call implementation was added.

## Test retirement and preservation

Deleted obsolete backend-only fixtures:

- `cluster_domain_ref.moss`, `cluster_external_ref.moss`, `cluster_mixed_fifo.moss`.
- `phase25_atomic_{bool,contention,copy_boundary,counter,effect_fallback,external_fallback,float_fallback,invariant_fallback,multi_field,swap}.moss`.
- `phase25_batch_break.moss`, `phase25_batch_targets.moss`, `phase25_batching.moss`.
- `phase25_lock_coalesce.moss`, `phase25_lock_multi_caller.moss`.
- `phase25_rwlock.moss`, `phase25_rwlock_contention.moss`,
  `phase25_rwlock_read_region.moss`.

The integer-overflow regression remains. Useful synchronous tests were renamed
from await/mailbox terminology to message terminology; the old cluster cycle
fixture is now a migration-negative await test. Retired source syntax and closed
handle/self-send/chaining rejection tests remain.

`tests/phase106e_domains.moss` and `check_phase106e_domains.py` compare compiled and
interpreted output, state observations, independent message/reply values, helper
and method writes, terminating reply, exact routes/specializations, nested/sibling
order, deterministic traces, and transitive source modules. They also reject
source-free interpretation and retired flags and inspect generated implementation
constructs for legacy runtime symbols. Emacs tests require retired spellings to
receive warning highlighting.

The existing 10.6D concurrency harness and D.1 non-Clone/borrowed-read harness are
retained, with constructor/ABI expectations updated. Each D run repeats its
concurrency harness five times, testing compatible overlap, conflicting access,
reply release, fail-closed behavior, and exact physical classes.

## Repository search classification

The audit searches implementation, active docs, examples, tests/tooling, editor
integration, and `.codex` records for await, spawn, mailbox, worker, FIFO, commit
order, coalescing, batching, clustering, and atomic domains. Remaining occurrences
are classified as follows:

| Category | Remaining uses |
| --- | --- |
| Active, unrelated | Application names such as Worker/Batch; ordinary Queue/VecDeque values; subprocess tools and Rust concurrency test threads; English “awaiting specialization.” |
| Migration/absence tests | Retired await/spawn diagnostics and negative fixtures; editor warnings; rejection of removed flags; assertions that generated runtime constructs are absent. |
| Explicit history | `.codex` design/decision/checkpoint/handoff records, historical status sections, and the historical error example. |
| Current documentation | Descriptions of what was removed and explicitly deferred future optimizations. |

Stale active await-analysis documentation, positive tests named after retired
semantics, dormant implementation paths, and constructor helpers recognizing
retired syntax were removed or corrected. Historical records are not current
language authority.

## Historical handoff from 10.6E

The subsequent [10.6F report](PERFORMANCE_10_6F.md) completes the measurements
requested by this historical handoff. At the 10.6E checkpoint, Phase 10.6F was to measure static accessor code size, class storage and absent-slot
metadata, explicit-boundary copies, uncontended handler overhead, compatible and
conflicting contention, and nested-call hold times. Diagnostics and layout choices
should follow those measurements. No performance guarantee, fairness guarantee,
lock optimization, or Phase 10.6F completion is claimed here.

Interpreter iteration/functional coverage, the pre-existing provider-internal
generic linkage limitation, package resolution, lexical domain scopes, future
concurrency ingress, and supervision remain separate work. No new Phase 20/21
semantics were introduced. Final validation results are recorded in
[CURRENT_STATUS](../.codex/CURRENT_STATUS.md).
