# Phase 20 R5 runtime lock inventory

This inventory covers production synchronization reachable by a Root or Branch
in `executor_runtime.hpp` and `fileio_runtime.hpp`. Generated handler `RwLock`s
are Moss locks under A2, outside R5. `moss_perf` and `test` hooks are listed
separately. A ticket protocol is not counted as a complete fairness proof if
the holder of the served ticket can starve on the ordinary mutex protecting
the protocol state. Repeated paths now use one shared `MossFairMutex` primitive:
atomic FIFO predecessor linking, parking with `thread::park`, and direct
predecessor release via `unpark`. A served entrant does not reacquire an
ordinary state mutex. Each node's `Arc` ownership remains live until the
successor has observed release. A release racing waiter registration is safe:
the successor checks the released bit after registering, while `unpark`
retains a token if the successor has not parked yet. There is no spin loop.
`MossFairCondvar` registers its waiter under the same fair state guard before
dropping that guard to park, so notifications cannot be lost between a
predicate check and wait registration. The holder performs no kernel wait,
join, or acquisition of another runtime lock.

| Lock | Purpose and possible waiters | Repeated bypass? | Held across another lock, kernel wait, or join? | R5 disposition |
| --- | --- | --- | --- | --- |
| `MossGlobalExec.inner` | Root admission, worker scheduling, Solo compensation, drain state. Host submitters, started Roots/Branches, and workers can wait. | Yes; fair leaf admission prevents later acquisitions from bypassing an enqueued waiter. Admission tickets still order queued Roots. | Fair condition waits release the guard; worker creation and joins occur after release. No nested runtime lock in production. | Satisfies R5 via `MossFairMutex` and `MossFairCondvar`. |
| `MossGlobalExec.branch_scopes` | Ready Branch-scope queue. Publishing/joining Roots and worker Branch schedulers can wait. | Yes; fair leaf admission orders the repeated accesses. | Pending lock is acquired only after this guard is dropped; no kernel wait or join while held. | Satisfies R5 via `MossFairMutex`. |
| `MossGlobalExec.worker_threads` | Tracks worker join handles and reaps exited compensation workers. A compensation-triggering Root and the joining host can wait. | Yes; fair leaf admission orders compensation cycles and join-handle collection. | Finished handles are joined after the vector guard is dropped. | Satisfies R5 via `MossFairMutex`. |
| `MossProcessRuntime.inner` | INLINE/ACTIVE/DRAINING ingress, inline tickets, executor publication. Host callers, a finishing inline Root, and Roots/Branches querying the active executor can wait. | Yes; fair leaf admission prevents later ingress and Branch queries from bypassing an enqueued waiter. | Fair condition waits release the guard; executor admission and Root execution happen after release. | Satisfies R5 via `MossFairMutex` and `MossFairCondvar`. |
| `runtime_invoke` reply-slot mutex | One Root writes one reply, its one host caller takes that reply. The Root or caller can wait. | No: at most two one-shot critical sections for this slot. | Reply Condvar wait releases the guard; no other lock or kernel wait while held. | Satisfies R5 by finite one-shot contention; no fair implementation needed. |
| `MossBranchScopeState.pending` | One scope's bounded publication and ready work. Its owner Root and worker Branch schedulers can wait. | For compiler-generated scopes, no: the fixed K=4 window and one-shot join give a finite set of acquisitions before that Root joins. | Ready-queue operations and Branch execution occur after release. | Satisfies R5 for generated Phase 20 use by finite contention. The raw runtime API is not a general unbounded publication proof. |
| `MossBranchScopeState.done_mutex` | Coordinates one joining Root with the last completing Branch. | No: one join and one final completion per scope. | Condvar wait releases the guard; Branch execution is outside it. | Satisfies R5 by one-shot contention. |
| Compiler-owned chunk result-slot mutex (`file_chunk_codegen.inc`) | One Branch writes its result once; its parent Root reads only after `branch_join` has completed that Branch. | No: one writer and one post-join reader, with no simultaneous acquisition under the generated protocol. | Neither acquisition waits for a kernel operation or joins while holding the slot. | Satisfies R5 by one-shot, ordered access; a fair replacement would add no progress guarantee. |
| `FileIO.inner` | Descriptor state/lifecycle and short read borrow. A FileIO Root and its compiler-created Branches can wait. | Repeated valid host ingress is possible; fair leaf admission orders all descriptor-state accesses. | All `pread`, `pwrite`, `fsync`, `close`, metadata, registry, and Solo callbacks occur after the guard is dropped. `Debug` snapshots state under the guard and formats after release. | Satisfies R5 via `MossFairMutex`. |
| `MOSS_REGISTRY.state` within `FairRegistryLock` | Protects inode claim set and served-ticket state. FileIO Roots can wait. | Existing tickets keep claim/release order; fair leaf admission now prevents later arrivals from indefinitely bypassing a served ticket at the state guard. | Registry HashSet operations only; fair condition wait releases the guard; no FileIO syscall or other runtime lock while held. | Satisfies R5 via the preserved ticket protocol over `MossFairMutex` and `MossFairCondvar`. |
| `MOSS_SOLO_ENTER_HOOK`, `MOSS_SOLO_LEAVE_HOOK` | Copy callback pointers used at every Solo entry/exit. FileIO Roots and Branches can wait. | Repeated operations are ordered by fair leaf admission. | Callback runs after guard release; no kernel wait or join while held. | Satisfies R5 via `MossFairMutex`. |

`MOSS_CLOSE_OVERRIDE` and `MOSS_SYNC_HOOK` in `fileio_runtime.hpp`, all
`moss_perf` observer state, and registry trace/hold-turn support are test-only.
They are not production R5 obligations. The registry's ticket mechanism,
process Root ingress tickets, executor admission tickets, and FIFO Root queue
remain in place. `check_phase20_r5_fair.py` fixes the atomic enqueue order
with a test-only observer while a current turn is held, then asserts exact
critical-section entry order after release. The existing inode-registry
contention test continues to assert its ticket order.

The generated domain-field physical tests prove actual SHARED/SHARED overlap
and SHARED/EXCLUSIVE exclusion. They do not prove A2's fair-waiter premise for
the pre-existing Moss `RwLock`; that is a separate Moss synchronization-proof
issue and does not expand this R5 runtime-lock pass.
