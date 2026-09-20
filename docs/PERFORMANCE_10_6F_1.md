# Phase 10.6F.1 — static typed synchronization validation

Base: `94fca2c` (F implementation `a907336`). This addendum preserves the
[original F report](PERFORMANCE_10_6F.md) and replaces its deferred tiny-handler
metadata cost with a measured backend correction. No language semantics or
10.6C synchronization analysis changed. **F.1 is ready for review; Moss v0.1
requires explicit re-closure before Phase 15 begins.** No release/tag was made.

## Representation and module boundary

SynchronizationPlan remains the compiler/introspection source of truth and is
consumed only by code generation. The old `MossClassRuntime`, heterogeneous leaf
maps/enums, `MossPhysicalPlan`, handler descriptors, `MossHandlerFrame`, guard maps,
evacuated sets, restore vectors, and runtime `MossSlot` selection are removed.

A representative generated layout, with private names shortened, is:

```rust
struct AccountClass0 { balance: i64 }
struct AccountClass1 { fills: i64 }
struct AccountClass2 { limit: i64 }
struct AccountImmutable { currency: String }
struct AccountRuntime {
    class0: RwLock<AccountClass0>,
    class1: RwLock<AccountClass1>,
    class2: RwLock<AccountClass2>,
    immutable: AccountImmutable,
}
struct AccountRef { state: Arc<AccountRuntime> }
```

Each instance constructs these fields directly. Identical layouts share a Rust
type, with compiler validation of equal membership/effects/modes; distinct
materialized specializations get their own types. The application generates
all physical domain layouts, even for source-free providers. Immutable routes
remain typed handle fields, borrowed by bodies rather than cloned at entry.

A three-class entry has this shape (instrumentation and fail-closed sentinels
omitted here for readability):

```rust
let mut c0 = moss_write_or_abort(&self.state.class0);
let mut c1 = moss_write_or_abort(&self.state.class1);
let c2 = moss_read_or_abort(&self.state.class2);
let mut state = AccountThreeState {
    balance: &mut c0.balance,
    fills: &mut c1.fills,
    limit: &c2.limit,
};
let result = __moss_body_Account_Three(&mut state);
drop(c2);
drop(c1);
drop(c0);
result
```

The real generator emits direct fields in increasing class rank and releases
local guards in reverse order after the complete body. No runtime ClassSet loop,
name search, leaf-to-class lookup, map allocation, rank comparison or generic
restoration runs. Empty handlers have no locks/guard collection and an empty
zero-sized view which LLVM removes. Their fail-closed unwind sentinel remains;
it is failure policy, not synchronization planning.

READ is a direct borrowed typed field; immutable READ uses immutable storage.
WRITE uses `&mut` through a write guard, including primitive WRITE-through
helpers. Ordinary WRITE no longer evacuates state. Existing legal ownership
operations continue using their checked ordinary-call ABI; no blanket state
movement is introduced. In particular, the current checker rejects consuming a
domain-owned nontrivial binding into an ordinary local/helper, and this phase
does not relax that rule. Stored CONSUME modes still lower to EXCLUSIVE access.
Collection operations mutate their valid typed storage under the guard.

Whole/nested objects use monomorphized access traits over typed borrowed field
capabilities. Mixed capabilities can span classes, without reconstruction or
cloning. Only used top-level state appears in a handler view; an unused member
of a partial aggregate is a zero-sized absent capability, not a default value or
runtime mode tag. Message/reply boundaries still materialize independent values.
No Rust references escape into another domain or through a reply.

**No unsafe was added.** RwLocks own the protected state; safe Rust borrows are
bounded by guards. No concurrent mutable whole-domain reference exists. Parent
guards remain held across nested calls. Failure sentinels are declared after
acquired guards, so unwinding aborts before releasing them; poison aborts too.
The static `(domain_rank, class_rank)` proof, no upgrades, full-handler 2PL,
failure-free conflict serializability, and unspecified fairness are unchanged.

Native ABI is deliberately **5**. Providers export backend-private semantic
borrowed handler-body interfaces and static route contracts; aggregate/route
bodies use Rust generics, not dynamic dispatch. The final consumer emits its
own synchronized entries and typed state from its stored plan. A regression
builds a provider with no instances, removes its source, then constructs two
instances and a routed caller. Private provider helpers still work through the
compiled body. `.mossi` contains no lock layout, borrow layout or Rust lifetimes.
Old ABI providers must be rebuilt. Fast Debug's logical state and execution
engine are unchanged.

## Measurement method

Measurements were run sequentially after correctness/build activity finished,
on the same i3-1315U Linux environment as F: 8 CPUs in affinity, Linux
6.6.141-09476-g954adab60416-x86_64, glibc 2.41, rustc 1.98.1
(48a229cea 2026-09-01). CPU affinity is not a CPU-capacity guarantee. Mixed cores,
frequency changes and scheduler noise remain visible, especially in the original
short threaded samples. No timing is a CI pass/fail threshold.

Both compilers use `-O`; generated Rust uses `--edition=2021 -C opt-level=3
-D warnings`. Normal throughput runs have neither `cfg(test)` nor `moss_perf`.
The old executable was saved before source edits. Commands:

```sh
python3 benchmarks/synchronization/run.py --compiler OLD --out tmp/106f1/f-before --repeats 7
python3 benchmarks/synchronization/run.py --out tmp/106f1/f-after --repeats 7
python3 benchmarks/synchronization/typed.py --compiler OLD --out tmp/106f1/typed-before --iterations 1000000 --repeats 11
python3 benchmarks/synchronization/typed.py --out tmp/106f1/typed-after --iterations 1000000 --repeats 11
python3 benchmarks/synchronization/scaling.py --compiler OLD --out tmp/106f1/scaling-before --repeats 5 --rustc-repeats 3
python3 benchmarks/synchronization/scaling.py --out tmp/106f1/scaling-after --repeats 5 --rustc-repeats 3
python3 benchmarks/synchronization/instrument.py --out tmp/106f1/instrument
```

`OLD` denotes the retained base compiler, not a second runtime in Moss. The
new tight-loop harness alternates Moss/Rust ordering over eleven samples with
one million calls each. Both sides use `std::sync::RwLock` with poison-abort
helpers and identical logical work. It reports run-average ns/call, not request
tail latency. The original harness retains 20,000 calls/thread, seven rotated
repetitions, 1/2/4/8 callers, and 128 iterations for large explicit copies.
Thread creation is outside timing; barrier/join costs remain amortized inside it.

## Tight-loop performance gate

Nanoseconds/call, medians. Rust is the contemporaneous handwritten baseline in
the new run. Old and new use exactly the same F.1 fixture/harness.

| Case | Old Moss | Typed Moss | Rust | Typed − Rust | Typed / Rust | Old / typed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| empty | 36.743 | 0.710 | 0.571 | +0.140 | 1.245 | 51.72 |
| shared | 65.682 | 11.782 | 11.652 | +0.129 | 1.011 | 5.57 |
| exclusive | 130.516 | 11.647 | 11.613 | +0.034 | 1.003 | 11.21 |
| two | 199.754 | 20.947 | 20.301 | +0.645 | 1.032 | 9.54 |
| mixed | 158.855 | 21.639 | 21.171 | +0.468 | 1.022 | 7.34 |
| three | 244.104 | 30.120 | 29.382 | +0.738 | 1.025 | 8.10 |

The metadata-driven multi-fold overhead is gone. One through three classes are
in the same cost class as equivalent handwritten RwLock code. The empty-loop
ratio needs its absolute context: less than 0.15 ns/call separates those medians.
It is a near-empty compiler/harness measurement, not a claim that a real service
request takes less than a nanosecond.

| Case | Typed min–max ns | Rust min–max ns |
| --- | ---: | ---: |
| empty | 0.676–1.482 | 0.544–0.600 |
| shared | 11.540–12.519 | 11.480–12.128 |
| exclusive | 11.498–12.174 | 11.471–12.425 |
| two | 20.495–21.687 | 20.213–21.092 |
| mixed | 21.456–22.714 | 21.056–21.957 |
| three | 29.850–32.166 | 29.070–30.637 |

Optimized assembly roots `probe_empty`, `probe_read`, `probe_write`, `probe_two`,
`probe_mixed`, and `probe_three` contain or tail-call direct typed wrappers. Their
reachable generated implementations contain no map operations, string comparisons,
heap allocator calls, descriptor loops, or Arc reference-count increments.
The protected hot paths show native lock atomics, direct field loads/stores,
unlock, and poison/unwind checks. Native RwLock contention/wakeup paths remain
ordinary standard-library calls. No custom allocator/unsafe test hook was needed:
source structure and the reachable optimized assembly independently verify no
synchronization metadata allocation on the tiny paths.

The remaining small Rust delta is consistent with the extra Arc indirection,
fail-closed sentinels/panic-count checks and ordinary call/codegen differences.
We do not assign an exact number of nanoseconds to each without separate profiling.
No substantial unexplained fixed gap remains in these microcases; no padding,
lock elision, early unlock, unsafe access or new synchronization policy was used.

## Original Phase F harness rerun

The original historical F medians were approximately 81/167/151/330 ns for
empty/shared/primitive-WRITE/two-class. They are preserved in the F report. This
fresh paired rerun uses its unchanged workload and measurement structure:

| Case | Old Moss ns | Typed Moss ns | Rust ns | Typed − Rust ns | Typed / Rust |
| --- | ---: | ---: | ---: | ---: | ---: |
| empty | 89.686 | 3.384 | 3.400 | -0.016 | 0.995 |
| shared | 107.754 | 14.552 | 17.429 | -2.877 | 0.835 |
| write_helper | 307.667 | 19.437 | 16.490 | +2.947 | 1.179 |
| multiple | 698.983 | 42.922 | 41.527 | +1.395 | 1.034 |
| nested | 388.283 | 38.055 | 46.414 | -8.359 | 0.820 |
| siblings | 586.726 | 49.125 | 50.081 | -0.956 | 0.981 |
| string_read | 91.377 | 18.364 | 17.340 | +1.024 | 1.059 |
| vector_read | 240.165 | 13.742 | 17.237 | -3.495 | 0.797 |
| object_read | 227.073 | 23.526 | 20.520 | +3.006 | 1.146 |

Absolute short-sample numbers vary substantially between the historical F run,
this old-compiler rerun, and contemporaneous baselines. The tight-loop results
above provide the cleaner overhead comparison. Neither table is a universal
speed claim.

Aggregate throughput, million operations/sec, with 500 arithmetic iterations per
call under the guard:

| Workload | Callers | Typed Moss | Coarse RwLock | Handwritten fine Rust |
| --- | ---: | ---: | ---: | ---: |
| readers | 1 | 8.33 | 4.17 | 3.74 |
| readers | 2 | 9.10 | 7.51 | 6.77 |
| readers | 4 | 10.73 | 7.24 | 9.32 |
| readers | 8 | 10.17 | 8.69 | 8.09 |
| disjoint | 1 | 6.65 | 7.83 | 7.45 |
| disjoint | 2 | 10.33 | 3.13 | 7.22 |
| disjoint | 4 | 5.76 | 2.89 | 4.28 |
| disjoint | 8 | 7.58 | 2.63 | 4.95 |
| writers | 1 | 5.60 | 4.65 | 5.48 |
| writers | 2 | 3.18 | 4.65 | 4.05 |
| writers | 4 | 4.21 | 2.73 | 3.01 |
| writers | 8 | 3.40 | 2.69 | 2.91 |
| reader_writer | 1 | 10.29 | 4.12 | 7.46 |
| reader_writer | 2 | 8.69 | 3.33 | 2.98 |
| reader_writer | 4 | 4.81 | 3.43 | 3.06 |
| reader_writer | 8 | 4.14 | 3.30 | 2.99 |

The two-class disjoint workload now shows a measured advantage over coarse
locking at two callers (10.33 versus 3.13 Mops/sec). That is evidence for this
workload, not a universal speedup. Four/eight callers share only two writable
classes, so they also contend. Shared-reader overlap is preserved, but a coarse
RwLock supports compatible readers too: reader scaling alone does not establish
an advantage from partitioning. Conflicting operations remain mutually exclusive.
Rust/Moss arithmetic loop codegen, lock layout and run noise can differ despite
equivalent logical work; the wider table does not isolate those factors.

| Ordinary borrowed READ | 16 | 4,096 | 1,048,576 |
| --- | ---: | ---: | ---: |
| string_read ns | 18.364 | 12.987 | 17.193 |
| vector_read ns | 13.742 | 20.480 | 18.842 |
| object_read ns | 23.526 | 35.271 | 43.777 |

There is no size-proportional deep-copy cost. These short samples include noise
and cache effects; the original-storage/non-Clone regressions provide the stronger
no-copy evidence. Explicit large message/reply copies remain intentionally costly:
1 MiB String message/reply medians are about 71/51 microseconds; a vector of
1,048,576 i64 elements is about 373/375 microseconds. Semantic independence was
not traded away for speed.

Optional instrumentation observed four simultaneous shared holders, two across
disjoint writer classes, and one conflicting writer. Reader/writer exclusion is
asserted by the collector; barriers in the correctness suite remain the decisive
non-timing proof. Nested parent mean hold time was 1,318 ns, of which 1,052 ns
was descendant time; siblings were 1,905/1,528 ns. Maximum call depth remained
three. These observer-inflated measurements show intentional ancestor hold
amplification; they must not be compared with uninstrumented handler latencies.

## Generated size and compilation

Five C++ samples and three complete optimized rustc compile/link samples per
size, with byte-identical Rust and introspection verified across repetitions.
The stress program executes a representative route/read; it does not invoke every
emitted handler. Rust may eliminate unreachable code. No new analysis was added.

| Leaves / handlers per Store | Instances / classes | Rust bytes old → typed | C++ total ms old → typed | Rust generation ms old → typed | rustc ms old → typed |
| --- | --- | ---: | ---: | ---: | ---: |
| 8 / 16 | 2 / 8 | 80,873 → 57,511 | 10.58 → 9.60 | 0.36 → 0.34 | 887.99 → 244.83 |
| 32 / 64 | 8 / 128 | 852,748 → 221,548 | 52.52 → 52.06 | 3.33 → 1.82 | 2632.42 → 216.91 |
| 96 / 192 | 24 / 1152 | 7,062,780 → 728,572 | 385.20 → 357.92 | 30.01 → 16.78 | 37354.23 → 535.73 |

Typed output is smaller here because it removes per-handler full-state setup,
restoration scaffolding and repeated runtime descriptors, while identical layouts
share types. At the largest case, plan derivation remained about 76/75 ms before/
after; the plan algorithm is unchanged. The dramatic rustc reduction is specific
to these previously large generated scaffolds, not a claim about arbitrary
application compile times. Static accessor/generic body volume remains something
to monitor on larger real applications.

## Validation and remaining work

- Strict C++17 `-O2 -Wall -Wextra -pedantic -Werror` build, `make check`, and
  `make examples` passed; generated Rust is compiled with warnings denied.
- A/B/B.1/C semantics, closure, exact specialization, plan corruption checks and
  deterministic introspection remained green. No analysis code changed.
- D concurrency harness repeated five times: readers/disjoint writers overlap,
  conflicts serialize, reader/writer exclusion, nested/sibling ordering, reply
  release, WRITE-through, panic/poison abort, and exact specialization all pass.
- D.1 non-Clone/clone-counter/lifetime/original String-Vector backing storage,
  whole/nested/helper/method/generic/functional and independent value boundaries
  pass against the typed representation.
- Source/source-free modules, provider compiled without instances, transitive
  ledger, Fast Debug equivalence/traces, functional/dataflow, agent/schema, tooling,
  and all 20 Emacs ERT tests pass. Editor byte compilation passes with warnings
  treated as errors. Live LLDB/DAP remains capability-skipped: process tracing is
  unavailable. Existing object-map/objdump tests pass.
- New F.1 structural/layout/acquisition/no-allocation assembly checks pass.
  `sh -n tests/run.sh` and Git whitespace checks pass.

No language, synchronization or unsafe design debt was introduced. The generic
runtime is not retained as an alternate backend. Existing module ergonomics,
Fast Debug functional/for coverage and trace-slicing limitations from F remain;
this change does not begin Phase 15 or Phase 20. Minor call/panic-check overhead,
static accessor code volume, explicit-boundary copy costs and layout tuning are
future measurement topics, not justification for runtime plan interpretation.

Phase 10.6F.1 replaces the generic metadata-driven synchronization runtime with
static typed synchronization lowering. Each concrete domain instance now owns
compiler-generated `RwLock<ClassState>` synchronization classes, and every handler
emits direct statically known shared/exclusive lock acquisitions against those
fields. SynchronizationPlan remains the compile-time source of truth but is no
longer interpreted on the production hot path. Protected state is accessed
directly through typed Rust guards and borrowed views, eliminating
handler-descriptor, class-map, leaf-map, guard-map, and generic take/restore overhead
while preserving the existing handler-level 2PL and global lock-order proof.

**The structural and quantitative F.1 gates pass. Moss v0.1 is ready for review
and explicit re-closure; it has not been re-closed or published automatically.**
