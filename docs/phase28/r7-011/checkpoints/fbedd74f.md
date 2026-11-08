# Combined checkpoint fbedd74f — NO-GO

Source `fbedd74faf50f85f6b115a8dcb5606086968a0f3` was committed/pushed before measurement. Both builds and the complete gate set ran on that clean combined source. Team C independently checked the source/binary hashes, primary suite statuses, field causes and focused imported-call controls in a separate checkout. No protected branch merge or runtime clone authorization.

| Measure | Combined fbedd74f | Delta against a17fb625 |
|---|---|---|
| Default suite | 36 PASS /138 FAIL /1 SKIP,175 total | 3 existing jobs recover,4 regress,1 new passing cursor job; total+1 |
| Shadow suite | 36 PASS /138 FAIL /1 SKIP,175 total | Same exact job changes |
| Frozen150ab36b corpus | 161 losses /236 baseline-compiling programs;0 gains;238 files | 26 emission recoveries,9 new losses; net17 fewer losses |
| Identity | 7/9 required repros;20/36 generic cells; imported pair1/1; additional method1/1;FAIL | Required+3, generic+2, import+1, extra method+1 |
| Field | 50/60 runnable,4 structural N/A;FAIL | +20 cells under stronger source-preservation controls;8handler READ Step1 failures,2projected indexed READ Step2 failures |
| Hidden-copy gate | FAIL:0 detected among548 compiling sources /2107 scanned;1559 compile failures,660 unexpected,1 expected failure missing | 102 fewer sources emit;103 more unexpected failures; coverage regressed, no whole-corpus zero-copy claim |

Exact primary job sets, changed names, each corpus path/first diagnostic, cell axes/causes, exits and compiler hashes are in [fbedd74f-evidence.json](fbedd74f-evidence.json). Corpus results measure Moss-to-Rust emission, not Rust execution or semantic acceptance. Unchanged suite PASS totals conceal regressions.

## Recovered and regressed jobs

Existing recoveries in both modes: `check_phase27_5_match_copy`, `check_phase27_est_functional_tree`, `check_phase27_est_mossi`. New passing job: `check_phase28_native_call_argument_cursor`.

Regressions in both modes:

- `check_phase25_sa_numeric_literals`: Native statement EST handle missing/invalid. Independent verification found the new field boundary always selects RhsValue, although let/var own an Initializer slot. Existing `swarm_013_immutable_let_positive` demonstrates the error even for `let value = 1`. Team B repairs slot selection while retaining fail-closed field checks.
- `check_phase20_fileio_runtime`: the guard treats `Status.Range` (fresh unit enum construction) as a nonprimitive field place. The checker must supply/retain constructor authority and the consumer must distinguish it without a type/name exception. This is separate from slot selection.
- `check_phase21_abi_v9_migration` and `check_phase21_abi_v9_external_cache`: missing imported external module/SpecId pair. A provider package's dense table includes an unimported executable module. Eagerly requiring its unused row makes an otherwise independent provider API fail import. Root's next candidate preserves strict resolution for every actual reference, including optional execution-body iterator maps; independent review found and closed a missing optional-body remap in that candidate. Validation is pending.

All nine new corpus losses report the Native missing-statement-handle invariant: `for_loop_mutable_update`, `phase15_15_enum_positive`, `phase224_debug_failure`, `phase45_cross_let_filter_count`, `primitive_field_projection`, `read_alias`, `swarm_013_immutable_let_positive`, `swarm_034_tail_push_unit`, `swarm_052_concrete_vector_trait_parameter` (full paths in JSON). The slot defect is proven on an existing let control; it must not be assumed to explain every occurrence without rerunning the controls.

## Field and identity findings

Every field source axis now uses .frontier, and every READ cell rereads its source after alias use. The50 passing cells are30 required CONSUME rejections and20 successful READ cells. Named parameter alias/caller reuse controls pass. The two former Native E0507 method cases now borrow their checked field in both physical method forms, including reassignment, and output3/3; no clone is emitted in those bodies.

All8handler READ cells remain blocked: Native RootIngress exact edge matching; Fast Debug reaches a nested Inner record initializer whose exact facts are missing because domain-construction checking returns early. The outer domain constructor already uses the static composition graph; no domain-name fallback is required. The2vector-element READ-main cells emit/execute3/3 in Native but fail Fast Debug projected-read authority at the source reread, a known Step2 gap. They were printed UNCLASSIFIED by the gate; independent review supplies the exact Step2 classification here. No field-rule failures were observed on this complete run, while blocked handler execution remains unverified.

The required passing repros are nest_spec,meth_spec,try_spec,dead_tail2,o15b,field_read,field_consume. dom_spec and cb_spec still fail. Generic method depth0 now passes both scalar types; method depths1/2 fail checker return inference, and handler/callback rows remain red. The source-free pair outputs10/3.5 in both engines. Independent cursor/method gates verify module capitalization, nominal/function-name collision and production corruption controls. Their PASS is not Phase acceptance.

The ID-only concrete-provider test deliberately expects an internal error to prove the previous SIGSEGV is contained. It remains a known valid-program rejection; a passing guard test is not evidence that concrete import materialization works.

## Independent review and next frozen work

Team C returns NO-GO for combined acceptance. Its dev hash is635ee06ee17271513aac8ff8f6ba3cac80629a7a994d6868f6ce9ea0de6f5a61; opt69f3f8e45f849f1c085133c50ff82bc1002f4b58cfa1518b591d758616ea989a. No source/oracle changes or shared-scratch gates were made by the verifier.

Next isolated A changes608ff631/05937c7b/a0c1dd51/f99c3412 address nested message publication, composition initializer traversal, per-instance caller isolation, and callback producer identities. None are included in this measured SHA. Root's bb42f97a/8f42433b/e4e79ab4 remap candidate addresses only unused table dependencies and complete reference remapping. B owns the Native slot/enum boundary regressions. These remain WIP until coupled focused controls and the next full combined checkpoint. No new schema, fact class or runtime authorization is granted. Frozen oracle inputs remain unchanged; earlier oracle measurements do not validate this source.
