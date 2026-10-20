# Phase 25.1 execution status

Updated: 2026-10-20 20:02 UTC  
Branch: `phase25-1-work`  
Pinned instruction commit: `3de64dc633312267c37c4e4990b28c2a73cab104`

## Gate status

| Gate | Status | Evidence |
|---|---|---|
| T0 | PASS | Annotated tags published and remote peeled commits verified below; `origin/main` unchanged. |
| R0 | PASS | Final independent GO at frozen checkpoint `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4`; 19-case oracle, complete 71-path harness map, strict corpus and P2 inventory exception recorded. |
| R1 | IN PROGRESS | Independent GO at architecture SHA `ff4470ffdcc93cedb3d66b1e068b7f5ea8ca3f5f`. Current pushed base `b2b12b94`; uncommitted continuation adds statement-call and pipeline-stage capture. Full source-free hostile/v9 corpus and generic collection facts pass. Exact-SHA review, total structural coverage and shared lowerer remain open. |
| R2–R5 | NOT STARTED | ABI-v9 remains default; no `main` or `phase25-integration` changes. |

## T0 transcript and verification

Starting checkout: clean `phase25-1-work` at pinned instruction commit `3de64dc...`; local authorization clarification commit `fcd91dab` was then made on the task branch without pushing a branch. Release lineage checks passed:

- `origin/main` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- `origin/recovery/pre-phase25-main` = `fa3a340a0a6652568b97869489da1a84038b9857`
- `origin/releases/phase25-delivered-v9` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- Both target commits exist locally. Neither tag name existed locally or remotely before creation.

Commands executed, in order:

```sh
git fetch --prune --tags origin
git ls-remote origin refs/heads/main refs/heads/recovery/pre-phase25-main refs/heads/releases/phase25-delivered-v9
git cat-file -e 'fa3a340a0a6652568b97869489da1a84038b9857^{commit}'
git cat-file -e 'dd45e30722bcc2d62d4bb46ba184d426cbaea066^{commit}'
git tag -a phase25-pre-main fa3a340a0a6652568b97869489da1a84038b9857 -m "Phase 25 pre-main recovery baseline"
git push origin refs/tags/phase25-pre-main
git tag -a phase25-delivered-v9 dd45e30722bcc2d62d4bb46ba184d426cbaea066 -m "Phase 25 delivered ABI-v9 baseline"
git push origin refs/tags/phase25-delivered-v9
git ls-remote origin 'refs/tags/phase25-pre-main^{}' 'refs/tags/phase25-delivered-v9^{}'
git ls-remote origin refs/heads/main
```

Remote peeled-tag verification:

- `phase25-pre-main^{}` = `fa3a340a0a6652568b97869489da1a84038b9857`
- `phase25-delivered-v9^{}` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- Post-push `origin/main` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`

Both refs are annotated tags. Tag object SHAs: `phase25-pre-main` = `c899b084e6cdefe9fcc7cf40af6555e281ef3a02`; `phase25-delivered-v9` = `71ce8fbd2a7efa300f51e925787b1f8b90cc4ea2`. T0 passed before R0 work began. The branch authorization wording in `AGENTS.md` is clarified in commit `fcd91dab`; that file already permitted explicitly authorized normal pushes and contained no prohibition.

## Next work

## R0 inventory, oracle and review checkpoint

Inventory and oracle are at compiler SHA `43973e1adfdf015d5588e85f41d986925a85a565` (before the R0 evidence commit). `tests/phase25/compatibility/inventory.py` enumerated 26 production package roots: 25 package builds passed and `packages/time` failed at `time__pad_digit` (inferred `char`, annotated `string`). The successful builds contain four open generic exports: `bytes.from_range` and `collections.index_of`, `contains`, `count_equal`. Exact exported facts, artifact hashes and harness path candidates are in `docs/phase25/PHASE_25_1_GENERIC_INVENTORY.json`. The package failure remains P251-GEN-008 and is not counted as an empty export list.

The source-backed/v9/v10 oracle ran six corpus workloads plus the real `bytes.from_range` / Range-copy consumer in Debug and Release. Source-backed and v9: 14/14 profiles passed; v10: 10/14 profiles passed for already supported parameter-return, scalar literal, field-read, direct private call and collection loop; generic Vector iteration and actual bytes returned `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` for both profiles. Passing v10 rows removed provider source, poisoned legacy body records and required a checked-projection trace. The range-as-helper-argument negative probe passed as `invalid-by-language` with `MOSS_COMPILE_ERROR`.

Additional evidence passed: `check_phase25_m2b2_source_free_corpus.py` (including all artifact mutation negatives and v9 controls), the 14-provider advanced-data source-free v9 gate (5 Team A providers; no provider source), foundations `validate_source_free.py` (actual Range-copy output `329`), collections source-free v9 smoke, and D4 actual SDK checked-body source-free tests for all three collection callables (legacy removals 7/7/8). The existing corpus gate was corrected to validate unsupported vector iteration at source-free consumer selection when the provider emits an ABI-v10 interface.

The R0 architecture note proposes one checker-retained typed traversal/codec/projector and preserves ABI-v9 as default; it adds no compiler implementation. `adversarial_architect` is the only additional team member and is assigned to inspect the exact pushed R0 SHA before R1. Feedback validation passed. R0 review and checkpoint delivery remain unfinished. No R1 implementation has started. All scratch output is under `tmp/`.


## R0 final review and R1 entry

The final adversarial review at frozen commit
`9c90573bda8daec5bd4254cc5f6c64c5dd3239e4` returned **GO**, with no open P0/P1.
The reviewer verified the Phase 24 generic source-free callables, Team A and
typed semantic API map entries, one-to-one disposition for all 71 discovered
candidates, corrected 19-case wording, closed GEN-006, and the native-only fs
classification. P251-GEN-008 (`packages/time`) remains P2 on the R4 path.

## R1 architecture review and Queue baseline — 2026-10-20

The first R1 architecture checkpoint `84bdaf4b0758bc523a5c8fc49a542a1cecaf2dc3`
received NO-GO with one P1: no exhaustive source-to-facts/IR mapping or
per-occurrence completeness invariant; Queue construction was unprobed. The
amended architecture at `ff4470ffdcc93cedb3d66b1e068b7f5ea8ca3f5f` received
independent GO with no open P0/P1. The totality ledger and acceptance invariant
are in `PHASE_25_1_R1_ARCHITECTURE.md`; the historical R0 proposal's status was
corrected. Reviewer P2 required Queue source-backed plus ABI-v9 source-free
evidence before classification.

Queue baseline passed at compiler source commit
`c27350b6a9d521eba30858a6aa47095f9c8bde63` (no compiler source edits between
the R0 checkpoint and probe): source check and interpreter output `9`; native
source-backed Debug/Release output `9` and `moss`; ABI-v9 provider source was
physically deleted before consumer Native Debug/Release, which output `9` and
`moss`. Exact source and commands are preserved in
`PHASE_25_1_R1_BASELINE_PROBES.json`. This classifies inferred `Queue()` plus
`push` as valid source-backed/v9, while v10 checked-body coverage remains
pending. The separate open-receiver Queue method rejection remains invalid.

No R1 compiler implementation or focused codec change has started. Next map
all existing expression-checker recursion/operation facts and determine how
the ordinary checker can retain occurrence-linked typed nodes without a
second expression parser or specialization engine; then implement the common
IR path and preserve the admitted H3/D1-D4/checked-arithmetic regressions.

### R1 shared decoder checkpoint — pending commit

`decode_checked_body_v1` now provides one bounded checked-body-v1 import entry
point and ABI-v10 `import_checked_body` calls it without selecting H3,
collection-loop, or empty-vector decoders from summary shape. It currently
uses the shared bounded reader's union of admitted grammars; historical narrow
decoder APIs remain for compatibility tests and old-reader rejection proof.
The provider still has shape-specific lowerers and emitters, so this is a
partial R1 implementation checkpoint, not R1 acceptance.

Validation on this source revision:

- `make --jobs="$(nproc)" moss-dev` passed (existing missing-field-initializer
  warnings from `TypeSolver::Undo::binding` remain).
- `python3 tests/tooling/check_phase25_d2_collection_loop_codec.py` passed; the
  new unified reader round-trips the collection-loop body and rejects a bad
  semantic hash, while the historical H3 decoder still rejects Count/Index.
- `python3 tests/tooling/check_phase25_empty_vector_records.py` passed with the
  unified reader round-tripping the empty-vector body.
- `python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py` passed:
  checked-body positives executed after provider-source deletion and legacy
  statement poisoning; malformed summary/body/hash/provenance/dependency cases
  rejected; ABI-v9 compatibility controls passed.
- `python3 tests/tooling/check_phase25_h3_direct_records.py`,
  `check_phase25_h3_arithmetic_decode.py`,
  `check_phase25_checked_body_ir.py`,
  `check_phase25_generic_collection_facts.py`,
  `check_phase25_d3d_collection_source_free.py`, and
  `check_phase25_d4_collection_source_free.py` passed.
- No full `make check` was run at this partial checkpoint.

Next: finish occurrence-linked checker facts and the common structural body
lowerer, then repeat the controls plus the focused source-free and
hostile-artifact corpus at a frozen commit before the required independent
foundational R1 implementation review.

### R1 shared callable-record builder — pushed implementation checkpoint

Checkpoint `9db537ae900a0a802c32936c2eed71c759a5b7ea` is pushed to
`origin/phase25-1-work`.

One `append_abi_v10_checked_callable_records` path now validates a summary/body
identity pair, validates both ABI records, rejects duplicate callable summaries,
bodies and provenance IDs, and inserts the pair atomically into the public or
private summary section. Existing checked constructor, scalar-return,
arithmetic, empty-vector, collection-loop and private-helper-chain producers
use this path. Their shape-specific body lowerers and the generic provenance
inventory remain as they were; this is consolidation progress, not R1
acceptance.

Validation on this exact source revision:

- `make --jobs="$(nproc)" moss-dev` passed with only the existing
  `TypeSolver::Undo::binding` initializer warnings.
- `python3 tests/tooling/check_phase25_h3_direct_records.py` passed.
- `python3 tests/tooling/check_phase25_h3_arithmetic_decode.py` passed.
- `python3 tests/tooling/check_phase25_d2_collection_loop_codec.py` passed,
  including old H3 rejection for the collection tags and pre-D2 no-fallback
  rejection.
- `python3 tests/tooling/check_phase25_empty_vector_records.py` passed.
- `python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py` passed all
  admitted source-free positives, legacy-record removal, malformed artifact
  negatives, dependency closure and ABI-v9 controls.
- `python3 tests/tooling/check_agent_feedback.py` and `git diff --check` passed.

R1 remains IN PROGRESS: checker-owned occurrence facts, one structural body
lowerer and removal of shape-specific body projection/admission are not yet
implemented. No R1 acceptance review has been requested at this partial
checkpoint.

### R1 checker occurrence ledger — partial implementation checkpoint

This checkpoint adds statement-local expression
occurrence identities, parent/child ordering and explicit root slots in the
ordinary checker pass. It links the existing open aggregate-operation facts
and equality-operator requirements to the active occurrence, and handles
function/object-method result expressions with a synthetic return root after
`check_stmts`. IDs are checker-local and must not enter wire identity. This is
metadata scaffolding, not the required typed IR or a completeness proof; node
types, resolved target identities, binding/control-flow facts, and a unified
tree-to-record lowerer remain missing.

Validation after the latest source edits:

- `make --jobs="$(nproc)" moss-dev` passed; only the existing
  `TypeSolver::Undo::binding` initializer warnings were emitted.
- `python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py` passed all
  admitted source-free positives, malformed-artifact negatives, dependency
  closure, and ABI-v9 controls.
- `python3 tests/tooling/check_phase25_h3_direct_records.py` passed.
- `python3 tests/tooling/check_phase25_d3d_collection_source_free.py` passed
  actual SDK `index_of` Int/String execution with legacy-body poison.
- `python3 tests/tooling/check_phase25_d4_collection_source_free.py` passed
  actual SDK collection source-free execution and bad-requirement/effect
  rejection.

R1 remains IN PROGRESS. Next extend checker-owned facts to typed nodes, call
targets, members, bindings and control flow; implement one generic structural
lowerer/record path; preserve fail-closed admission and ABI-v9 behavior.

### R1 occurrence-capture correction — current worktree

The independent architect reviewed pushed commit `22c05010` and returned
NO-GO for using its visit graph as a lowering tree. Findings: range loops had
no required root, root checks omitted nested statements, comparison checking
created duplicate operand visits, and the graph lacked node kinds/source
identity. The current worktree changes range validation to the roots actually
captured, validates roots for all checker-registered statements (including
nested blocks), and removes the duplicate comparison traversal. It additionally
retains checker-selected categories, checked TypeRefs and direct resolved
callable IDs where available. These edits do not resolve source occurrence
identity/roles or complete structural coverage; the graph remains unusable as
the generic lowering input until those are addressed. Do not interpret this as
a GO review or R1 completion.

Validation on the current worktree: compiler build, full M2b2 source-free
hostile/v9-control corpus, H3 direct import, D3d SDK `index_of`, and D4 SDK
collection source-free/hostile checks passed. The R1 gate remains open; this
correction has not yet been committed or independently reviewed.

The reviewer’s follow-up on `dcb912b3` confirmed the earlier root and duplicate
visit defects were fixed, but found a repeated ordering-category mark, a
nested-call target attribution hazard, and the unmarked enum-case constructor
category. This correction checkpoint removes the ordering-pass mark,
returns the selected callable only to the syntax-owning direct-call branch,
adds explicit enum constructor capture, and labels recursive children by their
checker roles (left/right, receiver, argument, field, container/index, and
pipeline seed/source). M2b2 passed after these changes and the compiler build
is green. A fresh exact-SHA review remains pending; source occurrence coverage,
member target/resolution facts, local binding identity lowering, and a common
tree emitter are still open.

### R1 statement invocation and pipeline-stage capture — working checkpoint

The continuation now gives statement-form direct and member calls a single
invocation occurrence with checked argument children. Direct functions retain
the checker-selected callable ID; open generic callable parameters retain a
parameter ordinal (or a local binding label); member calls retain a resolved
method/builtin-method identity. Member expression validation still runs, with
its duplicate internal occurrence walk suppressed. Functional pipeline stages
are children of their checker-visited pipeline node and retain the stage kind,
source operands, input/output types, and the selected named callable,
bound method, placeholder, or generic parameter identity.

Validation on the current uncommitted source revision:

- `make --jobs="$(nproc)" moss-dev` passed; existing `TypeSolver::Undo::binding`
  missing-initializer warnings remain.
- `python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py` passed the
  ABI-v10 positives, legacy poisoning, hostile records, dependency closure,
  ABI-v9 controls, and the generic-range fail-closed case.
- `python3 tests/tooling/check_phase25_generic_collection_facts.py` passed.
- `python3 tests/tooling/check_phase25_h3_direct_records.py`,
  `check_phase25_d3d_collection_source_free.py`, and
  `check_phase25_d4_collection_source_free.py` passed on the final source.
- `python3 tests/tooling/check_agent_feedback.py` and `git diff --check` passed
  on the final source.

This is capture progress only. The occurrence ledger still does not establish
that every source expression has exactly one node, the checker has not
deduplicated repeated semantic visits generally, and no common checked-body
tree lowerer/record emitter exists. R1 remains open and the ledger must not be
used for lowering yet. Freeze, rerun the listed gates, then obtain the
independent exact-SHA review before proceeding.
