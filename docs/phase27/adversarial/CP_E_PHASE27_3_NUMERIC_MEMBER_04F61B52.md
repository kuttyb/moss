# CP-E independent review: Phase 27.3 numeric member fix

NO-GO

Source: `04f61b52ba410b1f542328fd878ce518fb25658f`
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

## Finding

The fix restores the original diagnostic for the target malformed member `p.0`: baseline and candidate, in both `MOSS_EST=off` and `MOSS_EST=shadow`, return `UNKNOWN_FIELD`, `record 'P' has no field '0'`, at line 6, column 1.

The numeric-member handling is not yet diagnostic-parity complete. A nearby malformed suffix, `p.0x`, gives this four-mode result:

| Revision/mode | Result |
| --- | --- |
| Baseline off | `UNKNOWN_FIELD`: `type 'P' has no field '0x'` (line 6, column 1) |
| Baseline shadow | `UNKNOWN_FIELD`: `type 'P' has no field '0x'` (line 6, column 1) |
| Candidate off | `MOSS_INTERNAL_OR_IO_ERROR`: `EST invalid numeric literal` (line 0, column 1) |
| Candidate shadow | `MOSS_INTERNAL_OR_IO_ERROR`: `EST invalid numeric literal` (line 0, column 1) |

This is a candidate-only regression, classified **MIGRATION BUG**. The suffix is treated as invalid numeric syntax by EST instead of preserving the legacy member name and allowing ordinary unknown-field validation. Shadow mode reproduces the candidate result, so it does not detect or correct the parity failure.

The independently generated suffix probes also found candidate acceptance where the baseline reports `UNKNOWN_FIELD`: `p.0xff`, `p.0_1`, `p.1e3`, and `p.1.`. `p.0e`, `p.1foo`, and `p.1_name` produce the same candidate internal-error class as `p.0x`. `p.00` and `p.0.n` preserve baseline diagnostics; `p.n` succeeds in both revisions. Full per-mode results are in `numeric_suffix_four_mode.json`.

## Reader and closure audit

The widened checker/emitter reader lint passed. It reported 40 audited raw `Stmt` NAME comparisons and 9 binding-name comparisons. The three reader exemptions are keyed by exact function plus API and checked once each by the lint: `phase20_is_nontransferable_capability | split_top_level`, `builtin_equality_type | split_top_level`, and `contains_float_type | split_top_level`. A direct scan found zero `tree_expression_text` references in `src/semantic_checker.cpp` and `src/native_generator.cpp`.

I audited the checker/emitter expression-reader categories against the phase inventory: pipeline and grouping reads; identifier/name recognition and occurrence scans; trimmed equality; simple/member/bound-method calls; named arguments; binary operators; indexes; vectors and delimited child lists; enum cases; FileIO and executor chains; and assignment/colon scans. Expression-bearing statement slots (`a`, `b`, `args`, and `text`) were included in the widened raw-comparison audit. No unclassified reader or unchecked exemption was found in this pass.

The common source corpus completed full compiler `check --json` in baseline/candidate off/shadow modes for 1,620 files under `examples/` and `tests/`, excluding adversarial artifact directories. After normalizing checkout paths in diagnostics, there were zero verdict/diagnostic differences or off/shadow deltas. The focused Phase 27.1 rejected-source sweep covered 11 repository controls plus generated symbolic-OR and trailing-subtraction probes (52 checks across the four modes); all common controls retained identical verdicts and diagnostics. This corpus does not include the newly introduced numeric suffix edge cases above, which were probed separately.

## Validation

- `make --jobs="$(nproc)" moss-opt moss-dev` passed on the candidate.
- The same optimized and development compiler targets passed on the baseline.
- `python3 tests/tooling/check_phase27_string_debt.py` passed on the candidate.
- No performance run was performed.

## Evidence

Evidence files are in `tests/phase27/adversarial/cp-e-numeric-member-04f61b52/`.
Evidence-manifest SHA-256: `8669f308c4b49cb18abef43e876b32bebd447e90cb80edafc34b398e688f9313`.
