# Swarm feedback observations

Every fresh-agent, swarm, dogfood, or language-surface experiment creates an
`examples/swarm/<experiment>/FEEDBACK.jsonl` file. It is the authoritative
inventory of what independent agents and workloads observed. `REPORT.md` and
`COORDINATOR_REPORT.md` remain useful narrative summaries, but are not a
substitute for this structured evidence.

Copy the record shape in `OBSERVATION_TEMPLATE.json`. One nonblank JSONL line is
one complete, atomic observation. Record it as it occurs, rather than trying to
reconstruct it from a commit message or memory at the end of the experiment.

Use an experiment-local, stable observation ID, for example
`15.12A-parser-001`, `15.12A-parser-002`, `15.12D-geolib-001`, or
`15.12D-geolib-002`. These are not SWARM IDs: an observation describes what one
independent agent/workload saw. Multiple independently discovered observations
may later support one canonical finding:

```text
15.12D-geolib-014
15.12D-text-toolkit-009
15.12D-calc-021
        |
        +--> SWARM-061
```

Set `discovered_naturally` to `true` only when the issue arose while doing the
assigned workload. Set it to `false` for a deliberate probe or follow-up. Keep a
minimized reproducer derived from a natural failure on the original observation;
it is not a second independent natural discovery.

Preserve the first-attempt application evidence before applying a workaround.
For natural defects, strongly prefer both `natural_source` (where the programmer
hit it) and `reproducer` (the smallest durable case). Both path fields are
repository-relative, must not point into `tmp/`, and may be `null` for a minor
documentation or ergonomics observation. Never rely on evidence that exists only
under `tmp/`.

Use the controlled `classification` vocabulary from the schema; an observation
is not automatically a compiler bug. Run every applicable execution mode and
write every matrix field. Use `not_tested` or `not_applicable`, never an omitted
field or an informal spelling.

The only controlled values are:

- `classification`: `language_rule`, `agent_misunderstanding`, `ergonomics`, `documentation`, `diagnostic`, `frontend`, `formatter`, `ownership_effect`, `specialization`, `native_lowering`, `fast_debug`, `module_interface`, `package_tooling`, `semantic_tooling`, `compiler_crash`
- `severity`: `crash`, `wrong_result`, `correctness`, `parity`, `usability`, `documentation`
- `confidence`: `candidate`, `reproduced`, `confirmed`, `rejected`
- each matrix field: `pass`, `fail`, `unsupported`, `not_tested`, `not_applicable`

Fresh agents normally leave `existing_swarm` as `null` until their independent
experiment is substantially complete. Do not read the findings ledger first just
to force observations into existing buckets. During coordinator closeout,
independent reproductions are linked to the canonical `SWARM-xxx` finding, new
validated defects may receive an ID, and rejected observations use
`"confidence": "rejected"` with an explanatory note. Do not place several
SWARM IDs in one observation unless there is no cleaner decomposition.

All record keys are required so inventories have a predictable shape. The
`natural_source`, `reproducer`, `diagnostic`, `workaround`, and `notes` fields
may use `null` when inapplicable. The matrix is mandatory even for
documentation/ergonomics observations; such records will usually contain mostly
`not_applicable` values.

The `baseline_commit` field is also required. Set it to the full 40-hex Git
commit SHA of the repository revision against which the observations were first
established. Obtain it at experiment start:

```sh
git rev-parse HEAD
```

Reuse that baseline for all observations produced in one experiment run. Do not
change `baseline_commit` merely because a coordinator later links the observation
to a SWARM finding; it records when, not how the observation was categorised.

```text
Experiment begins
    ↓
copy observation structure
    ↓
write FEEDBACK.jsonl as observations occur
    ↓
finish natural workload
    ↓
minimize credible failures
    ↓
coordinator validates/deduplicates
    ↓
link observations to SWARM-xxx
```

Validate all tracked experiment inventories with:

```sh
python3 tools/check_swarm_feedback.py
```
