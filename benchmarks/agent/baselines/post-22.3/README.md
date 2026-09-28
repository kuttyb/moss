# Post-22.3 Fresh-Agent Benchmark

This is the canonical one-trial AB001–AB030 run for Phase 22.3 Semantic Query
Expansion. It compares compiler commit `ddf927ca490d092b776168c45d330c439d220268`
with the fixed post-22.1 run while retaining corpus commit
`b187009f6ef06738c52216d34490342d76d7ab4c`, prompts, wrapper, validators,
Codex CLI 0.156.0, `gpt-6-sol`, medium reasoning, isolation, and limits.

`protocol.json` records the exact environment. Each AB directory contains the prompt,
final workspace, last agent message, command log, manifest, and authoritative validator
result. `aggregate.json`, `comparison.json`, and `REPORT.md` are deterministic derived
artifacts.

The run adds query-resolution status telemetry. This is additive instrumentation:
semantic queries remain excluded from meaningful validation attempts. The report does
not claim causality from a single stochastic trial.
