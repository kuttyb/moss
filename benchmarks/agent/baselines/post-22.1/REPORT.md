# Moss Phase 22.1 Agent-Teaching Diagnostics Comparison

## Protocol and interpretation

This compares the frozen `pre-22.1` run at compiler commit `22fdb024e4f284d17fb9abfe887856e17127aae7` with `post-22.1` at `bb388d4aeb1df7f6dac682b427d666a44a748bf4`. AB001-AB030, the corpus revision, prompts, wrapper, one-session protocol, Codex CLI 0.156.0, `gpt-6-sol`, medium reasoning, 900-second limit, isolation, network policy, and validators match.

There are no known model/runtime/protocol confounders. The compiler and orchestration commit changed together as the intended treatment. This is one stochastic trial per task, so changes are engineering evidence, not statistically significant estimates.

## Overall comparison

| Metric | Pre | Post | Delta |
|---|---:|---:|---:|
| Final passes | 28 | 28 | +0 |
| Final failures | 2 | 2 | +0 |
| First-validation successes | 17 | 16 | -1 |
| Eventually green | 30 | 30 | +0 |
| Failed correctness attempts before green | 13 | 14 | +1 |
| Attempts-to-green mean | 1.433 | 1.467 | +0.034 |
| Attempts-to-green median | 1.0 | 1.0 | +0 |
| Attempts-to-green max | 2 | 2 | +0 |
| Diagnostic occurrences | 18 | 20 | +2 |
| Repeated-diagnostic-loop tasks | 2 | 1 | -1 |
| Failed semantic-query calls | 4 | 2 | -2 |
| QUERY_TARGET_NOT_FOUND calls | 2 | 0 | -2 |
| Agent tool calls | 407 | 422 | +15 |
| Moss/Margo invocations | 157 | 156 | -1 |
| Infrastructure failures | 0 | 0 | +0 |
| Timeouts | 0 | 0 | +0 |
| Out-of-scope modification tasks | 0 | 0 | +0 |

Headline outcomes were unchanged at 28/30 final passes and 30/30 eventually green. First-validation success fell by one and total tool calls rose, so this run does not support a broad efficiency claim. Both post-run failures (AB014 and AB017) were again exact-output formatting errors after successful compilation, not failures to recover from a compiler diagnostic.

The query-target result is the clearest targeted recovery change: AB015 used canonical qualified targets immediately, eliminating two `QUERY_TARGET_NOT_FOUND` calls and removing that repeated-diagnostic loop. AB022 still deliberately queried the same ownership error through check, ownership, and effects; its cross-command rule and cause were consistent, but the three recorded occurrences remained.

## Targeted task comparison

| Task | Pre diagnostic(s) | Post diagnostic(s) | Attempts pre/post | Failed queries pre/post | Moss/Margo pre/post | Agent tools pre/post | Final pre/post |
|---|---|---|---:|---:|---:|---:|---|
| AB008 | `MOSS_COMPILE_ERROR` | `DOMAIN_SELF_MESSAGE` | 2/2 | 0/0 | 6/6 | 15/15 | pass/pass |
| AB009 | `MOSS_COMPILE_ERROR` | `DOMAIN_SELF_MESSAGE` | 2/2 | 0/0 | 6/6 | 14/16 | pass/pass |
| AB010 | `TYPE_INFERENCE_FAILED` | `DOMAIN_ROUTE_NOT_DECLARED` | 2/2 | 0/0 | 6/6 | 14/15 | pass/pass |
| AB012 | `OWNERSHIP_CONFLICTING_ACCESS` | `OWNERSHIP_CONFLICTING_ACCESS` | 2/2 | 0/0 | 5/5 | 12/12 | pass/pass |
| AB015 | `QUERY_TARGET_NOT_FOUND x2` | `none` | 1/1 | 2/0 | 8/6 | 12/14 | pass/pass |
| AB017 | `TYPE_INFERENCE_FAILED` | `TYPE_INFERENCE_FAILED` | 2/2 | 0/0 | 7/5 | 19/17 | fail/fail |
| AB018 | `FUNCTIONAL_SEMANTIC_ERROR` | `FUNCTIONAL_PLACEHOLDER_REQUIRED` | 2/2 | 0/0 | 5/5 | 14/13 | pass/pass |
| AB019 | `FUNCTIONAL_SEMANTIC_ERROR` | `FORMAT_CHECK_FAILED, FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED` | 2/2 | 0/0 | 4/8 | 11/17 | pass/pass |
| AB020 | `FUNCTIONAL_SEMANTIC_ERROR` | `FUNCTIONAL_CAPTURE_MUTATION` | 2/2 | 0/0 | 5/5 | 13/14 | pass/pass |
| AB021 | `MOSS_COMPILE_ERROR` | `DOMAIN_HANDLER_REQUIRES_MESSAGE` | 2/2 | 0/0 | 4/5 | 12/12 | pass/pass |
| AB022 | `OWNERSHIP_CONFLICTING_ACCESS x3` | `OWNERSHIP_CONFLICTING_ACCESS x3` | 2/2 | 2/2 | 9/8 | 17/16 | pass/pass |
| AB023 | `MOSS_COMPILE_ERROR` | `DOMAIN_SELF_MESSAGE` | 2/2 | 0/0 | 5/5 | 13/12 | pass/pass |

Across these 12 evidence-backed tasks, Moss/Margo invocations were unchanged at 70, failed semantic-query calls fell 4→2, and agent tool calls rose 166→173. All reached green in at most two correctness attempts. Diagnostic differentiation—not a lower attempts-to-green result—is the main result.

## Diagnostic-specific findings

- AB008, AB009, and AB023 changed from generic `MOSS_COMPILE_ERROR` to `DOMAIN_SELF_MESSAGE`; AB021 changed to `DOMAIN_HANDLER_REQUIRES_MESSAGE`; AB010 now reports `DOMAIN_ROUTE_NOT_DECLARED` instead of the downstream `TYPE_INFERENCE_FAILED`. Each recovered in one additional correctness attempt.
- AB018-AB020 changed from `FUNCTIONAL_SEMANTIC_ERROR` to, respectively, `FUNCTIONAL_PLACEHOLDER_REQUIRED`, `FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED`, and `FUNCTIONAL_CAPTURE_MUTATION`. AB019 performed extra formatting/check commands; the run does not show lower command cost for this family.
- AB012 and AB022 retained `OWNERSHIP_CONFLICTING_ACCESS`, now with structured conflicting actuals, inferred modes, related locations, and sound separation guidance. AB022's repeated exploration remained.
- AB015's failed target-resolution queries fell 2→0. The compiler-known qualified handler identities in bootstrap/API guidance were sufficient for this trial; no fuzzy lookup was added.
- AB017 retained `TYPE_INFERENCE_FAILED` and used fewer Moss commands, but again failed only final output shape. The exact transient starter source was not retained in the frozen evidence, so Phase 22.1 did not invent a more specific inference class for it.

## Scope and limitations

The accepted/rejected language set and Moss semantics were intentionally unchanged. Raw task artifacts are retained adjacent to this report. `aggregate.json` is the deterministic post-run aggregate and `comparison.json` is the machine-readable source for this comparison. The frozen pre-run directory was not regenerated or modified. Generic diagnostics outside the evidence-backed families, including AB005's unrelated `MOSS_COMPILE_ERROR`, remain deferred.
