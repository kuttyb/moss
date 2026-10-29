# CP-A adversarial review

Reviewed frozen commit `0f116ada267b3772895e84859132a07218be8aa1` in a detached
worktree. Built with `make --jobs="$(nproc)" moss-dev`; bootstrap reported
`language_version: moss-0.1`. The primary checkout was not edited. No implementation
files were changed.

**VERDICT: GO — 0 confirmed implementation defects; 1 measurement-scope caveat.**

## Findings

- **No consumer-path defect found.** `src/parser.cpp:129` calls `est::populate`;
  repository references outside the EST module only declare AST-owned fields.
  `populate` returns before allocation in off mode and only adds metadata in shadow
  mode. `lookup` is mode-gated. The focused check reports `arena_size=0
  handle_count=0` for off mode and passes shadow dumps and stale-handle assertion.
- **Arena/copy/rebuild invariants pass inspection and focused checks.** Arena nodes
  are only appended; `at` is const; `duplicate` recursively appends child nodes;
  `rebuild_slot` removes matching handles and appends a parsed replacement while
  preserving old nodes. The contract check verifies same-source Program copies
  share arena and IDs, new parsing allocates distinct IDs, replacement leaves the old
  dump unchanged, and the member/index fresh-occurrence root changes. The test only
  asserts the new root ID for that member/index tree; recursive distinct child IDs
  are supported by implementation inspection, not a complete child-ID assertion.
- **Coverage deduplication has compilation-local scope.** `populate` creates one
  Arena and initializes `next_owner=1` for each Program; `Arena::note` deduplicates
  `(owner, slot, argument_index)` within that Arena via `seen_`. It is real within a
  Program, but owners and the set restart for every Program/compilation, so aggregated
  `unique` totals across compiler invocations are sums of per-compilation unique
  slots, not globally deduplicated source occurrences. The observed equality of
  `unique` and `attempts` (403,436) does not independently demonstrate cross-run
  deduplication. This is a metric-scope caveat, not a correctness failure in the
  current once-per-slot population path.
- **Fast Debug timeout comparison reproduced.** The exact baseline compiler built
  from `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576` and the frozen compiler both hit
  the 15-second timeout on both sources. Commands and a reproducible harness are in
  `tests/phase27/adversarial/cp-a/timeout_compare.py`.

## Validation and reproduction

```sh
python3 tests/tooling/check_phase27_est.py build/compiler/moss-dev
python3 tests/phase27/adversarial/cp-a/timeout_compare.py \
  ../cp-a-baseline/build/compiler/moss-dev build/compiler/moss-dev
```

The focused EST command passed. Timeout results:

| Source | baseline `4d6d0c8` | frozen `0f116ada` |
|---|---:|---:|
| `examples/frontend_syntax.moss` | timeout at 15s | timeout at 15s |
| `examples/functional_pipeline.moss` | timeout at 15s | timeout at 15s |

Both exact source commands were `moss run --interp <absolute-source-path>` with the
checkout root as working directory. Thus neither timeout is introduced by the frozen
EST change.
