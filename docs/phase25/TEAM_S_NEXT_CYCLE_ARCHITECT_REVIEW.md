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
