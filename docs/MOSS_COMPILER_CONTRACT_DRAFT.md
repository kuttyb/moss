# Moss compiler contract (DRAFT, Oct 31 2026)

The compiler counterpart to the Moss language docs: the architectural invariants every stage must hold. Each invariant names the gate that enforces it. Status reflects the state at the start of Phase 27.5.4.

## Invariants

1. **One parse.** Each expression's source text is parsed exactly once: at the initial parse of its source file, or at the initial load of an imported .mossi body. Diagnostics, fmt and edit tools are exempt.
   - Gate: parse-boundary counter (all stages, including the checker) = 0 after the initial parse. Status: counter live; target pending (27.5.4).

2. **The tree is the source of truth.** Every rewrite after parsing (desugaring, renaming, specialization substitution, optimizer rewrites) operates on tree nodes. Expression text is derived from the tree, never the reverse.
   - Gate: text-to-tree rebuild counter = 0. Status: counter live (baseline 10,095); target pending (27.5.4).

3. **Nodes are passed, not text.** Stages pass node IDs between functions. No stage looks up a node by expression text or text fingerprint, including through text-keyed caches.
   - Gate: text-lookup counter (cache hits and misses included) = 0. Status: pending (27.5.4).

4. **The checker is the only semantic authority.** Only the checker decides: expression types, call targets, specializations, pipeline types, parameter effects and passing, clone/move/borrow, control-flow joins, pattern meaning, lock requirements. Other stages execute or lower those decisions; they never make them. Executing (computing a + b) and lowering (choosing Rust syntax) are not deciding.
   - Gate: per decision class, a perturbation test (change the checker's answer; every engine follows or fails closed) and a drop test (remove the answer; every engine fails closed). Status: types, targets, specializations, pipeline types pending (27.5.4); joins, patterns, locks need tests; ownership classes pending (27.6).

5. **Answers live on every node.** The checker records its answer on every node it types, per specialization context, and every node stays findable by ID.
   - Gate: coverage counter = 100% of consumer questions answered. Status: baseline 8.3%; target pending (27.5.4).

6. **Code created after checking carries answers.** Desugaring, optimizer rewrites and Fast Debug invocations are built as nodes and are either checked or carry checker-consistent answers.
   - Gate: covered by gates 1, 3 and 5.

7. **Engines agree.** Fast Debug, native and optimized native produce the same observable behavior: output, exit status, failures and trace events.
   - Gate: three-engine check, Fast Debug/native parity, .mossi parity. Pending: trace-event parity; seeded program generator (after 27.6).

8. **New decisions ship with their gates.** Any new semantic decision class (including those added by language features such as the Graph DSL or new backends) ships with its declared authority, perturbation test and drop test in the same change.
   - Gate: AGENTS.md rule plus review. Status: rule recorded; per-feature enforcement by review.

## Maintenance

- Kept current like the language docs; fed to cold-start reviewers alongside them.
- CI should verify that every gate named here exists in make check (pending).
