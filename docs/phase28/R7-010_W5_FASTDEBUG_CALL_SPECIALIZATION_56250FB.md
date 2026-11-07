# R7-010 W5 Fast Debug call specialization

## Scope and result

This isolated follow-up starts from `56250fb10563dc8955ac355a4f06a1d8703c8862`.
It closes Fast Debug's nested static-call specialization gap by consuming the
checker-selected target semantic identity and specialization key. It also
retains the existing checked pipeline callback path through exact EST callback
facts. No clone, ownership, or `.mossi` schema changes are included.

For ordinary source calls, `SemanticCallEdge::target` remains a call-graph
display spelling. The checker now sets `target_callable_identity` alongside a
concrete `specialization_key` for both open-result and closed-result static
calls. Fast Debug matches caller context/specialization, source line, and the
exact target semantic identity, then requires one matching target
specialization. Missing, duplicate, wrong-target, or contradictory authority
fails closed. It does not select from runtime argument types.

Pipeline callbacks do not have ordinary call edges. The checker already
specializes named callbacks through `check_functional_callable`; this follow-up
publishes that resolved callable identity and stable specialization key on the
exact EST callback Identifier selected by its owning checked PipelineStage and
stage argument ordinal. Fast Debug consumes those facts only for that
Identifier node. A missing or contradictory callback fact fails closed.

`SemanticCallEdge` is in-memory checker data, copied into module projections;
the new target identity field is rebuilt during checking and is not a `.mossi`
wire field. Callback facts use the existing EST semantic-facts representation.
The registered gate checks the source-backed callback path; imported
source-free pipeline callbacks were not separately exercised.

## Validation

- Dev and optimized compiler builds completed with the repository's existing
  warnings.
- Registered `check_phase28_w5_call_specialization` passed 1/1 in Default and
  1/1 in EST-shadow through `tests/run_parallel.py`.
- The gate covers `two<int>` calling two different targets on the same source
  line, output parity, direct-edge drop/duplicate/specialization perturb/wrong
  target identity, and callback fact drop/duplicate/specialization/target
  perturbations. Each corruption must produce `MOSS_INTERNAL_INVARIANT`.
- `check_phase26_w2_inference_worklist.py` passed in Default and EST-shadow,
  including its pipeline callback output check.
- Native Debug and `rustc -C opt-level=3` outputs both matched Fast Debug.
- `python3 tests/tooling/check_agent_feedback.py`, Python compilation, and
  `git diff --check` passed.
- Full suites and Phase 28 oracle were not run.

## Review status

This candidate is not integrated. Team A and Team C exact-SHA review are
required before any integration. Review should inspect exact edge source and
target matching, callback stage/argument identity mapping, drop/duplicate/
perturb coverage, and the source-free callback limitation above.
