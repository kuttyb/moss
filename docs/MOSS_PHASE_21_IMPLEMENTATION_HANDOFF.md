# Moss Phase 21 — Independent Review and Agent Implementation Handoff

**Date:** October 9, 2026  
**Design baseline:** `MOSS_PHASE_21_ERROR_HANDLING.md`, consolidated v4.5.2.  
**Status:** Design approved, implementation not yet established. This is a review/handoff supplement, **not a replacement for the normative specification**.

## 1. Independent-review disposition

**Verdict:** Proceed to interface freeze and implementation. The handler-level typed recovery, Root-only fresh `on_fail` interval, open nesting, source-free provider separation, ordered Branch outcome model, and O1–O9 proof obligations are coherently specified. Paper proofs remain conditional on implementation evidence.

### Required editorial clarification — synchronization class repartitioning

Part II §3 step 5 and Part III P10 describe `on_fail` as not adding an exclusive class to the normal handler. While the `h_fail` *effects* are correctly excluded from `h`'s own effects, the *global class partition* is computed from the complete per-handler mode-signature vectors (v0.1 §24). Adding a pseudo-handler may split a class that the normal handler previously held exclusively into two or more classes that it must also hold exclusively. Thus `on_fail` may change the **count** of exclusive lock acquisitions without adding new normal-path WRITE **leaf requirements**.

Concrete conceptual witness: normal handler `h` writes both `x` and `y`; neither is accessed differently by other handlers; they originally have identical exclusive signatures and share a class. Add `h_fail` which writes `x` but not `y`; the signatures diverge, and `h` now acquires two exclusive classes rather than one. Both plans are ranked and deadlock-safe. Revise wording to: "The fresh `on_fail` effects are not unioned into the original handler's state-effect footprint and do not increase its per-leaf write requirements. As with any new writer or new signature column, they can change the protected mutable universe or synchronization-class partition, including adding shared locks or changing the number of locks (including exclusive locks) acquired by normal handlers."

Add class-partition regression: with one handler writing two leaves, show adding a failure pseudo-handler writing one leaf splits class signatures while retaining correct locks and ranked acquisition.

### Tutorial/spec example correction — avoid a misleading unreachable-arm warning

Part I Example 10 and the corresponding Part II §21 cross-enum example open and close a file without writing it, yet include `on_fail FileError.Full`. Because Phase 21 requires **operation-precise** inferred failure sets and warns on well-formed unreachable arm patterns, `Full` may be unreachable in that specific program, defeating the example's purpose. Part II §21 additionally parses the constant literal `"bad"`, which may be statically known to raise before any FileIO operation. Replace these examples with a dynamic parse input and a bounded file **write/sync** path so both `ParseError` and `FileError.Full` are plausibly reachable. Keep this as an example repair, not a change to the six-variant FileError taxonomy. Validate the actual inferred effects, rather than asserting open/close always exclude `Full` without an OS-site audit.

### Numeric contract freeze checks (not design redesign)

- Part II §13.2: canonical shortest-round-trip Float formatting now chooses the nearest decimal value and an even final significand digit on an exact tie. Keep adversarial halfway/tie vectors in the golden suite and pin the algorithm independently of host Rust formatting.
- Part II §13.3: establish an explicit accepted finite decimal token grammar in tests (e.g. `.5`, `1.`, `1e+3`, uppercase `E`, signed zero, leading zeros) so Rust/Interpreter parsers cannot quietly diverge on forms not explicitly covered by the text. Do not impose a new grammar by inference in an agent; seek approval for any previously unsettled token spelling.

These are contract-completeness checks, not objections to the central exception-handling architecture.

### Proof review

- **Accepted as conditional paper proofs:** Lemma 1; Lemma 2; Theorems A1, A2, B1, B2; Lemma Q; conditional progress B3; separate intervals B4. Keep Phase 20 assumptions A0–A7 and `SYNC-FAIR-001` caveat.
- **Must prove by implementation:** O1–O9. In particular, exceptional CFG edges (O4) must feed guard cancellation, early-acquisition placement, D7, ownership, and FileIO teardown; never only the code generator. Parent Branch slots are observed only after join (O9), and all Root-local files die before old guards drop (O6).
- **Other proof debt remains explicitly open:** §16 indexed Branch-outcome ordering extension to Phase 20 Theorem 3; §42 early-release reconciliation; runtime verification of no `Range`/`RangeBatch` escape.

## 2. Hard dependency: Phase 21.0 — shared representation/ABI contract

**Single owner:** lead / 21.0 interface agent. **Worktree suggestion:** `phase-21-0-effects-abi`.

Before Agents A–D integrate changes, produce a small committed, reviewable contract documenting:

1. **Effect domain.** Exact concrete C++ representation of existing `ObservableEffects`, with `may_panic` and stable sorted/normalized variant-precise `raise_set`. Specify unions, removal for handled variants, re-raise, unresolved providers, and context-specific `chunk_speculation_safe`. **Preserve** `fusion_safe()` as the ordinary eager-reordering barrier.
2. **Tagged outcomes.** Generated Rust sum representation and stable tag + payload layout for normal values, scalar error types, and enum variants. Error payloads are bounded, owned, no capabilities/borrows. Define how a concrete specialization with several error enums encodes its internal tagged value without exposing a cross-enum source type. Define error identity and propagation across helpers, nested messages, provider callables and Root wrappers. Panics are separate and non-catchable.
3. **Role-separated wrappers.** Provider exports handler body, plus separately materialized failure-arm bodies and metadata. Consumer application emits (i) nested wrapper, which never calls `on_fail`; (ii) Root wrapper, which quiesces, closes Root-local FileIO, releases old guards, acquires `h_fail`, dispatches typed arm, and fulfills reply contract. No panic catching/unwinding implementation.
4. **Captures and D7.** ABI for owned, definitely initialized, legal values made available to a failure-arm body **after** the attempt's frame ends. Forbid FileIO, Range, RangeBatch, guards, protected borrowed views. Plan arm-refined D7 for variant-specific paths. Preserve provider-private types safely through provider-exported signatures and calling convention.
5. **Semantic interfaces.** `.mossi` version, serializable effect/arm pattern/capture/reply records, canonical specialization hash, missing-record fail-closed behavior, and full prior-ABI→v8 rebuild requirement. Version number only after the representation is frozen.
6. **Diagnostics/CFG schema.** Identify syntactic raising nodes, exceptional-edge wiring, which analyses consume them, and how selected-error slots scope across nested `recover` arms. O4 must be checkable as an independent verifier test.

**Gate 21.0 passes only if:** a design/ABI header and example native Rust signatures are reviewed, a source-free provider fixture can be planned unambiguously, owners A–D have agreed on names/paths, and there are no unexplained version/interface dependencies. Commit only this interface contract first; do not merge divergent tagged-outcome implementations.

## 3. Suggested parallel worktrees and agent briefs

### Agent A — Core syntax, inference, D7 and Root semantics

**Worktree:** `phase-21-a-recovery`  
**Depends on:** 21.0.  
**Primary ownership:** compiler parser, AST/IR, inferencer, handler/root lowering, `moss check`, `.mossi` integration with interface agent.

Instructions:

- Parse `raise value`, bare `raise` only in an active `recover`; `try` with >=1 arm; typed enum payload patterns; grouped tag-only arms; final `recover:`; handler-level contiguous `on_fail` trailer arms, typed and final catch-all.
- Track exact selected variant through grouped/re-raise and prevent sibling arms from intercepting errors raised inside another recovery arm.
- Infer per-specialization effects and transitive `raise_set` through helper and routed nested messages. Inferred effects of ordinary `Result`-like enum return values remain ordinary data.
- Build exceptional CFG edges before D7, guard cancellation, early lock placement, ownership/move checking, and codegen. Ensure source-free providers expose the same edges via metadata.
- Distinguish Root from nested invocation; only the Root wrapper dispatches failure arms; no new Root admission from inside a handler.
- Implement pseudo-handler `h_fail` signatures and separate ClassSet. Include every `recover` access in `h`'s own current-domain entry locks in adequate mode; warn accurately on recovery-only entry inflation, including previously lock-free fields.
- Implement two-interval failure protocol under O5–O8; file teardown itself is B-owned, but A owns the wrapper sequencing and handshake. Every normal completion path of value-returning Root failure arms must produce a correctly typed `reply`.
- Apply path-refined D7 to captures visible at trailer; reject capability/view crossings. Keep `main` and Root totality, test blocks' special unhandled-raise behavior, and `on_fail` empty escaping raise set.
- Warn for empty catch-all after possible writes and unreachable but valid variants; error for unknown variants, duplicates/overlaps, bad arm order, bad attachment, bare try, bare raise outside `recover`, missing Root coverage and missing reply.

**Agent A acceptance:** O1, O2, O3, O4, O5, O7, O8; same `Get` local-recover typed result as Root and nested; source-free typed-arm fixture; both warnings and hard diagnostics; class-repartition regression above. Coordinate wrapper O6 with Agent B.

### Agent B — FileIO raises, RAII and deterministic failure injection

**Worktree:** `phase-21-b-fileio-errors`  
**Depends on:** 21.0 tagged runtime interface; coordinates O6 with A, O9 with C.  
**Primary ownership:** `src/fileio_runtime.hpp`, `src/fileio_semantics.inc` and dedicated native fault-injection tests.

Instructions:

- Enumerate actual OS failure/abort sites for `open`, `read`, `write`, `sync`, `close`, created-file parent-directory sync, and registry/path operations. Keep programmer misuse as a non-catchable fatal panic.
- Map by cause to **exactly six** public variants: `NotFound`, `PermissionDenied`, `NotRegularFile`, `InUse`, `Full`, `IO`. Per-operation inferred sets must be no broader than supported failure sites, but conservative where truly reachable.
- Keep same `(device,inode)` collision immediate `InUse` without waiting/retry; `InUse` is never a retry signal. Maintain registry claim cleanup on partial `open` failure.
- Preserve partial-write/no-rollback and failed-sync uncertain-durability contract; `sync` must permit `Full` and `IO`; rewriting and re-syncing on same handle remains legal. Explicit close errors raise; secondary destructor close errors never override the original cause.
- Define precise Rust lexical RAII teardown for FileIO opened inside `try` on exceptional exit; FileIO opened before `try` stays live through local recovery; at abandoned Root, close all Root-local FileIO before guards drop and before `on_fail`. Never transfer a view/capability to cleanup; retain explicit close on normal exit.
- Add test-only deterministic fault injection for failure before/after prefix write, read, write, sync, close and parent dir. Keep it out of production behavior and do not implement a second Fast Debug POSIX backend.

**Agent B acceptance:** O6 with Agent A; deterministic fault injection, no stale inode claims, exactly-once resource teardown, grouped `Full, IO` recovery, broken-domain-field misuse remains fatal, open fallback goes to distinct file, no lock/blocking InUse retry. Native tests with error variants and successful checked close.

### Agent C — Branch outcome ordering and separate speculative legality

**Worktree:** `phase-21-c-branch-errors`  
**Depends on:** 21.0 and tagged FileIO I/O results from B (C's analysis can begin immediately after 21.0).  
**Primary ownership:** `src/file_chunk_lowering.inc`, `src/file_chunk_codegen.inc`, functional effects/eligibility passes.

Instructions:

- Implement **separate** `chunk_speculation_safe` eligibility. Leave ordinary `fusion_safe()` semantics intact; typed raises remain an ordering barrier for eager map/filter fusion.
- Preserve `K`-bounded window. Each Branch produces indexed `Value`, `Raised`, or EOF/short outcome; read failure skips mapper. Parent joins entire window **before inspecting any slot** and commits strictly by index, with parent fold ordered. Discard later outcomes after first in-order terminal event.
- Preserve no-domain-mutation/no-FileIO-in-map/no-illegal-capture constraints, plus proof of no possible speculative panic. Conservative sequential fallback if proof incomplete or unavailable provider facts.
- Encode P21-10A: each invoked chunk mapper receives nonempty `Range`; contextual `chunk[0]` proven only at that invocation site, never erase generic helper may-panic. Reject `chunk[1]` under possible 1-byte last chunk.
- Encode P21-10B: stable `for i in range(0, r.length())` same-range indexing safe, but rebinding, shifted indices, different range or changed induction variable invalidate proof. Confirm compiler loop semantics and source-free analyzer before changing inference.
- Verify tagged outcomes and Branch read-borrow tokens are owned/scoped so all token lifetimes end by join. The Root wrapper must never observe a raised slot with live siblings.

**Agent C acceptance:** O9 plus no speculative panic; early raised chunk and later speculative outcomes; parent fold error wins over later maps; zero EOF, short chunk, read errors, map errors, fixed deterministic outcome fixture; eager fusion order remains unchanged.

### Agent D — Numeric/parsing, ownership hardening, Fast Debug parity, docs

**Worktree:** `phase-21-d-language-parity`  
**Depends on:** 21.0 effect/error type interfaces, A syntax for full tests.  
**Primary ownership:** numeric runtime/interpreter, parsing prelude, `Range`/`RangeBatch` parameter checker, documentation migration and parity suites.

Instructions:

- Implement IEEE Float non-panicking division with ±inf, NaN, signed zero; `is_nan`, `is_finite`; Moss-defined shortest-round-trip Float formatting and finite decimal lexical grammar after outstanding tie/grammar decisions are frozen.
- Strict ASCII-only `parse_int` and `parse_float`: enumerated surrounding ASCII whitespace, single between-digit underscores, exact `inf`, `-inf`, `NaN`, overflow/underflow contracts; `to_int` truncates toward zero and raises NonFinite/Overflow instead of host casts.
- Repair Fast Debug Float divide-by-zero mismatch and add supported-general `raise`, `recover`, `on_fail` tests in concert with A. Do **not** claim FileIO parity for Fast Debug without a backend. Preserve test-only process abort behavior vs ordinary direct-body `assert` test failure.
- Reject inferred WRITE on formal `Range` and `RangeBatch`, including across generic/source-free specialization; leave supported WRITE ordinary parameters alone. Run the documented `RangeBatch` escaping view reproducer as a negative checker test.
- Migrate v0.1 §§32–33, §42 and Appendices A/B, preserving panics as fail-closed; update raising serializability and untouched-lock/observable barriers. Include the independent unsupported `!` checker bug (#2) only if assigned as separate cleanup; do not mix into Phase 21 semantics.
- Add eventual Fast Debug `for` parity (bounds once, half-open), or explicitly track as pending if not in the phase's implementation scope.

**Agent D acceptance:** native/Fast Debug numeric conformance and golden corpus, parsing accepted/rejected spellings, `to_int` cases, both view WRITE-negative regressions plus ordinary WRITE-positive, proof/documentation updates, test harness split for assertions and typed raises.

## 4. Merge/integration DAG and ownership boundaries

```text
21.0  effects / tagged outcome / semantic & ABI contract
  |
  +-- 21.A  parser + inference + exceptional CFG + Root/on_fail wrappers
  |
  +-- 21.B  FileIO typed failures + abnormal-exit RAII + fault injection
  |
  +-- 21.C  indexed Branch outcomes + separate chunk eligibility
  |
  +-- 21.D  numeric/parser + view ownership + Fast Debug + docs
  |
  `-- Integration gate 21.I (do not merge independently without contract checks)
          A+B: quiesce -> close -> drop -> fresh acquire -> dispatch
          B+C: scoped FileIO borrow/token lifetime and join order
          A+D: native/Fast Debug raised-effect parity and test harness
          All: provider-source-removed ABI v8; make check; make examples
```

Do not give multiple agents concurrent write ownership of `ObservableEffects`, ABI serialization, Rust tagged outcome types, or the same lowering entry. Land 21.0 first. Each workstream should rebase to the frozen contract instead of editing the contract in its own branch.

## 5. Non-negotiable integration matrix

| Gate | Mandatory witness |
|---|---|
| O1 | Recover/callee/helper effects transitive and variant precise, including source-free metadata. |
| O2 | `h_fail` has separate signature column/effects/ClassSet; repartition and normal-path lock counts correct. |
| O3 | No current-domain late `recover` acquisition or dynamic upgrade; ranks and Phase 15.3 barriers preserved. |
| O4 | Exceptional CFG edges affect D7, protected-borrow lifetimes, guard cancellation and 2PL planning. |
| O5 | Every raised handler exit materializes owned error then releases exactly its own guards. |
| O6 | Branch quiescence, Root-local FileIO close, old guard drop, fresh `h_fail` plan, selected arm, typed reply, all ordered. |
| O7 | Nested call never executes trailer; no Root admission inside any handler, including `on_fail`. |
| O8 | Trailer captures are owned/definitely initialized and contain no FileIO, Range, RangeBatch or guard-borrow. |
| O9 | Each Branch window joined before first outcome access; zero outstanding read tokens at propagated raise/teardown. |
| ABI | Native source-free provider compiles with private typed failure arm, payload, owned capture, full .mossi records; consumer builds wrapper; missing old metadata fails closed; version bumped with clean rebuild. |
| Totality | All ingress Roots and main total; one-way Roots report via domains; synchronous Root replies typed; `executor.join` remains a drain. |
| Stability | `make check`, `make examples`, native rustc positive/negative tests, owned resources leak checks, Fast Debug supported subset, docs and approval checklist. |

## 6. Launch brief for coordinator/lead agent (copy/paste)

> Implement Moss Phase 21 from the consolidated `MOSS_PHASE_21_ERROR_HANDLING.md` v4.5.2. Treat its Parts I–III, 18-item backlog, V4-01–04, and O1–O9 as normative. First finish and commit **21.0**: concrete `ObservableEffects` may-panic/raise-set representation, tagged result/error propagation ABI, provider `.mossi` metadata, separated Root/nested wrappers, `on_fail` arm export/capture ABI, and exceptional CFG design. Make the interface names and native signatures explicit and review them before coding broadly. Then launch independent worktrees 21.A recovery/locking/CFG; 21.B FileIO error conversion and RAII/fault injection; 21.C ordered Branch outcomes and `chunk_speculation_safe`; 21.D numeric/Float parity, Range/RangeBatch WRITE safety, and documentation. Preserve Phase 20 invariants, do not weaken `fusion_safe()`, never retry `FileError.InUse`, and never execute `on_fail` as part of a nested handler. Reconcile at 21.I with source-free provider ABI tests, deterministic failure injection, proof gates O1–O9 and `make check`/`make examples`. Do not claim completion from design-level proofs alone. Report commits, test evidence, uncovered blockers, and any requested spec changes rather than silently modifying agreed semantics.
