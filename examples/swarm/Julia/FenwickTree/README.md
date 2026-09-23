# Julia `FenwickTree` → Moss

This dogfooding experiment translates `FenwickTree{T}` (binary indexed tree) from
JuliaCollections/DataStructures.jl into pure Moss, specialized to `Int`.

## Source and scope

- Upstream: [DataStructures.jl `src/fenwick.jl`](https://github.com/JuliaCollections/DataStructures.jl/blob/master/src/fenwick.jl)
- Frozen reference: `reference/fenwick.jl`. This is the real upstream file,
  fetched verbatim from `raw.githubusercontent.com` (master) on 2026-09-23. It was not
  written from memory.
- Implemented (Julia → Moss):

| Julia | Moss |
| --- | --- |
| `FenwickTree{Int}(n)` | `fenwick_zeros(n)` |
| `FenwickTree(a::AbstractVector)` | `fenwick_from(values)` (n × `inc!`, same as Julia) |
| `length(ft)` | `ft.length()` |
| `inc!(ft, ind, val)` | `ft.inc(ind, val)` |
| `dec!(ft, ind, val)` | `ft.dec(ind, val)` = `inc(ind, 0 - val)` |
| `incdec!(ft, left, right, val)` | `ft.incdec(left, right, val)` = `inc(left)` + `dec(right)` |
| `prefixsum(ft, ind)` / `ft[ind]` | `ft.prefixsum(ind)` (returns 0 for `ind < 1`, as in Julia) |
| (not upstream) | `ft.rangesum(left, right)` = `prefixsum(right) - prefixsum(left - 1)` |

Not translated: generic `T`/`eltype`/`convert` (Moss v0.1 has no source generics,
so the tree is `Int`-only); Julia's default `val = 1` arguments; and the
`@boundscheck ... throw(ArgumentError)` checks. Moss v0.1 has no throw/fail
surface for this, so `1 <= ind <= n` is a documented precondition. An
out-of-range index reaches a Vector index failure (`effects` reports
`may_fail: true`), not an `ArgumentError`.

## Translation decisions

### 1-based API, 0-based storage

Julia arrays and the Fenwick index math are 1-based: the `i & -i` navigation
only works when index 1 is the first node. The Moss public API keeps **Julia's
1-based logical indices** (`inc(1, …)` is the first element, `prefixsum(n)` is the
total), so tests and call sites read exactly like the Julia docstrings. Storage is
an ordinary 0-based `Vector[Int]` of length `n`, and logical index `i` lives in
`bi_tree[i - 1]`. The offset appears only at the two vector accesses. The
brute-force test oracle takes 0-based Moss vectors, so `values[i - 1]` is logical
element `i`.

### `i & -i` without bitwise operators

Before writing a workaround I checked discovery. Bootstrap `source_surface.operators`
lists only `+ - * / %`, comparisons, and `not`, with no bitwise operators. The
Gentle Introduction and the design document do not mention them either. Probes
(`tmp/fenwick_repros/`) found:

- `i & j` gives the misleading `UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'i &'`.
- `i | j` and `i ^ j` **pass `moss check`** with type `_value`. Native builds pass
  the operator straight through to Rust, so they "work" (6|3 = 7). Fast Debug
  rejects them with `unsupported expression`. They are not part of the documented
  surface, so I did not use them.
- `<<` and `>>` give `invalid arithmetic expression`.

The lowest set bit is computed arithmetically by `lowbit_from(i, start)`, which
doubles a power of two while it still divides `i`. A Fenwick walk only moves to
indices whose lowest set bit is strictly larger: `i + lowbit(i)` and
`i - lowbit(i)` both clear that bit. So each traversal carries the previous
lowbit forward as `start`, and the total doubling work across one `inc` or
`prefixsum` stays O(log n). Asymptotic complexity matches Julia's.

### Other decisions

- `!` is not legal in Moss identifiers, so `inc!` becomes the method `ft.inc`. Methods
  replace Julia's free functions, with the receiver implicit inside the body.
- `incdec!` keeps Julia's exact semantics: `inc!` at `left`, `dec!` at `right`.
  When the tree is used as a difference array, `prefixsum(k)` is the point value and
  `left <= k < right` is raised by `val`. The half-open `right` is Julia's, and a
  test pins it.
- All loops use `while`, not `for`, because Fast Debug does not yet execute `for`
  traversal.

## What worked well

- `type` with `Vector[Int]` field, methods, indexed read/write on a field, and
  sibling method calls (`dec` → `inc`, `rangesum` → `prefixsum`) worked first time,
  natively and in Fast Debug.
- The assertion failure message (`actual=4, expected=-2` with the source line)
  located my own arithmetic error in a test immediately.
- `effects FenwickTree.inc` correctly showed `self: WRITE`, `ind/val: READ`,
  `may_fail: true`, `may_diverge: true`. `impact lowbit_from` listed every dependent
  method and test. `moss test --affected` correctly skipped everything when nothing
  changed.

## Friction (with classification)

Reproducers are in `tmp/fenwick_repros/` (disposable). All are worked around in
source, and the compiler was not changed.

1. **Parenthesized right operand fails native build.** *(native-lowering bug, new)*
   `c = a * (b + 2)` (also `a + (b * 2)`, `a % (b * 2)`) checks and runs in Fast
   Debug. Native Rust emits `(a).wrapping_mul(((b).wrapping_add(2_i64)))`, and
   rustc's `-D unused-parens` rejects it with `BUILD_BACKEND_ERROR`. A parenthesized
   *left* operand is fine. Repro: `rhs_paren_native.moss`, `rem_paren/`.
   Workaround: a temporary (`next = start * 2`, then `i % next`).
2. **`moss fmt` rejects an `if`/`while` condition that begins with `(`.** *(formatter
   bug, new; formatter family of SWARM-003 but a distinct construct)*
   `while (i / p) % 2 == 0:` passes `moss check` and runs, but `moss fmt` reports
   `FORMAT_PARSE_ERROR: indentation jumps more than one block level` on the *body*
   line. `if (i) == 4:` fails too, and `if 0 == (i / 2) % 2:` is fine. Repro:
   `fmt_paren_while.moss`, `fmt_paren_if.moss`. Workaround: reorder the condition.
3. **Returning a unary-negated variable fails inference.** *(frontend type-inference
   bug, new; same family as SWARM-005)*
   `fn negate(v: Int) -> Int: return -v` gives `TYPE_INFERENCE_FAILED`, even through a
   local (`r = -v; return r`) and even though both types are annotated. The
   `legal_alternatives` ("add a concrete type annotation") do not apply because the
   annotations are already present. `return -5`, `echo -v`, and `y = -x` in `main`
   are fine. Repro: `unary_neg_return.moss`, `unary_neg_local.moss`. Workaround:
   `0 - v`.
4. **Unsupported operators pass checking.** *(frontend validation gap, same class as
   SWARM-026)* `|`, `^`, `&&`, and Int `and`/`or` pass `moss check` with type
   `_value`. Native either works by Rust accident (`|`, `^`) or fails in rustc (Int
   `&&`/`and`). Fast Debug rejects all of them. `&` instead gets a misleading
   `unknown local function 'i &'`. Repro: `op_pipe.moss`, `op_xor.moss`,
   `op_and.moss`, `op_ampamp.moss`, `bitand.moss`.
5. **Boolean `and`/`or` are native-only.** *(Fast Debug gap / undocumented
   surface)* `if a and b:` on `Bool` builds and runs natively but Fast Debug reports
   `unsupported expression 'a and b'`. `source_surface` documents only `not`. Not
   used here. Repro: `bool_andor.moss`.
6. **No Fast Debug path for `test` blocks.** *(tooling gap)* `moss run --interp` and
   `margo debug` execute `main` only. For test parity I mechanically rewrote each
   `test` block into a function called from a generated `main` (scratch file
   `tmp/fw_tests_interp.moss`), and all 8 passed in the interpreter.
7. Minor: `moss fmt --help` / `moss debug --help` treat `--help` as a source path.
   Skill text says `Moss.toml`, while siblings (and this project) use `moss.toml`.
   Both work.
8. *(agent error)* One test expected `-2` where the correct value was `4`. The
   assertion diagnostic made this obvious.

## Validation (from this directory)

| Command | Result |
| --- | --- |
| `../../../../moss fmt` | exit 0 (idempotent) |
| `../../../../moss check src/main.moss --json` | `ok: true`, no diagnostics |
| `../../../../margo test` | 8 passed, 0 failed |
| `../../../../margo build` | exit 0 |
| `../../../../margo run` | output below |
| `../../../../moss run --interp src/main.moss` | identical output |
| `../../../../margo debug` | identical output |
| `../../../../margo debug --trace` | exit 0, 1036 NDJSON events (FunctionEnter/Exit, MethodEnter/Exit, LocalRead/Write, LoopIteration, Return) |
| test blocks under Fast Debug (rewritten scratch copy) | 8/8 pass |

```
length 6
prefixsum(3) 16
prefixsum(6) 24
after inc!(2, 10): prefixsum(3) 26
rangesum(2, 5) 25
after incdec!(1, 4, 1): prefixsum(3) 27
```

Tests: lowbit vs `i & -i`, zero tree, the Julia docstring example, point inc/dec
(including a negative total), `incdec!` range semantics, construction from a vector
vs brute-force prefix sums (with a negative element), construction plus updates vs a
brute-force mirror, and edge indices (first/last of 16, and n = 1).
