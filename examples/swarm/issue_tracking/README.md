# Moss dogfood issue database

`../ISSUES.jsonl` is the canonical engineering/design classification layer for
dogfood discoveries. It sits above the raw experimental observations in
`FEEDBACK.jsonl` and the detailed, deduplicated SWARM ledger in
`../FINDINGS.md`:

```text
FEEDBACK.jsonl                  experimental observations
        ↓
FINDINGS.md                     canonical SWARM findings and reproducers
        ↓
ISSUES.jsonl                    classification and disposition
```

`ISSUES.jsonl` answers: “What kind of problem is this, and what are we
supposed to do about it?” It intentionally keeps detailed reproducers and
diagnostics in `FINDINGS.md`; `swarm_ids` and `related_issue_ids` provide the
links to that evidence and to cross-cutting design items.

## Decision model

```text
Does the source violate settled Moss rules?
        |
       yes
        ↓
FALSE ACCEPTANCE — reject earlier

Otherwise, is the rule itself unsettled?
        |
       yes
        ↓
AMBIGUOUS SPEC — decide semantics

Otherwise, is the intended abstraction impossible to express?
        |
       yes
        ↓
MISSING EXPRESSIVENESS — decide whether to extend Moss

Otherwise
        ↓
LOST SEMANTICS — repair the compiler so accepted meaning survives
```

The semantic categories are exhaustive for records with
`tracking_scope: "semantic"`:

- `false_acceptance`: illegal source passes checking; normally reject it with a
  Moss diagnostic. This does not imply adding the rejected programming model.
- `ambiguous_spec`: existing behavior and documentation do not establish one
  authoritative meaning; make the language decision first.
- `lost_semantics`: legal source has an authoritative meaning, but a compiler
  or toolchain representation fails to preserve it.
- `missing_expressiveness`: a useful abstraction cannot be expressed directly
  in legal Moss without materially redesigning the program.

Records with `tracking_scope: "tooling"` have `category: null` and are tracked
with `disposition: "tooling_only"`; tooling is a scope, not a fifth semantic
category.

## Deliberate v0.1 non-goals

Dynamic dispatch, runtime trait objects, escaping closures/general runtime
function values, and recursion are deliberate Moss v0.1 non-goals. A translated
source language using one of these models must be adapted and recorded as an
intentional language difference, not entered as `missing_expressiveness`.

In particular, `Vector[Trait]` is not a feature request: runtime trait objects
are excluded, so accepting it is a `false_acceptance` issue.

## Error handling deferral

Recoverable error propagation is worth tracking as missing expressiveness, but
its design is deferred. `EXPRESS-007` is `deferred` to **Phase 21 — Error
Propagation & Supervision**. This database does not decide exception or
`Result` semantics. SWARM-040 separately tracks the unsettled v0.1 contract for
ordinary assertions/preconditions and links to that deferred expressiveness
item.

## Record contract

The machine-readable contract is [schema.json](schema.json). IDs beginning
with `SWARM-` reference headings in `../FINDINGS.md`; `EXPRESS-` IDs are stable
cross-SWARM design items. Validate both the issue database and its references
with:

```sh
python3 tools/check_swarm_issues.py --self-test
python3 tools/check_swarm_issues.py
```
