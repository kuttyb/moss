# Team C verification: Fast Debug indexed READ follow-up

## Exact revisions and scope

- Candidate: `1d64e51fc3ef3243aea29f4125ededa8cb6905e2`
- Base: `a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0`
- Fresh detached worktree: `tmp/team-c-verify-1d64e51f` (clean at setup; no tracked files edited).
- Focused follow-up review only; no full suite or ownership oracle run.

## Build artifacts

Built both candidate compiler modes with isolated paths:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-1d64e51f-build moss moss-opt
```

Build succeeded. SHA-256:

- `moss-dev`: `51089bf71f7d282eaa43fe2449921ae2c717f46a475dd74e67570e27f23d3f90`
- `moss-opt`: `8fe546e267773a111baca284986222f71ec44ace82c50826f1f796330cabe9e6`

## Imported generic regression and corruption checks

Ran the registered imported-generic expression gate on the exact dev compiler:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-1d64e51f-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-1d64e51f-build/moss-dev"
```

Both passed with `Phase 24 imported generic expression specializations passed (source-backed and source-free)`. The gate confirms source-backed native, release, and Fast Debug output parity (`-1\n7\n3\n1\n1\n`), and source-free imported native check/build/run. Fast Debug's `MOSS_TEST_REQUIRE_INDEXED_EXPRESSION_READ_BORROW=1` positive-use assertion passes for imported `Vector[String]` conditions.

Fast Debug consumer corruption hooks for drop authority, drop/duplicate/perturb requirement, and drop/duplicate/perturb fact all fail closed. The source-free native compiler path also runs its 12 checker/consumer fact and requirement drop/duplicate/perturb hooks. No hook failure falls back to a value copy: the consumer raises `MOSS_INTERNAL_INVARIANT` when exact authority is unavailable or contradictory.

For the optimized compiler, reran source-backed/source-free functional flows in both modes with test hooks intentionally skipped:

```sh
MOSS_SKIP_TEST_HOOKS=1 env -u MOSS_EST python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-1d64e51f-build/moss-opt"
MOSS_SKIP_TEST_HOOKS=1 MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-1d64e51f-build/moss-opt"
```

Both passed.

## Primitive versus borrowed nonprimitive disposition

The checker now sets `PrimitiveValue` only when the indexed result TypeRef is Copy, with `permits_copy=true` and no temporary borrow. Nonprimitive indexed results receive `BorrowedNonprimitive`, with `permits_copy=false` and `temporary_nonescaping_borrow=true`. The fact's authority predicate checks that disposition against the exact result TypeRef and receiver element type. Fast Debug resolves the exact condition occurrence and specialization before using that disposition.

The registered String-condition case confirms the nonprimitive path and positive borrow counter. I also ran a separate primitive probe from `tmp/team-c-1d64e51f-build/classification/primitive_index_condition.moss`:

```moss
fn main():
  values = [1, 2]
  if values[0] == 1:
    echo "primitive"
```

Checker and normal Fast Debug execution pass (`primitive`). With `MOSS_TEST_REQUIRE_INDEXED_EXPRESSION_READ_BORROW=1`, the program prints its result and then fails with `MOSS_INTERNAL_INVARIANT no checker-authorized indexed READ borrow was observed`, confirming that the exact primitive fact uses the scalar-value path and does not take the nonprimitive borrowed-element path.

For a nonprimitive condition, Fast Debug stores the vector's existing `shared_ptr` and element index in `Value::BorrowedElement`; equality dereferences the indexed `Value` by const reference. It does not create another `std::string` value. This is the no-deep-String-copy behavior missing at base `a4cb6fb4`.

## Box projected-field limitation

The repository status note for this exact follow-up records the Box projected-field probe as removed after Fast Debug hit `MOSS_INTERNAL_INVARIANT conditional Map read authority unavailable`; it explicitly leaves aggregate projected-field coverage open. The source still has that fail-closed diagnostic in `resolve_conditional_map_read_fact`. The registered follow-up fixture covers direct `Vector[String]` indexed conditions, not Box projected-field conditions. I therefore treat Box/projected-field Fast Debug support as an open limitation, not a verified pass.

`git diff --check a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0 1d64e51fc3ef3243aea29f4125ededa8cb6905e2` passed.

## Team C scoped verdict

**PASS for exact checker disposition, borrowed Fast Debug `Vector[String]` condition execution, primitive scalar classification, fail-closed hooks, and native/Fast Debug functional parity. Box projected-field conditional Map reads remain an explicitly documented limitation.** This result is not a whole-phase GO.
