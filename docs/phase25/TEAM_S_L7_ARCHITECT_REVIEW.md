# Phase 25 Team S: Independent L6 Architect Review of the L7 Repair Pass

- **Role:** Independent L6 Architect, Team S. I did not write the code under review.
- **Date:** 2026-10-15
- **Reviewed implementation SHA:** `5920e385b3c3805a48ec89fafa4ebb4b041a9e70`
  (branch `phase25-semantics-engines`; `HEAD` equalled this SHA when the review ran)
- **Repair base (L7 NO-GO):** `983acb2135d2aea6f5aecc9aa35005525c2f40f4`
- **Baseline:** `fa3a340a0a6652568b97869489da1a84038b9857`
- **Team G contracts consulted:** `origin/phase25-generic-types` @ `97b7d42`
  (`TYPE_IR_CONTRACT.md` §8 and `ABI_V10_DESIGN.md` §3.5)
- **Toolchain:** `moss-0.1` (from `./moss agent bootstrap --json`), rustc 1.98.1

## Scope

I reviewed the diff `983acb2..5920e38` (9 commits, 36 files). I checked the claims in
`TEAM_S_L7_REPAIR_REPORT.md` and `TEAM_S_UNICODE_STRING_BENCHMARK.md` against the code
and against my own three-engine probes. I read the key code:

- `src/interpreter.hpp`: `BlockScope`, `declare_local`, `block_binders`/`joined_binders`,
  `execute_block`, `record_edge_locals`, the `for` loop, and the escape-aware scanners;
- `src/char_string_ordering_runtime.hpp`: the plain and indexed-v1 String runtimes;
- `src/moss.cpp`: `canonicalize_char_literals`, `named_argument_label` and the module
  rewriter, `check_sort_call`, `sort_call_rust`, `scalar_protocol_rust`,
  `check_scalar_protocol_call`, `reject_fast_debug_fileio`, and `indexed_strings_staged`
  with its uses.

I wrote no source, test, or SDK file. All probes are under `tmp/arch/`.

## Commands run and outcomes

| command | outcome |
|---|---|
| `git rev-parse HEAD 5920e38` | both `5920e385…0e70` |
| `./moss agent bootstrap --json` | `language_version` = `moss-0.1` |
| `make` | nothing to rebuild; `./moss` is byte-identical (`cmp`) to `tmp/wt-ckpt6/moss`, which was built from a clean 5920e38 worktree |
| `python3 tests/tooling/check_phase25_l7_repairs.py ./moss` | **pass** (exit 0, 26 s) |
| `python3 tests/tooling/check_phase25_indexed_string.py ./moss` | **pass** (exit 0) |
| `python3 tests/phase25/ledger/run_ledger.py repaired=./moss` | **pass** for every row except #12 (open receiver), which is still rejected as the joint Team G G2 item |
| `python3 tests/phase25/ledger/run_ledger.py baseline=tmp/wt-base/moss reviewed=tmp/wt-rev/moss` | matches the report's baseline and reviewed columns exactly, including #10 still failing at 983acb2 |
| `git diff fa3a340 5920e38 -- <5 ABI-v9 migration tests>` | empty, so the tests are restored byte for byte |
| Adversarial probes, about 45 programs (`tmp/arch/run3.sh`: check, Fast Debug, native debug, native release; String probes also with `MOSS_STAGED_STRING_REPR=indexed-v1`) | results below |
| `make check` | **not re-run.** I relied on `tmp/wt-ckpt6/tmp/check9.log`: worktree at 5920e38, `git status` clean, ends `all Moss v0.1 tests passed` / `EXIT 0`, and contains no `FAIL` lines. That log predates my findings, so it shows only that the existing suite passes |
| `phase25_unicode_traversal.py` | **not re-run** (about 70 s for the plain 1M case). I checked the method and ran my own indexed-vs-plain differential at the 64-code-point stride boundaries instead |

## Per-item status (L7 items 1–7)

| # | L7 item | status | evidence |
|---|---|---|---|
| 1 | IEEE Float `==`/`!=` separate from `isequal`, with NaN/±0 regressions | **Repaired** | `interpreter.hpp equal()` uses IEEE `==`. `key_equal()` (total) is used only for Map identity. My probe `f1.moss` (26 checks: `==`, `!=`, `<`, `>=`, `less`, `isequal`, `hash`, `sort` with NaN/−0.0/+0.0, Vector equality) agrees in all three engines with the expected values. `assertEqual(NaN, NaN)` fails in every engine |
| 2 | Real indexed String; linear Unicode traversal; metadata cost | **Partially delivered** | `MossString` (indexed-v1) is correct in my review of `push_str`/`byte_of`/`codepoint_of` offset maintenance (ASCII→non-ASCII transition, stride boundaries, `index == codepoints`). Probe `st1.moss` exercises `char_at`/`slice`/`find`/`append`/`chars`/`split`/`join`/Map keys/sort across 64-code-point boundaries and agrees for plain, indexed, and Fast Debug. The benchmark method (build-only vs build+scan medians, runtime-built input, printed result) supports "linear" at 100K/1M. However, **default builds are still quadratic** (`plain` `char_at` = `chars().nth`). Indexed-v1 is env-staged and single-crate only (`STRING_REPRESENTATION_STAGED_SINGLE_CRATE` confirmed on a 2-package build). See the assessment below |
| 3 | Canonical `MossMap<K,V>` / key-protocol-v1; no competing adapter | **Repaired (pending Team G confirmation)** | `MossFloatKey`, the `MossLess`/`MossIsequal`/`MossHash` traits, and `MossKey_T` are gone (grep). `scalar_protocol_rust` emits direct per-type calls with no trait or `dyn`. `MossString`'s std `Eq`/`Hash`/`Ord` are the built-in String record's own semantics, not a key adapter. `Map[Float,V]` fails closed (index/assign: `FLOAT_MAP_KEY_UNSUPPORTED`; declared positions: `UNKNOWN_SYMBOL_OR_TYPE`, see the inaccuracies). It is consistent with Team G §8 ("compiler-owned map runtime"; "Float receives no key proof"). Team G's written confirmation of the scalar targets is still outstanding |
| 4 | Single coordinated ABI-v10; no solo `kNativeAbiVersion` change | **Repaired** | `kNativeAbiVersion = 9`. The false `ABI_VERSION_MISMATCH` text is removed. The ABI-v9 tests are byte-identical to the baseline. The staged String record is gated by `.mossi` header plus backend fingerprint, and plain/staged mixing fails closed. The ABI-v10 contribution is a proposal only, with no unilateral change |
| 5 | Fast Debug range overflow, iteration caps, loop scoping, remaining parity | **Substantially repaired; parity gaps remain** | Ranges near `Int` max/min and step > Int range (`r1.moss`), non-literal/negative step rejection, and the cap removal all pass. Scoping/joins (`s1`–`s4`): nested if/else, if-in-loop joins, try/recover, match, shadow restore, diverging branches rejected consistently. `on_fail` capture under shadowing (`o1`, `o2`) and consumed reinit through `match consume`/`replace` in loops (`c1`) all agree. Char delimiter literals and escaped quotes (`ch1`, `esc`) agree. **Remaining divergences:** ARCH-003 (`Char` user function), ARCH-002 (user `sort`, native side), ARCH-006 (FileIO preflight misses `on_fail`), and pre-existing ARCH-008 (String escapes) |
| 6 | Re-run the original ledger reproducers (#4 parameter form, #10) | **Repaired, with a residual hole in #10** | The ledger matrix reproduces exactly. #4 passes in parameter form. #10 passes, and my package probe (labels next to same-named functions; label values that *are* module-function calls, `row_count = row_count(t0)`, `width:width(...)`, enum payload `radius: radius(n)`) runs correctly in `margo run` and `margo debug`. However, named arguments at statement-call level are rewritten one argument at a time, so the label fix does not apply there (ARCH-005) |
| 7 | Purity, ordering-law, source-free, Unicode-conversion, performance gates | **Partially delivered** | Purity: I/O, raise, divergence, and possible panic (`/`, `char_at`) are rejected; nested impurity is found transitively (`pk1`, `pk5`, `pk6`). But the purity gate admits keys that native cannot lower (ARCH-001). Ordering laws and source-free: regression script passes. Unicode conversion: `Char(cp)` passes, but String-literal escape parity is broken (ARCH-008, pre-existing). Performance: met only under the staged representation |

**G0 boundary:** respected. I found no runtime trait dispatch, `dyn`, or vtables. There
are no source-level generic parameters (`Reverse` is a checked compile-time key
wrapper), and no Phase 25.5 D1/D2 work.

### Assessment of item 2 staging

The justification is **sound in substance**:

1. L7 item 4 forbids a solo layout change, and flipping `MossStr` changes the source-free
   boundary.
2. Each generated crate embeds its own prelude, so a per-crate `MossString` is a
   distinct Rust type. The fail-closed single-crate guard is the honest interim.
3. Team G's TYPE_IR §8 already defines a compiler-owned runtime for `MossMap`.
   Co-locating `MossString` there is the non-competing choice.

It still does **not** complete G4 as L7 worded it. Shipped programs keep the quadratic
plain path until the joint cut. Whether "implemented and proven, staged" satisfies
item 2 is L7's decision. The +32 bytes per String value (56 vs 24) is measured and
disclosed correctly.

## Net-new defects

No L7 finding is restated here. "Introduced" means the behaviour differs from 983acb2.

| ID | severity | reproducer | engines affected | status |
|---|---|---|---|---|
| P25-S-ARCH-001 | **High** | `type P:\n  name: String\n  age: Int\nfn name_of(p: P) -> String:\n  return p.name\nfn main():\n  var v = [P(name: "b", age: 1), P(name: "a", age: 2)]\n  sort(v, by: name_of)\n  echo v[0].name`. The same happens with `fn ident(s: String) -> String: return s` over `Vector[String]`. `moss check` ok; Fast Debug prints `a`; native fails with rustc E0308 (`expected P, found &P`). `sort_call_rust` passes `&T` to a key whose parameter is CONSUME. | native debug and release | Confirmed; present since 983acb2 (where `sort` was introduced); not raised by L7 |
| P25-S-ARCH-002 | Medium | `fn sort(v: Vector[Int]) -> Int:\n  return 7\nfn main():\n  var v = [3, 1, 2]\n  echo sort(v)\n  echo v[0]`. check ok; Fast Debug prints `7`,`3`; native fails with rustc E0277 because the builtin sort is lowered (`sort_call_rust` paths lack the `!functions_.count("sort")` guard that the checker and interpreter have) | native | Confirmed; **regression** (at 983acb2 native ran the user function correctly) |
| P25-S-ARCH-003 | Medium | `fn Char(x: Int) -> Int:\n  return x + 1\nfn main():\n  echo Char(65)`. Fast Debug prints `66`; native prints `A` (native `Char` lowering has no user-function guard) | Fast Debug vs native divergence | Confirmed; **introduced** (both engines printed `A` at 983acb2) |
| P25-S-ARCH-004 | Low | `fn Reverse(x: Int) -> Int:\n  return 0 - x\nfn main():\n  echo Reverse(5)`. Rejected `SORT_REVERSE_POSITION`: the checker claims the name even when a user function exists | check (all) | Confirmed |
| P25-S-ARCH-005 | Medium | Package `net` with any module function `fn by(x: Int) -> Int` plus `sort(v, by: key)` in the same module. `margo run` and `margo debug` both fail with `SORT_SIGNATURE`: statement-call arguments are rewritten individually, so `named_argument_label` (which needs a preceding `(`/`,`) does not see the label and `by` becomes `net__by` | check (packages only) | Confirmed; same class as ledger #10; single-file programs unaffected |
| P25-S-ARCH-006 | Low–Medium | A domain handler `on_fail Failure.Bad:\n    reply probe_disk()` where `probe_disk` uses `FileIO.open` (`tmp/arch/p/fio2.moss`). Fast Debug prints `before`, then fails mid-run with the FileIO error. `reject_fast_debug_fileio` scans `handler.body` but not `failure_arms` (or `Method::result_expression`) | Fast Debug | Confirmed; breaks the "before any statement runs" guarantee of G0 proof 3 |
| P25-S-ARCH-007 | Low | `fn main():\n  echo "FileIO is not used here"`. Rejected `FAST_DEBUG_FILEIO` (at `moss:0`) because the preflight tokenizes string-literal content | Fast Debug | Confirmed; a false positive beyond the "same-named identifier" limitation the report discloses |
| P25-S-ARCH-008 | Medium (parity) | `echo "\u{E9}" == "é"` gives Fast Debug `false`, native `true`. `echo "\x41\|\0\|\r".length()` gives Fast Debug `7`, native `5`. The checker accepts these escapes and native passes them to Rust | Fast Debug vs native divergence | Confirmed; **pre-existing** (same at fa3a340 and 983acb2); open under L7 item 5 "remaining parity" |

Concerns (unconfirmed or minor, not blocking):

- `margo test --interp` skips the FileIO preflight (`test_mode`), so the mid-run failure
  mode remains for tests.
- An unrecognised `MOSS_STAGED_STRING_REPR` value (for example `bogus`) silently selects
  plain. A fail-closed diagnostic would be safer.
- `var m = Map()` followed only by `m.get(k, d)` passes check and fails in rustc for both
  Int and Float keys. This is pre-existing and not Float-specific.
- Purity is conservative: keys using `/` or `char_at` are rejected as "possible panic".
  This is acceptable, but it limits `sort(v, by: first_char)`-style keys.

## Inaccurate or incomplete claims in the Team S documents

1. `TEAM_S_L7_REPAIR_REPORT.md` §Validation contains the literal placeholder
   `VALIDATION_SECTION`. The header promises a "Submission SHA … recorded in the final
   section", but none is recorded.
2. "Map[Float, V] … rejected with `FLOAT_MAP_KEY_UNSUPPORTED`, in index, assignment, and
   declared-type positions". Declared positions (field, parameter, return, domain state,
   `Map[Float,Int]()`, `Vector[Map[Float,Int]]()`) fail with `UNKNOWN_SYMBOL_OR_TYPE` or
   `INVALID_TYPED_VECTOR_CONSTRUCTOR`. They still fail closed, but not with the stated
   code.
3. "a user function named `hash`/`less`/`isequal` is never hijacked". This is true for
   those three, but `sort` (native), `Char` (native), and `Reverse` (checker) are still
   hijacked (ARCH-002/003/004).
4. The FileIO preflight is described as acting "before any statement runs". This does
   not hold for FileIO reachable only through `on_fail` arms (ARCH-006).
5. `TEAM_S_UNICODE_STRING_BENCHMARK.md` states that the fragment is "25-code-point /
   40-byte". It is 30 code points / 41 bytes; the raw JSON correctly records
   `fragment_codepoints: 30`, and the 999,990 rows are 33,333 × 30.
6. The benchmark says "later commits change Fast Debug and documentation only". In fact
   d0e1765/6a51cf4 also add the backend-fingerprint, mixing, and single-crate guards in
   `moss.cpp`. String lowering is unchanged, so the measurements stand.

## Verdict

**CONDITIONAL GO for L7 re-review**, attributable to the Independent L6 Architect, Team S,
for implementation SHA `5920e385b3c3805a48ec89fafa4ebb4b041a9e70`.

The architectural corrections L7 demanded are made:

- the competing key-adapter layer is retracted;
- the solo ABI bump is reverted, and the v9 tests are restored;
- IEEE equality is correct;
- the ledger reproduces;
- the indexed String record is real and linear under staging.

The branch is ready for L7 re-review **only after** these conditions are met:

1. Fix ARCH-001, ARCH-002, and ARCH-003, each with a three-engine regression (check,
   Fast Debug, native debug, native release). They are checked programs that fail rustc
   or diverge, and two of them were introduced in this pass.
2. Fix ARCH-005 (statement-level named-argument labels in the module rewriter), with a
   package regression.
3. Make the FileIO preflight scan `on_fail` arms (ARCH-006), or document the gap
   explicitly as a limitation.
4. Fill in the report's Validation section and Submission SHA, and correct inaccuracies
   2–5 above.

ARCH-004, ARCH-007, and the pre-existing ARCH-008 should be listed as known limitations
if they are not fixed. The decision on whether staged indexed-v1 satisfies item 2, and
on the ABI-v10 cut, belongs to L7 together with Team G. **This verdict concerns only
readiness for L7 re-review; L7 alone decides G5 integration.**

---

## Addendum: re-review of the Architect-findings repair (2026-10-15)

- **Role:** Independent L6 Architect, Team S (the same reviewer as above; the text
  above is unchanged).
- **Reviewed implementation SHA:** `6ee8fee312f4b99eafc447cff189a0ead59d52c0`
  (`HEAD` of `phase25-semantics-engines`). The diff from `5920e38` is one commit,
  `6ee8fee`, touching `src/interpreter.hpp`, `src/moss.cpp`, the L7 regression
  script, the benchmark script, and the docs. This review document was committed in
  6ee8fee byte-identical to what I wrote.
- **Binary:** `./moss` was built at 02:54, after the last `src/` edit (02:53), and
  `make` reported nothing to rebuild. `tmp/wt-ckpt6` is at 6ee8fee, but its `moss`
  binary was still the 5920e38 build when I looked.

### Commands and outcomes

| command | outcome |
|---|---|
| `python3 tests/tooling/check_phase25_l7_repairs.py ./moss` | **pass**, including `test_architect_findings` (001, 002–004, 005, 006/007, 008) |
| `python3 tests/tooling/check_phase25_indexed_string.py ./moss` | **pass** |
| `python3 tests/phase25/ledger/run_ledger.py repaired=./moss` | unchanged: all pass except #12 (joint G2) |
| My original probes `tmp/arch/p/{pk9,pk11,pk12,h2,h3,h4,esc,ue,xe,f1,s1,c1,r1,so1,st1,ch1,sd,o1,o2,hs}.moss` through `run3.sh` | all **AGREE** across Fast Debug, native debug, and native release with correct values. `st1` and `pk12` also agree under `MOSS_STAGED_STRING_REPR=indexed-v1`. `h4` (user `Reverse`) prints `-5` |
| `fio1.moss` / `fio2.moss` (Fast Debug) | `fio1` runs; `fio2` is rejected before output with `FAST_DEBUG_FILEIO` at `main`'s line |
| `tmp/arch/pkg` with a module `fn by` plus `sort(v, by: negk)` (`margo run` / `margo debug`) | both print `5 205 8 321` |
| New adversarial probes (below) | three net-new defects |
| `make check` / `make examples` for 6ee8fee | **not relied on.** `tmp/wt-ckpt6/tmp/check10.log` was still running (no `EXIT` line) and `examples10.log` did not exist when I looked |

### Adversarial probes of the new fixes

- **Leading-label rule (005).** In package `tmp/arch/pkg2`, a module defines
  functions `by` and `total` beside a type with fields `by` and `total`. It uses
  module-function calls as label values, `Box(by = by(4), total = total(...))`,
  nested `by(by(0))`, `assert(by(0) == 1)`, `assertEqual(total(...), 4)`,
  try/recover on a module enum with payload, and domain state updated with
  `count + by(step)`. `margo run` and `margo debug` agree (`217002`, `52`, `5`).
  I found no case where a real value is skipped. The rule needs a leading
  identifier followed by `:` or a lone `=`, which is not valid as a value
  expression. (Named message arguments `message c.Bump(step: 2)` pass check but
  fail in Fast Debug and rustc. This is pre-existing at fa3a340 and unrelated.)
- **Arity mismatch with user functions.** User `sort(a, b)`, `Char(a, b)`, and
  `hash(a, b)` called with the builtin arity give consistent arity or type errors
  at check, so the user function wins everywhere. User `Reverse`, with one or two
  parameters, is still the builtin key wrapper inside `sort(v, by: Reverse(k))`.
  All engines agree on that. It is a coherent rule, but the report should state it.
- **Consuming keys with nested non-Copy types** (`ck2.moss`):
  - a struct holding a struct, a `Vector[Struct]`, and a `Map`;
  - an enum key using `match consume`;
  - `Vector[Vector[String]]` with a key that moves and pushes;
  - `Reverse` over a consuming key.

  All agree across the three engines, and a typed WRITE key is
  `SORT_KEY_IMPURE`, including under `Reverse`. **An untyped consuming key
  fails** (ARCH-009).
- **Escape edge cases.** The checker accepts every malformed escape I tried. See
  ARCH-010 and ARCH-011.

### Net-new defects

| ID | severity | reproducer | engines affected | status |
|---|---|---|---|---|
| P25-S-ARCH-009 | Medium | `fn loose(x) -> String:\n  return x\nfn main():\n  var ws = ["b", "c", "a"]\n  sort(ws, by: loose)\n  echo ws[0]`; the same with `by: Reverse(loose)`. check ok, Fast Debug prints `a`, native fails with rustc E0308 (`__moss_specialize_loose_0` expects `MossStr`, found `&String`). The clone decision reads the *unspecialized* function's `parameter_effects`, not the specialization's | native debug and release | Confirmed; the 001 fix is incomplete for specialized (untyped-parameter) keys |
| P25-S-ARCH-010 | Medium | Malformed String escapes pass `moss check`: `"\x80"`, `"\xZZ"`, `"\x4"`, `"\u{110000}"`, `"\u{D800}"`, `"\u{}"`, `"\u{1F600"`, `"\q"`, `"\u{0000041}"`, `"\u{zz}"`. Native: rustc error in every case. Fast Debug: runs with invented values (`"\u{110000}"` length 0, `"\u{D800}"` length 1 with an invalid internal encoding, `"\xZZ"` length 3) or fails with a raw internal `moss: error: stoul` (`\u{}`, `\u{zz}`, overlong). Char literals behave the same: `'\u{D800}'` and `'\u{110000}'` pass check, Fast Debug yields Char 55296 or 1114112 (non-scalar values), and rustc rejects them | check, Fast Debug, native | Confirmed. The checker gap is pre-existing; the `stoul` leak and the surrogate values come from the 008 decoder and 983acb2's Char work. The checker should reject these with a structured diagnostic |
| P25-S-ARCH-011 | Medium | `fn main():\n  let c = '\x41'\n  echo c.codepoint()`. Fast Debug prints `92`; native prints `65`. `canonicalize_char_literals` does not handle `\x`, and Fast Debug reads the backslash. `'\q'` also gives Fast Debug `92` while rustc rejects it | Fast Debug vs native divergence (valid program) | Confirmed; present since 983acb2 (baseline Fast Debug rejected the literal) |

### Status of my conditions

| condition | status |
|---|---|
| 1. Fix ARCH-001/002/003 with three-engine regressions | **Met for 002/003. 001 is met for typed keys only.** ARCH-009 shows the remaining hole |
| 2. Fix ARCH-005 with a package regression | **Met** (the package regression runs in three modes; my broader package probe agrees) |
| 3. FileIO preflight covers `on_fail` arms (ARCH-006) | **Met.** Methods' `result_expression` is still not scanned, and test mode is still exempt; both are now disclosed |
| 4. Fill the Validation section and Submission SHA; correct inaccuracies 2–5 | **Partly met.** Inaccuracies 2–5 are corrected. `TEAM_S_L7_REPAIR_REPORT.md` still has the literal `VALIDATION_SECTION` placeholder (§Validation) and no Submission SHA. Its new "Independent Architect review" section says the missing validation section was corrected, which is not yet true |

ARCH-004, 007, and 008 were fixed rather than listed as limitations.

### Verdict (addendum)

**CONDITIONAL GO for L7 re-review**, attributable to the Independent L6 Architect,
Team S, for implementation SHA `6ee8fee312f4b99eafc447cff189a0ead59d52c0`.

All eight earlier findings are addressed in substance, and the fixes hold under my
probes, with the exceptions below. Before the branch goes to L7:

1. Fix ARCH-009 by deciding clone-vs-borrow from the key's concrete specialization
   effects, with a three-engine regression for an untyped consuming key.
2. Fix ARCH-011 by canonicalizing or decoding `'\xHH'` Char literals identically in
   all engines, with a regression.
3. Fill §Validation with the finished `make check` and `make examples` results
   (`EXIT 0` from `check10.log`/`examples10.log`, or later logs) and record the
   Submission SHA. Remove the claim that this was already done.
4. ARCH-010: preferably the checker rejects malformed String and Char escapes
   (invalid `\x`, out-of-range or surrogate or empty or overlong `\u{}`, unknown
   escapes). At minimum, Fast Debug must not leak a raw `stoul` failure or create
   non-scalar Char/String contents, and the gap must be listed as a known
   limitation.

The decisions on item 2 (staged indexed String) and on the ABI-v10 cut remain with
L7 and Team G. **L7 alone decides G5 integration; this verdict concerns only
readiness for L7 re-review.**
