# Branch-local early return inference

The natural application used `return -100` in three nested rejection branches
of a function declared `-> Int`. `moss fmt --json` rejected the program with
`TYPE_INFERENCE_FAILED`, pointing at the first nested branch even though the
function result annotation and the literal are both concrete integers.

`moss check --json`, `moss type run_workflow`, native `margo build`, native
execution, and Fast Debug all accept the application. A smallest nested
reproducer also checks, but `moss fmt --json` rejects it with
`FORMAT_PARSE_ERROR`. The one-level minimal form checks successfully.

Classification: **formatter validation bug / diagnostic mismatch**, not an
ordinary type-inference restriction. The application uses `rejection_score()`
at the nested early-return sites so it can be canonically formatted; this keeps
the early-results control-flow shape while avoiding the formatter-only fault.
