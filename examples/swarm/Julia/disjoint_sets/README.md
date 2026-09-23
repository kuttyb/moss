# Julia `IntDisjointSets` → Moss

This dogfooding experiment translates the core disjoint-set / union-find algorithm
from JuliaCollections/DataStructures.jl's `IntDisjointSets` into pure Moss.

## Source and Scope

- Repository reference: [DataStructures.jl](https://github.com/JuliaCollections/DataStructures.jl) (`IntDisjointSets`)
- Frozen reference file: `examples/swarm/Julia/disjoint_sets/reference/disjoint_sets.jl`
- Operations implemented:
  - Disjoint set initialization for `n` elements (`0..n-1`)
  - `find_root` with iterative path compression
  - `union` by rank with group count decrement
  - `in_same_set` connected/same-set query
  - `num_groups` query
  - `num_elements` query

## Translation

In Julia, arrays are 1-based (`1:n`); Moss `Vector` is 0-based (`0..n-1`).
The translation models elements as integers `0..n-1`, indexing directly into the
`parents` and `ranks` vectors without offset overhead.

1. **Data Structure**:
   `type IntDisjointSets` stores:
   - `parents: Vector[Int]` initialized to `parents[i] = i`
   - `ranks: Vector[Int]` initialized to `0`
   - `ngroups: Int` initialized to `n`

2. **Iterative Find & Path Compression**:
   Moss v0.1 does not support general recursion. The implementation directly
   follows the iterative reference algorithm:
   - First loop climbs `parents[root]` until finding `root == parents[root]`.
   - Second loop traverses from `curr = x` to `root`, reparenting each intermediate node (`parents[curr] = root`).

3. **Union by Rank**:
   - Computes roots `rx = find_root(x)` and `ry = find_root(y)`.
   - If roots differ, compares `ranks[rx]` and `ranks[ry]`.
   - Lower rank root becomes a child of higher rank root.
   - If ranks are equal, one becomes child and the other's rank increments by 1.
   - Decrements `ngroups` by 1 on merge; returns the merged root.
   - If roots are equal, returns existing root without modifying `ngroups` (idempotent).

## What Worked Naturally

- **Language syntax and typing**: `type IntDisjointSets` fields and methods cleanly express the disjoint-set data structure.
- **Vectors and indexing**: `Vector[Int]()`, `push()`, indexed reads (`parents[i]`), and indexed writes (`parents[curr] = root`) worked without friction.
- **Control flow**: `while` loops, `if/else` statements, integer arithmetic (`+`, `-`), comparisons (`!=`, `==`, `<`, `>`), and boolean operators (`not`) functioned as expected.
- **Testing**: Built-in test syntax (`test "name":`) with `assert` and `assertEqual` worked cleanly for unit tests.
- **CLI & Project Tooling**:
  - `./margo test` successfully built and executed all 7 unit tests (100% pass).
  - `./margo build` and `./margo run` compiled to native code via rustc and ran with correct output (`true false 2`).
  - `./moss fmt` reliably and canonically formatted the source file.
  - Semantic query commands (`inspect`, `effects`, `calls`) accurately revealed durable identities, callers, and inferred parameter effects.
  - `./moss impact` correctly identified all dependent unit tests.

## Friction and Tooling Observations

1. **Fast Debug Interpreter Parity (Resolved in SWARM-025)**:
   - Initial observation: Native compilation (`margo test`, `margo build`, `margo run`) succeeded and ran cleanly, but Fast Debug (`moss run --interp` or `margo debug`) halted with `interpreter error: unsupported or unresolved callable 'find_root'` at unqualified sibling calls inside `in_same_set` and `union`.
   - Resolution: `FastInterpreter::eval` was updated in Phase 15.10 to dispatch unqualified method calls against `self` in the current frame when present (SWARM-025). Fast Debug now runs with 100% parity to native execution (`true false 2`).

2. **Project Manifest Directory Sensitivity**:
   - `moss impact` and `moss test --affected` resolve `Moss.toml` / `moss.toml` by walking upwards from the current working directory.
   - Running `moss impact` from the repository root rather than inside the project directory produces `PROJECT_MANIFEST_ERROR: no Moss.toml was found in this directory or any parent`. Running from within the project directory succeeds.

## Verification

From `examples/swarm/Julia/disjoint_sets/`:

```sh
../../../../margo test
../../../../margo build
../../../../margo run
```

### Test Suite Summary

1. `handles single element disjoint set`: verifies `n=1`, self-root, same-set query, and idempotent union.
2. `initializes disjoint sets of n elements`: verifies initial size, group count, and self-parenting for `n=5`.
3. `in_same_set identifies separate and connected components`: verifies disjoint components before and after sequential unions.
4. `union by rank maintains balanced trees and rank increments`: verifies tree ranks increment on equal-rank joins and `ngroups` decrements.
5. `path compression flattens tree on find_root`: verifies all nodes along a merge chain point directly to root after find.
6. `deep tree path compression directly points leaf to root`: constructs a height-3 tree using rank-balanced merges and asserts that a non-direct child is flattened directly to the root on `find_root`.
7. `repeated union on same elements is idempotent`: verifies redundant union calls preserve `ngroups` and root stability.

All 7 unit tests pass natively.
