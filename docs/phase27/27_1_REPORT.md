# Phase 27.1 EST foundation — closeout

Branch: `phase27-work` (merged to `main` at `5627727657ad7d26d860d4824b4ba03764252b0e`, tag `phase27-1-m1`).
Required corpus/performance baseline: `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`
CP-A candidate: `0f116ada267b3772895e84859132a07218be8aa1`. CP-A review completed; see the review record below.

## A. Corpus count

The requested tree at `4d6d0c8` contains 2,061 tracked `.moss` files, and the corpus runner checked all 2,061. There are no three omitted sources to name. The 2,064 denominator belongs to `d109b8f1`, not the required tree: it has seven older-only sources and four newer-only sources at `4d6d0c8`, yielding a net difference of three. The seven absent from the required tree are `tests/negative/swarm_013_immutable_let_direct.moss`, `tests/negative/swarm_013_immutable_let_index.moss`, `tests/negative/swarm_013_immutable_let_member.moss`, `tests/negative/swarm_013_immutable_let_receiver.moss`, `tests/phase45_mutable_binding_materializes.moss`, `tests/tooling/fixtures/builtin_generic_typing/map_delete_immutable.moss`, and `tests/tooling/fixtures/moss_language_surface_immutable_let.moss`. The four added by the required tree are `sdk/cbor/tests/support/support.moss`, `tests/phase45_single_assignment_var_virtualizes.moss`, `tests/phase45_var_mutation_materializes.moss`, and `tests/phase45_var_reassignment_materializes.moss`. This was a denominator mismatch, not an accidental omission.

## B. Construction-failure triage and rulings

Counts below are the initial failure groups before fixes. Slot distributions and original reproducers are also recorded in `EST_DIVERGENCES.md`. Bank rulings supersede its historical NEEDS BANK labels; no NEEDS BANK group remains.

| Form | Label | Count | Reproducer(s) |
| --- | --- | ---: | --- |
| Pipelines | DEFERRED-27.1b | 9,919 | `tests/phase27/est/repro/pipeline.moss` |
| Collection literals | DEFERRED-27.1b | 7,447 | `tests/phase27/est/repro/collection_literal.moss` |
| Collection constructors | DEFERRED-27.1b | 30 | `tests/phase27/est/repro/collection_constructor.moss` |
| Record/enum constructors and constructor argument fragments | DEFERRED-27.1b | 22,712 | `tests/phase27/est/repro/constructor.moss` |
| Message expressions | EST BUG, fixed | 1,645 | `tests/phase27/est/repro/message_expression.moss` |
| Bitwise and shift operators | EST BUG, fixed | 402 | `tests/phase27/est/repro/bitwise_shift.moss` |
| `**`, `in`, `&&`, `||`, incomplete `2 +` | REJECTED-SOURCE | 10 | `tests/phase27/est/repro/unsupported_operators.moss`, `symbolic_boolean.moss`, `incomplete_expression.moss` |
| Isolated `]` / `)` fragments | REJECTED-SOURCE | 7 | `tests/phase27/est/repro/split_fragment.moss` |
| Numeric literal member access | EST BUG, fixed | 4 | `tests/phase27/est/repro/numeric_literal_member.moss` |

The seven delimiter fragments were traced to six rejected files; none came from an accepted source. The exact paths and each string-path diagnostic are in `EST_DIVERGENCES.md`. The 10 rejected-operator/incomplete-expression cases and all seven fragments retain identical off/shadow diagnostics (G4). No `.mossi`-loaded expression was present in the `.moss` corpus.

## C. Final unique-occurrence counters

The 27.1b/c final corpus pass covered 2,078 tracked sources and 1,351 accepted. Columns are attempts / unique `(owner, slot, argument_index)` occurrences / built / deferred 27.1b / rejected source / failed. Attempts and unique counts match within each Program arena scope; this does not establish cross-compilation deduplication. Deferred and failed are zero in every slot.

| Slot | Attempts | Unique | Built | Deferred | Rejected | Failed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Condition | 22,029 | 22,029 | 22,023 | 0 | 6 | 0 |
| Initializer | 16,953 | 16,953 | 16,951 | 0 | 2 | 0 |
| ReturnValue | 32,785 | 32,785 | 32,767 | 0 | 18 | 0 |
| Iterable | 744 | 744 | 744 | 0 | 0 | 0 |
| Receiver | 32,942 | 32,942 | 32,941 | 0 | 1 | 0 |
| Argument | 59,115 | 59,115 | 59,080 | 0 | 35 | 0 |
| LhsPlace | 47,936 | 47,936 | 47,936 | 0 | 0 | 0 |
| RhsValue | 47,936 | 47,936 | 47,923 | 0 | 13 | 0 |
| Scrutinee | 147 | 147 | 147 | 0 | 0 | 0 |
| RaisedValue | 3,024 | 3,024 | 3,024 | 0 | 0 | 0 |
| Expression | 68 | 68 | 42 | 0 | 26 | 0 |

Pattern had no occurrences. Aggregate: 263,679 attempts and unique occurrences; 263,578 built, 0 deferred, 101 rejected, 0 failed. Final slot data and divergence labels are also in `docs/phase27/EST_DIVERGENCES.md`.

## D–F. Places, copy/rebuild contract, and off mode

- D: `tests/phase27/est/raw_places.moss` and `tests/tooling/check_phase27_est.py` verify the requested projection chain, different IDs for `i`, `j`, and both `u` occurrences, compound `+=` bindings, and ExprStmt classification for `f(x)`. Assignment/call statements promoted by the legacy parser receive EST-only RawForm metadata once; consumers are unchanged. Pass.
- C dump positives: `tests/phase27/est/message_expression.moss`, `bitwise_shift.moss` (cites `docs/LANGUAGE_SYNTAX.md` §Bitwise and shift operators), and `numeric_member.moss`. `MOSS_EST_DUMP=1` writes S-expressions with ExprIds. Pass.
- E: the focused contract test checks ordinary Program copy ExprId retention, statement insertion/erasure, deep fresh occurrence IDs for identical member/index text, rebuild rebinding while the old node remains readable, append-only arena growth, and the negative `Stmt.text` staleness assertion in shadow. Module-name rewrite shadow hooks call `rebuild_slot`; the functional optimizer changes semantic plans and does not rewrite expression text. Pass.
- F: `MOSS_EST=off MOSS_EST_STATS=1` reports `arena_size=0 handle_count=0`; the off path returns before arena allocation or handle population. Pass.
- Test command: `python3 tests/tooling/check_phase27_est.py build/compiler/moss-dev`.

## G. String-debt baseline

Counts at `4d6d0c8`, ordered as `parse_simple_call / split_top_level / strip_redundant_outer_parentheses / parse_enum_case_expression / interpreter eval(string)`:

| Consumer | Counts |
| --- | --- |
| Checker | 50 / 16 / 6 / 7 / 0 |
| Emitter | 17 / 6 / 9 / 2 / 0 |
| Reach walk | 7 / 10 / 1 / 0 / 0 |
| Fast Debug | 0 / 0 / 0 / 0 / 99 |

`docs/phase27/STRING_DEBT_BASELINE.md` gives the measurement command and machine-readable per-site baseline consumed by the lint. A temporary `split_top_level` emitter call failed the lint with exit 1 (`tmp/phase27-g-emitter-lint-failure.log`). A temporary interpreter `eval` call failed with exit 1 (`tmp/phase27-g-interpreter-lint-failure.log`). Both probes were removed; the current lint passes.

## G1–G9 final-head gates

Baseline hash for the corpus and performance gates: `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`.

| Gate | Result | Evidence |
| --- | --- | --- |
| G1 `MOSS_EST=off make --jobs="$(nproc)" check` | PASS, 157/157 jobs, zero failures or skips, on merge commit `5627727657ad7d26d860d4824b4ba03764252b0e` | `tmp/phase27-closeout/merge-G1-off.log`; baseline `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576` |
| G2 `MOSS_EST=shadow make --jobs="$(nproc)" check` | PASS, 157/157 jobs, zero failures or skips, with opt-in tracer code present, on the merge commit | `tmp/phase27-closeout/merge-G2-shadow.log`; baseline `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576` |
| G3 branch off vs 4d6d0c8 binary | PASS, 2,087 sources, zero verdict or default Rust-hash differences; the initial 20-second timeout discrepancy disappeared on the documented 120-second retry | `tmp/phase27-closeout/merge-G3-baseline-retry.log`, `merge-G3-off-retry.log`, matching TSVs; baseline binary `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576` |
| G4 off vs shadow | PASS, same 2,087-source corpus, zero verdict or Rust-hash differences | `tmp/phase27-closeout/merge-G4-shadow-retry.log`; `merge-G3-off-retry.tsv` equals `merge-G4-shadow-retry.tsv` |
| G5 examples and corpus optimized Rust | PASS, off/shadow/baseline optimized TSVs identical across 2,087 sources; 1,347 successful emissions, with the same 724 rejects, 5 NOJSON, and 11 EMITFAIL rows per mode | `tmp/phase27-closeout/G5-baseline-opt-final.log`, `G5-off-opt-final.log`, `G5-shadow-opt-final.log`; TSVs under the same directory |
| G6 Fast Debug parity | PASS, 546 accepted examples, baseline/off/shadow identical, zero R12 hits; same two timeouts in each mode | `tmp/phase27-closeout/merge-G6-fast-debug.log`, `merge-G6-fast-debug.json` |
| G7 `.mossi` parity | PASS: off/shadow raw bytes identical on the same binary; baseline-identity recomputation equal for derived hashes and all remaining Section 1/2 bytes | `tmp/phase27-closeout/merge-G7-raw.log`, `merge-G7-baseline.log`; detailed report `tmp/phase27-mossi-baseline/1793256362679123879/report.json` |
| G8 off compiler-perf | PASS against merge first parent `0ecfa51c6dfa4a3d87b678012e5605ad29a8da7c`: 4.871 s baseline, 4.858 s candidate (−0.3%), no workload beyond noise floor | `tmp/phase27-closeout/merge-G8-perf-off.log`; report `tmp/compiler-perf/20261028-234617/LOCAL_REPORT.md` |
| G9 shadow compiler-perf | Not a merge gate in the closeout gate list; the previously measured shadow overhead was +2.3% on the earlier 4d6d0c8-baseline run | `tmp/phase27-final/G9-perf-shadow-final.log`; report `tmp/compiler-perf/20261028-215905/LOCAL_REPORT.md` |

The first G1 attempt exposed standalone native-test links that omitted the nested EST translation unit. The shared source enumeration was corrected; the final G1 and G2 runs above both pass. The first G7 fixture used different absolute roots and therefore changed a Section 2 path hash; using the same root for both modes produced identical Section 1 and Section 2 hashes.

### G7 ruling and revised evidence

The Bank ruled that the compiler-bound cache contract must remain intact: distinct compiler binaries retain distinct `package_toolchain_identity` values. G7 therefore requires (1) raw `.mossi` equality for off and shadow using the same binary, and (2) for branch versus `4d6d0c8`, recomputing `package_fingerprint` under the baseline toolchain identity and recomputing its dependent `execution_body_hash`. The focused two-package probe passes both checks. The candidate and baseline raw hashes differ, while both recomputed package fingerprints and body hashes match and all other Section 1/2 bytes are equal. Detailed values are in `tmp/phase27-final/G7-baseline-recomputed.log`.

## Decisions and self-review

### CP-A / CP-B reviews

- CP-A on frozen `0f116ada267b3772895e84859132a07218be8aa1`: **`VERDICT: GO — 0 confirmed implementation defects; 1 measurement-scope caveat.`** Confirmed defect count 0. The 403,436 attempts/unique values are per Program arena scope and do not mean a global cross-compilation dedup key. The two Fast Debug timeouts reproduced on 4d6d0c8. Review: `docs/phase27/adversarial/CP-A.md`.
- CP-B original finding: NO-GO due distinct compiler-bound cache identities. Per Bank ruling, preserve those identities and use the revised invariant. Fresh revised-invariant review at `dacf2fab1ab252968dc974b180e7279e46cb22e9`: **`VERDICT: GO — 0 confirmed product defects; 1 test-coverage caveat closed by the independent probe.`** Revised final fresh review at `3d4d3b4fcdcb9e4113f8217850ceccc9b9d0dc07`: **`VERDICT: GO — 0 confirmed defects.`** Both confirmed defect counts are 0. The pipeline shape oracle correction after that frozen review is limited to canonicalizing left-associated pipeline stages; G2 passed on the corrected code. Final review artifacts: `docs/phase27/adversarial/CP-B-final.md`, commit `feb5c111077e5d06b6b621a724a18cdd8927871f` integrated unchanged as `16fc4a5f`.

### 27.1b/c completion

- All deferred 27.1b expressions, including pipelines, placeholders, constructors, collection literals, and named arguments, are built. ABI-v9 generic statement bodies and Section 2 execution bodies loaded from `.mossi` use EST parsing with package/interface origin IDs.
- In shadow mode the structural oracle compares the EST shape with the string path wherever that path parses. G2's first run exposed a left-associated versus flattened pipeline representation mismatch; the shape oracle was canonicalized, and the single rerun passed. Structural comparison stats and an injected mismatch test are covered by `tests/tooling/check_phase27_est.py`.
- The conformance gate allowlists retain existing runnable fixtures, including `Group A/push_self_field_read`, `Group A/field_self_assign_struct`, and `mutate_loop_element`. The four T6 entries were removed because reachable Phase 24 history contains only README references and no fixture files. Both gates now fail if an allowlist path is missing; see `tests/tooling/phase27_allowlist_paths.py`.
- CP-B-final review limitation: its focused structural-shape probes do not compare cases where the legacy string-shape parser returns null. Corpus shadow builds had no comparable structural mismatches; the remaining unparsed shapes are tracked by shadow comparison stats.
- Bank accepts the measured blind spot: all 173 never-compared occurrences are uppercase-qualified calls (`Module.func` / `Type.method`) for which the legacy shape parser returns no shape. This is a 27.1 validation limit, not a compiler behavior difference. First item to close in 27.2: after module-name resolution moves onto the tree, add structural comparison for qualified calls and use `tests/tooling/measure_phase27_est_blindspots.py` to show the count reaches zero.

- **Self-review:** The 2,064 count is stale for the specified baseline; all source-set differences are listed rather than inventing three omissions.
- **Self-review:** Parser-promoted assignment and call statements retain their existing kinds and semantics. EST-only metadata covers the required Assign / CompoundAssign / ExprStmt classifications.
- **Self-review:** The one initial G1 failure was a test-link inventory issue, fixed by discovering nested `src/**/*.cpp`; final G1/G2 evidence is clean.
- **Self-review:** Rebuild and fresh-occurrence APIs only append nodes and rebind explicit handles. No consumer was switched to tree-based behavior.
- **Self-review:** Off/shadow output parity and off/base parity are both zero-difference on the required corpus. No language semantics, `.mossi` format, or ABI was changed.


## Merge delivery

The merge commit is `5627727657ad7d26d860d4824b4ba03764252b0e`; annotated tag `phase27-1-m1` points to it. All required closeout gates above were run on that merge commit. The merge was pushed to `origin/main`.
