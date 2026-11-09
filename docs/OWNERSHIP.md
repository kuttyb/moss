# Ownership

This is the source of truth for Moss ownership. Whole-local moves and message
borrows are distinct: a binding may move, a synchronous message lends, and an
executor invocation takes ownership of its arguments.

## Copy and non-copy values

`copy(x)` makes an explicit, independent value of the same type. Moss derives
`Moss::Copy`: primitives, `String`, `Vector`, `Map`, `Queue`, `option`, and
records and enums whose contents are all `Moss::Copy`. Files, FileIO `Range`
values, domain handles, the executor, branch and scope handles, and any value
that contains one of these are `NonCopy`. The name `copy` is reserved.

<!-- moss-test: pass output="1 1 2" -->
```moss
fn main():
  var original = Queue()
  original.push(1)
  duplicate = copy(original)
  duplicate.push(2)
  echo original.pop(), duplicate.pop(), duplicate.pop()
```

An implicit copy of a non-primitive element, map value, payload, or domain-state
value is rejected with `IMPLICIT_COPY`. Write `copy(...)` at the use that needs
an independent value.

<!-- moss-test: fail code=IMPLICIT_COPY -->
```moss
type Point:
  x: Int

fn main():
  points = [Point(x: 1)]
  point = points[0]
  echo point.x
```

<!-- moss-test: pass output="1 9" -->
```moss
type Point:
  x: Int

fn main():
  points = [Point(x: 1)]
  point = copy(points[0])
  point.x = 9
  echo points[0].x, point.x
```

The same rule applies to map values:

<!-- moss-test: fail code=IMPLICIT_COPY -->
```moss
fn main():
  var values = Map()
  values["answer"] = [7]
  result = values["answer"]
  echo result |> count
```

<!-- moss-test: pass output="1 2" -->
```moss
fn main():
  var values = Map()
  values["answer"] = [7]
  result = copy(values["answer"])
  result.push(8)
  echo values["answer"] |> count, result |> count
```

Replying with a received payload or domain state also needs an explicit copy:

<!-- moss-test: fail code=IMPLICIT_COPY -->
```moss
type Point:
  x: Int

domain Echo:
  fn RoundTrip(payload: Point) -> Point:
    reply payload

fn main():
  echoer = Echo()
  result = message echoer.RoundTrip(Point(x: 7))
  echo result.x
```

<!-- moss-test: pass output="7" -->
```moss
type Point:
  x: Int

domain Echo:
  fn RoundTrip(payload: Point) -> Point:
    reply copy(payload)

fn main():
  echoer = Echo()
  result = message echoer.RoundTrip(Point(x: 7))
  echo result.x
```

<!-- moss-test: fail code=IMPLICIT_COPY -->
```moss
domain Store:
  items = [1, 2]

  fn Items() -> Vector[Int]:
    reply items

fn main():
  store = Store()
  result = message store.Items()
  echo result |> count
```

<!-- moss-test: pass output="2" -->
```moss
domain Store:
  items = [1, 2]

  fn Items() -> Vector[Int]:
    reply copy(items)

fn main():
  store = Store()
  result = message store.Items()
  echo result |> count
```

`copy` cannot duplicate a `NonCopy` value; read it in place. This is reported as
`COPY_REQUIRES_MOSS_COPY`. If an aggregate contains a `NonCopy` field, the
diagnostic names that field.

<!-- moss-test: fail code=COPY_REQUIRES_MOSS_COPY -->
```moss
enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO

fn main():
  try:
    file = FileIO.open("tmp/doc-copy-noncopy.data", create)
    value = copy(file)
    file.close()
  recover:
    pass
```

## Field values

A non-primitive field can leave its field only through `take` or `copy`:

- `take(s.f)` is for `Vector`, `Map`, and `option` fields. The target must be a
  field-only path such as `s.inner.items`; indices and method calls are not
  allowed in the path. It moves the value out and leaves `[]`, an empty `Map`,
  or `None` in the field.
- `copy(s.f)` makes an independent value of any `Moss::Copy` field, including
  records, enums, and `String`.
- A `NonCopy` field cannot leave its field. Read it in place.

An implicit field move is rejected with `IMPLICIT_FIELD_MOVE`.

<!-- moss-test: fail code=IMPLICIT_FIELD_MOVE -->
```moss
type Holder:
  items: Vector[Int]

fn main():
  var s = Holder(items: [1, 2])
  values = s.items
  echo values |> count
```

<!-- moss-test: pass output="2 0" -->
```moss
type Holder:
  items: Vector[Int]

fn main():
  var s = Holder(items: [1, 2])
  values = take(s.items)
  echo values |> count, s.items |> count
```

<!-- moss-test: pass output="1 9" -->
```moss
type Point:
  x: Int

type Holder:
  point: Point

fn main():
  var s = Holder(point: Point(x: 1))
  point = copy(s.point)
  point.x = 9
  echo s.point.x, point.x
```

Assigning a field to a local does not create a borrow. It is an error by design;
choose `take` or `copy` according to the field type.

<!-- moss-test: fail code=IMPLICIT_FIELD_MOVE -->
```moss
type Holder:
  items: Vector[Int]

fn main():
  var s = Holder(items: [1, 2])
  b = s.items
  echo b |> count
```

`Queue` is not a `take` target:

<!-- moss-test: fail code=TAKE_UNSUPPORTED_FIELD_TYPE -->
```moss
type Holder:
  items: Queue[Int]

fn main():
  var s = Holder(items: Queue())
  items = take(s.items)
  echo items |> count
```

## Whole values, matching, and calls

Whole-local assignment still moves a non-primitive value (`b = a`); rebinding
and the existing join and loop rules are unchanged. Reads in place, including
field reads, `for`, `match`, and READ-only arguments, do not copy or move the
value. `match consume` consumes an owned enum and gives its selected payloads to
the matching arm. A field is an interior place, so it cannot be consumed
directly by `match consume`.

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

## Messages and executor work

`message` lends each argument as a read-only borrow for the synchronous call.
It emits no argument copy, the handler cannot edit or consume the payload, and
the sender keeps its value. A handler that needs to reply with the payload or
domain state writes `reply copy(value)`.

<!-- moss-test: pass output="2 2" -->
```moss
domain Reader:
  fn Count(values: Vector[Int]) -> Int:
    reply values |> count

fn main():
  reader = Reader()
  values = [3, 4]
  message_count = message reader.Count(values)
  echo message_count, values |> count
```

`executor.invoke` gives its arguments to queued work. The value moves into the
work, and later use is `OWNERSHIP_USE_AFTER_CONSUME`; pass `copy(x)` when the
caller must keep it.

<!-- moss-test: fail code=OWNERSHIP_USE_AFTER_CONSUME -->
```moss
domain Worker:
  total = 0

  fn Count(values: Vector[Int]):
    total = total + (values |> count)

fn main():
  worker = Worker()
  values = [3, 4]
  executor = Executor().threads(1).start()
  executor.invoke(worker.Count(values))
  executor.join()
  echo values |> count
```

<!-- moss-test: pass output="2 2" -->
```moss
domain Worker:
  total = 0

  fn Count(values: Vector[Int]):
    total = total + (values |> count)

  fn Total() -> Int:
    reply total

fn main():
  worker = Worker()
  values = [3, 4]
  executor = Executor().threads(1).start()
  executor.invoke(worker.Count(copy(values)))
  executor.join()
  total = message worker.Total()
  echo total, values |> count
```

## Collection removal and indexing

`Vector.remove(i)`, `Vector.swap_remove(i)`, and `Map.remove(k)` move the
removed element or value out. They raise `CollectionError.InvalidIndex` or
`CollectionError.MissingKey`, respectively; use `try`/`recover` when those
errors may occur. By contrast, `pop()` on an empty collection, a missing
`m[k]`, and an out-of-range `v[i]` panic.

<!-- moss-test: pass output="1 2 2\n4 6 2\n7 0" -->
```moss
fn main():
  var values = [1, 2, 3]
  try:
    first = values.remove(0)
    echo first, values[0], values |> count
  recover CollectionError.InvalidIndex:
    echo "invalid"

  var other = [4, 5, 6]
  try:
    first = other.swap_remove(0)
    echo first, other[0], other |> count
  recover CollectionError.InvalidIndex:
    echo "invalid"

  var lookup = Map()
  lookup["key"] = 7
  try:
    found = lookup.remove("key")
    echo found, lookup.keys() |> count
  recover CollectionError.MissingKey:
    echo "missing"
```

## Unsupported

Borrowing on local assignment is not supported: `b = s.f` is an
`IMPLICIT_FIELD_MOVE` error. `Queue` is not a `take` target. Neither rule is
implemented as an implicit copy or move.
