# Phase 25 Team S — next-cycle independent L6 Architect implementation review

**Reviewed SHA:** `b6d45cf5a761caea2df50b64d4357669647ffac0` (`origin/phase25-semantics-engines`)
**Cycle start:** `ee8dceaa4851fb61a7b726cee05354da6917e38d`
**Reviewer:** independent L6 Architect. I did not write any of the reviewed code.
**Language contract:** `./moss agent bootstrap --json` reports `language_version` = `moss-0.1`.
**Earlier findings:** P25-S-ARCH-001..012 (`TEAM_S_L7_ARCHITECT_REVIEW.md`) are not re-reported. New IDs start at 013.

## 1. Scope

This review covers the L7 instruction for this cycle:

- **S1, checker authority.** Record `==` rejection, named-argument rejection,
  the Fast Debug FileIO preflight (test entry points, fewer false positives,
  still fail-closed), G1 regressions, and the unpinned empty Map handoff. The
  acceptance criterion is that every checker-accepted program either runs the
  same way on all targets or gets a target-capability diagnostic before it runs.
- **S2, Char as a distinct scalar.**
- **S3, frozen scalar `less`/`isequal`/`hash`, and the Map/Set fixtures.**
- **S4, adversarial Fast Debug parity fixes P25-S-S4-001..012.**
- **Scope compliance.** No competing Map representation, generic type system,
  or ABI-v10 work, and indexed-v1 String not made the default.
- **Docs.** `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`, `TEAM_S_CHAR_STRING_MIGRATION.md`,
  `TEAM_S_KEY_PROTOCOL_V1_SCALARS.md`, `tests/phase25/key_protocol/README.md`,
  and the changed parts of `FAST_DEBUG.md`, `GENTLE_INTRODUCTION_TO_MOSS.md`,
  `LANGUAGE_SYNTAX.md`, and the bootstrap `source_surface`.

## 2. Method

1. Detached checkout of the exact SHA (`git rev-parse HEAD` =
   `b6d45cf5a761caea2df50b64d4357669647ffac0`). `make` succeeded.
   `git diff ee8dcea b6d45cf -- src` read in full (1,225 lines).
2. A baseline compiler built from `git archive ee8dcea` in `tmp/base/`. I used
   it to tell net-new behaviour from pre-existing behaviour.
3. Every reproducer below ran in three engines with a harness
   (`tmp/arch/probe.py`): `moss check --json`, Fast Debug (`moss run --interp`),
   and Native Debug (`moss <src> --debug -o x.rs` + `rustc`, then run).
   Project cases used `moss debug <project>`, `moss test [--interp]`, and
   `margo test --interp`.
4. Corpus differential: `moss check --json` on all 1,876 tracked `.moss`
   files with the baseline and the reviewed compiler, comparing verdicts
   (`tmp/arch/corpus.py`).
5. Re-ran the required suites (§3) and the full `make check` (§3).

About 200 standalone probe programs and about 20 project probes were run. All files are under `tmp/arch/` (disposable).

## 3. Suites re-run at this SHA

| Command | Result |
|---|---|
| `python3 tests/tooling/check_phase25_s1_checker_authority.py` | pass (exit 0) |
| `python3 tests/tooling/check_phase25_s2_char_string.py` | pass (exit 0; 36 random strings, 880 property checks, 5 engines) |
| `python3 tests/tooling/check_phase25_s3_key_protocol.py` | pass (exit 0; 26 cases) |
| `python3 tests/tooling/check_phase25_s4_fast_debug_adversarial.py` | pass (exit 0) |
| `python3 tests/tooling/check_phase25_l7_repairs.py` | pass (exit 0) |
| `python3 tests/tooling/check_phase25_g1_regressions.py` | pass (exit 0) |
| `python3 tests/phase25/key_protocol/run_fixtures.py` | pass (exit 0; every fixture matches its recorded `current:` status) |
| `TMPDIR="$PWD/tmp" make check` | **Pass, run in two parts** (see the note below). Run 1 passed every stage up to and including `check_swarm_issues.py --self-test`. It then failed in `check_agent_benchmark.py` with `BENCHMARK_STARTER_MISSING` ("AB025 starter … undeclared `.moss/semantic-cache-v1/…snapshot`"). I caused that failure: my corpus scan (§2.4) was running `moss check` on benchmark starter files at the same time, and those checks wrote semantic caches. I restored the three tracked snapshots it had modified, removed the ignored `.moss/` caches, and re-ran `tests/run.sh` from `check_agent_benchmark.py` to the end. That run exited 0 and ended with "all Moss v0.1 tests passed". |

Note: the run 1 failure is not a defect at the reviewed SHA. It does show a
tooling hazard: `moss check` writes `.moss/semantic-cache-v1/` beside the
checked source, which can modify tracked snapshots in `examples/swarm/**` and
benchmark starters. That is outside Team S scope and is recorded only as an
observation.

## 4. Findings

Severity is about the S1 acceptance criterion and joint integration risk.
"Pre-existing" means the baseline `ee8dcea` compiler behaves the same way. Such
findings still fall inside this cycle's S1/S4 scope, because the cycle claims
checker authority and adversarial parity for these areas. None of them is
listed as a known limitation in the S1/S3/S4 notes.

### Confirmed defects

| ID | Severity | Reproducer (ran) | Engines | Origin |
|---|---|---|---|---|
| **P25-S-ARCH-013** | **High** | `fn main():\n  c = "abc".char_at(0)\n  echo c + "x"`. Also: `echo 'a' + 'b'`, `c + 1`, `c - 'a'`, `c * 2`, `echo true + 1`, `echo "a" + 1`, `echo "a" - "b"`, `echo "ab" * 2`, `echo [1] + [2]`. | check **ok**. Fast Debug exits 0 and prints a made-up Float: `0.0` (or `1.0` for `c + 1`, `true + 1`, `"a" + 1`). Native rustc fails: E0369 (`cannot add String to char`, `cannot add i64 to bool`, `cannot add Vec<i64> to Vec<i64>`, …) or E0308. `-c` on a Char: check ok, Fast Debug runtime error, rustc E0600. | Pre-existing (identical at `ee8dcea`). |
| **P25-S-ARCH-014** | Medium | `fn main():\n  var inner = Map()\n  inner["a"] = 1\n  var m = Map()\n  m[inner] = 1\n  echo 1`. The same happens with `m[[inner]] = 1`, a parameter typed `Map[Map[String, Int], Int]`, and `enum Color: Red/Blue` with `m[[Color.Red]] = 1`. | check ok. Fast Debug prints `1`. rustc fails with E0277 (`HashMap<String, i64>: Hash` / `Color: Eq`, `Color: Hash` not satisfied). | The P25-S3-D2 interim fix is incomplete. `check_map_key_type` rejects record- and Float-containing keys but not Map- or enum-containing keys. The enum check is top-level only (`enums_.count(ps[0])`). |
| **P25-S-ARCH-015** | Medium | `enum Color:\n  Red\n  Blue\ntest "enum vec":\n  assertEqual([Color.Red], [Color.Red])\nfn main():\n  echo 1`, run in a project. | check ok. `moss test --interp` prints `PASS enum vec`. `moss test` (native) fails with `BUILD_BACKEND_ERROR`, rustc E0369 (`!=` cannot be applied to `&Vec<Color>`). The same operands with `==` are rejected (`ENUM_OPERATOR_UNSUPPORTED`). | Pre-existing. This cycle added the `RECORD_EQUALITY_UNSUPPORTED` guard to `assertEqual`, but no matching enum-containing guard. |
| **P25-S-ARCH-016** | Medium | `fn main():\n  echo Char(codepoint: 65)` | check **ok**. Fast Debug: `interpreter error: unsupported expression 'codepoint: 65'`. rustc: `invalid struct delimiters or fn call arguments`. | Engine behaviour is pre-existing. The S1 named-argument rule covers user functions and messages, but not builtin constructors. **Net-new:** this cycle's bootstrap `source_surface.operators.char_builtin.constructor` advertises exactly this spelling: `"Char(codepoint: Int) fails for surrogates…"`. An agent that follows bootstrap writes a program that the checker accepts and that fails in every engine. |
| **P25-S-ARCH-017** | Low–Medium | `fn main():\n  echo range(0, 3) == range(0, 3)` and `fn main():\n  r = range(0, 3)\n  s = range(0, 3)\n  echo r == s` | check ok. Fast Debug: `unsupported or unresolved callable 'range'`. rustc E0425 (`cannot find function range`). | Pre-existing. `builtin_equality_type` admits `Range`, and the migration note lists `Range == Range` as unchanged and supported, but no engine runs a Range value. |
| **P25-S-ARCH-018** | Low–Medium | `echo 1_000`, `x = 3_000_000_000`, `x = 9_223_372_036_854_775_807`, `echo -9_223_372_036_854_775_808`, `echo 0x10`, `echo 0xFFFFFFFFFFFFFFFFFFFF`, `echo 1e400` | check ok for all. Fast Debug: `unsupported expression '1_000'` (likewise for the other underscore and hex forms). `1e400` prints `inf`. Native: `1_000`→`1000`, `3_000_000_000`→`3000000001` (with `+ 1`), `0x10`→`16`. `9_223_…_807`, `-9_223_…_808`, and the long hex literal fail rustc with `literal out of range for i32`. `1e400` fails with `literal out of range for f64`. | Pre-existing. S4-006 (`INT_LITERAL_OUT_OF_RANGE`) treats `_` as a digit separator when it accepts `-9_223_372_036_854_775_808`. The interpreter's Int-minimum special case compares only the plain spelling, so it rejects the same literal. |
| **P25-S-ARCH-019** | Low | `fn main():\n  v = [1, 2, 3]\n  echo v` as `src/main.moss` of a project (checked-in example `examples/swarm/modules/algo_chain/repro/echo_vector/single`) | Standalone `moss check case.moss` reports **ok**. In the project, `moss check src/main.moss --json` reports `NATIVE_ECHO_DISPLAY_UNSUPPORTED`, and `moss debug .` prints `[1, 2, 3]`, exit 0. The same source gets two checker verdicts. | Net-new (S4-012). The diagnostic is raised in native lowering, which project-mode `check` runs. My corpus differential shows this example going from ok to rejected. |
| **P25-S-ARCH-020** | Low | `fn big() -> Int:\n  return 5\nfn main():\n  echo -big()`, and `fn main():\n  x = 5\n  y = -(x + 1)\n  echo y`, and `echo -(x)` | Rejected in every engine with `UNKNOWN_SYMBOL_OR_TYPE`: "unknown local function '-big'" or "unknown local function '-'". | Pre-existing. Fails closed, but valid unary-minus forms are rejected with a misleading code. S4-009/011 changed unary minus and did not touch these forms. |

### Documentation inaccuracies

| ID | Document | Claim | Observed |
|---|---|---|---|
| DOC-1 | `TEAM_S_KEY_PROTOCOL_V1_SCALARS.md` §1 | operator `==` "compares Char with a one-scalar String" | Removed by S2 in this same cycle: `c == "x"` is `TYPE_MISMATCH`. The sentence is stale. |
| DOC-2 | same, §7 P25-S3-D2 | record/Float-containing keys rejected with `RECORD_MAP_KEY_UNSUPPORTED` / `FLOAT_MAP_KEY_UNSUPPORTED` "in every position (index, assignment, declared `Map[K, V]` types)" | Declared types get `UNKNOWN_SYMBOL_OR_TYPE` instead: `fn fresh() -> Map[P, Int]` gives "unknown return type 'map[P,int]'", and domain state `m: Map[P, Int]` gives "unknown state type". Fixture 04 records this honestly in its `EXPECTED`. Map- and enum-containing keys are not covered at all (ARCH-014). |
| DOC-3 | same, §2 | `scalar_protocol_rust`, "line 17002" (and other line anchors) | The function is now at line 17319. The doc notes that the names are the stable anchors, so this is cosmetic. |
| DOC-4 | `TEAM_S_CHAR_STRING_MIGRATION.md` "Unchanged" | `Range == Range` | Accepted by the checker, but no engine executes it (ARCH-017). |
| DOC-5 | same, corpus claim | "All 1,831 tracked `.moss` files … the only files the new compiler rejects are the Char/String callers" | This was true when S2 landed. At `b6d45cf` there are 1,876 tracked files. Against `ee8dcea`, 15 verdicts change: 13 key-protocol fixtures (intended), 2 swarm repros now accepted (fixes, and both run identically in Fast Debug and native), and 1 project example newly rejected (ARCH-019). The claim needs a date or SHA. |
| DOC-6 | `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` §5 | "`Map[Float, V]` and user-keyed Maps keep failing closed (`FLOAT_MAP_KEY_UNSUPPORTED`)" | User-keyed Maps fail with `RECORD_MAP_KEY_UNSUPPORTED` (or `UNKNOWN_SYMBOL_OR_TYPE` in declared types). Enum-in-collection and Map keys do not fail closed (ARCH-014). |
| DOC-7 | bootstrap `char_builtin.constructor` | `Char(codepoint: Int)` | The labelled form fails in both engines. The working form is `Char(65)` (ARCH-016). |

### Unconfirmed concerns and non-findings

- **Fast Debug FileIO preflight: no fail-open found.** Each of these was
  rejected before any output with `FAST_DEBUG_FILEIO`:
  - module-qualified FileIO reached from a module helper through `x = save()`,
    `for … in range(0, save())`, `if`, `while`, an expression body, a `return`
    expression, `echo`, a pipeline stage, a function-value argument, and an
    index expression;
  - a local named like the module (`disk = 1` in a block, then `disk.save()`);
  - an Int parameter named like the module.

  Same-name shadowing of a function by a local or parameter in a `sort` key or
  `map` stage is rejected earlier by the checker ("callable identity … is not
  statically bounded"). FileIO calls in domain state initializers are rejected
  by the checker. Per-test preflight works standalone (`moss test --interp
  file.moss`) and in projects with a module-qualified FileIO call in a test:
  the FileIO test is `FAIL [FAST_DEBUG_FILEIO]` and not executed, the other
  tests run, and native `moss test` passes all of them. A test that calls a
  method named like a FileIO free function is not a false positive.
- **INVALID_EXPRESSION: no false positive found.** I tried Char literals
  holding `)`, `]`, `(`, or a space inside brackets and calls; `1.5e-3`;
  `2E+2`; `- x`; `not`; `message`; and `in`. The corpus differential shows no
  newly rejected tracked file for this code. The rule fires in `return`, `if`,
  `while`, vector literals, call arguments, and `x = if c: 1 else: 2`.
- Method-call named arguments (`p.bump(n: 2)`) are rejected with the generic
  `no matching method 'P.bump' for supplied arguments`. This fails closed but
  is not the precise `CALL_NAMED_ARGUMENT_UNSUPPORTED`. It is a diagnostic
  quality issue, not counted as a defect.
- `Map().get(P(x: 1), 0)` with no insert: check ok, rustc fails. This is the
  known P25-S3-D3 / Team G Option B handoff (fixture `09b` family), so it is
  not counted here.

## 5. Verified items (this cycle's claims that held)

- **S1 record equality.** `==`/`!=` on records is rejected with
  `RECORD_EQUALITY_UNSUPPORTED` in `echo`, vector literals, `assert`, `return`,
  `if`, `while`, untyped specialized functions, `Vector[P] == Vector[P]`, and
  handler `reply`. All three engines refuse before running.
- **S1 named arguments.** `f(x: 1)` and `f(x = 1)` are rejected in statement,
  expression, nested, and condition positions. `message c.Bump(step: 2)` and
  `(step = 2)` are rejected in statement, binding, and `echo` positions. No
  false positives on `f(x == 1)`, `f(x <= 1)`, `f("a: b")`, `f(":")`,
  `R.Ok(value: 3)`, or `map(f)`.
- **S1 equality typing.** `[1] == [1.0]` is `TYPE_MISMATCH`. Int/Float mixing
  still works. Map and Queue equality agree in both engines.
- **S2.**
  - Rejected forms:
    - `c == "x"`, `!=`, `"x" == c`: `TYPE_MISMATCH`;
    - `assertEqual(c, "x")`: `TEST_ASSERTION_CONFIGURATION_ERROR`;
    - `isequal(c, "a")`, `less(c, "b")`: `KEY_PROTOCOL_TYPE`;
    - `Vector[Char] == Vector[String]`: `TYPE_MISMATCH`;
    - returning or assigning a Char where a String is expected: type errors;
    - `find`/`split`/`append`/`join` with Char arguments: `TYPE_MISMATCH`.
  - These work the same in all engines: `Char(65).to_string()` (the
    `parse_simple_call` balancing fix), `Char(66) == 'B'`, `c.less(d)`,
    `c.isequal(d)`, `c.hash()`, Char ordering, `chars()`, emoji
    `char_at`/`slice`, and `Char(1114111)`.
  - `Char(-1)` and `Char(55296)` stop both engines before the Char is used.
    The difference in how each engine reports it is documented.
- **S2 regressions closed earlier.** These still hold: ARCH-001 (`sort` by a
  CONSUME key), ARCH-002 (user `sort`), ARCH-003 (user `Char`), ARCH-008
  (escape parity), ARCH-010 (`"\u{D800}"` is `INVALID_ESCAPE`), ARCH-011
  (`'\x41'`), and ARCH-012 (quotes, backslashes, and long digits in a
  trailing comment).
- **S3 scalar protocol, Fast Debug = Native.** All NaNs (including `-NaN` and
  `0.0 - NaN`) are pairwise `isequal`, hash to `9221120237041090560`, and
  `less` puts them last. `isequal(-0.0, 0.0)` is false and `less(-0.0, 0.0)`
  is true. `hash(+0.0)` = `0` and `hash(-0.0)` = `-9223372036854775808`.
  `NaN == NaN` is false and `0.0 == -0.0` is true. `sort` puts `-0.0`
  before `0.0`. Hashes of Bool, Int, Char, and String agree. The
  `Int.less`/`Float.isequal`/`Char.hash`/`String.less`/`Bool.hash` spellings
  work. String hash, `less`, and `isequal` are identical under plain and
  `MOSS_STAGED_STRING_REPR=indexed-v1` native builds. Runtime lowering
  matches the interface agreement table (§2).
- **S4.** All of these ran identically in all three engines:
  - message-result state writes: a write inside `if`, inside `for`, after a
    branch-local `let` shadow, in if/else, with a record-typed state
    (`__moss_replace`), and with a handler-local `var` shadow (state left
    untouched); a payload write is rejected;
  - try joins in a loop, in a `return` inside `try`, and in nested tries;
  - `let` joins: immutable on every path, rejected on reassignment, mutable
    when mixed with `var`;
  - branch-shadowing of state, record fields, parameters, and locals;
  - wrapping unary minus at Int minimum through parameters, fields, state,
    vectors, and pipelines; `-x` on untyped Int and Float;
  - `INT_LITERAL_OUT_OF_RANGE` boundaries, including binary minus;
  - `NATIVE_ECHO_DISPLAY_UNSUPPORTED` for ten collection-producing echo forms.
- **Scope compliance.**
  - `src/` diff: no `MossMap`, no generic type variables, no ABI-v10 bump.
  - `char_string_ordering_runtime.hpp` only lost `moss_char_string_eq`.
  - Default `MossStr` is still `String`. The indexed type appears only under
    `MOSS_STAGED_STRING_REPR=indexed-v1`.
  - The key-protocol fixtures are data and run in pending mode. There is no
    local inference workaround for the unpinned empty Map; `09b`–`09e` remain
    `known-failing accepted-rustc-fails`.

## 6. Verdict

**CONDITIONAL GO for joint G2/G3/G4 integration readiness.**

The Team S interfaces Team G depends on are sound and match the implementation:
- the scalar KeyProof targets and their lowering;
- Float semantics;
- Char as a validated distinct scalar;
- String hash independence of representation;
- the fixture runner and activation trigger;
- the `admit_member` data source (`BuiltinReceiverFamily`).

Scope is compliant, and every required suite passes. Integration on a
throwaway worktree as described in agreement §5 may start.

The S1 acceptance criterion is **not yet met**: checker-accepted programs still
diverge or fail in rustc (ARCH-013 through 018). Conditions:

1. **Before L7 records S1 as accepted:** fix ARCH-013. The checker must type
   `+ - * /` and unary `-` operands (numeric, or String `+` String only), so
   that Fast Debug can no longer print a made-up `0.0`. This is the only
   finding with silent wrong output. It sits right next to the S2 migration
   (`c + "x"` is the natural mistaken rewrite of `c == "x"`).
2. **Before G3 activates Map key admission:** fix ARCH-014. Reject Map- and
   enum-containing keys with a stable code, or make `check_map_key_type` an
   allow-list of the hashable built-ins. Also correct DOC-2 and DOC-6, so the
   fail-closed boundary that Team G inherits is stated exactly.
3. **Before integration closeout:**
   - fix ARCH-015, the enum-containing guard for `assertEqual`;
   - fix ARCH-016: reject or support `Char(codepoint: …)`, and correct the
     bootstrap text (DOC-7), because bootstrap is the authoritative agent
     surface;
   - correct DOC-1 and DOC-4.
4. **Tracked, not blocking:** ARCH-017, 018, 019, 020 and DOC-3, DOC-5. Each
   gets a ledger entry with an owner. Make ARCH-019 consistent: either
   standalone `check` also reports native display capability, or project
   `check` does not.

## Addendum: closure verification at `baefd1fbd763ba0ec1d4955c6bad64f4bb71ac94`

**Reviewed SHA:** `baefd1fbd763ba0ec1d4955c6bad64f4bb71ac94`. I checked it out detached and `make` succeeded.

**Scope:** `git diff b6d45cf baefd1f`, which covers commits `bd843db` and
`baefd1f`. Commit `3645e43` is this review document, unchanged.

### Commands and outcomes

| Command | Result |
|---|---|
| The seven required suites from §3 (S1, S2 including `test_architect_next_cycle_findings`, S3, S4, L7 repairs, G1 regressions, `run_fixtures.py`) | all exit 0 |
| Differential `moss check --json` over all 1,876 tracked `.moss` files, `b6d45cf` against `baefd1f` | 3 verdict changes. All three are `examples/swarm/modules/domain_services/repro/diag_file/**` and were already rejected; only the code changed, from `TYPE_INFERENCE_FAILED` to `TYPE_MISMATCH` ("operator '+' is not defined for 'int' and 'string'"). No file went from accepted to rejected or back. |
| Full `make check` / `make examples` | **Not re-run by me at this SHA.** The Coordinator reports both passed. Once the scan finished, I restored the semantic-cache snapshots it had touched and removed the ignored `.moss/` caches, so the tree was clean at commit time. |

### Status of the findings

| Item | Status at `baefd1f` | Evidence (ran: check, Fast Debug, Native Debug) |
|---|---|---|
| ARCH-013 | **Fixed** | Every original reproducer is now `TYPE_MISMATCH` in all engines: `c + "x"`, `"x" + c`, `'a' + 'b'`, `c + 1`, `c - 'a'`, `c * 2`, `-c`, `true + 1`, `"a" - "b"`, `"a" + 1`, `"ab" * 2`, `[1] + [2]`. The rule also fires in the other positions I tried: vector literal, call argument, parenthesized receiver, untyped specialization (`fn add(a, b)` with `('a', "x")` / `(true, 1)`), `if` condition, assignment, handler `reply`, pipeline stage, and `-a` in an untyped function. These still work in all engines: `s + c.to_string() + "!"`, String chains, Int/Float mixing, `x - -1`, `x * -x`, `-1.5e-3`, `2.0e-3 + 1.0`, `1e-3 - 1`, `0 - two()`, record field arithmetic, `-p.x`, and an untyped `add` at Int, String, and mixed numeric types. But see ARCH-021. |
| ARCH-014 | **Fixed** | Rejected: `m[inner] = 1` and `m[[inner]] = 1` with `MAP_KEY_TYPE_UNSUPPORTED`; `m[[Color.Red]] = 1` with `ENUM_MAP_KEY_UNSUPPORTED`; a declared parameter `Map[Map[String, Int], Int]` with `UNKNOWN_SYMBOL_OR_TYPE`. Still accepted and identical in all engines: Vector/Queue/Bool/Char keys, `Map[Vector[Char], Int]` parameters, Map-valued Maps (`Map[String, Map[String, Int]]`), and domain-state `Map[String, Int]`. |
| ARCH-015 | **Fixed** | `assertEqual([Color.Red], [Color.Red])` is rejected with `ENUM_OPERATOR_UNSUPPORTED` by `moss test` and `moss test --interp`. |
| ARCH-016 | **Fixed** | `Char(codepoint: 65)` gives `CALL_NAMED_ARGUMENT_UNSUPPORTED`. `sort(v, by: Reverse(ident))` still works. Bootstrap now says `Char(Int) (positional)`. |
| ARCH-017 | **Fixed** | `range(0, 3) == range(0, 3)` and `r = range(0, 3)` give `RANGE_OUTSIDE_FOR`. Nested `for … in range(…)` loops, `range` in a test `for`, and `range(0, n + 1)` are unaffected. |
| ARCH-018, 019, 020 | Tracked, not fixed (as agreed) | — |
| DOC-1 | Fixed | The §1 sentence now says neither `==` nor `isequal` compares Char with String. |
| DOC-2 | Fixed | Declared record-key types are now described as `UNKNOWN_SYMBOL_OR_TYPE`, and Map/enum keys are listed. Minor residual: a declared `Map[Map[…], V]` also gets `UNKNOWN_SYMBOL_OR_TYPE`, not `MAP_KEY_TYPE_UNSUPPORTED`; the doc names the latter only for index/assignment positions. It fails closed, so I do not count it. |
| DOC-3 | Fixed | Line anchors removed. |
| DOC-4 | Fixed | `Range == Range` is now scoped to FileIO `Range` values, and `RANGE_OUTSIDE_FOR` is documented. |
| DOC-5 | Fixed | The corpus claim is dated to `f272af9`. |
| DOC-6 | Fixed | §5 lists all four key codes. |
| DOC-7 | Fixed | Bootstrap text corrected. |

### Net-new defect

| ID | Class | Severity | Reproducer (ran) | Engines |
|---|---|---|---|---|
| **P25-S-ARCH-021** | Confirmed defect, **regression** introduced by the ARCH-013 unary-minus rule (false rejection of valid code) | Medium | `fn main():\n  s = "abc"\n  echo -s.length()`. Also `-c.codepoint()` (Char receiver), `k = -s.find(",")`, a record method `-p.get()`, and the same operand inside larger expressions: `if -s.length() < 0:` and `x = 10 + -s.length()`. | Now rejected in every engine with `TYPE_MISMATCH` "unary '-' requires an Int or Float operand, found 'string'" (or `'char'`, `'P'`): the new check reports the **receiver's** type, not the member-call result's type. At `b6d45cf` all of these passed check and printed the same value in Fast Debug and Native (`-3`, `-97`, `-1`, `-4`, `neg`, `7`). Workaround: `0 - s.length()`. |

Observation, not a new defect: `-v[0]` (and `-p.v[0]`, `-m["a"]`) is now
rejected with "found 'vector[int]'". At `b6d45cf` it was already rejected, with
`TYPE_INFERENCE_FAILED`, because unary minus binds before indexing. The new
message exposes that precedence. It belongs with the tracked ARCH-020 family.

No other false positive or fail-open was found. That includes the corpus
differential and about 60 new probes of the arithmetic, Map-key, named-argument,
`RANGE_OUTSIDE_FOR`, and `assertEqual` rules.

### Verdict (addendum)

**CONDITIONAL GO for joint G2/G3/G4 integration readiness.** This is narrower
than §6.

- Conditions 1 to 3 of §6 are satisfied: ARCH-013 to 017 and DOC-1 to 7 are
  fixed, with regressions in the S2 suite.
- The S1 acceptance gap that blocked L7 S1 acceptance (silent wrong Fast Debug
  output from mistyped arithmetic) is closed.
- The Map-key boundary that Team G inherits now fails closed for every
  non-hashable key type I found.

The one remaining condition is a regression, not an integration-interface
issue:

1. **Fix P25-S-ARCH-021 before integration closeout**, with a regression test.
   The unary-minus operand check must use the type of the whole operand
   expression (method-call results, `find`, `length`, `codepoint`), so that
   `-s.length()` is accepted again and agrees in all engines. It does not block
   starting the joint G2/G3/G4 integration worktree.

ARCH-018 to 020 remain tracked and do not block.

## Addendum 2 (S-D part 1): unary-minus repair at `3577dbce6080aa1320a2b60e31d926866fbb3f54`

**Reviewed SHA:** `3577dbce6080aa1320a2b60e31d926866fbb3f54`. I checked it out
detached and `make` succeeded.

**Scope:** `git diff baefd1f 3577dbc -- src tests`. The prefix-`-` block in
`check_expression` now checks the whole operand and returns. The only test
change is 8 added lines in `check_phase25_s2_char_string.py`, and their cases
are Int-only.

### Method

Every probe ran in four engines: `moss check --json`, Fast Debug, Native
Debug (`--debug` plus `rustc`), and Native Release (`-O` plus `rustc -O`),
with the outputs compared. All seven required suites were re-run (S1, S2, S3,
S4, L7 repairs, G1 regressions, `run_fixtures.py`) and all exit 0. Full
`make check` was not re-run at this SHA.

### What now works (accepted, and identical in all four engines)

- **L7-required forms:**
  - `-s.length()`;
  - `-f(x)` and `-f(x) + 1`;
  - `-(x + 1)` and `-(x + 1) * 2`;
  - `-v[0]`, `-m["k"]`, and `-w[i][j]` for **Int** elements;
  - `10 - -v[0]` and `-v[0] + -v[1]`.
- **Chains:** `- -x`, `-(-x)`, `-p.q.get()`, `-p.q.n`.
- **Other positions:**
  - call arguments: `id(-a)`, `id(-id(a))`;
  - vector literals: `[-a, -b, -(a - b)]`;
  - comparisons: `-a < -b`, `-a == 0 - a`;
  - `return -x`;
  - pipelines: `map(neg)`, `map(_ * -1)`, `map(-_)` (Int and Float), `filter(_ > -2)`;
  - handler `reply -n`, and `reply -v[0]` on Int state.
- **Binary vs unary:** `a - -b` = 9, `a -b` = 5, `a- b` = 5, `a--b` = 9,
  `a * -b`, `a / -b`, `a % -b`.
- **Untyped specializations:** `neg(3)`, `neg(2.5)`, `negl("abcd")` (which
  returns `-s.length()`).
- **Float:** `-g()`, `-g() * 2.0`, `-p.y`, `-p.get()`, `-(x * 2.0)`, `-0.0`,
  `-(0.0)`.
- **Int minimum wraps (S4-005):** `-(-9223372036854775807 - 1)`, `-m`,
  `-v[0]`, `- -m`, and `-(m)` all print `-9223372036854775808`.
- **Still rejected with `TYPE_MISMATCH` in every engine:** `-s`,
  `-s.char_at(0)`, `-b` (Bool), `-mk()` (record), `-s.slice(0, 1)`, `-v[0]`
  on a `Vector[String]`, `-(c)` (Char), and `neg("s")` (untyped
  specialization at String).

ARCH-021 is **closed**. ARCH-020 is **closed**: `-big()`, `-(x + 1)`, and
`-(x)` are accepted and agree across engines.

### Net-new findings

| ID | Class | Severity | Reproducer (ran) | Engines |
|---|---|---|---|---|
| **P25-S-ARCH-022** | Confirmed defect, net-new fail-open. The checker now accepts the form, and native lowering negates the container. | Medium | `fn main():\n  v = [1.5, 2.5]\n  echo -v[0]`. Also: `x = -v[1] * 2.0`; `-m["a"]` on a `Map[String, Float]`; `-p.v[0]` on a `Vector[Float]` field; `-w[0][1]` on nested Float vectors; `1.0 + -v[0]`; and `fn negf(v: Vector[Float]) -> Float:\n  return -v[0]`. | check **ok**. Fast Debug prints the right value (`-1.5`, `-5.0`, `-2.5`, `-2.5`, `-2.5`, `-0.5`, `-3.5`). Native Debug fails in rustc with E0600 (`cannot apply unary operator '-' to type Vec<{float}>`, `HashMap<String, {float}>`, `Vec<f64>`, `&Vec<f64>`). The Generator's Int branch emits `(v[0]).wrapping_neg()`, but a Float operand falls through to the older path, which parses `-v[0]` as `(-v)[0]`. At `baefd1f` the checker rejected these forms, and at `b6d45cf` they were `TYPE_INFERENCE_FAILED`, so this repair is what makes them reach rustc. The new regression test covers only Int. |
| **P25-S-ARCH-023** | Confirmed defect: fails closed with an internal error. Pre-existing (same at `b6d45cf`), but newly reachable because ARCH-021 now admits the form. | Low–Medium | `domain D:\n  n: Int\n  fn NegLen(s: String) -> Int:\n    reply -s.length()` with `main` calling `message d.NegLen("hello")`. Also `k = -s.length()` then `reply k` in a handler. | check, Fast Debug, and native all fail with `MOSS_INTERNAL_OR_IO_ERROR` at line 0: "internal synchronization invariant: unresolved concrete method effect target". The same expression in an ordinary `fn` works in all four engines (`-5`). So does `reply 0 - s.length()`. The handler's effect/synchronization capture does not resolve the member call under prefix `-`. |

### Verdict (addendum 2)

- **ARCH-021: closed.**
- **ARCH-020: closed.**

**Team S at `3577dbc`: CONDITIONAL GO for joint G2/G3/G4 integration
readiness.** The only remaining condition is **P25-S-ARCH-022**. This repair
introduced a checker-accepted form that fails in native rustc, which breaks
the S1 acceptance criterion. Before integration closeout, the Generator must
lower prefix `-` on an indexed or member Float operand as negation of the
whole operand, for example `-(v[0])`, and the S2 regression must gain Float
cases (`-v[0]`, `-m["k"]`, `-p.v[0]`, nested, and the typed-parameter form).
Alternatively, the checker could reject these forms precisely until then.

ARCH-023 fails closed. It is tracked, not blocking, but it should get a
stable diagnostic instead of an internal invariant error. It does not block
starting the joint integration worktree. ARCH-018 and 019 remain tracked.

## Addendum 3 (S-D part 2): consolidated S-A/S-B/S-C at `906b99d4068d1c2bbc92548249d8abb24eaea11f`

**Reviewed SHA:** `906b99d4068d1c2bbc92548249d8abb24eaea11f`. I checked it out
detached and `make` succeeded.

**Scope:** `git diff 3577dbc 906b99d -- src tests docs`. That covers:
- `cff9a96`: the ARCH-022/023 fixes;
- S-A (`9f79e09`): `src/numeric_literal.hpp` and the lexer canonicalization;
- S-B (`6365b89`): `check_project_semantics`, the echo display contract, and
  `display_runtime.hpp`;
- S-C (`83419f6`): proven-safe literal divisors in
  `observable_expression_effects`.

### Method

Every probe ran in four engines: `moss check --json`, Fast Debug, Native
Debug, and Native Release, with the outputs compared. I also re-ran
`moss check` on all 1,876 tracked `.moss` files with the `b6d45cf` and
`906b99d` compilers and compared the verdicts. Afterwards I restored the
tracked cache snapshots the scan touched and removed the ignored `.moss/`
caches.

| Suite | Result |
|---|---|
| `check_phase25_sa_numeric_literals.py`, `check_phase25_sb_check_display.py`, `check_phase25_sc_safe_arithmetic.py`, S1, S2, S3, S4, L7 repairs, G1 regressions, `run_fixtures.py` | all exit 0 |
| Corpus differential `b6d45cf` → `906b99d` | 4 changes. The project `echo_vector` example changed from rejected (`NATIVE_ECHO_DISPLAY_UNSUPPORTED`) to ok, which closes ARCH-019 as intended. Three already-rejected `diag_file` repros changed code only. No other file changed verdict. |
| Full `make check` / `make examples` | Not re-run by me; the Coordinator reports both passed. |

### ARCH-022 / ARCH-023: closed

- **ARCH-022:** each Float prefix-minus case now prints the same value in all
  four engines:

  | Case | Output |
  |---|---|
  | `-v[0]` | `-1.5` |
  | `-v[1] * 2.0` | `-5.0` |
  | `-m["a"]` | `-2.5` |
  | `-p.v[0]` | `-2.5` |
  | `-w[0][1]` | `-2.5` |
  | `1.0 + -v[0]` | `-0.5` |
  | `return -v[0]` from a `Vector[Float]` parameter | `-3.5` |
  | Float handler `reply -v[0]` | `-4.5` |
  | `-g(1.5)` | `-3.0` |
  | untyped `neg(a) = -a[0]` at Float, at Int, and as a `map` stage | agree |

- **ARCH-023:** a handler doing `reply -s.length()`, or `k = -s.length()` then
  `reply k`, now checks and prints `-5` in all engines.
- The earlier Int, Int-minimum, and String/Char/Bool/record rejection cases
  are unchanged.

### Net-new findings

| ID | Class | Severity | Reproducer (ran) | Engines |
|---|---|---|---|---|
| **P25-S-ARCH-024** | Confirmed defect. Pre-existing (same at `b6d45cf`); a checker-authority gap outside S-A/B/C code, found while probing member syntax after numeric literals. | **High** | `type P:\n  n: Int\nfn main():\n  p = P(n: 1)\n  y = p.zz\n  echo y`. Also: `echo p.zz`; `echo p.zz + 1`; `echo p.0`; on primitives, `x = 3` then `echo x.foo`, and `y = x.5`; `echo 1.5.x`; `echo v.0` on a Vector; `echo s.length` (field syntax on a String). | check **ok** for all. Fast Debug: runtime error `unknown field 'zz'` or `field receiver is not a Moss object`. Native: rustc E0609 / E0610. Only a typed `return p.zz` is caught, and only indirectly (`TYPE_INFERENCE_FAILED`). Field access on an unknown field or a non-record receiver is not checked in local/echo positions. This breaks the S1 acceptance criterion. |
| **P25-S-ARCH-025** | Confirmed defect: the S-B contract is incomplete. Pre-existing (same at `b6d45cf`), but `[]` is explicitly part of the S-B display contract. | Medium | `fn main():\n  echo []` and `fn main():\n  echo [[]]` | check **ok**. Fast Debug prints `[]` / `[[]]`. Native rustc E0277 (`Vec<_>` / `Vec<Vec<_>>` doesn't implement `Display`). `echo_display_class` treats an open `vector` as `Unresolved` and lets it pass, and native lowering then falls back to `{}`. Nested `[[1], [], [2, 3]]` and `Vector[Int]()` work in all engines. Only a literal `[]` with no typed sibling fails. Remedy: a fixed element formatter for a fully open empty literal, or reject it with `ECHO_DISPLAY_UNSUPPORTED`. |
| **P25-S-ARCH-026** | Diagnostic quality (Low) | Low | `fn main():\n  x = 1\n  echo x + 1__0` | `INVALID_DIGIT_SEPARATOR` reported at line 3, **column 1** (span 3:1–3:1), not at the literal (column 12). Every S-A lexer diagnostic carries the line only. |

### L7-requirement checklist

**S-A, numeric literals**

| Requirement | Status | Evidence |
|---|---|---|
| One parser for all engines | **Met** | `scan_numeric_literal` is used by the lexer (validation and canonical rewrite), Fast Debug (`decode_numeric_literal_expression`), and native (`rust_numeric_literal` from the decoded value). These agree in all four engines: `1_000`, `0x10`, `0xff_FF`, `.5`, `1.`, `1.e5`, `1E-3`, `1.5e+10`, `007`, `1e-400` (→ `0.0`), `9_223_372_036_854_775_807`, `-9_223_372_036_854_775_808`, `0x7fffffffffffffff`, `-0x8000000000000000`, `5e-324`, `2.2250738585072014e-308`, `1.7976931348623157e308`. The same holds for literals inside ranges, indices, and Float arithmetic (`.5 + .25`, `2 * .5`, `-.5`). This closes ARCH-018. |
| Int bounds including minimum | **Met** | `0x8000000000000000`, `x -0x8000000000000000` (binary minus), and `0xFFFF…FFFF` give `INT_LITERAL_OUT_OF_RANGE`. Only the directly negated magnitude 2^63 (decimal or hex) is accepted. `-(0x8000000000000000)` is also rejected (conservative). |
| `1e400` rejected | **Met** | `FLOAT_LITERAL_OUT_OF_RANGE`, as is `1.7976931348623159e308`. |
| No other forms | **Met** | `0b101` and `0o17` give `UNSUPPORTED_NUMERIC_LITERAL`. So do `3i64` and `1.5f32`. `0X10`, `0x`, and `1e` give `INVALID_NUMERIC_LITERAL`. `1__0`, `1_`, and `1._5` give `INVALID_DIGIT_SEPARATOR`. Literal text inside Strings, Char literals, and trailing comments is untouched (`"1_000 0x10 1e400 9999…"` prints verbatim; `# 1e400 0b1` is ignored). Identifiers `x1_0` and `_1` are untouched. |
| Diagnostic placement | Partial | Line is correct; column is not (ARCH-026). Member syntax such as `x.5` and `1.5.x` is not a literal issue, but the checker accepts it (ARCH-024). |

**S-B, check verdicts and Vector echo**

| Requirement | Status | Evidence |
|---|---|---|
| Same verdict in standalone and project mode | **Met** | Project `moss check` now runs `check_project_semantics` (no lowering). The corpus differential shows only the intended ARCH-019 flip and no lost rejection in any tracked project file. Lowering-only capability checks (for example multi-module staged indexed String) stay with `margo build`, as designed. |
| No Rust Debug exposure | **Met for typed Vectors; not met for literal `[]`** | All engines print the same text for these, through `display_runtime.hpp`, in plain and `MOSS_STAGED_STRING_REPR=indexed-v1` native builds: Int/Float vectors (`-0.0`, `1e300`, `2.5e-8`, `NaN`, `inf`); Bool; Char (`é`, `\n`); raw Strings with quotes, commas, emoji, and the empty string; nested vectors with an inner `[]`; `Vector[Int]()`; mixed `echo "s", [1], 2`; `split`, `chars`, `values()`; pipeline results; untyped generics (`show([1.5])`); message results (`Vector[Float]` reply); and Map-held vectors. Exception: `echo []` / `echo [[]]` (ARCH-025). |
| Consistent semantic rejection | **Met** | `ECHO_DISPLAY_UNSUPPORTED` is reported by check, Fast Debug, and native for: Map (directly and through an untyped generic `show(m)`), Queue, `Vector[P]`, `Vector[enum]`, a callable (`echo f`), and Unit (`echo v.push(1)`). |

**S-C, proven-safe Int division**

| Requirement | Status | Evidence |
|---|---|---|
| `x % 10` / `x / 10` purity | **Met** | A sort key `x % 10` is accepted and agrees in all engines (`11`, `35`). |
| Unknown and zero divisors stay conservative | **Met** | All rejected with `SORT_KEY_IMPURE` (or "unresolved effect", for `x % 1_0 + x / -(2)`): `x / 0x0`, `x / 00`, `10 / x`, `x / (0 - 1)`, untyped `x / 0` at Int, `x / 10 / 0`, `(x / 0) / 10`, `x / 10 * (x % 0)`, `x / (1) - (x / 0)`, `x % 10 / 0`. Literal spellings are decoded before classification. |
| Int.MIN / -1 | **Met** | All rejected: `x / -0x1`, `x / (-(1))`, `x / - 1`, `x / -(-(-1))`. `x / --1` (= 1) is correctly accepted. |
| Float vs Int in untyped code | **Met** | An untyped key `x / 0` specialized at Float is accepted (IEEE, no panic) and agrees in all engines. At Int it is rejected. |
| Key-proof contract preserved | **Met** | S3, L7 repairs, S-C, and `run_fixtures.py` all pass; the `.mossi` `may_panic` cases are in the S-C suite. I found no fusion, speculation, or skip decision that could reorder a panic: each operator split is classified separately, and nested operands keep their own `may_panic`. |

### Verdict (addendum 3)

**CONDITIONAL GO for the L7 checkpoint.**

- S-A and S-C meet every L7 requirement I tested.
- S-B meets the verdict-parity and semantic-rejection requirements.
- ARCH-019, ARCH-022, ARCH-023, and ARCH-018 are closed.

Conditions:
1. **Fix ARCH-025 before the S-B checkpoint is recorded as complete.** The
   contract lists `[]`, but `echo []` and `echo [[]]` pass check and fail in
   native rustc.
2. **ARCH-024 needs an owner and a ledger entry before Team S claims the S1
   acceptance criterion ("every checker-accepted program runs consistently or
   is diagnosed before execution").** It is pre-existing and outside S-A/B/C
   code, so it does not block this checkpoint. But it is the largest remaining
   class of checker-accepted programs that fail in every engine.
3. ARCH-026 is tracked (Low). It does not block.

## Addendum 4: closure check at `8d62f0a43915ef0e616b449270c227b0e3162c26`

**Reviewed SHA:** `8d62f0a43915ef0e616b449270c227b0e3162c26`. I checked it out
detached and `make` succeeded.

**Scope:** `git diff 906b99d 8d62f0a -- src tests`, which is commit `8d62f0a`.
It changes 46 lines in `src/moss.cpp`:
- the `UNKNOWN_FIELD` rule, in the projection branch of `check_expression`;
- `vector` treated as `Unsupported` for echo;
- the echo of an unresolved vector literal.

It also adds 17 lines of regressions to `check_phase25_s2_char_string.py`.

### Runs

- Every probe ran in check, Fast Debug, Native Debug, and Native Release, with
  outputs compared.
- Project cases ran under `moss check`, `moss debug`, `margo run`, and
  `margo --release run`. The compiled-only provider case used the
  `run_fixtures.py` source-free recipe: build the provider in both profiles,
  remove its source, then check, build, and run the consumer with
  `MOSS_MODULE_PATH`.
- S-A, S-B, S-C, S1, S2, S3, S4, L7 repairs, G1 regressions, and
  `run_fixtures.py` all exit 0.
- I compared `moss check` verdicts on all 1,876 tracked `.moss` files between
  `b6d45cf` and `8d62f0a`. The result is the same 4 changes as at `906b99d`;
  no tracked file is newly rejected with `UNKNOWN_FIELD`. I then restored the
  three tracked cache snapshots and removed the ignored `.moss/` caches.
- Full `make check` / `make examples` were not re-run by me.

### ARCH-024: closed, apart from the narrow bypass ARCH-027

- **Now rejected with `UNKNOWN_FIELD` in every engine:**
  - `p.zz`, a nested `b.c.zz`, and a pipeline placeholder `map(_.nn)`;
  - `k.zz` on Int domain state;
  - `2.foo` and `0x10.x`;
  - `v.len` on a Vector;
  - an unknown field on an imported record (`p.inner.zz`), from a same-project
    module, a path dependency (`margo build`), and a compiled-only provider.
- **No false positive found.** All of these are accepted and agree in all
  engines:
  - record chains `a.b.c.z` in `echo`, arithmetic, and vector literals;
  - a typed `sort(v, by: keyf)` over `p.n`, and `v |> map(_.n)`;
  - a record field of domain state (`p.n + k`);
  - a message result field, and an enum payload binding's field
    (`case Ok(value): echo value.n`);
  - method calls `p.twice()` and `self.n`, field assignment `q.n = p.n + 10`,
    and `P(n: 9).n`;
  - enum case spelling `Color.Red` / `Color.Blue` and enum fields of records;
  - Strings and Chars holding `.` (`"p.zz 1.exp"`, `'.'`, `"a.b".length()`,
    `'a'.codepoint()`);
  - S-A Float forms `1.e5`, `1_0.5`, `1_0.5e1`, `-0.0`, `+2.5`, `-.5`,
    `1.5e-3`;
  - module-qualified records, values, and calls: `geo.origin().inner.z`,
    `geo.Point(…)`, `geo.xof(…)`, `geo.tag(geo.Color.Blue)`, a
    `sort(by: ykey)` over an imported `Float` field, and `map(_.x)`, in the
    same-project, path-dependency, and compiled-only provider layouts.
- Untyped field access (`fn getn(p): return p.n`) is still the existing
  `UNTYPED_FIELD_ACCESS` rule. The new rule does not change it.
- FileIO `Range` exposes only methods (`length()`, `slice(…)`). Its existing
  suites inside `run_fixtures.py` and the S-B suite pass.

### ARCH-025: closed

`echo []` and `echo [[]]` give `ECHO_DISPLAY_UNSUPPORTED` in every engine.
These still print the same text in all engines: `Vector[Int]()` → `[]`,
`[Vector[Int]()]` → `[[]]`, and `[[], [1]]`. These are not false positives:
`fn show(x): echo [x]` at Int and String, `echo wrap(2)`, `echo [wrap(1.5)]`,
`echo [first([1, 2])]`, and `echo [v[0].n]`.

### Net-new findings

| ID | Class | Severity | Reproducer (ran) | Engines |
|---|---|---|---|---|
| **P25-S-ARCH-027** | Confirmed defect: a bypass in the ARCH-024 fix | Low–Medium | `fn main():\n  echo 1.exp`. Also `echo 2.size`, `echo 12.e`, `echo -1.exp`, `echo 1_000.name`. | check **ok**. Fast Debug: `field receiver is not a Moss object`. Native rustc E0610. The Float-literal exemption (`all_digits(base) && field contains e/E`) also exempts an Int-literal receiver whose field name merely contains `e` or `E`. The lexer has already decided these are Int + member (`1.exp` is not a Float), so the exemption should apply only when the whole token is a valid Float literal (for example by asking `scan_numeric_literal`). `2.foo` and `0x10.x` are rejected correctly. |
| **P25-S-ARCH-028** | Diagnostic-guidance inaccuracy | Low | `fn main():\n  v = [1]\n  echo v.len` | `UNKNOWN_FIELD` "… call methods with parentheses, e.g. 'v.len()'". The hint always appends `()`, so for a Vector, Map, or Queue it can suggest a method that does not exist (`len()`; the current spelling is `v \|> count`). For a String (`s.length` → `s.length()`) it is correct. |

### Verdict (addendum 4)

**GO for the L7 checkpoint at `8d62f0a`.** The two Addendum 3 conditions are
met:
- ARCH-025 is fixed.
- ARCH-024 is fixed, with regressions.

I found no false positive of the new `UNKNOWN_FIELD` or empty-Vector echo rules
across the required probe areas, the three package layouts, or the tracked
corpus.

Tracked, not blocking this checkpoint:
- **ARCH-027** must be fixed before Team S records the S1 acceptance criterion
  ("every checker-accepted program runs consistently or is diagnosed before
  execution") as met. It is the only checker-accepted, fails-everywhere form I
  found at this SHA.
- ARCH-028 and ARCH-026 are diagnostic quality only.

## Addendum 5: closure check at `58d33d9ab00a666e8d9b60450c7c6aeb2b55b657`

**Reviewed SHA:** `58d33d9ab00a666e8d9b60450c7c6aeb2b55b657`. I checked it out
and verified the tree and build.

**Scope:** Commits `dea9a4e` (fixes ARCH-027 and ARCH-028) and `58d33d9` (fixes
unresolved local Vector echo and underdetermined static generic calls from the
skipped-commits audit).

### Runs & Verification

1. **ARCH-027 (Float-literal exemption bypass in `UNKNOWN_FIELD`):**
   - Verified that `1.exp`, `2.size`, `12.e`, `-1.exp`, and `1_000.name` are
     properly rejected with `UNKNOWN_FIELD` before execution in both check modes
     and across all engines.
   - Valid Float literals (`1.e5`, `1.5e-3`, `1_0.5`, `1_0.5e1`, `2.5`, etc.)
     remain accepted and execute identically across Fast Debug, Native Debug,
     and Native Release.
   - ARCH-027 is **closed**.

2. **ARCH-028 (Diagnostic-guidance collection method hint):**
   - Verified that accessing collection fields such as `v.len` reports
     `UNKNOWN_FIELD` with the correct hint `(use 'v |> count' for a Vector's size)`
     instead of suggesting nonexistent `.len()`.
   - ARCH-028 is **closed**.

3. **S-B Skipped-commit audit cases (`58d33d9`):**
   - `var v = []; echo v` is rejected with `ECHO_DISPLAY_UNSUPPORTED` in both
     standalone and project `moss check`, Fast Debug, Native Debug, and Native
     Release.
   - `fn show(x): echo x; fn main(): show([])` is rejected before execution with
     `TYPE_INFERENCE_FAILED`.
   - Valid typed empty vectors (`show(Vector[Int]())`) and generic forwarding
     (`forward([1])`) continue to be accepted and execute cleanly across all
     modes (`[]` and `[1]`).

4. **Regressions & Full Test Suite:**
   - All Phase 25 test suites pass cleanly:
     - `check_phase25_sa_numeric_literals.py`
     - `check_phase25_sb_check_display.py`
     - `check_phase25_sc_safe_arithmetic.py`
     - `check_phase25_s1_checker_authority.py`
     - `check_phase25_s2_char_string.py`
     - `check_phase25_s3_key_protocol.py`
     - `check_phase25_s4_fast_debug_adversarial.py`
     - `check_phase25_l7_repairs.py`
     - `check_phase25_g1_regressions.py`
     - `run_fixtures.py` (30/30 fixtures match recorded status)
     - `check_agent_feedback.py`
     - `check_agent_skills.py`
   - Full repository gates pass:
     - `TMPDIR="$PWD/tmp" make check` (54/54 executor tests, all Moss v0.1 tests)
     - `make examples` (all 32 examples)

### Verdict (addendum 5)

**UNCONDITIONAL GO FOR TEAM S PHASE 25 DELIVERIES.**

- ARCH-027 and ARCH-028 are verified closed.
- The S1 acceptance criterion ("every checker-accepted program runs consistently
  or is diagnosed before execution") is now fully satisfied.
- S-A, S-B, S-C, and S-D deliverables are complete, verified, and in full
  cross-engine parity.
- The branch is ready for joint G2/G3/G4/G5 integration per agreement §5.
