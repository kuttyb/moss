# Phase 27.1a kickoff — Expression Tree (EST) foundation

Date: Oct 28 2026. Base: `main` at `0ecfa51`. Work branch: `phase27-1-est`. Design: `docs/MOSS_LAZY_EXPRESSION_TREE.md` (read §2, §3, §4, §10 before writing code; the Oct 28 revision is authoritative). No PRs; review off the remote branch; all gates on the work machine.

## What 27.1a delivers

The tree exists, is built lazily, and nothing reads it yet. Specifically:

1. **Expression ADT and arena.** A recursive expression type (`std::variant` node kinds or equivalent) in a compilation-owned, append-only arena. Nodes are immutable once constructed. No subtree sharing: every node is one occurrence. `ExprId` = arena index.
2. **`ExprSlot` and the per-kind slot mapping.** A semantic slot enum (`Condition`, `Initializer`, `ReturnValue`, `Iterable`, `Receiver`, `Argument`, `LhsPlace`, `RhsValue`, …) and, for every `Stmt::Kind`, the mapping from `text`/`a`/`b`/`args[i]` to slots. Places are slots distinct from values.
3. **Inline handles on every expression-bearing owner.** `Stmt`, `Function`, `Method` (for `result_expression`) and any other owner found carry a lazily populated collection of `{ExprSlot, argument_index, ExprId, source_fingerprint}`. No external map keyed by statement pointer or index. Existing fields untouched.
4. **Copy contract, stated in the API.** A copy of the same source occurrence (checker shadow `Program` copies, per-specialization handling) shares nodes and keeps `ExprId`s. A copy that creates a new logical occurrence (optimizer duplication or transformation) allocates new `ExprId`s even for identical text.
5. **Rebuild invariant.** Rebuild = invalidate the owner's handle, construct the replacement into *new* arena nodes, bind the handle to the new `ExprId`. Never erase or mutate existing nodes. Asserted, not documented only.
6. **Initial parser.** Enough of the grammar to build trees for the statement forms in `examples/` and the Phase 26 benchmark fixtures: literals, identifiers, arithmetic and comparison operators, `and`/`or`/`not`, calls with arguments, member access and indexing as structured projection chains (`base.f[i].g[j]`, index sub-expressions as independent occurrences), parentheses. `Raw` statements are classified once on first touch (`Assign`, `CompoundAssign`, `ExprStmt`, …) and their `LhsPlace`/`RhsValue` slots bound. Pipelines, placeholders, constructors, collection literals and `.mossi`-loaded expressions are 27.1b.
7. **Runtime flag `MOSS_EST=off|shadow|on`.** `off` is the default and is a no-op (no arena allocated, no handles filled). `shadow` builds trees at the points where the string path parses and records per-slot coverage counters and construction failures. `on` is reserved and rejected in 27.1.
8. **Lint.** A check that fails `make check` if a new call site of `parse_simple_call`, `split_top_level`, `strip_redundant_outer_parentheses`, `parse_enum_case_expression` or an interpreter `eval(string)` path is added relative to the committed string-debt baseline (deliverable 9).
9. **String-debt baseline.** Committed count of those call sites per consumer (checker, emitter, reach walk, interpreter) at `0ecfa51`.

Keep all EST code in dedicated source files (`src/est/*.cpp`, `src/est/*.h` or the repository's equivalent). Touch existing files only to add the handle field, the flag plumbing, and the shadow build calls.

## What 27.1a does not do

- No consumer reads the tree: checker, emitter, reach walk and interpreter behave byte-for-byte as before in every mode.
- No semantic side tables, no `TypeRef`s on nodes, no ownership facts, no lexical resolution beyond what the parser needs for structure. That is 27.2.
- No grammar unification: when the tree and a string splitter disagree, record it (see Divergences); do not change the string path.
- No deletion of anything.

## Divergences (applies from the first shadow run)

Every mismatch between the tree's structure and the string path's interpretation gets one of three labels, recorded in `docs/phase27/EST_DIVERGENCES.md` with a minimal reproducer:

| Label | Treatment |
|---|---|
| EST bug | fix before 27.1a acceptance |
| Existing-compiler inconsistency | reproducer + which side the language spec supports; allowlisted only with a tracking entry, adjudicated by Bank |
| Unexplained | blocks acceptance |

The string path is the behavioural baseline, not the definition of Moss. Do not "fix" the tree to match a string splitter the spec contradicts; record it.

## Gates for 27.1a acceptance (on the branch head)

- `make --jobs="$(nproc)" check` fully green, no waivers, with `MOSS_EST=off`.
- With `MOSS_EST=shadow`: the 2,064-source corpus differential, C1 gates, Fast Debug corpus parity and `.mossi` Section 1/2 hashes identical to `off`; zero construction failures on the statement forms in scope; coverage counters reported per slot; divergence file committed with zero Unexplained entries.
- `MOSS_PERF_BASELINE=0ecfa51 make compiler-perf` with `MOSS_EST=off`: no slowdown beyond the noise floor. Record shadow-mode overhead separately; it is informational.
- Byte-identical generated Rust between `off` and `shadow` across the corpus and `examples/`.

## Stop rules (record why in the status doc, push the branch, stop)

- A change to language semantics, the `.mossi` format or the ABI would be needed.
- The inline handle cannot be added to an owner without changing existing fields or copy behaviour.
- A divergence cannot be classified as EST bug or existing-compiler inconsistency after one honest investigation.
- `make check` is not green on the branch after one honest attempt to fix.
- compiler-perf shows a slowdown beyond the noise floor with `MOSS_EST=off`.

For anything else, decide, write the decision and its reason in the status doc, and keep going. No timing studies.

## Final report (print it and commit it to the status doc)

1. Owner types that received handles; any owner found beyond `Stmt`/`Function`/`Method`.
2. `ExprSlot` list and the per-kind mapping table.
3. Statement forms covered by the initial parser; forms deferred to 27.1b.
4. Per-slot coverage counters from the shadow corpus run.
5. Divergence table: counts per label; reproducer paths.
6. String-debt baseline per consumer.
7. Gate results with commit hashes; compiler-perf baseline named.
8. If a stop rule fired: which, where, and what decision is needed.

Keep every excerpt under 10 lines.
