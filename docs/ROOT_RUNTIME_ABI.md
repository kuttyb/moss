# Phase 20 runtime interface contract

This is the shared naming contract for the four implementation areas. The
normative behavior is in [Moss Phase 20](MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md).
Use these names across compiler lowering, generated Rust, Fast Debug, and host
integration. The request says “five” but enumerates six interface groups; all
six are included here.

## Runtime-facing semantic types

| Exact name | Contract |
| --- | --- |
| `FileIO` | Pinned, single-owner regular-file capability; synchronously borrowable, never copied or transferred. |
| `Range` | Scoped, read-only borrow of a runtime-owned FileIO buffer. |
| `RangeBatch` | Ordered, scoped result of a bounded batch read; the only collection-like holder of Ranges. |
| `Executor` | The single active root-admission and worker pool, including branches and Solo-block compensation. |
| `Root` | One independent handler execution, including synchronous nested messages and its branch join scopes. |
| `Branch` | Compiler-created child work belonging to exactly one Root and join scope; no Moss locks of its own in Phase 20. |

Runtime `Range` is distinct from Moss source `range(...)` iteration syntax.
These names do not make capabilities or handler references first-class Moss
values.

## Six reserved interfaces

### 1. FileIO runtime ABI

The source operation surface is:

```text
FileIO.open(path, mode)
file.open(path, mode)
file.read(offset, size)            -> Range
file.read([(offset, size), ...])  -> RangeBatch
file.write(offset, data)
file.sync()
file.sync(dataonly)
file.close()
file.chunks(size)
```

`mode` is `ro`, `rw`, or `create`. Operations block and use explicit offsets.
Only regular files are accepted; a second live capability for the same
`(device, inode)` fails. The runtime completes short kernel reads/writes
internally; a short returned Range establishes EOF. Buffer and batch sizes
must satisfy the static bounds in the spec. Ownership, borrowing, effects,
durability, and close behavior follow §§9–12. Console I/O stays separate.

### 2. Executor/root runtime ABI

The Moss-facing configuration chain is `Executor().threads(n).max_threads(n)
.queue_capacity(n).start()`, with optional `affinity(cores)` and
`priority(level)`. `executor.invoke(concrete_domain.Handler(args...))` submits
one one-way root from `main`. `executor.join()` drains admitted roots, stops
workers, consumes the executor, and returns ingress to inline mode. Top-level
`message` from `main` and host `runtime_invoke` use the same admission path;
nested `message` remains inside its caller's Root. Preserve fair admission,
finite `T_max`, one active executor, and no root running another root (§§3–7,
E1–E11).

### 3. RootDescriptor representation

`RootDescriptor` is the internal submission representation for one statically
resolved concrete domain instance, handler, and evaluated argument snapshot.
It is produced for `executor.invoke(...)`, top-level `message`, and generated
host ingress. It is not a Moss value, first-class handler reference, or
dynamically dispatched target. Use checked Moss semantic identity for the
target, not a generated Rust symbol. Queued descriptors obey
`queue_capacity`. The phase spec fixes these represented facts, not the Rust
field layout, byte ABI, generic order, or error encoding. All three ingress
paths must share one representation.

### 4. Branch publish/join API

Reserve `branch_publish` for bounded, nonblocking publication of compiler
branch work and `branch_join` for joining a Root's own branch subtree. If a
branch cannot be published, its Root may execute it inline; publication never
waits for a queue slot. Join helping starts only unstarted branches of that
Root's join subtree. Branches hold only compiler-scoped borrows and acquire no
Moss locks. Chunk lowering keeps at most `K` branches in flight, commits by
chunk index, and performs the original ordered fold in the parent Root (§§7,
13; R9).

### 5. Solo-block enter/leave hooks

Reserve `solo_enter` and `solo_leave` around each known Solo kernel wait,
including FileIO operations. Enter permits bounded compensation; leave lets
the same worker continue immediately without waiting for a worker slot. Pair
them on normal and failure paths. Hold no runtime-internal lock across the
wait. Moss-lock waits do not use these hooks (§15; R1–R5).

### 6. `runtime_invoke` host ABI

`runtime_invoke` is the Rust host's synchronous ingress for a statically known
handler invocation. It shares root admission, waits for completion, and returns
the handler's reply. During draining, an unadmitted host caller waits outside
Moss for inline ingress, holding no Moss lock. The host entry remains valid
after executor `join()` while the Moss runtime/program instance lives. No
dynamic handler lookup, future, result queue, or host-only Moss execution pool
belongs behind this ABI (§§5, 26.2). The spec does not fix a C ABI, Rust
function signature, or error carrier; generated typed wrappers may be needed.

## Exact ingress lifecycle

```text
INLINE → ACTIVE → DRAINING → INLINE
```

These are executor ingress states, not states of an individual Root. `start()`
holds the inline ingress gate until the current inline Root finishes, then
changes `INLINE` to `ACTIVE`. `join()` changes `ACTIVE` to `DRAINING`, closes
executor admission, waits for admitted roots, stops workers, consumes the
executor, and changes `DRAINING` to `INLINE`. Unadmitted host calls enter only
after inline admission resumes (§5.4). Use these identifiers exactly.

## Module ownership

Put runtime types, Rust emission, and ABI helpers in new `src/*.hpp` modules.
Put generator-context lowering in new `src/*.inc` modules. Touch
`src/moss.cpp` only for includes, runtime emission registration, and narrow
calls into those modules. `handler_runtime_rust()` is the existing native
registration seam; native `gen_main` and Fast Debug `run_main` are separate
entry paths that must implement the same root-ingress semantics.

Record physical signatures and layouts here once defined by their owning
implementation area from the phase spec, before other areas call them.

## Agent D compiler seams

Agent D (compiler concurrency lowering) emits no runtime of its own: no root
queue, Executor or Branch scheduler, Solo compensation, or FileIO
implementation. Generated native code names the physical ABI owned by Agent
B (FileIO/Range) and Agent C (Executor/Root/Branch), so a native build that
uses these features links against their crates.

### Executor and Roots (Agent C)

The checker records `ExecutorStartPlan` (ordered configuration calls) and,
for every `executor.invoke(...)`, a `RootSubmissionPlan` (target binding,
checked domain and handler semantic identities, backend handler selector,
formal argument types, argument snapshot expressions, one-way). Codegen
consumes only these plans:

```rust
let __cfg0 = /* threads arg */;   // each argument evaluated once, in source order
// ...
let __cfg0_checked: usize = <usize as std::convert::TryFrom<i64>>::try_from(__cfg0)
    .unwrap_or_else(|_| { eprintln!("moss executor: invalid threads ..."); std::process::abort() });
// ...
let mut executor = MossExecutor::new().threads(__cfg0_checked)/* ... */.start();
{
    let __domain = worker.clone();
    let __arg0 = /* owned value-boundary snapshot, evaluated at invoke */;
    executor.enqueue_root(MossRootDescriptor::with_target(
        next_root_id(), "handler:Worker.Process@N",
        move || { let _ = __domain.Process_shared(/* lend __arg0 */); }));
}
executor.join();
```

`executor.invoke` never lowers to a synchronous handler call. Root work is
`FnOnce() + Send + 'static`. `affinity` lowers to `Vec<usize>`, `priority`
to `i32`, and counts to `usize`. Every conversion is checked (`TryFrom`,
never `as`). A run-time value that is out of range fails closed before
`start()`.

### Top-level `message` root ingress (Agent C)

The checker marks a `message` statement directly in `main`'s body as root
ingress (`Stmt::message_root_ingress`). It lowers to a synchronous Root:

```rust
let before = {
    let __moss_root_domain_N = worker.clone();
    let __moss_root_arg_N = /* owned snapshot, evaluated once */;
    runtime_invoke(move || __moss_root_domain_N.Get_shared(/* lend __moss_root_arg_N */))
};
```

This holds in `INLINE` (before `start()`), `ACTIVE`, and again after
`join()`. A nested `message` inside a Root (handler or helper body) is never
marked. It stays a direct synchronous call within its caller's Root.
`runtime_invoke` must therefore accept `F: FnOnce() -> R + Send + 'static`.
It returns the reply.

Today the compiler emits `runtime_invoke` only in programs that construct an
Executor (`root_ingress_uses_runtime()`). Executor-free programs keep the
direct call, so they need no Agent C crate to link. That predicate is the
single switch to flip at integration. Fast Debug runs a root message
synchronously, with no scheduler.

### Chunk-pipeline Branches (Agent C)

An eligible `file.chunks(C) |> map(f) |> reduce(init, combine)` lowers per
`ChunkParallelPlan` (a compiler-constant `window_k`, currently 4) to windows
of K Branches in one join scope:

```text
acc = init                                   // before any publication
loop over windows:
    scope = branch_scope_new_current()
    for lane in 0..K:                        // K publications precede the join
        slot  = Arc<Mutex<Option<(Option<Mapped>, i64, bool)>>>   // compiler-owned
        token = moss_fileio_branch_read_borrow(&file)
        branch_publish(&scope, move || { read chunk (index * C, C) via token;
                                         map if nonempty; store outcome in slot })
    branch_join(scope)
    commit slots in index order:
        zero-length -> stop (no map/fold)
        short       -> fold once, stop
        full        -> fold, continue
        (slots after the first terminal are discarded)
```

`branch_publish` returns nothing and must not block; the runtime may run
overflow inline. Branch work is `FnOnce() + Send + 'static`. The mapped
result never passes through the scheduler. `combine` runs only in the
parent Root, left to right; there is no tree reduction.
`branch_scope_new_current()` takes no argument. The runtime associates the
scope with the Root currently executing on the calling thread; generated
code never names a Root id.

A pipeline lowers to the plain sequential loop and uses no Branch ABI when:

- its map is impure, reaches FileIO, or captures parent locals;
- its combine reaches FileIO;
- either callable reaches a source-free provider function. A `.mossi`
  interface carries no FileIO fact, so such a callable's FileIO effect is
  unknown and is never assumed FileIO-free.

Agent D's FileIO recognition is provisional and not authoritative. This
covers its typing, its `fileio`/`fileio_unknown` effect facts, and the
capability predicate. It only selects a lowering. It must not short-circuit
or replace Agent A's checker, and it is isolated behind one adapter so A's
facts can replace it.

### FileIO and Range (Agent B)

Generated calls: `FileIO::open(&str, &str)`, `file.read(i64, i64) -> Range`,
`file.write(i64, data)`, `file.sync()`, `file.sync_dataonly()` (for
`sync(dataonly)`), `file.close()`, and `Range::len() -> i64` (for Moss
`range.length()`). `file.chunks(C)` is a scoped `seq[Range]` source that is
only consumed by the chunk-pipeline lowering above, never materialized.

**Reserved seam (not yet provided by Agent B):**

```rust
fn moss_fileio_branch_read_borrow(file: &FileIO) -> MossFileIOReadBorrow;
impl MossFileIOReadBorrow { fn read(&self, offset: i64, size: i64) -> Range; }
```

A compiler-internal, owned, read-only token that a `'static` Branch can
carry instead of `&FileIO`; it must be `Send + 'static` (compile-checked by
`tests/tooling/fixtures/phase20_read_borrow_contract.rs`). It is not a Moss value and not a second FileIO
owner; it shares the open file's runtime backing, permits only reads, and
the compiler guarantees every token is dead by the window's `branch_join`.
Agent B's current `FileIO { inner: Mutex<Option<FileIOInner>> }` would need
shareable backing (for example an `Arc` inside) to implement it.

### Fast Debug

`executor.invoke` runs as a deterministic sequential schedule of deferred
roots (snapshot at invoke, run at join). A top-level `message` runs synchronously as
its own Root. FileIO execution in Fast Debug
awaits Agent A/B integration and raises a clear error.

### Test-only shims

`tests/tooling/fixtures/phase20_executor_runtime_shim.rs`,
`phase20_branch_runtime_shim.rs`, and `phase20_fileio_range_shim.rs` mirror
the signatures above so Agent D's lowering can be compiled and run in
isolation. They are never emitted by the compiler and are not a design for
Agent B/C.
