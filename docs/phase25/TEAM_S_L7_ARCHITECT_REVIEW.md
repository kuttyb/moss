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
