# Team C4 closeout — regex lookup attribution

| Category | Report |
|---|---|
| **Leaf agents** | 0 actual; no implementation worker was needed for the measurement-only outcome. |
| **Peak leaf concurrency** | 0. |
| **Non-leaf reviewers/managers** | Coordinator: 1 (`/root`). Independent Architect: 1 (`/root/architect`). Independent adversarial reviewer: 0. |
| **Submissions** | 1 docs-only PR for C4 findings; no non-PR handoff submission. |
| **Coordinator reviews** | Coordinator reviewed the profile and report; no implementation submission or repair rounds. |
| **Other review events** | Architect: 1 planned adversarial measurement checkpoint; recommended no new code change. Final report-SHA review pending. Adversarial: 0. L7: 0. |
| **Defects** | None confirmed. The inclusive type-environment walker profile is not evidence that any stateful traversal is redundant. |
| **Test evidence** | Measurement used the C3 candidate already validated by 2,064-source differential, Fast Debug parity, `make check` 141/141, and compiler-perf. C4 adds no code; standalone regex Callgrind and per-helper counters are recorded in `C4-regex-profile.md`. |
| **Review provenance** | C3 source candidate SHA `fdeb72b16faa82a8dfefd04a1db90e9d87fe4cf9`; C4 report-only delivery SHA to be recorded after commit. Architect measurement checkpoint: no-change recommendation. |
| **Usage** | Token counts and billed costs: unknown; provider evidence is unavailable. |
| **Value** | Per-helper counts isolate 3.268M binary-split cache hits from the other roughly 2.14M hits, while Callgrind shows parser-cache lookup work is under 0.1% Ir. Avoiding a speculative map optimization keeps scope on the dominant stateful type-environment walker and defers further work until the C2+C3 profile identifies a redundant caller/pass. |
