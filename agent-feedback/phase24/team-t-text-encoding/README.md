# Phase 24 Team T — Text & Encoding feedback

This project dogfoods the repository-wide
[`agent-feedback` policy](../../README.md) while implementing `moss.text` and
`moss.encoding`.

Project identity for every Team T observation:

```json
{"phase":"phase24","project":"team-t-text-encoding"}
```

Create a workstream directory only when a meaningful observation is ready to
record. The record path is:

```text
agent-feedback/phase24/team-t-text-encoding/<workstream>/observations.jsonl
```

Expected workstreams may include `text-primitives`, `text-builder`,
`utf8-codec`, `string-interop`, `cross-package-integration`, `validation`, and
`performance`, but use the actual task area rather than forcing work into this
suggested list. Do not organize directories by feedback category.

Team T agents record observations during implementation, retain original
records after resolution, and run:

```sh
python3 tests/tooling/check_agent_feedback.py
```

The independent Architect reviews classification and evidence, challenges
unsupported positive and negative claims, and produces this project's
`SUMMARY.md` from the repository template. If Team T work also becomes a SWARM
experiment, follow both protocols and cross-link evidence rather than
duplicating the SWARM issue database.
