# Team C exact-SHA verification — imported generic ReturnValue `d8727687`

**Scoped verdict: PASS** for the three-hop imported generic ReturnValue authority gate at exact implementation SHA `d8727687f6fcf9e38607dfd88978db26720db544`. This does not certify full Phase 28, full suites, oracle, or clone provenance.

## Evidence

- Worktree: `tmp/phase28-r7-010-imported-generic-return-chain`, HEAD `d8727687f6fcf9e38607dfd88978db26720db544`.
- Exact compiler: `build/compiler/moss-dev`, SHA-256 `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`.
- Command, run once under Default and once with `MOSS_EST=shadow`:

```sh
python3 tests/tooling/check_phase25_r2_wp1_call_chain.py build/compiler/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase25_r2_wp1_call_chain.py build/compiler/moss-dev
```

Both runs completed with the script's final `R2 WP1 three-hop generic result chain, source-backed Debug/Release interfaces, ABI-v9 controls, poisoned legacy bodies, and hostile closure: PASS` line and exit success.

## What the gate exercised

- ABI-v10 provider artifacts are built in Debug and Release, then the legacy generic body is poisoned. Source-free consumers project checked bodies for `fresh -> middle -> relay`; checked-body tracing confirms all three. Each consumer specializes the same imported `relay` call chain as `Vector[Int]` and `Vector[Bool]`; Debug and Release executions return `[17]` and `[true]` respectively.
- ABI-v9 source-backed compatibility controls build and execute the same chain in Debug and Release with matching outputs.
- Twelve hostile ABI cases fail closed: forged intermediate identity, wrong result type, missing intermediate/deep summary, missing intermediate/deep body, missing dependency, dependency-hash corruption, duplicate summary, duplicate body, duplicate dependency, and missing dependency hash. The canonical writer rejects duplicate body, duplicate dependency, and missing dependency hash; importer consumers reject the other nine.
- In both Default and EST-shadow, all five checker authority hooks reject: dropped fact, perturbed ReturnValue type, perturbed context, perturbed specialization, and duplicate fact.
- In both modes, all six native consumer hooks reject: dropped/duplicated ReturnValue fact and perturbed source-free body hash, statement, fingerprint, and EST context.
- In both modes, the same six corruption cases fail Fast Debug closed with `MOSS_INTERNAL_INVARIANT`.
- Cross-specialization isolation passes: perturbing only the `Vector[Int]` specialization still lets a `Vector[Bool]`-only source-free consumer build and run as `[true]`.

## Scope and worktree state

No source, tests, registries, or allowlists were changed by this verification. At closeout, `git diff` is empty; the worktree has one untracked Agent 2 review report (`docs/phase28/agent2-reviews/imported-generic-return-chain-d8727687.md`) belonging to the concurrent review process. The exact-SHA focused result is independent of that report and is not a whole-phase GO.
