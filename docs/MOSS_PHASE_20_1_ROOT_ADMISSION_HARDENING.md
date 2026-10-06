# Moss Phase 20.1 — Root Admission Reentrancy Hardening

**Specification and Hardening Report**  
*Branch: `phase-20.1-root-admission-hardening`*

---

## 1. Overview and Purpose

Phase 20 introduced explicit root concurrency (`executor.invoke`), compiler branches, and blocking FileIO.

Phase 20.1 makes the underlying safety invariant against root-admission deadlock explicit, centralizes runtime context validation, and establishes deterministic regression coverage for bounded-queue deadlock topologies.

Phase 20.1 does **not** change Moss source semantics, introduce new concurrency constructs, or modify the execution model.

---

## 2. Invariants and Specification Updates

### 2.1 Invariant E12 — No Root-Originated Root Admission

Added to `docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md` (§16):

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

### 2.2 Strengthened Assumption A6

Assumption A6 now derives directly as a structural consequence of invariant E12:

> **A6 — Admission waits occur only outside Root execution.**
>
> A caller waiting for root admission is not currently executing a Moss Root and therefore holds no Moss lock as part of Moss execution.
>
> A running Root is forbidden by E12 from entering root admission. Consequently, bounded root-queue backpressure cannot block a Root while that Root holds Moss locks.

### 2.3 Motivating Deadlock Topology

The exact failure topology prevented by E12 is documented in Proof 1a (§18.2):

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

*E12 removes the edge from a running Root to root admission. Therefore this cycle cannot arise.*

### 2.4 Clarifications: `executor.invoke` and `runtime_invoke`

- **`executor.invoke(...)`**: Restricting `invoke` to `main` is a deadlock-safety requirement rather than merely an ownership or syntax constraint. `invoke` performs bounded admission and may wait for queue capacity; permitting this from a running handler could block while holding Moss locks.
- **`runtime_invoke`**: An external ingress API for host threads outside Moss execution. It cannot be called recursively from an active Root or Branch context.
- **Roots vs Branches**:
  - *Synchronous nested message:* Same Root (allowed).
  - *Compiler Branch:* Child work of same Root (allowed).
  - *`executor.invoke` / `runtime_invoke`:* New independent Root (forbidden during Root execution).

### 2.5 Runtime ABI Contract

`docs/ROOT_RUNTIME_ABI.md` documents the explicit root ingress requirement:

```text
root ingress requires:
    current_root_id == None
```

---

## 3. Implementation Details

### 3.1 Centralized Runtime Ingress Assertion

In `src/executor_runtime.hpp`, added `assert_external_root_ingress`:

```rust
#[inline]
fn assert_external_root_ingress(op: &str) {
    if let Some(rid) = current_root_id() {
        panic!("illegal root ingress ('{}') from within active Root (id={}): running Roots cannot admit new Roots (E12)", op, rid.0);
    }
    if let Some(wid) = current_worker_id() {
        panic!("illegal root ingress ('{}') from within executor worker (id={}): worker threads cannot admit new Roots (E12)", op, wid.0);
    }
}
```

Guarded call sites:
- `moss_root_start_with_config`
- `MossExecutorHandle::enqueue_root`
- `MossExecutorHandle::join`
- `runtime_invoke`
- `admit_root` (defensive check before acquiring tickets or waiting on condition variables)

### 3.2 Compiler Enforcement and Unit Regression

- The compiler retains strict confinement of `Executor` and `executor.invoke` to `main`.
- Added unit regression `Checker::unit_test_executor_invoke_outside_main()` invoked via `moss --self-test` to permanently test the `EXECUTOR_INVOKE_OUTSIDE_MAIN` diagnostic path.
- Added `tests/negative/phase20_executor_construct_outside_main.moss` verifying `EXECUTOR_CONSTRUCT_OUTSIDE_MAIN`.

---

## 4. Verification and Regressions

### 4.1 Test Suites

1. **`tests/tooling/check_phase20c_executor.py` (54/54 passed)**
   - **Test 50 (`50-root-admission-full-queue-reentrancy-rejected`):** Configures `threads=1, max_threads=1, queue_capacity=1`. Root A on the only worker attempts root admission when the queue is full; verifies immediate fail-closed rejection without deadlocking.
   - **Test 51 (`51-branch-context-root-ingress-rejected`):** Verifies that a compiler Branch inherits the Root context and rejects root admission while Branch publish/join completes successfully.
   - **Test 52 (`52-active-recursive-runtime-invoke-rejected`):** Verifies that recursive `runtime_invoke` in ACTIVE mode fails closed before admission.
2. **`tests/tooling/check_phase20_executor_fileio.py` (passed)**
   - Verifies compiler self-test `--self-test` for `EXECUTOR_INVOKE_OUTSIDE_MAIN`.
   - Verifies `tests/negative/phase20_executor_construct_outside_main.moss`.
3. **Repository Regression Suites (`make check`, `make examples`)**
   - All standard compiler and language tests pass without regressions.
