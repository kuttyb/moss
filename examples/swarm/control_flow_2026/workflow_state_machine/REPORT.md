# Workflow state-machine dogfood report

## Intent and first attempt

I built a release-workflow evaluator rather than isolated branch examples. Its
input stream drives intake, review, approval, completion, retry, rejection, and
incomplete-trail paths. The first full application used direct nested
`return -100` statements for rejection. The checker, semantic type query,
native build/run, and Fast Debug all accepted it, but the canonical formatter
rejected it. The source and raw structured formatter results are preserved in
`failed_attempts/01_branch_return_inference/` before the workaround.

## Iteration and diagnosis

The smallest one-level early-return form checks. A nested reproducer also
checks but fails only under `moss fmt --json` with `FORMAT_PARSE_ERROR` and the
misleading text “cannot infer the type of this return expression.” `moss type
run_workflow` resolves `int`; `moss ownership run_workflow` infers its
`Vector[Int]` input as `READ`; `moss calls` resolves the score/rejection helper
edges. This is a formatter validation/parity bug with a diagnostic mismatch,
not a language restriction or ownership failure. The final source preserves
the control flow by returning `rejection_score()` from nested branches.

## Successful ordinary control and value flow

`run_workflow` rewrites mutable `stage`, `retries`, `score`, and `index` over a
`while` traversal. Every loop iteration records the pre-transition state/event
pair in a mutable `Vector[Int]` history. Nested branches advance, retry, or
penalize the workflow; rejection and successful finish naturally return early.
An exhausted input flows into `incomplete_score`, which consumes the built
history only after all subsequent local uses have ended. `retry_penalty` is a
second while-loop helper, and `resolved_score` connects the accumulated values
to both terminal and incomplete outcomes. `completed_steps` confirms that an
ordinary loop `break` is supported by checker, native execution, and Fast
Debug.

## Ownership and inference observations

The `events` parameter is inferred `READ`, allowing both indexed reads and
`count`; the history is locally mutated with `push` then passed only at the
terminal incomplete path. No source ownership annotations were needed. The
only surprise was the formatter-specific nested negative-literal return fault;
semantic checking and native lowering consistently inferred the function as
`int`.

## Validation agreement

Final source passes `moss fmt --check --json` and `moss check src/main.moss
--json`. `margo build --json` succeeds; `margo run` prints the retry recovery
score `55` and terminal-event count `6`; `margo test --json` passes all six
tests. `moss test --affected --json` also passes all six (conservative fallback
selection on its first baseline). `margo debug --trace` executes the same demo,
including loop, branch, local-mutation, helper return, and early-value-flow
events; its stdout agrees with native execution.

## Tests

The six tests cover straight-through completion, retry/rewrite recovery,
immediate rejection, incomplete-history scoring, intake fallback, and break
termination. The formatter issue's application source plus one-level and
nested minimal reproducers are retained under `failed_attempts`.

## Ledger overlap

This package was designed and diagnosed independently before any ledger or
other swarm report comparison. No ledger comparison was performed in this
subtask; the nested-return formatter reproducer is supplied for the coordinator
to compare after independent discovery.
