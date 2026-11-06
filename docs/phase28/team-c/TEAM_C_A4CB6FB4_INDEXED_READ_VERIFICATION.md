# Team C verification: imported indexed-expression READ authority

## Exact source checkpoint

- Candidate: `a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0`
- Parent: `7a17015187ecd2652229157f13ca39cdfbe6bbe8`
- Fresh detached worktree: `tmp/team-c-verify-a4cb6fb4` (clean at setup; no tracked files edited).
- Focused checkpoint only; not full Phase 28 acceptance.

## Build and compiler artifacts

Built using isolated artifacts/cache:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-a4cb6fb4-build moss moss-opt
```

Build succeeded. SHA-256:

- `moss-dev`: `cbaee2eaf89fe9cf33cdf9615d068bc70c20c7347d92fff1d5c3729f89fe7c50`
- `moss-opt`: `def6ad18d18c217a3e006087fb3521f52e1ca7b49b26192080e3794520c0d4fd`

## Imported generic source-backed/source-free gate

Ran the registered Phase 24 gate with the exact candidate dev compiler in both modes:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-a4cb6fb4-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase24_imported_generic_expressions.py "$PWD/tmp/team-c-a4cb6fb4-build/moss-dev"
```

Both report `Phase 24 imported generic expression specializations passed (source-backed and source-free)`. The gate's imported generic provider has `if items[i] == target` in `contains` and `index_of`. It runs source-backed Margo build, debug, normal run, and `--release` run; then builds a consumer against the compiled provider `.mossi`/rlib via `MOSS_MODULE_PATH`, without provider source fallback, and runs the imported consumer. Expected output in the full fixture is `-1\n7\n3\n1\n1\n`.

All 12 checker/consumer corruption hooks are exercised against source-free imported indexed READ lowering in both modes: fact and requirement drop, duplicate, and perturb for the checker; and the same six consumer cases. Each must fail closed with `MOSS_INTERNAL`. No `MOSS_SKIP_TEST_HOOKS` setting was active.

The gate is routed through the dev compiler in `tests/run_parallel.py`: `check_phase24_imported_generic_expressions` is in the `lightweight` set, selecting `compiler_dev`. Test mutation code is enclosed in `#ifdef MOSS_TEST_BUILD`; build flags define it only for dev (`CXXFLAGS_DEV`), not optimized (`CXXFLAGS_OPT`). Binary string inspection corroborated this: 12 indexed-expression hook strings in `moss-dev`, zero in `moss-opt`.

## Checker authority and native no-copy evidence

The candidate checker publishes `TypedIndexedExpressionReadFactV1` only after closed receiver/selector/result types are available in the specialized pass. Its authority records `required_access=Read`, `temporary_nonescaping_borrow=true`, `permits_copy=false`, plus exact EST occurrence, specialization context/identity, and provenance. The native consumer resolves the exact condition-handle occurrence and verifies receiver/index children, TypeRefs, context, specialization, Read access, and no-copy status before lowering the source expression.

Independently rebuilt a retained provider/consumer probe under `tmp/team-c-a4cb6fb4-build/inspection` and inspected the generated consumer specialization. The specialization is:

```rust
fn __moss_specialize_probe__contains_0(items: &std::vec::Vec<MossStr>, target: &MossStr) -> bool {
    // Moss line 5: if items[i] == target:
    if (((items)[(i) as usize]).as_str()) == ((target).as_str()) {
```

Within this specialized function, there is one direct `items[index]` access and no `.clone()` or `.to_owned()`. This is direct native borrowing/read lowering with no emitted Moss copy. The registered source-backed `margo run --release` and the source-free build/run both passed.

## Fast Debug no-copy/direct-reference limitation

Fast Debug normal and source-free outputs pass, but I cannot verify a no-copy/direct-reference claim for its ordinary `if` indexed-read path. This candidate does not modify interpreter code. In `src/interpreter.hpp`, ordinary EST `Index` evaluation returns `values->at(index)` by value; `Value` contains a `std::string` member. A distinct borrowed-element path is conditional on `active_echo_argument_fact_` and is used for borrowed echo arguments, not the `if items[i] == target` condition. Therefore the test's Fast Debug output parity is not evidence that Fast Debug uses the new checker-issued indexed READ fact or avoids host value copying for this condition. Native generated Rust satisfies the no-copy claim; Fast Debug direct-reference behavior remains unproven and appears to retain value-returning evaluation.

`git diff --check 7a17015187ecd2652229157f13ca39cdfbe6bbe8 a4cb6fb4a1fda8ea27c71c5908a06ef1ae9360f0` passed.

## Team C scoped verdict

**PASS for checker-issued authority, native consumer, dev-only hook routing, source-backed/source-free functional behavior, optimized run, and native no-copy lowering. Fast Debug no-copy/direct-reference is not established by this candidate's implementation or gate.** This scoped result is not a whole-phase GO.
