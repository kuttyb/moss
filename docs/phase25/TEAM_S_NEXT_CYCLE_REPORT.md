# Phase 25 Team S — next execution cycle report

**Branch:** `phase25-semantics-engines` (pushed)
**Starting SHA:** `ee8dceaa4851fb61a7b726cee05354da6917e38d`
**Architect-reviewed implementation SHA:** `b6d45cf5a761caea2df50b64d4357669647ffac0` (CONDITIONAL GO)
**Delivered implementation SHA (Architect findings fixed):** `baefd1fbd763ba0ec1d4955c6bad64f4bb71ac94`
**Milestone:** ready for joint G2/G3/G4 integration. This cycle does not complete
ABI-v10 or Phase 25, and nothing was merged into `main` or `phase25-integration`.

## Summary

| Workstream | Result |
|---|---|
| S1 checker authority | Rejected before any engine runs: record `==`/`!=`, mixed-type equality, named call and message arguments, juxtaposed operands, and record and Float-containing Map keys. The Fast Debug FileIO preflight now covers each test entry point and has fewer false positives, and stays fail-closed. G1 and ledger regressions are preserved. The empty-Map case is handed to Team G as failing fixtures. |
| S2 Char/String | Char is distinct; transparent `Char == String` and mixed `assertEqual` are removed in every engine. Callers are migrated. Conversion, escape, and randomized Unicode property tests pass in five engine configurations. The indexed String stays staged. The benchmark was rerun. |
| S3 key protocol | The scalar `less`/`isequal`/`hash` protocol is frozen and documented. Law and purity tests now cover imported, specialized, non-Copy, NaN-payload, and adversarial keys. There are 30 Map/Set fixtures ready to activate. One sort-key lowering defect is fixed. No Map/Set implementation. |
| S4 Fast Debug parity | 324 adversarial programs. 12 confirmed defects are fixed, each with a regression. 8 items need an L7 decision. |
| Team G interface | `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` proposes all five items. Team G concurrence is pending. |

## S1 — checker authority

| ID | Defect (reproducer) | Fix | Regression |
|---|---|---|---|
| S1.1 | `a == b` on records passed check, ran in Fast Debug, and failed in rustc (E0369). | `check_equality_operands` reports `RECORD_EQUALITY_UNSUPPORTED`. This also covers records nested in collections, `assertEqual`, and untyped specializations. No equality is synthesized. | `check_phase25_s2_char_string.py::test_record_equality_rejected`, `test_imported_record_equality_rejected` |
| S1.1b | `1 == "a"`, `true == 1`, `[1] == [1.5]` passed check and failed in rustc. | `TYPE_MISMATCH` for mismatched builtin operand types. `Int`/`Float` mixing is unchanged. | `test_mixed_operands_rejected` |
| P25-S-COORD-001 | `x = if c: 1 else: 2`, `x = foo bar`, `x = 1 2` passed check and failed in both engines. | `INVALID_EXPRESSION` for two operands with no operator between them. Postfix `data [0]`/`f (x)` stay valid, and `in` keeps its dedicated diagnostic. | `test_invalid_expression` |
| P25-S-COORD-002 | `Char(65).to_string().length()` was typed `char.length`, because `parse_simple_call` read it as a call of `Char`. | The callee's `(` must close at the final `)`. | `test_char_conversion_and_failure` |
| S1.2 | `message c.Bump(step: 2)` and `sub(a: 10, b: 3)` passed check and failed in every engine. Named arguments are not a documented contract. | Rejected with `MESSAGE_NAMED_ARGUMENT_UNSUPPORTED` / `CALL_NAMED_ARGUMENT_UNSUPPORTED`. Constructor, enum, and builtin (`by:`) labels are unchanged. | `check_phase25_s1_checker_authority.py::test_named_arguments` |
| S1.3a | `moss test --interp` and `margo test --interp` ran a test's earlier statements, then failed at its first FileIO statement. | A per-test preflight reports `FAIL <name>: [FAST_DEBUG_FILEIO] … not executed`, the other tests run, and the exit code is 1. | `test_fileio_test_entry_points` |
| S1.3b | False positives: FileIO names inside literals, labels, patterns, enum cases, locals, and same-named handlers in other domains. | Reachability skips names that cannot run code. Message targets are narrowed through checked call edges only when every message on the line has an edge (fail-closed guard). | `test_fileio_false_positives`, `test_fileio_still_rejected` (15 fail-closed cases) |
| P25-S3-D2 | `Map[Record, V]` passed check. rustc then failed (no `Eq`/`Hash`), and Fast Debug ignored the custom `isequal`/`hash`. | `RECORD_MAP_KEY_UNSUPPORTED` or `FLOAT_MAP_KEY_UNSUPPORTED` for any key containing a record or a Float, at index, assignment, and declared-type positions. This is fail-closed only; real support is Team G's. | `test_map_keys_fail_closed`; fixtures `03`–`07` |
| S1.4 | — | G1/L7/G3/G4 suites and `tests/phase25/ledger/run_ledger.py` (repaired column) pass. | — |
| S1.5 | Unpinned empty Map: a never-inserted `m.get`, `Map().get`, `fn fresh(): return Map()`, and an unused `Map()` pass check and fail in rustc (P25-S3-D3). | **Not fixed locally.** Handed to Team G Option B. | fixtures `09b`–`09e` (known-failing), `09a`/`09f`–`09h` |

## S2 — Char/String contract

- **Decision implemented:** Char is a distinct scalar. The transparent
  `Char == String` operator and mixed `assertEqual` were removed from:
  - the checker;
  - Fast Debug `equal`/`moss_isequal`;
  - native lowering (`moss_char_string_eq` and the assertEqual special case);
  - the runtime prelude.

  Mixed `isequal`/`less` were already `KEY_PROTOCOL_TYPE`.
- **Migration:** `docs/phase25/TEAM_S_CHAR_STRING_MIGRATION.md`. Migrated
  callers:
  - `sdk/path` (4);
  - `sdk/tables` (2);
  - the text-encoding benches (14);
  - two existing suites.

  The Gentle Introduction, the language skill, and bootstrap
  `source_surface.operators` (new `char_builtin` and `equality_operands`) are
  updated. A `moss check` diff over the 1,831 `.moss` files tracked at `f272af9`, against the
  starting compiler, shows only these intended rejections.
- **Hardened and tested** in `check_phase25_s2_char_string.py` (52 cases):
  - `Char(Int)` boundaries;
  - invalid dynamic codepoints stop every engine before use: surrogate edges,
    negative values, values above U+10FFFF, and 2³²+65 (no `u32` truncation);
  - 14 escapes in four forms agree across engines;
  - a seeded randomized property test (`MOSS_S2_SEED`; seeds 1, 7, 99, 2025 run)
    with about 900 checks each, against a Python oracle. It covers `length`,
    `chars`, `char_at`, `slice`, `find`, `reserve`/`append` construction, and
    concatenation, in Fast Debug, plain native debug/release, and staged
    indexed-v1 native debug/release.

  Malformed escapes remain covered by the 11 `INVALID_ESCAPE` cases in
  `check_phase25_l7_repairs.py`.
- **Diagnostic difference for L7:** an invalid `Char(Int)` reports a Fast Debug
  `ValueError` (exit 1) but a native panic (exit 101). No typed error was
  introduced.
- **Staged representation:** `MOSS_STAGED_STRING_REPR=indexed-v1` remains opt-in.
  - Benchmark rerun at `e95df42`
    (`docs/phase25/data/team_s_unicode_traversal_next_cycle.json`), at 1M code
    points: plain 81.5 s (≈106× per 10×, quadratic), indexed-v1 0.0176 s
    (≈10.8× per 10×, linear). Output is identical.
  - Cost: **+32 bytes per String value** (56 vs 24 bytes); metadata is
    1.6–7.1% of the payload.

## S3 — scalar key protocol and collection acceptance

- `docs/phase25/TEAM_S_KEY_PROTOCOL_V1_SCALARS.md` freezes the native and Fast
  Debug lowering targets, signatures and effects, and the Float semantics.
- Measured hashes are identical across the three engines:
  - Int: the value itself;
  - Float: the bit pattern, with one canonical NaN;
  - Char: the codepoint;
  - Bool: 0 or 1;
  - String: FNV-1a 64 over the UTF-8 bytes.
- `check_phase25_s3_key_protocol.py` has 26 cases:
  - imported keys from a module, a path package, and a compiled-only provider;
  - one untyped key specialized at six types;
  - String and record keys;
  - six Moss NaNs plus 8 native NaN bit patterns;
  - lawless and degenerate keys, with engine-identical results;
  - 11 rejection cases.
- **P25-S3-D1 fixed:** an untyped imported key used only by `sort(by:)` passed
  check and failed in rustc with a missing specialization. The sort key is now
  a call edge, in `collect_local_call_sites`.
- `tests/phase25/key_protocol/` has 30 fixtures and `run_fixtures.py` (default:
  check that each fixture still matches its recorded state; `--activate`: run
  them for real). Coverage:
  - Float keys;
  - imported nominal keys (source and source-free);
  - two consumers of one provider Map;
  - coherence mismatch;
  - malformed, impure, missing, and forged proofs;
  - `keys()` snapshots;
  - Map and Set on one protocol;
  - the S1.5 empty-Map cases.

  The Set spelling is **PROVISIONAL**, pending the Team G G2 Set API. Today: all
  fixtures match. They are now fail-closed rather than "rustc fails", except the
  4 Option B known-failing cases.
- **Not done, by instruction:** Map/Set operations, `MossMap`, and KeyProof
  records.

## S4 — Fast Debug adversarial parity

324 adversarial programs were run, each through check and all three engines.
The 12 confirmed defects are all fixed, with
`check_phase25_s4_fast_debug_adversarial.py` (18 cases). Each of its 8 test
functions fails on the starting compiler.

| ID | Engines | Defect | Fix |
|---|---|---|---|
| S4-001 | native | A try/recover join binding assigned after a raising call gave rustc E0381. | try lowering exits through an outer labelled block |
| S4-002 | Fast Debug | Branch-local bindings that shadow domain state were kept as join names; state was not written. | interpreter `BlockScope` |
| S4-003 | native | The leading-`if` split leaked branch `let`s into the suffix (wrong output 1001 vs 6). | conservative placement fallback |
| S4-004 | checker | A `let` bound on every path became assignable after the join. | `merge_binding_identities` |
| S4-005 | native | Unary minus on Int min panicked; large negated literals failed rustc. | `wrapping_neg`, `_i64` literals |
| S4-006 | all | Int literals above 2⁶³−1 saturated (Fast Debug) or failed rustc. | `INT_LITERAL_OUT_OF_RANGE` |
| S4-007 | effects, native | `seen = message w.Work(k)` into state never wrote state natively. | effect walker and state-write lowering |
| S4-008 | checker | A message result assigned to an immutable local or an incoming payload was accepted. | WRITE check |
| S4-009 | checker | A state write before a raise was rejected as "shadowed before raise". | raise-edge identity |
| S4-010 | checker | A Copy outer binding shadowed in `try` and read in `recover` was rejected. | Copy exemption (non-Copy still conservative) |
| S4-011 | native | `echo` of a Vector, Map, or record gave rustc E0277. | `NATIVE_ECHO_DISPLAY_UNSUPPORTED` target diagnostic at lowering |
| S4-012 | checker | `return -x` / `[-x, 1]` gave a type inference failure. | unary-minus inference |

## Team G coordination

`docs/phase25/TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md` proposes the following.
Team G concurrence is pending; until a Team G Coordinator signs it, it is a
proposal.

1. A single `moss_runtime` crate. Team G owns the crate and `MossMap`; Team S
   owns the `string` and `key_protocol` modules.
2. The built-in KeyProof targets are exactly the Team S scalar lowering, with
   deterministic unseeded hashes that do not depend on the String
   representation.
3. ABI-v10 Char and String:
   - layout and invariants;
   - the `moss-string-indexed-v1` String record;
   - provider identity;
   - six cross-crate tests.
4. The `admit_member` → `Admitted`/`Deferred`/`Rejected` interface over the
   existing builtin receiver table, and the joint ledger #12 suite.
5. The trigger for final Map/Set integration: a Team G G2 SHA passing its
   foundations and `make check`, then `run_fixtures.py --activate`.

## Validation

**Delivered SHA `baefd1fbd763ba0ec1d4955c6bad64f4bb71ac94`** (clean worktree):
`TMPDIR="$PWD/tmp" make check` **passed** (Executor 54/54, editor 61/61, "all
Moss v0.1 tests passed"); `TMPDIR="$PWD/tmp" make examples` **passed** (32).
All 126 `reject_case`/`reject_source` expectations in `tests/run.sh` were also
run directly against this compiler.

An intermediate run at `bd843db` failed (`swarm_014_remainder_string`: the
ARCH-013 rule pre-empted the `%` diagnostic), fixed in `baefd1f`.

### Earlier validation (Architect-reviewed SHA `b6d45cf5a761caea2df50b64d4357669647ffac0`, clean worktree)

| Command | Result |
|---|---|
| `make` | built |
| `TMPDIR="$PWD/tmp" make check` | **passed**: Executor 54/54, editor 61/61, "all Moss v0.1 tests passed" |
| `TMPDIR="$PWD/tmp" make examples` | **passed** (32 examples) |
| New suites (inside `make check`) | S1 31, S2 52, S3 26, S4 18 PASS lines; key-protocol fixtures: all 30 match |
| Existing Phase 25 suites (inside `make check`) | `l7_repairs`, `indexed_string`, `g1`, `g3`, `g4` passed |
| `python3 tests/tooling/check_agent_feedback.py`, `check_agent_skills.py`, `check_agent_api.py` | passed |

Earlier full runs found two regressions of the Coordinator's own changes:
- the mixed `assertEqual` diagnostic code was renamed;
- `INVALID_EXPRESSION` pre-empted the `in` diagnostic.

Both were fixed (`55b50a6`, `b6d45cf`) before the run above.

## Independent Architect review and disposition

The independent L6 Architect reviewed `b6d45cf` and issued **CONDITIONAL GO**
for joint G2/G3/G4 integration readiness
([`TEAM_S_NEXT_CYCLE_ARCHITECT_REVIEW.md`](TEAM_S_NEXT_CYCLE_ARCHITECT_REVIEW.md),
preserved unchanged).

| ID | Finding | Disposition |
|---|---|---|
| P25-S-ARCH-013 | Arithmetic on Char/Bool/String/collections passed check; Fast Debug printed invented values; rustc failed | **Fixed** `bd843db`, `baefd1f` (`TYPE_MISMATCH`, binary and unary; `%` keeps its Int-only diagnostic) |
| P25-S-ARCH-014 | Map and enum-containing Map keys not fail-closed | **Fixed** `bd843db` (`MAP_KEY_TYPE_UNSUPPORTED`, `ENUM_MAP_KEY_UNSUPPORTED`, declared types too) |
| P25-S-ARCH-015 | `assertEqual([Color.Red], …)` passed Fast Debug tests, failed native | **Fixed** `bd843db` (`ENUM_OPERATOR_UNSUPPORTED`) |
| P25-S-ARCH-016 | `Char(codepoint: 65)` accepted; bootstrap advertised it | **Fixed** `bd843db` (`CALL_NAMED_ARGUMENT_UNSUPPORTED`; bootstrap says `Char(Int)`) |
| P25-S-ARCH-017 | `range(...)` as a value accepted, failed in both engines | **Fixed** `bd843db` (`RANGE_OUTSIDE_FOR`) |
| P25-S-ARCH-018 | Numeric literal grammar (`1_000`, `0x10`, `1e400`) disagrees by engine | **Tracked**: needs the L7 literal-grammar decision (same as S4 L7 item 1) |
| P25-S-ARCH-019 | Standalone `check` accepts `echo v` of a collection; project `check` rejects | **Tracked** (Team S); native target diagnostic is consistent, check modes are not |
| P25-S-ARCH-020 | `-f(x)`, `-(x + 1)` wrongly rejected | **Tracked** (Team S); same as S4 L7 item 6 |
| DOC-1…DOC-7 | Doc and bootstrap inaccuracies | **Corrected** `bd843db` |

Regressions: `check_phase25_s2_char_string.py::test_architect_next_cycle_findings`.
The fixes after `b6d45cf` have **not** been Architect-reviewed unless an addendum
in the review document says so.

## Outstanding limitations

- **Team G / joint:**
  - Map/Set with user and Float keys, and Set itself, need `MossMap<K,V>` and
    KeyProof.
  - The indexed String default and the shared runtime crate wait on ABI-v10.
  - #12 open receiver.
  - The empty-Map Option B cases (09b–09e).
- **L7 decisions raised by S4:**
  1. Numeric literal grammar (`1_000`, `0x10`, `.5`, `1.5e400` disagree by engine).
  2. User functions named `parse_int`/`parse_float`/`to_int` (engines disagree),
     and inconsistent builtin-name precedence.
  3. Rust-colliding identifiers (`self`, `Self`, `Ok`, `Err`, `None`, `Some`,
     `drop`, a handler parameter `state`) fail in rustc.
  4. `Float.to_string()` vs the Phase 21 §13.2 canonical text.
  5. `recover err:` typing.
  6. Unary minus on calls and indexes (`-f(x)`).
  7. Non-Copy shadow before a raise.
  8. Shadowing with a different type in a nested block.
- **Architect-tracked:** P25-S-ARCH-018 (literal grammar), 019 (check-mode
  inconsistency for native echo), 020 (`-f(x)` rejected).
- **Other limitations:**
  - **S3 L1:** an untyped member-calling key from a path dependency is rejected
    as impure (`.mossi` effect summary).
  - **S3 L2:** purity is conservative (`%`, `/`, `char_at`, and indexing count as
    possible panics). This affects the G0 `UserKey` example and must be decided
    before proof purity freezes.
  - **S3 L3:** moving a non-Copy field of a `for` element passes check and fails
    in rustc.
  - **S3 L4:** `moss check <project-dir>` reports ok without checking.
  - **S1:** the FileIO preflight is still an identifier over-approximation (it
    can conservatively reject; it is not known to fail open).
  - **S4-011:** a target diagnostic at lowering. Standalone `moss check` accepts
    `echo v`, while project-mode check (which lowers) rejects it.
  - `docs/FAST_DEBUG.md` says Queue is a `for` source; the checker rejects that.
  - `MOSS_STAGED_STRING_REPR` with an unknown value silently selects the plain
    representation.

## Agent execution

See `docs/phase25/agent-patterns/team-s-semantics-engines.md` ("Next execution
cycle"). Three leaf implementation agents (S1, S3, S4; S2 was Coordinator-authored) and one independent Architect ran; each
leaf agent worked in its own worktree.
