# Phase 25 Team S — L7 parallel-execution cycle report (S-A, S-B, S-C, S-D)

**Branch:** `phase25-semantics-engines` (pushed)
**L7-reviewed starting checkpoint:** `c2e106c0da3debde6b0334c23d45635f2b498cd6`
**Team G checkpoint reviewed:** `a50fa30c8420538534471c4404afb0c645ae9b0c`
**Architect-reviewed consolidated SHA:** `906b99d4068d1c2bbc92548249d8abb24eaea11f` (CONDITIONAL GO)
**Architect GO SHA:** `8d62f0a43915ef0e616b449270c227b0e3162c26` (**GO for the L7 checkpoint**, Addendum 4)
**Delivered implementation SHA:** `dea9a4e21f9925e6d69943960eee0239460f2a3b` (post-GO fix of ARCH-027/028; not Architect-reviewed)

Nothing was merged into `main` or `phase25-integration`, and there was no
ABI-v10 switch.

## Summary

| Item | Result |
|---|---|
| S-A numeric literals | `1_000`, `0x10` and the existing Float forms go through **one shared parser** (`src/numeric_literal.hpp`), canonicalized in the lexer and emitted as `16_i64` / `2.5_f64`. Int bounds are signed 64-bit; only a directly negated 2^63 (decimal or hex) is accepted. `1e400` gives `FLOAT_LITERAL_OUT_OF_RANGE`. `0b`, `0o` and type suffixes are rejected. 68 accepted literals agree with a Python oracle in three engines; 63 rejection cases. |
| S-B check and display | Standalone and project `moss check` share one semantic pipeline (`check_project_semantics`; project check no longer lowers). `echo` of a Vector follows a deterministic contract (Int, canonical Float, Bool, raw Char/String, nested Vectors, typed `[]`) through a Moss runtime helper and never Rust `{:?}`. Map, Queue, records, enums, callables, Unit and untyped `[]` give `ECHO_DISPLAY_UNSUPPORTED` in every engine and both check modes. |
| S-C safe arithmetic | Int `/` and `%` by an Int literal other than 0 and -1 is proven non-panicking in the one observable-effect analysis. That analysis feeds `SORT_KEY_IMPURE`, initializer purity, fusion and speculation, and `.mossi` `may_panic`. Unknown, zero and `-1` divisors stay conservative. `Int.MIN / -1` and `% -1` already wrap identically in all engines (no runtime change). Lawful nominal `isequal`/`hash` methods using `% 10` and `/ 10` are pure. |
| S-D Architect | `3577dbc`: ARCH-020/021 closed, net-new 022/023. Consolidated `906b99d`: 018/019/022/023 closed; S-A and S-C meet all L7 requirements; net-new 024/025/026. Closure `8d62f0a`: **GO**; 024/025 closed, no false positives; net-new 027 (fix required before recording S1 acceptance) and 028. 027/028 fixed at `dea9a4e`; 026 tracked. |
| Runtime agreement | L7 decision recorded in `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`, with a Team S review of Team G `a50fa30` and a concurrence checklist. **Team G Coordinator concurrence: pending** (no Team G session was reachable; the request is in the pushed document). |
| Item 6 (deferred) | Reproducer retained (S3 L1); the required effect information is documented; no workaround. |

## Defects

| ID | Origin | Defect | Fix |
|---|---|---|---|
| S-A #1–12 | leaf S-A | Separators, hex, `0b`/`0o`/suffixes, bad separators, malformed hex/exponent, `1e400`, `.5`, hex bounds, Int minimum, exponent signs in Fast Debug, Int `-0` emitted as `-0.0`, `1.e5` (details in the S-A section of the commit history and suite) | merge `9f79e09` |
| ARCH-019, S4-011 (rev.), SB-1..4 | Architect / leaf S-B | Check-mode divergence; Vector echo failed natively; Map, Queue, record, enum, callable and Unit echo printed ad hoc text in Fast Debug | merge `6365b89` |
| S3-L2 (arithmetic) | leaf S3 / S-C | `x % 10` classified as may-panic | merge `83419f6` |
| P25-S-ARCH-022 | Architect at `3577dbc` | `-v[0]` on a Float lowered to `(-v)[0]` (rustc) | `cff9a96` |
| P25-S-ARCH-023 | Architect at `3577dbc` | `reply -s.length()` hit an internal synchronization invariant | `cff9a96` |
| P25-S-ARCH-024 | Architect at `906b99d` (pre-existing) | `p.zz`, `x.foo`, `x.5`, `s.length`, `v.0` passed check and failed in both engines | `8d62f0a` (`UNKNOWN_FIELD`) |
| P25-S-ARCH-025 | Architect at `906b99d` | `echo []` passed check and failed in rustc | `8d62f0a` (`ECHO_DISPLAY_UNSUPPORTED`) |
| P25-S-ARCH-026 | Architect at `906b99d` | S-A lexer diagnostics report column 1 | **tracked** |
| P25-S-ARCH-027 | Architect at `8d62f0a` | `1.exp`, `12.e`, `1_000.name` slipped through the Float-literal exemption | `dea9a4e` (exponent must be `e`, sign, digits) |
| P25-S-ARCH-028 | Architect at `8d62f0a` | Collection hint suggested a nonexistent `.len()` | `dea9a4e` (`\|> count` hint) |

Regressions for 022–025 and 027 are in `check_phase25_s2_char_string.py::test_architect_next_cycle_findings`.

## L7 requirement checklist

- **S-A:**
  - separators — met;
  - hex — met;
  - signed 64-bit bounds including the minimum — met (Moss has no `Int.MIN`
    name; the boundary is the literal `-9223372036854775808` /
    `-0x8000000000000000`);
  - `1e400` rejected before execution — met;
  - one shared parser for checker, Fast Debug and native — met;
  - valid/invalid syntax, boundaries, unary signs, three-engine identity —
    met;
  - no extra literal forms — met.
- **S-B:**
  - identical standalone and project verdicts — met;
  - Vector echo with a deterministic contract — met;
  - no Rust `Debug` exposure — met;
  - consistent semantic rejection of undefined displays — met after
    ARCH-025;
  - standalone, project, Fast Debug, Native Debug and Native Release tests —
    met (plus `moss debug .`, `margo run`, and `margo run --release`).
- **S-C:**
  - `x % 10` / `x / 10` proven safe — met;
  - unknown and zero divisors conservative — met;
  - Int minimum / -1 handled — met (wraps in every engine and stays
    classified may-panic);
  - key-proof contract preserved — met;
  - lawful nominal key regressions — met.
- **S-D:** the unary-minus review at `3577dbc` and the consolidated review at
  `906b99d` are done (Addenda 2 and 3 in `TEAM_S_NEXT_CYCLE_ARCHITECT_REVIEW.md`).

## Validation

**Architect GO SHA `8d62f0a43915ef0e616b449270c227b0e3162c26`** (clean worktree):
`TMPDIR="$PWD/tmp" make check` **passed** (Executor 54/54, editor 61/61, "all
Moss v0.1 tests passed"); `make examples` **passed** (32).

**Final delivered SHA `dea9a4e21f9925e6d69943960eee0239460f2a3b`** (clean worktree): `make check` **passed** (Executor
54/54, editor 61/61, all Moss v0.1 tests); `make examples` **passed** (32).

Earlier: **Architect GO SHA `8d62f0a`** above. **Consolidated SHA `906b99d`**, the one the Architect reviewed, also passed
both.

New suites registered in `tests/run.sh`:
- `check_phase25_sa_numeric_literals.py`: 68 accepted literals × 3 engines, 63
  rejections.
- `check_phase25_sb_check_display.py`: 7 accepted programs × 6 engine/mode
  runs, 18 rejected programs in standalone and project check.
- `check_phase25_sc_safe_arithmetic.py`: 23 cases.
Before every commit, the Coordinator also:
- reran all Phase 25 suites — S1, S2, S3, S4, S-A, S-B, S-C, L7 repairs and G1;
- reran the 126 `tests/run.sh` negative-diagnostic expectations directly;
- ran a repo-wide `moss check` verdict diff against `ee8dcea`.

## Outstanding

- **Team G:**
  - concurrence on the five interface items;
  - `moss_runtime`;
  - `MapKeyProof`;
  - per-argument access in `Requirement`;
  - fixture `09c` (`Map().get(1, 2)` still reaches rustc on `a50fa30`).
- **Item 6:** pending L7 reopening after G2 review.
- **Tracked:**
  - ARCH-026 (diagnostic columns);
  - the S-B note that Strings inside a Vector print raw, so the output is
    ambiguous;
  - native `assertEqual` failure text still uses `{:?}` (stderr only);
  - a `-1` divisor is classified may-panic although every engine wraps.
- **L7 items still undecided from the previous cycle:**
  - builtin-name precedence (`parse_int`, …);
  - Rust-colliding identifiers;
  - `Float.to_string()` format;
  - `recover err` typing;
  - non-Copy shadow before a raise;
  - type-changing shadowing;
  - typed `Char(Int)` failure.
