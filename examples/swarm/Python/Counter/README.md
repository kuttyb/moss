# Python `Counter` → Moss experiment

## Result

Bounded failure. The checked Moss source can construct and mutate a concrete
`Map[String, Int]`, but current Map reads do not provide Python Counter's
required missing-key-as-zero behavior. A direct missing-key read reaches the
native map index operation and fails with `no entry found for key`. The core
counter target therefore cannot be completed without pre-populating behavioral
keys, which this experiment deliberately does not do.

## Source

- Repository: [python/cpython](https://github.com/python/cpython)
- Revision: [`d5dede7442ebb4a05cff5309697fb5dcba3d5297`](https://github.com/python/cpython/commit/d5dede7442ebb4a05cff5309697fb5dcba3d5297)
- File: `Lib/collections/__init__.py`
- Relevant operations: `_count_elements`, `Counter.__missing__`, `Counter.total`,
  `Counter.update`, and `Counter.subtract`.

The translation targets only a concrete `String -> Int` counter: construction,
`get`, `increment`, `increment_by`, `decrement`, and `total`. It does not
translate Counter's generic/dictionary inheritance or its broader API.

## Translation attempt

`Counter` uses a `Map[String, Int]` plus `total_count`. CPython's readable
counting loop is a read-modify-write:

```python
mapping[elem] = mapping_get(elem, 0) + 1
```

The Moss attempt mirrors this with `counts[key] + amount` followed by indexed
assignment. `total_count` is updated beside successful mutations; this avoids
requiring Map-entry iteration merely to implement `total()`.

The source starts with direct map indexing in `get`, intentionally preserving
the natural Counter operation that the tests show is not presently available.

## What works for an existing key

- A Moss type can hold concrete `Map[String, Int]` state.
- Indexed assignment establishes concrete Map key/value types.
- Existing-key lookup, read-modify-write, `increment`, `increment_by`, and
  `decrement` execute successfully in the `updates an existing counter key`
  control test.
- Explicit `total_count` maintenance executes successfully for those updates.

This experiment does **not** establish that Moss can naturally compute `total()`
by iterating over Map values; `total_count` is maintained incrementally.

## Friction encountered

### Required missing-key default — bounded blocker

- Python operation: `mapping_get(elem, 0)` and `Counter.__missing__`.
- Natural Moss attempt: `return counts[key]` in `get`.
- Result: source checks and native build succeeds, but a missing key fails at
  runtime with `no entry found for key`; all three behavioral tests therefore
  fail at their first absent-key access.
- Rewrite attempts: `counts.contains(key)` is rejected as `invalid collection
  operation 'contains'`; `counts.get(key, 0)` cannot infer the return type.
- Outcome: no supported Map existence/default lookup was found. Pre-populating
  the behavioral keys would hide the operation under experiment, so it was not
  used. This blocks the required Counter semantics.

### Empty Map field construction

- Natural Moss attempt: `Counter(counts: Map(), total_count: 0)` for a field
  declared `Map[String, Int]`.
- Diagnostic: `field 'Counter.counts' has conflicting inferred types
  'map[string,int]' and 'map'`.
- Rewrite: initialize a local `Map()`, assign one inert type-seed entry, then
  pass that typed local to the constructor.
- Outcome: the source checks. The seed is not a behavioral test key and does
  not implement missing-key behavior.

### Map write from a String method parameter

- Natural Moss attempt: `counts[key] = next` in an ordinary type method.
- Result: source checks, but native lowering passes `&String` to Rust map
  insertion, which requires an owned `String`.
- Rewrite: `stored_key = "" + key` before indexed assignment.
- Outcome: native compilation succeeds. This is recorded as a compiler
  lowering finding rather than a source-language feature.

### Method-to-method reuse

The original draft temporarily treated `increment_by(key, 1)` as unavailable.
A focused String-argument and all-`Int` reproduction both check, as does the
current Counter's implicit call. This was reclassified as the agent’s
intermediate-draft misunderstanding (SWARM-009), not a Moss method-call limit.

### Unary negative formatter issue

The negative-count assertion initially used `-1` and reproduced `SWARM-003`.
The checked source uses `0 - 1` so `moss fmt` can complete; this does not affect
the Map blocker.

## Validation

From this project directory, using repository-local tools:

- `moss fmt src/main.moss`: passes with the documented negative-literal form.
- `moss check src/main.moss --json`: passes.
- `margo build`: passes.
- `margo test`: `1 passed / 3 failed`; the existing-key control passes, while
  the three required Counter tests remain expected failures at their first
  missing `Map` key.
- `margo run`: expected bounded failure, `no entry found for key`.

No compiler, runtime, language, or collection implementation was changed for
this experiment.

## Cross-language comparison

Not yet written. This Agent A report was completed independently and does not
inspect the Julia `Accumulator` experiment until both isolated experiments are
complete.
