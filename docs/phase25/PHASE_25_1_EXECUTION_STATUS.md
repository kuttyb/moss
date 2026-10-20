# Phase 25.1 execution status

Updated: 2026-10-20 20:02 UTC  
Branch: `phase25-1-work`  
Pinned instruction commit: `3de64dc633312267c37c4e4990b28c2a73cab104`

## Gate status

| Gate | Status | Evidence |
|---|---|---|
| T0 | PASS | Annotated tags published and remote peeled commits verified below; `origin/main` unchanged. |
| R0 | PASS | Final independent GO at frozen checkpoint `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4`; 19-case oracle, complete 71-path harness map, strict corpus and P2 inventory exception recorded. |
| R1 | IN PROGRESS | Independent GO at architecture SHA `ff4470ffdcc93cedb3d66b1e068b7f5ea8ca3f5f` and capture correction SHA `11fbf47e210af8dad158039dbf6a458709af4f64`. Pushes `783f47ea`, `246f51c6`, and `7a71f95c` received NO-GO; the exact `11fbf47e` review closed the member dispatch and role-verifier findings with no open P0/P1/P2. Total structural coverage and shared lowerer remain open. |
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

### R1 exact-SHA review and correction — working tree after `783f47ea`

The independent architect reviewed pushed `783f47eaec6f14519b7e75e70a0c57f0872a3bdc`
and returned NO-GO with four P1 findings: statement invocation argument links
did not match the root validator; void statement calls were made incomplete by
the missing result TypeRef; expression-position open generic callbacks lacked
parameter/Callable links; and generic pipeline sources returned before stage
capture. The review also required a selected member target/requirement link and
an explicit completeness invariant for calls, members and stages.

Current working correction: statement call validation now checks the
`call-invocation` root and unique `call-argument:<i>` children, recording
non-expression builtin operands separately. Void invocation nodes may complete
without a value TypeRef. Expression-position generic callbacks retain a
parameter ordinal and copy the matching Callable constraint. Generic-source
pipeline paths now retain deferred stage nodes and their operand payloads; they
remain incomplete when the ordinary checker has not resolved their concrete
input/output semantics. Resolved builtin, trait, nominal, generic method,
function, and functional stage paths attach their selected identity or
requirement links, and missing identities keep the ledger incomplete.

Validation on the corrected working tree:

- `make --jobs="$(nproc)" moss-dev` passed.
- `python3 tests/tooling/check_phase25_m2b2_source_free_corpus.py` passed all
  admitted checked-body positives, hostile-artifact negatives, dependency
  closure, generic-range fail-closed behavior and ABI-v9 controls.
- `python3 tests/tooling/check_phase25_generic_collection_facts.py` and
  `check_phase25_generic_arithmetic_requirements.py` passed.
- H3 direct records, D3d actual SDK `index_of`, D4 actual SDK collections,
  feedback validation and `git diff --check` passed.

This correction is not yet committed or independently reviewed. R1 remains
open; deferred valid generic pipeline semantics, source-to-occurrence totality,
single-visit semantics, binding/scope lowering and the shared structural body
lowerer are still work, not exclusions. No R2 work has started.

### R1 statement member-call correction — working tree

The architect reviewed pushed commit
`246f51c62d5b8351f4dcdc018bc326a596f43ec1` and returned NO-GO with one P1:
statement-form member calls discarded checker-selected method identity and
generic `Method` constraints while suppressing the receiver occurrence. The
working correction now routes `statement.text` through the ordinary expression
checker as the `call-invocation` root, which captures the receiver and arguments
and retains exact builtin, trait, nominal, or constrained generic dispatch
facts. The synthetic `method:<receiver type>.<name>` target and suppressed
duplicate walk were removed. The statement call path reads the resulting root
ID back from the checker root map for operand bookkeeping.

Validation on the corrected source: `make --jobs="$(nproc)" moss-dev` passed
with existing `TypeSolver::Undo::binding` warnings; the M2b2 source-free
hostile corpus; generic collection facts; generic arithmetic requirements;
H3 direct records; D3d and D4 actual SDK source-free controls; the feedback
checker; and `git diff --check` passed. One initial `-Werror` compiler-including
test caught signedness in the structural root-count verifier; the type was
corrected to `size_t`, and both compiler-including generic tests passed after
that edit.

This is an uncommitted capture correction, not R1 acceptance. The occurrence
ledger does not yet prove source-to-node totality or single-visit behavior, and
the common checked-body structural lowerer, full generic statement/expression
coverage, binding/scope lowering, and cross-crate source-free slice remain
unfinished. R2 has not started. Commit and push this correction for exact-SHA
adversarial follow-up before building further on the ledger.

### R1 checked occurrence return lowerer — working tree

The checker-retained expression occurrence tree now feeds checked-body-v1
lowering for single-return generic functions. This path reads resolved
occurrence TypeRefs, operation IDs and child roles, and can lower parameter and
scalar literal leaves, unary/binary operators, nominal field reads and vector,
queue or map index expressions. It verifies the result TypeRef, encodes and
decodes the canonical checked body, and stores its bytes/hash on the function.
The old dedicated parameter-return and scalar-return body constructors were
removed. ABI-v10's direct scalar producer now reads that retained checked body
instead of rebuilding those two forms. Other ABI-v10 producer/import paths
remain shape-specific and are still required for current fixtures; this is an
incremental lowering slice, not a claim that R1 is complete.

Validation after the lowering edits:

- `make --jobs="$(nproc)" moss-dev` passed; existing TypeSolver
  missing-initializer warnings remain.
- `check_phase25_m2b2_source_free_corpus.py` passed all current admitted
  positives, hostile artifact negatives, dependency closure, generic-range
  fail-closed behavior, and ABI-v9 controls.
- `check_phase25_generic_arithmetic_requirements.py` and
  `check_phase25_generic_collection_facts.py` passed.
- H3 direct checked import, D3d actual SDK `index_of`, and D4 actual SDK
  collections source-free tests passed, including bad requirement/effect
  rejection and legacy-body removal.
- `check_agent_feedback.py` and `git diff --check` passed.

This working slice is not independently reviewed yet. It does not lower
multiple statements, local bindings/scopes, constructors, generic calls,
member calls, collection literals, pipelines, or general control flow. The
occurrence totality/single-visit proof and consolidation/retirement of the
remaining shape-specific producers/importers are still open. R1 remains open;
R2 has not started.

### R1 direct-scalar admission correction — working tree

Independent review of `533215fb919997e908bdc17a3676015e90c2579f` returned
NO-GO with one P1: the broadened lowerer could produce Binary/Field/Index
bodies, while the direct-scalar record producer serialized only parameter and
result types/access/hash and omitted requirements, dependencies and effects.
For `return value * 2 + 1`, this producer also ran before the arithmetic
producer and could publish a conflicting requirement-free summary.

The direct-scalar producer now admits only the original safe record contract:
one open variable and one READ parameter; no operator/member requirements,
semantic dependencies or observable effects; and a Parameter or primitive
literal return node. More general checked-body nodes remain retained for future
unified producer work but cannot enter this incomplete summary route. After
this guard, `make --jobs="$(nproc)" moss-dev`, the full M2b2 positive/hostile/v9
corpus, generic arithmetic requirements, feedback validation and
`git diff --check` passed. Follow-up exact-SHA adversarial review is pending.

Review of the corrected checkpoint `0df08629efc045853b7ee9de3c12502243466032`
confirmed the scalar-summary P1 was closed, but found a second P1: the
collection-loop producer selected any `builtin:index` operation and rejected
a valid one-parameter indexed-return body as malformed loop summary. Its
admission now requires the checked Count initializer that identifies the
existing loop body. A targeted staged-v10 provider build for generic
`first(values): return values[0]` passed without `SOURCE_FREE_SUMMARY_INVALID`.
After this routing guard, the full M2b2 corpus and D4 actual SDK collections
source-free/mutation suite passed. Feedback validation and diff check remain
green. Exact-SHA adversarial review of this second correction is pending.

The architect reviewed exact pushed SHA
`62ff4975cde3268f91986e20aad478df8fe32559` and returned **GO**. The Count gate
excludes the one-parameter index return while preserving the existing loop
shape; its matcher requires exactly one Count and one Index operation. The
review found no other broad-trigger/narrow-validator collision for the
currently lowered Binary, Field or Index nodes. No P0/P1 findings remain for
this routing correction. R1 remains incomplete.

### R1 occurrence child-role verifier — working tree

The checker now verifies child-role shape for leaf, group, unary, binary,
field, index, vector-literal, direct-call, and member-call occurrences.
`Unknown` nodes make the ledger incomplete instead of passing the generic
ID/parent/count checks. Vector literal child count is checked against the
retained source operand list. This strengthens the occurrence totality proof;
it does not complete it. Constructor, collection-literal, pipeline, message,
statement/binding and control-flow accounting remain open.

Validation on this source: `make --jobs="$(nproc)" moss-dev`, full M2b2
source-free positive/hostile/v9 corpus, generic arithmetic requirements,
generic collection facts, feedback validation and `git diff --check` passed.
Existing TypeSolver initializer warnings remain. Exact-SHA adversarial review
is pending; R1 remains open.

### R1 member invocation role-verifier correction — working tree

The architect reviewed exact pushed SHA
`7a71f95caa4752f884705f7e84f41380a06df72e`. It confirmed the statement member
dispatch fix but returned NO-GO with one P1: the completeness verifier required
`call-argument:<i>` for all statement calls, while the expression checker
records member arguments as `argument:<i>`, and the verifier did not require a
receiver child. The current correction validates direct-call and member-call
child roles separately, requires one receiver child for value receivers,
captures receivers once across dispatch paths, and preserves FileIO's
non-value namespace receiver and static operands explicitly.

Validation on this source revision: the full M2b2 hostile source-free corpus,
generic collection facts, generic arithmetic requirements, H3 direct records,
D3d and D4 actual SDK source-free controls, feedback validation,
`git diff --check`, and `make --jobs="$(nproc)" moss-dev` passed. Build output
contains only the existing TypeSolver missing-initializer warnings. This
follow-up is not yet committed or reviewed. R1 totality, one common structural
lowerer and a cross-crate source-free slice remain open; R2 has not started.

The follow-up was committed and pushed as
`11fbf47e210af8dad158039dbf6a458709af4f64`. Independent adversarial review at
that exact SHA returned **GO**, confirming exact dispatch/receiver/constraint
capture, member receiver and `argument:<i>` verification, direct-call role
preservation, and explicit FileIO namespace/static operand handling. No P0,
P1, or P2 findings remain for this slice. This is not R1 acceptance: full
source-to-occurrence totality, binding/scope lowering, the common structural
lowerer, and a cross-crate source-free execution slice remain unfinished.

### R1 occurrence child-role verifier correction — pending exact-SHA review

The initial verifier checkpoint at `858b82e24f7c6cdd6b591a9f928472714b87a2a2`
received **NO-GO** from the adversarial architect. Findings: `replace` uses
`place`/`replacement` roles instead of `argument:<i>`; static `FileIO.open`
stores a namespace operand before its arguments; and known
collection-literal/pipeline/constructor/message kinds fell through as
complete. The correction validates special `replace` roles, checks sort's
retained vector child as `argument:0`, skips its static `by:` operand, aligns
FileIO argument ordinals after its namespace operand, and marks unimplemented
constructor/collection-literal/pipeline/message contracts incomplete. Unknown
kinds also fail closed.

Validation after the correction: `make --jobs="$(nproc)" moss-dev`; full
M2b2 source-free positive/hostile/v9 corpus; generic arithmetic requirements;
generic collection facts; D3d actual SDK `index_of` source-free execution and
legacy-body poison; D4 SDK collection source-free and mutation suite; agent
feedback validation; and `git diff --check` passed. The build retains existing
TypeSolver missing-initializer warnings. Constructor/collection-literal/
pipeline/message lowering, statements, binding identity, source-to-occurrence
totality, the common structural lowerer, and most ABI-v10 paths remain open.
The corrected checkpoint requires exact-SHA adversarial review; R1 remains
incomplete.

The architect reviewed exact pushed SHA
`323105db332526d16b1bcf519672cbbaa8ac4863` and returned **NO-GO** with one
P1: the per-kind Call verifier used expression roles for statement roots,
while `Stmt::Call` stores `call-argument:<i>` children and statement-form sort
marks its skipped operands explicitly. The correction applies the
`call-argument:<i>` contract to `call-invocation` roots and derives expected
children from the retained non-expression ordinals. Post-fix validation:
`make --jobs="$(nproc)" moss-dev`; full M2b2 source-free/hostile/v9 corpus;
`moss check --json tests/swarm_044_index_specialization.moss`; a concrete
statement-form sort probe in `tmp/phase25_statement_sort_roles.moss`;
FileIO source check; feedback checker; and `git diff --check` passed. The
initial untyped generic sort probe failed with `SORT_REQUIRES_VECTOR` while
the vector TypeRef was still unknown; this is an unresolved checker limitation
to classify against the generic language contract, not a passing case. The
updated checkpoint needs exact-SHA review. A separate attempt to use the
language builtin `replace(place, replacement)` as a statement was rejected as
an unknown local function by the current statement checker; this must be
classified against the source-surface contract and is not treated as an
invalid-language conclusion. R1 remains incomplete.

Architect follow-up at exact pushed SHA
`b6ab6c862175db915ad9cdf107f506af7020e2c7` confirmed the direct
statement-call correction and found one remaining P1: statement-form sort's
active call root captured its vector as `argument:0`, then marked all sort
arguments non-expression. The fix selects `call-argument:0` when the checker
is under a `call-invocation` root and marks only the static key operand.
`tests/tooling/check_phase25_statement_call_occurrence_roles.py` now compiles a
focused C++ checker test that asserts the statement-call and statement-sort
child roles, non-expression marker, and complete retained expression ledger.
Validation on this source: focused role test, `make --jobs="$(nproc)" moss-dev`,
full M2b2 source-free/hostile/v9 corpus, agent feedback checker, and
`git diff --check` passed. Existing TypeSolver missing-initializer warnings
remain. The exact-SHA follow-up review is pending; R1 is still incomplete.

Architect review of `cd1762b3414229d9ac8a16a482e1111a5e9fad47` returned
**NO-GO** with one P1: statement-form sort could pass ledger completeness
without retaining the statically selected `by:` callable identity and effects.
The sort occurrence now retains the exact key callable ID, concrete argument
and result TypeRefs, parameter access, and observable effects; the verifier
requires that fact for keyed sort and rejects unexpected linked callables. The
focused C++ checker regression also asserts those facts. Validation after this
fix: focused checker test, `make --jobs="$(nproc)" moss-dev`, full M2b2
source-free/hostile/v9 corpus, feedback checker and `git diff --check` passed.
Existing TypeSolver missing-initializer warnings remain. Exact-SHA review is
pending; R1 remains incomplete.

The adversarial architect reviewed exact pushed SHA
`856fd0c84dc85233ada27c513220f08255ace1d2` and returned **NO-GO** with one
P1: `sort(v, by: Reverse(key))` retained the inner callable but dropped reverse
ordering semantics when `check_sort_call` stripped the wrapper. The linked
callable fact now carries an explicit `reverse_order` flag; the completeness
path retains that typed fact, and the focused checker regression covers both
ordinary and reverse keyed statement sorts. Validation after this correction:
focused statement-call checker test, `make --jobs="$(nproc)" moss-dev`, full
M2b2 source-free positive/hostile/v9 corpus, agent feedback checker, and
`git diff --check` passed. The build retains existing
`TypeSolver::Undo::binding` missing-initializer warnings. This correction needs
exact-SHA adversarial review; R1 remains incomplete.

The architect returned **GO** on exact pushed SHA
`e81c1a2b411ad950872c83df9787dc1d738897b3`: `Reverse(key)` is preserved as
an explicit fact alongside the selected callable, and the focused regression
checks ordinary and reverse keyed sort. This confirms checker fact capture,
not source-free lowering of sort. R1 remains incomplete.

R1 consolidation probe (uncommitted experiment reverted): the exact valid
source `export fn fresh(): return []` retains a completed `VectorLiteral`
return occurrence but no TypeRef (`kind=VectorLiteral`, `root_slot=return-value`,
`completed=true`, `checked_type=null`), so the ledger is incomplete and the
occurrence lowerer declines it. Switching the empty-vector producer to this
route failed `python3 tests/tooling/check_phase25_empty_vector_records.py` at
its expected 1-summary/1-body assertion. The experimental code was reverted;
the same focused test then passed with the existing shape-specific producer.
This remains an R1 checker-fact gap, not a language exclusion. The checker must
attach the resolved return TypeRef to this literal before common lowering can
consume it. The probe did not change tracked compiler code.

R1 empty-vector occurrence gap resolved in the current working change: at the
checker expression-type retention point, the sole direct return's empty
`VectorLiteral` root now receives the complete open-callable result TypeRef
under exact root/body/Vector guards. The occurrence lowerer supports ordered
vector-literal children and routes round-trip validation through
`decode_checked_body_v1`; the empty-vector emitter consumes those canonical
checked bytes instead of constructing a second body from the source shape. The
focused occurrence test now asserts ledger completeness and equality to the
checked summary result TypeRef. Validation on this source passed:
`check_phase25_empty_vector_records.py`, `check_phase25_r2c_empty_vector_consumer.py`
(source deleted, checked-body projection trace, hostile controls, native
execution, v9 default), `check_phase25_statement_call_occurrence_roles.py`,
`make --jobs="$(nproc)" moss-dev`, and the full M2b2 source-free positive /
hostile / v9 corpus. Existing TypeSolver missing-initializer warnings remain.
The exact working checkpoint needs adversarial review; this resolves only the
empty-vector occurrence slice and does not close R1's other totality,
consolidation, or cross-crate requirements.
