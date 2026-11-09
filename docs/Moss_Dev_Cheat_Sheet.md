# Moss ownership cheat sheet

- `copy(x)` makes an independent copy of a `Moss::Copy` value (primitives, String, Vector, Map, Queue, option, and records/enums of those); it is reserved, and `NonCopy` values cannot be copied (`COPY_REQUIRES_MOSS_COPY`).
- A non-primitive element, Map value, payload, or domain-state value cannot be copied implicitly; write `copy(...)` (`IMPLICIT_COPY`).
- A non-primitive field leaves only with `take(field)` for Vector/Map/option or `copy(field)`; otherwise `IMPLICIT_FIELD_MOVE`.
- Whole-local moves, rebinding, join/loop rules, and in-place reads (`for`, `match`, READ-only calls) are unchanged; `match consume` consumes an owned enum.
- `message` lends read-only arguments and the sender keeps them; `executor.invoke` moves arguments into queued work, so use `copy(x)` to retain one.
- Replies of incoming payloads or domain state require `reply copy(value)`.
- `Vector.remove`, `Vector.swap_remove`, and `Map.remove` return moved values and raise `CollectionError`; empty `pop`, missing `m[k]`, and out-of-range `v[i]` panic.
- `take` accepts only a field-only path to a Vector/Map/option field; Queue is unsupported.
- `copy` and `take` are reserved names. Borrowing a field through assignment (`b = s.f`) is not supported.

See [OWNERSHIP.md](OWNERSHIP.md) for the complete rules, examples, and diagnostics.

`match consume` takes an owned enum and binds the selected payload:

<!-- moss-test: pass output="ready" -->
```moss
enum Status:
  Ready(value: String)
  Waiting

fn main():
  status = Status.Ready(value: "ready")
  match consume status:
    case Ready(value):
      echo value
    case Waiting:
      echo "waiting"
```

It cannot consume an enum directly from a field (the field is an interior
place):

<!-- moss-test: fail code=MATCH_CONSUME_INTERIOR_PLACE -->
```moss
enum Status:
  Ready(value: String)
  Waiting

type Holder:
  status: Status

fn main():
  var holder = Holder(status: Status.Ready(value: "ready"))
  match consume holder.status:
    case Ready(value):
      echo value
    case Waiting:
      echo "waiting"
```
