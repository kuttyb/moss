# Surface / Report

Parses 20 inline CSV-like lines of the form `region,product,units,price`. It
drops malformed and zero-unit rows, groups the rest by region, computes
per-region row count, total units, revenue, average unit price and max units,
sorts the regions by revenue (highest first), and prints the top 3 as an
aligned text table. The program uses only the core language: no domains,
handlers or `message`.

```
lines=20
valid=18
regions=5
total revenue=642.45
some row sold more than 15 units
all kept prices are positive
Rank  Region     Rows  Units   Revenue  AvgPrice  MaxUnits
1     south         4     30    158.75      5.29        14
2     central       3     31    141.00      4.54        15
3     east          3     33    116.05      3.51        20
```

This output is byte-identical to the Python intent (`tmp/surface_report/intent.py`),
natively (`margo run`) and under Fast Debug (`moss run --interp`, `margo debug`).

## Layout

- `src/main.moss`: the program, with text utilities, records, grouping, sort and table code
- `tests/report_test.moss`: 7 `test` blocks
- `Moss.toml`

## Intent (Python)

```python
rows = [r for r in map(parse, DATA.splitlines()) if r is not None and r.units > 0]
groups = defaultdict(list)
for r in rows: groups[r.region].append(r)
stats = [(g, len(rs), sum(r.units for r in rs), sum(r.units*r.cents for r in rs), ...)
         for g, rs in groups.items()]
stats.sort(key=lambda s: -s.revenue)
for i, s in enumerate(stats[:3], 1):
    print(f"{i:<6}{s.region:<10}{s.rows:>5}{s.units:>7}{s.revenue/100:>10.2f}...")
```

## Design decisions

1. **Text is a `Vector[String]` of one-character strings.** In checked Moss v0.1
   a `String` supports only `+` and `==`. It cannot be indexed, iterated,
   measured, split or parsed (see T1). The inline CSV is therefore written as
   per-line character vectors, and each line has the original CSV text above
   it as a comment. `raw_csv()` flattens the lines into one character stream
   with `"\n"` separators, and the program parses that stream. The vectors
   were generated mechanically from the Python `DATA` string.
2. **I wrote every string utility myself:** `concat`, `join`
   (`reduce("", concat)`), `digit_value`, `is_digit`, `digit_char`, `slice`,
   `find_all`, `split`, `parse_int`, `is_int`, `parse_cents`, `is_price`,
   `spaces`, `int_text`, `money_text`, `pad_left`, `pad_right`.
3. **`Text { s, width }`.** Moss cannot measure a String's length, so every
   printable cell carries its width. Header labels use `lit("Region", 6)`, with
   the width counted by hand.
4. **Money is fixed-point `Int` cents.** This avoids Float parsing and
   formatting. The average price is `revenue / units`, a unit-weighted average
   in whole cents with truncating division.
5. **Grouping uses pipelines, not filtering.** `records |> filter(_.region == g)`
   is rejected for object elements (T2). Instead `Record` has
   `units_in(name)`, `revenue_in(name)` and `rows_in(name)`, which return 0 for
   other regions. `group_stat` is then four pipelines: `map(...) |> sum` three
   times and `map(...) |> reduce(0, bigger)` for the max.
6. **I wrote my own sort: `sortperm_desc(keys: Vector[Int])`.** It is a stable
   insertion sort over indices, like Julia's `sortperm(rev=true)`. Swapping
   objects inside a `Vector` consumes the vector (T3), so the program sorts
   `Int` indices and reads `stats[order[k]]`.
7. **`while` loops only.** Fast Debug cannot execute `for`. Loops never use
   `and`/`or` because Fast Debug mis-parses them (T11).
8. Pipelines in use: `map`, `sum`, `count`, `reduce`, `any` (`_ == name`,
   `_.units > 15`), `all` (`is_digit`, `_.price_cents > 0`, `_.units > 0`),
   with `_` placeholders, placeholder method calls with a captured argument
   (`_.units_in(name)`), and named functions (`revenue_of`, `concat`, `bigger`,
   `is_digit`).

## Tension log (sorted by cost)

| # | COST | CLASS | Summary |
|---|---|---|---|
| T1 | 5 | missing surface + docs gap | String is opaque: no char access, length, split, parse or int-to-string |
| T4 | 4 | native-lowering bug | String `+` on READ params or on `var` + literal fails rustc; `var out = a` CONSUMEs `a` |
| T11 | 4 | Fast Debug gap (SWARM-037) | `and` mis-parsed in Fast Debug; `(a) and (b)` fails check |
| T10 | 3 | native-lowering bug (new) | A parameter named like a top-level function makes `push` emit `&(...)` |
| T1b | 2 | frontend bug (SWARM-036 class) | Unknown String methods pass `moss check` as `_value` |
| T3 | 2 | intended rule | `x = vec[i]` for a non-trivial element consumes the whole vector |
| T6 | 2 | missing surface / diagnostic quality | `Map()` read-before-write fails to infer; no `Map[K,V]()` |
| T9 | 2 | native-lowering bug (new) | `vec[i].field` as call arg / struct field / assert operand gets no `usize` cast |
| T15 | 2 | formatter bug (SWARM-032) | `if (pipe) == 0:` breaks `moss fmt` **and `moss edit`** |
| T2 | 1 | intended rule + docs gap | `filter` over objects rejected; no documented "explicit deep copy" |
| T5 | 1 | frontend bug (SWARM-034) | No-value fn ending in `push` fails result inference |
| T7 | 1 | frontend (ownership) | Rebinding a moved `var` (`cur = Vector[String]()`) rejected |
| T8 | 1 | native-lowering bug (new) | `f(xs \|> count).field` emits raw `\|>` into Rust |
| T12 | 1 | Fast Debug bug (new) | `f(xs \|> map(g))` gives "internal error: missing checked map output type" |
| T14 | 1 | formatter bug (~SWARM-003/033) | `return -1` rejected by fmt/edit only |
| T16 | 1 | missing surface | No multi-line collection literals |
| T20 | 1 | diagnostic quality | Semantic queries refuse to run while the program has any error |
| T23 | 1 | docs gap | TESTING.md is stale; `moss test --interp <file>` exists but is undocumented |
| T24 | 1 | docs gap | `reduce(init, fn)` argument order undocumented |
| T13 | 0 | Fast Debug gap (documented) | No `for` in Fast Debug |
| T17 | 0 | Fast Debug bug (new) | `y == "\n"` gives "unsupported expression"; `"\n" == y` works |
| T18 | 0 | Fast Debug bug (new, wrong result) | `"apple" < "banana"` is `false` in Fast Debug, `true` natively |
| T19 | 0 | native-lowering bug (new) | `echo mk("y").s` with a consuming String param passes `&str` |
| T21 | 0 | diagnostic quality | `moss fmt` reports ownership/type errors as `FORMAT_PARSE_ERROR` |
| T22 | 0 | tooling bug | `impact sortperm_desc` lists no affected tests although a test calls it |

Details follow. Reproducers live in `tmp/surface_report/r_*.moss` (scratch).
Every one was run through `moss check --json`, `moss fmt`, `moss run --interp`
and native `margo run`, using `tmp/surface_report/matrix.sh`.

**T1**
- WANTED: `f = line.split(",")`, `int(f[2])`, `len(region)`, `f"{x:>8}"`
- HAD TO: character-vector data plus 17 hand-written utilities and a `Text` width carrier (design decisions 1–3)
- WHY: bootstrap `source_surface` lists no String operations. `s[0]` gives `value is not an indexable container`. `for ch in s` gives `type 'string' is not statically iterable`. `len(s)`, `int(s)` and `str(n)` give `UNKNOWN_SYMBOL_OR_TYPE`. The compiler source has no string builtins.
- CLASS: missing surface, plus a docs gap (the docs never say that String is opaque)
- COST: 5
- Matrix (`r_string_no_char_access.moss`): check error, fmt error, interp error, native error. All four agree.

**T1b**
- WANTED: a check-time error for `s.len()`
- HAD TO: n/a (a trap)
- WHY: `s.bogus_zzz(1)` checks OK with type `_value`. Fast Debug says `method receiver is not a Moss object`. Natively it passes straight through to Rust (`s.len()` prints 12).
- CLASS: frontend bug, same class as SWARM-036
- COST: 2
- Matrix (`r_string_method_passthrough.moss`): check ok / fmt ok / interp error / native runs

**T4**
- WANTED: `fn concat(a, b): return a + b`, and `out = out + " "`
- HAD TO: `var out = ""; out = out + a; out = out + b; return out`, with every String `+` routed through `concat`
- WHY: native builds emit `(a) + (b)` on `&String` (E0369) and `(out) + (" ".to_string())` (E0308). `moss ownership concat` showed that `var out = a` makes `a` CONSUME.
- CLASS: native-lowering bug
- COST: 4
- Matrix (`r_strcat_params.moss`, `r_var_plus_literal.moss`): check ok / fmt ok / interp ok / native rustc error. `r_strcat_local.moss` is the working form.

**T11**
- WANTED: `while j > 0 and keys[a] < keys[b]:`, `return r.valid and r.units > 0`, `return (n > 0) and all(...)`
- HAD TO: nested `if`s, plus a `while j > 0:` loop with `else: j = 0`
- WHY: Fast Debug fails differently per form:
  - `j > 0 and x` gives `unsupported expression '0 and keys[j - 1]'` (it seems to split on `>` before `and`)
  - `r.valid and r.units > 0` gives `unknown field 'valid and r'`
  - `yes(1) and yes(2)` gives `unsupported expression`
  - `(r.valid) and (r.units > 0)` fails **check** with `TYPE_INFERENCE_FAILED`
- CLASS: Fast Debug gap (SWARM-037, with new precedence detail) plus a frontend bug for the parenthesized form
- COST: 4
- Matrices:
  - `r_interp_and_field.moss`, `r_interp_not_and.moss`, `r_interp_and_pipe.moss`: check ok / fmt ok / interp error / native ok
  - `r_paren_and_return.moss`: check error in all four

**T10**
- WANTED: `fn add_line(text: Vector[String], ...)` alongside `fn text(chars: Vector[String]) -> Text`
- HAD TO: rename the function to `chars_text` with `moss edit rename`, then rename the parameter to `buf`
- WHY: the native build emitted `(*text).push(&("x".to_string()))` (E0308). I found this by delta-bisecting the full program.
- CLASS: native-lowering bug (new)
- COST: 3
- Matrix (`r_push_param_shadow.moss`): check ok / fmt ok / interp ok / native rustc error

**T3**
- WANTED: in-place insertion sort swapping `GroupStat` objects, or `tmp = gs[j]`
- HAD TO: `sortperm_desc` over `Int` keys. Element copies are made with `concat("", xs[i])` or `copy_record(r)`.
- WHY: `OWNERSHIP_USE_AFTER_CONSUME: value 'gs' was transferred`. `y = xs[1]` on a `Vector[String]` also consumes `xs`.
- CLASS: intended rule (ownership)
- COST: 2

**T6**
- WANTED: `seen = Map(); if seen.get(name, 0) == 0: seen[name] = 1`
- HAD TO: a `Vector[String]` of seen names checked with `seen |> any(_ == name)`
- WHY: `MOSS_COMPILE_ERROR: invalid collection operation 'get'` (no fixes or alternatives). `Map[String, Int]()` gives `unknown local function`.
- CLASS: missing surface / diagnostic quality
- COST: 2
- Matrices: `r_map_get_first.moss` and `r_map_typed_ctor.moss` fail identically in all four

**T9**
- WANTED: `group_stat(records, name, records[i].region_width)`, `Record(region: records[i].region, ...)`, `assertEqual(stats[order[0]].region, "south")`
- HAD TO: accessor helpers `width_of`, `region_of`, `copy_record`, and the test helpers `group_region` and `group_max`. Method calls such as `stats[i].avg_price_cents()` work.
- WHY: the native build emitted `records[i].x` with an `i64` index (E0277)
- CLASS: native-lowering bug (new)
- COST: 2
- Matrix (`r_index_field_arg.moss`): check ok / fmt ok / interp ok / native rustc error

**T15**
- WANTED: `if (chars |> count) == 0:`
- HAD TO: `n = chars |> count` first
- WHY: check, interp and native accept it. `moss fmt` and `moss edit rename` both fail with `indentation jumps more than one block level`, so a formatter bug blocks semantic edits.
- CLASS: formatter bug (SWARM-032; the `moss edit` impact is new)
- COST: 2
- Matrix (`r_fmt_paren_pipe_if.moss`, plus the `edit_repro/` project): check ok / fmt error / interp ok / native ok

**T2**
- WANTED: `records |> filter(_.region == g) |> map(_.units) |> sum`, and `records |> filter(keep)`
- HAD TO: `map(_.units_in(g)) |> sum`, and a loop plus `copy_record` in `clean()`
- WHY: `filter over nontrivial element type 'Rec' cannot produce a new collection without an explicit deep copy`. The diagnostic has no fixes or alternatives, and no doc says how to write an "explicit deep copy".
- CLASS: intended rule plus docs gap
- COST: 1
- Matrix (`r_filter_objects.moss`): error in all four

**T5**
- WANTED: a no-value `add_line` ending in `buf.push("\n")`
- HAD TO: add a trailing bare `return`
- WHY: `TYPE_INFERENCE_FAILED: cannot infer the result type`. The suggested alternative, "add a concrete type annotation", has no documented spelling for a no-value result.
- CLASS: frontend bug (SWARM-034)
- COST: 1
- Matrix (`r_trailing_push.moss`): error in all four

**T7**
- WANTED: a split loop doing `parts.push(cur); cur = Vector[String]()`
- HAD TO: `find_all` separator positions, then build each part fresh with `slice`
- WHY: `OWNERSHIP_USE_AFTER_CONSUME` at the re-assignment line. Reinitializing a moved binding is legal in most move checkers.
- CLASS: frontend (ownership). It may be intended, but I could not find documentation.
- COST: 1
- Matrix (`r_rebind_after_move.moss`): error in all four

**T8**
- WANTED: `echo concat("lines=", int_text(records |> count).s)`
- HAD TO: bind the count to a local first
- WHY: the native build emitted `wrap(xs |> count).n` verbatim into Rust
- CLASS: native-lowering bug (new)
- COST: 1
- Matrix (`r_pipe_in_call_arg.moss`): check ok / fmt ok / interp ok / native rustc parse error

**T12**
- WANTED: `order = sortperm_desc(stats |> map(revenue_of))`
- HAD TO: `revenues = stats |> map(revenue_of)` first
- WHY: Fast Debug fails with `internal error: missing checked map output type`
- CLASS: Fast Debug bug (new)
- COST: 1
- Matrix (`r_map_in_call_arg.moss`): check ok / fmt ok / interp error / native ok

**T14**
- WANTED: `return -1`
- HAD TO: `return 0 - 1`
- WHY: `moss fmt` (and therefore `moss edit`) fails with `FORMAT_PARSE_ERROR: cannot infer the type of this return expression`
- CLASS: formatter bug (close to SWARM-003/033, but here check passes)
- COST: 1
- Matrix (`r_return_neg_literal.moss`): check ok / fmt error / interp ok / native ok

**T16**
- WANTED: a multi-line data literal
- HAD TO: one `add_line(buf, [...])` statement per line
- WHY: `indentation jumps more than one block level`
- CLASS: missing surface
- COST: 1
- Matrix (`r_multiline_literal.moss`): error in all four

**T20**
- WANTED: `moss ownership concat` while `main` still had an ownership error
- HAD TO: build a separate probe file
- WHY: every query returns the program's first error instead of a result
- CLASS: diagnostic quality
- COST: 1

**T23** (docs gap, cost 1)
- `docs/TESTING.md` uses lowercase `int` types and says "Moss does not yet have a module or import system".
- Earlier SWARM-039 wording overstated the gap: `moss test --interp
  <standalone.moss>` works. The remaining limitation is project-wide interpreted
  discovery/orchestration through Margo; I ran this workload on src+tests
  concatenated into a scratch file.

**T24** (docs gap, cost 1)
- `reduce(fn)` gives `Reduce stage expects 2 arguments`, and `reduce(fn, init)` gives `unresolved functional callable '0'`. The correct order is `reduce(init, fn)`, which no doc states.

**T13** (Fast Debug gap, documented, cost 0)
- WANTED: `for r in records`
- HAD TO: `while` with an index everywhere

**T17** (Fast Debug bug, new, cost 0; `r_interp_escape_rhs.moss`)
- `y == "\n"` fails in Fast Debug with `unsupported expression`, but `"\n" == y` works.
- check ok / fmt ok / interp error / native ok

**T18** (Fast Debug bug, new, wrong result, cost 0; `r_string_less.moss`)
- `less("apple", "banana")` is `false` in Fast Debug and `true` natively. I avoided String ordering entirely.
- Comparing two string literals directly also fails natively (`&str < String`).

**T19** (native-lowering bug, new, cost 0; `r_literal_consume_field.moss`)
- `echo mk("y").s`, where `mk` consumes a String, fails with E0308 `expected String, found &str`.
- check ok / fmt ok / interp ok / native error

**T21** (diagnostic quality, cost 0)
- `moss fmt` refuses any file with a semantic error and labels it `FORMAT_PARSE_ERROR`.

**T22** (tooling bug, cost 0)
- `moss impact sortperm_desc --source src/main.moss` returns `affected_tests: []`, although `tests/report_test.moss` calls `sortperm_desc` directly.
- `moss test --affected` ran all 7 tests, which is conservative but correct.

## Validation results (final source)

Run from `examples/swarm/Surface/Report/`. The log is in `tmp/surface_report/validation.log`.

| Command | Result |
|---|---|
| `moss fmt` | exit 0, no changes |
| `moss check src/main.moss --json` | `ok: true`, no diagnostics |
| `moss check tests/report_test.moss --json` | `ok: true` |
| `margo test` | **7 passed, 0 failed** (native) |
| `margo build` | exit 0 |
| `margo run` | table above |
| `moss run --interp src/main.moss` | identical output |
| `margo debug` | identical output |
| `moss test --interp tmp/surface_report/combined.moss` (src+tests concatenated) | **7 passed** (Fast Debug) |
| `moss run --interp --trace src/main.moss` | 57,701 NDJSON events: `FunctionEnter`/`FunctionExit` 2,816 each, `LoopIteration` 3,189, `BranchTaken` 2,460, `MethodEnter` 381 |
| `moss test --affected --json` | 7 selected, 7 pass |

I did not look at any other `examples/` project. After the first version passed
`moss check`, I read only `examples/swarm/FINDINGS.md`, to match defects to
SWARM entries.
