# SUMT shadow harness

Shadow-only tooling for the SUMT summary table. Nothing here changes compiler
output; summaries are produced only with `MOSS_SUMT_SHADOW` set.

- `MOSS_SUMT_SHADOW=<file> ./moss check f.moss` writes one JSON line per generic
  function: summary, ownership obligations, parameter access, completeness and
  requirement origins.
- `MOSS_SUMT_VALIDATE=1` (with the above) also checks each summary against the
  real compiler for a set of substitutions (`phase: validate` lines, then one
  `validation_summary` per function). Slow: two nested full checks per
  substitution.

Run everything from the repository root:

    tools/sumt/run.sh <unmodified-base-moss> <out-dir>      # bounded set
    SWEEP=all JOBS=32 tools/sumt/run.sh <base-moss> <out>   # every .moss

Expected: `complete_unknown_pred` 0; every DISAGREE line in `summary.txt` is a
defect; parity differs only on tests/negative/generic_constructor_field_consume{,_alias}.moss
(the hold2 fix); byte identity on all top-level programs; both test groups rc=0.

- `fixtures/`: small targeted programs (ownership, callbacks, pipelines,
  call chains). `rejected_generic_body.moss` is meant to fail `moss check`.
- `targets.txt`: corpus files behind earlier disagreements.
