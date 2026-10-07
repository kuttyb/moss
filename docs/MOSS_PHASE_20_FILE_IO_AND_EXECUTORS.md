# Moss Phase 20 — Blocking File I/O and Executors

Oct 4, 2026 · Canonical merged design · revised after peer review 2

## Summary

Phase 20 gives Moss one coherent execution model for blocking file I/O, explicit root-level concurrency, and compiler-managed parallel work.

The design has three pieces:

1. **Blocking `FileIO`.** Regular-file operations are synchronous. A handler calls `open`, `read`, `write`, `sync`, or `close` and waits for the operation to finish.
2. **The executor.** `executor.invoke(domain.Handler(args...))` turns one statically known handler invocation into an independent Moss root. The source language gains roots, not futures, tasks, `async`, or user-visible threads.
3. **Compiler branches.** Functional decomposition such as `file.chunks(size) |> map(...) |> reduce(...)` has sequential source semantics. The compiler may execute eligible reads and maps as branches of the current root, then commit their results in source order.

Inside a root, Moss does not change:

- nested `message` remains synchronous;
- compiler-derived READ/WRITE/CONSUME effects remain authoritative;
- Moss locks remain compiler-generated and globally ordered;
- domain topology remains statically closed;
- payloads still cross root/domain boundaries by value;
- no user code handles futures, task objects, mailboxes, or worker threads.

The central principle is:

> **Blocking is legal when forward progress is external to Moss. Roots express architectural concurrency. Functional decomposition expresses compiler-managed parallelism.**

### Guarantees

| Guarantee | Claim | Where |
| --- | --- | --- |
| Deadlock freedom | Under the Solo contract, every wait chain ends at a higher-ranked Moss lock, a leaf runtime lock, a Solo kernel operation, or a root-scoped join over lock-free branches. | Proof 1a |
| Progress | Assuming roots terminate, Solo operations return, and executor fairness holds, every started root completes and every admitted runnable root starts. | Proof 1b |
| Bounded FileIO buffer memory | Runtime-owned FileIO buffers are bounded independently of file size. | Proof 2 |
| Sound chunk lowering | Parallel reads/maps plus ordered commit produce the same successful result as the sequential chunk pipeline. | Proof 3 |
| Unique Moss file capability | At most one live `FileIO` in the process may refer to a given `(device, inode)`. | File identity rule |

Errors and recovery remain Phase 21. This document specifies where failure occurs and what work may be observed, but does not introduce a recoverable error type or result-carrying executor join.

### Word count: the design in one example

This example captures the intent of Phase 20 in its smallest useful form. The program is written as a normal synchronous computation: open a file, divide it into ordered chunks, summarize each chunk, fold those summaries into a final word count, and close the file. There are no futures, callbacks, task handles, async functions, or explicit worker management in the algorithm itself.

```moss
fn summarize(chunk):
  word_summary(chunk)

fn merge(a, b):
  merge_word_summary(a, b)


domain WordCount:
  fn Count(path) -> Int:
    file = FileIO.open(path, ro)
    words = file.chunks(1048576) |> map(summarize) |> reduce(empty_word_summary(), merge)
    file.close()
    reply words.count()


fn main():
  counter = WordCount()
  executor = Executor().threads(8).start()
  total = message counter.Count("input.txt")
  executor.join()
  echo total
```

`word_summary`, `merge_word_summary`, and `empty_word_summary` are ordinary library functions over a word-summary value (§21 describes what a summary holds). None of them is a language primitive.

The important point is that the source expresses meaning, not scheduling. `file.chunks(...) |> map(...) |> reduce(...)` has a simple sequential interpretation, but it exposes enough structure for the compiler to recognize independent chunk reads and pure per-chunk work. When an executor is active, Moss may run those reads and summaries in parallel using compiler-generated branches, then commit their results in chunk order and perform the same ordered fold required by the source semantics. If no executor is active, the same program simply runs inline.

`FileIO` remains owned by the `Count` root for the lifetime of the operation. Compiler-generated branches receive only scoped read borrows; ownership is never copied or transferred. Because the file is root-local, no Moss state lock is required. Blocking reads are safe under the Solo-I/O contract, and the executor may compensate for workers blocked in the kernel without changing the programming model.

This example also shows why `executor.invoke(...)` is not needed for every use of the executor. `Count` returns a value, so `main` invokes it synchronously with `message`; the active executor is still available underneath to execute compiler-generated chunk branches. `executor.invoke(domain.Handler(...))` is reserved for independent no-result roots where the programmer is explicitly expressing architectural concurrency.

The result is the central Phase 20 proposition in concrete form:

> **Write the obvious synchronous program. Expose independence through values and functional structure. Let the compiler discover parallel work, and let the executor supply the physical concurrency.**

---

## 1. Scope

Phase 20 covers:

- regular files;
- explicit-offset reads and writes;
- blocking `open`, `read`, `write`, `sync`, and `close`;
- bounded batch reads;
- borrowed read ranges;
- file chunk pipelines;
- one Rust-backed executor active at a time;
- independent root invocation from `main`;
- compiler-generated branch parallelism;
- bounded worker compensation for Solo kernel waits.

Phase 20 does **not** add:

- sockets, pipes, eventfd, or other Duo I/O;
- `async` / `await`;
- futures or promises;
- user-visible task handles;
- user-created threads;
- arbitrary closures as tasks;
- first-class handler references;
- dynamic handler dispatch;
- per-domain mailboxes;
- root submission from arbitrary handlers;
- multiple active executors;
- result-carrying executor joins;
- recoverable root failure.

Console input/output remains separate synchronous library functionality.

### Solo versus Duo

**Solo I/O** is an operation whose completion does not require another Moss root in the same process to make progress.

A normal regular file is expected to be Solo. The important counterexample is a filesystem whose implementation depends back on the same process, such as a FUSE server driven by Moss roots in that process.

Console I/O (`echo` and the console library) is treated as Solo under the same contract: nothing that reads or feeds the console is a Moss root in this process. A handler may therefore write to the console while holding Moss locks, and Proof 1 covers that wait.

**Duo I/O** can require another participant to act before the operation completes. Sockets and pipes are the obvious examples. Blocking Duo I/O while holding Moss locks can create a wait cycle and is outside Phase 20.

The Solo property is an **environmental contract**. Moss may diagnose suspicious storage, including FUSE, but the runtime does not claim that it can prove the absence of every hidden same-process dependency.

A slow external NFS server or external FUSE daemon threatens progress, not Moss deadlock freedom: it can violate the finite-completion assumption without introducing a dependency back on a Moss root.

---

## 2. Vocabulary

| Term | Meaning |
| --- | --- |
| Root | An independent Moss handler execution. It may hold Moss locks and issue synchronous nested messages. |
| Branch | Compiler-generated child work belonging to exactly one root and join scope. Phase 20 branches take no Moss locks of their own. |
| Root admission | The single mechanism that decides when a new root may start. While an executor is active, every concurrent Moss root enters through it. |
| Executor | The one active Rust-backed worker pool that admits roots, runs compiler branches, and compensates for Solo blocking. |
| Solo blocking region | The span in which a worker is blocked in a known Solo kernel operation. |
| Compensation | Activating an extra physical worker because another worker is in a Solo blocking region. |
| `FileIO` | A built-in, pinned-owner capability referring to one regular file. It is non-copyable, non-transferable, and synchronously borrowable. |
| Range | A borrowed read-only view into a runtime-owned FileIO buffer. |
| RangeBatch | The only collection-like value that may contain Ranges; it is scoped to the batch read that created it. |
| Chunk pipeline | `file.chunks(size)` followed by functional stages such as `map` and `reduce`. |
| Lowering | Compiler execution of eligible chunk reads/maps as parallel branches while preserving the sequential pipeline's observable order. |
| Join | The point where a root waits for compiler-created branches in its own join scope. |
| Root-scoped helping | A root waiting at a join may run only unstarted branches of its own root. |
| Leaf lock | A runtime-internal lock whose holder takes no other lock and never holds it across a kernel wait or branch join. |
| `runtime_invoke` | The Rust host's synchronous root ingress. It enters through root admission and returns the handler's reply. |
| Sans-I/O | Logic expressed as state transitions in Moss while a Rust top half performs the data movement Moss requests and enters Moss through `runtime_invoke`. |

**Notation.** A `fn` declared directly inside a domain is a handler; `fn Handler(...)` is the canonical spelling. `message` calls a handler synchronously, `reply` returns a handler's value, and `echo` writes to the console.

---

## 3. Executor source surface

The Moss-facing executor is configured in `main`.

```moss
fn main():
  worker = Worker()

  executor = Executor().threads(8).max_threads(32).queue_capacity(128).start()

  executor.invoke(worker.Process("input.txt"))

  executor.join()
```

The lifecycle is:

```text
configure
   ↓
start
   ↓
invoke*
   ↓
join
```

Configuration is complete before `start()` and immutable afterward.

### 3.1 `threads(n)`

`threads(n)` is the target compute parallelism: the number of workers the runtime tries to keep available for runnable computation.

If omitted, the runtime may choose a host-dependent default.

### 3.2 `max_threads(n)`

`max_threads(n)` is the finite cap on physical workers, including workers activated as blocking compensation.

The source spelling is optional, but the bound is not. When omitted, the runtime supplies a finite default `T_max`.

### 3.3 `queue_capacity(n)`

`queue_capacity(n)` bounds roots waiting to start.

It does **not** bound compiler branches. Branch publication never blocks; see §7.

`executor.invoke(...)` is permitted only from `main`, so waiting for root-queue capacity never occurs while a Moss handler holds domain locks.

Host-side root ingress may also wait for capacity, but the host caller is not a running Moss root and holds no Moss lock as part of the ingress contract.

### 3.4 `affinity(cores)` and `priority(level)`

These are portable scheduling hints.

If the operating system refuses the requested affinity or priority, the runtime warns and continues. Refusal does not change Moss semantics.

Compensation workers inherit the executor's affinity and priority policy.

Raw OS-specific masks, real-time policies, and NUMA controls are outside the initial API.

---

## 4. `executor.invoke`

The source form is:

```moss
executor.invoke(worker.Process(item))
```

The expression inside `invoke` is **not** evaluated as a synchronous handler call.

`invoke` is compiler-recognized syntax over a normal-looking direct call expression. The compiler resolves:

```text
concrete domain instance
+
statically known handler
+
evaluated argument snapshot
```

and lowers it to an internal root descriptor and enqueue operation.

Conceptually:

```text
executor.invoke(worker.Process(item))

                ↓ compiler

Root {
  domain  = worker
  handler = Worker.Process
  args    = snapshot(item)
}

                ↓ runtime

internal enqueue(root)
```

`enqueue` is an implementation concept, not the Moss-facing concurrency operation.

### 4.1 Static restriction

The accepted form is narrowly:

```text
executor.invoke(concrete_domain.Handler(arguments...))
```

The target domain and handler must resolve statically. Application code cannot manufacture a runtime handler value.

`executor.invoke(...)` is legal only directly from `main`.

This is a deadlock-safety restriction, not merely an ownership or API-shaping rule. `invoke` performs bounded Root admission and may wait for root-queue capacity. Permitting that wait from a running handler could block while the handler holds Moss locks required by Roots that must complete before the queue can drain.

Moss therefore gains explicit root concurrency without gaining first-class tasks or dynamic routing.

### 4.2 Arguments

Arguments are evaluated when `invoke` executes and cross into the future root by Moss's ordinary semantic value-boundary rule.

The future root cannot retain an alias into mutable storage in `main`.

Normal payload eligibility applies. Capabilities such as `FileIO` cannot be arguments to `invoke`.

### 4.3 Results

An invoked handler must be one-way in Phase 20.

Invoking a value-returning handler is a compile-time error. The rule is specific to `executor.invoke`: synchronous ingress (a top-level `message` from `main`, or host `runtime_invoke`) waits for the root and returns the handler's reply (§5). Phase 20 deliberately does not create `Task[T]`, futures, promises, result queues, or `await`.

Value-producing parallel computation belongs to functional decomposition, not architectural root invocation.

### 4.4 `message` remains distinct

```moss
message worker.Process(item)
```

means:

> Invoke this handler synchronously and wait for its result or completion.

```moss
executor.invoke(worker.Process(item))
```

means:

> Admit this handler invocation as an independent root and continue after submission.

Nested `message` inside a root never creates another root.

---

## 5. Root ingress and admission

There is one conceptual root-admission mechanism.

Root ingress can originate from:

1. `executor.invoke(...)` in `main`;
2. a synchronous top-level `message` initiated by `main`;
3. the Rust top half's host ingress, `runtime_invoke`.

Nested `message` calls from inside an existing root are not root ingress; they remain part of that root.

| Form | Caller | Caller waits for | Result |
| --- | --- | --- | --- |
| `executor.invoke(domain.Handler(args))` | `main` | submission only | none (one-way root) |
| top-level `message domain.Handler(args)` | `main` | root completion | the handler's `reply` |
| `runtime_invoke` | Rust host | root completion | the handler's `reply` |
| nested `message domain.Handler(args)` | a running root | handler completion | the handler's `reply`; not root ingress |

`runtime_invoke` is an **external ingress API**.

It may be called by a Rust host thread that is outside Moss Root execution. It may not be called recursively from code currently executing a Moss Root or one of its Branches. Recursive use fails before admission begins.

### 5.0 Roots versus Branches inside execution

| Operation while inside a Root | Phase 20 |
| --- | --- |
| ordinary function call | allowed |
| synchronous nested `message` | allowed |
| compiler-created Branch | allowed |
| `branch_join` on own Root | allowed |
| Solo blocking FileIO | allowed |
| submit another Root | forbidden |
| recursive `runtime_invoke` | forbidden |
| executor start/join | forbidden |

The distinction to preserve is:

```text
Nested message:
    same Root

Compiler Branch:
    child work of same Root

executor.invoke / runtime_invoke:
    new independent Root
```

Only the third category is forbidden from Root execution.

None of these creates a future, task handle, or result queue. The reply returned by `runtime_invoke` is the host's control channel from Moss: Moss cannot call Rust, so Moss tells the host what to do next by replying (§26.2).

### 5.1 While an executor is active

Every new concurrent Moss root enters through the active executor's admission mechanism.

```text
main executor.invoke ─┐
main top-level message ├──→ ROOT ADMISSION ──→ active executor
Rust runtime_invoke ──┘
```

A synchronous top-level caller waits for its admitted root to complete. `executor.invoke` returns after submission is accepted. A Rust `runtime_invoke` caller waits synchronously for the root it requested and receives the handler's reply.

This rule is important for both progress and resource bounds: there is no second population of uncounted Moss roots running on arbitrary host threads beside the executor.

### 5.2 With no active executor

Moss uses inline mode:

- every inline root, whether a top-level `message` from `main` or a host `runtime_invoke`, takes the root-ingress gate, so inline roots run one at a time, synchronously on the caller's thread;
- compiler branches execute inline;
- there is no hidden ambient worker pool.

This applies to pool-less programs, execution before `start()`, and execution after `join()`.

The root-ingress gate is inline mode's admission mechanism: a single root slot, held for the whole of the root it admits and granted to waiters fairly (R4). It is part of root admission, not a runtime-internal lock; R1 and R5 govern runtime-internal locks only. A caller waiting for the gate is waiting for admission and holds no Moss lock (A6). Because the gate admits one root at a time, `T = 1` in inline mode (§19.1).

`start()` takes the root-ingress gate before activating the executor, so no inline root is still running when admission begins. Otherwise that root would run beside the executor as the uncounted root §5.1 rules out. Activation moves any callers still waiting on the gate into executor admission. While ACTIVE, all ingress goes through executor admission. During DRAINING the gate stays closed, and unadmitted callers wait on it until the executor is consumed (§5.4).

### 5.3 One active executor

At most one executor may be active.

The compiler rejects Moss source whose `main` lifecycle can contain two simultaneously active executors. The runtime also asserts the invariant defensively.

Multiple executors are parked for a later phase.

### 5.4 Join drains, then ingress returns to inline

Root ingress moves through one lifecycle:

```text
INLINE
  ↓ start()
ACTIVE
  ↓ join()
DRAINING
  ↓ drained
INLINE
```

`executor.join()`:

1. moves the executor to DRAINING, so no further root is admitted to it;
2. lets already admitted roots run to completion and waits for them;
3. stops the workers;
4. consumes the executor and returns ingress to INLINE.

A host `runtime_invoke` that has not been admitted when DRAINING begins, whether it arrives during DRAINING or was already waiting for queue capacity, waits outside Moss on the root-ingress gate, holding no Moss lock. Once the executor is consumed, it runs under serialized inline ingress like any other inline root. Requests admitted before DRAINING complete normally, and `join()` waits for them.

DRAINING still counts as an active executor for E6, E9 and Proof 2 (`T = T_max`): draining roots run on its workers until it is consumed.

Executor shutdown therefore never ends Moss ingress; there is one ingress model throughout. Executor lifetime is distinct from Moss runtime lifetime: `runtime_invoke` is valid only while the Moss runtime/program instance itself remains alive, and executor `join()` does not terminate host ingress.

---

## 6. Executor lifecycle rules

These are compile-time rules in Moss `main`:

- `join()` consumes the executor;
- any use after `join()` is an error;
- every normal path leaving `main` after `start()` must reach `join()`;
- there is no implicit join;
- explicit `invoke` is legal only from `main`;
- executor objects do not cross domain boundaries or live in domain state.

Until Phase 21, unexpected root failure aborts the program under Moss's existing fail-closed rule.

A failing compiler branch fails its owning root. `join()` carries no failure result in Phase 20.

---

## 7. Roots and branches

The runtime schedules two distinct work classes.

| Class | Meaning | Moss locks | Execution |
| --- | --- | --- | --- |
| Root | Independent handler execution | May hold them | Runs on a worker that is not inside another root |
| Branch | Compiler-generated child work of one root | None of its own in Phase 20 | Runs on a free worker or its own root's worker while helping |

The distinction is a safety invariant, not merely a scheduler optimization.

### 7.1 No root inside a root

A worker running root A never starts root B before A completes.

This matters because A may hold Moss locks. Starting B on the same thread could violate global lock-rank order, deadlock B on a lock held by A, or accidentally exploit thread-reentrant lock behavior.

### 7.2 Root-scoped helping

A worker waiting at a branch join may execute only unstarted branches belonging to its own root's join subtree.

It must never behave like a generic Rayon-style worker that runs arbitrary queued roots while waiting.

### 7.3 Branch publication never blocks

Compiler branch publication is bounded by a lowering window `K`, not by root `queue_capacity`.

If a branch cannot be published to another worker, the owning root may execute it inline. A branch never waits for a queue slot while the parent root holds Moss locks.

### 7.4 Branches take no Moss locks

Phase 20 compiler branches are restricted to work whose branch body requires no Moss lock acquisition of its own.

For FileIO chunk lowering, branches use scoped read borrows of the FileIO capability and may wait only on runtime leaf locks and Solo kernel operations.

### 7.5 Root/branch contention

Branch parallelism is opportunistic. If every available worker is occupied by a root (at most `T_max` under E7 and E11), compiler-generated branches run through root-scoped helping and may become effectively sequential within each root:

```text
worker 0 → root A → A0 → A1 → A2 ...
worker 1 → root B → B0 → B1 → B2 ...
```

This affects throughput, not semantics or progress (E4, Proof 1). Root-level architectural parallelism has consumed the machine, and compiler branch parallelism yields to it. Compensation (§15) adds workers only for Solo kernel waits and never beyond `T_max`; Moss does not oversubscribe workers merely to preserve branch parallelism.

---

## 8. FileIO source model

`FileIO` is a built-in capability for one regular file.

Root-local form:

```moss
file = FileIO.open(path, ro)
range = file.read(offset, size)
file.close()
```

Persistent domain-state form:

```moss
domain Store:
  file: FileIO

  fn Open(path):
    file.open(path, rw)

  fn Flush():
    file.sync()

  fn Close():
    file.close()
```

The operation surface is:

```text
FileIO.open(path, mode)        # construct/open a root-local capability
file.open(path, mode)          # open a domain-field capability in place
file.read(offset, size)
file.read([(offset, size), ...])
file.write(offset, data)
file.sync()
file.sync(dataonly)
file.close()
file.chunks(size)
```

All operations are synchronous.

`mode` is one of:

| Mode | Opens | Access |
| --- | --- | --- |
| `ro` | an existing file | read |
| `rw` | an existing file | read and write |
| `create` | the existing file, or a new empty file if none exists | read and write |

No Phase 20 mode truncates; truncation is parked (§29). When `create` makes a new file, that open owes a parent-directory sync (§12.8, §17).

---

## 9. FileIO ownership

A `FileIO` has **pinned ownership**.

It is:

- single-owner;
- non-copyable;
- non-transferable;
- synchronously borrowable.

Ownership does not move after creation/opening.

### 9.1 Root-local ownership

A root-local FileIO belongs to the root execution that created it.

It cannot be:

- assigned into another owner as a move;
- copied;
- returned from the function that opened it;
- sent through `message`;
- passed through `executor.invoke`;
- stored into domain state;
- stored in a collection;
- otherwise allowed to outlive the function that opened it.

Because ownership never moves or returns, a root-local FileIO must be explicitly closed before every normal exit from the function that opened it, whether that is a handler or an ordinary helper. Leaving a live root-local FileIO open on a normal path is a compile-time lifecycle error.

Abnormal process/root failure remains Phase 21 territory; Phase 20 does not introduce implicit recovery semantics.

### 9.2 Domain-field ownership

A `FileIO` may instead be declared as domain state when the file lifetime genuinely belongs to the domain.

The capability remains owned by that field for its lifetime. User code does not create a local FileIO and move it into the field.

The field begins closed and is opened and closed in place by handlers.

A domain-field FileIO may remain open until process teardown. Teardown reclaims the resource only; it is not a durability operation. Programs that need durable contents must call `sync` explicitly. Phase 20 deliberately does not prove that every domain-field capability is closed before program exit.

### 9.3 Synchronous borrowing

Ordinary synchronous helper calls may borrow a FileIO temporarily.

The borrow:

- does not transfer ownership;
- cannot be stored or returned;
- cannot cross a root or domain boundary;
- ends before the helper returns.

When the borrowed FileIO originates in a domain field, effects through the borrow retain provenance to that field, so the compiler attributes READ or WRITE access to the owning domain state.

Moss source does not require Rust-style lifetime annotations for this borrow.

---

## 10. File identity: one live FileIO per inode

Moss rejects two live FileIO capabilities that refer to the same physical file inside one process.

After opening and validating a regular file, the runtime identifies it by `(device, inode)` and claims that identity in a process-local registry.

If another FileIO attempts to claim the same identity while the first is live, the second open fails.

It fails rather than waiting. Waiting for another root to close would be a root-on-root wait that Proof 1 does not cover, because the holder may itself be waiting on a Moss lock the opener holds.

This catches aliases through:

- different relative or absolute paths;
- symbolic links that resolve to the same file;
- hard links;
- repeated opens of the same path.

The registry serves **alias rejection only**. It is not the synchronization mechanism for FileIO operations.

Synchronization is determined by Moss ownership and generated domain locks (§11).

The registry's internal lock is a runtime leaf lock. It is never held across a kernel call. Closing a FileIO releases the registry claim after the close attempt has completed.

The registry covers Moss FileIO only. Descriptors that the Rust top half opens in the same process are outside it, like files held by other processes (P6).

---

## 11. Domain-field FileIO effects and locking

A FileIO stored in domain state is ordinary shared domain state as far as Moss synchronization is concerned.

The compiler derives:

| Operation | Effect on FileIO field |
| --- | --- |
| `read(offset, size)` | READ |
| batch `read(...)` | READ |
| `chunks(size)` / chunk reads | READ |
| `write(offset, data)` | WRITE |
| `open(path, mode)` | WRITE |
| `sync()` / `sync(dataonly)` | WRITE |
| `close()` | WRITE |

Compatible READ executions may overlap. WRITE excludes concurrent READ and WRITE access to the field.

The implementation may use a generated reader/writer lock or any equivalent mechanism that preserves Moss READ/WRITE semantics and the global lock-order discipline.

For example:

```moss
domain Store:
  file: FileIO

  fn Verify(offset, expected):
    data = file.read(offset, 4096)
    assert checksum(data) == expected

  fn Update(offset, data):
    file.write(offset, data)
```

Once `file` is open, `main` can admit two independent readers:

```moss
executor.invoke(store.Verify(0, 4096, expected0))
executor.invoke(store.Verify(4096, 4096, expected1))
```

The two `Verify` roots may overlap on `file`. Any handler that writes, opens, syncs, or closes `file` (here `Update`; in §8, `Open`, `Flush`, and `Close`) excludes them according to the generated lock effects.

`Verify` is one-way, as §4.3 requires of an invoked handler. A value-returning reader such as `reply checksum(data)` is equally legal as a root, but it enters through synchronous `message` (as in the front word-count example) or host `runtime_invoke`, never through `executor.invoke`.

A root-local FileIO needs no Moss lock because no other root can possess that capability.

---

## 12. Reads, writes, ranges, sync, and close

### 12.1 Opening

Only regular files are accepted.

The runtime opens in a form that cannot block indefinitely merely because the target is a FIFO, then validates the descriptor with `fstat`. Non-regular files are rejected.

The runtime also claims `(device, inode)` in the unique-live-FileIO registry (§10).

No Phase 20 mode truncates (§8), so an open that the registry rejects never changes the contents of the file the live FileIO holds.

For `create`, the runtime first attempts `O_CREAT | O_EXCL` and, if the file already exists, opens the existing file. The attempt's outcome records whether this open created the file (§17).

`open` requires a closed FileIO owner. `read`, `write`, `chunks`, and `sync` require an open FileIO. `close` requires an open FileIO. Violations fail; Phase 21 defines recoverable error representation.

A best-effort filesystem check may warn when the descriptor resides on FUSE or another environment where the Solo assumption deserves scrutiny. Such a check is diagnostic evidence, not proof of Solo.

### 12.2 Explicit offsets

FileIO has no implicit shared file position.

Every read and write identifies its offset explicitly. This makes independence visible and avoids turning otherwise read-only operations into writes of a shared cursor.

### 12.3 Reads

`read(offset, size)` blocks until the requested bytes have been read or end-of-file is established.

The runtime handles short kernel reads internally. Therefore a short returned Range means EOF rather than an arbitrary partial syscall result.

A zero-length Range at a requested nonzero size means the read began at EOF.

### 12.4 Writes

`write(offset, data)` blocks until all bytes have been written or the operation fails.

The runtime handles short kernel writes internally.

`data` is observed only for the duration of the synchronous call.

### 12.5 Batch reads

```moss
batch = file.read([(offset0, size0), (offset1, size1), ...])
```

A batch read submits the bounded set of independent requests together and returns only when they are complete.

Results appear in request order as a `RangeBatch`.

The runtime may use io_uring, vectored facilities where applicable, or bounded internal fan-out. Batch implementation does not create Moss roots.

### 12.6 Range lifetime and operations

A Range is a borrowed, read-only view into a runtime-owned buffer of bytes (`[u8]`).

It may be used locally and lent down ordinary synchronous helper calls. It may not be stored in domain state, retained after its owning scope, transferred across a root/domain boundary, or placed in an ordinary collection.

`RangeBatch` is the only collection-like container that may hold Ranges, and it is itself scoped to the batch read.

Indexing or iterating a `RangeBatch` produces a scoped borrowed Range view. It neither copies nor transfers the underlying Range. The view may be bound to a local, used, and lent to synchronous helpers, but it cannot outlive the `RangeBatch` or enter ordinary storage:

```moss
range = batch[0]
consume_bytes(range)      # legal: stays inside the batch's lifetime

saved.push(batch[0])      # illegal: ordinary collection
reply batch[0]            # illegal: crosses the root boundary
```

The runtime reuses a backing buffer only after every Range referring to it is dead.

#### Normative Range byte surface and operations

Moss provides a dedicated, byte-exact surface for `Range`:

```moss
range.length() -> Int
range[index] -> Int

for byte in range:
  ...

range.slice(start, length) -> Range
```

- **Length (`range.length() -> Int`):** Returns the number of bytes in the range as an `Int`.
- **Byte Indexing (`range[index] -> Int`):** Extracts the byte at 0-based `index` as an `Int` in the range `0..255`. Out-of-bounds indexing aborts with a diagnostic error message on stderr specifying the invalid index and valid length bounds rather than returning a default or silent EOF.
- **Byte Iteration (`for byte in range`):** Iterates over the bytes in sequential index order, yielding byte-valued `Int`s in `0..255`.
- **Slicing (`range.slice(start, length) -> Range`):** Produces a scoped sub-range view. Phase 20 defines the following behavior:
  - `start < 0` → empty Range;
  - `length <= 0` → empty Range;
  - `start >= range.length()` → empty Range;
  - otherwise length is clipped to the remaining bytes (`min(length, range.length() - start)`).
- **Exact Byte Equality (`range1 == range2`):** Evaluates exact byte-slice equality (`[u8] == [u8]`) between two `Range` values.
- **Type Distinction (`Range` vs `String`):** The current implementation rejects `Range == String` and `String == Range` with `TYPE_MISMATCH`. This remains a provisional design choice pending owner confirmation; no implicit conversion is implemented.
- **Writing Ranges (`FileIO.write(offset, range)`):** `FileIO.write(offset, range)` is legal when the compiler carries a statically known finite bound for that Range (inherited from the originating `read`, slice, batch indexing, or chunk iteration).
- **Bounds Propagation:** Static payload bounds propagate through `read(offset, size)` (bounded by `size`), batch indexing `batch[i]` (bounded by the statically known batch entry size or maximum of batch entry sizes), chunk iterators `for chunk in file.chunks(C)` (bounded by `C`), and sub-slicing `r.slice(offset, length)` (bounded by `min(bound(r), length)`). Non-mutating methods such as `.length()` and `.slice(...)` do not invalidate static payload bounds.

### 12.7 Bounded sizes

In Phase 20, request bounds must be established from a nonnegative compile-time
integer constant or an immutable single-assignment alias of such a constant.
String payloads use their statically known byte length. A Range payload inherits
the finite bound of the `read` expression that produced it.

This covers:

- direct read/write sizes;
- batch length;
- total batch byte size;
- individual batch entry sizes;
- chunk size.

#### Current implementation scope

Phase 20 implementation currently supports compile-time constants and immutable
aliases. Startup-declared bounds and compiler-proven dynamic clamps are deferred
implementation and design work. A compiler-proven clamp is also a static upper
bound and is compatible with the bounded-memory proof. Do not pass an
unconstrained function parameter as a request size. The canonical domain
example uses a fixed bounded read, as shown above.

### 12.8 Sync

`sync()` waits for prior writes to become durable as data and required metadata (`fsync`).

`sync(dataonly)` uses `fdatasync` semantics: metadata unnecessary for reading the data back need not be forced.

If this open created the file (`create` on a file that did not exist), it owes a parent-directory sync so the directory entry itself is made durable. While that obligation is pending, every sync of either kind also performs it:

- `sync()` performs `fsync(file)`, then `fsync(parent directory)`;
- `sync(dataonly)` performs `fdatasync(file)`, then `fsync(parent directory)`.

The obligation clears when a directory sync succeeds, so the first successful sync of either kind discharges it. It belongs to the open that created the file: closing without a successful sync discards it, consistent with close implying no durability, and a later reopen owes nothing. `dataonly` skips metadata unnecessary for reading the data back, and the directory entry is necessary: without it the data cannot be found by name.

A sync failure is not automatically retried. Recovery semantics belong to Phase 21.

### 12.9 Close

`close()` releases the file and its inode-registry claim.

Close implies no durability. A program requiring durability must call `sync` explicitly.

The runtime never retries the underlying close operation because the descriptor's state after a reported close error does not permit a safe blind retry.

---

## 13. Chunk pipelines

`file.chunks(size)` describes the file as consecutive explicit-offset regions:

```text
[0, C)
[C, 2C)
[2C, 3C)
...
```

The last yielded chunk may be short. A zero-length read terminates the sequence without yielding another chunk.

The chunk source is a scoped compiler-known view borrowing the FileIO. It is not an ordinary storable collection, cannot cross `message`, `reply`, or `executor.invoke`, and cannot outlive the FileIO. Phase 20's optimized form is an immediate chunk pipeline ending in a terminal fold/reduction rather than a materialized whole-file collection.

A canonical pipeline is:

```moss
summary = file.chunks(1048576) |> map(summarize) |> reduce(empty_word_summary(), merge)
```

The **source meaning is sequential**:

1. read chunk 0;
2. if non-empty, map it and fold it into the accumulator;
3. continue in increasing offset order;
4. stop after a short chunk or zero-length EOF read.

The compiler may lower eligible reads and maps into parallel branches, but it must preserve this sequential observable meaning.

### 13.1 Compiler-created FileIO borrows

FileIO ownership never moves into a branch.

For a lowered chunk pipeline, the compiler creates scoped internal **read borrows** of the owning FileIO capability:

```text
root owns FileIO
      │
      ├── read borrow → branch 0
      ├── read borrow → branch 1
      └── read borrow → branch 2
                 │
                join
                 │
         all branch borrows dead
                 │
         root still owns FileIO
```

These branch borrows:

- are not first-class Moss values;
- permit only the compiler-approved read operation for their chunk;
- cannot escape the branch;
- end by the join;
- never transfer or copy FileIO ownership.

For a domain-field FileIO, the parent root already holds the field's generated READ access across the lowered operation. Branches do not reacquire Moss locks.

For a root-local FileIO, no Moss lock is needed; the root's ownership and scoped branch borrows establish exclusivity.

### 13.2 Lowering conditions

The baseline lowering requires:

- the map stage to be pure with respect to Moss state;
- the map stage to make no FileIO calls;
- the `reduce` initializer to be evaluated before branch publication;
- the fold combiner to make no FileIO calls;
- branch work to require no Moss locks;
- the FileIO to remain live for the entire join scope;
- branch publication to be bounded by `K` and never block;
- results to be committed and folded in chunk order.

The final fold runs in the parent root in the same left-to-right order as the sequential source.

The fold may have ordinary parent-root/domain-state effects because those effects occur in the same order as the source program. It may not make FileIO calls in the baseline Phase 20 lowering: moving future reads ahead of an observable FileIO operation in the fold would not preserve the sequential program's I/O ordering.

### 13.3 No baseline tree reduction

Phase 20 does **not** require associativity merely to parallelize chunk reads/maps.

The baseline is:

```text
parallel read/map
       ↓
results indexed by chunk
       ↓
ordered commit
       ↓
original left-to-right fold
```

A later optimization may replace the ordered fold with an order-preserving tree only when the compiler has valid algebraic evidence for the combiner and handles the initializer consistently.

Known library combiners may carry such evidence. The compiler may also prove properties for restricted user code, but Moss does not claim that arbitrary user functions can generally be proven associative.

---

## 14. Scheduling and domain synchronization

Two independent roots may target the same domain instance.

```moss
executor.invoke(counter.Add(1))
executor.invoke(counter.Add(2))
```

The executor does not serialize them itself.

Normal Moss effect-derived locking decides whether they overlap.

This separation is deliberate:

> **The executor decides when a root gets a worker. Moss's compiler-generated synchronization decides which state accesses may proceed concurrently.**

Lock-oblivious admission is correct but can be inefficient: workers can park behind the same hot lock while unrelated roots remain queued.

Conflict-aware admission using statically known ClassSets is parked for Phase 23.

---

## 15. Managed blocking and compensation

Blocking FileIO does not create a Moss deadlock under the Solo contract, but it can reduce available compute parallelism.

The executor therefore supports managed Solo blocking.

When a worker enters a known Solo kernel wait, the runtime may activate another worker, up to `T_max`.

```text
target threads = 8

7 runnable workers
1 worker in FileIO wait

        ↓ compensate

8 runnable workers
1 blocked worker
```

When the blocked worker wakes, it resumes immediately. It never waits for a worker slot before continuing, even if the executor temporarily has more runnable workers than the target count.

This rule matters because the waking root may still hold Moss locks needed by other roots.

Excess compensation workers may park afterward.

Only known Solo kernel waits are compensated. Moss-lock waits are **not** compensated; doing so would merely create more physical threads queued behind the same hot lock.

The hard physical bound remains `T_max`.

---

## 16. Executor invariants

The following are mandatory Phase 20 runtime/compiler invariants.

| # | Invariant |
| --- | --- |
| E1 | Roots and branches are distinct scheduler classes. |
| E2 | A worker running a root never starts another root before the current root completes. |
| E3 | Join helping is root-scoped: a root helps only its own branches. |
| E4 | Branch publication never blocks; bounded overflow executes inline. |
| E5 | Root scheduling is fair: every admitted runnable root eventually starts. |
| E6 | At most one executor is active; without it, roots/branches use inline mode. |
| E7 | Physical workers, including compensation, never exceed finite `T_max`. |
| E8 | A worker returning from a Solo blocking region never waits for an executor slot. |
| E9 | While an executor is active, every concurrent Moss root—Moss- or host-originated—uses the same root-admission mechanism. |
| E10 | Root-queue backpressure is encountered only by callers that hold no Moss locks as part of root ingress. |
| E11 | Once a root starts, it retains one root worker until completion; branch workers are additional workers from the same finite physical population. |
| E12 | A running Moss Root may never enter root admission. While executing a Root, or a compiler-generated Branch belonging to that Root, execution may not synchronously submit, admit, or start another independent Root. The rejection must occur before waiting for queue capacity, an ingress gate, executor lifecycle state, or any other root-admission resource. |

### E12 — No root-originated root admission

> **A running Moss Root may never enter root admission.**
>
> While executing a Root, or a compiler-generated Branch belonging to that Root, execution may not synchronously submit, admit, or start another independent Root.
>
> This prohibition includes:
>
> - `executor.invoke(...)`;
> - internal executor root submission;
> - recursive `runtime_invoke`;
> - executor `start()` or `join()` from Root execution;
> - any future API that enters the root-admission mechanism.
>
> The rejection must occur **before** waiting for queue capacity, an ingress gate, executor lifecycle state, or any other root-admission resource.

---

## 17. Runtime and compiler requirements

| # | Requirement | Purpose |
| --- | --- | --- |
| R1 | **Managed Solo blocking.** Mark each known FileIO kernel wait as a Solo blocking region. Hold no runtime-internal lock across it. | Proof 1 |
| R2 | **Immediate wake continuation.** A worker leaving a Solo blocking region resumes without waiting for a slot. | Proof 1 |
| R3 | **Finite workers.** Compensation never exceeds `T_max`. | Proof 2 |
| R4 | **Unified fair root admission.** All concurrent roots use one admission mechanism while the executor is active (ACTIVE or DRAINING); in inline mode the root-ingress gate admits one root at a time. Both grant waiters fairly, and queued roots hold no Moss locks. A started root retains its root worker until that root completes. | Proofs 1 and 2 |
| R5 | **Leaf runtime locks.** Runtime-internal locks take no other lock, grant waiters fairly, and are not held across kernel waits or joins. | Proof 1 |
| R6 | **Regular file + unique identity.** Validate a regular file, identify `(device,inode)`, and reject a second live FileIO claim for the same identity. FUSE detection is warning-only. | Ownership, Proof 3 |
| R7 | **Explicit-offset whole operations.** Use `pread`/`pwrite`-equivalent explicit-offset operations (or an equivalent facility such as io_uring with fixed offsets), never `lseek` plus a shared file position. Complete short kernel reads/writes internally so source-level reads/writes have whole-operation semantics. | FileIO semantics |
| R8 | **Bounded batch read.** Submit a bounded batch and return one ordered RangeBatch after completion. | FileIO semantics, Proof 2 |
| R9 | **Scoped branch lowering.** Keep at most `K` chunk branches in flight, use scoped read borrows, acquire no branch Moss locks, commit by chunk index, and join with root-scoped helping. | Proofs 1–3 |
| R10 | **Sync durability.** Implement `fsync`/`fdatasync`. If the open created the file (detected through `O_CREAT \| O_EXCL`), every sync of either kind also syncs the parent directory until one directory sync succeeds; close discards an undischarged obligation. | Durability |
| R11 | **Close once.** Do not retry a kernel close; release the runtime inode claim after the close attempt. | File identity |
| R12 | **Plain runtime buffers.** Ranges refer to runtime-owned buffers rather than `mmap`; reuse only after all borrows end. | Proof 2 |
| R13 | **FileIO lifecycle checking.** Enforce no copy, no transfer, scoped borrow only, and explicit close of a root-local FileIO before every normal exit of the function that opened it. | Ownership |
| R14 | **Domain-field effect attribution.** READ for reads/chunks; WRITE for write/open/sync/close, including through helper borrows. | Moss locking |
| R15 | **Executor lifecycle checking.** Enforce at most one active executor, `invoke` only in `main`, use-after-join rejection, and join on all normal exits. | Root admission |
| R16 | **Best-effort scheduling hints.** Warn rather than change semantics if OS affinity/priority requests cannot be honored; compensation inherits policy. | Portability |

### Created-file directory sync

When a `create` open makes a new file, the runtime records that fact. It detects creation by attempting `O_CREAT | O_EXCL` first and falling back to opening the existing file, because plain `O_CREAT` cannot report whether it created anything. It retains a descriptor for the parent directory opened in a form suitable for `fsync` (for example `O_RDONLY | O_DIRECTORY` on Linux; `fsync` on an `O_PATH` descriptor fails with `EBADF`). While the obligation is pending, each sync of either kind on that FileIO also syncs the directory. The obligation clears when a directory sync succeeds and is discarded at close (§12.8).

---

## 18. Proof 1 — deadlock freedom and progress

There are two claims.

**Theorem 1a:** under the Solo contract A0, Phase 20 introduces no wait-for cycle.

**Theorem 1b:** under A0–A7, every started root completes and every admitted runnable root eventually starts.

### 18.1 Assumptions

| # | Assumption | Established by |
| --- | --- | --- |
| A0 | A Solo kernel operation never needs another Moss root in this process to make progress. | Environmental Solo contract |
| A1 | Every root and branch takes finitely many computation steps between waits and does not run forever. | Program assumption |
| A2 | Moss locks are acquired in one global rank order, held under Moss's two-phase discipline, and grant waiters fairly. | Synchronization-model assumption; implementation fairness tracked by SYNC-FAIR-001 |
| A3 | A running root waits only on a Moss lock, a runtime leaf lock, a Solo kernel operation (FileIO or console I/O, §1), or a join over its own branches. | Phase 20 restrictions |
| A4 | Every Solo kernel operation completes or fails in finite time. | Responsive storage assumption |
| A5 | Runtime leaf-lock holders take no other lock and never hold the leaf lock across a kernel wait or branch join. | R5 |
| A6 | Admission waits occur only outside Root execution (a caller waiting for root admission is not currently executing a Moss Root and therefore holds no Moss lock as part of Moss execution; by E12, bounded root-queue backpressure cannot block a Root while that Root holds Moss locks); admitted runnable roots are scheduled fairly; a waking Solo-blocked worker does not wait for a slot. | E5, E8–E10, E12, R2, R4 |
| A7 | Branches take no Moss locks, wait only on leaf locks or Solo kernel operations, and a joining root can execute its own unstarted branches. | E3, E4, R9 |

### 18.2 Theorem 1a — no deadlock

Take any running root or branch `T` that waits.

By A3 (roots) and A7 (branches), the wait is one of four kinds.

**Case 1: Solo kernel operation.**

By A0, completing the operation does not require a Moss root in this process. Therefore the kernel wait cannot lead through another Moss root back to `T`.

A handler may hold Moss locks while blocked, but the kernel is a dead end in the Moss wait graph rather than another lock-dependent participant.

**Case 2: runtime leaf lock.**

By A5, the holder of that leaf lock takes no other lock and does not enter a kernel wait or branch join while holding it. The leaf-lock edge therefore cannot extend into a cycle.

The inode-identity registry uses such a leaf lock. It rejects aliasing but creates no FileIO-specific wait graph.

**Case 3: branch join.**

By A7, branches take no Moss locks. Their waits are only leaf-lock or Solo-kernel waits, both already shown to terminate without leading back through Moss locks.

An unstarted branch does not introduce a dependency on a free worker: branch publication cannot block, and the joining root can execute its own unstarted branch inline.

Root-scoped helping is essential here. A joiner never starts an unrelated root while still holding the current root's Moss locks.

**Case 4: Moss lock `L`.**

By A2, `T` holds only Moss locks ranked below `L`. If the holder of `L` waits on another Moss lock, that next lock has a strictly higher rank.

A chain of Moss-lock waits therefore climbs strictly in the finite global rank order and cannot return to `T`.

Eventually the chain reaches a root that is running, a leaf lock, a Solo kernel operation, or its own branch join; the latter three have already been shown not to point back into the chain.

**Admission adds no cycle.**

A root that is waiting to start holds no Moss locks (A6). `executor.invoke`, top-level synchronous ingress, and host `runtime_invoke` all reach the same admission mechanism; there is no hidden host thread running an uncounted Moss root while bypassing the executor.

Consider the motivating deadlock topology that would arise if a running Root could enter admission:

```text
Root A holds Moss lock L
        |
        v
Root A attempts root admission
        |
        v
bounded root queue is full
        |
        v
Root A waits for queue capacity
        |
        v
queued/running Root B needs L
        |
        v
B cannot complete until A releases L
        |
        +----------------------+
                deadlock
```

E12 removes the edge from a running Root to root admission. Therefore this cycle cannot arise.

Host callers add no edge back into Moss either. A `runtime_invoke` caller, including one waiting on the ingress gate during DRAINING, waits outside Moss holding no Moss lock and receives its reply after the root completes. No Moss root ever waits on a host thread, because Moss cannot call Rust.

**`main` adds no cycle.** `main` is not a root and holds no Moss lock. Its own waits (root-queue backpressure in `executor.invoke`, completion of a top-level `message`, the ingress gate in `start()` or before an inline top-level `message`, and `join()`) are waits that nothing in Moss waits on in turn. When `main` runs an inline root on its own thread, that root's waits are covered by A3 like any other root's.

A worker returning from a Solo wait never waits for a scheduler slot (A6), so a lock holder cannot become blocked behind executor admission after its kernel operation finishes.

Therefore the wait-for graph has no cycle. ∎

### 18.3 Lemma 1 — every wait of a started root ends

- **Solo kernel wait:** ends by A4.
- **Leaf-lock wait:** the holder releases after finitely many computation steps and cannot wait while holding the leaf lock (A1, A5); fair granting eventually serves the waiter.
- **Branch join:** every branch has finite work (A1), every branch wait ends by the two cases above, and the root can execute any branch that has not started (A7).
- **Moss-lock wait:** prove downward from the highest rank. A holder of the highest-ranked Moss lock cannot wait on a higher Moss lock, so all of its possible waits are already known to end. It therefore finishes and releases. Inductively, a holder at rank `r` waits only on locks above `r` or on already-settled wait kinds, so it too eventually releases. Fair lock granting then serves every waiter at rank `r`.

Thus every wait of a started root ends. ∎

### 18.4 Theorem 1b — progress

A started root takes finitely many computation steps and every wait it encounters ends (A1 and Lemma 1), so every started root completes.

As roots complete, workers become available. Root admission is fair, queued roots hold no Moss locks, and returning Solo-blocked workers do not wait for slots (A6). Therefore every admitted runnable root eventually starts.

Without an active executor, root ingress is serialized and branches execute inline, so the executor-specific starvation case does not arise.

Hence every started root completes and every admitted runnable root starts. ∎

### 18.5 What Proof 1 does not claim

Proof 1 excludes:

- a root or branch that runs forever;
- storage that never answers;
- storage whose progress depends back on Moss and therefore violates Solo;
- latency guarantees;
- Phase 21 recovery behavior.

Holding a Moss lock during a slow but Solo disk operation is safe under this proof, but can still be poor for latency and throughput (§22).

### 18.6 Proof debt: SYNC-FAIR-001 (interleaved Solo and Moss-lock fairness)

- **Debt Identifier:** `SYNC-FAIR-001`
- **Assumptions Implicated:** A2 (fair Moss lock granting) and A4 (finite Solo latency).
- **Scope:** Roots interleaving Solo I/O operations while holding Moss domain locks.
- **Implementation Status:**
  - **R5 runtime-leaf fairness is implemented and closed:** Runtime leaf locks (such as the process-local inode registry mutex) use fair ticket locks (`MossFairMutex`) and are never held across kernel waits or branch joins (A5).
  - **Moss domain read/write exclusion:** Lowered domain locking is implemented by `std::sync::RwLock` via `handler_lowering.inc` and `handler_runtime.hpp`. The physical implementation proves mutual exclusion and concurrent readers, but does **not** establish fair waiter admission or FIFO queuing.
- **Proof Impact:**
  - Theorem 1a remains conditional on A0, A1, the ranked two-phase-locking portion of A2, and A3–A7; it does not depend on A2's fair-waiter premise.
  - Consequently Theorem 1b (progress) remains strictly **conditional on the fairness premise of A2**: while qualitative progress holds assuming finite Solo latency (A4), starvation freedom under domain lock contention requires fair granting.
- **Proof Obligation:**
  - `SYNC-FAIR-001` tracks this outstanding broader Moss synchronization-proof and implementation issue: formally modeling and establishing starvation-freedom bounds across interleaved Solo and Moss-lock phases. Replacing generated `RwLock`s with fair domain synchronizers is deferred to a dedicated synchronization design and hardening phase.
- **Current Mitigations:**
  1. R5 runtime-leaf fairness is implemented and closed with fair ticket mutexes.
  2. The Moss compiler emits diagnostic warnings when blocking FileIO calls are executed within domain handlers that hold domain locks.
  3. Workload guidance advises structuring FileIO around root-local capabilities or short-duration handler operations rather than long-running multi-stage FileIO within exclusive domain lock scopes.
- **Closeout Criteria:** Formal modeling bounding worst-case queuing latency for a Moss lock under a given upper bound on Solo kernel service time ($T_{solo}$) and root contention factor ($N$), backed by fair domain synchronization primitives.

---

## 19. Proof 2 — bounded FileIO buffer memory

The claim is deliberately about **FileIO/runtime buffer memory**, not all memory allocated by arbitrary user computation.

Map results, fold accumulators, domain state, and other user values are ordinary program memory.

### 19.1 Definitions

Let:

- `T` = `T_max` while an executor is active, otherwise `1` in inline mode;
- `B` = the largest total byte size of one direct FileIO operation, where a batch uses the sum of its requested sizes;
- `R` = the maximum number of simultaneously live ordinary Ranges/RangeBatch entries one root can retain under the scoped Range rules;
- `K` = the maximum number of chunk reads in flight for one lowered pipeline;
- `C` = that pipeline's statically bounded chunk size.

### 19.2 Assumptions

| # | Assumption | Established by |
| --- | --- | --- |
| M1 | Every FileIO request and batch is statically bounded. | §12.7 |
| M2 | Ranges cannot be stored or escape their scope; buffers are reused only after borrows die. | §12.6, R12 |
| M3 | At most `T` roots are started concurrently because every started root retains a worker and all concurrent root ingress uses the same bounded worker population; in inline mode the root-ingress gate admits one root at a time. | E7, E9, R3, R4, §5.2 |
| M4 | Each lowered pipeline has at most `K` chunk reads in flight. | R9 |
| M5 | Runtime buffer allocator overhead is a bounded multiple of live bounded buffers. | Runtime allocator requirement |
| M6 | `R` is finite: the number of simultaneously live Ranges one root can hold has a static bound. | Ordinary recursion is forbidden in Moss v0.1, and the concrete domain routing graph is closed and acyclic. A root's synchronous call graph, functions and nested messages alike, is therefore acyclic and has finite depth. Each frame has finitely many Range/RangeBatch bindings, RangeBatch cardinality is bounded (§12.7), and Ranges cannot accumulate in collections or domain state (§12.6). |

### 19.3 Lemma 2 — direct FileIO buffers are bounded

At most `T` roots are started at once.

Each root retains at most `R` live Range-backed buffers (M2, M6) of at most `B` bytes each, plus at most `B` bytes for a direct FileIO operation currently in progress.

Therefore direct FileIO buffers are bounded by:

```text
T × (R + 1) × B
```

### 19.4 Lemma 3 — chunk read buffers are bounded

One lowered pipeline keeps at most `K` chunk reads in flight, each of at most `C` bytes (M4).

A root has at most one active lowered pipeline. Map and combine make no FileIO calls (§13.2), so pipelines cannot nest, and a pipeline joins before its root's next statement. At most `T` roots are started at once (M3), so chunk read buffers are bounded by:

```text
T × K × C
```

### 19.5 Theorem 2

Combining Lemmas 2 and 3:

```text
M_FileIO ≤ T × [ (R + 1)B + KC ]
```

By M5, allocator footprint is a bounded multiple of that amount.

The bound is independent of file size. A slow disk does not create a Moss-side backlog of unbounded buffers: blocking stops the owning root, batch size is bounded, and chunk speculation is capped by `K`. ∎

### 19.6 What Proof 2 intentionally does not bound

Proof 2 does not bound:

- user/domain program state;
- mapped values produced by arbitrary user functions;
- the fold accumulator;
- kernel page cache or dirty-page memory;
- external libraries' allocations.

The executor does bound the **number** of pending chunk branches/results by `K`, but Phase 20 does not require a new general theorem that arbitrary user-produced values have a static byte bound.

That count is a real cost of lowering. The sequential program holds one mapped value at a time; a lowered pipeline can hold up to `K` awaiting ordered commit. Lowering can therefore raise peak program memory (§20.5).

Root descriptors are count-bounded by `queue_capacity`; their ordinary value payloads remain program/runtime values rather than FileIO buffers.

---

## 20. Proof 3 — sound parallel chunk lowering

Consider:

```moss
result = file.chunks(C) |> map(f) |> reduce(initial, combine)
```

### 20.1 Sequential reference semantics

The sequential meaning is:

```text
acc = initial
for i = 0, 1, 2, ...:
  x = read(i × C, C)

  if x is zero-length:
    stop and return acc

  y = f(x)
  acc = combine(acc, y)

  if len(x) < C:
    stop and return acc
```

An I/O failure fails the pipeline rather than returning a successful result. Recoverable representation belongs to Phase 21.

### 20.2 Lowered execution

The compiler may keep at most `K` chunk requests in flight.

Each branch:

1. holds only a scoped read borrow of the owning FileIO;
2. reads its fixed explicit offset;
3. applies pure `f` to a non-terminal chunk result;
4. publishes an indexed outcome to the parent root.

The parent observes outcomes strictly by increasing chunk index.

For each index in order:

- a full chunk commits `f(chunk)` into the original left fold;
- a non-empty short chunk commits its mapped value and then terminates the pipeline;
- a zero-length chunk terminates without contributing a value;
- an error fails the pipeline;
- any speculative outcome at a later index is discarded once an earlier terminal outcome has been committed.

This ordered-commit rule is the semantic boundary between speculation and the source program.

### 20.3 Conditions

| # | Condition | Established by |
| --- | --- | --- |
| P1 | `initial` is evaluated before any speculative branch work; `f` is pure, reads only its arguments, writes no Moss/domain state, and makes no FileIO call. | Lowering order + compiler effect analysis |
| P2 | `combine` executes only in the parent in source order and makes no FileIO call. It may have ordinary parent-root/domain-state effects. | Compiler effect analysis + ordered fold |
| P3 | No other live Moss FileIO aliases this physical file. | Unique `(device,inode)` registry |
| P4 | If the FileIO is a domain field, the owning root's READ access excludes WRITE/open/close/sync on that field for the pipeline lifetime. | Moss effect-derived locking |
| P5 | A root-local FileIO is exclusively owned by that root; branches receive only scoped read borrows. | FileIO ownership rules |
| P6 | The file is not concurrently modified outside Moss FileIO during the operation: not by another process, and not by host (Rust top-half) code in this process. | Environmental condition for equivalence |
| P7 | Outcomes are committed and folded in source chunk order; speculative later work is unobservable. | R9 |

P6 is intentionally not a runtime promise. If another process or host code modifies the file concurrently, the reads have ordinary OS semantics and Moss makes no sequential-equivalence guarantee for the compiler-parallelized pipeline.

### 20.4 Theorem 3

Assume P1–P7 and a successful sequence of required reads.

Because of P3–P6, Moss itself cannot modify the file through another FileIO while the pipeline is live, and the bytes observed by each required explicit-offset read are stable for the comparison.

For each sequentially required chunk `x_i`, the lowered execution reads the same region. By P1, evaluating `f(x_i)` in a branch produces the same value `y_i` as evaluating it in the sequential program and creates no observable state change merely by happening early.

By P2 and P7, the parent exposes `y_0, y_1, ...` to `combine` in exactly the same order as the sequential reference semantics and begins from the same `initial` value.

Therefore it executes:

```text
combine(... combine(combine(initial, y_0), y_1) ..., y_n)
```

exactly as the source program does.

If a short chunk terminates the sequential sequence, ordered commit includes that chunk once and discards all speculative later outcomes. If a zero-length read terminates it, no later speculative outcome is observed.

Thus the lowered successful execution returns the same result and produces the same fold-side effects as the sequential reference execution. ∎

### 20.5 I/O failures and speculation

Parallel execution can issue a read the sequential execution would never reach.

Those speculative reads and pure maps are not observable after an earlier EOF/short-read terminal outcome because their results are discarded.

If an in-order required chunk fails, the root fails and no successful later value is committed past that index.

Phase 20 does not promise that environmental I/O failures occur identically under two different physical schedules; Phase 21 defines recoverable failure values and reporting policy. The semantic guarantee here is that speculation does not expose successful results or fold effects past the first in-order terminal event.

Theorem 3 compares successful executions. Speculation also costs memory: the lowered run keeps up to `K` mapped values alive where the sequential run keeps one (§19.6), so it can exhaust memory where the sequential run would not. That is a resource failure of the lowered run, never a different successful result.

### 20.6 Optional tree reduction

The baseline proof requires **no algebraic property of `combine`** because the actual fold order is unchanged.

An optimizer may later use an order-preserving tree only if it has valid evidence that regrouping is semantics-preserving and handles `initial` correctly. Commutativity would additionally permit reordering, but Phase 20 does not assume it.

---

## 21. Word-count walkthrough

The front example ("Word count: the design in one example") demonstrates why ordered parallel map + sequential fold is useful.

The source says one root, admitted through `main`'s synchronous `message`, and one sequential chunk pipeline inside it.

The runtime may execute:

```text
WordCount.Count root
      │
      ├── read/map chunk 0 ─┐
      ├── read/map chunk 1 ─┤
      ├── read/map chunk 2 ─┼──→ ordered results
      └── read/map chunk 3 ─┘          │
                                       ↓
                                left-to-right merge
```

A word may span a chunk boundary. The summary therefore carries enough boundary state for `merge` to reconstruct words crossing adjacent chunks.

The baseline parallelization does not need `merge` to be associative because `merge` still executes left-to-right in file order.

### 21.1 Optional associativity fact for word summaries

For a byte string `x`, let `s(x)` retain its left boundary fragment, complete interior words, and right boundary fragment. Define merge so that adjacent boundary fragments are joined exactly as concatenating the two byte strings would join them.

Then:

```text
s(xy) = s(x) ⊕ s(y)
```

for all `x` and `y`.

It follows that on summaries produced from real byte strings:

```text
(s(x) ⊕ s(y)) ⊕ s(z)
  = s(xyz)
  = s(x) ⊕ (s(y) ⊕ s(z))
```

so the word-summary merge is associative. It is not commutative because left and right boundary positions matter.

That fact may justify a later order-preserving tree optimization, but it is not required for the Phase 20 baseline lowering.

---

## 22. Blocking while holding domain state

Blocking is safe under the Solo proof even when the root holds Moss locks, but doing so may unnecessarily serialize unrelated work that needs the same state.

The compiler therefore emits a warning when a handler performs potentially blocking FileIO while holding WRITE/CONSUME access to other shared domain state.

Example shape:

```moss
domain Cache:
  table: Map
  file: FileIO

  fn Fill(key, offset):
    # table is mutated in this execution
    data = file.read(offset, 4096)
    # slow FileIO can hold table's write protection across the wait
    ...
```

This is a performance diagnostic, not a correctness error.

A domain-field FileIO's own generated READ/WRITE protection does not by itself make every FileIO operation diagnostic-worthy; the warning is aimed at other shared state whose progress becomes coupled to storage latency.

For high-concurrency shared-state completion workloads, use the sans-I/O split described in §26 rather than holding shared-state locks across disk waits.

---

## 23. Many independent files

A stateless domain instance may service many independent roots, each with its own root-local FileIO.

```moss
fn main():
  worker = Worker()

  executor = Executor().threads(8).start()

  for path in files:
    executor.invoke(worker.Process(path))

  executor.join()
```

If the paths resolve to different inodes, the roots may proceed independently subject to worker availability.

If two concurrent opens resolve to the same `(device,inode)`, the second live FileIO claim is a runtime failure. Phase 21 will define recoverable error plumbing; Phase 20 preserves the fail-closed behavior for unexpected root failure.

This failure depends on timing: two roots on the same inode collide only if their FileIO lifetimes overlap. Failing is still the right rule, because waiting would be a root-on-root wait (§10). It is therefore named explicitly in the Phase 21 list. Programs whose roots should share one file use a domain-field FileIO (§11): it is opened once, readers overlap under READ, and it never collides.

Each independent root may itself expose chunk branches. Root-level and branch-level concurrency share the same physical executor resources (§7.5).

---

## 24. Batched random reads

For random-access storage workloads, one synchronous FileIO call can still expose deep device parallelism.

Example flow:

1. compute a bounded list of offsets;
2. issue one batch read;
3. wait for the batch;
4. consume/install the returned ranges;
5. allow all ranges to die before their buffers are reused.

This is suitable for workloads such as fetching a set of file-backed KV blocks without introducing user-visible async tasks.

If installing those blocks requires heavily shared domain state, a program should avoid holding that state locked across the batch wait; the Rust top-half sans-I/O pattern may be a better architecture.

---

## 25. Durable save

A durable create/save follows this model:

1. open the file with `create`;
2. write bytes at explicit offsets;
3. call `sync()` or `sync(dataonly)`;
4. if the open created the file, that first sync also syncs the parent directory;
5. close the FileIO.

`close()` alone does not promise durability.

Because no Phase 20 mode truncates, this pattern is exact for a newly created file. Saving shorter contents over an existing, longer file leaves the old file's tail in place. Overwriting in place needs truncate, and atomic replacement (write a temporary file, sync it, rename it over the target, sync the directory) needs rename; both are parked (§29).

A sync failure is not blindly retried. The program may need to rewrite from its own source data; recoverable policy belongs to Phase 21.

---

## 26. Beyond the core

### 26.1 Native io_uring-style library

A specialized library may later expose explicit submission/completion queues without changing Moss's domain model.

Outstanding asynchronous kernel operations must own their buffers because the kernel may still access them after submission returns; ordinary borrowed Range rules are insufficient for such an API.

A blocking wait for completion can still be a Solo wait when completion does not require a Moss root.

### 26.2 Shared-state completion workloads

Some programs are deadlock-safe with blocking FileIO but perform poorly because a handler would retain shared-state locks across storage latency.

Examples include:

- buffer-pool misses for many independent queries;
- concurrent tree descents through shared pages;
- cache fills for many requests;
- transaction/log coordination;
- overlapped LLM KV transfers.

The Phase 20 answer is the **sans-I/O Rust top half**:

> **Moss is the compute and state engine. Rust is the DMA/I/O engine.**

Moss owns computation, scheduling decisions, and shared request/cache state. Rust performs the long-lived data movement that Moss requests and reports completion:

- Moss handlers perform short state transitions;
- each `runtime_invoke` returns the handler's reply, which tells Rust what to move next;
- Rust performs the transfer without holding any Moss lock;
- completions re-enter Moss through `runtime_invoke`, using the same root-admission mechanism as every other root while the executor is active.

**Example: LLM KV transfer.**

```text
Rust DMA engine                    Moss compute/state
────────────────                   ──────────────────

runtime_invoke(kv.Need(req))
                 ───────────────→  inspect cache/request state
                 ←───────────────  reply DMARequest(page)

DMA transfer(page)
       │
       │ completion
       ▼
runtime_invoke(kv.Install(page))
                 ───────────────→  install page
                                   update waiters
                 ←───────────────  reply next work
```

Moss decides what computation and data movement are required and owns the shared request/cache state. Rust performs the long-lived DMA or I/O operation without holding Moss locks. The synchronous `runtime_invoke` reply tells Rust what transfer to perform; completion later re-enters Moss through the same root-admission mechanism as every other host-originated root. `Install` receives an ordinary page identifier/descriptor by value rather than transferring the KV byte block through Moss. Ownership and access semantics for externally managed DMA buffers are outside Phase 20.

Because the reply is the control channel from Moss to Rust, Moss never waits on the driver and Proof 1 needs no new case. Sans-I/O protocol libraries work the same way; h11's calls, for example, return the bytes to send. An egress queue that a root could block on would instead be a wait on a host thread that may itself be inside `runtime_invoke`, which is a Duo dependency.

The Rust driver is responsible for admission, backpressure, and one-completion-per-operation discipline for its external DMA/I/O operations. Any Moss execution it requests through `runtime_invoke` still uses Moss root admission.

The previously explored in-Moss cursor design remains parked as an alternative if this class later needs its I/O logic expressed directly in Moss.

### 26.3 Duo I/O

Sockets, pipes, and other in-process-cooperative I/O remain outside FileIO.

They stay in the Rust top half until a later protocol layer supplies a model whose progress proof explicitly covers both parties.

---

## 27. Design rationale

### 27.1 Why blocking FileIO

The earlier cursor design pushed I/O completions into handlers as new roots. It could be made safe, but reading and writing became separated by callbacks, credits, readiness notifications, and continuation state.

Blocking restores the natural program structure:

```text
read
compute
write
```

A slow write automatically applies backpressure because the root cannot issue the next read until the write finishes.

### 27.2 Why explicit offsets

An implicit shared file position would make every operation mutate hidden shared state and therefore serialize otherwise independent reads.

Explicit offsets expose independence directly.

### 27.3 Why `executor.invoke`

`executor.invoke(domain.Handler(args...))` names the semantic operation: create an independent root.

The receiver is the point. `invoke` tells a particular executor to run this handler invocation on one of its threads; it is not a language-level async call. Only code with access to an executor can invoke, and in Phase 20 that is only `main`.

The compiler can statically resolve the target without creating handler values or callable task objects. Internal enqueueing remains a runtime implementation detail.

### 27.4 Why roots and branches are different

Roots may hold Moss locks. Branches are compiler-controlled child work whose safety depends on remaining inside one root's lock context.

Allowing a joiner to run arbitrary queued roots would mix independent lock contexts on one thread and break the deadlock proof.

### 27.5 Why unique FileIO identity remains runtime-checked

Moss locks protect a **capability**, not pathnames.

Two paths can identify the same inode. Rejecting a second live FileIO avoids hidden same-file aliasing without turning the inode registry into a second synchronization system.

Within a domain-field FileIO, normal Moss READ/WRITE locking controls concurrency. A root-local FileIO needs no Moss lock.

### 27.6 Why no move for FileIO

Moss generally supports ownership transfer for ordinary non-Copy values, but FileIO deliberately has stricter pinned ownership.

A capability's synchronization/lifecycle meaning is tied to the root-local binding or domain field that owns it. Allowing move would add owner-state transitions with no Phase 20 use case.

Borrowing gives helpers and compiler branches the access they need without changing ownership.

---

## 28. Final Phase 20 contract

Phase 20 establishes the following contract:

> **Moss supports synchronous blocking FileIO for Solo regular files and one optional Rust-backed Executor for root-level concurrency. `executor.invoke(concreteDomain.Handler(args...))` is compiler-recognized and lowers to internal root submission; arguments cross by value and invoked handlers return no value in Phase 20. All concurrent Moss roots share the active executor's bounded admission mechanism, while compiler-generated branches remain children of exactly one root and use root-scoped helping. Host `runtime_invoke` enters through the same root-admission mechanism and synchronously returns the handler's reply; it introduces no second Moss execution population. FileIO has pinned ownership: it may be root-local or domain state, may be synchronously borrowed, and may never be copied, moved, or transferred across a root/domain boundary. Domain-field FileIO operations use ordinary Moss READ/WRITE locking; root-local FileIO needs no Moss lock. The runtime rejects a second live FileIO for the same `(device,inode)`. Blocking compensation is finite, a waking lock holder never waits for an executor slot, and eligible chunk pipelines may parallelize reads/maps only while preserving ordered sequential fold semantics.**

The execution model has three entry points and one substrate:

```text
Architectural concurrency
    executor.invoke(...)
        ↓
      roots

Compiler-discovered parallelism
    chunks |> map |> reduce
        ↓
     branches

External data movement
    runtime_invoke ↔ Rust DMA/I/O
        ↓
      roots
```

All three converge on the same Moss synchronization model and the same bounded execution substrate.

The design introduces no second programming model. The programmer still writes synchronous Moss; the compiler and runtime decide how provably independent work uses physical threads.

---

## 29. Status and deferred work

The Phase 20 core design is closed.

### Phase 20.1 hardening

Phase 20.1 made the no-root-originated-admission rule explicit and added regressions for bounded-admission deadlock. It did not change Phase 20 source semantics or add new concurrency constructs.

### Phase 20.2 reviewer completeness hardening

Phase 20.2 hardened FileIO, Range semantics, request bounds propagation, chunk lowering, and generic callback specialization against the independent reviewer probe suite. The strict suite is permanently integrated into the normal repository gate (`tests/run.sh`).

#### Owner decisions pending after completeness review

1. **Range/String equality.** The checker currently rejects `Range == String`
   and `String == Range` with `TYPE_MISMATCH`. This is a provisional design
   choice, not a settled owner decision. Confirm the strict byte/text type
   distinction or specify explicit conversion semantics.
2. **Untyped function specialization.** Current finalization marks every
   function with any remaining untyped parameter as generic with static
   dispatch, including functions unrelated to FileIO. This broadens
   acceptance and can increase specialization count, generated code size, and
   compilation work. Existing concrete calls remain statically closed; the
   review suite and repository gates audit their behavior. A narrower
   alternative is contextual specialization only for untyped functions used
   as functional callables or reached with a concrete Range/Vector argument
   on the Phase 20 path. That alternative needs an explicit owner decision
   before replacing current behavior.
3. **Bound narrowing.** Compile-time constants and immutable aliases are the
   current implementation scope. Startup-declared bounds and compiler-proven
   dynamic clamps remain deferred. A proven clamp can satisfy a static upper
   bound and is compatible with the bounded-memory proof.

### Phase 20.5 performance follow-up

Phase 20.5 is scheduled to benchmark the performance and contention characteristics of:

- `MossFairMutex` (ticket lock vs unfair OS mutex under high contender thread counts);
- executor central lock contention during high root-throughput churn;
- `FileIO.inner` locking overhead across sequential vs parallel reads;
- Solo hook locks and compensation worker spawn latency;
- generated Moss `std::sync::RwLock` read/read scaling and read/write contention under domain lock traffic.

### Deferred to Phase 21

- recoverable FileIO errors, including the same-inode open collision (§10, §23), whose occurrence depends on whether two roots' FileIO lifetimes overlap;
- result-carrying executor join;
- recovery without process abort after root failure;
- policy for partial writes/other roots still in flight when a root fails.

### Parked for later optimization/runtime phases

- conflict-aware root admission using conservative ClassSets (Phase 23 candidate);
- multiple executors;
- root submission from arbitrary handlers;
- suspended/asynchronous FileIO instead of blocking workers;
- batched writes;
- prefetch hints;
- rename, truncate, stat, and broader filesystem surface (a later truncating open mode must truncate only after the registry claim succeeds, so a rejected open never modifies the live holder's file);
- generalized algebraic/tree reductions where proof evidence exists.

### Later protocol work

- Duo I/O such as sockets and pipes;
- the in-Moss cursor model if future workloads justify its additional language/runtime weight.
