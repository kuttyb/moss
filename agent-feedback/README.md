# Moss developer-experience feedback

`agent-feedback/` is the repository-wide record of what it is like to build real
Moss source, SDKs, compiler support, runtime support, and ecosystem tooling. It
captures specific, evidence-backed experiences while project context is still
fresh. The record is meant to improve the language and its tools, not to grade
agents or produce a predetermined conclusion.

Report candidly. Useful feedback may be positive, negative, uncertain, or
critical of Moss's intended design. Do not soften a supported finding to make
Moss appear successful, and do not manufacture friction or praise to fill a
quota.

## Relationship to SWARM feedback

This system and the experimental SWARM protocol have different jobs:

- Ordinary project development records ongoing developer experience here.
- Dogfood, fresh-agent, language-surface, and swarm experiments follow
  [`examples/swarm/SWARM.md`](../examples/swarm/SWARM.md), including its
  `FEEDBACK.jsonl`, finding, and issue-classification workflow.
- When work is both project development and a SWARM experiment, follow both
  protocols. Keep each record in its canonical location and cross-link the
  relevant evidence or `SWARM-xxx` ID instead of copying a defect report into
  two competing databases.

An `agent-feedback` observation may reference an existing SWARM issue through
`related_issue`. It does not allocate a SWARM ID or replace SWARM's canonical
issue database.

## Who records feedback and when

Every agent working on Moss source, SDKs, compiler support, runtime support, or
ecosystem tooling must read this policy at startup and use it throughout the
work. Record a meaningful observation near the time it occurs, after enough
investigation to describe it honestly. Do not wait until project closeout and
reconstruct an entire experience from memory.

Keep the original observation after a bug is fixed or an assumption is
corrected. Record the eventual explanation in `resolution`, or add a later
observation that references the original ID when the follow-up itself is
important. A small number of meaningful observations is better than repetitive
boilerplate. There is no minimum count, and an agent that encounters nothing
noteworthy must not invent an entry.

## Repository organization

Raw records use JSON Lines: one complete JSON object on each nonblank line.
Store them by project and actual workstream, not by feedback category:

```text
agent-feedback/<phase>/<team-project>/<workstream>/observations.jsonl
```

For example:

```text
agent-feedback/phase24/team-t-text-encoding/utf8-codec/observations.jsonl
agent-feedback/phase24/team-t-text-encoding/performance/observations.jsonl
agent-feedback/phase24/team-i-integration/merge-validation/observations.jsonl
```

The `phase`, `project`, and `workstream` fields must match those three path
components exactly. Use stable lowercase kebab-case names. A workstream names
the task context, such as `utf8-codec` or `cross-package-integration`; it is not
a category such as `surprise` or `tooling-positive`. Create a workstream
directory only when it has an observation. Do not pre-create empty team or
category directory trees.

Reusable illustrative records live in
[`examples/observations.jsonl`](examples/observations.jsonl) and are exempt from
project-path matching because they are examples, not project evidence.

## Observation categories

Choose the category that best describes the primary experience. Categorization
is independent of directory organization.

### Language expressiveness

Use `language-friction` when legal Moss made an abstraction difficult or
cumbersome to express: a missing construct, unexpected inference constraint,
ownership-driven restructuring, generic limitation, or material boilerplate.
Use `language-positive` for a specific language property that made the work
clearer or more concise, such as effective inference, a clean pipeline, useful
specialization, or a compiler-detected ownership mistake.

Separate a demonstrated language limitation from agent unfamiliarity. Before
calling something a limitation, follow the repository's compiler-first gap
workflow: consult the skills and bootstrap surface, read structured diagnostics,
use semantic queries, minimize a probe, and verify native lowering when it is
relevant. A failed guessed spelling is not evidence that a capability is absent.

### Surprises

Use `surprise` when actual behavior differed materially from a reasonable
expectation. State the expected behavior, observed behavior, and eventual
explanation when known. A corrected agent assumption can be valuable feedback;
label it honestly rather than recasting it as a language defect.

### Debugging and ecosystem bottlenecks

Use `debugging-bottleneck` when diagnostics, lowering, Fast Debug, traces, or
the investigation path materially delayed diagnosis. Use `ecosystem-friction`
for package management, documentation, cross-package integration, ABI, build,
or surrounding workflow friction.

An ordinary algorithm error is not a language or ecosystem problem. Record it
only when Moss or its ecosystem materially changed how the defect was found,
diagnosed, or repaired, and explain that connection.

### AI-friendly tools

Use `tooling-positive` or `tooling-negative` for tools an agent actually used,
including bootstrap and capability discovery, structured diagnostics, `inspect`,
`type`, `effects`, `ownership`, `calls`, `why`, `cost`, `impact`, Fast Debug,
structured tracing, Margo, repository-local skills, or documentation discovery.

Name the tool and explain how it helped or failed to help. Evidence might include
a command, diagnostic code, query result, reduced reproducer, or avoided test
scope. Availability alone is not evidence of usefulness. A negative record may
say that a tool returned insufficient or misleading information; uncertainty is
acceptable when the record says what remains unknown.

### Positive Moss experiences

Positive observations use `language-positive`, `tooling-positive`, or, when
appropriate, `performance-observation`. They require the same specificity as
negative observations. Good examples include concise inferred types, clean
functional pipelines, straightforward imported specialization,
compiler-detected ownership errors, useful inferred synchronization, agreement
between Fast Debug and native execution, actionable structured diagnostics, or
measured native performance. Avoid general praise without a concrete task and
evidence.

### Performance and development workflow

Use `performance-observation` for measured language- or ecosystem-induced
performance behavior: allocation or copying costs, compiler/toolchain
bottlenecks, optimization wins, or useful native performance. Record the
workload, profile, command, and measurement method when making quantitative
claims. Do not fabricate timing, extrapolate from an incomparable baseline, or
label a poor algorithm choice as a compiler performance defect.

## Record contract

[`SCHEMA.json`](SCHEMA.json) is the canonical JSON Schema. The current record
version is `1.0`. Every record contains:

- `schema_version`: the schema version used by this line.
- `id`: a repository-unique, stable observation ID.
- `phase`, `project`, `workstream`: project identity matching the path.
- `agent_role`: the engineering role that encountered the experience.
- `category`: one of the categories above.
- `task`: what the agent was trying to accomplish.
- `observation`: what happened.
- `impact`: the development consequence, including uncertainty when relevant.
- `evidence`: a nonempty array of repository-relative paths, exact commands,
  diagnostic codes, query results, or commit references.
- `resolution`: an object with `status` (`resolved`, `unresolved`, `workaround`,
  or `explained`) and a concise `summary`.
- `commit`: the related full 40-hex commit SHA, or `null` when one is not yet
  available.

Optional fields can add severity, tool experience, a durable reproducer,
suggestions, tags, related issues, or a comparison language. Omit an unknown
optional field or use `null` only where the schema permits it. Never invent
elapsed time, benchmark numbers, or quantitative impact.

IDs should remain meaningful across merges. A practical form is
`<phase>-<project>-<workstream>-<sequence>`, for example
`phase24-team-t-text-encoding-utf8-codec-001`. Do not reuse an ID for a new
observation after resolution.

Schema versions are explicit so old observations remain interpretable. A
compatible clarification may retain the current version. A structural or
semantic contract change introduces a new version, updates the schema and
validator together, documents migration or continued support, and does not
silently rewrite historical records. The validator must continue to identify
the version of every line and reject only structurally unsupported versions,
not honest critical or incomplete optional content.

## Evidence and privacy

Evidence should let another engineer understand or reproduce the experience.
Prefer durable repository paths and exact commands. Include structured
diagnostic codes or semantic-query facts where applicable. Link a minimized
reproducer when the observation asserts a defect. Commit hashes may be added
when available, but lack of a commit at the moment of discovery is not a reason
to postpone the record.

Do not commit credentials, access tokens, private remote URLs, personal data,
machine-specific secrets, or sensitive local paths. Summarize or redact such
details without weakening the technical evidence. Do not rely on disposable
`tmp/` artifacts as the sole evidence for a durable claim.

## Working and closeout workflow

1. At project startup, read this policy and identify the project's phase,
   project name, and real workstreams.
2. During implementation, record only meaningful experiences as they occur.
3. Investigate enough to distinguish language behavior, tooling behavior,
   ecosystem friction, performance, and ordinary implementation mistakes.
4. Preserve the original line; update resolution metadata or add a linked
   follow-up rather than deleting inconvenient history.
5. Before integration, run `python3 tests/tooling/check_agent_feedback.py`.
6. At project closeout, review records for duplicates, unsupported claims,
   missing positive evidence, and accidental secrets. Write a project summary
   from [`templates/SUMMARY.md`](templates/SUMMARY.md) when the project requires
   one; the summary supplements rather than replaces raw observations.

Project coordinators own directory consistency, deduplication within their
project, and closeout validation. Reviewers should challenge classification and
evidence without erasing the original account. Repository maintainers own schema
evolution and cross-project analysis. No reviewer should reject feedback merely
because it is critical, uncertain, negative, or challenges Moss's design.
