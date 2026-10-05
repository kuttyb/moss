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

## Process-wide FileIO runtime linkage & platform contract

### Platform Support
Two runtime modules are emitted, with different platform scope:

| Module | Emitted for | Contents | Platform |
| --- | --- | --- | --- |
| `moss_fileio` (`fileio_runtime_rust()`) | programs/providers that use `FileIO`, `Range`, or `RangeBatch` | Linux POSIX FileIO implementation | **Linux x86_64 / aarch64 only** |
| `moss_root_runtime` (`fileio_root_runtime_rust()`) | every executable root, even with no local FileIO | inode registry, fair leaf lock, Solo hook storage, process-wide symbol definitions | portable (std only, no target guard) |

Only `moss_fileio` carries the target guard:
```rust
#[cfg(not(all(
    target_os = "linux",
    any(
        target_arch = "x86_64",
        target_arch = "aarch64"
    )
)))]
compile_error!("Moss Phase 20 FileIO runtime is only supported on Linux x86_64 and aarch64.");
```
A root must define the coordination symbols because a source-free linked
provider may use FileIO. Those symbols are portable, so an ordinary Moss
program that never uses FileIO keeps its existing portability. The restriction
applies only when a crate in the program actually compiles `moss_fileio`. It is
a limit of the initial runtime implementation, not of Moss semantics.

### Process-Wide Linkage Architecture
Across all linked crates in a process (compiled provider `.rlib`s and the root executable):
- **Root Executable**: Defines the process-wide `#[no_mangle]` symbol definitions and manages the synchronized process state in `moss_root_runtime`.
- **Provider Libraries (`.rlib`)**: Consume the runtime symbols via `extern "C"` and `extern "Rust"` declarations under `moss_fileio::sys`.
- A physical change to cross-crate signatures after Phase 20 integration requires an appropriate native/provider ABI version bump and rebuild of source-free providers.

### Cross-Crate Production ABI
The only symbols a provider resolves against the executable in production:
```text
extern "C":
  moss_fileio_registry_claim(dev: u64, ino: u64) -> bool
  moss_fileio_registry_release(dev: u64, ino: u64)

extern "Rust":
  moss_solo_enter(reason: &str)
  moss_solo_leave(reason: &str)
```

### Executable-Internal Runtime Integration Helpers
Production runtime machinery inside the root executable. Not provider-facing and
not Moss semantic surface:
- `moss_root_runtime::moss_set_solo_hooks(enter: fn(&str), leave: fn(&str))` / `moss_clear_solo_hooks()`: installation point for Agent C executor compensation hooks. With no hooks installed, `moss_solo_enter`/`moss_solo_leave` are no-ops.

### Test-Only Instrumentation (Non-ABI)
Gated behind `#[cfg(any(test, moss_perf))]` (the repository convention shared with `handler_runtime_rust()`). Absent from ordinary generated runtimes, so production code cannot resolve them:
- `moss_fileio_registry_contains(dev: u64, ino: u64) -> bool`
- `moss_fileio_registry_reset()`: can erase a live inode claim, so it must never reach production
- `moss_set_close_override(f: fn(RawFd) -> i32)` / `moss_clear_close_override()`
- `moss_set_sync_hook(hook: fn(&str, RawFd, &str))` / `moss_clear_sync_hook()`
- `moss_root_runtime::test_support`: fair-lock trace (assigned ticket and critical-section entry order), issued-ticket and parked-waiter counters, and `hold_turn()` for deterministic contention tests

### Fair Leaf Lock Discipline (Phase 20 R5)
The process-wide inode registry is a private `FairRegistryLock`: an `AtomicU64` ticket counter, a `Mutex<RegistryState { serving_ticket, inode_set }>`, and a `Condvar`.
1. **FIFO Admission**: Each request takes a ticket with `fetch_add`, locks the state, and waits on the `Condvar` (releasing the mutex) until `serving_ticket` equals its ticket. On exit it increments `serving_ticket` and calls `notify_all`. Waiting is event-driven, with no spin or yield loop.
2. **Leaf Discipline**: Only the private concrete operations `claim`/`release` run in production (`contains`/`reset` are test-only). Each critical section is a single in-memory `HashSet` insert/remove. No kernel syscalls, no Moss or other runtime locks, and no callbacks run while the registry lock is held. There is no general callback facility.
3. **No Stranded Tickets**: Critical sections cannot unwind. Poisoned mutex or `Condvar` results abort. A ticket that was taken is therefore always served and advanced.
4. **Lock-Dropping Hook Invocation**: Solo hook registration locks and test hook mutexes are released before user/executor callbacks are called, preventing reentrancy deadlocks.

### Agent B ↔ Agent C Solo Bridge & Integration Contract
- FileIO operations bracket all potentially blocking kernel waits (`open`, `fstat`, `pread`, `pwrite`, `fsync`, `fdatasync`, `close`, `fstatfs`) with RAII `SoloGuard`, which invokes `moss_solo_enter` and `moss_solo_leave`.
- Agent C registers executor compensation hooks via `moss_set_solo_hooks`. Agent B does not implement the executor or compensation scheduling.
- **Integration Test Contract**:
  - Executor configured with `threads = 1, max_threads >= 2`.
  - Root A starts and enters a blocking FileIO operation (`moss_solo_enter` fires).
  - Executor activates compensation worker; independent work (Root B or another branch) makes progress while Root A is blocked in the kernel.
  - FileIO finishes, `moss_solo_leave` fires without waiting for compute slots.
  - Active worker count never exceeds `T_max`, and excess workers park cleanly.
- Status: this end-to-end compensation regression is an A+B+C integration dependency. It has not been run on the isolated Agent B branch. Agent B verifies the bridge only: provider FileIO reaches the executable-installed hooks across `.rlib` boundaries, with no FileIO or registry lock held during the callback.
## Physical signatures & Integration Seam (Agent C Runtime)

### Process-Wide Ingress and Runtime
```rust
// Process runtime accessor (shared across .rlib and bin crates):
pub fn moss_process_runtime() -> &'static MossProcessRuntime;

// Cross-crate raw pointer symbol (exported by main root, imported by modules):
#[no_mangle] pub extern "C" fn __moss_process_runtime_raw() -> *mut ();
```

### Executor API
```rust
pub struct MossExecutor { ... }
impl MossExecutor {
    pub fn new() -> Self;
    pub fn threads(self, n: usize) -> Self;
    pub fn max_threads(self, n: usize) -> Self;
    pub fn queue_capacity(self, n: usize) -> Self;
    pub fn affinity(self, cores: &[usize]) -> Self;
    pub fn priority(self, level: i32) -> Self;
    pub fn start(self) -> MossExecutorHandle;
}

#[derive(Clone)]
pub struct MossExecutorHandle { ... }
impl MossExecutorHandle {
    pub fn enqueue_root(&self, desc: MossRootDescriptor);
    pub fn join(self);
}
```

### Branch Publish / Join ABI
```rust
pub fn branch_scope_new(root_id: u64) -> MossBranchScope;
pub fn branch_publish<F: FnOnce() + Send + 'static>(scope: &MossBranchScope, work: F);
pub fn branch_join(scope: MossBranchScope);
```

### Solo Compensation Seam (Agent B FileIO Integration)
```rust
// Thread-local worker identity established during worker execution:
thread_local! {
    pub static MOSS_CURRENT_WORKER_ID: std::cell::Cell<Option<usize>> = const { std::cell::Cell::new(None) };
}

// Low-level Solo hooks for known worker ID:
pub fn solo_enter(worker_id: usize);
pub fn solo_leave(worker_id: usize);

// Context-aware Solo hooks using TLS (safe for non-worker threads):
pub fn solo_enter_current() -> bool;
pub fn solo_leave_current() -> bool;

// FileIO adapter bridge for Agent B integration:
pub fn executor_solo_enter_from_fileio(op_name: &'static str);
pub fn executor_solo_leave_from_fileio(op_name: &'static str);
```

### Synchronous Host Ingress (`runtime_invoke`)
```rust
pub fn runtime_invoke<R: Send + 'static, F: FnOnce() -> R + Send + 'static>(work: F) -> R;
```
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
let mut executor = MossExecutor::new().threads(__cfg0 as usize)/* ... */.start();
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
to `i32`, counts to `usize`.

### Chunk-pipeline Branches (Agent C)

An eligible `file.chunks(C) |> map(f) |> reduce(init, combine)` lowers per
`ChunkParallelPlan` (a compiler-constant `window_k`, currently 4) to windows
of K Branches in one join scope:

```text
acc = init                                   // before any publication
loop over windows:
    scope = branch_scope_new(owner_root_id)
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
parent Root, left to right; there is no tree reduction. Agent C exposes no
current-Root accessor yet, so generated code passes `0` as the
instrumentation-only `owner_root_id`.

A pipeline whose map is impure, reaches FileIO, or captures parent locals,
or whose combine reaches FileIO, lowers to the plain sequential loop and
uses no Branch ABI.

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
carry instead of `&FileIO`. It is not a Moss value and not a second FileIO
owner; it shares the open file's runtime backing, permits only reads, and
the compiler guarantees every token is dead by the window's `branch_join`.
Agent B's current `FileIO { inner: Mutex<Option<FileIOInner>> }` would need
shareable backing (for example an `Arc` inside) to implement it.

### Fast Debug

`executor.invoke` runs as a deterministic sequential schedule of deferred
roots (snapshot at invoke, run at join). FileIO execution in Fast Debug
awaits Agent A/B integration and raises a clear error.

### Test-only shims

`tests/tooling/fixtures/phase20_executor_runtime_shim.rs`,
`phase20_branch_runtime_shim.rs`, and `phase20_fileio_range_shim.rs` mirror
the signatures above so Agent D's lowering can be compiled and run in
isolation. They are never emitted by the compiler and are not a design for
Agent B/C.
