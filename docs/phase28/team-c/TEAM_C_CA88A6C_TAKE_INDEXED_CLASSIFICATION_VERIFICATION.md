# Team C verification: take/Fast Debug indexed-read classification

## Exact revisions and scope

- Candidate: `ca88a6c3242aa0344e69d81e7a644b93cb4742b7`
- Base: `58e80e8d89e74dcf4923a2cb9eb1325b55f88b80`
- Fresh detached worktree: `tmp/team-c-verify-ca88a6c` (clean at setup; no tracked files edited).
- Focused checkpoint only; no full suite or oracle run.

## Build artifacts

Built both compiler profiles in isolated paths:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-ca88a6c-build moss moss-opt
```

Build succeeded. SHA-256:

- `moss` / `moss-dev`: `143f1e8807ebd24f39be867f640d174b208decbe0c21c8b8960de124b44504e6`
- `moss-opt`: `fbb0eaa2135f0e3483df4abc10e860cd2697c39454a1569945ed18ca6b729fc3`

## Take and authority gates

Ran the take fixture matrix in both modes:

```sh
env -u MOSS_EST python3 tests/phase28/verify_take_fixtures.py
MOSS_EST=shadow python3 tests/phase28/verify_take_fixtures.py
```

Both reported 18/18 PASS. The two repaired regressions, `take_specialized_effects` and `take_multi_selector`, pass checker, Fast Debug, native, and optimized-native parity. Accepted take fixtures all report zero generated clones. The verification harness also injects indexed-read checker and interpreter authority corruption cases for those projected-selector fixtures and requires fail-closed `MOSS_INTERNAL_INVARIANT` behavior.

The indexed-copy gate passed:

```sh
python3 tests/tooling/check_phase28_copy_index_authority.py "$PWD/tmp/team-c-ca88a6c-build/moss-dev"
```

Result: `Phase 28 indexed-copy authority: 8 scenarios PASS (default/shadow)`.

The imported-generic gate passed in Default and EST-shadow:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-ca88a6c-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-ca88a6c-build/moss-dev"
```

Both report source-backed/source-free imported generic expression specializations passed.

## Checker authority and runtime consumer audit

The checker now walks the indexed receiver's `Parenthesized`, `Member`, and nested `Index` base nodes to identify its root `Identifier` binding. It derives the projected receiver TypeRef from the checker environment and exact receiver source span, then records the exact index `ExprId`, receiver and selector ExprIds, statement occurrence, root binding/definition identities, specialization identity/context, and result TypeRef. Disposition is produced from the checked element TypeRef (`PrimitiveValue` for Copy types; `BorrowedNonprimitive` otherwise), with the corresponding copy/borrow flags. It does not use runtime `Value` kinds to choose the semantic disposition.

Fast Debug resolves the exact indexed occurrence against its checker requirement/fact and exact frame specialization before executing it. Missing, duplicate, or contradictory indexed authority fails closed; there is no vector-value copy fallback. Nonprimitive results use the checked borrowed-element representation, while primitive results use the permitted scalar-value path. The change does not add runtime type inference as a substitute for a checker fact. Native and Fast Debug parity plus the take harness's zero-clone assertions passed.

`git diff --check 58e80e8d89e74dcf4923a2cb9eb1325b55f88b80 ca88a6c3242aa0344e69d81e7a644b93cb4742b7` passed.

## Team C scoped verdict

**PASS for the two take regressions and exact projected indexed READ classification at frozen SHA `ca88a6c`.** Focused Default/Shadow take, imported-generic, and indexed-copy gates pass; no implicit copy/clone fallback was found. This does not establish full Phase 28 acceptance or GO.
