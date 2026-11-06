# Independent Review: R7-010 Validation Regression Fixes

## Scope and candidate

- Exact candidate: `f0d22c1315f70450d23953224932795b433b75cd`
- Parent: `56ffc1cb0e90d6a0e6dcc83165e0c09ce9ad347d`
- Worktree: `tmp/phase28-r7-010-validation-regressions`
- Scope: WP1 runner routing to `moss-dev` for test hooks; Phase 27 `.mossi`
  corruption coverage for the exact local specialization-scoped ReturnValue
  EST fact; ABI-v9 source-backed behavior and fail-closed controls.
- No implementation or test source was modified during this review. Report is
  stored separately from the frozen candidate.

## Review findings

The `run_parallel.py` manifest selects `use_opt_compiler=False` for
`check_phase25_r2_wp1_call_chain` and leaves the job isolated. This is the
needed route because the WP1 checker/native/Fast Debug perturbation hooks are
test-build hooks. Independently inspecting `build_manifest()` confirmed the
selected compiler and isolation settings. The focused gate passed with
`build/compiler/moss-dev`, including hostile source-free call-chain mutations,
Default/Shadow imported ReturnValue drops/duplicates/type/context/
specialization perturbations, native and Fast Debug consumer corruption, and
the crossed-Int corruption control that leaves the Bool sibling working.
Its ABI-v9 compatibility section builds source-backed provider/app artifacts
and runs the imported call chain in both Debug and Release. Thus ABI-v9
source-backed behavior remains covered separately from ABI-v10 source-free
authority.

The Phase 27 test no longer drops a legacy source-context fact which the
implementation is not required to use. Instead it selects the exact local
`est_est_library__generic` specialized ReturnValue EST fact and tests both
drop and type-perturb hooks. Both must fail with the exact local specialized
ReturnValue missing/contradictory diagnostic. Normal provider and app `.mossi`
artifacts are still produced in Default and Shadow; their raw bytes match
within each package across modes. The gate also checks the imported generic
Section 1 body and Section 2 execution body. This updates the exercised
authority fact without weakening the fail-closed contract.

## Independent evidence

- `make --jobs="$(nproc)" moss-dev` — passed; resulting compiler SHA-256:
  `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`.
- `python3 tests/tooling/check_phase25_r2_wp1_call_chain.py build/compiler/moss-dev`
  — passed. Includes both EST modes, v9 source-backed Debug/Release controls,
  hostile ABI artifact mutations, checker/native/Fast Debug ReturnValue
  corruption matrices, and specialization isolation.
- `python3 tests/tooling/check_phase27_est_mossi.py build/compiler/moss-dev`
  — passed. Raw Default/Shadow `.mossi` hashes matched: library
  `d7c1216e2fb6cd106bb083d433bc656eeee3bfe0b7dbeafbe50840329f59bc73`, app
  `eeb2b144669e2e0b035695ea9bcfe726c251c837129e46a5958fc46913b4fd90`;
  generic imported dump count 1, Section 2 imported dump count 2.
- Independent `run_parallel.build_manifest()` assertion — WP1 selects dev
  compiler and isolated worktree.
- `python3 tests/tooling/check_agent_feedback.py` — passed.
- `git diff --check f0d22c^ f0d22c` — passed; candidate worktree remained
  clean.

## Verdict

**Scoped GO** for these two validation harness/runner fixes at exact SHA
`f0d22c1315f70450d23953224932795b433b75cd`. The changes route the tests to
the hooks they need and exercise the current exact ReturnValue authority
without deleting ABI-v9 source-backed controls or accepting missing or
contradictory facts. This is not a whole-Phase-28 GO and does not approve
integration beyond the authorized staging checkpoint; full regression suites
and oracle/clone-provenance gates remain separate.
