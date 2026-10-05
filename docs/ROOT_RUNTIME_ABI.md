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

## Physical signatures & Integration Seam (Agent C Runtime)

### Crate roles
`executor_runtime_rust(owns_process_runtime)` emits one of two variants.
The final application crate (an implicit-module program, or the explicit
module that declares `main`) owns the process runtime: the scheduler, the
ingress state, Root/Branch identity counters, and the TLS execution context.
Every other generated crate is a provider and emits only an opaque
`MossBranchScope` wrapper over the root's exported C Branch ABI; providers
never own a scheduler or allocate Root identities.

```rust
// Exported by the process-root crate, imported by provider crates:
#[no_mangle] pub extern "C" fn __moss_branch_scope_new_current() -> *const ();
#[no_mangle] pub unsafe extern "C" fn __moss_branch_scope_clone(scope: *const ()) -> *const ();
#[no_mangle] pub unsafe extern "C" fn __moss_branch_scope_drop(scope: *const ());
#[no_mangle] pub unsafe extern "C" fn __moss_branch_publish_trampoline(
    scope: *const (), data: *mut (),
    invoke: unsafe extern "C" fn(*mut ()), discard: unsafe extern "C" fn(*mut ()));
#[no_mangle] pub unsafe extern "C" fn __moss_branch_join(scope: *const ());
```
`new_current` and `clone` return one owned count; `drop` and `join` consume
one; `publish` borrows. `discard` releases a foreign closure that never ran.

### Execution context (runtime-owned TLS)
```rust
pub fn current_worker_id() -> Option<usize>; // None off Executor workers
pub fn current_root_id() -> Option<u64>;     // set on every ingress path and for Branches
pub fn next_root_id() -> u64;                // process root only
```

### Executor API
```rust
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MossExecutorConfig {
    pub threads: usize, pub max_threads: usize, pub queue_capacity: usize,
    pub affinity: Option<Vec<usize>>, pub priority: Option<i32>,
}
impl MossExecutorConfig {
    pub const DEFAULT_QUEUE_CAPACITY: usize; // 1024
    // Omitted fields: threads = host parallelism; max_threads =
    // max(threads, 4 * host parallelism); queue_capacity = default.
    pub fn resolved(threads: Option<usize>, max_threads: Option<usize>,
                    queue_capacity: Option<usize>, affinity: Option<Vec<usize>>,
                    priority: Option<i32>) -> Self;
}

pub struct MossExecutor { ... } // builder
impl MossExecutor {
    pub fn new() -> Self;
    pub fn threads(self, n: usize) -> Self;
    pub fn max_threads(self, n: usize) -> Self;
    pub fn queue_capacity(self, n: usize) -> Self;
    pub fn affinity(self, cores: Vec<usize>) -> Self;
    pub fn priority(self, level: i32) -> Self;
    pub fn config(&self) -> MossExecutorConfig;
    pub fn start(self) -> MossExecutorHandle;
}

pub struct MossExecutorHandle { ... } // linear: not Clone
impl MossExecutorHandle {
    pub fn enqueue_root(&self, desc: MossRootDescriptor);
    pub fn config(&self) -> &MossExecutorConfig;
    pub fn join(self);
}
```
`start()` validates the config (fails closed on zero fields or
`max_threads < threads`) and is legal only from `main`, outside any Root;
a second concurrent `start()` panics. `enqueue_root` and `join` panic from
inside a running Root.

### Agent D seam
```rust
pub fn moss_root_start() -> MossExecutorHandle; // all defaults
pub fn moss_root_start_with_config(config: MossExecutorConfig) -> MossExecutorHandle;
pub fn moss_root_submit(executor: &MossExecutorHandle, work: impl FnOnce() + Send + 'static);
pub fn moss_root_submit_with_target(executor: &MossExecutorHandle,
                                    target_identity: &'static str,
                                    work: impl FnOnce() + Send + 'static);
pub fn moss_root_join(executor: MossExecutorHandle);
```

### Branch Publish / Join API
```rust
pub fn branch_scope_new_current() -> MossBranchScope; // panics outside a Root
pub fn branch_publish<F: FnOnce() + Send + 'static>(scope: &MossBranchScope, work: F);
pub fn branch_join(scope: MossBranchScope);
```
Scopes are one-shot: publish after join, or a second join, fails closed.
Publication never blocks. With no Executor, or when the per-scope window is
full, the owner runs the Branch inline. Each scope occupies at most one
ready-queue entry. While joining, the owner helps only its own unstarted
Branches.

### Solo Compensation Seam (Agent B FileIO Integration)
The process-wide `moss_solo_enter` / `moss_solo_leave` symbols belong to the
FileIO root runtime. FileIO registers the executor callbacks through its hook
table (`moss_set_solo_hooks(enter, leave)`) and never supplies a worker
identity.
```rust
pub fn solo_enter_current(reason: &str) -> bool; // false (no-op) off workers
pub fn solo_leave_current(reason: &str) -> bool;
pub fn executor_solo_enter_from_fileio(reason: &str);
pub fn executor_solo_leave_from_fileio(reason: &str);
pub fn executor_fileio_solo_enter_callback() -> fn(&str);
pub fn executor_fileio_solo_leave_callback() -> fn(&str);
```
Solo nesting is counted per worker. Entering may activate one compensation
worker, but never more than `max_threads`. Leaving resumes immediately, and
excess workers exit at their next idle point.

### Synchronous Host Ingress (`runtime_invoke`)
```rust
pub fn runtime_invoke<R: Send + 'static, F: FnOnce() -> R + Send + 'static>(work: F) -> R;
```
