# Combined checkpoint 6cc9fac7 — not accepted

Measured source `6cc9fac7c541dbf82e1a35062f0f8510dc61cb71` was committed and pushed before measurement. Both compiler builds pass. All requested gates completed on that clean combined tree. The imported-function candidates and held `40455b72` remain outside staging.

| Measure | Combined 6cc9fac7 | Delta against 56693998 |
|---|---|---|
| Default suite | 36 PASS / 137 FAIL / 1 SKIP, 174 total | +4 PASS; no new failing jobs |
| Shadow suite | 36 PASS / 137 FAIL / 1 SKIP, 174 total | Same four recoveries; no new failing jobs |
| Frozen 150ab36b corpus | 178 losses / 236 baseline-compiling programs; 0 gains; 238 files; 0 timeouts | No recoveries or new losses |
| Identity | 4/9 required repros; 18/36 generic cells; imported pair 0/1; extra method control 0/1; FAIL | No complete repro/grid recovery; see denominator correction below |
| Field gate | 30/60 existing cells; 4 structural N/A; FAIL. 28 Step 1 first failures and 2 Step 2; zero observed field-rule first failures | Counts unchanged; coverage limitation found, so this is not certification of the requested full field grid |
| Hidden-copy gate | FAIL: 0 detected in 650 compiling sources / 2107 scanned; 1457 compile failures, 557 unexpected, 0 expected missing | +15 compile successes; 13 fewer unexpected failures; 2 expected-missing discrepancies resolved by certified oracle disposition. No whole-corpus zero-copy claim |

Both modes recover `check_phase20_fileio_runtime`, `check_phase20_fileio_semantics`, `check_phase21_raising_iterators`, and `check_teaching_diagnostics`. The first three previously stopped at `functional pipeline has no checker-owned caller SpecId`; the integrated handler/functional-IR producer scope addresses that shared omission. The teaching job previously stopped on its expected self-send-repair source. These are measured job recoveries, not a claim that all handler paths pass. This checkpoint remains 81 suite passes below accepted Step 0 (117/170). The frozen corpus loss set is unchanged.

Exact failing jobs, each corpus loss and first diagnostic, every failing field-cell tag, compiler hashes and exits are in [6cc9fac7-evidence.json](6cc9fac7-evidence.json). Source searches are in [6cc9fac7-structure.json](6cc9fac7-structure.json): no `specialization_context` occurrences in either consumer, no listed type-based selector/name-line matcher/prefix-edit patterns, and 24 fact classes, unchanged from 45b586c2. The remaining 30 `functional_context` lines are unused signature parameters and explicit `(void)` statements; they do not select or look up facts. This structural evidence does not make the failing execution gates pass.

## Field coverage and cause audit

Existing failed-cell axes: source local 8, parameter 8, vector element 6, nested field 8; binding new 15/reassigned 15; use READ 22/CONSUME 8; site main 4/function 2/method 12/handler 12. Each cell's observed cause is retained in the evidence JSON. Earlier identity failures can preempt field-rule execution, so zero observed field-rule causes is not proof that every blocked cell is correct.

Inspection found that local, parameter and vector-element rows bind ordinary Vector aliases (`b = source`, `b = items[0]`), rather than fields. Named probes cover actual fields, but the aggregate does not establish the requested field projection grid. The next test correction makes every source axis end in `.frontier`, retaining the same 60 runnable cells and both-engine execution. Proposal §5 forbids destructive extraction through an ordinary field alias; §§3/8.2 require READ aliases and rebinding to preserve the source. All plain-field CONSUME cells must therefore reject with the existing diagnostic. This changes coverage/expectations openly; any resulting count change must be separated from compiler recovery. The revised handler fixture remains supported by §7.5, and Int-accept/String-reject Echo controls remain present.

## Reporting corrections

Older identity reports counted the nine positive entries (including an additional nested-method control) and reported 3/9. L8's required nine instead include the passing `field_consume` rejection control. On both 56693998 and this source, the required result is 4/9: nest_spec, try_spec, dead_tail2 and field_consume pass. The additional method control fails. This is a denominator correction, not a compiler recovery. Current suite-name extraction stops before nested failure-detail logs, so nested `[PASS]` text is not mistaken for job names; total suite counts were unchanged.

## Active source dependencies and published work

- `phase28-r7-011-team-a-import-preserve-7461` at `01ba7976`: preserves loader-restored specialization rows and validates dense IDs, module/callable ownership and provider origin. The existing visible/source-free method, scalar function and corruption controls pass before the final pair case. The remaining exact missing edge is main → local generic pair at `(SpecId 3, arena 0, ExprId 47)`, while its nested imported calls have rows. This is still WIP and is not integrated.
- `phase28-r7-011-team-b-callback-consumer` at `9285db6f`: exact pipeline arena/argument-slot transport candidate. Typed and dynamic callback positives remain red, so it is not integrated. Generic callback publication also asks for a SpecId before registering the specialization; Team A owns that producer ordering.
- `phase28-r7-011-l7-generic-template` at `d72b8f5d`: shared import restoration/canonical body candidate plus binder duplicate key `(ordinal, SpecId)`. A valid multi-specialization return has sibling records at one ordinal. Independent mutation testing on the current producer is pending; missing-sibling coverage remains open. No new schema or fact class.

Independent Team C's prior verification belongs to 56693998, not this source. No new combined GO is claimed. Certified 62-case oracle inputs are integrated unchanged; the earlier 62/62 semantic result belongs to 00b54a51 and does not validate this compiler. No runtime clone authorization or merge into main/phase28-work.
