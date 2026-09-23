# Moss Swarm Experiment Rules

This file is the canonical operating contract for Moss dogfood, fresh-agent, language-surface, and swarm experiments.

Experiment-specific prompts define only:

- the area to explore;
- the workloads to build;
- any experiment-specific constraints.

This file defines the common discovery, feedback, classification, coordination, and closeout process.

Future agents should follow this file rather than relying on copied instructions from previous prompts.

## Worker-agent rules

### Start independently

Before experimentation:

1. Follow `AGENTS.md`.
2. Follow the canonical Moss agent workflow/skills.
3. Bootstrap compiler capabilities normally.
4. Read the experiment-specific assignment.

Do NOT begin by reading:

- `examples/swarm/FINDINGS.md`
- `examples/swarm/ISSUES.jsonl`
- prior experiments intended only for comparison

unless the experiment prompt explicitly requires it.

The purpose is to preserve independent discovery.

### Maintain structured observations

Every swarm experiment must maintain:

`examples/swarm/<experiment>/FEEDBACK.jsonl`

Use the canonical contract in:

- `examples/swarm/feedback/README.md`
- `examples/swarm/feedback/OBSERVATION_TEMPLATE.json`
- `examples/swarm/feedback/schema.json`

Record observations during the experiment, not only during closeout.

Preserve:

- first natural failure;
- `discovered_naturally`;
- baseline commit;
- original workload source;
- minimized reproducer;
- execution matrix;
- diagnostic;
- workaround, if any.

Do not allocate SWARM or EXPRESS IDs as an independent worker.

Do not force observations into known categories before independent work is substantially complete.

## Natural discovery vs deliberate probing

A naturally encountered failure is different evidence from a deliberately constructed probe.

Set:

`discovered_naturally: true`

only when the issue appeared while completing the assigned workload.

A minimized reproducer derived from that failure remains attached to the same natural observation.

Do not count minimization as another independent discovery.

## Coordinator rules

After independent worker experimentation is substantially complete, the coordinator may read:

- all experiment `FEEDBACK.jsonl` files;
- `examples/swarm/FINDINGS.md`;
- `examples/swarm/ISSUES.jsonl`;
- related historical swarm reports.

The coordinator is responsible for canonicalization.

### Coordinator sequence

1. Gather all structured observations.
2. Validate evidence and reproductions.
3. Deduplicate repeated observations.
4. Compare validated observations with existing SWARM findings.
5. Amend existing SWARM findings when appropriate.
6. Allocate new SWARM IDs only for genuinely distinct validated findings.
7. Update the canonical issue database.
8. Produce the coordinator report.
9. Update phase/status bookkeeping.
10. Run all swarm validators.

## Canonical tracking layers

The swarm system has three levels:

```text
FEEDBACK.jsonl
    individual observations
          ↓
FINDINGS.md
    deduplicated empirical SWARM findings
          ↓
ISSUES.jsonl
    engineering/design classification and disposition
```

Do not collapse these layers.

`FEEDBACK.jsonl` is evidence.

`FINDINGS.md` says what recurring/distinct problem was empirically found.

`ISSUES.jsonl` says what kind of engineering/design problem it is and what class of action is appropriate.

## Canonical semantic issue categories

Every semantic issue must use exactly one of:

### 1. `false_acceptance`

The frontend accepts source that is not legal under settled Moss semantics.

Normal disposition:

`reject_earlier`

The solution is a Moss diagnostic, not accidental implementation of the unsupported feature.

### 2. `ambiguous_spec`

The repository does not yet establish one authoritative language meaning.

Normal disposition:

`specify_then_fix`

Make the semantic decision before repairing implementations.

### 3. `lost_semantics`

The source has a settled legal Moss meaning, but a later compiler representation or execution engine fails to preserve it.

This includes failures in:

- type inference/elaboration;
- ownership/effects;
- specialization;
- functional IR;
- modules;
- `.mossi`;
- test lowering;
- Fast Debug;
- native lowering/codegen.

Normal disposition:

`preserve_semantics`

Core invariant:

> Once Moss accepts a source construct with an authoritative meaning, every subsequent compiler representation must preserve that meaning.

### 4. `missing_expressiveness`

A useful programming abstraction cannot be represented directly in legal Moss without materially redesigning the program.

Normal disposition:

`design_needed`

Do not confuse a compiler defect or inconvenient syntax with Missing Expressiveness.

A useful test is:

> Can the same abstraction be expressed in different legal Moss syntax, or must the programmer change the architecture?

Only the latter is a Missing Expressiveness candidate.

## Deliberate non-goals

The following are deliberate Moss design exclusions and MUST NOT be promoted to Missing Expressiveness merely because another source language used them:

- runtime dynamic dispatch;
- runtime trait objects;
- escaping closures / general runtime function values;
- recursion.

Agents should adapt such workloads to Moss's intended model.

For example, `Vector[Trait]` is not a request to add runtime trait objects. If the frontend accepts it, that is a False Acceptance issue.

## Error handling

Recoverable error propagation is tracked as Missing Expressiveness but is deferred to:

`Phase 21 — Error Propagation & Supervision`

Do not design exception/Result/supervision semantics during ordinary dogfood swarms unless explicitly assigned to that phase.

Assertions, preconditions, diagnostics, and expected-failure testing may still generate narrower findings.

## Missing Expressiveness promotion rule

Worker observations should not automatically become `EXPRESS-*` issues.

The coordinator should promote a new `EXPRESS-*` issue only when evidence establishes that:

1. the desired abstraction is useful/reasonable;
2. current legal Moss cannot express that abstraction directly;
3. the workaround materially redesigns the program rather than merely changing syntax;
4. it is not one of Moss's deliberate non-goals;
5. it is not actually a compiler bug or ambiguous semantic rule.

Use the next monotonic `EXPRESS-*` ID.

## Tooling-only issues

Pure formatter, diagnostic, editor, query, orchestration, or workflow issues may remain:

`tracking_scope: tooling`

with:

`category: null`

Do not invent a fifth semantic category.

## Canonical issue database

The canonical issue database is:

`examples/swarm/ISSUES.jsonl`

Its schema/rules live under:

`examples/swarm/issue_tracking/`

Every new or materially changed open SWARM finding must be reflected in `ISSUES.jsonl`.

Every coordinator must classify new semantic findings there.

## Database completeness invariant

Strengthen `tools/check_swarm_issues.py` so it enforces:

> Every SWARM finding whose status begins with `Open` in `examples/swarm/FINDINGS.md` must have exactly one corresponding `issue_id` record in `examples/swarm/ISSUES.jsonl`.

The validator should fail if:

- an open SWARM has no issue record;
- an open SWARM has more than one canonical issue record;
- an issue record references a nonexistent SWARM;
- a semantic issue uses anything outside the four canonical categories.

Historical fixed/rejected SWARMs do not need to be imported into `ISSUES.jsonl` unless explicitly desired later.

Add a validator self-test for a missing open SWARM classification.

## Coordinator report

Every completed swarm should produce a coordinator synthesis report.

The report should summarize:

- workloads attempted;
- what worked;
- natural friction encountered;
- intended language restrictions;
- new vs repeated findings;
- parity matrix where applicable;
- new/updated SWARM IDs;
- issue-category assignments;
- Missing Expressiveness candidates promoted or rejected;
- overall conclusion.

Do not treat workarounds as Moss architectural recommendations until the underlying issue has been classified.

## Closeout validation

At minimum, every coordinator must run:

```sh
python3 tools/check_swarm_feedback.py
python3 tools/check_swarm_issues.py
git diff --check
```

Run experiment-specific Moss/Margo validation as appropriate.

A swarm is NOT closed when:

- a new observation exists only in prose or a commit message;
- a distinct validated defect lacks a SWARM allocation;
- an open SWARM is missing from `ISSUES.jsonl`;
- a newly promoted expressiveness issue is absent from `ISSUES.jsonl`;
- the coordinator report and canonical databases disagree.

## Authority

For swarm process mechanics, this file is the canonical source.

Experiment prompts should reference this file rather than restating its contents.

If swarm process rules need to change, update `SWARM.md`, its validators/templates where necessary, and then let future experiments inherit the new rules.
