# Python `collections.deque` → Moss Implementation

## Result

Success, with two workarounds for native-lowering defects and one for a formatter
defect. `src/main.moss` implements a bounded/unbounded `Int` deque backed by a
ring buffer over a Moss `Vector[Int]`, with Python semantics for construction
(empty, `maxlen`, from a vector, from a vector with `maxlen`), `append`,
`appendleft`, `pop`, `popleft`, `maxlen` eviction at both ends (including
`maxlen=0`), `extend`, `extendleft` (reversing), `rotate(n)` for positive,
negative, and `|n| > len`, `len`, indexing with negative indices (`at(i)`),
`clear`, `count` (`count_of`), `reverse`, and ring growth when unbounded.

All 22 `test` blocks pass natively under `margo test`. The same 22 test bodies
also pass under Fast Debug when run through a scratch harness (see Validation).
`margo run`, `moss run --interp`, and `margo debug` produce identical output.

## Source Reference

- Repository reference: [`reference/deque.py`](reference/deque.py)
- Upstream: CPython `collections.deque`, which is written in C
  (`Modules/_collectionsmodule.c`, a doubly linked list of blocks). The reference
  file is **not** a copy of that code. It is a small pure-Python *semantic model*
  of the targeted API, written from the documented behaviour and checked against
  the real `collections.deque` on the same vectors (`python3 reference/deque.py`
  prints the same line for both).

## Translation Decisions

| Python | Moss | Note |
| --- | --- | --- |
| `deque()` | `new_deque()` | starts with ring capacity 4 |
| `deque(maxlen=m)` | `new_bounded(m)` | ring capacity fixed at `max(m, 1)` |
| `deque(xs)` / `deque(xs, m)` | `deque_from(xs)` / `deque_from_bounded(xs, m)` | built by `extend`, so a bounded deque keeps the rightmost `m` |
| `d.maxlen` (`None`) | `d.maxlen` field (`-1`) | Moss v0.1 has no `Optional`, so `-1` stands for unbounded |
| `len(d)` | `d.len()` | |
| `d[i]` | `d.at(i)` | user types cannot overload `[]` (`value is not an indexable container`) |
| `d.count(x)` | `d.count_of(x)` | avoids confusion with the `count` pipeline stage |
| `IndexError` | `assert(...)` precondition | see below |

- **Ring buffer.** Fields: `buf`, `head`, `size`, `cap`, `maxlen`. Slot of logical
  index `i` is `(head + i) % cap`. Decrementing `head` uses `(head - 1 + cap) % cap`
  because Moss `%` truncates like C and is not Python's floor modulo. `rotate` uses
  an explicit `floor_mod` helper so that `rotate(-12)` on 5 elements behaves like
  Python's `-12 % 5 == 3`.
- **Eviction.** A bounded deque keeps `cap == maxlen`, so "full" means
  `size == cap`. A full `append` overwrites the slot at `head` and advances `head`,
  which drops the leftmost element. A full `appendleft` moves `head` back one
  slot and overwrites it; that slot held the rightmost element. `maxlen == 0`
  drops every element, as Python does.
- **Growth.** A full unbounded deque doubles `cap` and copies the elements in
  logical order into a fresh vector, so `head` becomes 0.
- **rotate.** `k = floor_mod(n, len)`. The code picks the cheaper direction:
  `k` moves from right to left, or `len - k` moves from left to right. A move
  from one end to the other never triggers eviction, because `size` drops first.
- **Failure (`IndexError`).** Moss v0.1 has no exceptions and no source-level
  `fail`/`panic`; both spellings give `UNKNOWN_SYMBOL_OR_TYPE`. `assert(cond)` is
  accepted in ordinary functions and methods, not only in tests, and fails
  closed: native code panics with `Moss assertion failed at line N` (exit 101),
  and Fast Debug reports `interpreter error: assertion failed` (exit 1). The
  built-in `Vector.pop()` fails the same way on an empty vector (SWARM-019). So
  `pop`, `popleft`, and out-of-range `at` assert. The code does not return a
  sentinel, because any `Int` is a legal element. The `effects` query reports
  `may_fail: true` for these methods. There is no way to write a test that
  expects a failure, so the empty-pop path was checked by hand with a scratch
  program (see Validation).

### Value semantics vs Python reference semantics

A Python deque is a shared mutable object: `e = d` aliases it, and
`deque(xs)` copies the iterable's elements. In Moss, `Deque` is an owned
value. Assigning it moves ownership, and it is never silently aliased.
`deque_from(xs)` reads `xs` and copies it into a new ring, so it matches
Python's copy-on-construction. The behaviour that differs is sharing through
aliases: two names can never observe the same deque. Methods that mutate
(`append`, `rotate`, ...) take the receiver with an inferred `WRITE`, and
`ownership` confirms it: `self: WRITE, values: READ` for `extend`. In tests a
mutated deque must be a `var` local.

## What worked

- A `type` with a `Vector[Int]` field and index reads and writes on it, methods
  that call sibling methods without a qualifier (`append(values[i])`), `while`
  loops, `Vector[Int]()`, and `values |> count` all worked on the first try, in
  native code and in Fast Debug.
- `assertEqual` on whole vectors (`assertEqual(d.to_vector(), [3, 2, 1])`) worked
  and gave a clear `actual=[1, 2, 3, 4, 5], expected=[2, 3, 4, 5, 1]` message. That
  message caught a wrong expected value in one of my own tests.
- `assert` works as a general precondition mechanism in both engines.
- Native output and Fast Debug output matched exactly.

## Friction encountered (with classification)

Minimal reproducers are under `tmp/deque_repros/` (the repository's scratch
directory).

1. **`moss fmt` rejects `return (expr) % m`** with
   `UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'return'`, while `moss check`,
   Fast Debug, and native all accept it and compute the right value.
   *Classification: frontend bug (formatter parser disagrees with checker).*
   Reproducer: `tmp/deque_repros/fmt_return/`. Workaround: I removed the helper;
   the code never starts a `return` expression with `(`.
2. **`return (a + 1)` breaks the native build.** The generated Rust
   `return ((a).wrapping_add(1_i64));` trips `-D unused-parens`, while check
   and Fast Debug succeed. *Classification: native-lowering bug.* Reproducer:
   `tmp/deque_repros/native_return_parens/`. Workaround: same as item 1.
3. **Nested mutating calls on one receiver fail rustc.** `appendleft(pop())`
   inside a method (and `b.put(b.take())` outside one) passes `moss check` and
   runs correctly in Fast Debug. Native lowering emits
   `self.appendleft(self.pop())`, which rustc rejects with E0499 (two `&mut self`
   borrows). In Moss semantics the argument is an owned `Int` that is fully
   evaluated before the outer call. *Classification: native-lowering bug*, the
   same family as SWARM-011, which only covered pure Copy sibling reads.
   Reproducer: `tmp/deque_repros/nested_self_mut_call/`. Workaround:
   `moved = pop()` then `appendleft(moved)`.
4. **A no-value function whose last statement is `v.push(x)` fails inference**
   (`TYPE_INFERENCE_FAILED: cannot infer the result type of function 'put'`). Its
   `legal_alternatives` suggest "add a concrete type annotation", but a no-value
   result has no type to annotate. A trailing `return`, or any non-call last
   statement, fixes it. *Classification: frontend bug. Tail-position built-in
   unit call is treated as an implicit return value.* I hit this only while
   reducing item 3; the deque itself never ends a method with `push`.
   Reproducers: `tmp/deque_repros/tail_push_inference.moss`,
   `tail_push_inference_method.moss`.
5. **No `[]` overloading for user types.** `d[0]` on a `type` fails with generic
   `MOSS_COMPILE_ERROR: value is not an indexable container` and no
   alternatives. *Classification: ergonomic/syntax-sugar omission (intended v0.1
   surface).* The code uses `at(i)`.
6. **No documented failure primitive.** The Gentle Introduction and bootstrap
   `source_surface` never say how a user function signals an unrecoverable
   error. That `assert` works outside tests and fails closed had to be
   discovered by probing. *Classification: documentation gap.*
7. **Semantic query oddity.** `effects zeros` reports `may_diverge: true,
   unresolved: true` for a pure loop-and-push helper. Yet `moss check` accepts
   `buf = zeros(4)` as a domain state initializer, and the documented rule says
   divergent or unresolved work is rejected. Either the query is conservative or
   the rule text is imprecise. `calls Deque.rotate` also reports
   `argument_types: [""]` for a local `Int` argument.
   *Classification: tooling inconsistency (low severity).* Reproducer:
   `tmp/deque_repros/p_init_zeros.moss`.
8. Wrong expected value in my own `rotate(-12)` test (*agent error*). The
   implementation was right, confirmed against CPython.

The Julia `CircularBuffer` experiment was glanced at only after this
implementation compiled and passed. Nothing was taken from it.

## Validation

Run from `examples/swarm/Python/deque/`:

| Command | Result |
| --- | --- |
| `../../../../moss fmt` / `moss fmt --check` | clean, idempotent |
| `../../../../moss check src/main.moss --json` | `"ok": true`, 0 diagnostics |
| `../../../../margo test` | **22 passed, 0 failed** |
| `../../../../margo build` | success |
| `../../../../margo run` | output below |
| `../../../../moss run --interp src/main.moss` | identical output |
| `../../../../margo debug` | identical output |
| `../../../../margo debug --trace` | 625 NDJSON events (Method/Function enter/exit, LocalRead/Write, BranchTaken, LoopIteration, Return) |
| Fast Debug test harness | 22/22 test bodies pass under `moss run --interp` |
| Empty `pop()` probe | native: `Moss assertion failed at line 50: assert(size > 0)`, exit 101; Fast Debug: `interpreter error: assertion failed: size > 0`, exit 1 |

```text
rotated len 5
first 3
last 2
bounded first 3
bounded popped 5
count of 3 1
```

This matches CPython: `deque([3, 4, 0, 1, 2])`; `deque(maxlen=3)` extended with
1..5 gives `[3, 4, 5]`, and `pop()` returns 5.

The Fast Debug test harness is needed because there is no interpreted test
runner. The harness is a generated scratch copy of `src/main.moss`
(`tmp/deque_fastdebug_tests.moss`) in which each `test "..."` block becomes a
function called from `main`. `assertEqual` works in ordinary functions, so the
bodies run unchanged. A deliberately wrong expectation was confirmed to fail
there.
