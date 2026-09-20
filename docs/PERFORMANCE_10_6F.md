# Phase 10.6F performance validation

Measured on 2026-09-20, starting from `202d9c7` (checked-in 10.6E plus its
README/status correction). These are observations on one machine, not language
performance guarantees or CI thresholds. The final backend keeps the same stored
SynchronizationPlan, safe Rust class storage, guard order/lifetime, borrowed READs,
and semantic message/reply copies.

## Environment and method

- CPU: 13th Gen Intel Core i3-1315U; x86_64; eight CPUs in process affinity.
- OS: Linux 6.6.141-09476-g954adab60416-x86_64, glibc 2.41. CPU quota information
  was not exposed at `/sys/fs/cgroup/cpu.max`; physical capacity is not inferred
  from affinity alone. Frequency, mixed cores, and scheduler placement add noise.
- Rust: `rustc 1.98.1 (48a229cea 2026-09-01)`; Moss `-O`; generated Rust
  `--edition=2021 -C opt-level=3 -D warnings`, without instrumentation/test cfg.
- Seven repetitions; 20,000 operations/caller (128 for large copy cases); rotated
  Moss/coarse/handwritten order within repetitions. Caller counts 1/2/4/8.
- The before and after runtime suites ran sequentially, with no compiler build or
  scaling stress intentionally running alongside. Results include harness dispatch,
  black-box, barrier/join overhead. p95 below is the maximum of seven run averages,
  not a per-request tail latency. No arbitrary Rust-relative acceptance band exists.

[Reproduction commands and logical equivalence](../benchmarks/synchronization/README.md)
are checked in. Coarse locking exists only in the benchmark. It is an RwLock,
so shared readers remain compatible, but independent writes share one lock.
The handwritten baseline uses direct typed field locks and the same logical work,
including ownership-independent copies at message/reply boundaries.

## Uncontended costs and representative Rust comparison

Nanoseconds per operation, one caller, 16-byte/String or 16-element/Vector input.
“Rust” means the straightforward typed implementation, not another Moss backend.

| Case | Moss median | Moss p95 | Coarse median | Rust median |
| --- | ---: | ---: | ---: | ---: |
| empty | 81.1 | 105.4 | 5.3 | 6.0 |
| immutable | 121.7 | 174.2 | 10.1 | 6.9 |
| shared | 167.2 | 261.7 | 34.2 | 35.6 |
| write_helper | 151.1 | 332.5 | 19.2 | 20.0 |
| multiple | 330.2 | 524.7 | 20.0 | 34.9 |
| readers | 285.2 | 486.4 | 200.6 | 183.2 |
| writers | 290.0 | 486.8 | 144.3 | 161.3 |
| disjoint | 334.7 | 491.1 | 193.2 | 294.2 |
| string_read | 154.2 | 180.6 | 19.1 | 16.2 |
| vector_read | 91.4 | 173.8 | 19.5 | 19.1 |
| object_read | 215.1 | 407.2 | 36.4 | 34.1 |
| message_string | 132.9 | 237.4 | 23.6 | 34.4 |
| message_vector | 254.6 | 279.9 | 65.1 | 45.8 |
| reply_string | 98.8 | 179.4 | 40.8 | 41.0 |
| reply_vector | 115.0 | 176.7 | 39.4 | 36.9 |
| nested | 394.0 | 440.5 | 51.2 | 38.9 |
| siblings | 584.4 | 1028.5 | 68.7 | 66.9 |

A one-class protected read costs 167.2 ns versus 81.1 ns
for empty ClassSet, a difference of 86.1 ns in these different
wrappers. That difference includes descriptor lookup and state-view work and is
**not** a pure RwLock latency estimate. Two-class mutation costs 330.2 ns.
Tiny generated calls remain materially slower than typed Rust (shared read:
35.6 ns); descriptor searches, BTreeMap guard/leaf storage,
exclusive take/restore, and general view/restore scaffolding explain real fixed
costs. There is no claim that Moss is close to Rust for every tiny operation.

## Compatible concurrency and conflicting access

Aggregate million operations/second, 500 wrapping arithmetic iterations per call
inside the protected region. More callers do not imply more independent state:
disjoint has exactly **two** writable classes, so four/eight callers compete in
pairs/groups. The mixed workload has only a reader at one caller, then alternates
reader/writer lanes at higher counts.

| Workload | Callers | Moss Mops/s | Coarse Mops/s | Rust Mops/s |
| --- | ---: | ---: | ---: | ---: |
| readers | 1 | 3.51 | 4.99 | 5.46 |
| readers | 2 | 5.82 | 6.94 | 8.65 |
| readers | 4 | 7.09 | 8.12 | 7.98 |
| readers | 8 | 7.98 | 8.92 | 8.66 |
| disjoint | 1 | 2.99 | 5.18 | 3.40 |
| disjoint | 2 | 4.11 | 4.09 | 7.63 |
| disjoint | 4 | 2.59 | 2.87 | 5.67 |
| disjoint | 8 | 3.03 | 3.06 | 5.39 |
| writers | 1 | 3.45 | 6.93 | 6.20 |
| writers | 2 | 1.92 | 3.68 | 3.84 |
| writers | 4 | 1.90 | 2.69 | 2.98 |
| writers | 8 | 1.87 | 2.66 | 2.88 |
| reader_writer | 1 | 3.45 | 4.18 | 4.22 |
| reader_writer | 2 | 2.39 | 3.85 | 4.66 |
| reader_writer | 4 | 1.69 | 3.25 | 3.56 |
| reader_writer | 8 | 1.62 | 3.18 | 3.33 |

Shared-reader throughput grows; disjoint two-caller throughput improves while the
coarse baseline loses throughput. Contention at four/eight callers reduces the
benefit of the two-class design. Conflicting writers and mixed access serialize
where required; additional callers add contention rather than logical parallelism.
The Moss metadata/storage overhead can outweigh exposed parallelism on very small
workloads: the coarse baseline can still be faster. No universal speedup is claimed.

The optional instrumented run independently observed maximum overlap **4** for
shared readers, **2** across disjoint writer classes, and **1** for conflicting
writers. Writer/read coexistence on the same class is asserted impossible in the
collector, using before-release events to avoid counting already-unlocked guards.
Existing barrier-based 10.6D regressions remain the timing-independent proof of
compatible overlap.

Instrumented totals below cover 16,000 calls per workload. Observer overhead and
benchmark-only overlap atomics distort absolute durations; production throughput
above is measured separately. “Contended” means try-lock returned WouldBlock.

| Workload | Contended acquisitions | Mean wait ns/call | Mean hold ns/call |
| --- | ---: | ---: | ---: |
| readers | 0 | 404.5 | 838.9 |
| writers | 4336 | 5799.4 | 1713.0 |
| mixed | 9125 | 2910.6 | 1284.6 |
| disjoint | 11522 | 1023.6 | 980.6 |

## Borrowed READ and legitimate independent copies

Median ns/op. String sizes are bytes; Vector sizes are element counts (eight bytes
per i64). These reads do O(1) useful work: emptiness/first element, or a whole-object
READ helper projecting the first element. They do not scan entire payloads.

| READ case | 16 | 4,096 | 1,048,576 |
| --- | ---: | ---: | ---: |
| string_read | 154.2 | 108.6 | 121.0 |
| vector_read | 91.4 | 116.5 | 121.8 |
| object_read | 215.1 | 189.7 | 239.0 |

No size-proportional snapshot cost appears across a 65,536-fold size increase.
Noise in these short calls is substantial; the D.1 non-Clone/clone-counter and
backing-address tests provide stronger evidence of the zero-copy invariant.

Explicit copy boundaries do scale with payload size. At the largest input:

| Boundary | Moss median us | Coarse median us | Rust median us |
| --- | ---: | ---: | ---: |
| message_string | 53.62 | 44.13 | 43.67 |
| message_vector | 387.62 | 372.84 | 366.42 |
| reply_string | 55.05 | 58.49 | 49.92 |
| reply_vector | 362.81 | 352.25 | 351.96 |

At large sizes, allocation/copy dominates and Moss is much closer to the equivalent
Rust baselines. This is legitimate independent-value cost, not a reason to alias
payloads or replies to domain state.

## Nested lock-hold amplification

Maximum measured nested depth is **3**. Means below come from 2,000 instrumented
parent calls each; both parents hold one class, so hold time is directly comparable
to wall-clock handler duration. Local/runtime is handler time minus child-frame
time; it includes setup/restoration/instrumentation, not just source work.

| Parent | Wait us | Hold us | Handler us | Nested us | Local/runtime us |
| --- | ---: | ---: | ---: | ---: | ---: |
| Nested | 0.090 | 1.827 | 2.035 | 1.331 | 0.704 |
| Siblings | 0.095 | 2.552 | 2.769 | 1.956 | 0.813 |

Descendant time is a substantial part of ancestor lock hold time (roughly three
quarters here). This is the intended baseline architecture. No early unlock or
multi-domain transaction semantics were introduced. Sibling-after-return order
remains legal and is exercised by both engines and the physical harness.

## Measured fixes and deferred costs

Before measurement, no storage/locking optimization was applied. Generated Rust
inspection identified redundant self-handle and route-handle Arc clones on every
handler entry, including handlers that never sent a message. A benchmark-only
borrow transformation was measured first. The production fix makes private working
route fields `&'a RouteRef` and uses borrowed `self_ref`; those borrows cannot outlive
the wrapper. Handles in the runtime still own their routes. Message/reply values,
class locks, and guard lifetimes are unchanged. No unsafe or new synchronization
proof is needed.

The uninstrumented before/after medians support removing that fixed work, but host
variation and broad ranges rule out a precise universal speedup claim:

| Case | Before ns | After ns | After min–max ns |
| --- | ---: | ---: | ---: |
| empty | 213.6 | 81.1 | 74.3–105.4 |
| immutable | 244.4 | 121.7 | 81.0–174.2 |
| shared | 310.0 | 167.2 | 132.0–261.7 |
| write_helper | 560.0 | 151.1 | 146.9–332.5 |
| multiple | 744.4 | 330.2 | 260.4–524.7 |
| nested | 545.1 | 394.0 | 382.9–440.5 |
| siblings | 633.0 | 584.4 | 489.3–1028.5 |

Runtime-shaped class objects are **40 bytes**, alignment **8** on this toolchain;
adjacent classes can share an assumed 64-byte cache line. An isolated nine-run
probe measured packed median **9.12 ns/op** versus **9.11 ns/op**
with 128-byte separation. This does not establish absence of false sharing on
other hardware/workloads. It does establish no consistent benefit here while
padding raises each slot from 40 to 128 bytes. **No production layout/padding
change was made.** BTreeMap node placement is also allocator-dependent.

| Observation | Classification | Decision |
| --- | --- | --- |
| Redundant private handle clones | Fixed in 10.6F | Borrow private routes/self; preserve all value/lock semantics. |
| Missing rustc transitive crate-name discovery | Fixed in 10.6F | Write native rlib aliases for local/source-free providers; clean ledger regression. No module redesign. |
| Tiny-handler metadata, guard maps, take/restore, absent-slot/restore scaffolding | Deferred optimization | Material fixed overhead; future typed/static layout work needs wider measurements and correctness coverage. |
| Possible false sharing | Deferred optimization | Probe provides no padding benefit; retain compact layout. |
| Nested ancestor hold amplification | Deferred optimization | Intentional 2PL retention; early unlock needs a separate equivalence proof. |
| Explicit large payload/reply copy cost | Deferred optimization | Independent value semantics are mandatory; no alias shortcut. |
| Cross-module generic/structural dispatch and qualified statement-call friction | Phase 15 usage issue | Use concrete exported helpers/factories; record failing forms rather than redesign modules. |
| Trace growth, limited interpreter traversal/library surface | Phase 15 usage/tooling issue | Keep bounded events; future slicing/coverage guided by real programs. |

## Compiler/plan scaling and determinism

Five repetitions, optimized C++ compiler; median milliseconds. The native Rust
compiler is **not** included. Named-stage times are approximate and exclude other
frontend passes. Each size also checks identical generated Rust and introspection
on repeated runs, including domain/class ranks, membership, ClassSets, and layout.

| Leaves/store | Handlers/store | Instances / routes | Total classes | Effects ms | Graph ms | Plan ms | Rust emission ms | Total ms | Rust bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 16 | 2 / 1 | 8 | 0.174 | 0.027 | 0.297 | 0.451 | 9.42 | 80,873 |
| 32 | 64 | 8 / 4 | 128 | 0.662 | 0.085 | 3.580 | 2.947 | 47.66 | 852,748 |
| 96 | 192 | 24 / 12 | 1152 | 3.789 | 0.412 | 75.430 | 29.388 | 379.59 | 7,062,780 |

The signature matrix and plan derivation remain usable at these sizes; this is not
an asymptotic guarantee. Seven megabytes of generated Rust at the largest case
shows a real code-size concern from per-handler general state-view scaffolding.
Do not infer native compile-time scalability from the C++ emission timings. Static
projection/code-size reduction remains future backend work; no analysis was weakened.

## Fast Debug, module seed, and trace size

The expanded equivalence corpus covers the ledger, index, nested/sibling calls,
exact specializations, payload/reply independence, primitive/nested/helper/method
mutation, ordinary values, output order, and final state exposed through replies.
The clean four-module ledger builds/runs and directly interprets to the same output;
its typed model also works as a source-free production provider. Structural trait
specialization is separately exercised by the existing traits seed; cross-module
generic use remains limited. Functional analytics has its own production seed,
because Fast Debug pipeline execution is still unsupported.

The optional index trace-size sample performs five deterministic runs per size.
It checks expected output and exact trace hashes, not a timing threshold:

| Messages (including final reads) | Events | Trace bytes | Median elapsed ms |
| ---: | ---: | ---: | ---: |
| 42 | 617 | 148,438 | 12.14 |
| 402 | 6017 | 1,455,769 | 59.17 |
| 4002 | 60017 | 14,611,670 | 497.99 |

Event volume is linear rather than an aggregate-dump explosion, but about 14.6 MB
for 4,002 messages is substantial tooling traffic. Keep tracing opt-in; trace
slicing is a real future need. Event bounds and deterministic identities are
preserved; no trace-query language or interpreter optimization was added.

## Validation and release scope

The complete correctness suite, generated Rust warnings-denied builds, repeated
2PL/borrowed-read harnesses, module/source-free cases, Fast Debug equivalence,
functional/dataflow, agent/tooling, and Emacs checks are recorded in
[CURRENT_STATUS](../.codex/CURRENT_STATUS.md). Benchmark construction/instrumentation
has structural regression coverage without timing gates. No unsafe, scheduler,
new ingress syntax, lock elision, early unlock, atomics, scopes, or supervision was
introduced. The next milestone is **Phase 15 Dogfooding**, not interoperability.

## Final architecture terminology audit

The tracked source, tests, examples, editor/tooling, active docs, and `.codex`
records were searched for old version labels, await/spawn, mailbox/worker-loop,
FIFO/commit-order, coalescing/cluster/atomic backends, and Phases 11–13.
Remaining matches are classified as:

- **Valid unrelated use:** application names, ordinary collections, Rust caller
  threads in isolated benchmark/correctness harnesses, and compiler terminology.
- **Negative regression:** retired-syntax rejection, editor warning highlighting,
  and assertions that legacy generated implementation constructs are absent.
- **Historical/superseded:** explicitly labelled `.codex` checkpoints and design
  decisions, the historical error example, removal descriptions, and the 10.6E
  retirement handoff. Old v0.2 and phase-number references remain only as history.
- **Stale contradiction:** corrected in active README/version banners/JSON examples
  and status/roadmap. No selectable alternate domain backend remains.

The active milestone is v0.1; next is Phase 15 Dogfooding, then Phase 20 Rust
Interoperability, Phase 21 Error Propagation & Supervision, and Phase 22 Agent
Agency Tooling. No release/tag or Phase 15 feature work is part of this change.
