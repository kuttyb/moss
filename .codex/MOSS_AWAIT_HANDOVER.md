# Moss `await` implementation handover

Repository: https://github.com/kuttyb/moss

## Goal

Extend the current C++17 Moss v0.1 compiler so a message handler may declare a reply type, `reply` a value, and a caller may wait for that reply:

```moss
on Reserve(quantity: int) -> bool
  if available >= quantity:
    available = available - quantity
    reply true
  reply false

let reserved = await inventory.Reserve(quantity)
```

`await` is not a separate communication mechanism. It is a normal domain message carrying a one-shot reply capability. The calling domain must remain logically occupied while it waits: it must not process another message or become reentrant.

Implement this feature in the existing compiler and generated Rust runtime. Do not merely remove `await` from the forbidden-keyword list.

## Existing architecture

The compiler is a single C++17 source file at `src/moss.cpp`:

- `Parser` creates `Program`, `Domain`, `Handler`, and flattened `Stmt` nodes.
- `Checker` validates types, receiver domains, handlers, and message arity.
- `Generator` emits Rust using one OS thread and one `std::sync::mpsc` queue per spawned domain.
- Each generated `DomainMsg` enum contains one variant per handler.
- `DomainRef` methods enqueue messages and update `MossTracker`.
- A domain thread processes one message handler to completion before receiving the next.

That final property gives the desired first implementation almost for free: an awaited receive can block the domain's thread. While blocked, the thread cannot dequeue another domain message, so the domain is non-reentrant.

## Required language contract

### Handler declarations

Support both forms:

```moss
on Notify(text: string)
  echo text

on Contains(key: string) -> bool
  reply true
```

A handler without `-> T` is a one-way handler. A handler with `-> T` is a request/reply handler.

Store the optional reply type on `Handler`, for example:

```cpp
std::optional<string> reply_type;
```

Validate the reply type with the existing `valid_type` logic.

### Await expressions

For this milestone, support `await` only as the complete right-hand side of `let` or local `var`:

```moss
let result = await worker.Compute(42)
var result = await worker.Compute(42)
```

Do not attempt general expression nesting such as:

```moss
let x = 1 + await worker.Compute(42)
```

An awaited target must:

- resolve to a domain reference;
- name an existing handler;
- pass the correct number of ordinary arguments; and
- target a handler that declares `-> ReplyType`.

The inferred type of the new local is the handler's reply type, not `_value`.

Reject `await self.Handler(...)`. Blocking while sending a request to the same serialized domain necessarily deadlocks.

### Reply statements

Support:

```moss
reply expression
```

`reply` is valid only inside a handler declaring `-> T`. It sends the response and terminates the current handler, like `return` after sending.

Reject:

- bare `reply`;
- `reply value` in a one-way handler;
- `return value` (retain the existing rule);
- a reply expression in `main`.

For v0.2, require each reply handler to contain at least one syntactic `reply`. Full control-flow proof that every path replies is desirable but not required for this milestone. If execution reaches the end without replying, the generated code must drop the reply sender; the awaiter must report a clear runtime failure rather than block forever.

### Calling reply handlers without awaiting

Allow a request/reply handler to be sent without `await`; this means “send and ignore the reply.” Generate the one-shot channel, enqueue its sender, and immediately drop its receiver. `reply` should ignore a receiver-dropped send error.

Keep ordinary calls to one-way handlers unchanged.

## Suggested AST changes

Add a dedicated representation rather than encoding await as raw text:

```cpp
struct Handler {
  string name;
  vector<Param> params;
  std::optional<string> reply_type;
  vector<Stmt> body;
  int line = 0;
};

struct Stmt {
  enum class Kind {
    Raw, Send, Echo, If, Else, While, Let, Var, AwaitLet, Reply, Return
  } kind;
  // For AwaitLet:
  //   a = local name
  //   b = receiver
  //   c = handler name (add a field if necessary)
  //   args = message arguments
};
```

Prefer explicit fields or a small `Call` structure over overloading `a` and `b` further if that makes the implementation clearer.

## Parser work

1. Update `parse_handler` to accept either `on Name(args)` or `on Name(args) -> Type`.
2. Remove `await` from the blanket forbidden-prefix list. Keep `async`, `yield`, `lock`, `shared`, and `thread` forbidden.
3. Before ordinary `let`/`var` parsing, recognize an initializer beginning with `await `.
4. Parse the remainder strictly as `receiver.Handler(args)` using shared call-parsing code also usable by ordinary sends.
5. Parse `reply expression` as `Stmt::Kind::Reply`.
6. Preserve current two-space indentation behavior and line-numbered diagnostics.

Avoid substring-based parsing that mistakes `await` inside a string or identifier for the keyword.

## Checker work

For an awaited call:

1. Resolve the receiver using the current environment.
2. Confirm it is a domain reference.
3. Resolve the handler and validate ordinary argument count.
4. Require `handler.reply_type`.
5. Reject `self` as receiver.
6. Bind the destination local to the reply type.

For `reply expression`, require a current reply-capable handler. At minimum, validate that the statement is in the proper context. If practical within the existing lightweight checker, validate obvious literal/local type mismatches; do not build a full type system solely for this change.

Refactor `check_stmts` to receive the current `Handler*` in addition to the current `Domain*`, because reply validation requires handler context.

## Generated Rust protocol

Use `std::sync::mpsc` as a one-shot reply channel for this prototype. No new dependency is needed.

For this Moss declaration:

```moss
on Reserve(quantity: int) -> bool
```

generate the conceptual equivalent of:

```rust
enum InventoryMsg {
    Reserve(i64, Sender<bool>),
}

impl InventoryRef {
    fn Reserve(&self, quantity: i64, __reply: Sender<bool>) {
        self.tracker.begin();
        if self.tx.send(InventoryMsg::Reserve(quantity, __reply)).is_err() {
            self.tracker.end();
        }
    }
}
```

An awaited call should lower conceptually to:

```rust
let (__reply_tx, __reply_rx) = mpsc::channel::<bool>();
inventory.Reserve(quantity, __reply_tx);
let reserved = __reply_rx.recv().unwrap_or_else(|_| {
    panic!("Moss await failed: Inventory.Reserve completed without a reply")
});
```

Use unique temporary names for every awaited or ignored-reply call so multiple calls in one scope compile.

Inside the generated match arm, bind the sender and lower:

```moss
reply value
```

to:

```rust
let _ = __reply.send(value);
break 'handler;
```

The existing domain loop must still call `tracker.end()` exactly once after the handler block finishes. `reply` must not call it directly.

For a non-awaited call to a reply handler, generate a channel, pass its sender, and discard the receiver:

```rust
let (__reply_tx, __reply_rx) = mpsc::channel::<bool>();
inventory.Reserve(quantity, __reply_tx);
drop(__reply_rx);
```

## Tracker and ordering behavior

Preserve these invariants:

- Enqueuing any message calls `tracker.begin()` once.
- Failed enqueue calls `tracker.end()` once.
- Completing the target handler calls `tracker.end()` once, whether it replies or falls through.
- The awaiting caller does not separately modify the tracker for receiving the reply.
- Messages already queued for the awaiting domain remain queued until its current handler resumes and completes.
- Per-sender FIFO and each domain's serialized handler order remain unchanged.

Do not implement reentrant event-loop behavior while awaiting. That would violate Moss's agreed semantics.

## Known deadlock boundary

The blocking prototype can deadlock when domains form an await cycle, for example A awaits B while B awaits A. Self-await must be rejected statically. General cross-domain cycle detection is out of scope because the cycle may be data-dependent.

Document this limitation. A future runtime can suspend continuations instead of OS threads while still keeping the domain logically occupied, but that is not required now.

## Required example

Add `examples/checkout.moss`:

```moss
domain Inventory
  var available: int = 10

  on Reserve(quantity: int) -> bool
    if available >= quantity:
      available = available - quantity
      reply true
    reply false

domain Payments
  var collected: int = 0

  on Charge(amount: int) -> bool
    collected = collected + amount
    echo "charged:", amount
    reply true

domain Checkout
  on PlaceOrder(inventory: Inventory, payments: Payments, quantity: int, price: int)
    let reserved = await inventory.Reserve(quantity)
    if not reserved:
      echo "order rejected: insufficient inventory"
      return

    let paid = await payments.Charge(quantity * price)
    if paid:
      echo "order completed"
    else:
      echo "payment failed"

proc main()
  let inventory = spawn Inventory()
  let payments = spawn Payments()
  let checkout = spawn Checkout()
  checkout.PlaceOrder(inventory, payments, 3, 25)
  checkout.PlaceOrder(inventory, payments, 8, 25)
```

Expected output:

```text
charged: 75
order completed
order rejected: insufficient inventory
```

The two `Checkout` requests are sent by one sender and therefore execute FIFO. The second order cannot run while the first `PlaceOrder` handler is awaiting either dependency.

## Required tests

Add automated tests covering at least:

1. A successful await returning `bool`.
2. Two sequential awaits from one handler.
3. Await from `main`.
4. A reply handler called without await and its reply ignored.
5. Await of a one-way handler is rejected.
6. `reply` in a one-way handler is rejected.
7. `reply` in `main` is rejected.
8. Await of an unknown receiver or handler is rejected.
9. Await with wrong argument count is rejected.
10. Self-await is rejected.
11. A reply handler that falls through causes a clear runtime failure, not a hang.
12. Messages sent to a domain while it awaits are not processed reentrantly.

For test 12, use observable ordering. A handler should print `start`, await another domain, then print `finish`; a second queued handler prints `second`. Assert `start`, `finish`, `second`.

Update `make check` so it builds the compiler, checks and generates both examples, compiles generated Rust, runs it, and validates expected output. Ensure temporary generated files do not become tracked artifacts.

## Diagnostics to provide

Use clear line-numbered compiler errors in the existing style, including messages equivalent to:

```text
moss:12: error: cannot await one-way handler 'Inventory.Notify'
moss:8: error: reply is only valid in a handler declaring '-> Type'
moss:15: error: a domain cannot await itself because handlers are non-reentrant
```

## Documentation updates

Update the README to describe:

- `on Name(...) -> Type`;
- `reply value`;
- `let value = await domain.Message(...)`;
- non-reentrant await semantics;
- the blocking thread-per-domain implementation;
- the possibility of cross-domain await-cycle deadlock; and
- the fact that cancellation, timeouts, and failure propagation remain unimplemented.

Change the compiler's displayed version and generated-file banner from v0.1 to v0.2.

## Definition of done

The task is complete only when:

- the compiler builds warning-free with the repository's C++17 flags;
- all old examples still compile and run;
- `examples/checkout.moss` compiles through Moss to Rust;
- the generated Rust compiles without warnings that indicate broken lowering;
- the checkout output matches exactly;
- all required negative tests fail quickly with useful diagnostics;
- the non-reentrancy ordering test passes repeatedly; and
- README and `--help` accurately describe the implemented feature.

Do not redesign Moss broadly during this task. Keep the implementation dependency-free and scoped to synchronous waiting for one-shot replies over the existing message mechanism.
