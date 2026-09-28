# Moss Phase 22.3 Semantic Query Expansion Comparison

## Protocol and interpretation

This compares the fixed `post-22.1` run at compiler commit `bb388d4aeb1df7f6dac682b427d666a44a748bf4` with `post-22.3` at compiler commit `ddf927ca490d092b776168c45d330c439d220268`. The AB001–AB030 corpus, prompts, wrapper, one-session isolation, Codex CLI 0.156.0, `gpt-6-sol`, medium reasoning, 900-second task limit, validators, and network policy match.

Phase 22.3 adds query-resolution telemetry but does not change which commands count as meaningful validation. There is one stochastic trial per task. Deltas are measured observations, not causal or statistically significant claims.

## Overall comparison

| Metric | Post-22.1 | Post-22.3 | Delta |
|---|---:|---:|---:|
| Final passes | 28 | 28 | +0 |
| First-validation successes | 16 | 18 | +2 |
| Eventually green | 30 | 30 | +0 |
| Failed correctness attempts before green | 14 | 12 | -2 |
| Attempts-to-green mean | 1.467 | 1.4 | -0.067 |
| Diagnostic occurrences | 20 | 14 | -6 |
| Repeated-diagnostic-loop tasks | 1 | 1 | +0 |
| Failed semantic-query calls | 2 | 2 | +0 |
| Successful semantic-query calls | 11 | 8 | -3 |
| QUERY_TARGET_NOT_FOUND calls | 0 | 0 | +0 |
| Agent tool calls | 422 | 367 | -55 |
| Moss/Margo invocations | 156 | 134 | -22 |
| Infrastructure failures | 0 | 0 | +0 |
| Timeouts | 0 | 0 | +0 |

Headline completion was flat at 28/30 and all tasks again reached a green compiler or execution command. First-validation success rose 16→18, mean attempts fell 1.467→1.400, agent tool calls fell 422→367, and Moss/Margo calls fell 156→134. Because this is one stochastic trial, these changes do not establish that the query API caused the reduction.

## Semantic-query findings

Agents made 10 semantic-query calls: 8 returned facts and 2 were blocked by the program diagnostic. Successful queries per completed task were 0.367→0.267. The eight target resolution outcomes were all `resolved`; ambiguous/missing rate was 0.000 and `QUERY_TARGET_NOT_FOUND` remained 0.

The benchmark did not invoke the new `resolve` command and did not log source-file reads, so it cannot support a claim about semantic facts obtained without source inspection. It also retained AB022's three-occurrence ownership diagnostic loop. The new API is directly covered by compiler regressions, while this trial shows limited spontaneous adoption by fresh agents.

## Final failures

- **AB017**: exact-output-format-mismatch; stdout did not match exactly. It reached green in 1 meaningful validation attempt(s).
- **AB020**: requested-structure-missed; no file matched /\|>\s*(?:map|filter)\s*\(/. It reached green in 2 meaningful validation attempt(s).

AB017 failed exact output formatting after compiling successfully. AB020 recovered from its compiler diagnostic but missed the validator's requested pipeline structure. The failing-task identities differ from post-22.1, another reason not to overinterpret aggregate movement.

## Scope and retained evidence

Moss semantics and the frozen corpus were unchanged. Raw prompts, final task trees, tool logs, manifests, and validator results are retained beside this report. `aggregate.json` and `comparison.json` are deterministic derived artifacts. Direct callable effects, per-use move provenance, and line-to-expression source ranges remain explicit availability gaps documented by Phase 22.3; trace slicing is Phase 22.4 and repair/workflow automation is Phase 22.5.
