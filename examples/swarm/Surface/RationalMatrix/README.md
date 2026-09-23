# RationalMatrix

Exact rational arithmetic and square rational matrices written in Moss v0.1,
split across two Margo packages:

```text
RationalMatrix/
  numeric/            library package (no main)
    Moss.toml
    src/rational.moss   module rational: normalized Rational, gcd/make/add/sub/mul/div/eq/neg/copy/show
    src/matrix.moss     module matrix: Matrix (NxN, row-major), identity, from_ints, mat2,
                        get, size, transpose, mul, det2, determinant (Gaussian elimination), equal, show
  app/                application package, depends on numeric by path
    Moss.toml           [dependencies] numeric = { path = "../numeric" }
    src/main.moss       module app: demo program
    tests/rational_tests.moss   module app_rational_tests (5 tests)
    tests/matrix_tests.moss     module app_matrix_tests   (6 tests)
```

Written by a fresh agent (Claude) as a SWARM "Surface" exercise, core language only
(no domains/messages). The intent was first written in Python
(`tmp/surface_rationalmatrix/intent.py`, disposable) and then expressed in Moss.

## Design decisions

- **Rational is a plain `type` with two `Int` fields**, always built through
  `rational.make(num, den)`, which normalizes sign (denominator > 0), divides by
  the gcd, and maps zero to `0/1`. `den == 0` fails with `assert(den != 0)` (the
  only failure primitive found; see SWARM-040). Moss has no field privacy, so the
  invariant is a convention: a consumer *can* write `rational.Rational(num: 2, den: 4)`.
- **Arithmetic is exported functions** (`rational.add(a, b)`), not operators: Moss
  v0.1 has no operator overloading.
- **Public API is functions, not methods.** Methods on exported types are not
  carried in `.mossi`, so a consumer *package* cannot call them natively
  (`show`, `get`, `size` are exported functions for that reason). The intra-package
  methods `Rational.is_zero` and `Matrix.at` remain, used only inside `numeric`.
- **Field reads across modules go through accessors** (`rational.numer/denom`)
  because native lowering emits private Rust struct fields.
- **Matrix is `n` plus a row-major `Vector[rational.Rational]`.** Reading an element
  returns `rational.copy(...)` of it: returning or pushing `cells[k]` directly moves
  the element and silently makes the parameter CONSUME.
- **Determinant is iterative Gaussian elimination with row swaps** over exact
  rationals (no recursion in v0.1). `det2` is the closed 2x2 formula; tests check
  they agree and that `det(BC) = det(B) det(C)`.
- **All loops are `while`** — `for ... in range(...)` fails to type-check inside an
  explicit module in a project, and Fast Debug does not run `for` anyway.
- **No string building.** `String + String`, `str()`, `to_string()` are not usable in
  both engines, so printing is `echo` with several arguments (`5 / 6`).

## Tension log

Sorted by COST (edit/check cycles consumed), highest first. Reproducers are in
`tmp/surface_rationalmatrix/` (disposable scratch).

1. **`for` over `range` inside an explicit module**
   - WANTED: `for i in range(0, n):` in `export fn identity(n: Int)`.
   - HAD TO: `var i = 0` / `while i < n:` / `i = i + 1` everywhere.
   - WHY: `TYPE_INFERENCE_FAILED: cannot infer the static iterator source type`.
     Same code passes as a standalone file and in a project without `module`; a
     vector-literal `for` in a module works. Repro `repro_range_module/`.
   - CLASS: frontend bug
   - COST: 5
2. **Building a display string**
   - WANTED: `return str(num) + "/" + str(den)` (a `show() -> String`).
   - HAD TO: `echo r.num, "/", r.den` inside `rational.show(r)`.
   - WHY: `str` unknown (in a method it surfaced as `TYPE_INFERENCE_FAILED`, not
     unknown symbol). `"a" + "b"` passes check, Fast Debug prints `a" + "b`, native
     fails rustc E0369. `n.to_string()` passes check, works natively (Rust
     pass-through), fails Fast Debug. Repros `probe_strcat.moss`,
     `probe_strplus_int.moss`, `probe_to_string.moss`.
   - CLASS: frontend bug (checker accepts unsupported String ops) + missing surface
   - COST: 4
3. **Methods of an exported type across packages**
   - WANTED: `value.show()`, `t.at(0, 1)` in `app` on numeric's types.
   - HAD TO: exported functions `rational.show(r)`, `matrix.get(m, i, j)`.
   - WHY: `no matching method 'pt__Point.total' for supplied arguments` at
     `margo build`/`margo test`; `margo debug` runs it fine; the emitted `.mossi`
     lists only fields. Same-package cross-module method calls work. Repro
     `repro_xpkg_method/`. The `margo test` error was also attributed to
     `src/main.moss:15` although the call was in `tests/matrix_tests.moss:15`.
   - CLASS: native-lowering bug (module interface omits methods) + diagnostic quality
   - COST: 3
4. **`assert` as the last statement of a no-value function** (Fast Debug test harness only)
   - WANTED: test bodies turned into plain `fn t_x():` ending in `assert(...)`.
   - HAD TO: end each function with an `echo`.
   - WHY: native emits `assert(expr)` (Rust macro without `!`) for a final
     statement, E0423; non-final asserts lower to `if !(..) { panic!(..) }`.
     Repro `probe_assert_last_stmt.moss`. Looks like the SWARM-034 family.
   - CLASS: native-lowering bug
   - COST: 3
5. **Returning / pushing a Vector element**
   - WANTED: `return cells[i * n + j]`; `tmp = work[a]; work[a] = work[b]`.
   - HAD TO: `rational.copy(cells[...])` in `at`, the swap, and the work-copy loop.
   - WHY: `OWNERSHIP_USE_AFTER_CONSUME ... value 'self' was transferred`.
     Documented rule (no implicit deep copy); `legal_alternatives` were accurate.
   - CLASS: intended rule
   - COST: 2
6. **Object parameters moved into a vector literal**
   - WANTED: `return Matrix(n: 2, cells: [a, b, c, d])` in `mat2`.
   - HAD TO: `[rational.copy(a), ...]`.
   - WHY: checker infers a..d as READ and accepts; native rustc E0308 (expected
     `Rational`, found `&Rational`). Fast Debug fine. Repro `probe_param_into_vec.moss`
     (no modules needed).
   - CLASS: native-lowering bug
   - COST: 2
7. **Reading or constructing an exported type's fields from another module**
   - WANTED: `cell.num` in `matrix`, `p.y` in `app`.
   - HAD TO: `rational.numer(cell)` / `rational.denom(cell)` accessors.
   - WHY: check + Fast Debug accept; native E0616 `field is private` (E0451 for
     construction). `.mossi` even lists `public_representation field`. Repro
     `repro_xmod_field/` (single package, two modules).
   - CLASS: native-lowering bug
   - COST: 2
8. **`moss check` / `moss fmt` in a package with a path dependency**
   - WANTED: `moss fmt; moss check src/main.moss --json` in `app/`.
   - HAD TO: `margo build` first, then `MOSS_MODULE_PATH=$(realpath ../numeric/build/debug)`.
   - WHY: `imported module 'matrix' was not found in source or compiled interfaces`;
     `moss fmt` exits 1 for the same reason. `MOSS_MODULE_PATH` is mentioned only in
     MODULES.md.
   - CLASS: docs gap (and missing Margo-aware check/fmt)
   - COST: 2
9. **`moss test --affected` after a dependency-package change**
   - WANTED: affected tests reselected after I changed `matrix.det2`.
   - HAD TO: rely on full `margo test`.
   - WHY: after a behavior-breaking edit to `det2` and `margo build`, `--affected`
     skipped all 11 tests ("semantic dependency cone is unchanged",
     `conservative_fallback: false`), while `margo test` failed one. Reverted.
   - CLASS: frontend bug (tooling; unsafe selection)
   - COST: 2
10. **Silent CONSUME on a READ-looking API**
    - WANTED: `determinant(m)` to leave `m` usable.
    - HAD TO: copy elements; confirmed with `moss ownership matrix__determinant`.
    - WHY: `work.push(m.cells[idx])` was accepted and inferred `m: CONSUME` with no
      diagnostic at the definition — the surprise would only appear at a caller.
    - CLASS: intended rule (inference is working as specified)
    - COST: 1
11. **`and` in Fast Debug**
    - WANTED: `return a.num == b.num and a.den == b.den`; `if pivot < 0 and not ...`.
    - HAD TO: nested `if`s.
    - WHY: SWARM-037 (read in FINDINGS.md before writing it).
    - CLASS: Fast Debug gap
    - COST: 1
12. **Method call on a call result**
    - WANTED: `rational.add(half, third).show()`.
    - HAD TO: a helper `report(label, value)` taking a local.
    - WHY: `local member calls are not implemented; use a top-level function`
      (consistent in check/interp/native). Message wording is confusing.
    - CLASS: missing surface (+ diagnostic quality)
    - COST: 1
13. **Fast Debug for `test` blocks**
    - WANTED: `margo debug` running the tests.
    - HAD TO: generate a scratch package that turns tests into functions (`fdtests/`).
    - WHY: Margo has no project-wide interpreted test discovery/orchestration
      equivalent to `margo test`; standalone test sources can use
      `moss test --interp` (SWARM-039).
    - CLASS: Fast Debug gap
    - COST: 1
14. **`moss edit rename` in a module project**
    - WANTED: rename private `rational__abs_int`.
    - HAD TO: nothing (abandoned).
    - WHY: `EDIT_TARGET_AMBIGUOUS: rename could not map every semantic reference to
      one exact token`, pointing at an unrelated file/line. Repro `repro_rename/`.
    - CLASS: frontend bug (tooling)
    - COST: 1
15. **Query target names**
    - WANTED: `moss ownership matrix.determinant`.
    - HAD TO: `matrix__determinant` (the mangled name).
    - WHY: `QUERY_TARGET_NOT_FOUND` for `determinant`, `matrix.determinant`, `matrix::determinant`.
    - CLASS: docs gap
    - COST: 1
16. **Private symbol diagnostic** — `rational.abs_int(...)` from `app` reports
    `unknown local function '__moss_private__rational__abs_int'` rather than "not
    exported". CLASS: diagnostic quality. COST: 0.
17. **Invariant cannot be enforced** — exported fields are constructible, so
    `rational.Rational(num: 2, den: 4)` bypasses `make` (Fast Debug prints `2 / 4`).
    CLASS: intended rule. COST: 0.
18. **No `abs`, no unary minus habit** — wrote `0 - x` (avoiding SWARM-033) and a
    private `abs_int`. CLASS: missing surface. COST: 0.
19. **No operators for user types** — `rational.add(rational.mul(a, b), c)` nesting.
    CLASS: intended rule. COST: 0.
20. **Stale testing/project docs** — TESTING.md and PROJECT_WORKFLOW.md still say
    "Moss does not yet have a module or import system", use `int`, `proc main`,
    `moss.toml`, and say nothing about which `module` a `tests/` file declares. I
    guessed "one module per test file, importing the library modules" and it worked
    first time. CLASS: docs gap. COST: 0.

## Validation results (run in `app/`, 2026-09-23)

| Command | Result |
| --- | --- |
| `moss fmt` (plain) | exit 1: imported module 'matrix' not found (see tension 8) |
| `moss check src/main.moss --json` (plain) | `ok: false`, same reason |
| `MOSS_MODULE_PATH=../numeric/build/debug moss fmt` / `fmt --check` | exit 0 / exit 0 |
| `MOSS_MODULE_PATH=... moss check src/main.moss --json` | `ok: true`, no diagnostics |
| `numeric/`: `moss fmt --check`, `moss check` both modules | exit 0, `ok: true` |
| `margo test` | **11 passed, 0 failed** (native) |
| `margo build` | ok |
| `margo run` | ok, 28 lines (output below) |
| `margo debug` | ok, byte-identical to `margo run` |
| `margo debug --trace` | ok, 13 812 NDJSON events (LocalRead 6702, LocalWrite 2039, Return 1238, FunctionEnter/Exit 1106, BranchTaken 937, LoopIteration 420, MethodEnter/Exit 132) |
| Fast Debug test parity (scratch `fdtests/`, tests rewritten as functions) | 11/11 in `margo debug`, 11/11 native, identical output |

`margo run` output (abridged):

```text
1/2 + 1/3 =
5 / 6
...
gcd(84, -36) = 12
det2(A) =
-1 / 10
determinant(A) =
-1 / 10
B * I == B: true
det(B) =
6
det(B*B) =
36
```

`rational.make(1, 0)` fails with `Moss assertion failed at line 25: assert(den != 0)`
natively (exit 101) and `interpreter error: assertion failed: den != 0` in Fast
Debug (exit 1).

Other examples under `examples/` were not browsed at any point. `examples/swarm/FINDINGS.md`
(the ledger, not an example) was read after the first probes, before the first
version built.
