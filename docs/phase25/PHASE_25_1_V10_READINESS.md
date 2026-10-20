# Moss Phase 25.1 — ABI-v10 Readiness and Complete Generic Source-Free Parity

**Status:** Gate R0 kickoff on the delivered Phase 25 baseline; implementation not yet started
**Baseline:** `main` at `dd45e30722bcc2d62d4bb46ba184d426cbaea066` (Phase 25 delivery; ABI-v9 default; source-free v10 opt-in; Char/String SDK fixes). Release validation accepted with two intermittent parallel-suite failures, both passing in isolation.
**Working branch:** `phase25-1-work` (forked from Phase 25 delivered `main`)
**Repository:** `kuttyb/moss`

## 0. Decisions and non-negotiable boundaries

1. **ABI-v9 remains the default throughout Phase 25.1.** ABI-v10 is explicitly selected with `MOSS_STAGED_ABI=v10`. Do not silently change either default, delete ABI-v9 support, or merge to `main`.
2. Phase 25.1 is **not** a cosmetic release checklist and is **not** complete merely because all previously supported narrow v10 fixtures pass. Its objective is to close the functional gap between **valid native source-backed and ABI-v9 source-free generic Moss** and **ABI-v10 source-free native generic specialization**.
3. v10 must take **authoritative checked semantics** from the provider artifact. No executable fallback to `generic_stmt`, `generic_source`, source-shaped expression reparsing, or speculative inference from untrusted interface text. Legacy records may remain for v9 compatibility and diagnostics; do not execute them in v10.
4. Reuse the **one Moss checker/type solver, specialization, ownership/effects and native lowering pipeline**. Do not add a second generic specialization engine, a new user-visible generic declaration syntax, dynamic dispatch, or an arbitrary parallel compiler frontend.
5. Preserve **per-callable fail-closed admission**: importing a package containing an unsupported generic should not block a different supported callable, but selecting any unsupported callable must reject deterministically.
6. Do not mistake passing `make check` (which intentionally tests both ABI modes) for complete ABI-v10 parity. Existing source-free SDK suites often enforce `native_abi 9`.
7. The existing `docs/phase25/ABI_V10_CUTOVER_AUDIT.md` was written before several M2b.3 / D3 / M2b.4 deliveries. Treat it as historical evidence, not a current coverage inventory. Update/reconcile rather than copying its obsolete classification.
8. Keep source-free Fast Debug's established native-dependency limitation and normal compiler target restrictions intact. Phase 25.1 does not add source-free Fast Debug execution or revive features invalid in Moss v0.1.

## 1. Definition of a clean generic implementation

Phase 25.1's compatibility baseline is the **set of generic functions and language constructs valid under the current Moss language contract**, with first priority on existing v9 source-free working SDK/package workloads. A valid, checked generic body should not become unexecutable solely because a consumer imports it from a compiled-only v10 provider.

A successful v10 path must:

- Check the provider source and retain one canonical typed generic function representation, including lexical binding identity, complete result/parameter TypeRefs, operations, requirements, effects, visibility, ownership and dependency closure.
- Serialize a canonical checked body, type and requirement summary, callable IDs, dependency hashes and source provenance into the sealed v10 provider artifact.
- Load, structurally and semantically validate, substitute and project the checked body through the **existing** Moss checker/specialization/native pipeline.
- Produce the same native-debug and native-release observable results and compiler diagnostics as the corresponding valid source-backed or ABI-v9 source-free workload, subject only to intentional and documented language-contract differences.
- Work with the provider's source directory **physically deleted** and the selected callable's legacy `generic_stmt` removed or poisoned. A passing output without this test is not v10 source-free proof.
- Reject malformed/forged identities, hash chains, requirements, access and effect records, provenance and body structure without executing legacy statements.

**Release standard:** No **known working ABI-v9 generic SDK workload** or representative valid Moss generic form may remain unsupported in v10 without an explicit L7-approved scope exception. A ledger of untested forms is not equivalent to a green parity test.

Do not conflate generic body limitations with existing unsupported language features. Source-backed-invalid code does not have to become valid in 25.1.

## 2. Gate P25.1-R0 — Inventory and compatibility oracle (first deliverable)

Before writing a new emitter, build a machine-readable **v9 versus v10 source-free parity matrix**, not a guessed list.

### Audit the entire shipped SDK and its source-free consumers

Start with:
- `sdk/foundations/{core,collections,algorithms,bytes}`, encoding, text, parse, fmt, csv;
- `sdk/{ndarray,datastructures,tables,binary,cbor}`;
- packages for numerics, math, sparse, linalg, statistics, other existing SDK dependencies;
- all `tests/**` and `sdk/**` source-free provider/consumer harnesses.

Enumerate exported generic callables through compiler-owned/typed semantic facts, not regex guessing. Identify each actual source-backed or v9 compiled-only consumer and every generic operation it instantiates. Capture zero-argument open-result functions, member constraints, generic nominal methods, collection and nested generic functions.

For each workload, in **clean isolated builds**:
1. Compile and execute source-backed control using the current compiler.
2. Compile the provider with default ABI-v9; delete source; run native debug and native release consumers; establish actual baseline result.
3. Rebuild the provider and consumer from scratch under staged v10; delete provider source; poison/remove executable `generic_stmt`; require checked-body trace; run both native profiles.
4. Record whether v10 fails at provider checking, checked-body emission, interface sealing, import validation, specialization, native lowering/linking, runtime execution, or diagnostic comparison.
5. Record exact provider/callable identities, operation class, command, return code, diagnostic, output hash and compiler SHA.
6. Distinguish expected-invalid, unsupported-but-valid, test-mode mismatch, and actual behavioral regression.

The matrix must include coverage for every SDK provider involved in the existing 14-provider advanced-data source-free graph. Existing smoke tests that assert ABI-v9 must be retained as v9 controls and **cloned/adapted**, not silently converted.

The R0 coordinator must publish: `docs/phase25/PHASE_25_1_COMPATIBILITY_MATRIX.md`, machine-readable test inventory, failure grouping, a bounded implementation DAG and an ordered critical path. Preserve all red entries; do not call an inventory scan a successful acceptance result.

**Known R0 seeds** (not an exhaustive list):
- `sdk/foundations/bytes::from_range`: generic `for`, assert, local vector construction/mutation, borrowed Range semantics; v10 provider rejects.
- Zero-argument open-result `fresh()/relay()` chain; existing v10 tests intentionally reject `relay`.
- Generic member call `size(value): return value.length()`; existing strict v10 tests reject `size`.
- Nonempty vector, Map/Queue construction and operations; generic nominal methods, nested calls, additional control-flow expressions, matches and effectful bodies where source-backed/v9 valid.
- `MOSS_PHASE25_EMPTY_VECTOR_EMIT`: identify whether the extra flag is still required for otherwise valid v10 open empty-vector functions; readiness must not depend on hidden per-shape switches.
- Existing v9 source-free advanced-data, encoding, algorithms, collections and FileIO Range acceptance paths.

## 3. Gate P25.1-R1 — Generic checked-body architecture consolidation

Avoid fixing every red row by adding a new `checked_<shape>_body`, `append_abi_v10_<shape>_records` or projector case that independently reconstructs semantic facts.

Implement an **exhaustive typed checked-body lowering path for the supported Moss generic statement/expression algebra**, supplied by authoritative checker facts. It may be incrementally activated by node category, but its traversal, local binding IDs, dependency capture and record generation must be **generic** rather than recognizing a handful of source/program shapes.

Requirements:
- Deterministic typed IR node identity, dense local ordinals, scoped implicit/explicit declarations, mutability, captures, argument bindings, join/branch identities and checked TypeRefs.
- Uniform typed expression lowering: literals; variable/parameter/local; unary/binary; field and index; vector/Map/Queue literals and constructors; named static calls; member dispatch; collection operations; functional pipelines as their checked operations.
- Uniform typed statement lowering: let/var/implicit new assignment, later assignment and indexed/member mutation, if/while/for, return (including early return), and other statements actually observed in valid R0 generic functions (including match/assert/raise/try-recover where supported).
- Preserve operation identities, read/write/consume modes, inferred requirements, effects including may_panic/raise, nominal provider identity, private dependency and provenance edges. No optimistic pure/effect weakening.
- Reuse existing canonical `checked-body-v1` additive tags if backwards-compatible and fail-closed; otherwise provide complete versioned schema and hash compatibility evidence for L7 before changing wire version.
- Bound node counts, recursion and nesting; validate lexical binding and control-flow structure, canonical encoding, semantic hashes and type-substitution relationships. Pass the projected AST back through the normal Moss checker before native emission.
- Fail closed for any genuine missing semantic fact. No claiming summary completeness because a subset of operations matched.
- Preserve exact previously validated H3, D1-D4 and checked-arithmetic behavior; don't loosen their adversarial tests.

**Architectural review gate:** Before broad R1 landing, compare new path to old shape-specific emitters/importers. Demonstrate one source of truth and a credible route to retiring unnecessary per-shape paths, with no accidental behavior change under ABI-v9.

## 4. Gate P25.1-R2 — Open results, member constraints and generic composition

Make genuine positive source-free v10 tests for:
- `fresh(): return []` with independently instantiated concrete element types.
- `relay(): return fresh()`, multi-hop chains and private helper dependency closure, including zero-argument calls.
- `size(value): return value.length()` and selected generic member requirements.
- Generic parameter return, scalar/field, composed arithmetic and direct private call chains already supported; preserve existing proofs.
- Nominal generic instances/methods, correct receiver/template visibility, multiple independently compiled consumers sharing provider-owned type identity.
- Open-result ambiguity and conflict rejections, negative unsupported methods, requirement call chain and provider/consumer diagnostic attribution.
- No additional emitter opt-in other than `MOSS_STAGED_ABI=v10`.

All positives: provider source removed, legacy body poisoned, checked projection trace, Int/String or two distinguishable concrete specializations, debug/release native execution. Mutation negatives must verify callable, body, requirement, dependency and provenance identity.

## 5. Gate P25.1-R3 — Structured iteration, effects and mutation

Close the remaining valid generic SDK operations found in R0. In particular, make `sdk/foundations/bytes::from_range` execute **as written** from a source-free v10 provider; do not rewrite the SDK to a special case.

Required `from_range` evidence:
1. Typed `for` iteration and loop-element binding; `Vector[Int]()` local construction; `is_byte` helper; `assert` and its failure effect; `result.push(byte)` mutable receiver; returned owned vector.
2. Real `sdk/foundations/bytes/examples/range_copy` FileIO consumer; provider source deleted and legacy bodies removed; verify output bytes, owned-copy independence after closing FileIO, valid borrow lifetime and Native Debug/Release.
3. Determine empirically whether the borrowed Range is only a **consumer-local specialization** or whether the actual native call ABI needs a borrowed Range across crates. Do not redesign the Phase 20 borrowed-Range ABI merely based on speculation.
4. Reject invalid/expired borrows, conflicting mutation, mismatched iterator element type, omitted/forged assert effects and malformed dependency edges.
5. Add other generic effectful/control-flow forms only when R0 shows they are valid supported v9/source-backed workloads; track each in the parity matrix.

No special exception or blanket purity assertion for the Range workload.

## 6. Gate P25.1-R4 — SDK-wide, cross-crate and adversarial acceptance

Re-run the **whole** R0 parity matrix against the same committed candidate SHA:
- Fresh, isolated provider artifacts in each ABI mode; no stale cache or ambient module path.
- All actual SDK packages/consumer fixtures, not just the three collection helpers.
- Provider source deletion, no path-based rebuilding, checked-body trace, full legacy poisoning for every admitted generic callable and dependent helper.
- Native Debug and Native Release; multiple concrete instantiations; independent consumer crates; private dependencies; provider-owned nominal type identity.
- ABI-v9 controls remain green. ABI-v10 rejects ABI-v9 binaries with `SOURCE_FREE_ABI_REBUILD_REQUIRED`, with tested rebuild/migration path.
- Fail-closed mutations: summary/body mismatch, weakened effects/access, missing or forged private dependency, invalid nominal identity, wrong native provider, stale/replaced artifact, key-proof record and provenance corruption, bad control-flow or binding ordinals.
- Cross-check semantic introspection and diagnostics after sealed import.
- Differential/source-backed parity plus generated/adversarial combinations of supported generic forms; a mere list of successful hand-written shapes is insufficient.

Every admitted v10 callable must remain executable after complete removal of its own legacy statement payload. Every unavailable callable must reject rather than silently reconstruct legacy code.

## 7. Gate P25.1-R5 — Opt-in release readiness, NOT default promotion

To call Phase 25.1 v10-ready, require:
- **Zero unapproved ABI-v10 regressions** relative to existing working ABI-v9 source-free SDK/generic functions; all R0 red rows closed or explicitly escalated for L7 scope decision.
- All committed corpus and SDK compatibility tests green in both native profiles; no `MOSS_PHASE25_*` shape-specific enable flags required for normal v10 execution.
- `make check` passes without skipped/masked functional failures, plus broader staged-v10-only acceptance; separately run `make examples`, SDK source-free matrix and applicable cross-crate live gates.
- A reconciled release report distinguishing historical 62-gate bookkeeping from **fresh executable acceptance evidence**. Do not claim 62/62 solely from `make check`.
- Verified source-free v10 migration, module cache invalidation, ABI-v9/v10 mismatch and provenance guarantees.
- Documents updated to the actual coverage and remaining intentional language/engine limitations.
- A separate independent L6 adversarial review of the consolidated checked-body path and v10 artifact loader.
- `phase25-integration` and `main` unchanged until independent L7 authorization; ABI-v9 remains default after the Phase 25.1 opt-in readiness milestone.

The eventual v10 default switch is a **separate L7 decision** after opt-in stability, SDK distribution/migration assessment and final release approval.

## 8. Operating model and delivery

L7 is the user and ChatGPT. **L6 Coordinator** owns the ordered DAG and implements or delegates **one compiler-code writer at a time**. Optional testing/inventory workers may use separate worktrees and files. Independent L6 adversarial reviewer runs at the R1 architectural gate, after R3 semantic coverage, and at final R5 readiness; measure actual defects found rather than optimizing for reviewer count.

All implementation work on `phase25-1-work` or temporary child feature branches. No direct edits to `main` and no automatic integration into `phase25-integration`. Commit, push and report exact SHAs per green gate. Prefer focused tests during implementation; L7 controls when to demand full regression runs.

**First delegated task:** R0 inventory and honest compatibility matrix, with minimal test harness extensions where necessary. No immediate broad compiler rewrite. Return the actual failing callable/family list and proposal for the generalized IR bridge before R1 implementation. After L7 accepts architecture, an overnight coordinator may run R1-R4 autonomously gate-to-gate unless a semantic or ownership invariant is missing.

## 9. Non-goals

- New explicit generic parameter/trait syntax, generalized new record type, dynamic polymorphism, or Phase 25.5 language rules.
- Source-free Fast Debug execution where Moss v0.1 explicitly requires source.
- Unlimited future AST constructs absent from valid current Moss; don't block this phase on features not yet in the language.
- Removing v9 ABI readers/artifacts from the compiler or flipping the ABI default.
- A false sense of completeness based on test counts alone.

**Success statement:** A native consumer can use the generics that Moss already supports—even across SDK modules with provider source deleted—under either v9 or opt-in v10, with v10 using validated typed checked bodies and never executable legacy reconstruction.
