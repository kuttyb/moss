# Moss Phase 25 — Overnight Execution Brief
## M2b.3d completion, ABI-v10 no-fallback gate, and release readiness

**Authority:** L7 (user + ChatGPT) has authorized this *bounded overnight feature-branch execution*, not a merge to `main` or an ABI-default switch. This brief is for an L6 coordinator (Codex) to execute now. Do not reinterpret it as an L8 autonomous orchestration system.

**Repository:** `kuttyb/moss` — https://github.com/kuttyb/moss  
**Known verified integration HEAD (at brief creation):** `phase25-integration` = `39e7d8ac810f579243400766f007e9e7550b209b`  
**Approved D2 source:** `phase25-m2b3d-d2-loop-codec` = `4ff42566ff66fa012cc9ac24c5440691e3b4cfd3`  
**D2 base:** `39e7d8ac810f579243400766f007e9e7550b209b`  
**Expected after D2 fast-forward:** `phase25-integration` at `4ff42566ff66fa012cc9ac24c5440691e3b4cfd3`, followed *only if needed* by a status-only commit. Record that actual SHA before starting implementation.
**Pre-created overnight branch:** `phase25-m2b3d-overnight`, branched from reviewed D2 commit `4ff42566ff66fa012cc9ac24c5440691e3b4cfd3`; this document is committed to that branch. Do not try to create the branch again.
**Morning checkpoint:** Gate 0 passed and `phase25-integration` is at `4ff42566ff66fa012cc9ac24c5440691e3b4cfd3`. Overnight handoff `590f7e8005ab7a6c06d2fca38de3911646cf0856` stopped at D3a over implicit-local mutability. L7 resolved that conflict below. Resume directly from D3a on the existing overnight branch; do not repeat Gate 0 or modify integration again.

## Outcome and non-negotiable boundaries

Bring real Phase 25 generic collection loops from **checker-owned TypeRefs** through **canonical checked-body encoding** to **provider ABI-v10 artifacts**, **source-free importing**, **ordinary Moss specialization/codegen**, and **native execution**, with *no executable legacy `generic_stmt` dependence*. Extend the first proven `index_of` path to `contains` and `count_equal`, then make staged ABI-v10 refuse fallback to legacy generic bodies, while maintaining ABI-v9 exactly as before. Produce enough evidence for L7 to run the full suite and make a later cutover decision.

**Must not:** merge to `main`; change the default ABI from v9; remove v9 compatibility fields; claim all Phase 25 is complete without auditing `from_range`; silently alter Moss generic/ownership/equality semantics; invent a second generic specialization engine; use provider source or `generic_stmt` to recreate checked facts in v10; broadly support unrelated control flow, `For`, arbitrary methods, Map/Queue, messages or Range in the core loop path.

**Single-writer rule:** Only one implementation agent/worktree edits `src/moss.cpp`, `src/ast.hpp`, `src/typed_semantic_api.hpp`, `src/abi_v10_records.hpp`, or any interdependent checker/importer/ABI machinery at a time. The L6 coordinator may use isolated, parallel **tests-only or audit-only** helpers in their own worktrees, with explicit files and no compiler source edits; otherwise use a single agent sequentially. A separate skeptical review pass after substantive milestones is valuable, but do not create an expensive multi-agent review swarm. Never let two coding agents edit `src/moss.cpp` concurrently.

**Git:** All implementation after the approved D2 fast-forward goes onto the existing `phase25-m2b3d-overnight` branch, which starts at reviewed D2 HEAD and contains this brief. After Gate 0, verify reviewed D2 is an ancestor of both integration and overnight; a status-only integration commit does not require changing the overnight base. Push it and each meaningful checkpoint. Within that branch, use linear milestone commits D3a, D3b, D3c, D3d, D4, M2b.4, audit as feasible; if an independent test/audit helper produces a commit, cherry-pick it under single-writer coordination. Do **not** merge the overnight branch to `phase25-integration` without a new L7 review. Do not touch `main`. Never force push; verify SHA/ancestor before fast-forward. If the expected D2 baseline moved, STOP rather than guessing.

**Evidence standard:** tests must use **independently compiled** sealed provider artifacts with provider source removed, poison/remove the *exact* relevant exported/private `generic_stmt` records and assert they were present and then gone, demand `MOSS_CHECKED_BODY_PROJECTED` on every admitted callable, and execute at least Int and String concrete consumers where meaningful. Build success or v9-only compiled-library behavior is not ABI-v10 proof. Record tests actually run, not tests inferred from source inspection.

## Run order and dependency DAG

```text
0  Approved D2 fast-forward + baseline checks
                  |
1  D3a authoritative checked statement retention (checker)
                  |
2  D3b provider typed summary/body/requirements/effects emission
                  |
3  D3c source-free import validation and AST projection
                  |
4  D3d executable source-free index_of proof + mutation tests
                  |
5  D4 contains + count_equal from same machinery
                  |
6  M2b.4 strict staged-v10 no-legacy admission + provenance audit
                  |
7  Focused G5/G6 release-readiness report (L7 owns full suite and cutover)

Independent test-only/audit-only work may overlap 1–3 but cannot merge into
compiler code out of order. Optional from_range/borrowed-Range feasibility
inspection may proceed separately; it cannot hold up the index_of chain.
```

### Gate 0 — Integrate already-reviewed D2, without compiler edits

1. Fetch remote. Confirm `origin/phase25-integration` is exactly `39e7d8ac810f579243400766f007e9e7550b209b`, and `origin/phase25-m2b3d-d2-loop-codec` is exactly `4ff42566ff66fa012cc9ac24c5440691e3b4cfd3` and is a descendant. If anything differs, stop with an exact SHA report; do not merge blindly.
2. Fast-forward `phase25-integration` to D2. Run focused D2 codec, pre-D2 rejection/compatibility, H3 decoders, H3 direct and private-chain, D1 generic collection-facts, generic arithmetic, G2 and the existing source-free corpus. Verify historical checked-body v1 golden hashes are unchanged. The SDK collection-loop corpus is still classified legacy-dependent at this point.
3. Push the integration ref. If a status record is needed, commit **only** `.codex/CURRENT_STATUS.md` and push. Confirm clean worktree and report the actual integration HEAD. Then check out the already-created `phase25-m2b3d-overnight` branch, verify it descends from reviewed D2 HEAD and contains `docs/phase25/MOSS_PHASE25_OVERNIGHT_EXECUTION_BRIEF.md`, and do all subsequent work there. Do not recreate the branch, merge an integration status-only commit into it, or touch `main`.
4. Baseline-exception ledger: old `TypeSolver::Undo::binding` missing-field-initializer warning promoted by `-Werror` may require *only* the documented targeted `-Wno-missing-field-initializers` when running affected standalone test scripts; record which invocations need it. The typed-semantic API `add_one` requirement-count assertion was already failing at D1 baseline with two requirements (`return-operator`, `checked-arithmetic:0`), unchanged body hash. Do not rewrite that test to produce artificial green without separately verifying expected behavior and obtaining review.

**Gate 0 PASS:** D2 fast-forward only, clean remote, compatible fixtures green, exact integrated SHA recorded. A failure in any reviewed D2 core check stops the overnight compiler chain.

### L7 semantic decision — D3a implicit mutable binding (2026-10-20 morning)

**Binding contract correction, overriding any older contradictory language below:** Keep the actual SDK source unchanged. Moss accepts first `n = items |> count` as introducing an **implicitly mutable** local. A subsequent `n = 2` is legal; an explicit `let n = ...` would be immutable. The original D2 positive fixture's `Let(n)` was therefore an incorrect model of the SDK semantics. It is a fixture/plan defect, not a justification to change the language or SDK.

**Mandatory D3a preflight, before retaining the typed statement tree:**

1. Prove from checker-owned binding/local information that the *first* `n = ...` in this lexical scope introduces a new mutable `Int` binding at local ordinal 1, as distinct from assigning an already-bound local. Do not infer this solely from name spelling, line order, or a scan of `Stmt::text`; verify scope, definition, shadowing and all relevant control-flow paths. If the checker lacks the needed introduction/mutability fact, implement its smallest authoritative retention mechanism; otherwise stop with an exact missing invariant.
2. In checked-body IR, normalize that checker-proven **implicit mutable introduction** to `StmtKind::Var(ordinal=1, type=Int, initializer=CollectionCount(items))`. `StmtKind::Var` here denotes the semantic mutable-binding introduction, not a claim that source contained the `var` keyword. Encode subsequent assignments to `n` as `StmtKind::Assign(ordinal=1, ...)`. Retain existing `Var(i)` at ordinal 0. Explicit `let` remains `StmtKind::Let` and rejects later assignments. No new tag or altered wire schema is needed for this accepted subset.
3. On the overnight feature branch, correct `tests/tooling/check_phase25_d2_collection_loop_codec.cpp` to use `Var(n)` rather than `Let(n)`. Change the earlier "assign to immutable n rejects" assertion to **mutable n assignment accepts and round-trips**. Add a separate `Let(n)` negative showing explicit immutability is enforced. Preserve invalid read-before-declaration and incorrect n TypeRef negatives; preserve existing H3 golden hashes and pre-D2 reader compatibility. Do not rewrite the already-integrated D2 commit or retroactively modify `phase25-integration`.
4. Prove semantic equivalence of actual implicit `n = 1; n = 2` and normalized `var n = 1; n = 2` in checker, Fast Debug and native where supported; prove the explicit `let n = 1; n = 2` case rejects with `IMMUTABLE_LOCAL_MUTATION`. Add a local-shadowing/branch-introduction negative so a first assignment in one scope cannot be silently treated as a safe outer declaration. Use the normal Moss checker as final authority after import projection.
5. Record this as a separately identified D3a-preflight correction commit with focused tests and `git diff --check`. Resume the rest of D3a only after this is green. If source and checked IR semantics differ in ownership, scope or effect interpretation, fail closed and report rather than changing SDK behavior.

The original plan's *general* statement-tree and provider/consumer acceptance gates still apply, with `n` now correctly treated as a mutable local. Local-only mutation does not turn parameter accesses from READ/READ into WRITE, nor justify changing the existing `may_panic` effect.

### Gate 1 — D3a: checker-owned typed statement-tree retention

**Target:** actual `sdk/foundations/collections/src/collections.moss::index_of`, as well as the equivalent `Service.fresh` source-free corpus fixture. For reference, its exact source structure is:

```moss
export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
```

The D1 complete open summary already requires `Vector[T]`, `T` -> `Int`, one bound `T`, READ/READ, Count (`builtin:collection-count-v1`), Index (`builtin:index`, key Int, result T), equality (`operator:==`, T,T -> Bool), and `may_panic`. D1 deliberately rejects extra/unaccounted generic operations and obligations. Keep this fail-closed contract, do not generalize admission based on superficial source patterns.

Find the **actual checker semantic owner** of the statement/body TypeRefs: `Checker::run`, `check_function`, `check_expression`, `check_functional_pipeline`, `refresh_open_collection_summaries_from_checked_types`, `checked_body_states_`, per-statement solver snapshots, existing `retain_checked_body_ir()`, `OpenCallableSummaryFacts`, and `checked_ir::Stmt`/`Expr`. Search by symbol rather than trusting old line numbers. Retain the *single authoritative checked body* when the checker completes a valid open summary, not by reparsing `Stmt::text`, copying `generic_stmt`, or guessing a result type from a display string.

Preserve in retained IR:
- parameter ordinals 0 (`Vector[T]`) and 1 (`T`);
- local ordinal 0 for explicit mutable `i: Int`, ordinal 1 for **implicitly introduced mutable** `n: Int` (checker-certified semantic `Var` introduction, not an explicit `Let`);
- the typed initializer expressions and actual `CollectionCount` operation identity;
- `While(Less(LocalRead(i), LocalRead(n)))`, with Bool result and nested block;
- nested `If(Equal(Index(items, i), target))` with exact Index and equality semantics, early `Return(i)`, and subsequent `Assign(i, Add(i, 1))`;
- terminal `Return(-1)` (or an equivalent checked canonical Int arithmetic representation consistent with source);
- each local's identity, mutability, scope, joined environment/definite assignment and checked TypeRef;
- checker-owned requirement/operator origins and `may_panic`, not approximations from arbitrary text.

Use the D2 canonical body encoder **without changing its existing wire semantics**. Implement *small checker-lifecycle retention adapters* if needed; do not build an independent statement type inference engine. Prove the retained body and open summary share exactly one TypeRef binding for T; no requirement/operation omission, no source text payload. The summary must be derivable with **no concrete consumer calls**. Check that checker retention is not accidentally specific to the name `index_of` or number of textual lines; supported semantic shape + complete authority determine admissibility.

**D3a focused tests:** provider-only check, checked IR exactly matching the *corrected mutable-n D2 fixture*, canonical body hash reproducibility, Int/String bound variable contexts, invalid/incomplete summaries do not yield checked-body availability, extra `target + target`, duplicate Count/Index, extra equality and unsupported record equality remain fail-closed. Prove implicit mutable-n reassignment succeeds, explicit `let n` reassignment rejects, and branch-local/shadowed introduction does not escape scope. Ensure G2 and D1 facts still pass. A fully typed checked body must never be emitted for an incomplete summary.

**D3a commit:** code + tests + status with body representation, exact entry points and evidence; push branch checkpoint. If the checker truly cannot recover identity or path/ownership facts from its existing semantic state, stop this milestone, document the first missing invariant with a minimal reproducer, and do not fabricate a checked summary.

### Gate 2 — D3b: authoritative provider ABI-v10 emission and semantic closure

Inspect actual producer entry points (`append_abi_v10_*_records`, `abi_v10_checked_requirements`, `build_open_callable_summaries`, `src/abi_v10_records.hpp`, `typed_semantic_api.hpp`, provenance code). Bind emission to the D3a retained body and complete D1 summary. Reuse the existing canonical `checked-body-v1` record and `body-semantic-v1` hash; D2 added tags 15/16. **Do not invent `checked-body-v2`** or change legacy v1 bytes unless a demonstrable format incompatibility requires L7 review.

Provider artifact must contain:
- typed-v2 complete summary: parameter Vector[BoundVar(0)], BoundVar(0); Int result; READ/READ;
- all semantic requirements: concrete Index builtin receiver/key/element and equality admissibility for the same T; correctly serialized operation, arguments, result, access, requirement origin/provenance;
- Count represented as a **validated versioned builtin intrinsic** in checked body with `Vector[T] -> Int`, not as a guessed source-named private call;
- canonical checked body bytes and semantic body hash that match the checker-retained statement tree;
- consistent callable ID, source provenance, dependency records, required nominal/callable identities and effect bits/tags (including `may_panic`);
- compatibility records may still be present for v9, but they cannot serve as v10 executable authority.

Validate *completeness before writing* rather than writing partial summaries: count of decoded Count/Index/equality operations must match the summary's operation/relation contract; no omitted generic operation; no forged requirement; no weakened WRITE/CONSUME/effect/panic behavior. `may_panic` is not a reason to reject this admitted body merely because earlier simple shapes required pure effects; instead verify this body's exact supported effect contract, leaving all unrelated shapes under their existing constraints. Do not globally relax purity/effect admission.

**D3b tests:** provider-only sealed v10 artifact with a checked summary + checked body + provenance; decode record independently and compare canonical TypeRefs/body hash/requirements/effects to checker facts; reject missing/malformed/reordered/duplicate summary or body; forged equality/index/Count ID; wrong T relation; missing/wrong panic effect; missing/wrong provenance; invalid dependency; switch-off rejects this staged checked-body admission deterministically. Verify no v9 output/semantic changes.

**D3b commit:** targeted producer changes and tests; push checkpoint. Do not begin consumer projection until provider contract tests are green.

### Gate 3 — D3c: source-free importer verification and projection to normal Moss

Extend the existing ABI-v10 importer and `project_imported_h3_checked_bodies()` path to select D2's bounded loop decoder **only for an already validated admitted body**. Dispatch by validated body node structure and exact record identity, not function names, source spelling or an open-summary type signature alone. Resolve typed-v2 summary, canonical body hash, dependency and callable IDs, origin/provenance, requirements, parameter access and effect bits against each other first.

D2 decoder is a **structural typed verifier**, not a replacement for the ordinary checker. It validates dense ordinals, scopes, mutability, correct Bool conditions, Int indices, Vector[T]/T relationship, assignment types, node and depth bounds. D3c must also close the **semantic contract**: body Count/Index/`==` occurrences, parameter/result types, requirement provenance/IDs and `may_panic` agree with the provider summary and the consumer's concrete specialization. Reject any unaccounted generic operation and mismatched Index/equality/panic semantics. Do not accept `operator:==` for types for which the ordinary checker rejects equality (e.g., unsupported record equality); don't break the existing mixed-numeric equality rules outside this generic shape.

Validate **the entire** decoded function before mutating imported `Function::body`; preserve atomic failure on malformed records. Project the validated structure into ordinary `Stmt::Var`, `Let`, `Assign`, `While`, `If`, `Return`, using deterministic hygienic generated local names and imported signature parameter ordinal mapping. Because normal AST uses textual expression fields, a **closed printer over validated typed IR nodes** is acceptable, but absolutely no reparsing provider source, `generic_stmt`, or generic wire display type strings. Re-run ordinary Moss checker, ownership/effect validation, substitution and native codegen; they remain authoritative. Preserve early returns, local scope and no-leak semantics. If checker and imported typed summary disagree, reject; don't silently amend the provider contract.

**D3c tests:** Int and String concrete specialization **from checked IR**, proof that each exported and private admitted body emits `MOSS_CHECKED_BODY_PROJECTED`, correct hygienic locals, repeated imports do not collide, shadowing/branching rejects malformed records, no successful fallback when the new body has unknown op/type/ordinal. Keep every previous H3, arithmetic, constructor, scalar, field, direct private-call source-free test green, including ABI-v9 control. All producer/artifact negatives should be rejected before a legacy statement can be selected.

**D3c commit:** importer/projector code, tests, diagnostics, push checkpoint. Do not claim the D3 milestone complete until Gate 4 executes true source-free binaries.

### Gate 4 — D3d: end-to-end source-free `index_of` acceptance

Extend `tests/phase25/source_free_corpus/corpus.json` and `tests/tooling/check_phase25_m2b2_source_free_corpus.py`; update the checked-body status of `collection-loop-equality-index` **only after** it runs without legacy bodies. Add a dedicated source-free test for the actual SDK exported `collections.index_of`, not merely the artificial `Service.fresh` equivalent, if package build/ABI staging supports it; prove both where possible and distinguish any packaging blocker.

Acceptance sequence for each independently compiled consumer:
1. Build provider with `MOSS_STAGED_ABI=v10` and existing feature emitter setting as applicable; seal interface + `.rlib` marker.
2. Remove provider `.moss` source from all lookup paths. Build consumer in a distinct directory.
3. Use the proven scoped poisoner to find the exact `index_of` (or corpus `fresh`) `generic_stmt`; assert **1 removed from that callable** and **0 remaining**, with other callables' counts unchanged. Poison checked private helpers too if any are in dependency closure.
4. Require checked-body projection trace for each admitted callable.
5. Run `index_of([8,3,8],3)==1`, `index_of([8,3,8],9)==-1`, and String vector `index_of(["oak","moss"],"moss")==1`, missing String `==-1`. Include empty vector, first/last match and duplicates where supported. Ensure consumers separately instantiate Int and String.
6. Corrupt each distinct semantic layer and require **deterministic negative**: remove summary or body; change callable/provenance/dependency hash; forged Count/Index/equality operation or requirement; mismatch `Vector[T]`, T, Int index/result; drop/forge `may_panic`; invalid local ordinal, mutability, forward reference, branch, noncanonical bytes; unsupported node; duplicate record; unsupported equality specialization; ABI v9/v10 mixing; emitter switched off.
7. Repeat normal source-free SDK smoke under default ABI-v9 to ensure no regression. No test may pass by reconstructing a legacy body.

**D3d PASS:** real provider source removed; no admitted legacy body; both element types execute native correctly; positive & negatives pass. Mark corpus row checked-body supported and report exact outputs. If only codec or synthetic fixture passes, D3d is **not complete**.

### Gate 5 — D4: generalize to `contains` and `count_equal`, narrowly

After Gate 4, inspect the actual existing SDK in `sdk/foundations/collections/src/collections.moss`. Both use one Count, one Index, equality, mutable cursor and a loop. `contains` returns `Bool` with early `true`/final `false`; `count_equal` accumulates an additional mutable Int counter and returns count. Generalize the same checker retention/codec/ABI/projection pipeline for these **semantic body patterns**, not via hardcoded exported function names or legacy statement copies. Preserve Count/Index/equality and conservative panic-effect handling. Expand the bounded D1 admission rule only to the proved operation inventory: additional concrete Int arithmetic/local updates are not an unsupported generic operation when closed. Avoid weakening the exactness check for extra open operations. Do not change D2's wire format unnecessarily.

**D4 tests:** compiled-only actual SDK provider under staged v10, no source, scoped poisoner for `contains`, `count_equal`, `index_of` separately; independent Int/String consumer calls (`contains` matches and misses; `count_equal` duplicates/no hits); each projected through checked-body; all malformed requirements/types/locals/effects reject; full previous corpus and default v9 SDK smoke still pass. Document any specific shape that cannot be supported without larger checker changes and stop just that expansion, not the already-green `index_of` chain.

**D4 commit:** focused implementation & real SDK acceptance, push checkpoint.

### Gate 6 — M2b.4: strict staged ABI-v10 generic admission, provenance and fallback removal

**Only after D3d is green.** Inspect `load_module_interface`, `verified_abi_v10_interface_records`, `project_imported_h3_checked_bodies`, generic-statement parsing, `append_abi_v10_*` producers, `Summary::provenance_id` hydration, R2b/R2c and ABI mode selection. Establish these invariants for **all v10 imported generic callables eligible for execution**:

- A callable is executable in staged ABI-v10 **only** if a complete, validated typed-v2 summary and matching checked body/provenance/dependency closure have been accepted. If not, reject with stable source-free checked-body missing/unavailable/invalid diagnostics. **Never execute via `generic_stmt`**, even if the artifact still contains that text for v9 compatibility.
- Distinguish imported open/generic functions from ordinary concrete providers: do not accidentally demand checked generic bodies for unrelated non-generic callable exports. Preserve source-free compiled concrete SDK behavior.
- `generic_stmt`, `generic_source` and other legacy fields may remain serialized as compatibility metadata, but in v10 they are **not semantic authority**, including callable origin. Hydrate diagnostic origin from validated provenance records; test tampered or removed legacy source fields with valid provenance and forged provenance with valid legacy fields.
- No producer-side opt-out may silently activate v10 legacy fallback: unsupported callables either fail staged provider emission where required or fail import/admission deterministically; exact choice and diagnostic should be documented and tested. Emitter switch-off behavior must not claim supported checked bodies. Do not change v9 default.
- Reverify previous parameter/scalar/field/call/constructor/arithmetic/empty-vector shapes and the new loop shapes with source removed and legacy poisoned. Maintain canonical hashes, dependency verification, purity/may_panic decisions and full v9 behavior.

**Inventory/gap decision:** In the current six-case corpus, `generic-range-iteration`/SDK `bytes.from_range` has `For`+assert+vector `push`+borrowed Range and is **not** covered by the Count/Index/While subset. Strict staged-v10 should reject it instead of reconstructing legacy. Do not silently mark it supported or remove it from the release ledger. If existing phase-level acceptance requires that behavior, mark overall Phase 25/cutover **BLOCKED** pending an L7 decision or independently bounded Range task. No unreviewed borrowed-Range ABI change overnight. The source-free corpus should explicitly distinguish admitted checked-body rows from intentionally rejected unsupported rows, rather than expecting old v10 legacy success.

**M2b.4 PASS:** all admitted generic forms require typed records, all unsupported generic forms reject instead of falling back, negative tampering cannot cause legacy execution, ABI-v9 remains unchanged, and a versioned acceptance matrix records what is still unsupported.

### Gate 7 — Focused G5/G6 readiness; prepare morning L7 review

Run **focused** build and testing after every compiler milestone; at the end run broad *relevant* shards, not the full repository suite unless L7 separately requests it:

- `make --jobs="$(nproc)" moss-dev` (build and capture warnings);
- D1 `tests/tooling/check_phase25_generic_collection_facts.py`;
- D2 `tests/tooling/check_phase25_d2_collection_loop_codec.py`, pre-D2 reader compatibility and checked-body IR/dependency suite;
- v10 source-free corpus `tests/tooling/check_phase25_m2b2_source_free_corpus.py`;
- source-free real SDK collection tests, including staged v10 and standard v9;
- H3 direct records, H3 private chain, checked-body importer/decoder, R2b/R2c, M2b.1 provenance, G2 (11/11), checked arithmetic, H3 Scenario 6 (expected output 99, source removed), typed semantic API baseline comparison;
- relevant `tests/shards/shard_abi_v10_units.sh` and other sharded ABI/generic tests if not too costly;
- `git diff --check` and agent-feedback validator.

Check each test runner's CLI/environment first; do not guess flags. Distinguish `PASS`, `FAIL_NEW`, `FAIL_BASELINE`, `NOT_RUN`, `BLOCKED` in the final table. The warning-suppression exception and typed-semantic `add_one` baseline failure must be explicit; don't call an unmodified failing suite a pass. Do not disguise a real new failure with broader warning flags. L7 will run the *full repository regression suite*, decide Phase 25 gate closure, decide whether `from_range`/borrowed-Range is a required blocker, authorize main merge, and separately decide ABI default cutover.

### Optional audit lane — borrowed Range / `from_range` (no implementation by default)

In parallel with D3a–D3c, a tests/docs-only agent may inspect `sdk/foundations/bytes/src/bytes.moss::from_range`, `docs/ROOT_RUNTIME_ABI.md`, existing borrowed-Range ABI restrictions and provider compile diagnostics. Return a bounded proposal: (a) generic checked `For`/assert/`push` statement requirements, (b) actual cross-crate borrowed-Range ABI dependency and whether it still exists, (c) source-free evidence needed, (d) whether Phase 25 exit criteria explicitly require its inclusion. Do not touch compiler sources or widen D3 to Range. Escalate a genuine runtime ABI contract change for L7 approval.

## Control loop and stop conditions

After each gate: (1) build; (2) run relevant positive and negative tests; (3) inspect `git diff --check` and untouched API contracts; (4) save concise commit with gate ID; (5) push feature branch; (6) update `.codex/CURRENT_STATUS.md` with baseline/HEAD, tested commands, errors, known limitations; (7) continue only if the current gate is demonstrably green. A skeptical agent can review the checkpoint by reading diffs/tests only, but must not concurrently edit core compiler files. Avoid repeating full focused battery for every tiny commit; do mandatory gate tests, then a broad battery at end.

**Hard STOP, push partial work and report immediately on:** required SHA mismatch or divergence; missing checker-owned semantics; irreconcilable requirements/effects/ownership mismatch; necessity to change Moss language design, borrowed-Range/runtime ABI or compiler-wide IR; ABI-v9 regression; inability to prove no legacy body use; unanticipated serialized schema migration; substantive test failures not reproduced on baseline; repeated failed fix attempts after two targeted tries per blocker. Preserve the last green checkpoint. Never claim a partial gate PASS to keep going.

**If a later gate blocks:** retain successful earlier checkpoint(s) on the overnight branch, run independent audit/test coverage if helpful, document the exact first failing invariant, and stop core implementation. Do not erase progress, push speculative hacks, or self-approve a scope expansion.

## Final morning handoff — required deliverables

1. Exact `phase25-integration` SHA after approved D2 integration; exact overnight branch HEAD; verify remotes, clean worktree and no main merge.
2. A gate-by-gate status matrix (Gate 0, D3a, D3b, D3c, D3d, D4, M2b.4, G5/G6): PASS / BLOCKED / NOT STARTED with commit SHA per gate.
3. Exact source files/functions changed; lines added/deleted per milestone; no second specialization engine or new language syntax.
4. Test matrix including complete commands, environment/ABI mode, runtime outputs for Int/String, projection trace evidence, how many `generic_stmt` entries removed **per callable**, all negative record mutations, v9 controls, and baseline-only exceptions.
5. A source-free admission matrix for parameter return, scalar literal, generic field, private call/chain, constructors, arithmetic, empty vector, `index_of`, `contains`, `count_equal`, `from_range`; label each **checked-body proven / v9-only / staged-v10 rejected / pending**. Evidence required for every “proven.”
6. Audit of typed-v2 requirement completeness, Count/Index/equality and panic-effect nonweakening, dependency/hash/provenance and absence of legacy executable authority.
7. Any unresolved defects with reproducers and line/function locations; estimate next engineering tasks by concrete behavior, not vague “work remaining.”
8. L7 decision requests: merge overnight feature branch into integration? Phase 25 acceptance readiness? Does `from_range` block? Full regression results still needed? Default ABI-v10 cutover **not authorized**.

**Success for overnight work is not a claim of Phase 25 completion.** The highest-value result is a credible, source-free native execution proof for actual generic SDK collection loops and a fail-closed staged-v10 path, all isolated for L7 morning review.

## One-line launch directive

**Execute this plan now, continuously and sequentially through the highest independently validated gate. Push each checkpoint; do not stop merely to request routine confirmations, but stop on the hard conditions above. Preserve one compiler writer, leave main untouched, and deliver a rigorous morning report rather than self-merging unreviewed code.**