# Phase 27.3 report (in progress)

## Corrections and independent reproductions

| Classification | Finding and correction | Oracle | Reproducer |
| --- | --- | --- | --- |
| MIGRATION BUG | Checker effect/ownership walks split on the final dot in FileIO path strings, passing truncated fragments to recursive expression checking. Both now project EST `Member` nodes. | Phase 27.2 compiler at `04be1a1`; current off and shadow produce the same successful check result. | `tests/tooling/fixtures/phase20_fileio_runtime_integration.moss:29`; CP-E paired dotted-member probes. |
| MIGRATION BUG | `inferred_expr_core` split a member receiver at the final dot in the string argument `"tmp/phase21c-concurrent-a.data"`. It now projects member access from the EST node. | Phase 27.2 compiler at `04be1a1`; current off and shadow agree on successful checking. | `tests/tooling/fixtures/phase21c_concurrent_roots.moss:57`; calls `a.Run("tmp/phase21c-concurrent-a.data", 0, 1)`. |
| MIGRATION BUG | The `project_enum_case` shadow oracle parsed parenthesized case source without first removing redundant grouping, while EST projects the underlying constructor node. Shadow parity now invokes the unchanged legacy parser on the normalized child source. | `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py` compares Phase 27.2 off, candidate off, and candidate shadow; all check and execution results agree for `unwrap((Choice.Value(value: 7)))`. | The parenthesized enum constructor case in that committed CP-E probe. |
| LEGACY MISPARSE | `split_binary` sees `==` inside the rejected statement-shaped expression `if ...: ... else: ...`; EST correctly has no expression root. Shadow comparison narrowly accepts only this `if`/colon/`else:` shape. | Phase 27.2 off, candidate off, and candidate shadow report the same `INVALID_EXPRESSION`; the independent oracle is the checker diagnostic. | `invalid_if_expression` in `tests/phase27/adversarial/cp-e-fresh/check_cp_e_fresh.py`; also `x = if 1 == 1: 1 else: 2` in `tests/tooling/check_phase25_s2_char_string.py`. |

Focused evidence from the current tree: `tmp/phase27-3/cp-e-after-enum-and-if-fix.json`, `tmp/phase27-3/phase21c-phase27-2.json`, and `tmp/phase27-3/fileio-phase27-2.json`. Full gates at pushed `53025f4d` are recorded in `.codex/CURRENT_STATUS.md`; they predate the fixes in this report and must be rerun at a new checkpoint.

## Remaining sections

The fresh CP-E rerun, CP-F verdict, semantic-fact audit, current full gate table, performance comparisons, sanitizer scan, merge SHA, and release tag will be appended after they are completed.
