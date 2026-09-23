# Validation Engine

A standalone Moss package that normalizes configuration-style rules, applies per-key limits, computes penalties, and summarizes a collection score.

The demo intentionally uses type-field projections, nested predicates and arithmetic, named calls, `Map` indexing/default lookup, `Vector` indexing, and eager `map`/`filter`/`sum`/`count` pipelines.

Run from this directory:

```sh
../../../../moss check src/main.moss --json
../../../../margo build --json
../../../../margo run
../../../../margo test --json
../../../../margo debug --trace
```

Expected demo output:

```text
rule latency normalized 68 accepted true
health 192 accepted 2
```

The independently observed failed drafts and their structured results are retained under `failed_attempts/`; see `REPORT.md` for classifications and final validation.
