# Moss Phase 21 — Error Handling

**Consolidated design document · v4.5.2 · October 9, 2026**

**Status:** Design-approved implementation baseline. Not implemented. Part III gives paper proofs for four §16 obligations, conditional on the implementation obligations O1–O9. The proofs are not machine-checked.

**Supersedes:** the separate v4.5.1 specification, the v4.5.1 example sheet, the §16 proof note, and every earlier Phase 21 draft. The language semantics are unchanged from v4.5.

**Builds on:**
- Phase 15.15's enum and ownership decisions;
- Phase 20's FileIO and Executor contracts;
- Phase 15.3's synchronization placement;
- the v0.1 language design, whose fail-closed text needs the revisions in §15 (V4-01).

**Repository checkpoint:** `kuttyb/moss` main at `395b4c0` (October 8, 2026).

## How to read this document

- **Part I — Error handling by example.** Fourteen short programs, from a first `raise` to the diagnostics. Start here.
- **Part II — Specification.** The normative rules, the 18-item acceptance checklist, V4-01 to V4-04, the conformance test plan, and the implementation DAG. References written `§N` point to Part II.
- **Part III — Proofs for §16.** Deadlock freedom and serializability for `try`/`recover`, and deadlock freedom for the fresh `on_fail` phase. Its sections are numbered P0–P11, and its implementation obligations O1–O9.

## Changes from v4.5.1

1. **Merged documents.** The example sheet is now Part I and the §16 proof note is now Part III. The proofs include three precision edits from peer review:
   - O3 and Lemma 2 are scoped to the handler's own domain;
   - Lemma Q covers raises that a Branch produces while its siblings are still running;
   - progress has moved out of Theorem B2 into Corollary B3.
2. **§3 step 5 and §2:** writes in `on_fail` and in `recover` arms can add *shared* locks to readers of the same leaves (P10, C1). The old wording, "never causes a normal-path inflation warning", overstated this.
3. **§7 and §15 (V4-01):** serializability now covers executions that exit by a typed raise (Theorem A2; P10, C2). The validity of the state they leave is still disclaimed.
4. **§16:** new status column. Four rows cite their Part III proofs, and O1–O9 are named as mandatory acceptance gates. §17 and §18 are updated to match, including a new "Proof gates" test row.
5. **Editorial:**
   - §3 step 3 notes that under the §8 ABI, exiting the body frame closes Root-local FileIO in the required order;
   - the note about the `!` compiler bug now states the behavior as verified;
   - the duplicate copy of that note in the examples was dropped.

---

## Part I — Error handling by example

Fourteen short programs that illustrate the design. They use the proposed Phase 21 forms (`raise`, `try`/`recover`, and handler `on_fail` trailers), which current Moss does not yet accept. Everything else (`domainroutes`, `Executor`, `FileIO`, one-line pipelines, and `not`) follows current Moss syntax. A snippet without a `main` assumes a suitable enclosing function or handler.

### Example 1. Expected errors use `raise` / `recover`

```moss
enum DivideError:
  Zero

fn divide(a: Int, b: Int) -> Int:
  if b == 0:
    raise DivideError.Zero
  return a / b

fn main():
  try:
    echo divide(10, 0)
  recover DivideError.Zero:
    echo "Cannot divide by zero"
```

Expected output:

```text
Cannot divide by zero
```

`divide` infers the `DivideError.Zero` raise effect automatically. A `try` **must** have at least one `recover` arm; it is not needed merely to activate a handler's `on_fail`.

### Example 2. `on_fail` is a handler trailer, not a `try` arm

```moss
enum JobError:
  Bad

domain Reporter:
  failures = 0

  fn Record():
    failures = failures + 1

  fn Count() -> Int:
    reply failures

domain Worker:
  domainroutes(reporter: Reporter)

  fn Run():
    raise JobError.Bad

  on_fail err:                 # SAME indentation as fn Run()
    message reporter.Record()

fn main():
  reporter = Reporter()
  worker = Worker(reporter: reporter)

  executor = Executor().threads(2).start()
  executor.invoke(worker.Run())
  executor.join()

  count = message reporter.Count()
  echo count
```

Expected output:

```text
1
```

`on_fail` attaches to the preceding handler and runs only for a **Root** invocation. It receives a definitely available error and executes after Branch quiescence, Root-local FileIO teardown, dropping old guards, and acquiring a fresh lock plan. The handler does not need `try` unless it also wants local `recover`.

### Example 3. Cause-based FileIO recovery: use a distinct fallback file

```moss
domain ConfigLoader:
  fn Load():
    try:
      file = FileIO.open("user.cfg", ro)
      file.close()
      echo "user configuration"
    recover FileError.NotFound:
      fallback = FileIO.open("default.cfg", ro)
      fallback.close()
      echo "default configuration"

  on_fail err:
    echo "configuration failed"
```

The fallback is a different file, not an `rw`/`create` retry on the same file. Other uncaught FileIO errors go to the Root-only trailer. `FileError.InUse` is **not** a retry signal; shared files should be owned by an appropriate domain-field FileIO.

### Example 4. One recovery body for multiple tag-only causes

```moss
domain Storage:
  fn Flush():
    file = FileIO.open("report.txt", rw)
    try:
      file.write(0, "new report")
      file.sync()
    recover FileError.Full, FileError.IO:
      file.write(0, "trusted replacement")
      file.sync()
    file.close()

  on_fail err:
    echo "flush failed"
```

The grouped recovery syntax is approved in Phase 21 v4.4. It consumes both tag-only variants. The first `write()` establishes new data before the first `sync()`; recovery repairs either a failed write or a failed sync. If `file.write` or the second `file.sync` raises, that **new** error goes outward to the handler's Root-only `on_fail`, not back into its own recovery arm. Sync can raise `Full` because some filesystems report allocation/writeback exhaustion only during synchronization. No automatic handle poisoning or implicit rollback occurs.

### Example 5. Recovery lock inflation can create a lock from none

```moss
enum InputError:
  Bad

fn check_input():
  raise InputError.Bad

domain Cache:
  value = 10

  fn Load():
    try:
      echo value
      check_input()
    recover InputError.Bad:
      value = 0
```

In an eligible one-handler domain, the original READ-only `value` access may be completely lock-free. Adding a WRITE in `recover` can make **every invocation** acquire an exclusive lock from handler entry. Moss warns about that real performance cost, but compiles it: recovery never performs a dynamic lock upgrade.

### Example 6. Distinguish `assert` inside tests from handler panics

```moss
test "bad integer":
  try:
    parse_int("abc")
    assert(false)         # If parsing unexpectedly succeeds, this test fails.
  recover ParseError.Invalid:
    pass
```

An assertion executed directly in an ordinary `test` body fails that test. A failed assertion in production code or inside a domain handler **aborts the process**; handler-panic regressions must be executed in a subprocess. An unhandled typed raise escaping a test also fails just that test.

### Example 7. Chunk pipelines are written on a single logical line

```moss
fn first_byte(chunk: Range) -> Int:
  return chunk[0]

# Inside a Root handler with an open FileIO named `file`:
total = file.chunks(4096) |> map(first_byte) |> reduce(0, add)
```

The eligible chunk mapper is only called on nonempty chunks, so the compiler can prove `chunk[0]` safe **at this pipeline call site**. The generic `first_byte` still may panic on an arbitrary empty Range; do not erase its global may-panic effect. Ordinary `fusion_safe()` continues to reject order-changing typed raises; chunk speculation uses its own predicate.

### Example 8. Range-loop bounds are fixed before iteration

```moss
fn demonstrate():
  n = 3
  for i in range(0, n):
    echo i
    n = 0
```

Expected output (three distinct lines):

```text
0
1
2
```

This native rule will also be required of Fast Debug when it implements `for` loops. For `for i in range(0, r.length())`, unchanged `r` and induction variable `i` prove `r[i]` in bounds.

### Example 9. Convert errors into a typed outcome with local `recover`

```moss
enum StoreError:
  Down

enum FetchOutcome:
  Success(value: Int)
  Unavailable

domain Store:
  fn Load(available: Bool) -> Int:
    if not available:
      raise StoreError.Down
    reply 42

domain Service:
  domainroutes(store: Store)

  fn Get(available: Bool) -> FetchOutcome:
    try:
      v = message store.Load(available)
      reply FetchOutcome.Success(value: v)
    recover StoreError.Down:
      reply FetchOutcome.Unavailable

fn main():
  store = Store()
  service = Service(store: store)
  answer = message service.Get(false)
  match answer:
    case Success(value):
      echo value
    case Unavailable:
      echo "service unavailable"
```

The caller receives a normal, typed `FetchOutcome`: `Success` carries a real value; `Unavailable` is a distinct variant, not an ambiguous `-1` sentinel. **The same contract holds whether `Service.Get` is called as a Root or by a nested handler**, because the error is locally recovered. Using Root-only `on_fail` here would be misleading: nested calls bypass the trailer and could instead receive a raw `StoreError.Down`. Reserve `on_fail` for abandoning a Root and cleaning up under a fresh lock plan. Its required reply on a value-returning handler is a safety backstop, not the usual error-to-outcome idiom. `main` and every Root ingress remain subject to the approved totality rules.

### Example 10. Multi-enum errors use typed trailer arms

```moss
domain Worker:
  domainroutes(reporter: Reporter)

  fn Work(text: String):
    number = parse_int(text)         # ParseError
    file = FileIO.open("out.txt", rw) # FileError
    file.close()

  on_fail ParseError.Invalid:
    message reporter.BadInput()

  on_fail FileError.Full, FileError.IO:
    message reporter.FileFailure()

  on_fail:
    message reporter.OtherFailure()
```

This avoids inventing a cross-enum error value for `on_fail err:`. A handler with a single concrete enum or scalar error type can still use `on_fail err:` to receive that value. All failure arms are **handler-level trailers**, and only the selected arm runs at a Root boundary after quiescence and lock replacement.

### Example 11. Bare re-raise retains the original cause

```moss
try:
  message store.Write()
recover FileError.Full, FileError.IO:
  message audit.NoteFailure()
  raise
```

The final `raise` is **the same original FileError variant and payload**, not a new generic failure. It propagates beyond the current `try` and is not caught by its sibling recovery arms. Recording must not itself introduce an unhandled error.

### Example 12. Catch every typed variant without inventing a union

```moss
fn main():
  try:
    parse_and_write()
  recover:
    echo "operation failed"
```

The last `recover:` handles any variant from any error enum, including newly introduced library variants. No `err` binding is exposed, so it can remain source-compatible as the inferred raise set evolves. If the programmer needs cause-specific behavior, qualified `recover` arms go first, followed by the final catch-all. A `recover:` arm can also use bare `raise` to forward whichever original variant was selected.

### Example 13. Diagnostics: no-op catch-all after mutation

```moss
enum WorkError:
  Down

domain Counter:
  value = 0

  fn Update():
    try:
      value = 1
      raise WorkError.Down
    recover:
      pass  # warning: empty catch-all can hide partially modified domain state
```

The compiler **warns but does not reject** a no-op catch-all that may swallow an error after domain-state writes. The same warning applies to a no-op handler-level `on_fail:` covering an abandoned attempt that may have written domain state. An explicit repair action or bare re-raise does not trigger this specific warning; neither case implies automatic rollback.

### Example 14. Diagnostics: a valid but unreachable arm is a warning

```moss
try:
  number = parse_int("42")
recover FileError.Full:
  echo "storage exhausted"   # warning: this try cannot raise FileError.Full
recover:
  echo "invalid numeric input"
```

`FileError.Full` is a defined variant, but it is outside this `try`'s inferred raise set. The arm is therefore **unreachable and produces a warning**, not a compilation failure. Duplicate/overlapping arms, unknown or deleted enum variants, and missing required Root coverage remain **errors**.

---

## Part II — Specification

> **Design headline:** `try` is the normal attempt; `recover` handles a typed alternative **inside the existing handler 2PL interval**, with warned entry-lock inflation; `on_fail` abandons an independent **Root** attempt, waits for children, **closes Root-local FileIO**, releases every old Root-held Moss guard, and then runs the handler's Root-only failure clause under a **fresh inferred lock plan**. Typed raises do not imply rollback, poisoning, or automatic restart.

### 0. Scope, status, and authoritative terminology

Part II contains (1) the agreed Phase 21 language/runtime design; (2) the two accepted P21-10 proofs; (3) the full **18-item Phase 21 backlog acceptance checklist**; (4) four cross-phase integration obligations **V4-01 through V4-04**; and (5) the approved v4.5 typed-dispatch and diagnostics rules. Part I illustrates the design with examples, and Part III proves four of the §16 obligations. Items labeled *design closed* still require compiler/runtime work, tests, or proof before implementation completion.

**Normative terms:**

- **Raised error:** An expected, typed alternative on the inferred `raise` control channel. It terminates the current operation, but may be recovered. It is **not** a process-aborting panic.
- **Panic:** Programmer/runtime failure that is not catchable with `recover` or `on_fail`; it follows the fail-closed abort path.
- **Handler interval:** A handler's currently acquired, ranked, two-phase Moss-managed lock interval. Nested synchronous handlers preserve their normal *open-nesting* semantics; no Root-wide closed transaction is implied.
- **Root:** An independently admitted handler invocation from a permitted ingress (`main` synchronous top-level message, `executor.invoke` in `main`, or Rust `runtime_invoke`). The same handler may also run as a nested message, which is **not** a Root.
- **Branch:** A compiler-created, scoped parallel worker inside a Root, not a newly admitted Root. Branch work is joined/quiesced before the parent commits any terminal result and before raised-path resource teardown.
- **`on_fail` trailer group:** One or more **handler-level** failure arms, at the same indentation as their owning `fn` declaration, immediately following that handler body. Each arm selects a typed failure cause, or a final catch-all. Lowered **only in the handler's Root-entry specialization**. It is not a `try` arm and is unavailable inside nested handlers and helpers.

**Explicit non-goals:** no user-authored `throws`, no `?`, no implicit propagation of ordinary returned error values, no exception class hierarchy, no panic catching, no speculative abort suppression, no dynamic Moss lock upgrade, no automatic transaction/rollback, no automatic domain restart, no futures/Task/result queues, no result-bearing `join()`, no general `Option` redesign, and no second Fast Debug POSIX FileIO runtime in Phase 21.

#### Decision record since v3

| ID | Adopted v4 decision |
|---|---|
| P21-01 | Variant-precise inferred raise sets; owned, heap-free bounded scalar/enum error payloads; `.mossi` summaries. |
| P21-05 | In a `test` block, an unhandled typed raise fails that test with a diagnostic; ordinary code still obeys Root/`main` totality. |
| P21-06 | FileIO errors are named by **cause** (`NotFound`, `PermissionDenied`, `NotRegularFile`, `InUse`, `Full`, `IO`), not by operation or raw errno. |
| P21-02 | `recover` transitive locks are included in the original handler entry plan; stronger/wider locks compile **with a warning**; deadlock freedom and serializability are proved in Part III (Theorems A1 and A2), conditional on obligations O1–O9. |
| P21-03 | Handler-level Root-only `on_fail` trailer group, at `fn` indentation, not a `try` arm; v4.5 adopts qualified pattern arms/catch-all and reply totality. D7 governs locals. Borrowed views and **all Root-local FileIO** are dead before the fresh-lock boundary; FileIO closure is explicit. Export failure bodies as separate provider callables; the application emits the Root-entry wrapper. |
| P21-04 | `on_fail` must have an **empty escaping raise set**. Nested recovery inside it is allowed; an escaping raise is a compile-time error. A panic still aborts. |
| P21-07 | Phase 20 `Drop for FileIO` already closes without sync and releases the inode claim; Phase 21 must force the raised-exit lifetime transition in the compiler and respect Branch quiescence. |
| P21-08 | `sync` may raise `FileError.Full` as well as `FileError.IO`; failed `sync` leaves durability uncertain; **no poisoned-handle/reopen requirement**. Rewriting from a trustworthy source and syncing successfully on the same handle is an allowed recovery. |
| P21-10 | Indexed outcomes and **ordered commit** permit speculative typed raises, but only provably **non-panicking** speculative maps are eligible. Two bounds proofs are adopted in §11. Do **not** weaken ordinary `fusion_safe()`. |
| P21-11 | A statically routed **reporting domain** replaces the previously deferred result-bearing `join()`. `join()` remains a drain; Roots that need failure reporting use their `on_fail` clauses. |
| P21-12 | IEEE-754 Float behavior, Moss-specified shortest-round-trip printing, exact parsing spellings, Float predicates, and checked Float-to-Int conversion; see §13. |
| P21-13 | Keep lowercase `option[T]` unchanged; defer general optional-data/enum convergence. |
| P21-14 | At most one panicking and at most one non-panicking prelude form where useful; do **not** require a panicking form for parsing or I/O. |
| P21-16 | Remove misleading “automatic supervision/restart” implication from Phase 21's name. Explicitly decline restart and rollback. |
| P21-17 | Split existing `may_fail` into `may_panic` and a typed raise set; retain all other effect-barrier obligations; update `.mossi` and native ABI only after representation is settled. |
| P21-18 | Native fault injection first; no obligation to implement FileIO in Fast Debug now. |
| V4-01 | Rewrite v0.1 §32, §33, Appendices A/B, and §42's fail-closed-dependent text without relaxing 2PL, observable ordering, borrow lifetimes, or rank constraints. |
| V4-02 | Dedicated **chunk speculation eligibility** predicate; ordinary fusion remains raise-order-sensitive and conservatively rejects raising callbacks. |
| V4-03 | **No WRITE mode on `Range` or `RangeBatch` parameters.** This prevents WRITE-through escape of borrowed views into a caller's binding. Other supported WRITE parameters remain legal. |
| V4-04 | Fast Debug's eventual `for` implementation evaluates `range` bounds once, matching native semantics. |
| V4.5 reply totality | Value-returning Root `on_fail` arms must reply with the handler's declared type; use a **typed ordinary outcome enum**, never a magic failure sentinel. Keep Root totality for all ingress modes, including synchronous top-level `message` from `main`. |
| V4.5 dispatch and propagation | Qualified patterned `on_fail` arms, final unbound `on_fail:`, bare re-raise inside `recover`, and final `recover:` are approved. No anonymous mixed-enum error binding. |
| V4.5 diagnostics | Warn when an **empty catch-all** can swallow an error after domain-state writes; warn (do not reject) for a well-formed named variant that cannot arise at that arm. Duplicates, unresolved enum variants, overlapping cases, and missing required coverage remain errors. |

---

### 1. The typed raise channel

A Moss operation may either return an ordinary result or terminate that attempt by `raise` of a typed error. The compiler **infers** the set of possible raised alternatives per concrete specialization, follows it through helper and nested-message calls, and removes precisely the variants recovered by a matching arm.

- A `return Outcome.Unavailable` is ordinary data and never raises merely because of its enum tag.
- `raise FileError.Full` transfers control out of the failed operation. No source-level forwarding operator is required in intermediate calls.
- A checked runtime primitive can originate a typed raise according to its declared contract.
- Sibling recovery arms match failures from the associated **original `try`**, not failures generated inside another recovery arm. A nested `try`/`recover` is the way to handle a second error. A **bare `raise` inside a `recover` arm** re-raises the selected original error unchanged; see §2.
- An error may be represented as a scalar or an existing Moss enum variant. A payload-bearing variant may contain **heap-free owned scalar/enum fields**. No strings, borrowed views, FileIO/Range/RangeBatch capabilities, mutable aliases, or other unbounded/heap-bearing objects in the raised payload; text diagnostics go to traces/logs.
- Inference is **variant-precise**, not merely whole-enum: handling `FileError.InUse` does not consume `FileError.Full`. Values carried by a variant do not require an infinitely precise value-set analysis.
- Concrete exported functions and the semantic IR in `.mossi` must retain the inferred raise variants and the separate panic effect. A missing provider effect record must fail closed/request rebuilding, not count as the empty set.
- Moss does not promise to intercept memory exhaustion, process termination, or arbitrary foreign Rust panics as ordinary raised errors.

#### Illustrative surface spelling

New grammar remains subject to compiler/parser integration; examples demonstrate agreed **behavior**, not evidence that v4 syntax is already accepted by the current parser.

```moss
enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO

# A leaf:
raise FileError.Full

# A caller; no `?` or `throws`:
try:
  message storage.write_primary(data)
recover FileError.NotFound:
  message storage.create_primary(data)  # Create missing file rather than retry the same open.
recover FileError.Full:
  message storage.write_backup(data)
```

A `Full` raised *inside* the `NotFound` arm does not jump into the sibling `Full` arm. It propagates to an **enclosing** handler of that new raise unless the `NotFound` arm contains its own nested `try`/`recover`.

### 2. `try`/`recover`: one existing protected interval

**Grammar:** a `try:` suite must be followed by **at least one** `recover` arm. A bare `try` without `recover` is a compile-time error; Root-wide abandonment needs no `try` because `on_fail` is attached to the handler, not to a lexical `try`. Any `on_fail` inside a `try` or helper is a syntax/role error.

**Grouped tag-only recovery:** one arm may name multiple fully qualified, tag-only variants, separated by commas: `recover FileError.Full, FileError.IO:`. It runs for either variant and consumes precisely those variants from the originating `try`'s inferred raise set. Reject duplicate/overlapping variants across sibling arms. Payload-bearing alternatives use separate pattern arms, so no ambiguous multi-pattern variable binding is introduced. A new raise *from inside* a grouped arm cannot be caught by that same arm or a sibling. These rules apply inside functions, handlers, and tests.

**Catch-all recovery (v4.5 adopted):** `recover:` as the **last** arm matches every remaining typed variant raised by the original `try`, regardless of enum or additions to a library's raise set. It introduces **no error-valued binding**, so no cross-enum type is required. `recover err:` is additionally permitted **only if every covered alternative belongs to one concrete enum or scalar type**, in which case `err` has exactly that type; otherwise emit a diagnostic suggesting `recover:` or qualified arms. A catch-all consumes the remaining alternatives unless its body re-raises them. **Scalar error types:** an unbound catch-all always handles them; a one-scalar-type `recover err:` or `on_fail err:` can expose the scalar value. Qualified pattern dispatch is for enum alternatives and does not silently add general scalar value-pattern inference. A later arm following a catch-all, a second catch-all, or an overlapping variant is illegal. **Unreachable pattern diagnostic:** when a valid, resolvable named variant is absent from the `try`'s statically inferred escaping raise set, warn rather than reject; this permits a library to stop *raising* a still-defined variant without breaking its consumers. A nonexistent or removed enum variant is still an invalid pattern and remains a compile-time error. Do not treat a duplicate/overlapping arm as merely unreachable.

**Bare re-raise (v4.5 adopted):** a statement consisting of `raise` with **no expression** is legal only lexically within an active `recover` arm. It re-emits that arm's **original selected typed error, including its exact variant and owned payload**, even from a grouped or unbound catch-all arm. The compiler retains the selected error in an internal tagged slot; a bare `raise` does not require a source-level binding or invent a new type. The original `try`'s sibling arms cannot catch this re-raise; it propagates to the **next enclosing** recovery context or Root boundary. In nested recovery arms, bare `raise` refers to the **innermost active selected error**. A helper called from an arm cannot access the caller's selected error by bare `raise`; it uses its own explicit `raise <value>`. A bare `raise` outside any recovery arm is a compiler error. A re-raising arm contributes the re-raised alternatives to the **escaping** raise set; it does not consume those variants for totality checking.

**Example: clean up locally, then let the caller decide.**

```moss
try:
  message store.write(data)
recover FileError.Full, FileError.IO:
  message audit.RecordFailure()
  raise
```

`audit.RecordFailure()` must itself have no unhandled typed raise on this path, or a distinct new failure may replace the active error. Use a nested recovery if recording can fail. The re-raised FileError propagates beyond this `try` under the original enclosing handler interval; it does **not** trigger an early lock release.

`try` establishes the normal-path effect baseline. A `recover` arm expresses an alternative/retry **under the same handler-scoped 2PL protection**. It may be nested to any finite lexical depth within existing Moss's no-recursion restrictions. A failed operation does not resume at the point of failure: retries initiate a new call.

Let `L_normal` be the actual baseline lock plan for the handler, accounting for any existing eligible Phase 15.3 placement outside recovery. Let `L_recover` be the additional transitive requirements of all recovery alternatives including nested recovery. The compiler constructs a sound ranked plan for both, symbolically `L_entry = L_normal ⊔ L_recover`, merging classes and taking the stronger lock mode when necessary.

**For every recovery-only additional class or stronger lock mode:** acquire/plan it at the corresponding handler entry according to existing ranked 2PL; never opportunistically upgrade or acquire it after a raising action. When the merged plan materially inflates the *actual* normal-path plan, **emit a warning and continue compiling**. The warning identifies extra/stronger classes and their recovery provenance. Do not reject solely for reduced concurrency.

Phase 15.3's bounded deferral covers an eligible **leading** conditional and cannot generally be applied to a mid-body `recover`: moving its acquisition past an earlier fallible message/I/O could cross an observable-action barrier. This does not disable unrelated valid Phase 15.3 placements elsewhere.

**Scope restriction:** a handler's plan is not a Root-wide union of all descendant domain locks; normal nested message acquisition, retention, and release remain as in Phase 20. Existing static graph closure, rank, absence of upgrades, conflict-serialization, and 2PL obligations stay mandatory.

**Warning edge case:** in an eligible single-handler domain, a field that is only READ may require no Moss lock under the existing immutable/read-only optimization. If `recover` first introduces a WRITE to that field, the handler may move from **no lock at all to an exclusive lock on every invocation**. The entry-plan warning must report this real baseline-to-recovery cost, rather than assuming every READ always held a shared lock. A recovery write can also add **shared** locks to *other* handlers that read the same leaf lock-free, as any new writer does; this per-handler warning does not report that cost (Part III, P10, C1).

**Empty catch-all warning:** If `recover:` or a typed single-enum catch-all `recover err:` has an empty/no-op body (including only `pass`) and the protected `try` may already have written domain state along a path reaching that catch-all, emit a non-fatal warning about swallowing failure after a partial mutation. This does not turn a typed raise into a rollback. A genuine recovery action, explicit re-raise, or catch-all protecting read-only/pure code does not trigger this specific warning. The same principle applies to Root-level catch-all `on_fail:` or `on_fail err:` with writes in the abandoned attempt (§3). Use conservative transitive effect analysis where needed; warnings should give the protected write provenance when available.

#### Lexical binding and lifetime rules for `try`

Extend Moss's existing `if`-join scoping rule to `try`/`recover`. A **new name first bound in the `try` suite is not visible in any sibling `recover` arm**, even if the binding textually precedes the operation that raised: an error may occur before that binding executes. Each `recover` arm starts with the bindings available before `try` plus its own error-pattern binding. A new name is available **after the entire `try`/`recover` statement only if the normal-completion path of the `try` suite and every recovering arm bind it with compatible type and definite-initialization state**. Previously existing names follow normal ownership, assignment, and D7 join rules; a variable consumed on a path cannot be assumed initialized after the join. Unhandled raises do not form normal continuing paths.

**Resource consequence:** if `let file = FileIO.open(...)` first binds a Root-local FileIO *inside* a `try` suite, and that suite raises before normal completion, the `try` suite's scope ends **before** entering a sibling `recover` arm. The compiler must destroy/close that file at this raised scope exit, **after any dependent compiler Branches quiesce**; the sibling arm cannot reference `file` and may reopen by a separately available owned path if needed. This is ordinary lexical-scope cleanup rather than a new FileIO ownership mode. Normal successful exits still require explicit `file.close()`. A pre-existing FileIO bound *outside* `try` does not die merely because the inner suite raises; abandonment into Root `on_fail` instead follows §3's mandatory close-all boundary. Tagged-return lowering must preserve both forms of cleanup and the `FILEIO_MUST_CLOSE` normal-exit check.

#### Warning example (schematic)

```text
warning: recovery increases the handler entry lock plan
normal plan:       READ  cache.data
recovery requires: WRITE cache.data, WRITE cache.index
actual entry plan: WRITE cache.data, WRITE cache.index
The successful path may now serialize more work. Consider Root-level on_fail
for independent cleanup that should not inflate this handler's normal locks.
```

### 3. Root-only `on_fail`: handler attachment and a new locking interval

**Normative surface grammar (v4.5 adopted):** inside a domain, one handler-level *trailer group* may contain one or more contiguous `on_fail` arms at the **same indentation as `fn Handler(...)`**, immediately after that handler's body (blank lines/comments may intervene) and before the next domain member. Each arm is written `on_fail FileError.Full:`, `on_fail ParseError.Invalid:`, or, for tag-only variants with the same body, `on_fail FileError.Full, FileError.IO:`. A final `on_fail:` is a **catch-all without an error-valued binding**. An `on_fail err:` arm is shorthand for catch-all **only if all variants that can reach it belong to one enum**; then `err` has that enum type and may be passed as an ordinary owned enum value. In a multiple-error-type failure set, `on_fail err:` is rejected with guidance to use qualified variants and/or unbound `on_fail:`. Typed payload-pattern arms follow existing Moss enum pattern binding/ownership restrictions.

The group is one handler attachment, not independent `try` arms. It is evaluated by matching the original escaping error against arms in source order; duplicate or overlapping arms and any arm after a catch-all are errors. The group must be **exhaustive** for every typed variant that can escape the Root attempt; missing variants cause a totality diagnostic. A well-formed named variant that is not currently in the escaping raise set causes a **warning**, not a compilation failure; a removed/unknown enum variant remains an error, as do duplicate patterns and arms after catch-all. The failure group is not legal inside a `try`, ordinary helper, `main`, or another failure body. A handler can have such a group without a `try`. Wrong indentation, duplicate groups, orphan trailers, and an incomplete/ambiguous group are compile-time diagnostics. All arms obey D7, capability restrictions, and empty escaping-raise totality. This preserves the approved handler-trailer model while adding typed pattern dispatch across error enums, **without a general-purpose error union, runtime exception superclass, or heap allocation**.

The programmer declares the `on_fail` trailer group **on the handler**. The compiler validates all arms but includes them only when that same handler is entered as an independent Root. A nested `message` to that handler does not activate `on_fail`; unhandled typed errors propagate to the enclosing Root. The compiler must not execute a nested `on_fail` while ancestor locks survive.

An `on_fail` selects the raised error through its **typed pattern** or a catch-all. An error-valued `err` binding is available only where the failure set has a single concrete enum or scalar type; a multi-enum group uses qualified patterns to dispatch to suitably typed reporting messages, or an unbound catch-all for generic logging. The original error identity remains in the compiler/runtime diagnostic record even when no source binding is available. No cross-enum value can be passed to an ordinary domain handler as though it had a single Moss type. Each selected failure arm must have an **empty escaping raise set**; it may use nested `try`/`recover` to establish that. Panics are not caught and abort. Warn if an unbound or single-type-bound **catch-all** `on_fail` has no effect (only `pass`/no-op) while the abandoned Root attempt may already have written domain state; empty catch-all cleanup does not restore state invariants.

#### Mandatory transition sequence — implementation invariant

The wording and order below are normative:

1. Stop the failed Root attempt; propagate the selected typed error and abandon unsuccessful continuations. Do not automatically undo state/I/O.
2. **Join or safely quiesce all compiler Branches and in-flight descendants**. Do not propagate the selected terminal result or destroy its resources while an associated Branch can still use them.
3. **Close every Root-local FileIO belonging to the abandoned attempt**, including a FileIO still live in the Root handler's own frame. Normal Rust frame lifetime does **not** accomplish this on its own if the Root's frame is retained for `on_fail`. Under the §8 ABI, where each failure body is a separate callable, the body frame exits before the wrapper releases its guards, and that exit closes these files in the required order (Part III, O6). Teardown performs **no implicit sync**, ignores secondary close errors, and releases each inode claim; do not overwrite the original raised error. Borrowed views, `Range`, `RangeBatch`, and FileIO capabilities are unusable in the subsequent `on_fail`. Ordinary RAII applies to other abandoned-frame resources.
4. **Release all old Root-held Moss guards** (all remaining protected frames from the abandoned attempt); previously returned nested handlers already released their guards. No old guard or protected borrowed view may be carried into cleanup.
5. Independently infer and acquire the **fresh ranked Moss lock plan** required by `on_fail`; the plan may be larger, smaller, or different. It is planned as a separate pseudo-handler of the domain (Part III, O2), so it is never unioned into the handler's own normal/recovery entry plan and never adds an exclusive class to it. Its writes do join the domain's mutable state, so readers of the same leaves, including this handler's normal path, may need **shared** locks where they previously read lock-free, exactly as with any other writer (Part III, P10, C1).
6. Select the appropriate typed `on_fail` arm, execute programmer-authored cleanup/logging/repair/reporting, **satisfy the original handler's reply contract**, and release the new guards by normal rules. A Root calling a value-returning handler must receive a reply of the declared/inferred reply type from either the successful handler body or **every normally completing `on_fail` arm**. One-way handlers do not synthesize a reply. `on_fail` may not silently substitute a unit outcome for a required reply.

Other independent Roots may interleave between steps 4 and 5. No atomicity exists between the abandoned Root attempt and the fresh cleanup interval. A Root can retain owned, independent, definitely initialized **non-capability values** only when permitted by existing ownership/definite-initialization rules. EXPRESS-005 D7 applies to each raise-path join; a value used by `on_fail` must be definitely available on **every path that can enter it**. In addition, the explicit boundary forbids protected borrowed views and FileIO resources, even if their Rust lifetime or D7 initialization status might otherwise allow them. To access the same file during cleanup, `on_fail` must **reopen it by path**.

**No hidden ingress:** executing a handler's Root-attached `on_fail` is continuation of that already admitted Root, *not* permission to submit a new Root via `executor.invoke` from a domain/worker. All normal closed-graph route and messaging restrictions continue to apply.

#### Root specialization sketch (adopted trailer placement; Phase 21 syntax)

```moss
domain Worker:
  domainroutes(store: Store, reporter: Reporter)

  fn process(job: Job):
    try:
      message store.do_work(job)
    recover FileError.NotFound:
      message store.create_then_do_work(job)  # Establish missing data; never retry InUse.

  on_fail FileError.Full:          # typed handler trailer; Root-only
    message reporter.record_full()

  on_fail:                         # final catch-all, no mixed-enum value binding
    message reporter.record_other()
    # May reopen by path; cannot use an abandoned Root-local FileIO.
```

Here `store` and `reporter` are **domainroutes** dependencies, not ordinary domain-state fields. The two handler attachments have different scopes: `recover` belongs to its `try`, while the grouped `on_fail` arms belong to `process` itself. A caller need not construct a universal error value to report the cause. The handler could omit `try` entirely and still declare `on_fail`. When `process` is a nested handler, the Root-only trailer is inactive; it is not an unexpected new lock scope inside its parent.

### 4. Totality, `main`, and Executor reporting

- **Root totality:** for every statically admitted Root specialization, all possible raised alternatives must be handled by reachable `recover` code or its attached **exhaustive `on_fail` trailer group**. No unexpected typed raise may escape to Rust, the Executor runtime, or a host API as an untyped surprise. The Root's `on_fail` must itself have an empty escaping raise set.
- **`main` totality:** `main` is not a Root. Its escaping raise set must be empty, through ordinary lexical `recover` or a proven non-raising path. `on_fail` is not available directly on `main`.
- **Executor lifecycle:** after `Executor.start()`, every reachable **normal** exit from `main` must have joined/consumed its Executor according to the existing Phase 20 ownership rule, including normal exits through a recovery arm. A typed failure must not bypass `join()` by escaping `main`, because such escaping raises are statically rejected.
- **One-way Roots:** `executor.invoke` continues to submit a one-way Root and return no result. `join()` **drains** outstanding Roots but does **not** return an aggregate `Result`, failure queue, future, or Task. If the application needs abandoned-Root outcomes, `on_fail` synchronously messages a statically routed reporting domain; `main` reads its state after `join()`. The previous Phase 20 proposal for a result-carrying `join()` is explicitly **superseded**, not accidentally left open.
- **Reply totality (v4.5 adopted):** a handler declaring or inferring a reply type `T` must issue `reply <T-value>` on **every normally completing execution path**, including every applicable `on_fail` arm used at synchronous top-level `message` or Rust `runtime_invoke` ingress. This is the existing handler reply rule applied to the new Root-only path, not a new return type. The generated Root wrapper must not return `unit`, hang, or report success without a reply after cleanup. If there is no natural fallback value, the handler author must use a **typed ordinary outcome enum** (e.g. `FetchOutcome.Success(value: Int)` versus `FetchOutcome.Unavailable`) or reliably recover before Root completion. **Do not use an ambiguous sentinel** such as `reply -1` to represent failure. For one-way handlers invoked by `executor.invoke`, the trailer completes without returning a value. A handler entered nested follows its ordinary normal reply or raises outward; nested invocations never execute its trailer. A Root that completes successfully need not report failure.
- **No per-ingress exception:** this version deliberately **does not** allow a typed raise to escape a synchronous Root into `main`. Although an ingress-specific return channel could be designed later, it would weaken the settled uniform Root totality and require a distinct Root/application calling convention. `main` remains total; ordinary typed outcome values or Root recovery provide a consistent alternative across top-level `message`, `runtime_invoke`, and `executor.invoke`.
- Reporting is application-authored and subject to ordinary Moss effects, locking, and route legality. Merely using a reporting domain does not mean automatic supervision, retry, restart, or process recovery.

### 5. Panics, assertions, and tests

A panic denotes a programmer/invariant fault and is distinct from a typed raise. Integer divide-by-zero, invalid Range indexing, invalid resource use (unless explicitly classified as a checked OS/environmental error), and comparable faults are not caught by `recover` or `on_fail`. Existing integer arithmetic wrapping behavior remains intact. Float arithmetic is different (§13).

`assert(condition)` is the user-facing precondition/fatal assertion surface; an explicit `panic(msg)` spelling is not required for Phase 21 unless separately approved. **Context matters:** in production code, and especially inside a running domain handler, a failed assertion is a process-aborting panic. An assertion executed directly in an ordinary test body is captured by the test harness as a failure of that individual test, so `assert(false)` is valid as the failure sentinel after a call expected to raise. A process-aborting handler assertion must be tested in a subprocess. Tests for **expected** typed raises use ordinary `try`/`recover`. An **unhandled typed raise escaping a `test` block fails that test**, including the raised variant and source location in its diagnostic; it does **not** have to satisfy the production `main`/Root totality rule, does not abort the entire test process, and does not turn a failed test into a successful result. The harness catches the typed test outcome at the test boundary only; this does **not** permit typed raises to escape real Root/`main` entry points. Subsequent tests may continue. Process-aborting panic tests, especially panics inside domain handlers, must run in **subprocesses**; a harness-level Rust `catch_unwind` cannot intercept `std::process::abort()`.

Implementation must keep panic/abort effects separate from ordinary typed raise sets, expose them through the compiler effects diagnostics, and retain conservative barriers for semantics-changing optimizations.

---
### 6. FileIO error types and abnormal exits

Phase 20 has `FileIO` as both Root-local pinned resource and domain-owned field. It is a **Solo** resource and does not itself own Moss locks. Phase 21 does not change the normal rule that a Root-local file should be **explicitly closed on successful/ordinary exits** (`FILEIO_MUST_CLOSE`). Its special raised-path teardown uses the existing Rust `Drop for FileIO`, with compiler-managed lifetime transition.

#### Error taxonomy

Expose recoverable **OS/environmental failures** through one **cause-oriented**, variant-precise `FileError` enum, with the following public alternatives (independent of which operation raised them):

```moss
enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO
```

`NotFound` denotes a missing path or required filesystem entry; `PermissionDenied` an access refusal; `NotRegularFile` a path resolving to an unsupported nonregular target; `InUse` includes the same-`(device,inode)` live-claim collision; `Full` denotes storage exhaustion (`ENOSPC` and equivalent no-space failures); and `IO` is the fallback for other environmental/OS errors, including causes not covered by the named alternatives. Implementations must audit and map the existing OS failure sites to these **causes**, not expose operation-shaped variants such as `OpenFailed`, `WriteFailed`, or `SyncFailed`. The operation is already apparent from the call site. Recovery such as `recover FileError.Full` must never require inspecting a numeric `errno` inside an operation error to identify disk-full conditions. Low-level OS codes and explanatory strings belong in diagnostics/traces; they do not replace the typed cause. Any future approved error payload must obey §1's owned, bounded restriction.

Infer a distinct **exact subset of these variants for each concrete FileIO operation** (`open`, `read`, `write`, `sync`, `close`, and applicable metadata/parent-directory actions) according to its audited reachable errors. Do not assign every variant to every operation or invent additional operation-specific variants; reviewing the **mapping and completeness** of the OS-site audit remains an implementation acceptance requirement, not an unresolved choice of public enum shape.

The Phase 20 implementation currently aborts on OS failures; those sites must be **enumerated, classified, and converted**, not blanket-converted from every abort. Bad offsets, invalid modes that static checking should reject, invalid bounds, and use of invalid resources are programmer misuse and remain panic paths unless deliberately given a checked alternative. In particular, the reviewer identified dynamically closed domain-field FileIO as a policy boundary: the simplest proposed rule retains panic on invalid closed-resource use and can add `is_open()` for explicit checks. **This exact API-level disposition should be verified in the final review**, rather than silently inferred from the existing runtime.

The same `(device,inode)` already-open collision becomes an immediate recoverable `FileError.InUse`, **not a wait**: waiting could introduce a Root-on-Root forward-progress dependency. **`InUse` is not a retry signal.** In particular, repeatedly retrying `open` inside a Root can reproduce that Root-on-Root wait when the current file owner depends on locks held by the retrying Root. Roots that need to share a file must route access through a **domain-field FileIO**, rather than busy-retrying a competing open. No second live claim is installed on failure. `open` must clean up any descriptors/temporary registry state acquired before the raise.

#### Write and sync consequences

- `write` either completes the requested operation normally, or raises an error. On a raised write, **some prefix may already have been written**; the attempted range must be treated as unspecified until repaired/revalidated. No implicit rollback; independent Roots may continue, and partial application/domain state may be visible subject to ordinary synchronization.
- `sync` may fail with **`FileError.Full`** (e.g. delayed allocation, ENOSPC/EDQUOT) **or `FileError.IO`**, among the operation-specific audited causes. A single `recover FileError.Full, FileError.IO:` arm may handle both when its body is identical. `sync` failure means that the durability/writeback state of writes since the last known successful synchronization is **not established**. A subsequent successful `sync` alone must not be taken as retroactive proof that earlier failed writeback data reached storage. Do not prescribe blind retry of sync as recovery.
- **No poisoned-handle flag:** a program may obtain trusted source data, rewrite the affected content, and successfully sync on the *same* open FileIO. Reopening alone is neither a repair nor a durability proof.
- For a newly created file, retain Phase 20's directory durability obligations. Failure syncing the parent directory must be a distinct meaningful failure path even if file-content synchronization succeeded; avoid treating the two obligations as identical.
- `close` attempts closure without implicit `sync`. Failure must raise where an explicit checked close was requested, but the descriptor/inode registry claim is released after the attempt and blind close retry is not allowed. A *secondary* close failure during abandoned-path destructor cleanup must **not** mask the original raise.

#### Abnormal-exit lifetime integration

The existing `Drop for FileIO` provides descriptor close, no implicit sync, ignored destructor-close errors, and inode claim release. However, a Root handler's own frame may remain live into `on_fail`. The compiler must therefore **explicitly end/close Root-local FileIO lifetimes** before acquiring `on_fail` locks, not rely on ordinary Rust drop at a later lexical frame exit. `FILEIO_MUST_CLOSE` continues to reject missing explicit close on successful exits; it must not reject legitimate raised exits when the required teardown is generated. This includes FileIO values first bound inside a `try` suite, which are dropped on a raised exit **before** entering a sibling `recover` arm under §2's scope rule. It must reject any use of the dead handle in `on_fail`.

**Ordering:** quiesce all borrowed FileIO Branches **before** owner destruction, then close abandoned Root-local FileIO, then release all old Root guards, then acquire the new `on_fail` plan. Domain-field files have domain-defined lifetime; they are **not** automatically destroyed just because an invoking Root raised. Borrowed Range/RangeBatch views cannot survive the Root boundary.

### 7. Partial failure, nesting, and non-claims

A typed raise is neither a transaction abort nor proof of consistent domain-level application invariants. Existing nested synchronous handlers that have returned already released their guards; their mutations are not rolled back. A handler may have made changes before its own raised exit; safe Rust memory and Moss state-access synchronization still apply, but programmer-defined logical invariants are the programmer's responsibility.

Other independent Roots **continue** after a nonfatal typed raise handled by the failing Root's own `on_fail`. There is no automatic process-wide stop, cancellation, domain poisoning, or restart. A panic still uses the process-aborting fail-closed path. `on_fail` may observe state changed by another Root in the gap between releasing old guards and acquiring its fresh plan.

The existing deadlock statement (v0.1 Theorem 28.1) still holds: Part III Theorems A1 and B2 show that recovery and `on_fail` preserve it. The conflict-serialization statement (v0.1 Theorem 27.1) now covers **every completed execution, including one that exits by a typed raise** (Part III Theorem A2). That is an isolation claim only; it says nothing about whether the state a raising execution leaves is valid. For a raised/abandoned Root, **do not** claim application-level atomicity across the two intervals or that partial updates are invisible. §16 lists the proof obligations for the new error paths; Part III discharges four of them.

### 8. Compiler effects and lowering contract

Refine the existing `may_fail` information rather than invent a parallel, incompatible failure model:

```text
ObservableEffects {
  ...existing effects...
  may_panic: Bool
  raise_set: Set<ConcreteEnumVariantOrScalarType>
  ...existing divergence, I/O, message, ownership facts...
}
```

The notation is conceptual, not a new source-visible type or specific C++ data structure. The compiler infers both components transitively over concrete call specializations. For a typed recovery, subtract the variants consumed by matching the **original try**; add any newly raised variants in recovery arms that escape outer contexts. Inference must include both ordinary expression evaluation and checked runtime primitives.

**Preserve barriers where appropriate:** constructors/state initializers that prohibit failure continue to prohibit *both* `may_panic` and nonempty `raise_set`. Ordinary eager functional transformations must preserve whether and in which order callbacks execute and fail; keeping callback effects separate from raised-value commit is essential (§9). Domain/ownership/observable effects and unknown-provider handling remain conservative. Export the necessary concrete summaries in `.mossi` and include them in semantic interface identity/hash.

**Source-free provider and Root-entry ABI:** Since Phase 10.6F.1, providers export typed/materialized handler **bodies**, while the final application generates the synchronized entry wrappers using its concrete domain graph and lock classes. Preserve that division. A provider whose handler declares an `on_fail` trailer group must export each **typed failure-arm body as a separate callable**, or an equivalent single callable with a closed typed-dispatch ABI, along with pattern metadata, semantic IR, effect/raise summaries, reply contract, and the required ownership-checked captured-value contract through `.mossi` and its materialized provider artifact. The **final application**, not the provider, constructs the Root-specialized wrapper that joins/quiesces children, closes abandoned Root-local FileIO, releases old guards, acquires a fresh `on_fail` lock plan, then dispatches to the selected provider-exported typed failure body with pattern-bound values and permitted definitely initialized owned locals. The application's nested-handler wrapper must never call that failure body. This must work even when the consumer has only a source-free compiled provider. Define the concrete capture/argument and tagged-call ABI before generating wrappers or bumping the ABI version.

**Native lowering:** a fallible function is represented internally using a tagged normal-or-raised result, with explicit propagation and resource cleanup, not unwinding the host's exception mechanism. The exact Rust tagged representation, tag-only/payload error-pattern dispatch, Root reply contract, and cross-artifact calling convention must be fixed *before* bumping the native provider ABI. Because a raised function's materialized signature changes, plan a compatible version transition (from current ABI v6 to a new version such as v7), full provider rebuild, and source-free provider tests. An ABI version number does **not** replace a lowering design.

**Diagnostics:** expose variant-precise raises and possible panics separately (`moss effects`), show entry-lock inflation with provenance (`moss check`/teaching diagnostics), retain meaningful specialization/`.mossi` visibility, and maintain source-level errors when a typed raise cannot be handled at `main`/Root ingress. Parser diagnostics must also reject bare `try`, misplaced/orphan `on_fail` arms, overlapping or non-exhaustive failure-trailer patterns, catch-alls not last, invalid multi-enum `on_fail err:`/`recover err:` binding, bare `raise` outside a `recover` arm, missing reply paths in value-returning `on_fail`, overlapping grouped `recover` variants, and domain handles written as ordinary state instead of `domainroutes`. **Warnings rather than errors:** an empty/no-op catch-all swallowing errors following possible protected domain-state writes, and a well-formed but currently unreachable error-variant arm. Unknown enum variants, duplicated/overlapping arms, or non-exhaustive `on_fail` remain errors.

### 9. Ordinary fusion versus chunk speculation — separate legality predicates (V4-02)

**Do not weaken `ObservableEffects::fusion_safe()`** to permit typed raises. Ordinary map/filter fusion can interleave callbacks differently from the eager source program, changing the **first raised error**; for example an eager `map(f)` can reach `f`'s error at element 5 before `filter(g)` runs, whereas a fused loop may raise from `g` at element 2. Keep raises as conservative ordering barriers for such fusion.

**Chunk Branch speculation uses a different legality proof** because the parent commits buffered branch outcomes in exact source chunk order:

| Requirement | Ordinary map/filter fusion | FileIO chunk Branch speculation |
|---|---|---|
| Typed `raise` from callback | Preserve existing conservative failure-order barrier | Permitted as an indexed *value outcome*; commit in order |
| `may_panic` in speculative callback | Existing fusion rules remain | **Not permitted**, except when contextual/local proofs eliminate all speculative panic sites |
| Visible I/O / domain mutation / messaging in speculative map | Existing restrictions apply | Not permitted; preserve Phase 20 pure-map, no-FileIO constraint |
| Captured parent values / worker safety | Existing fusion rules | Preserve no unsafe captures, scoped read token, `Send + 'static` constraints |
| Commit sequencing | Eager/rewritten callback order matters | Indexed Branch outcomes; join window before ordered parent commit |

Implement a **dedicated chunk eligibility predicate** (illustratively `chunk_speculation_safe`) that tests purity, absence of `may_panic` **after justified proofs**, legal capture/ownership, and the existing no-FileIO-in-map restrictions. It must not use `fusion_safe()` as a substitute for those independent requirements and must not change the normal fusion contract. The parent-only left fold keeps its sequential callback/error order and may have its existing allowed domain effects; it must not be moved into Branches.

This intentionally expands today's Phase 20 eligibility: the current `fusion_safe()` use rejects index-based mappers because their general `may_fail` bit is nonempty. Under Phase 21, an index-based mapper may become eligible **at a proven-safe invocation site** without globally changing the function's error summary.

### 10. Compiler Branch errors: commit, quiescence, and panic exclusion

A Branch reading and mapping chunk `k` publishes exactly one indexed outcome:

```text
ChunkOutcome(k) = Value(mapped_value, actual_len, short_flag)
                | Raised(owned_error)
                | EndOfFile
```

If the read raises, the Branch publishes that typed error **without calling the mapper**. If it reads zero bytes, it publishes EOF **without calling the mapper**. If it reads a nonempty chunk, it may call a legally speculatable mapper; the mapper's typed raise is also an indexed result. The compiler-selected number `K` of outstanding Branches stays bounded according to Phase 20.

The parent joins/quiesces the **entire scoped window** before committing its outcomes in increasing logical index. At each in-order slot it either folds a full mapped chunk, folds a short nonempty chunk and stops, terminates at zero-length EOF, or propagates a raised error and stops. **A raise in the parent fold at index `k` wins over any raised speculative map/read at `k+1`**, because the sequential program would not reach that later callback. All outcomes after the earliest in-order terminal event are discarded without observable fold effects.

A typed raise from a discarded Branch is a *value*, and discarding that value causes no external failure. A panic/abort from a discarded Branch is **not containable**: it would terminate the process even when sequential execution would never reach that work. Therefore every speculative mapper invocation must be statically proven free of possible panics; the compiler must fall back to the sequential reference lowering when it cannot establish that. No `catch_unwind`-based speculative-panic suppression is part of Phase 21. Possible divergent work remains subject to the existing Phase 20 finite-completion/progress assumptions; do not invent a broader unconditional progress guarantee.

**Environmental precision:** physical I/O failure timing need not match across execution schedules. Native/Fast Debug can be required to agree on semantic outcomes given the *same* supplied per-read I/O results, not to induce identical real OS errors under different schedules. The theorem for successful execution remains explicitly conditional on Phase 20's stability and environmental assumptions.

**Mandatory resource invariant:** no raised outcome is propagated past the Root boundary—and no Root-local FileIO owner is dropped—while a scoped Branch retaining that FileIO's read token remains running. Existing bounded-K lowering already calls `branch_join(scope)` before iterating commit slots; Phase 21 must preserve it with tagged outcome storage and raised paths.

### 11. P21-10: accepted proof obligations and counterexamples

#### P21-10A — Nonempty FileIO chunk mapper argument (call-site fact)

**Assumptions:** (A1) Phase 20 sequential semantics call a mapper only if the returned chunk is nonempty. (A2) Both bounded-K Branch lowering and sequential fallback inspect length before mapper invocation; raised read becomes a `Raised` outcome, not a mapper call. (A3) `Range` is an immutable, length-stable view for the mapper invocation. (A4) For the indexed site being discharged, the index constant-folds to zero and the mapper parameter is still the identical argument binding (not reassigned/replaced) at the site.

**Proof:** For every mapper invocation, including an invocation whose result will later be discarded as speculation, A1–A2 give `len(p) >= 1`. By A3–A4, `p[0]` is an access into this same unchanged Range. Thus `0 <= 0 < len(p)` and the bounds check cannot panic. This proof does not require Phase 20 P6's assumption about other processes modifying the file; it depends on the **actual returned chunk's** measured length. ∎

**Compiler consequence:** re-analyze the relevant mapper invocation under a stage-local fact `len(p) >= 1` to decide that **specific chunk stage's** `may_panic` bit. Do **not** remove `may_panic` from the general helper summary exported through `.mossi`, `moss effects`, or other call sites. Optional call-context propagation into helpers is allowed only with a proven specialization; otherwise retain their general possible-panic effect. The same helper can legitimately be called elsewhere with an empty Range.

**Counterexample — `p[1]` is not safe:**

```moss
fn pair(chunk):
  if chunk[0] == 0:
    raise FormatError.Bad
  return chunk[1]
```

A 4097-byte file with chunk size 4096 yields an initial full chunk and a final **one-byte** chunk. If chunk 0's mapper raises, chunk 1's mapper may already have run speculatively; `chunk[1]` on chunk 1 aborts. The nonempty fact establishes `len >= 1` **not** `len >= 2`, so `pair` remains `may_panic` for this stage and is **not** eligible to speculate. The earlier typed raise must not be replaced by a process abort.

#### P21-10B — Half-open bounded Range indexing (local function fact)

**Assumptions:** (B1) `range(a, b)` enumerates the integers in the half-open interval, and its bounds are evaluated **once** when the loop begins; specifically `range(0, r.length())` yields `0, ..., n-1` where `n = len(r)` at entry. (B2) The induction variable is not reassigned. (B3) `r` has the **same reaching binding and value** throughout the loop (no rebinding, mutation, replacement, or alias-based invalidation). (B4) The access is exactly `r[i]`, not `r[i+1]` or another Range. (B5) a Range's length does not change for its lifetime.

**Proof:** Let `rho` be the Range referenced at header evaluation and let `n = len(rho)`. B1 yields only indices `i` satisfying `0 <= i < n`, or no iterations if `n=0`. B2 ensures the current `i` is the yielded index at access; B3–B5 ensure the access is still on `rho` with length `n`. Therefore `0 <= i < len(rho)` and that bounds access cannot panic. ∎

**Compiler consequence:** discharge this **particular index site** in the function's ordinary summary, for all callers; other potential panic sources remain. The checker must confirm stable reaching definitions, immutable Range length, exact induction-variable identity, and no invalidation. The argument generalizes to proven non-negative starts and positive literal steps **only if every yielded index stays within the half-open bound**; the native `step_by` iterator does not wrap, and future Fast Debug must match it. No unsupported induction/overflow reasoning is presumed.

**Counterexample — the Range is rebound:**

```moss
fn skip_count(r: Range) -> Int:
  s = r.slice(0, r.length())
  n = 0
  for i in range(0, s.length()):
    if s[i] == 32:
      s = s.slice(1, s.length())
      n = n + 1
  return n
```

For input `"  a"`, the header fixes length three. The first iteration can shrink `s` to length two; the final index `i=2` is then out of bounds. Assumption B3 fails. These also must **not** discharge the indexing panic: `s[i+1]`, `t[i]` with the header bounded by `s.length()`, and `range(0, s.length()+1)`.

**Separation from V4-03:** the rebinding example uses a **local** `s`, and is a valid counterexample to a static bound proof. The distinct bug where a helper assigns to a `Range` or `RangeBatch` **parameter** is addressed in §15 (V4-03) by prohibiting WRITE mode on those parameter types, not by globally declaring all Moss parameters immutable.

---
### 12. FileIO/Executor validation and native fault injection

Fast Debug currently models Executor Roots as a deterministic sequential schedule but intentionally has **no POSIX FileIO execution backend**. Phase 21 does not mandate a second implementation. For IO-specific failure coverage, provide **native fault-injection hooks** capable of causing the next eligible open/read/write/sync/close or parent-directory operation to fail at a selected site, including failures after partial write progress. Keep hook configuration test-only and avoid introducing ambient production behavior. Verify cleanup, inode release, error variants, and reporting-domain observations against these controlled native failures.

Fast Debug must implement general language-level `raise`, nested `recover`, Root-only `on_fail`, ownership/definite-initialization diagnostics, and expected-error traces for operations it supports. Do **not** claim runtime FileIO parity while its backend remains unavailable. In parallel execution tests, native and the sequential reference should agree **for fixed injected ordered I/O outcomes**, allowing genuine OS scheduling to differ naturally.

`join()` remains a drain. Inject a failure into one invoked Root, verify its reporter message, run another unaffected Root, join, and then read/report accumulated Root outcomes in `main`.

### 13. Float, numeric parsing, and conversion (P21-12)

#### 13.1 Arithmetic and predicates

`Float` uses **IEEE-754 binary64**, including signed zero, infinity, and NaN. Division by `0.0` returns the IEEE result (`1.0 / 0.0` → `inf`, `-1.0 / 0.0` → `-inf`, `0.0 / 0.0` → `NaN`) rather than panicking. This aligns with existing `sqrt(-1.0)` producing NaN and requires correcting Fast Debug's current explicit floating-point division-by-zero error. **Int** division by zero remains a panic; ordinary integer wrapping behavior is unchanged.

Provide pure non-raising Float predicates `is_nan(x)` and `is_finite(x)`. `NaN != NaN` remains true by IEEE equality rules; the predicates are the supported way to test for NaN and finite values. After concrete numeric type inference, **Float division does not contribute divide-by-zero `may_panic`**, whereas Int division retains it. Other possible faults in the expression are not dismissed by this fact.

#### 13.2 Canonical Float-to-text format — normative, Rust-independent

Moss's canonical formatter is defined by the following rules, **not** by the evolving output of Rust `Debug`/`Display`:

1. For every finite nonzero Float, print the **shortest decimal significand digits that parse back to exactly the same IEEE-754 binary64 value** under Moss's parser, with a deterministic nearest-roundtrip choice for any ties. The formatting algorithm must be independently pinned by golden tests across supported compilers/toolchains. Native may *implement* the formatter using a compatible routine but must not inherit unreviewed host-format changes.
2. If `10^-4 <= abs(x) < 10^16`, print **plain decimal notation** with **at least one fractional digit**, even for integral Float values.
3. Otherwise, print normalized **scientific notation**, one leading nonzero significand digit, optional fractional digits, and a lowercase `e` followed by the exponent in base ten; omit `+` for nonnegative exponents and superfluous exponent leading zeros. These conventions apply identically to negative finite values, with their leading minus sign.
4. Print positive zero as `0.0`, negative zero as **`-0.0`**, positive infinity as **`inf`**, negative infinity as **`-inf`**, and every NaN as exactly **`NaN`**. No guarantee that a NaN's sign/payload bits survive text conversion.

**Normative examples:**

| Input value | Moss text |
|---|---|
| `1.0` | `1.0` |
| `0.1 + 0.2` | `0.30000000000000004` |
| `1e-4` | `0.0001` |
| `1e-7` | `1e-7` |
| `1e16` | `1e16` |
| `1e300` | `1e300` |
| `-0.0` | `-0.0` |
| positive infinity | `inf` |
| negative infinity | `-inf` |
| NaN | `NaN` |

The agreed thresholds and spellings deliberately resemble Rust `f64` Debug behavior at the time of design, but **the Moss text contract is frozen independently** so a rustc upgrade cannot change a Moss program's visible output. **Both** native and Fast Debug must use it. This will change previously recorded golden output in both engines (for example `1.0` previously echoing as `1`), and the test corpus must be updated explicitly.

#### 13.3 Numeric parsing

`parse_int` and `parse_float` are **raising, not panicking**, operations. No compulsory panicking bare form is supplied.

- Lexical digits are **ASCII `0`–`9` only**. Do not silently accept Unicode decimal digits (e.g. Arabic-Indic digits).
- Leading/trailing whitespace accepts **exactly** these ASCII characters: horizontal tab U+0009 (`\t`), line feed U+000A (`\n`), vertical tab U+000B (`\v`), form feed U+000C (`\f`), carriage return U+000D (`\r`), and space U+0020. No other whitespace (including Unicode nonbreaking space) is accepted, and none is allowed inside the numeric token. A leading `+` or `-` is accepted for ordinary decimal numeric text.
- Underscores are permitted **only between ASCII digits** (including exponent digits when present); no leading/trailing, repeated, or punctuation-adjacent underscores. Examples: `1_000` accepted, `1__000` rejected.
- Decimal exponent notation is accepted for Float. For nonfinite **literal tokens**, accept **exactly** `inf`, `-inf`, and `NaN` after the permitted surrounding ASCII whitespace is removed. Reject **every other nonfinite spelling**, including `+inf`, `-nan`, `+NaN`, `nan`, `INF`, `Infinity`, and mixed-case aliases. The `+` sign allowed on ordinary decimal numbers is **not** an alias for `+inf`; syntactically valid finite-decimal input that overflows to infinity remains valid under the separate overflow rule below. Native must perform lexical validation rather than exposing the broader accepted spellings of Rust `parse::<f64>()` as Moss behavior.
- `parse_int` raises `ParseError.Invalid` for malformed text or `ParseError.Overflow` for values outside Moss Int range. It never silently defaults, wraps, or saturates malformed/out-of-range input.
- `parse_float` raises `ParseError.Invalid` for malformed lexical input. Syntactically valid decimal magnitude overflow produces IEEE infinity, and underflow follows IEEE binary64 rounding. Infinity is a Float value, not a parsing error.
- Formatting followed by parsing is **bit-exact for finite values**, including negative zero, subject to the stated IEEE representation; NaN sign/payload preservation is not required.

#### 13.4 Checked Float-to-Int conversion

Provide `to_int(x: Float)` with truncation toward zero for finite in-range values. Raise `ConversionError.NonFinite` for NaN or either infinity and `ConversionError.Overflow` when the truncated result falls outside the target Int representable range. No Rust saturating `as` cast or undefined C++ floating-to-integer cast is permitted to define Moss semantics. The conversion is a checked operation with its own inferred variant-precise raise set.

#### 13.5 Prelude API ceiling

A library operation may have **at most one panicking** form for programmer misuse and **at most one non-panicking** form, which may return normal data or raise an expected typed error. It need not have both. In particular, parsing and FileIO for untrusted/environmental input have no required panicking counterpart; existing `map[key]` and `map.get(key, default)` remain a legitimate two-form pattern.

### 14. Optional data and deliberately deferred features

The legacy lowercase `option[T]` iterator contract remains separate from user-defined matchable enums. Do **not** introduce `Option[T]` as an alias in Phase 21, broaden enum payload inference, or change Phase 15.15 D8's explicit enum payload field-type rule merely to implement error handling. Record the potential unification of option/enum generics for a later phase. This is an intentional disposition of EXPRESS-005 D9/D10 and EXPRESS-009's convergence note, **not** an omission.

Similarly, `on_fail`/reporting domains **replace** the deferral for result-bearing `join`, but do not introduce Erlang-style automatic supervision/domain restart, process restarts, automatic repair, snapshotting, compensation logs, or transactional Rollback. Phase 21 is named **“Error Handling and Recovery”** (or equivalently “Error Handling”), not “Error Propagation & Supervision” in a sense that implies automatic restarts.

### 15. Additional v4 compiler contracts outside the 18-item ledger

#### V4-03 — `Range` and `RangeBatch` WRITE parameter prohibition

Moss parameters are **not generally immutable**. Phase 10.6D deliberately supports inferred WRITE parameters that mutate the caller's binding by reference. Preserve that feature for supported writable values.

**New exception:** A formal parameter whose type is **`Range` or `RangeBatch`** must **never** infer WRITE mode. A helper attempting whole-binding reassignment/replacement of either such parameter must fail **`moss check`**, before generating Rust. Merely emitting the caller's binding as `mut` is the **wrong fix**.

**Rationale:** `Range`/`RangeBatch` are pinned or borrowed FileIO views subject to Phase 20 non-escape rules. A WRITE-through formal lets a helper **replace the caller's view** with a view created inside the helper, bypassing the restriction that such borrowed capabilities cannot be moved/returned or outlive their intended scope. `RangeBatch` demonstrates the concrete hole: the current compiler can accept a helper that assigns `b = file.read([(0,2),(2,2)])` to a `RangeBatch` parameter; the caller's batch binding then reflects the helper's newly created batch after return. Unlike the analogous `Range` example, the RangeBatch case can currently **compile and run**, making this a real semantic ownership escape, not merely an E0596 backend mismatch.

**Mandatory negative checks (illustrative source):**

```moss
# Both helper bodies must be rejected because WRITE-through a borrowed-view
# parameter is illegal. Formal type syntax is illustrative.
fn retarget_range(r: Range):
  r = r.slice(1, r.length())           # reject: Range param WRITE

fn retarget_batch(b: RangeBatch, file: FileIO):
  b = file.read([(0, 2), (2, 2)])    # reject: RangeBatch param WRITE
```

The checker should issue a precise capability/ownership diagnostic referencing both the forbidden formal type and the WRITE operation, in the spirit of `FILEIO_PINNED_OWNERSHIP`. It must **not** generally outlaw reassignment of supported scalar/aggregate WRITE parameters. Add one focused regression for each view type and a positive WRITE-parameter control test for an ordinary supported type.

The `RangeBatch` test must establish that no helper-created batch can become the caller's view through WRITE-through, even if other ownership checks correctly reject returning the same value. Both direct source checks and source-free specialization (`.mossi`) must enforce the rule. This is a separate Phase 20/21 compiler repair, not an assumption of P21-10A/B.

#### V4-04 — `for`-range bounds evaluated once in Fast Debug

Native lowering evaluates `range(start, stop[, step])` bounds **once before the first iteration**. Fast Debug currently lacks comparable `for` execution for these examples. When implemented, it must evaluate bounds once, preserve half-open iteration, step progression without wrap, and the same loop-variable/binding semantics. In particular, shrinking a Range inside a loop must **not** change the number of iterations already determined by its header; proof B must continue to reject indexing whose Range is rebound.

#### V4-01 — v0.1 fail-closed documentation migration

Revise `docs/MOSS_V0_1_LANGUAGE_DESIGN.md` in all identified places:

- **§32:** Replace the blanket claim that every unexpected handler failure aborts before exposing modified protected state. Phase 21 typed raises may unwind, release handler locks, and expose partial mutations as ordinary programmer-managed error safety. **Panics** alone retain the fail-closed process-abort model; recoverable raises carry no automatic rollback or logical state-validity guarantee. Also narrow §32's scope sentence: serializability (Theorem 27.1) covers executions that exit by a typed raise (Part III Theorem A2), while state-validity arguments stay scoped to failure-free completion.
- **§33 and Appendices A/B:** Update all echoed fail-closed claims/non-claims. Do not leave an internally contradictory “handler failure always aborts” statement next to the new `raise` semantics.
- **§42:** Restrict the *failure-specific* early-touched-lock-release barrier inherited from §32 to **possible panics**. A typed raise is not, by itself, a reason to require process-termination containment. **Do not** thereby authorize arbitrary early release: conventional 2PL (no acquisition after shrinking), whole-execution conflict ordering, barriers at observable actions, rank ordering, and protected borrowed-payload lifetimes **still apply**. The existing Phase 15.3 untouched-guard cancellation treatment is unchanged.

This is a cross-phase documentation and proof-scope correction. It does **not** claim that Phase 21 implements a new general early-lock-release optimizer.

---
### 16. Existing proof-scope and safety obligations

**Acceptance of this design is not a claim that its implementation has been formally established.** These proof obligations are required before marking Phase 21 implemented. Part III gives paper proofs for four of them. Each proof holds only if the implementation satisfies obligations O1–O9 (Part III, P1), so those obligations are mandatory acceptance gates.

| Proof obligation | Required argument / test witness | Status |
|---|---|---|
| Handler recovery locks | Variant-precise recovery effects and nested calls are contained in the affected handler's complete **ranked entry plan**. No dynamic upgrade or post-observable late acquisition. Warning comparison uses actual baseline, not a naive syntax union. | Paper proof: Part III Lemma 2 and Theorem A1 (gates O1, O3, O4) |
| Open nesting | Descendant handlers that have returned stay returned; no retroactive retention of descendant locks and no new Root-wide closed transaction. | Paper proof: Part III Lemma 1 (gate O5) |
| Fresh Root phase | No original Root guard or protected borrowed view is live when `on_fail` begins its independent ranked acquisition. Handler-attached clause is unreachable in nested role. | Paper proof: Part III Theorems B1 and B2 (gates O2, O6–O8) |
| Resource/lifetime boundary | All Branches/descendants are quiesced; **every abandoned Root-local FileIO is closed** before fresh cleanup; its inode claim is released exactly once; no FileIO or Range/RangeBatch survives into `on_fail`. | Quiescence and closure before `on_fail`: Part III Lemma Q and Theorem B1 (gates O6, O9). Exactly-once inode release: implementation test |
| Ownership/caller bindings | No `Range` or `RangeBatch` WRITE parameter is accepted, including source-free specializations; supported ordinary WRITE parameters continue to work. | Implementation and tests (V4-03) |
| Effect soundness | Compiler never silently drops a possible `may_panic` or typed raised variant from exported/concrete effects. Stage-specific nonempty-chunk facts do not contaminate generic helper summaries. | Implementation and tests |
| Parallel exception ordering | For fixed outcomes, indexed successful/raised/EOF Branch results and parent fold produce the same first **in-order terminal event** as sequential reference, with no fold effects afterward. | Argued in §10; extending Phase 20 Theorem 3 formally to raised outcomes is still open |
| Speculative panic safety | Every mapper invocation that actually runs speculatively is statically known free of panic; index bounds facts are validated in their correct call-site/local scope. | Bounds facts proved in §11 (P21-10A/B); implementation and tests |
| 2PL/early-release semantics | Changing §42's failure-specific block from general failure to **panic** does not weaken remaining 2PL, ordering, rank, or protected-borrow rules. | Open. Theorem A2 covers release at a raise exit, not early release mid-execution |
| Root/Main totality | `main` and every statically admitted Root specialization account for every escaping raised variant; `on_fail` itself has no escaping variant. | Implementation and tests |
| One-way Executor semantics | `join()` remains drain-only; reporting domain outcomes are observed only through ordinary Moss messages under existing route rules. | Implementation and tests |
| Float/numeric parity | Arithmetic, text formatting, parsing, conversion, and error effects are consistent in native and supported Fast Debug operations regardless of host rustc Debug changes. | Implementation and tests |

### 17. Original Phase 21 backlog: final 18-item acceptance matrix

**Terminology:** “Design closed” means the previously disputed language/runtime **direction is adopted**; it does **not** mean the code or tests exist. “Deferred” means intentionally out of Phase 21; the disposition itself must be recorded in the spec and issue ledger. All active items remain implementation work until validated.

| ID | Original obligation | v4 disposition | Primary acceptance requirement |
|---|---|---|---|
| **P21-01** | Typed error propagation / EXPRESS-007 | **Design closed** (§1, §8) | `raise`, inferred variant sets, checked runtime raises, transitive propagation, `.mossi`/native parity. |
| **P21-02** | `try`/`recover` locking | **Design closed; paper proof in Part III** (§2, §16) | Inflate handler entry plan; mandatory provenance warning; no late upgrades; preserve nested routing/rank. |
| **P21-03** | Root-only `on_fail` | **Design closed; v4.5 grammar adopted; paper proof in Part III** (§3) | Handler trailer with typed pattern arms and final catch-all; Root-only lowering, reply totality, D7 locals, explicit closed FileIO/borrowed-view boundary, fresh guards. |
| **P21-04** | Failure during `on_fail` | **Design closed** (§3) | Empty escaping raise set; nested recovery permitted; unhandled cleanup raise is compile error. |
| **P21-05** | SWARM-040 preconditions/failure tests | **Design closed** (§5) | `assert` fail-closed; an escaping typed raise fails its `test` block with diagnostics; expected-raise tests and subprocess abort tests; teaching diagnostics. |
| **P21-06** | Typed recoverable FileIO error set | **Cause-oriented public API closed; site mapping audit needed** (§6) | Public `NotFound`/`PermissionDenied`/`NotRegularFile`/`InUse`/`Full`/`IO`; audit existing OS abort sites and map exact per-operation cause sets; reject misuse; same-inode raises `InUse` immediately. |
| **P21-07** | Abnormal Root-local FileIO cleanup | **Runtime mechanism exists; integration required** (§3, §6) | Compiler closes on raised exits and pre-`on_fail`, suppresses secondary close errors, preserves normal explicit-close rule, joins Branches first. |
| **P21-08** | `sync()` failure and durability | **Design closed** (§6) | Uncertain-writeback contract; no blind retry, no poisoned-handle flag; permitted rewrite-and-sync recovery. |
| **P21-09** | Partial writes and other Roots | **Design closed** (§6–7) | Failed range unspecified; completed side effects persist; independent Roots continue after typed failure. |
| **P21-10** | Ordered Branch failures and bounds proofs | **Design closed; implement proofs** (§9–11) | Separate eligibility predicate, indexed raise/EOF outcomes, quiescence, A/B bounds proofs, no speculative panic. |
| **P21-11** | Deferred result-carrying Executor `join()` | **Superseded explicitly** (§4) | Preserve one-way invoke/drain-only join; Root `on_fail` messages statically routed reporting domain; verify with multiple Roots. |
| **P21-12** | EXPRESS-009 numeric parsing / Float consistency | **Design closed** (§13) | IEEE Float, frozen text format, ASCII parsing, nonfinite predicates, `to_int`, typed invalid/overflow failures. |
| **P21-13** | `option[T]` convergence / EXPRESS-005 D9 | **Deliberately deferred** (§14) | Document unchanged lowercase option and separate future generic-enum work. |
| **P21-14** | Checked and bare API convention | **Design closed** (§13.5) | At most one panicking + one non-panicking form when applicable; no mandatory panicking parser/FileIO variant. |
| **P21-15** | Fallible `main` + Executor lifecycle | **Design closed** (§4) | `main` raise set empty; normal exit through any `recover` reaches required `join`; handle resources correctly. |
| **P21-16** | Supervision/restart wording | **Design closed** (§14) | Rename/descope automatic supervision; explicitly decline automatic restart, rollback, transactional repair. |
| **P21-17** | `may_fail`/effects/ABI compatibility | **Design closed; engineering pending** (§8–9) | Split `may_panic`/raise sets; preserve barriers; export separate provider `on_fail` callable and semantic IR; application-generated Root/nested wrappers; update `.mossi`, tagged lowering ABI and diagnostics. |
| **P21-18** | Native vs Fast Debug FileIO testing | **Design closed** (§12) | Native deterministic failure injection; Fast Debug language parity; no misleading claim to Fast Debug FileIO runtime parity. |

#### Four cross-cutting v4 obligations (not duplicates of the ledger)

| ID | Scope | Acceptance condition |
|---|---|---|
| **V4-01** | v0.1 language design and proof-scope update | Rewrite §32, §33, Appendices A/B, §42 and all relevant echoes; only possible panic adds the old fail-closed early-release barrier, while all other 2PL/observable/borrow constraints survive. |
| **V4-02** | Distinct optimizer legality | `fusion_safe()` still forbids raise-order-changing fusion; new chunk predicate allows typed raises under ordered commit but no `may_panic`. |
| **V4-03** | Borrowed-view escape closure | Both `Range` and `RangeBatch` formals reject inferred WRITE, with separate negative regressions and an ordinary WRITE-positive control. |
| **V4-04** | Fast Debug `for` parity | Once supported, half-open range bounds are evaluated once, positive step never wraps, and reassignment in body does not alter the precomputed iteration count. |

### 18. Minimum conformance and regression program

This is a **planned test inventory**; no test success is claimed by this design document.

| Suite | Minimum coverage |
|---|---|
| **Core raise channel and syntax** | Variant-sensitive inference; `raise` from leaves and runtime; `raise` without expression inside `recover` re-emits original tagged variant; named and unbound catch-all recovery (multi-enum uses unbound `recover:`); unhandled Root diagnostic; `main` totality; normal returned `Outcome` does not propagate; helper specialization and `.mossi`; try-local scoping and all-arm definite binding at join; reject `try` without `recover`, misplaced/orphan `on_fail`, and domain handles in ordinary fields. |
| **`try` scope and FileIO** | A name first bound in `try` is inaccessible in sibling `recover`; a new name is available after the statement only when all normal completing alternatives establish it; an owned FileIO opened inside `try` is automatically dropped on a raised scope exit before `recover` (after quiescence); normal completion still requires explicit close; an outer FileIO remains live through local recover. |
| **Recovery locking** | READ→WRITE inflation, including previously lock-free READ-only one-handler field becoming exclusive WRITE; added domain class; multiple nested `recover`; sibling arm does not recatch own raised errors; non-inflating recover; warning fidelity to Phase 15.3 normal-plan baseline; concurrency/deadlock proof probes. |
| **Proof gates (Part III O1–O9)** | Recover-arm classes appear in the handler's entry plan in sufficient mode (O1, O3); a trailer write never adds an exclusive class to its own handler (O2); guard-cancellation and D7 analyses include exceptional edges (O4); raise exits release through the ordinary wrapper path (O5); the Root wrapper order is body exit, guard drop, fresh acquisition (O6); nested invocation never runs the trailer (O7); trailer captures contain no views, guards or capabilities (O8); the window join precedes every slot read (O9). |
| **Root role/cleanup** | Same handler as Root and nested; nested execution must never execute `on_fail`; exhaustive multi-arm typed trailer with optional catch-all; handler returning value requires `reply` in all normal-completion arms; abandoned Root permits fresh, unrelated lock plan; D7 definite-initialization positives/negatives; borrowed views rejected; FileIO closed before `on_fail`, reopened only via path; source-free provider exports typed failure bodies and application builds Root wrapper. |
| **Provider `on_fail` ABI** | Build a source-free provider with typed `on_fail` arms matching alternatives from multiple error enums; export separately callable bodies, pattern signatures, owned captures, and reply contracts. As Root the application wrapper quiesces/closes/unlocks/relocks and dispatches; as nested message it does not. |
| **`on_fail` totality** | Handler-level pattern trailer at `fn` indentation, with or without local `try`; payload and tag-only pattern arms; optional final catch-all; multi-enum `on_fail err:` rejected; **missing coverage** and missing `reply` diagnosed; orphan/misindented trailer rejected; cleanup with a newly raised error rejected; nested handled raise accepted; original error identity preserved in diagnostics; panic terminates process; ordinary typed-outcome replies used rather than magic numeric sentinels; separately test that local `recover` converts the same error into the same typed outcome for both Root and nested invocation (without using `on_fail`). |
| **FileIO conversion** | Inject open/read/write/sync/close/parent-directory errors; NotFound fallback opens a genuinely distinct default file path, not `rw` then `create` on the same path; same-inode `InUse` rejection (no retry-on-collision loop); `Full` recoverable without errno inspection; cause-oriented enum and per-operation raise subsets; create/open partial-cleanup; programmer misuse stays panic; no leaked inode claim. |
| **Durability/partial write** | Inject write failure after prefix; sync `FileError.Full` and `FileError.IO` both exercised with a grouped `recover` arm; initial write occurs before first sync; ensure no rollback promise; failed sync then rewrite and successful sync on same handle; separate directory-sync failure; other Roots continue. |
| **Branch ordering** | K-window typed read raises, mapped raises, EOF, short chunk, fold raises; earliest in-order event chosen; later speculative outcomes discarded; Branches joined before FileIO teardown; staged IO outcome fixtures. |
| **Proof A** | `chunk[0]` enables stage-specific Branch lowering; `chunk[1]` retains possible panic on one-byte short chunk; same generic helper called with empty Range elsewhere remains potentially panicking. |
| **Proof B** | Stable `for i in range(0, r.length()): r[i]` discharges index panic; rebinding `s`, using `s[i+1]`, `t[i]`, or `len+1` must not; valid positive-step forms yield only in-bounds indices. |
| **Fusion separation** | Eager `map(f)` then `filter(g)` with two differently positioned potential raises must not be fused so as to change first error. A raising non-panicking chunk mapper can still be speculated with ordered result commit. |
| **Range/RangeBatch WRITE** | Reject WRITE reassignment on a `Range` formal and separately on a `RangeBatch` formal; reproducer for `b = file.read([(0,2),(2,2)])` must fail check instead of leaking helper-created view; ordinary `Int` WRITE formal still allowed; source-free providers cannot bypass rule. |
| **Float parity** | `1.0`, `1e-4`, `1e-7`, `1e16`, `1e300`, `0.1+0.2`, ±0, ±inf, NaN, sqrt negative, ±Float division by zero; `is_nan`, `is_finite`; exact canonical formatting in native/Fast Debug and across compiler versions. |
| **Numeric parsing and cast** | ASCII-only digits; exactly ASCII HT/LF/VT/FF/CR/SP trim; optional numeric `+`; in-between-digit underscores; malformed underscore syntax; accept only exact `inf`/`-inf`/`NaN` nonfinite tokens, reject `+inf`/`nan`/`INF`/`Infinity`; Int overflow; Float overflow/underflow; `to_int` truncation and nonfinite/out-of-range variants. |
| **Test harness** | Direct-body `assert(false)` fails only that test while handler assertion aborts and needs subprocess; unhandled typed raise fails its `test` block with variant/source diagnostic and does not prevent subsequent tests; expected raises use explicit or catch-all `recover:`; tests for bare `raise` preserving original error identity; fatal handler panic tested in a subprocess; do not treat Rust `catch_unwind` as protection from process abort. |
| **Executor lifecycle** | `main` recovery arms reach `join` after start; invoked Root reports abandonment to reporting domain; join returns no error aggregate; other invoked Roots complete normally; top-level synchronous `message` does **not** leak an unhandled Root raise into `main` under this version. |
| **Recovery diagnostics** | Warn (not error) for a resolvable typed variant named by a `recover` or `on_fail` arm but absent from the corresponding inferred raise set; error for unknown/removed variant, duplicate/overlapping pattern, or nonexhaustive required coverage. Warn for no-op `recover:` / single-enum `recover err:` when the guarded attempt could have written domain state before its raise; likewise no-op catch-all `on_fail:` / eligible `on_fail err:` after an abandoned state-modifying Root. No warning for a nonempty recovery action, explicit re-raise, or no-write attempt. |
| **Fast Debug future `for`** | When `for` is implemented, evaluate bounds once and match native half-open and step semantics; until then do not claim this parity test is runnable in Fast Debug. |
| **Source examples/formatting** | All domain dependencies use `domainroutes(...)`, not ordinary state; example chunk pipelines appear on one logical Moss source line; `for` output expectations use separate lines for `0`, `1`, and `2`; use the supported `not` negation spelling, not `!` (independent checker defect tracked in issue #2). |

**Completion gates:** `moss check` rejects all prescribed invalid examples; native rustc compilation succeeds for all positive tests; existing project `make check` and `make examples` suites pass; source-free module/provider and runtime fault-injection tests pass; teaching/effects diagnostics agree with actual lowering; external reviewer signs off on the updated proof scope and closeout ledger. Compiler/runtime conformance is established by evidence, not this draft's status labels.

### 19. Implementation DAG and review handoff

Four agents/worktrees can implement independent areas *after* the shared type/effect/ABI contract is fixed. Suggested dependency structure:

```text
              Final v4 peer review
                     |
             P21-01 + P21-17
           (raise-set ABI/effects)
                     |
        +------------+------------+
        |            |            |
      21.A         21.B         21.D
  try/recover,    FileIO     Float, parse,
  Root on_fail,  errors,     SDK, tests,
  proof/typing   RAII, FI    view-WRITE
        |            |            |
        +------------+------------+
                     |
                   21.C
             Branch outcomes,
             eligibility, join
                     |
          Integrated verification
          docs, review, closeout
```

This is a dependency sketch, not a mandate that all workstream C work wait for completion of B/D. P21-10's chunk eligibility analysis can be built in parallel once `may_panic` and the raised-outcome representation are stable. **Quiesce-before-FileIO-close** is a hard integration ordering prerequisite for B's raised-exit path.

#### Information the independent reviewer should verify

Also verify the adopted v4.5 multi-arm handler-level `on_fail` trailer grammar, **typed outcome instead of sentinel replies**, value-returning trailer reply obligations, the prohibition on bare `try`, grouped tag-only `recover` matching, unbound catch-all recovery, bare re-raise, and both warning diagnostics against implementation and examples.

1. **Current Phase 20 compatibility:** Does any protected view or FileIO runtime capability remain live across the actual generated `on_fail` boundary? Is every abandoned local closed once, *after* borrowed Branches quiesce?
2. **FileError audit:** Are recoverable OS sites mapped by **cause** to the adopted `NotFound`, `PermissionDenied`, `NotRegularFile`, `InUse`, `Full`, `IO` variants, with exact per-operation subsets (especially `Full` without errno inspection)? Which abort sites are programmer panics? Verify the dynamically closed domain-field operation policy; no speculative guessed site mapping should be treated as implementation truth.
3. **P21-10A/B codegen:** Does the *exact specialized callable* receive the stage-local `len>=1` fact, and is global helper `.mossi` still conservative? Are loop ranges and values checked for stable definitions?
4. **Optimizer separation:** Does no change to `fusion_safe()` accidentally enable eager/fused callback error reordering? Are chunk-specific checks actually separate?
5. **Ownership escape:** Can a generic/source-free helper still retarget a `RangeBatch` parameter or otherwise leak a borrowed FileIO view by WRITE? Test both direct and `.mossi` paths.
6. **Numeric contract:** Are canonical Float text and parsing specified independently of host Rust Debug, with exactly the accepted nonfinite tokens, enumerated ASCII whitespace, ASCII digits, and bit-exact finite round trip? Are Float divide-by-zero effects classified correctly?
7. **Proof/documentation reconciliation:** Are historical fail-closed claims changed only for **typed raises**, while panics and all non-failure 2PL/observable/borrow proof obligations survive? Verify scope-join and scope-exit RAII behavior at `try`/`recover` and source-free provider `on_fail` lowering.

### 20. Project sources and historical traceability

The design consolidates earlier decisions and deferred backlogs. Primary repository files for review:

- [`docs/MOSS_V0_1_LANGUAGE_DESIGN.md`](https://github.com/kuttyb/moss/blob/main/docs/MOSS_V0_1_LANGUAGE_DESIGN.md) — §§27–28, 32–33, 42, and appendices; synchronization/failure claims requiring revision.
- [`docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md`](https://github.com/kuttyb/moss/blob/main/docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md) — FileIO ownership/lifecycle, Executor semantics, §20 ordered Branch lowering, §29 Phase 21 deferrals.
- [`docs/FUNCTIONAL_DATAFLOW.md`](https://github.com/kuttyb/moss/blob/main/docs/FUNCTIONAL_DATAFLOW.md) — eager functional fusion and failure-order barriers.
- [`docs/PHASE_15_15_EXPRESS_005_REVIEW.md`](https://github.com/kuttyb/moss/blob/main/docs/PHASE_15_15_EXPRESS_005_REVIEW.md) — D7 definitely initialized consumed locals, D8 enum payload types, D9/D10 `option[T]` disposition.
- [`examples/swarm/ISSUES.jsonl`](https://github.com/kuttyb/moss/blob/main/examples/swarm/ISSUES.jsonl) and [`examples/swarm/FINDINGS.md`](https://github.com/kuttyb/moss/blob/main/examples/swarm/FINDINGS.md) — SWARM-040, EXPRESS-007, EXPRESS-009.
- [`src/fileio_runtime.hpp`](https://github.com/kuttyb/moss/blob/main/src/fileio_runtime.hpp) — `Drop for FileIO`, close/read/write/sync error paths, Range and RangeBatch.
- [`src/fileio_semantics.inc`](https://github.com/kuttyb/moss/blob/main/src/fileio_semantics.inc) — pinned capability checker and normal-path `FILEIO_MUST_CLOSE`.
- [`src/file_chunk_lowering.inc`](https://github.com/kuttyb/moss/blob/main/src/file_chunk_lowering.inc) — current `fusion_safe()`-based chunk eligibility; requires independent predicate.
- [`src/file_chunk_codegen.inc`](https://github.com/kuttyb/moss/blob/main/src/file_chunk_codegen.inc) — the actual zero-length-before-map and `branch_join`-before-commit lowering.
- [`src/interpreter.hpp`](https://github.com/kuttyb/moss/blob/main/src/interpreter.hpp) and [`src/interpreter_phase20.inc`](https://github.com/kuttyb/moss/blob/main/src/interpreter_phase20.inc) — current Float division mismatch and Fast Debug/Executor boundary.
- [`docs/MODULE_ABI.md`](https://github.com/kuttyb/moss/blob/main/docs/MODULE_ABI.md) — existing exported effect and provider ABI versioning contract.

**Repository checkpoint used in the discussion:** main at `395b4c0` (October 8, 2026), with additional compiler/runtime reproducibility evidence supplied by peer review. Recheck current `main` when implementing or formally accepting this document. Those peer-reported reproducers are proposed regression requirements here; the act of drafting v4 does **not** independently certify running each reproduction or all repository tests.

---

### 21. Adopted v4.5 addendum: four corner cases and conformance gates

These four language decisions are **approved** and form part of the v4.5 implementation baseline. The two warning diagnostics and ordinary-outcome example below are also approved refinements.

| Gap | Normative adopted resolution | Minimal required test |
|---|---|---|
| Value-returning `on_fail` | Every normally completing selected trailer arm issues a `reply` of the handler's original reply type; one-way Roots have none. A typed ordinary outcome represents failure without sentinel values | Top-level `message` and Rust `runtime_invoke` both receive a typed outcome on failure; omitted reply or wrong reply type rejected |
| More than one error enum | Use **qualified, typed `on_fail` pattern arms** plus optional final unbound `on_fail:`. `on_fail err:` binds an ordinary enum/scalar value only when its reachable failure set has one concrete error type. No general cross-enum anonymous sum | Multi-enum Root reports by cause through distinct reporter messages; ambiguous binding rejected; source-free provider validates arms |
| Re-raise | Bare `raise` inside `recover` re-emits the **exact selected** original variant and payload; does not run a sibling arm | Nested handler logs locally and re-raises; enclosing handler receives same variant/payload; outside `recover` rejected |
| Catch-all | Final `recover:` matches all remaining variants with no value binding, including new variants introduced by libraries; `recover err:` requires one concrete enum/scalar type | `main` remains total with multiple error enums; failing test catches all; `recover:` + `raise` preserves the variant |
| Warnings | Empty/no-op catch-all following possible state writes warns; well-formed error pattern absent from inferred raise set warns. No catch-all effect safety/rollback guarantee is implied | Both `recover` and `on_fail` warning cases; unknown variant, overlap, and missing Root coverage still error |

**Preferred typed-outcome conversion: use local `recover`, not Root-only `on_fail`:**

```moss
enum StoreError:
  Down

enum FetchOutcome:
  Success(value: Int)
  Unavailable

domain Store:
  fn Load(available: Bool) -> Int:
    if not available:
      raise StoreError.Down
    reply 42

domain Worker:
  domainroutes(store: Store)

  fn Get(available: Bool) -> FetchOutcome:
    try:
      v = message store.Load(available)
      reply FetchOutcome.Success(value: v)
    recover StoreError.Down:
      reply FetchOutcome.Unavailable

fn main():
  store = Store()
  worker = Worker(store: store)
  result = message worker.Get(false)
  match result:
    case Success(value):
      echo value
    case Unavailable:
      echo "service unavailable"
```

`FetchOutcome.Unavailable` is ordinary, discriminated return data, never a numeric sentinel. Both independent Root invocation and nested `message` invocation of `Worker.Get` produce the **same `FetchOutcome` contract**: the local `recover` executes in either role, so `StoreError.Down` does not leak to nested callers and no Root-abandonment transition is needed. The example intentionally uses `not`, Moss's supported boolean-negation spelling, rather than unsupported `!`. Use `on_fail` for genuine Root abandonment and cleanup requiring fresh locks; its rule that a value-returning handler must reply on every normally completing failure arm remains a **backstop**, not the default pattern for converting an ordinary error into data. Under v4.5, synchronous Roots invoked by `main` retain the same Root-totality contract as other ingress modes; there is no special `main`-only raised-result channel.

**Cross-enum trailer example (without a synthetic union type):**

```moss
domain Worker:
  domainroutes(reporter: Reporter)

  fn Run():
    parse_int("bad")               # can raise ParseError.Invalid
    file = FileIO.open("out", rw)  # can raise FileError variants
    file.close()

  on_fail ParseError.Invalid:
    message reporter.BadInput()

  on_fail FileError.Full, FileError.IO:
    message reporter.StorageFailure()

  on_fail:                          # remaining variants, including future additions
    message reporter.OtherFailure()
```

Each `on_fail` arm shares the **same Root-abandonment transition** defined in §3. Its fresh-lock plan is conservatively the union of all reachable trailer arms, acquired after dropping all old guards. A later optimizer may specialize the plan per selected variant only if it proves the same ranking, conflict, and observable-action constraints. The arms are application-dispatched only when the handler invocation is a Root. Independent review must test the application-generated cross-module wrapper and failure-arm ABI.

---

#### Separate compiler bug discovered in tutorial review (not a Phase 21 semantic change)

The documented boolean negation operator is `not`. Today `moss check` accepts unsupported `if !available:`, native lowering forwards `!` to Rust, and Fast Debug rejects it. A checker-level syntax regression must **reject `!`** with a clear diagnostic while accepting `if not available:` in checker, native, and Fast Debug. Track this separately as [kuttyb/moss issue #2](https://github.com/kuttyb/moss/issues/2), independent of the 18-item Phase 21 error-handling backlog. The §21 example and Part I Example 9 use `not`.

### Final disposition

The previously approved Phase 21 architecture remains closed. **The four v4.5 language additions and the two new warning diagnostics are approved; v4.5 supersedes v4.4 as the implementation baseline.** P21-10A and P21-10B are accepted as conditional proofs with defined counterexamples. The **18-item ledger** plus **V4-01–V4-04** is the complete acceptance checklist assembled from this discussion. Explicitly deferred optional-data convergence and automatic restart are documented as dispositions, not forgotten requirements. Part III gives paper proofs for four §16 obligations, conditional on O1–O9. **Implementation, the remaining and machine-checked proofs, the exact FileIO OS-site-to-cause mapping audit, provider ABI work, conformance testing, and repository documentation updates remain.**

---

## Part III — Proofs for §16: recovery locking and the fresh Root phase

*Paper proofs, not mechanized.*

These proofs discharge four rows of §16 by reducing them to results Moss already has:

- v0.1 Theorem 27.1: whole-execution conflict serializability;
- v0.1 Theorem 28.1: deadlock freedom for Moss-managed locks;
- Phase 20 Theorems 1a and 1b: no wait-for cycle, and progress, with Roots, Branches, Solo FileIO and admission.

They hold only if the implementation meets obligations O1–O9 (P1). P9 shows what breaks when it doesn't.

Peer review on October 9 led to three precision edits, included here:
- O3 and Lemma 2 are scoped to the handler's own domain. A nested `message` issued from a `recover` arm still acquires its target's locks.
- Lemma Q is stated for raises that a Branch produces while its siblings are still running.
- Progress is separated from Theorem B2 into Corollary B3.

### P0. Results

| Result | Statement | Discharges §16 row |
|---|---|---|
| Lemma 1 | A handler that exits by raise releases exactly its own guards, like a normal exit. Nothing is retained from callees that returned or raised. | Open nesting |
| Lemma 2 | Every class of the handler's own domain that a `recover` arm may touch is held, in sufficient mode, before its `try` begins. | Handler recovery locks (coverage, no upgrade, no late acquisition) |
| Theorem A1 | `raise`, `try`/`recover` and bare `raise` preserve the rank invariant, so Theorem 28.1 and Phase 20 Theorem 1a still hold. | Handler recovery locks (deadlock freedom) |
| Theorem A2 | Theorem 27.1 extends from failure-free executions to executions that exit by raise. | Supports §7 (see C2) |
| Lemma Q | Wherever a Root commits a raised outcome from a Branch, raises in its own code, or propagates a raise out of a pipeline, no Branch of that Root and no Branch-held FileIO read token is live. | Resource/lifetime boundary (quiescence) |
| Theorem B1 | When the Root wrapper starts acquiring the `on_fail` plan, the thread holds no Moss lock. | Fresh Root phase |
| Theorem B2 | Programs with `on_fail` have no wait-for cycle. | Fresh Root phase; Resource/lifetime boundary |
| Corollary B3 | Under Phase 20's assumptions A0–A7, with A1 applied to trailer code, every started Root completes, trailer included. | Progress (conditional) |

Two corrections to the specification follow from the proofs (P10). Both are applied in Part II:
- in §3 step 5 and §2, `on_fail` and recovery writes can add *shared* locks to readers elsewhere;
- in §7 and §15, serializability now covers executions that exit by raise.

### P1. Model and obligations

Notation follows v0.1 §§27–28.

- `rank(L) = (domain_rank(D), class_rank_D(C))`, compared lexicographically. This is a strict total order on Moss-managed locks.
- `H(t)` is the set of Moss-managed locks thread `t` holds at a given moment. Nested messages are direct synchronous calls on the same thread, so `H(t)` includes the guards of every active frame on the call path.
- **Rank invariant (RI):** whenever `t` starts acquiring a lock `L`, every `L'` in `H(t)` has `rank(L') < rank(L)`. In particular `L` is not already in `H(t)`, so RI rules out upgrades and re-acquisition.

Theorem 28.1's proof is exactly the statement that if RI holds for every acquisition, there is no cycle among Moss-lock waits: a cycle would need `L1 < L2 < … < L1`. Phase 20 Theorem 1a adds the other wait kinds (Solo kernel operations, runtime leaf locks, Branch joins, admission) and shows that none of them closes a cycle. So deadlock freedom follows from two facts:
- RI holds for every acquisition;
- Phase 21 adds no wait outside Phase 20's kinds (assumptions A3 and A7).

An **execution** is one handler invocation's lock interval on its domain, from its first acquisition to its exit. A nested `message` starts a separate execution of the callee, on the callee's domain. When a Root's `on_fail` trailer fires, the Root has two executions on its domain: the abandoned attempt **E1** and the trailer interval **E2**.

The proofs assume the Phase 21 implementation satisfies:

| # | Obligation | Source |
|---|---|---|
| O1 | The effect summary of handler `h` covers all statically reachable code in `h`'s body, including every `recover` arm, nested arm, and helper they call. | §2, §8 |
| O2 | The `on_fail` trailer group of `h` is planned as a separate **pseudo-handler `h_fail`** of the same domain. It has its own effect summary, its own column in the class signatures (v0.1 §24), and its own ClassSet. Its accesses never enter ClassSet(h). | §3 step 5, made precise here |
| O3 | Within an execution of `h`, every class of ClassSet(h) (classes of `h`'s own domain) is acquired in increasing class rank, either at handler entry or at a Phase 15.3 site after an eligible leading condition. Either way, the acquisition precedes the first statement of any `try` whose arms may touch the class, and precedes the handler's first observable action. Inside a `recover` arm or after a raise, `h` acquires and upgrades none of its own domain's classes. A nested `message` issued from an arm starts a separate execution in a higher-ranked target domain, which acquires that domain's locks under the ordinary rules. | §2 |
| O4 | Every analysis that reasons about remaining paths, such as guard cancellation and D7 definite initialization, includes **exceptional edges**: from each possible raise site to the arms that may catch it, and to the handler's raise exit. | Implied by §2; made explicit here |
| O5 | A raised outcome is a tagged return. Every handler wrapper's raise exit releases exactly the guards that wrapper acquired, after the body's last protected access and after the raised payload has been materialized as an owned value. This is the same release path as a normal exit. | §8 |
| O6 | On a raised outcome, the application-generated Root wrapper of a handler with a trailer group does the following, in order: ends every Root-local FileIO of the attempt (under the §8 ABI, the body frame's exit does this); drops every guard it holds; acquires ClassSet(h_fail) in increasing class rank; runs the selected arm; materializes any reply; releases. | §3, §8 |
| O7 | Nested wrappers never run the trailer; a raised outcome propagates as a tagged return. `recover` arms and trailer arms message only declared routes. The trailer cannot admit Roots. | §3; Phase 20 E12 |
| O8 | Values passed from the body to the trailer are owned, and contain no protected borrowed view, guard, FileIO, Range or RangeBatch. | §3 |
| O9 | A construct that spawns Branches reads no Branch outcome slot until it has joined that window's Branches, runs no parent computation that can raise while Branches of the window are live, and publishes the next window only after the commit loop ends without a terminal event. | Phase 20 §20.2; §10 |

### P2. Lemma 1: raise exits release like normal exits

**Lemma 1.** When a nested `message` returns to its caller, by reply or by raise, `H(t)` is exactly what it was immediately before the message began.

*Proof.* The callee's wrapper acquires a set `G` after the call begins. By O5 it releases exactly `G` at exit, on either path. The caller's frame acquires nothing while the synchronous call runs. The callee's own nested messages satisfy the lemma by induction on call depth, which is finite because the routing graph is acyclic and calls are synchronous. ∎

So once its caller resumes, a callee that raised holds nothing. Its writes are visible, because it committed by exiting, and none of its locks is retained afterward. This is §16's open-nesting row.

### P3. Lemma 2: recover arms are covered before their `try` begins

**Lemma 2.** Let `T` be a `try` statement in handler `h`. At `T`'s first statement, `H(t)` contains every class of `h`'s own domain that any arm of `T` may access, whether directly, through nested arms, or through helpers. Each is held in at least the mode the access needs, and none is cancelled or released before the arm's last possible access.

*Proof.* By O1, every such access is in `h`'s effect summary. So its class is in ClassSet(h), and `h`'s mode for it is exclusive if any of `h`'s accesses to it writes (v0.1 §24); that is at least the mode the arm needs. By O3 the class is acquired before `T` begins. By O4, the arm is a remaining path from every point in `T`'s suite where a raise can occur, so the class is not cancelled while the arm can still run. Touched guards are released only at exit (strict 2PL). ∎

Locks of other domains, reached through an arm's nested messages, are acquired by those messages' own executions (Theorem A1, case 2), not by `h`.

Phase 15.3 cannot defer an arm's class past `T`. Its deferral applies only to a leading top-level conditional whose condition contains no message or observable action, and falls back to the full entry plan for every other control-flow shape. The decision to enter a `recover` arm is made mid-body, after the `try` suite has run. If `T` sits inside a branch of an eligible leading conditional, the arm's classes belong to that branch and are acquired right after the condition, which is still before `T`.

### P4. Theorem A1: recovery preserves the rank invariant

**Theorem A1.** In programs that use `raise`, `try`/`recover` and bare `raise`, every Moss-managed acquisition satisfies RI. Hence Theorem 28.1 and Phase 20 Theorem 1a hold, and there is no wait-for cycle.

*Proof.* Under O3 there are exactly two kinds of acquisition.

1. **Own-domain acquisition** of ClassSet(h), at entry or at a 15.3 site, in increasing class rank. At that point `H(t)` holds only lower-ranked classes of the same domain, plus locks of ancestor domains on the current call path, which have lower domain rank. Theorem 28.1 already covers this situation. Recover arms make the ClassSet larger, but the argument does not depend on its size.
2. **Acquisition by a nested callee's execution**, including one whose `message` was issued from a `recover` arm. The target lies on a declared route edge (O7), so its domain rank exceeds that of every domain whose locks `t` holds. By Lemma 1, those are only the ancestors on the current call path, because callees that returned or raised earlier retain nothing.

There is no third kind. A `recover` arm never acquires or upgrades its own domain's classes: by Lemma 2 they are already held in sufficient mode. The arm's messages fall under case 2. Raise propagation is a return. Joining Branches before acting on a raise is a join over Branches that hold no locks, which Phase 20's A7 already covers. So RI holds for every acquisition, and Phase 21 adds no new wait kind. ∎

The theorem holds for `recover` at any depth of the call stack. Lock safety never required `recover` to be Root-only.

### P5. Theorem A2: serializability extends to raising executions

**Theorem A2.** Theorem 27.1's conclusion holds for every completed execution, whether it completes by reply, by reaching the end of the body, or by a typed raise exit.

*Proof.* The argument for Theorem 27.1 uses four facts. Each still holds when the exit is a raise.

- **Every executed access is covered:** by O1 and Lemma 2.
- **Touched guards follow strict 2PL:** every exit path releases only at completion (O5). No acquisition by the execution follows a release, because `h` acquires none of its own domain's classes in an arm or after a raise (O3). A nested message's acquisitions belong to the callee's execution, which satisfies the same rule.
- **The lock point comes before the first observable action:** every acquisition by `h` happens at entry, or at a 15.3 site whose condition has no observable action (O3). So all of them come before any `message` or `echo` in the `try` suite.
- **Nested messages are synchronous, and results are materialized before completion:** a raised payload is owned and materialized before release (O5).

So a raise exit is just one more completion point of a strict-2PL execution, and the standard 2PL serializability argument applies to the whole history. Executions that panic never complete, because the process aborts before any guard is released, so they stay outside the claim as before. ∎

This is a statement about isolation, not validity. An execution that raised after writing `s = 1` is serialized correctly. Whether `s = 1` satisfies the application's invariants is still the programmer's problem, as §7 says.

### P6. Lemma Q: no live Branch where a raise takes effect

**Lemma Q.** Under O9, at every point where a Root's own code commits a raised outcome produced by a Branch, raises in its own code, or propagates a raise out of a pipeline, no Branch spawned by that Root is live, and no FileIO read token held by a Branch is live.

A Branch may produce a raised outcome while its siblings are still running. That outcome is a value in the Branch's slot; the lemma is about when the parent can act on it.

*Proof.* Branches exist only inside a bounded window, and windows run one at a time. A raise reaches the parent in one of four ways:

1. A Branch's read or mapper raises. The Branch stores an indexed raised outcome in its slot. The parent reads slots only after the window's `branch_join`, so by the time it commits that outcome, every Branch of the window has finished.
2. The parent's fold raises during the commit loop, which runs after the join.
3. The initializer raises before the first window is published.
4. The sequential fallback raises; it spawns no Branches.

The next window is published only after the commit loop ends without a terminal event (O9), so no Branch of a later window exists either. Read tokens belong to Branch closures and are dead by `branch_join` (Phase 20 §13.1; ROOT_RUNTIME_ABI). Nested messages are synchronous, so none is running while the Root's own code runs. ∎

The current lowering already has this shape: `file_chunk_codegen.inc` calls `branch_join(scope)` before it reads any slot. So the Root wrapper needs no separate quiesce step. Phase 21's tagged outcome slots must keep this join-before-read order, and so must any future construct that spawns Branches.

### P7. Theorem B1: the fresh Root phase starts with nothing held

**Theorem B1.** When a Root wrapper begins acquiring ClassSet(h_fail), `H(t)` is empty.

*Proof.* By the time the raised outcome reaches the Root wrapper, every frame of the body has exited. Nested messages have returned, and by Lemma 1 they retain nothing. A Root has no ancestor holding locks, because Root ingress happens where no Moss lock is held (Phase 20 A6: neither `main` nor a host thread holds one). So the only Moss locks `t` holds are those the Root wrapper acquired for `h`, and O6 drops all of them before acquiring ClassSet(h_fail). Branches hold no Moss locks (A7), and by Lemma Q none is live anyway. No value passed to the trailer keeps a guard alive (O8). ∎

The generated Rust backs up O8. Protected reads are borrows whose lifetime is tied to their guard (SYNCHRONIZATION_PLAN). A wrapper that tried to hand such a borrow to the trailer would fail to compile, because the guard could not be dropped while the borrow is still live.

### P8. Theorem B2: deadlock freedom with `on_fail`

**Theorem B2.** In programs with `on_fail` trailers, every Moss-managed acquisition satisfies RI, and the transition from the abandoned attempt to the trailer adds no wait outside Phase 20's kinds. Hence there is no wait-for cycle.

*Proof.* E1 satisfies RI by Theorem A1. Between E1 and E2 the wrapper does two things:

- It closes the attempt's Root-local FileIO. That is a Solo kernel `close` plus the inode registry's leaf lock, which is never held across a kernel call. These are Phase 20's Cases 1 and 2, and Phase 20 already allows them while E1's guards are held.
- It releases E1's guards, which waits on nothing.

E2 begins with `H(t)` empty (Theorem B1), so its first acquisition trivially satisfies RI. It then acquires ClassSet(h_fail) in increasing class rank (O6). Its messages follow declared routes (O7). Any `try`/`recover` inside the trailer satisfies Theorem A1, with `h_fail` in place of `h`. As far as the wait-for graph is concerned, E2 is indistinguishable from a Root running a handler whose body is the selected trailer arm, which Theorem 28.1 and Phase 20 Theorem 1a already cover. The trailer cannot admit Roots (O7, E12). A synchronous caller waiting for the Root's reply waits outside Moss and holds no Moss lock (A6). ∎

Theorem B2 is a deadlock-freedom result only. Completion is the separate, conditional claim in Corollary B3.

**Corollary B3 (conditional progress).** Assume Phase 20's A0–A7: Solo operations finish, Root and Branch code terminates (A1, applied here to the trailer's code too), Moss locks are granted fairly, and admitted Roots are scheduled fairly. Then every wait in E2 is of a kind that Phase 20's Lemma 1 shows ends, so Theorem 1b extends: every started Root completes, including its trailer, and `executor.join()` drains it. Without those assumptions there is no progress guarantee. In particular, lock fairness is still the open debt `SYNC-FAIR-001`.

**Corollary B4 (two intervals, no atomicity).** By Theorem A2, E1 and E2 are each serializable on their domains. Nothing places them next to each other in the serialization order, so other executions may fall between them. This is §7's stated non-claim.

**Remark: why FileIO closes before the guards are released.** Theorem B2 holds with either order, since it needs only that E2 starts with nothing held. Closing first adds one guarantee. An execution that needs any guard E1 held must wait for E1 to release it, so by the time it runs, E1's abandoned files have released their inode claims and cannot cause it a spurious `InUse`. Collisions with Roots on other domains stay timing-dependent, as in Phase 20 §23.

**Remark: why Root-only is a semantic rule.** B2 uses only one fact: every lock still held ranks below the first lock of the fresh plan. A nested handler that dropped just its own guards would still hold only its ancestors' locks, which all have lower domain rank, so a fresh own-domain plan would satisfy RI too. `on_fail` is Root-only because of what it means (only the Root knows no ancestor will handle the error), not because of lock safety.

### P9. Why each obligation is needed

Each counterexample uses a domain `D` with classes `C1 < C2`, and a second handler `g` on `D` that writes leaves in both classes, so `g` acquires `C1` and then `C2` at entry.

- **CE-A: acquiring inside a `recover` arm (violates O3).** `h`'s normal path touches only `C2`; its arm writes a `C1` leaf. Suppose the arm acquired `C1` only when it ran. Then `h` holds `C2` and requests `C1`, while `g` holds `C1` and requests `C2`: a cycle. Acquiring that late would also come after the `try` suite's `message`, which breaks Theorem 27.1's requirement that the lock point precede the first observable action.
- **CE-B: upgrading in the arm (violates the mode rule in O1 and O3).** `h` reads leaf `x` in class `C`, and its arm writes `x`. Two executions of `h` both hold `C` shared, both raise, and both request exclusive mode. Each waits for the other's shared lock: a cycle. Holding `C` exclusive from entry, which is the inflation the compiler warns about, prevents it.
- **CE-C: keeping E1's guards into E2 (violates O6).** `h`'s plan holds `C2`, and its trailer needs `C1`. If E2 requested `C1` while still holding `C2`, it would deadlock with `g` exactly as in CE-A. So dropping every guard first is needed for deadlock freedom, not only to avoid inflating the normal path.
- **CE-D: planning the trailer in `h`'s own column (violates O2).** This causes no deadlock. But every normal execution of `h` would then hold, in exclusive mode, everything the trailer writes: the global-reset inflation that `on_fail` exists to avoid.

### P10. Corrections applied to Part II

**C1. `on_fail` can add shared locks to readers elsewhere.** Under O2, the trailer's accesses never enter ClassSet(h), so it never adds exclusive locks to `h`. But its writes join the domain's set of mutable leaves (v0.1 §23), and every reader of a leaf that any handler writes must lock it. So a handler, including `h`'s own normal path, that used to read a leaf lock-free because nothing wrote it now takes that leaf's class in shared mode once the trailer writes it.

Example: in a one-handler `Cache`, `Load` reads `value` without a lock. Adding a trailer that sets `value = 0` makes every `Load` take `value`'s class in shared mode.

Any new writer of that leaf would impose the same cost, and writes in `recover` arms impose it too, on top of `h`'s own warned inflation. *Applied in §3 step 5, which previously said `on_fail` "never causes a normal-path inflation warning", and in §2's warning edge case.*

**C2. Serializability covers raising executions.** §7 previously kept the serialization statement only for failure-free completed intervals. Theorem A2 extends it to executions that exit by raise; only the validity of the state they leave behind is disclaimed. *Applied in §7 and in §15 (V4-01).*

### P11. What these proofs do not claim

- That application state is valid after a raise (an unchanged non-claim).
- That E1 and E2 are atomic together (Corollary B4).
- Fairness or freedom from starvation beyond Phase 20's A2 and the open fairness debt `SYNC-FAIR-001`.
- Anything about panics, which still abort the process before releasing guards.
- That the trailer's code terminates. That is an assumption (A1), as it is for all Root code.
- Branch ordering and speculative panics, which §10 and §11 (P21-10A and P21-10B) cover.
- That the current compiler satisfies O1–O9. These are acceptance gates for the implementation, not facts about today's code.
