# Phase 10 synchronization performance suite

These are opt-in optimized production measurements, separate from correctness CI.
No nanosecond or throughput result is a pass/fail requirement.

From the repository root:

```sh
python3 benchmarks/synchronization/run.py --out tmp/106f/bench --repeats 7
python3 benchmarks/synchronization/instrument.py --out tmp/106f/instrument
python3 benchmarks/synchronization/scaling.py --out tmp/106f/scaling --repeats 5
python3 benchmarks/synchronization/trace_size.py --out tmp/106f/traces
rustc --edition=2021 -C opt-level=3 -D warnings benchmarks/synchronization/layout.rs -o tmp/106f/layout
tmp/106f/layout
```

`run.py` compiles `workload.moss` with Moss `-O`, then builds a Rust caller harness
with `-C opt-level=3 -D warnings`. Production throughput has **no instrumentation**
or Rust test configuration. Thread creation precedes the start barrier; the timed
interval includes start/join overhead, amortized across operations. Workloads use
20,000 calls per thread (128 for megabyte copy cases), seven repetitions, and
rotating implementation order. Outputs include median, nearest-rank p95, range,
and aggregate operations/second. With seven repetitions, p95 is the maximum;
it is not a per-request tail-latency distribution. Raw output stays under `tmp/`.

Concurrent cases use 1/2/4/8 callers up to available CPU affinity. They perform
500 wrapping arithmetic iterations under the guard. Results depend on CPU quota,
heterogeneous cores, scheduling, clocks, and contention policy; no fairness or
scaling guarantee is implied. Tiny call costs also include harness dispatch and
black-box overhead. The baseline has the same logical data and work:

- `moss`: generated planned entry/view/body/restore/release code.
- `coarse`: benchmark-only whole-state RwLock; shared readers can overlap, while
  disjoint writes deliberately serialize. Empty/immutable cases need no lock.
- `rust`: straightforward typed per-field RwLocks and direct state access.

Copy cases establish independent String/Vector payload/reply values in every
implementation. Borrow cases inspect length/first element, including a whole-object
READ helper, so increased input size does not add useful scanning work. Inputs are
16, 4,096, and 1,048,576 String bytes / Vector elements. Destruction of replies is
included. The typed Rust baseline does not reproduce Moss's descriptor/view maps;
that difference is the overhead being measured, not omitted logical work.

`--borrow-routes` is a reproducible benchmark-only transformation for evaluating
the old compiler's redundant private route/self Arc clones. It does not alter
locks or copy boundaries. The selected fix is now in the production generator.
A previous compiler can be supplied through `--compiler` to repeat the comparison.

## Optional physical instrumentation

Generated runtime hooks are enabled with internal Rust `--cfg moss_perf` (off by
default), or exposed to existing correctness harnesses under `cfg(test)`. The perf
configuration uses `try_read`/`try_write` to identify actual `WouldBlock` events,
then the same blocking acquisition, order, and lifetime. Poison remains fatal.
Events are `handler_enter`, `lock_acquire`, `lock_contended`, `lock_acquired`,
`handler_complete`, `lock_releasing`, `lock_release`, and `handler_exit`, carrying
stable instance/handler identities, ranks, and mode.

The optional collector uses thread-local counters/timestamps and a current nested
frame stack. It requires no global event lock. Benchmark-only atomics check active
class overlap; final records are emitted after work, not per event. Wait spans
request→acquired; hold spans acquired→released. Handler time includes acquisition,
view setup/body/restoration/release. Child handler time is charged to the parent
as nested time; handler minus nested time includes local work and runtime overhead.
Hold durations summed across multiple classes are class-nanoseconds, not a single
wall-clock critical-section duration. Instrumented times include observer overhead
and are never substituted for uninstrumented throughput.

## Layout and compiler scaling

`layout.rs` compares adjacent runtime-shaped RwLock<BTreeMap> objects with a
benchmark-only 128-byte aligned wrapper. It reports sizes, alignment, and whether
adjacent objects share an assumed 64-byte line. This is an interference probe,
not a hardware cache-line query or a proposed production layout.

`MOSS_PROFILE_COMPILER=1` writes opt-in stage times to stderr: effects, concrete
graph, synchronization plan, and Rust generation. It does not enter semantic JSON,
provider ABI, or caches. `scaling.py` grows leaves, handlers, classes, instances,
and routes together; stage costs include their ordinary checks. Wall time includes
other frontend work. Repeated Rust/introspection equality checks ranks, membership,
ClassSets, layout, and diagnostic determinism. Measurements are approximate and
are not an asymptotic-complexity claim.

The correctness suite invokes tiny benchmark construction and instrumentation
checks; it never asserts a performance threshold. See
[the recorded report](../../docs/PERFORMANCE_10_6F.md) for results and deferred costs.
