# Team C independent verification — `bf5fc461`

**Overall verdict: NO-GO.** This read-only verification used a fresh detached
worktree at `bf5fc4616aa4dadd7ca4ef30a967e80237fef884`. No source, tests, oracle
expectations/mappings, or clone allowlist were modified. Build/test outputs are
under this worktree's ignored `tmp/`.

## Build and exact candidate identity

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-bf5fc461-build moss moss-opt
```

Both targets passed. Fresh binary hashes:

- `moss-dev`: `debd710b33a395c65c5fc8b7224bba1c63e3dc611f8cc4f99895872eb4a23018`
- `moss-opt`: `3f1e3bb21f8193aae56d6ab980574c578fc41b5a7c5d3d00380a72fab69ba8cb`

These hashes exactly match the `build/compiler/moss-dev` and `moss-opt` binaries
in `tmp/phase28-r7-010-vertical-integration`, whose `build/tests/parallel`
artifacts were produced for the coordinator run. That worktree advanced from
`bf5fc461` to `bed6ec8c` for documentation only; `git diff --name-only` between
the SHAs lists only `.codex/CURRENT_STATUS.md` and two Phase 28 reports. The
candidate run note in that worktree records exact commands
`make --jobs="$(nproc)" check` and `make --jobs="$(nproc)" check-shadow`, each
with 167 selected jobs: 97 PASS, 69 FAIL, 1 SKIP. It records three pinned
performance corpus SHA mismatches in both modes, separate from the compiler
regressions. The output base contains 166 per-job stdout/stderr pairs, but no
saved per-mode runner exit-code manifest; the exact counts above are from the
coordinator's exact-SHA run note, cross-checked to byte-identical compiler
binaries and the per-job artifact tree.

## Frozen v2 oracle and parity

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler tmp/team-c-bf5fc461-build/moss-dev \
  --json-out tmp/team-c-bf5fc461-oracle.json \
  --ledger-out tmp/team-c-bf5fc461-oracle.md
```

Result: 60/60 semantic PASS; zero engine disagreements across applicable
checker/Fast Debug/Native Debug/Native Optimized checks; 19/60 pass all
applicable gates; 41/60 fail provenance only. Generated Rust clone
observations are 252/252 unverified and unauthorized. Fresh artifacts:

- JSON: `tmp/team-c-bf5fc461-oracle.json`, SHA-256
  `626c1c160d08dd4df048c60eec9b264b7f97386ed17ade03d8d9ee6f5ac76205`
- Ledger: `tmp/team-c-bf5fc461-oracle.md`, SHA-256
  `d02220363d9511a714e90a1f3c4b9cbc38a5e43f79d3671eb2a871af4d16e6d5`

Frozen inputs remained unchanged:

- `expectations.json`: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- `diagnostic_mapping.json`: `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`
- `runtime_callsite_allowlist.json`: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`

## Focused gates

The frozen take fixture harness passed 18/18, including Fast Debug, Native,
and optimized-native parity with zero hidden clones. It was invoked by importing
`tests/phase28/verify_take_fixtures.py`, overriding only its compiler and
artifact directory to the isolated build paths.

The CollectionView consumer gate passed in Default and EST-shadow:

```sh
env -u MOSS_EST python3 tests/phase28/r7_010/team_b/check_collection_view_consumers.py \
  tmp/team-c-bf5fc461-build/moss-dev tmp/team-c-bf5fc461-collection-default
MOSS_EST=shadow python3 tests/phase28/r7_010/team_b/check_collection_view_consumers.py \
  tmp/team-c-bf5fc461-build/moss-dev tmp/team-c-bf5fc461-collection-shadow
```

Both runs report Native Debug/Optimized/Fast Debug parity, no `.cloned()` at
checked view sites, 8/8 corruption hooks across both fixtures, and no Rust
artifact after failed native generation.

## Suite comparison and baseline reconciliation

The user-provided historical comparison baseline is **166/167 PASS in both
Default and Shadow**; that figure is preserved here. Against it, the exact
candidate result is 69 fewer PASS jobs, 68 more FAIL jobs, and one SKIP in each
mode. Candidate status remains NO-GO.

The preserved exact-SHA baseline evidence at
`150ab36b41e45504aaa400aa717da241be7542fb` has a separate Shadow count:

- Baseline worktree metadata and its full logs report Default 166 PASS / 1 FAIL /
  0 SKIP and Shadow 165 PASS / 2 FAIL / 0 SKIP.
- The baseline logs are
  `tmp/phase28-r7-010-phase28-work-baseline/tmp/r7-010-baseline-default.log`
  (SHA-256 `3186f7e5344a409ce2e8458803f7d29b5867bc91c1e33ec24c5b78ce3fdd4bbd`)
  and `.../r7-010-baseline-shadow.log` (SHA-256
  `a5b2666f49b2a278a14f65e9c2b35fa94b6f84043e8b71fc2d1ac09a3f64f3f8`).
  They report `check_phase21_borrowed_view_write` in Default and that same job
  plus `shard_swarm_cases` in Shadow. Shadow's second failure is the known
  `MOSS_EST shadow structural divergence for 'put(take())'`.
- A separate fresh Team C rebuild at that same baseline SHA used
  `env -u MOSS_EST python3 tests/run_parallel.py --jobs 16 ...` and
  `MOSS_EST=shadow python3 tests/run_parallel.py --jobs 16 ...`, with isolated
  dev/opt compilers and output paths. Its logs are
  `tmp/phase28-r7-010-team-c-verify-baseline-150ab36b/tmp/team-c-baseline-default.log`
  (SHA-256 `1e0ba87ae4dcb5a57224e233f32862dccb780e1374dd5f2f04403edafa082236`)
  and `.../team-c-baseline-shadow.log` (SHA-256
  `a46116439a4aa1512e468b534756c5c0e95df3e3ad8d23e6862d8f9b702c1dc9`). It
  independently reproduced the same 166/167 Default and 165/167 Shadow
  results and failure IDs.

Thus the 166/167 Shadow figure remains the user-specified historical baseline;
the exact available `150ab36b` records and fresh reproduction show 165/167 due
to the known shadow-only `shard_swarm_cases` divergence. I preserve both facts
and do not rewrite the historical baseline. Against the reproduced baseline,
candidate Shadow is 68 fewer PASS, 67 more FAIL, and one SKIP; Default remains
166 baseline PASS versus candidate 97, 69 failures, and one skip.

## Clone inventory boundaries; no authorization

A read-only regex inventory of `src/**/*.{cpp,h,hpp}` on `bf5fc461` found 43
literal `.clone()` source occurrences and 8 qualified `Arc::clone` occurrences.
These are source-text matches, not distinct generated clone rows or Moss-level
semantic copies. The direct matches include native generator templates,
clone-audit/checker code, and runtime support: three in `executor_runtime.cpp`,
five in `fileio_runtime.cpp`, and one in `char_string_ordering_runtime.cpp`.
Seven Arc sites carry runtime markers in `executor_runtime.cpp`; one
`Arc::clone` in `fileio_runtime.cpp` is unmarked. The frozen runtime allowlist
has zero entries. No site is authorized or exempted. Oracle's 252 generated
observations remain unverified; no nonprimitive Moss copy is implicitly
approved by this inventory.

## Verdict

**NO-GO.** Semantic oracle and engine parity pass, as do focused take and
CollectionView checks. The full Default/Shadow matrices remain at 97/167 with
69 failures and one skip each, and 41/60 oracle fixtures fail clone provenance.
No runtime clone exception is approved.
