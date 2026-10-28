# Moss: Lazy Expression Tree (EST) — incremental adoption plan

Status: agreed direction (Oct 27 2026), revised after peer review. Adopt piecemeal alongside feature work; no big-bang rewrite. Kept separate from finishing Phase 26.1/26.1.1; base branch is `phase26-1-m1` (after its merge), not the pre-26.1 `main`.

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

1. **Keep existing structures as they are.** `Stmt`, its strings and its maps stay valid throughout the migration.
2. **Add handles per expression slot, not per statement.** A statement can hold several expressions (`text`, `a`, `b`, each `args[i]`), and functions/methods carry `result_expression` outside their body. Key each handle by **(owner, statement, semantic slot, argument index)** mapping to an `ExprId` (empty = not built yet). No hash ID.
   - Slots are **semantic**, not struct fields: `Stmt.a` may be an `if` condition, a return value or a binding name depending on `Stmt.kind`, and `Stmt.text` often holds the whole original statement. Define an `ExprSlot` enum (e.g. `Condition`, `Initializer`, `ReturnValue`, `Iterable`, `Receiver`, `Argument`, …) with a per-`Stmt::Kind` mapping from fields to slots; enumerate it as consumers migrate.
3. **One compilation-owned arena** of compact nodes (e.g. `std::variant` node kinds). **No sharing of identical subtrees across occurrences**, so every node is an occurrence. An arena index is a sufficient in-compilation identity; stable identity across edits is not needed (no eviction). Handles must stay valid across ordinary `Stmt`/`Program` moves and copies within a compilation (e.g. the checker's shadow `Program` copies, per-specialization body handling): the arena is compilation-owned and append-only during lazy construction, individual nodes are immutable once built, copies share it, and any copy whose text is rewritten must drop and rebuild its handles (covered by the staleness guard).
4. **Lazy construction:** the first visitor (reach walk, checker, emitter, or interpreter) builds the statement's tree and fills the handle; later visitors follow the handle and never re-parse.
5. **Move consumers one at a time.** Some consumers read the tree while others still read strings; both code paths coexist until the last consumer moves.
6. **Then retire** independent expression-string parsers, their now-unused parse-cache entries, and obsolete parsed-expression string fields. Retain original source text and spans for diagnostics and tooling.

## 3. What the tree carries

- **Syntax:** node kind, children, literals, operators.
- **Spans:** line plus column offsets within the statement (handle continuation lines). Preserve original source offsets through lexer normalization; normalized expression-text offsets alone are not source columns.
- **Occurrence identity:** the arena `ExprId`, reached from its (owner, statement, slot) handle.
- **Lexical resolution (side table):** associate each identifier occurrence with its lexical binding or conservatively collected declaration candidates. Type-dependent target selection happens later during checking, not inside the syntax tree.
- **Specialization facts:** in a **separate side table keyed by (node ID, specialization context)**, replacing the `*_by_context` maps. Kept separate so a future HM (2.0) move deletes the table without touching the tree.
- **Types:** the syntax tree carries no resolved types. Checked semantic side tables use `TypeRef` rather than type-name strings; the tree remains untyped across specializations.

**Layering — keep syntax and checked IR distinct:** expression tree (untyped syntax) → checker + semantic side tables → checked IR (ABI-v10 `checked_body_ir::Expr`) → ABI/execution consumers. Borrow operator enums and patterns from `checked_body_ir`, but do not reuse it as the syntax tree: it carries resolved types, specialization-dependent facts and a versioned ABI contract.

## 4. Safety rails

- **Runtime flag, not `#ifdef`:** `off` (today) / `shadow` (build the tree and compare expression syntax at parse boundaries, candidate edge sets during reachability, and resolved targets/types only after checking; any mismatch fails the gate) / `on` (the migrated consumer reads the tree).
- **Shadow mode is a corpus gate**, so the tree is validated against the current checker and cannot rot while unused.
- **Staleness guard:** passes that rewrite statement text (module-name rewriting, functional optimizer) must run before trees are built — or move onto the tree. Record a text hash at build time; in shadow mode assert the text is unchanged.
- Use asserts and lint rules to keep new code from adding string re-parsing.

## 5. Migration order (producer/consumer aware)

1. **Reach walk (C1/R11)** — once call syntax is covered. It runs *before* checking, so it consumes **syntax plus lexical candidate lookup** (declarations are known after parsing), never type-dependent targets (e.g. which method a receiver type selects). Ambiguity still adds every candidate. Gate: candidate edge sets and checked-function identities equal the current C1/R11 results. Replaces string-scanned edges (`add_callable_argument_references`).
2. **Checker** — reads the tree and **dual-writes**: fills the tree's side tables *and* keeps populating the `Stmt` `*_by_context` maps for unmigrated consumers.
3. **Rust emitter** — reads the tree plus checked semantic side tables.
4. **Fast Debug interpreter** — evaluates tree nodes instead of `eval(string)`.
5. Stop dual-writing; retire string helpers and W1 cache entries one at a time as their last consumer migrates.

**Exit criterion (hard, not a stretch goal):** zero consumers *interpret* expression strings by their own parsing. Source text and spans may be retained for diagnostics, IDE tooling and display — that is not string debt. Do not leave a permanent string fallback (the ABI-v10 lesson: a partially covered typed form plus a string fallback never gets cut over).

**Boundary of the criterion:** ABI record decoding, source lexing and source-text preservation are separate operations and stay. But expression strings *stored inside* `.mossi` (ABI-v9 `generic_stmt` bodies, Section 2 execution bodies) are source expressions: on load they must go through the same expression-tree grammar into the arena, not through a separate splitter. Existing `.mossi` contracts stay compatible unless deliberately versioned.

New features (e.g. the Graph DSL) are built tree-first.

## 6. Rules and non-goals

- **Laziness is in construction, not coverage.** Package mode (`margo build --check`, native builds) visits every statement, so every tree is built and every syntax error is reported. ECT commands build trees only for reached statements (consistent with C1: unreached errors are omitted in ECT).
- **No eviction.** `moss check` is a short-lived process; memory is released at exit. Revisit only if a long-running daemon is built.
- **Threading:** `moss check` is single-threaded; Margo parallelism is per process with separate arenas, so no races today. If the checker is ever parallelized in-process (very late, if ever): build trees in a parallel per-file phase, then check with trees read-only — or use a per-handle once-flag. File sharding alone covers building, not checking (checking reads across files).
- **Out of scope:** HM-style inference (language 2.0), eviction, in-process parallel checking.

## 7. Gates and progress metric

Baseline the **gates** (not a timing study) against a pinned, post-Phase-26.1 starting commit and keep them green at every step: 2,064-source corpus differential with **zero new verdict, diagnostic, or Rust-hash differences relative to that starting commit**, C1 corpus gates, Fast Debug corpus parity with zero R12 hits, Margo fingerprints, `.mossi` Section 1/2 hashes and `.locations` behavior, `make check`. The documented 12 C1 + 3 T1 changes are historical differences against pre-26.1 `main`, not allowances for new EST regressions. Compiler-perf is a no-regression guard only.


Track "string debt": count of call sites using string expression splitters (`parse_simple_call`, `split_top_level`, `strip_redundant_outer_parentheses`, `parse_enum_case_expression`, interpreter `eval(string)` paths) per consumer. It goes to zero per consumer as each migrates. Shadow-mode corpus gate must stay green at every step.

## 8. Precedents

- Branch by abstraction (incremental replacement behind a switch).
- V8 lazy parsing (pre-scan, full parse on first use).
- rustc query system / Salsa (demand-driven construction).
- rustc `TypeckResults` (types in side tables keyed by node ID, not on the tree).

## 9. Revision log

- Oct 27 2026 (2nd peer round): semantic `ExprSlot` enum with per-kind field mapping; handles valid across `Program`/`Stmt` copies; exit criterion made hard; `.mossi`-stored expressions must load through the same grammar; base branch `phase26-1-m1`.
- Oct 27 2026: revised after peer review — per-slot handles, compilation-owned arena, no stable-ID promise, reach walk on syntax + lexical candidates, syntax tree kept distinct from checked IR, exit criterion = zero string-parsing consumers (text/spans may stay), gate baseline instead of timing study.