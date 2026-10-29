# Moss: Lazy Expression Tree (EST) — incremental adoption plan

Status: **owned by Phase 27 (Language Stabilization).** Base: `main` at `0ecfa51` (after the 26.3 `let`-alias merge). Phase 27.1 builds the tree and validates it in shadow mode with no consumer cut over; Phase 27.2 adds the semantic side tables (§10). Agreed direction Oct 27 2026; revised Oct 28 after Phase 27.1 review by both peers. Adopt piecemeal alongside feature work; no big-bang rewrite.

Primary justification: one authoritative expression grammar and per-occurrence identity (correctness). W1/C3 already removed much repeated parsing, so any further speedup is a by-product, not the reason.

## 1. Problem

The Moss front end has a statement-level AST but **no expression layer**:

- The lexer is line-based (`lex_lines`): lines + indent, no tokens, no column spans.
- `Stmt{kind, text, a, b, args}` keeps every expression as a **string**; `Raw` is a catch-all kind.
- Every consumer re-parses those strings with its own splitters:
  - checker: `inferred_expr_core(string)`, `parse_simple_call`, `split_top_level`, …
  - Rust emitter: its own `parse_simple_call` / splitters
  - Fast Debug interpreter: `eval(string)` splitting on `" or "`, `" and "`, pipelines, …
- Facts are keyed by **spelling**, not occurrence (`known_expression(text)`, the W1/C3 parse cache).
- Per-instantiation results are written **onto the AST** (~14 `*_by_context` maps on `Stmt`).
- Types cross boundaries as strings (`semantic_type`, `TypeEnv` string assignment; ~176 string↔TypeRef round-trips in the checker).

Consequences: three grammars that can diverge (correctness risk, parity gates), line-only diagnostics, name resolution by text matching (source of the C1 callback-reach bugs), and a parse cache that is effectively a per-check, spelling-keyed, one-level, checker-only expression tree.

## 2. Decision: branch by abstraction, built lazily

1. **Keep existing structures as they are.** `Stmt`, its strings and its maps stay valid throughout the migration. The only addition is the inline handle collection in item 2.
2. **Handles per expression slot, stored inline on the owner.** A statement can hold several expressions (`text`, `a`, `b`, each `args[i]`), and functions/methods carry `result_expression` outside their body. Each expression-bearing owner (`Stmt`, `Function`, `Method`, and any other owner found during 27.1a) carries a small, lazily populated collection of handles, conceptually `{ExprSlot slot, uint32 argument_index, ExprId expr, uint64 source_fingerprint}`. Inline storage (not an external map keyed by statement pointer or index) is what keeps handles valid when the checker makes shadow `Program` copies or passes insert and elide statements. The container can be optimized later.
   - Slots are **semantic**, not struct fields: `Stmt.a` may be an `if` condition, a return value or a binding name depending on `Stmt.kind`, and `Stmt.text` often holds the whole original statement. Define an `ExprSlot` enum (e.g. `Condition`, `Initializer`, `ReturnValue`, `Iterable`, `Receiver`, `Argument`, `LhsPlace`, `RhsValue`, …) with a per-`Stmt::Kind` mapping from fields to slots; enumerate it as consumers migrate.
   - **Places are distinct from values.** Assignment targets and other places parse as structured projection chains (`base.f[i].g[j]`): the root place can receive WRITE or disjoint-field projection, while each index sub-expression is an independent READ occurrence with its own `ExprId`. This is what S3 (§1b, issue 2 and issue 7 of the Phase 27 ownership doc) will consume; it is cheap at construction time and expensive to retrofit.
   - **`Raw` is classified on first touch.** Building the tree for a `Raw` statement classifies its statement-level form once (`Assign`, `CompoundAssign`, `ExprStmt`, …) and binds its `LhsPlace` / `RhsValue` slots, so no later consumer re-classifies the text.
3. **One compilation-owned, append-only arena** of compact nodes (e.g. `std::variant` node kinds). Nodes are immutable once constructed. **No sharing of identical subtrees across occurrences**, so every node is an occurrence. An arena index is a sufficient in-compilation identity; stable identity across edits is not needed (no eviction).
   - **Copy contract.** A copy that represents the *same source occurrence* (the checker's shadow `Program` copies, per-specialization body handling) shares the immutable syntax nodes and keeps its `ExprId`s. A copy that creates a *new logical occurrence* (optimizer duplication or transformation) allocates new `ExprId`s even when the text is identical, so two distinct expressions never share semantic identity. The 27.1a API states this contract explicitly.
4. **Lazy construction:** the first visitor (reach walk, checker, emitter, or interpreter) builds the owner's tree and fills the handles; later visitors follow the handles and never re-parse.
5. **Move consumers one at a time.** Some consumers read the tree while others still read strings; both code paths coexist until the last consumer moves.
6. **Then delete** the string parsers, the parse cache, and the expression strings.

## 3. What the tree carries

- **Syntax:** node kind, children, literals, operators; places as structured projection chains (§2.2).
- **Spans:** line plus column offsets within the statement (handle continuation lines). Nodes built from expressions loaded out of `.mossi` carry a package/interface origin ID instead of current-file offsets, so cross-package diagnostics point at the imported contract.
- **Occurrence identity:** the arena `ExprId`, reached from its owner's handle.
- **Lexical resolution:** each identifier bound once to its declaration identity (replaces text matching). Built and validated separately from syntax (§10): syntax parity is the 27.1 shadow obligation; full lexical-binding parity needs scope-aware resolution and is validated where implemented, as conservative candidates.
- **Specialization and ownership facts:** in a **separate side table keyed by (node ID, specialization context)**, replacing the `*_by_context` maps. S3's ownership effect (READ/WRITE/CONSUME and temporary-view lifetime) lives in this table too, because `Copy`-ness and M1 boundary modes depend on instantiated types. Kept separate so a future HM (2.0) move deletes the table without touching the tree. This is Phase 27.2 (§10).
- Types as `TypeRef` throughout; no string types in the tree.

**Layering — keep syntax and checked IR distinct:** expression tree (untyped syntax) → checker + semantic side tables → checked IR (ABI-v10 `checked_body_ir::Expr`) → ABI/execution consumers. Borrow operator enums and patterns from `checked_body_ir`, but do not reuse it as the syntax tree: it carries resolved types, specialization-dependent facts and a versioned ABI contract.

## 4. Safety rails

- **Runtime flag, not `#ifdef`:** `off` (default; today's string path) / `shadow` (build the tree and assert at every lookup that the tree's answer — children, resolved target, and once side tables exist, type — equals the string path's answer) / `on` (consumer reads the tree).
- **Shadow mode is a corpus gate.** A divergence between the tree and the string path has exactly three outcomes: an EST bug (fix before acceptance); an existing-compiler inconsistency (minimal reproducer, adjudicated against the language spec, allowlisted only with a tracking issue); an unexplained discrepancy (blocks acceptance). The gate is **zero unexplained divergences**, not merely zero construction failures. The string path is the behavioural baseline, not the definition of Moss; the spec decides which side is right.
- **Coverage counters per expression slot.** Every slot that should be parsed is counted, so a green shadow run cannot be hiding skipped expressions. Accepted expressions in the covered checking scope must all be representable; rejected programs keep their expected diagnostics and are not required to pass construction after a legitimate earlier rejection.
- **Rebuild invariant (text-rewriting passes).** Passes that rewrite statement text (module-name rewriting, the functional optimizer, including under `-O`) run on the tree as follows: invalidate the handle in the modified owner; parse or construct the replacement into **new** arena nodes; bind the handle to the new `ExprId`. **Rebuild = append to the arena and update the slot handle; never erase or mutate existing nodes.** Old nodes stay valid for other references until the compilation ends. Once side tables exist (27.2), a rebuilt occurrence derives fresh semantic facts; obsolete facts may stay allocated but are never reused for a transformed occurrence without revalidation. `-O` output is inside shadow validation, not excluded from it.
- **Staleness guard:** record a source fingerprint at build time; in shadow mode assert the text is unchanged at every lookup, so a rewrite that skipped the rebuild step fails loudly.
- Use asserts and lint rules to keep new code from adding string re-parsing.

## 5. Migration order (producer/consumer aware)

Consumer migration begins in Phase 27.2 or later; nothing in this section is Phase 27.1 scope.

1. **Reach walk (C1/R11)** — once call syntax is covered. It runs *before* checking, so it consumes **syntax plus lexical candidate lookup** (declarations are known after parsing), never type-dependent targets (e.g. which method a receiver type selects). Ambiguity still adds every candidate. Gate: candidate edge sets and checked-function identities equal the current C1/R11 results. Replaces string-scanned edges (`add_callable_argument_references`).
2. **Checker** — reads the tree and **dual-writes**: fills the tree's side tables *and* keeps populating the `Stmt` `*_by_context` maps for unmigrated consumers.
3. **Rust emitter** — reads the side tables.
4. **Fast Debug interpreter** — evaluates tree nodes instead of `eval(string)`.
5. Stop dual-writing; retire string helpers and W1 cache entries one at a time as their last consumer migrates.

Whether the emitter may read EST facts for migrated slots before the checker has fully migrated ("vertical slices", raised in Phase 27.1 review) is decided when 27.2 opens, not here.

**Exit criterion (hard, not a stretch goal):** zero consumers *interpret* expression strings by their own parsing. Source text and spans may be retained for diagnostics, IDE tooling and display — that is not string debt. Do not leave a permanent string fallback (the ABI-v10 lesson: a partially covered typed form plus a string fallback never gets cut over).

**Boundary of the criterion:** ABI record decoding, source lexing and source-text preservation are separate operations and stay. But expression strings *stored inside* `.mossi` (ABI-v9 `generic_stmt` bodies, Section 2 execution bodies) are source expressions: on load they must go through the same expression-tree grammar into the arena, not through a separate splitter. Existing `.mossi` contracts stay compatible unless deliberately versioned.

New features (e.g. the Graph DSL, Phase 28) are built tree-first.

## 6. Rules and non-goals

- **Laziness is in construction, not coverage.** Package mode (`margo build --check`, native builds) visits every statement, so every tree is built and every syntax error is reported. ECT commands build trees only for reached statements (consistent with C1: unreached errors are omitted in ECT).
- **No eviction.** `moss check` is a short-lived process; memory is released at exit. Revisit only if a long-running daemon is built.
- **Threading:** `moss check` is single-threaded; Margo parallelism is per process with separate arenas, so no races today. If the checker is ever parallelized in-process (very late, if ever): build trees in a parallel per-file phase, then check with trees read-only — or use a per-handle once-flag. File sharding alone covers building, not checking (checking reads across files).
- **Out of scope:** HM-style inference (language 2.0), eviction, in-process parallel checking.

## 7. Gates and progress metric

Baseline the **gates** (not a timing study) at the starting commit `0ecfa51` and keep them green at every step: 2,064-source corpus differential (only the documented 12 C1 + 3 T1 rows, unchanged Rust hashes), C1 corpus gates, Fast Debug corpus parity with zero R12 hits, Margo fingerprints, `.mossi` Section 1/2 hashes and `.locations` behavior, `make check`. Compiler-perf is a no-regression guard only and runs with the flag `off`; shadow-mode overhead is recorded, not gated.

Track "string debt": count of call sites using string expression splitters (`parse_simple_call`, `split_top_level`, `strip_redundant_outer_parentheses`, `parse_enum_case_expression`, interpreter `eval(string)` paths) per consumer. It goes to zero per consumer as each migrates. Shadow-mode corpus gate must stay green at every step.

## 8. Precedents

- Branch by abstraction (incremental replacement behind a switch).
- V8 lazy parsing (pre-scan, full parse on first use).
- rustc query system / Salsa (demand-driven construction).
- rustc `TypeckResults` (types in side tables keyed by node ID, not on the tree).

## 10. Phase 27 milestones

**27.1 — tree and occurrence identity, redundant.** Internal split, one public phase:

| Milestone | Deliverable |
|---|---|
| 27.1a | Expression ADT, append-only arena, inline handles on every expression-bearing owner, `ExprSlot` and per-kind slot mapping, copy contract, initial parser; EST code in dedicated source files |
| 27.1b | Complete syntax coverage (arithmetic, nested calls, collection operations, indexing, member access, pipelines, placeholders, constructors, places), spans, `.mossi`-loaded expressions with origin IDs |
| 27.1c | Shadow parity, rebuild invariant under `-O`, coverage counters, string-debt baseline, corpus gates |

27.1 completes when: (1) every accepted expression occurrence in the covered compilation modes is representable, no silent skips; (2) zero unexplained shadow divergences, approved legacy discrepancies tracked; (3) `off` and `shadow` preserve verdicts, diagnostics, Rust output, Fast Debug behaviour and `.mossi` contracts relative to `0ecfa51`; (4) rewriting passes invalidate and rebuild handles correctly, including under `-O`; (5) string-debt and per-slot coverage counts are committed; (6) no production checker, emitter, reach-walk or interpreter consumer has been cut over. Parsing coverage is complete; semantic resolution (type-dependent member selection, specialization, ownership) is not attempted.

**27.2 — semantic side tables.** The (node ID, specialization context) side-table API with usable, validated facts: specialization results, `TypeRef`s, and the S3 ownership effect per occurrence. Empty infrastructure does not satisfy 27.2; S3 needs authoritative facts. Consumer migration (§5) starts here.

Then S3 enforcement (Phase 27), then the Graph DSL (Phase 28) on a finished tree and finished S3.

## 9. Revision log

- Oct 28 2026 (Phase 27.1 review, both peers GO): owned by Phase 27, base `main` at `0ecfa51`; inline handles on every expression-bearing owner; copy contract (same occurrence shares, new occurrence allocates); append-only arena with the rebuild invariant; places as structured chains; `Raw` classified on first touch; three-outcome divergence rule and per-slot coverage counters; `.mossi` origin IDs; `-O` inside shadow; S3 ownership facts in the specialization-keyed side table (27.2); §10 milestones and exit contract; vertical slices deferred to 27.2.
- Oct 27 2026 (2nd peer round): semantic `ExprSlot` enum with per-kind field mapping; handles valid across `Program`/`Stmt` copies; exit criterion made hard; `.mossi`-stored expressions must load through the same grammar; base branch `phase26-1-m1`.
- Oct 27 2026: revised after peer review — per-slot handles, compilation-owned arena, no stable-ID promise, reach walk on syntax + lexical candidates, syntax tree kept distinct from checked IR, exit criterion = zero string-parsing consumers (text/spans may stay), gate baseline instead of timing study.
