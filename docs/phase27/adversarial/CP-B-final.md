# CP-B frozen adversarial review

Reviewed commit: `3d4d3b4fcdcb9e4113f8217850ceccc9b9d0dc07` (`phase27-work`), in a detached worktree. No files under `src/` were edited.

**VERDICT: GO — 0 confirmed defects.**

## Evidence

- `./moss agent bootstrap --json` reported `language_version: moss-0.1`.
- `make --jobs="$(nproc)"` built the frozen compiler and the `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576` baseline compiler.
- `python3 tests/tooling/check_phase27_est.py build/compiler/moss-dev` passed. Its focused contract probes cover source fingerprint staleness, old-node readability after rebuild, distinct occurrence IDs, ordinary copies, and off-mode `arena_size=0 handle_count=0`. The test also injects a comparison mismatch and confirms shadow mode rejects it.
- `python3 tests/tooling/check_phase27_est_mossi.py build/compiler/moss-dev` passed. It compared raw same-binary off/shadow `.mossi` bytes for both a provider and app, then parsed imported ABI-v9 generic and Section 2 bodies with shadow EST; it observed one generic-body and two execution-body EST dumps.
- `python3 tests/tooling/check_phase27_mossi_baseline.py <frozen-moss> <baseline-moss>` passed using the same project root and compiler path for both builds. For both library and app, raw bytes differed as expected; after recomputing `package_fingerprint` with the baseline toolchain identity and recomputing the dependent `execution_body_hash`, every remaining byte matched. The identities differed only in `moss_sha256`.
- `MOSS_EST=shadow MOSS_EST_COMPARE_STATS=1` on `tests/phase27/est/syntax_27_1b.moss` reported 13 attempts, 13 compared, and 0 unparsed (twice, due to the two compiler checks). Existing focused comparison fixtures also passed.
- An independent `-O` compile with `MOSS_EST_CONTRACT_TEST=1` emitted Rust. A two-package `margo build --check` under shadow mode retained one nonzero-origin EST occurrence for the module-renamed helper.
- Source search found no production consumer of EST node values outside `src/est/expression_tree.*`; the other hits are parser population, arena/handle ownership and contract instrumentation, plus unrelated names containing `lookup`/`expression`. This supports the claim that the tree remains shadow-only. The implementation appends on `Arena::append`, duplicates into new nodes, and rebinds handles during rebuild; the focused contract passed.

## Limits

This is a focused adversarial recheck, not a rerun of the full repository corpus or performance gates. Structural equality is checked only when the legacy `string_shape` routine can produce an expected shape; the focused syntax fixture had zero skipped comparisons, but this review did not establish zero skips across every corpus occurrence. `.mossi` parity was checked on the two-package generic/concrete fixture, including both Section 1 and Section 2 imported-body paths.

## Reproduction

Run from the frozen worktree root:

```sh
python3 tests/phase27/adversarial/cp-b-final/recheck.py \
  ./build/compiler/moss-dev ../cp-b-final-baseline/build/compiler/moss-dev
```

The script writes its combined command output to `tmp/cp-b-final/recheck.log`. It does not modify source files. The standalone baseline recomputation details are also retained under `tmp/phase27-mossi-baseline/` for this worktree.
