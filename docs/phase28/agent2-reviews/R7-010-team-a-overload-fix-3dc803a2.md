# Independent review — Team A overloaded ReturnValue authority

- **Reviewed implementation SHA:** `3dc803a241c49bbff7c6976fbdb0c749e3aba56d`
- **Review worktree:** detached at the exact implementation SHA.
- **Reviewed Team A stack:** merge base `d4aa51111d62917c7bde2ecb78000b930e372496f`; commits `84465ac4` (None result type), `db95e3a0` (shadowed None), `735a36c3` (fact summary), and `3dc803a2` (overload return identity).
- **Scoped verdict:** **GO for the checker-side overloaded ReturnValue identity fix at this SHA.**
- **Whole-stack / Phase 28 disposition:** **NO-GO.** Phase 26 X1 fails on the Team A candidate stack relative to baseline; full suite/oracle and clone provenance are outside this scoped review and remain unapproved.

## Overloaded-method identity

The previous repro was retained as `tests/phase28/r7_010/team_a/return_none_overloads.moss`. It defines two `Choice.next` overloads returning `option[Int]` and `option[String]` respectively, each returning `None`.

Built the compiler from this exact checkout with:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-overload-fix-build moss-dev
```

The binary SHA-256 is `f91e8ed59dbbbaffe261c9da6c1ab61891fecd44203a81b00688c6521de9a7ca`.

The registered authority gate passes:

```sh
python3 tests/tooling/check_phase28_return_none_authority.py tmp/review-overload-fix-build/moss-dev
```

The gate asserts one `option[int]` and one `option[string]` fact, checks unique `method-return-v1` EST contexts, drops/perturbs each overload's context/type fact and requires fail-closed errors, then verifies Fast Debug output `empty-int\nempty-string\n`.

An independent checker dump confirms the stable, distinct contexts:

- Int: `method-return-v1:6:Choice4:next7:1;3:int`
- String: `method-return-v1:6:Choice4:next10:1;6:string`

Repeated checker invocations produce byte-identical fact dumps. The two ReturnValue facts retain the correct closed types and distinct exact EST fact contexts; no false `specialization_identity` is claimed for these non-specialized methods. The earlier method-name-only collision is closed.

## Phase 26 isolated C3 `-Werror`

Command:

```sh
python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py tmp/review-overload-fix-build/moss-dev
```

Result: PASS. Its isolated helper compiles with `g++ -std=c++17 -O0 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic`; the three profile fixtures and cache invariants also pass.

## Phase 26 X1 comparison

Each run used `tests/tooling/check_phase26_1_1_x1.py`, a fresh isolated artifact directory under that worktree's `tmp/`, and a compiler built from the listed exact revision. The shell had no inherited `MOSS_*`, `MARGO_*`, `CARGO_*`, `RUST*`, or `TMPDIR` variables; the script sets its `MOSS` variable to the provided compiler path.

| Revision | Compiler binary SHA-256 | Artifact directory | Result |
| --- | --- | --- | --- |
| Team A implementation `3dc803a2` | `f91e8ed59dbbbaffe261c9da6c1ab61891fecd44203a81b00688c6521de9a7ca` | `tmp/review-overload-fix-x1` | **FAIL** in check-only Fast Debug after `4\n7\n`: `MOSS_INTERNAL_INVARIANT checked return-value authority unavailable` |
| Frozen Team A `735a36c3` | `bcd2804cef51b3f27652474b00e02eacbcbe0e681cc22139bef46e54b5b327d4` | `tmp/review-x1-frozen-735` | **FAIL** at the same stage with the same output and diagnostic |
| Baseline `150ab36b41e45504aaa400aa717da241be7542fb`, freshly rebuilt in isolated `COMPILER_DIR=tmp/review-baseline-exact-build` | `6de238d137e07a11e80900f0109ba91cb53203ab2f0839fca94a73876337ab63` | `tmp/review-x1-baseline-rebuilt-150ab` | **PASS**, `ok: true` |

This X1 failure is present in the frozen `735a36c3` stack and unchanged by `3dc803a2`; it is not introduced by the overload repair. It is nevertheless a candidate-stack regression against the established `150ab36b` baseline and blocks whole-stack GO. A prior baseline binary was also checked; its hash matched the fresh isolated rebuild, and the fresh baseline result was reproduced.

## Scope

The exact Team A overload checker change and its registered authority/corruption tests receive scoped GO. This review does not approve the Phase 26 X1 stack regression, any native or `.mossi` consumer repair, full default/EST-shadow suites, frozen ownership oracle, runtime clone provenance, or broader Phase 28 staging.
