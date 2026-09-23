# ExprEval

Tokenizes and evaluates arithmetic expressions such as `3 + 4 * (2 - 1)` and
`-(5 - 8) / 2`. It uses shunting-yard to produce RPN, then evaluates the RPN.
Supported: `+ - * / %`, parentheses, unary minus (including `--4`), and Int and
Float literals (`12`, `2.5`, `.5`). Rejected with an error message: unbalanced
parentheses, a trailing operator, empty or whitespace-only input, a missing
operand or operator, a malformed number (`1.2.3`, a lone `.`), an unknown
character, division by zero, and `%` with a Float operand.

Only the core language is used: no domains, handlers, `message`, `domainroutes`,
or concurrency.

## Intent (how I would write it in Python)

```python
class Kind(Enum): NUM, OP, LPAREN, RPAREN
@dataclass
class Token: kind: Kind; op: str = ""; value: int | float = 0

def tokenize(s: str) -> list[Token]:
    for ch in s: ...            # iterate characters, int(ch), float("2.5")
def to_rpn(tokens) -> list[Token]: ...   # shunting-yard, raise SyntaxError
def eval_rpn(rpn) -> int | float: ...    # stack; raise ZeroDivisionError
def evaluate(s: str) -> int | float:
    return eval_rpn(to_rpn(tokenize(s)))
```

## Design in Moss

- **Input is `Vector[String]` of single characters**, not a `String`. Moss v0.1
  has no documented way to index, iterate, measure, or slice a `String`, so
  `evaluate(["3", "+", "4"])` stands in for `evaluate("3+4")`. This is the most
  significant change of shape.
- **Numbers**: `type Num { is_float: Bool, i: Int, f: Float }`. This is a manual
  tagged union. Int op Int gives Int, using truncating `/` and `%`. Any Float
  operand promotes the result to Float. There is no Int-to-Float builtin, and
  mixed arithmetic fails native lowering, so `int_to_float` builds the Float one
  decimal digit at a time.
- **Tokens**: `type Token { kind: String, op: String, value: Num }`. `kind` is
  one of `"num" | "op" | "lparen" | "rparen"`, because Moss has no enum. The
  tokenizer marks unary minus as `op == "neg"` when an operand is expected.
- **Errors**: there are no exceptions or `Result`, so each stage returns a
  record with `ok` / `error` (`Lexed`, `Rpn`, `Outcome`).
- **Token streams**: `tokens[k].kind` passes `moss check` but fails native
  lowering, and binding `t = tokens[k]` consumes the vector. So `to_rpn` and
  `eval_rpn` reverse the vector once and then `pop()` owned tokens in source
  order.
- **Boolean logic**: every `and`/`or` became nested `if` statements or small
  helpers (`is_operator_char`, `starts_number`, `either_float`).
- Loops are `while` loops because Fast Debug does not run `for`. There is no
  recursion.

## Tension log (sorted by cost)

| # | Wanted | Had to | Why | Class | Cost |
|---|---|---|---|---|---|
| 1 | `for ch in s`, `s[i]`, `len(s)` on a String | Take a `Vector[String]` of characters | `s[0]`: "value is not an indexable container"; `for c in s`: "type 'string' is not statically iterable"; `s \|> count`: "unknown local function 'count'"; `len(s)`: unknown function. No String API in bootstrap `source_surface` or the docs | missing surface | 6 |
| 2 | `tokens[k].kind` on a `Vector[Token]` | Reverse the vector, then `pop()` owned tokens | Passes check and Fast Debug. Native fails with rustc E0277 "the type `[Tok]` cannot be indexed by `i64`" (no `as usize`). `Vector[String]` indexing works | native-lowering bug | 3 |
| 3 | `if a and b`, `return x == 1 or x == 2` | Nested `if` statements and helper predicates | In an `if`: check ok, native ok, interp "unsupported expression 'a and b'". As a value (`return a and b`): TYPE_INFERENCE_FAILED at check. Not in `source_surface.operators` | Fast Debug gap + frontend bug (SWARM-037/036) | 3 |
| 4 | `Float(i)` / `i * 1.0` | `int_to_float` digit loop plus a `digit_float` lookup table | `i * 1.0`: check ok, interp prints 7, native rustc E0277 "cannot multiply `i64` by `{float}`". No conversion builtin is documented | native-lowering bug (plus missing surface) | 2 |
| 5 | `c = chars[i]` then reuse `chars` | Inline `chars[i]` at every use; field-wise `copy_num` | OWNERSHIP_USE_AFTER_CONSUME: binding an indexed String element consumes the whole vector. The message says "Create an explicit deep copy", but no copy syntax is documented | intended rule + docs gap | 2 |
| 6 | Guard `if not r.ok: return r` followed by use of `r` | `if ... return r else: ...` | OWNERSHIP_USE_AFTER_CONSUME: the checker treats the returning branch as falling through | frontend bug | 2 |
| 7 | `if (ops \|> count) == 0:` | `size(ops) == 0` helper | Check ok; `moss fmt` "indentation jumps more than one block level" | formatter bug (SWARM-032) | 2 |
| 8 | `return -1` | `return 0 - 1` | Check, interp, and native ok; `moss fmt` FORMAT_PARSE_ERROR "cannot infer the type of this return expression" | formatter bug (SWARM-033/003 family) | 1 |
| 9 | `while v \|> count > 0` | `size(v) > 0` | Parses as `v \|> (count > 0)`. Diagnostic: "unknown local function '0'" | diagnostic quality | 1 |
| 10 | `"unexpected operator " + c` | Fixed messages | String `+` checks and runs in interp; native rustc E0308 (`String + String`) | native-lowering bug | 1 |
| 11 | `break` out of the number-scanning loop | A `scanning` flag | Check ok, native ok, interp "unknown local 'break'". `break` is not in `source_surface` | Fast Debug gap | 1 |
| 12 | A Queue as the token stream, with emptiness via `q \|> count` | A reversed Vector | Queue has no `count` ("unknown local function 'count'") | missing surface | 1 |
| 13 | Run the `test` blocks under Fast Debug | A generated scratch harness (tests become `fn`s, called from `main`) | No interpreted test runner (margo/moss test are native only) | Fast Debug gap (SWARM-039) | 1 |
| 14 | `x % y` on Floats (Python allows it) | Runtime error "% requires Int operands" | TYPE_MISMATCH "integer remainder operands must have type 'Int'" | intended rule | 1 |
| 15 | `moss edit rename` of `either_float` | Kept the name | Rename rejected with "unknown local function 'either_float'": the call site under `not` was not rewritten (renaming `negate` worked) | tooling bug | 1 |
| 16 | `enum Kind` for token kinds | `kind: String` | No enum or tagged-union syntax in `source_surface` or the docs | missing surface | 0 |
| 17 | `raise` / `Result[T, E]` | `ok`/`error` records per stage | No exception or Result surface | missing surface | 0 |
| 18 | `s.len()` rejected at check | n/a (not used) | Any method name on a String (`s.frobnicate()`) passes check; interp fails "method receiver is not a Moss object"; native passes it to Rust (so `.len()` works by accident) | frontend bug | 0 (included in #1) |
| 19 | TESTING.md examples to match current syntax | Followed the Gentle Introduction instead | TESTING.md uses lowercase `int` and implicit return, and says "Moss does not yet have a module or import system" | docs gap | 0 |

Total significant edit/check cycles on the program: about 12, plus about 30
probe runs.

## Defect reproducers (tmp/surface_expreval/)

| File | check --json | fmt | run --interp | native margo run |
|---|---|---|---|---|
| repro_vec_obj_field_index.moss | ok | ok | `true` | rustc E0277 index by i64 |
| repro_mixed_int_float.moss | ok | ok | `7` | rustc E0277 i64 * float |
| repro_string_concat.moss | ok | ok | `3+` | rustc E0308 |
| repro_and_in_if.moss | ok | ok | unsupported expression 'a and b' | `true` |
| repro_and_return.moss | TYPE_INFERENCE_FAILED | FORMAT_PARSE_ERROR | same as check | same as check |
| repro_break.moss | ok | ok | unknown local 'break' | `3` |
| repro_return_consume_flow.moss | OWNERSHIP_USE_AFTER_CONSUME | FORMAT_PARSE_ERROR | same | same |
| repro_fmt_paren_condition.moss | ok | "indentation jumps more than one block level" | `1` | `1` |
| repro_fmt_negative_return.moss | ok | FORMAT_PARSE_ERROR (type inference) | `-1` | `-1` |
| repro_unknown_string_method.moss | ok | ok | method receiver is not a Moss object | rustc E0599 |
| repro_rename_under_not.moss | ok | ok | `yes` | `yes` (the `moss edit rename` fails) |
| repro_pipe_count_compare.moss | unknown local function '0' | FORMAT_PARSE_ERROR | same | same |
| repro_vec_string_index_move.moss | OWNERSHIP_USE_AFTER_CONSUME | FORMAT_PARSE_ERROR | same | same |
| repro_string_index.moss | not an indexable container | FORMAT_PARSE_ERROR | same | same |

`repro_fmt_paren_in_string.moss` is a negative control: it formats correctly.

## Validation (final source)

```
moss fmt                    rc=0 (fmt --check rc=0)
moss check src/main.moss    "ok": true
margo test                  9 passed, 0 failed
margo build                 rc=0
margo run                   7 / 1 / 1.5 / 4 / 3 expected errors
moss run --interp           identical output to native
margo debug                 identical output
moss run --interp --trace   rc=0, 6275 NDJSON events
Fast Debug test harness     9/9 PASS (tmp/surface_expreval/fd_tests.moss)
```

Program output:

```
3 + 4 * (2 - 1) => 7
-(5 - 8) / 2 => 1
-(5.0 - 8) / 2 => 1.5
17 % 5 * 2 => 4
(1 + 2 => error: unbalanced parentheses
1 + => error: trailing operator
 => error: empty expression
```

I did not browse other examples until the first version passed `moss check`.
After that, I grepped `examples/` and read `examples/swarm/FINDINGS.md` headings
only to match SWARM IDs.
