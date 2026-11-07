# P28-R7-011 identity reproduction checkpoint

Candidate compiler source: `00b54a515364f9237ce758eb866fa621da5375b5`.
The source was exercised with `build/compiler/moss-dev`; binary SHA-256 at the
oracle snapshot is `d8590fa4ff2b85d375db65518315d12a8f5b55c2fc0bea9b242a265c92ea479a`.
This is an observed pre-Step-1 reproduction table, not acceptance evidence for
the unfinished specialization identity work.

| Fixture | Expected | Fast Debug | Native | Result |
|---|---|---|---|---|
| `nest_spec` | `10`, `3.5` | `10`, `3.5` | `10`, `3.5` | PASS |
| `dom_spec` | `10` | `10` | fails with `MOSS_INTERNAL_INVARIANT return-value requirement/fact missing or ambiguous` | FAIL |
| `meth_spec` | `4`, `2.5` | line 8: `MOSS_INTERNAL_INVARIANT exact checked call edge specialization is missing, duplicated, or contradictory` | `4`, `2.5` | FAIL |
| `try_spec` | `5`, `1.5` | `5`, `1.5` | `5`, `1.5` | PASS |
| `cb_spec` | `12` | `12` | `12` | PASS |
| `dead_tail2` | `1`, `0` | `1`, `0` | `1`, `0` | PASS |
| `o15b` | `3` | `3` | `3` | PASS |
| `field_read` | `3` | `3` | `3` | PASS |
| `field_consume` | reject at line 9 | rejected with the unchanged diagnostic | rejected with the unchanged diagnostic | PASS |

The callback dynamic-fact drop and perturb cases fail closed. All nine callback
corpus programs compile in Fast Debug and Native. The two field holes and full
field-control grid pass both engines under Default and EST-shadow; see the
scoped Agent 2 review for source details.

`tests/tooling/check_phase28_l8_identity_repros.py` currently exercises this
set but is not registered as one suite job. L8 requires registration as
`check_phase28_l8_identity_repros` after the two specialization defects are
fixed; registering it as a passing gate before then would be incorrect.
