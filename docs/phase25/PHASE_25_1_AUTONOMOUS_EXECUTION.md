# Moss Phase 25.1 — Long-Run Autonomous Execution Orders

**Authority:** L7 (user + ChatGPT), authorization to work autonomously on Phase 25.1 feature branches; **NOT** permission to modify `main`, change the default ABI, release ABI-v10 as default, or redefine Moss v0.1 semantics.
**First action, before any Phase 25.1 work:** create and independently verify the two immutable **annotated Git tags** specified in Gate T0 below.
**Repository:** `kuttyb/moss`
**Phase 25 delivered `main` baseline:** `dd45e30722bcc2d62d4bb46ba184d426cbaea066`.
**Main predecessor:** `fa3a340a0a6652568b97869489da1a84038b9857`.
**Phase 25.1 implementation/integration branch:** `phase25-1-work` (the kickoff document branch; derived from delivered main).
**Primary design contract:** `docs/phase25/PHASE_25_1_V10_READINESS.md`.
**Tracking:** https://github.com/kuttyb/moss/issues/13

## 0. Mission, scope and long-run autonomy

Ship a **complete, reliable, opt-in ABI-v10 source-free generic compilation implementation for the valid Moss v0.1 language**. The deliverable is not just a passing `bytes.from_range` fixture or a patchwork of specialized return/body shapes. Every real exported generic function that works under native source-backed compilation or with compiled-only ABI-v9 consumers is in scope, unless its semantics were previously unsupported/invalid and evidenced as such.

**Keep ABI-v9 the default forever during these orders.** ABI-v10 remains opt-in via `MOSS_STAGED_ABI=v10`. Do not delete or degrade ABI-v9; do not merge Phase 25.1 into `main`, create a v10 release tag, or change compiler defaults. Do not touch `phase25-integration` or the protected Phase 25 snapshot refs.

The L6 coordinator may **proceed independently through R0 -> R1 -> R2 -> R3 -> R4 -> R5** without waiting for a routine L7 checkpoint, provided the explicit acceptance conditions and fresh **independent L6 adversarial review** pass at each designated boundary. An earlier R0 handoff/stop instruction in the design charter and Issue #13 is **superseded by this autonomous authorization**. Record the analysis and decisions so L7 can audit in the morning. **Hard stops** in section 10 still apply. Do not treat autonomy as permission to dilute scope, hide failures or change the language.

Keep working until all accepted gates are green or a real hard blocker occurs. Avoid an arbitrary time or token cutoff. If a blocker affects only one lane, publish a blocked checkpoint and continue work on independent gates rather than idling. If a gate cannot be proven, mark it RED/UNVERIFIED, not PASS.

## T0 — Prerequisite: create the Phase 25 historical Git tags FIRST

Run from a clean, authorized Git checkout. These are **permanent annotated refs**. Do not use lightweight tags and do not overwrite or move an existing tag.

```bash
set -eu
git fetch --prune --tags origin
git ls-remote origin refs/heads/main refs/heads/recovery/pre-phase25-main refs/heads/releases/phase25-delivered-v9
test "$(git ls-remote origin refs/heads/main | cut -f1)" = "dd45e30722bcc2d62d4bb46ba184d426cbaea066"
test "$(git ls-remote origin refs/heads/recovery/pre-phase25-main | cut -f1)" = "fa3a340a0a6652568b97869489da1a84038b9857"
test "$(git ls-remote origin refs/heads/releases/phase25-delivered-v9 | cut -f1)" = "dd45e30722bcc2d62d4bb46ba184d426cbaea066"
git cat-file -e fa3a340a0a6652568b97869489da1a84038b9857^{commit}
git cat-file -e dd45e30722bcc2d62d4bb46ba184d426cbaea066^{commit}
```

For **each** tag, first inspect both local and remote names:

- `phase25-pre-main` must peel to `fa3a340a0a6652568b97869489da1a84038b9857`.
- `phase25-delivered-v9` must peel to `dd45e30722bcc2d62d4bb46ba184d426cbaea066`.
- If absent both locally and remotely, create it with `git tag -a <tag> <sha> -m "<meaningful release note>"` and `git push origin refs/tags/<tag>`. Create/push one tag at a time.
- If already present, verify remote `refs/tags/<tag>^{}` (peeled commit); require the expected SHA and do **not** move it. If the existing tag is lightweight or points elsewhere, **STOP** and report.
- Use `git ls-remote origin "refs/tags/phase25-pre-main^{}" "refs/tags/phase25-delivered-v9^{}"` after both pushes; verify the two peeled commit SHA values. Annotated tag-object SHA is not the underlying commit SHA.
- Confirm `origin/main` is **unchanged**, at `dd45e307...`, after pushing tags; do not run a merge or a push to any branch in T0.
- Include command transcript and verification evidence in `docs/phase25/PHASE_25_1_EXECUTION_STATUS.md` (on the Phase 25.1 branch only) or a dedicated artifact, then begin R0.

**Tag creation or verification is a hard prerequisite. Do not start R0 until T0 passes.** If Git credentials/permissions prevent annotated tag creation, STOP and report the exact permission blocker; do not use branch snapshots as a substitute for the requested tags.

## 1. Team setup — intentionally lean, token-efficient

**L7:** user + ChatGPT, strategic language/ABI decisions and main merges only. No fictional L8 orchestrator.

**L6 Coordinator / primary compiler implementer (one persistent Codex context):** owns plan, dependency ordering, implementation, commit/push, focused testing, local self-review and exact gate evidence. Prefer **medium reasoning** for straightforward implementation; **high reasoning** for typed IR, alias/ownership, ABI validity, specialization, constraint solving and architecture. Keep one authoritative compiler writer at a time to avoid repeated conflict resolution. Use fresh worktrees and small feature branches when useful; merge only independently validated work into `phase25-1-work`. Never merge to `main`.

**L6 Adversarial Architect (independent context, preferably different model/provider when available):** reads the exact pushed commit, new and retired paths, test evidence and contract; attacks semantic completeness, source/legacy fallback, binding identity, effects, ownership, private dependency closure, decoder validation and source-free proof. **Do not have this reviewer implement compiler code**. Required after R0 architecture design, after foundational R1, after R3, and before R5 acceptance; additionally after every ~3 substantive compiler commits/PRs or a design pivot. Coordinator must resolve all P0/P1 issues, rerun focused regressions and request a second review if needed. Reviews should identify concrete reproducers and actual defects rather than style comments.

**Optional cheap workers:** parallel read-only inventory, SDK coverage enumeration, reproducible failing fixtures, tests and evidence collection. Prefer low/medium effort. Each test writer owns distinct isolated test files/worktrees. Coordinator reviews and integrates; no competing writes to `src/moss.cpp` or shared compiler headers. Avoid spawning agents without independent work and measurable token savings.

**Progress/metrics:** gates passed/failed, exact SHA and branch, files changed, source-free cases added, per-gate tests (pass/fail), new compiler defects found, adversarial review findings/closures, number of subagents/PRs/review iterations, time to green and known exceptions. Keep a compact current-state file updated each checkpoint; don't produce verbose transcripts as primary reporting.

## 2. Gate R0 — Full empirical inventory and design (do first after T0)

The prior corpus is a starting seed, **not** the coverage boundary. Inventory **all existing exported generic SDK code and actual source-free uses** from compiler-owned semantic facts; enumerate the complete accepted Moss v0.1 generic statement/expression/effect/ownership surface, not only known hand-built fixtures.

Include `sdk/foundations` (especially bytes/collections), algorithms, encoding, text, parse, fmt, csv, compress, math/linalg/sparse/statistics/numerics, advanced-data (`ndarray`, `tables`, `datastructures`, `binary`, `cbor`), host/filesystem and any other shipped packages. Reproduce the **14-provider** advanced-data source-free closure from its actual acceptance harness. Track explicit/concrete exports separately from open generic exports; concrete precompiled functions must not be confused with generic body projection.

Build for each representative valid generic shape:
- **A:** source-backed native Debug/Release control;
- **B:** ABI-v9 provider/consumer compiled separately with provider source physically removed;
- **C:** staged ABI-v10 provider/consumer built freshly in separate artifacts/cache, source removed, selected and dependent legacy `generic_stmt` bodies poisoned or deleted, checked projection proven; native Debug/Release outputs compared.
- **D:** malformed artifact, dependency/effect/binding/ABI mismatch negative control when applicable.

Capture exact compiler SHA, provider and callable identity, module/source identity, provider build versus consumer import/specialization/lowering/runtime failure stage, diagnostic code and origin, method/operation/effect category, trace, output and log path. Use `valid`, `invalid-by-language`, `supported-v10`, `valid-but-unavailable-v10`, `unverified`, `environmental` statuses. Zero unexplained omissions.

Known gaps: `bytes.from_range` (`for`, assert, `push`, mutable owned result, borrowed Range), `fresh(): return []` and zero-arg `relay(): return fresh()`, `size(value): return value.length()`, custom generic method constraints, generic field/nominal use, private helper closure, branch/match/early-return, nonempty collection literal, map/queue ops, arithmetic chains, effects/raise, nested specialization and multi-crate identities.

Deliver `docs/phase25/PHASE_25_1_COMPATIBILITY_MATRIX.md`, machine-readable inventory and runnable harness, issue ledger with reproducer/owner/critical path, and **one coherent generic checked-body representation + emitter/decoder projection plan**. Identify existing per-shape emitters, bodies and routes to retire; no second type checker. Reviewer must approve **design invariants** and R0 factual evidence before R1 starts. R0 need not have zero failures; failures are the actual work queue.

**R0 autonomous gate:** If adversarial architect's P0/P1 findings are closed, proceed directly to R1 without waiting for L7. If a different Moss language meaning, unsupported v0.1 semantics, or incompatible wire format is needed, trigger hard-stop escalation.

## 3. Gate R1 — Unified authoritative checked-body representation

Make generic v10 execution derive from checker-owned typed semantics, not from parsing provider source-shaped legacy strings. Implement a **general traversal of supported generic AST/typed semantic nodes** (bounded to today's valid language) with canonical, versioned checked-body encoding and decoding.

Require correct canonical type variable binding/freshening, local variable/mutability identity, shadowing/scope, statement/control-flow nesting, parameter/return/nominal identities, type substitution, read/write/consume access, instance versus static member constraints, effects (including raise/assert/panic), and declared private dependencies. Capture complete operation and required side conditions; never mark a summary complete if an operator/method/effect was not retained.

The source-free importer validates typed summary/body/hash/provenance/dependencies **before** projection, and projects only into existing AST/checker/specialization/native lowering. No alternate generic execution engine or runtime. Robust size/nesting limits, canonical ordering, error diagnostics and invalid-artifact rejection. Prefer extending existing wire contracts safely; any incompatible schema/ABI-v10 wire change requires fresh negative/migration tests and independent architectural review before landing (stop for L7 if the meaning of the ABI contract changes).

**R1 acceptance:** Existing admitted H3/M2b.3/D1-D4/checked-arithmetic fixtures remain green, no v9 drift; target a coherent general typed IR round-trip plus a minimal cross-crate source-free executable slice proving it is not another shape-specific shortcut. Adversarial review on the consolidated path before progressing.

## 4. Gate R2 — Type variables, open results, generic calls and members

Close R0 valid failures involving:
- `fresh()` -> `Vector[T]` with call-independent type variables; no hidden `MOSS_PHASE25_EMPTY_VECTOR_EMIT` extra enable flag.
- Zero-arg `relay(): return fresh()`, multiple levels, static/private helper dependencies.
- `size(value): return value.length()`, selected generic member dispatch and member requirements.
- Generic fields and constructors, nominal identity, generic methods/traits permitted by current Moss semantics, cross-module type/provider identity, independent consumer crates.
- Int/String/Float/Bool and nested collection specializations; arithmetic and typed requirement edges, effects/provenance and source-free error attribution.
- Negative cases for wrong method/visibility/type, open-result conflict, forged/missing requirement/body/provenance/private dependency.

**R2 acceptance:** Existing v9 semantics preserved; v10 positive native Debug/Release execution with provider source removed and all executable legacy generic statements poisoned, plus typed semantic query parity and checked projection traces. Coordinate R2 tests with R0 oracle; no redefining Moss trait rules or adding a new record type.

## 5. Gate R3 — Complete observed valid generic control flow, mutation and effects

Drive implementation from the **R0 gap ledger**, not a narrower list. Cover valid `let/var`, assignments, indexed/member mutation, vector/map/queue operations, nonempty literals, `if/while/for`, early return, `match`, assertions and exceptions/effects where actually valid in generic code. Confirm structural control-flow identity, effect ordering, nesting, type narrowing, borrow/ownership lifetime, path-sensitive requirements and checked ABI representation.

**Mandatory:** make the **actual** `sdk/foundations/bytes::from_range` work as written from a source-free v10 provider. Run the real `range_copy` FileIO scenario, close the owner, then inspect returned owned vector. Prove its checked `for` element binding, `is_byte` validation/assert effect, receiver mutation and local returned value; test malformed/expired borrows and invalid element types. Determine empirically whether there is any real native cross-crate borrowed Range ABI issue; **do not** redesign runtime ABI preemptively.

For any R0 v9-valid generic body shape still red, implement the general typed projection or show an already approved R0 exclusion. A single-pass list of fixes is insufficient: rerun the discovered parity corpus and record newly exposed failures.

**R3 acceptance:** all R0 confirmed current SDK generic execution gaps closed or escalated; verified diagnostic/effects/ownership parity and a fresh adversarial review. Continue autonomously if review passes.

## 6. Gate R4 — Comprehensive SDK, cross-crate and hostile-artifact matrix

Run all real SDK source-free acceptance paths (not merely the lightweight smoke), plus the full v9-v10 parity corpus, in both native profiles on an **exact committed SHA**. Rebuild every provider from clean v9 or opt-in v10 artifacts, physically delete source, select compiled artifacts only, poison legacy executable bodies, verify trace, and run consumer executables. Include all advanced-data providers and all exported generic callables actually used; do not silently skip packages that only offer concrete exports.

At minimum, test independent consumers sharing provider-owned nominal identity, nested generic private helpers, visibility boundaries, transitive imports, stale cached artifacts, ABI v9/v10 rejection and rebuild, swapped provider `.rlib`, digest, requirement, body, binding, provenance, type, ownership, effect and private-dependency corruption. Differential test source-backed/v9/v10 execution and semantic diagnostics; add generated combinations of operators, locals, branches and nested generic calls. Check Native Debug and Release, and source-backed Fast Debug where semantically applicable. Never claim source-free Fast Debug support beyond current design.

**R4 acceptance:** machine-readable matrix shows green for every in-scope v9-valid generic source-free SDK workload and supported language generic shape, with no silent fallback. Any remaining uncertain/unverified case remains red and blocks readiness; do not reclassify it to “out of scope” without explicit L7 authorization.

## 7. Gate R5 — Readiness proof and final handoff (NO default change)

On the final exact pushed Phase 25.1 branch SHA:

- Focused v10 source-free corpus, poisoned legacy bodies, mutated hostile artifacts and compatibility matrix complete; SDK source-free distribution debug/release passes.
- Default v9 controls pass and fresh `native_abi 9` artifact is emitted with **no** opt-in flags. Fresh `native_abi 10` artifacts and checked specialization occur with **only** `MOSS_STAGED_ABI=v10`.
- Run `make check`, `make examples`, SDK `--smoke` and `--full` when feasible. **Do not mask known failures:** record full logs and classify existing Phase 25 parallel timing flakes using isolated reruns, controlled concurrency and baseline comparison. A new semantics/source-free regression is always a blocker.
- Test optimized and development compilers, native Debug/Release, cross-crate provider origin and consistent diagnostics; rerun cache-migration and per-callable fail-closed admission.
- Independent final adversarial review, all P0/P1 findings closed.
- Update the actual coverage matrix, callable/operation counts, unverified entries and compatibility contract. Report requested “62-gate” historical records only if run on this exact SHA; never infer evidence from old branches or unit codecs.
- Push branch, tag exact *internal checkpoint* as a branch commit only if useful; **do not** create a v10 production delivery tag or move `main` without L7 approval.

**Definition of SUCCESS:** all currently valid in-scope Moss generic SDK programming patterns work with a source-free native v10 consumer, using provider-validated checked semantics and without requiring provider source or executable legacy generic records. ABI-v9 still works by default. Produce a detailed L7 handoff with candidate SHA, evidence, known limitations (if any), source-free proof counts and proposed follow-up decisions.

## 8. Work DAG, checkpoints, budget and continuity

**Execution order:** T0 -> R0 -> independent architecture review -> R1 -> independent implementation review -> R2 -> R3 -> independent semantic review -> R4 -> R5 -> independent final review -> stop for L7 release/default decision.

Only independent inventory/test/SDK coverage workers run in parallel with the one compiler writer. On blocker, work around by collecting other lane evidence, not silently changing scope.

To reduce tokens:
- Use the compiler's typed semantic API and indexed test inventory instead of repeatedly reading full `src/moss.cpp`.
- Agent coordinator self-reviews bounded diffs. Architect review after substantive changes/gates, not every tiny commit.
- Run focused tests for each source change, a representative SDK/source-free subset every gate, and expensive full `make check` after cohesive R1/R3/R5 checkpoints (or when justified by risk). Avoid retesting the entire repo per small patch.
- Keep a rolling evidence log; deduplicate repeated test outputs and record hashes, exact commands, and failing excerpts.
- One persistent coordinator context; checkpoint/resume after model/tool context windows with a short concise state. At each gate, push a recoverable feature-branch SHA and refresh `docs/phase25/PHASE_25_1_EXECUTION_STATUS.md`. Resume from latest reviewed checkpoint, not from R0.
- PR count/back-and-forth and reviewer interventions should be tracked. Keep progress via issue #13 or an updated branch status record rather than requesting routine L7 confirmation.

## 9. Suggested gate artifact checklist

| Gate | Required artifact |
|---|---|
| T0 | Both annotated tags published and remote peeled SHAs verified, unchanged main |
| R0 | SDK source-free inventory, runnable 3-mode oracle, typed surface map, red failures, reviewer-approved architecture DAG |
| R1 | Generic checked IR architecture and codec/loader with adversarial and source-free proofs |
| R2 | Open result, members, constructors, helper chains; explicit positive and negative v10 proofs |
| R3 | `from_range`, iteration/branch/mutation/effects, R0 coverage closure and adversarial review |
| R4 | SDK-wide v9/v10 source-free and corrupt-artifact compatibility matrix on pinned SHA |
| R5 | Full regression/release evidence, independent review, ready-for-L7 decision; v9 still default |

## 10. Genuine hard stops and escalation policy

Stop before affected compiler implementation and ask L7 only if:
1. T0 tags cannot be created/verified at correct SHAs, or the delivered main/recovery SHAs no longer match the frozen release lineage.
2. A fix requires changing Moss user-visible language semantics, public generics syntax, trait/state rules, ownership rules, or the agreed v0.1 exclusions.
3. A new incompatible v10 ABI schema/provenance identity or actual change to physical borrowed-Range cross-crate ABI is necessary and no backward-compatible solution can be proved.
4. The checker cannot retain an essential semantic fact, so “general typed IR” would require speculative reconstruction, weakened validation, legacy fallback, or unsound type/effect assumptions.
5. Reproducible release-breaking v9 regression, hostile-artifact admission, mixed-provider cross-call security violation, or irreversible branch/ref mutation.
6. Adversarial reviewer reports unresolved P0/P1 correctness concerns after bounded focused iterations; or a required gate cannot be validated despite honest isolated retries.
7. The task would require touching `main`, modifying default ABI, changing immutable Phase 25 tags/branches, or deciding an unapproved exception to generic parity.

When blocked: push clean checkpoint to `phase25-1-work` or current child feature branch, log exact reproducer and red gate, continue only independent safe work, and deliver a concise actionable L7 request. **Do not** bypass validations, mask tests, weaken diagnostic checks, declare partial v10 support “ready”, or open an unapproved main PR/merge.

**Autonomy boundary:** L7 has delegated bounded feature-branch execution across all R0-R5. L7 has *not* delegated architectural changes in the hard-stop list, a default ABI switch, or main promotion.

## 11. Final completion message to L7

Report: tags and their peeled SHAs; `main` and Phase 25.1 HEAD; gates R0-R5 PASS/FAIL/BLOCKED; number and category of generic SDK callables covered; typed-body operation classes supported; actual source-free native Debug/Release results with provider source physically deleted; strict no-legacy evidence; v9 controls; `make check` and both SDK smoke modes; genuine known issues; SHA/branch/PR metrics, subagent count and reviewer findings; exactly what remains before opt-in approval or changing the ABI default.

Do **not** report that this completed asynchronously if no persistent Codex process was actually launched. These are execution orders; the Codex coordinator must run and checkpoint them.
