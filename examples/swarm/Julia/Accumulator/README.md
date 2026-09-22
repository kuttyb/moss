# Julia `Accumulator` → Moss

## Result

Bounded failure. Current Moss can hold a concrete `Map[String, Int]` and perform
existing-key read/modify/write, but it cannot express Julia's missing-key default
read or compute `sum(values(map))` from a Map's actual contents.

## Source and scope

- Repository: [JuliaCollections/DataStructures.jl](https://github.com/JuliaCollections/DataStructures.jl)
- Revision: [`0ed46fbdb4bbfded67bd463814a3899b0c08444c`](https://github.com/JuliaCollections/DataStructures.jl/commit/0ed46fbdb4bbfded67bd463814a3899b0c08444c)
- File: `src/accumulator.jl`
- Translated operations: `get`, indexed access, `inc!`, `dec!`, and `sum(values(map))`.

The bounded specialization is String keys and Int values. The source defines an
`Accumulator` with `Map[String, Int]` storage; no generic syntax, interop, or
compiler changes are used.

## Translation

Julia's `get(map, key, zero(V))` was first attempted as `data.get(key, 0)`;
Moss reported `TYPE_INFERENCE_FAILED` for the return expression. An existence
test rewrite, `data.contains(key)`, was rejected as `invalid collection operation
'contains'`. Direct `data[key]` remains in the checked source so the required
missing-key behavior fails visibly rather than being hidden by prepopulation.

Julia computes `sum(values(data))`. Two source-faithful probes were tried:
`data |> sum` inferred the Map rather than an Int, and `for value in data` was
rejected because `map[string,int]` is not statically iterable. The `total_count`
field in this project is therefore a clearly marked secondary control for
existing-key mutation only, not a source-faithful implementation of `total()`.

## What worked naturally

- `Accumulator(data: Map(), ...)` obtains concrete Map field types from context.
- String-key insertion, indexed existing-key reads, and read/modify/write work.
- Ordinary methods, Int arithmetic, negative values, and canonical formatting work.

## Validation

The existing-key control test passes. The three required missing-key tests fail
at their first absent-key read with `no entry found for key`; `main` fails for the
same documented reason. `moss check --json` and `margo build` pass.

## Independence note

During setup, an overly broad general-example search printed Python Counter path
matches before this Julia conclusion. This implementation and its probes were
derived from `accumulator.jl` and the compiler results above, not from that Moss
implementation; the caveat is recorded for the later comparison.

## Comparison with Python `Counter`

Both concrete Map-backed experiments are bounded failures on the same
missing-key/default lookup requirement (SWARM-008), while both independently
demonstrate empty contextual Map construction, String-key insertion, and
existing-key mutation. Neither establishes source-faithful total aggregation:
the Python experiment maintains an explicit total, and this Julia experiment
additionally proves that Map values are not statically iterable (SWARM-010).
Neither experiment implements merge; the Julia failure is directly attributable
to absent Map iteration, while Python did not provide a separate iteration
probe. The shared missing-key result is a Map-surface limitation; the direct
aggregation probe is currently Julia-specific evidence.
