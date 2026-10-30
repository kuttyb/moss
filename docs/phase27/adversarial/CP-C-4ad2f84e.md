CP-C: GO

Frozen source: `4ad2f84e2ef52538ddd86b672f1abe7433648ec2` on detached `HEAD` in the isolated review worktree. The review worktree is clean; `git diff -- src` is empty. Bootstrap returned `language_version: moss-0.1`.

Pinned binaries:

- Legacy: `/home/owner/daji/code/moss/tmp/phase27-1-base/moss`, SHA-256 `4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd`.
- Candidate normal build: `moss`, SHA-256 `3d739d2f387b25ff68f8a0d3caf838b856aae9ab638910b06caadc1de89bda4`.
- Candidate ASan/UBSan build: `tmp/cpc/asan/moss-dev`, SHA-256 `4ee7d668c1c88c3137e928bc600c052b775eec04fa2377088bf9d054cca9f2ad`.

## Full corpus differential

The pinned two-path harness checked 2,088 tracked `.moss` sources, using legacy `MOSS_EST=off` and candidate `MOSS_EST=shadow` against the same archived source tree. It found zero unexpected verdict differences and zero unexpected checked-declaration omissions. The five verdict differences are the exact documented pinned-baseline SIGSEGVs in deferred generic pipeline fixtures; the candidate checks all five successfully. Eight legacy-only checked identities are the known qualified-module same-leaf artifacts, confirmed against each entry's two path checked sets and verdict. The only two tree-only checked identities are the `Ast.size` and `Env.size` method candidates from `parser_test.moss:15` via `p.ast.size()`.

The 13-instance prior declaration audit reconciles to four real call-chain instances and nine prior migration bugs without a valid chain. The corrected historical attribution has no lexer-test-to-AST chain. Eleven of the original tree-only instances no longer appear; the two current candidates above are the remaining receiver-name over-approximation. The current result exactly matches the full edge, checked-set, verdict, and trace rows from the fixed checker/emitter checkpoint `24abc2b9e79f95c3a7f7f7e4222214691af32f56` (zero changed rows), which establishes no check-reach output change from the Fast Debug slice.

Edge totals are 6,634 legacy-only and 965 tree-only rows across 389 entries. The exact edge sets match the adjudicated cause counts in `docs/phase27/REACH_WALK_DIFFERENTIAL.json`: same-leaf spelling (6,102/283), legacy keywords and placeholders (302/0), enum/pattern labels (66/0), shadowed locals or parameters (120/0), built-in constructor nodes (0/680), qualified-module legacy misparse (42/0), and receiver-name candidates (2/2). These sum to the observed totals. The distinct non-benign qualified-call and receiver causes have focused two-path examples below.

Full report and raw harness log: `tmp/cpc/reach-differential.json`, `tmp/cpc/reach-differential.log`.

## Focused behavior and pipeline checks

- `check_phase27_reach_named_forms.py` passed. It verifies function labels remain rejected, qualified `node.kind_num` and `inner.make` keep same-leaf candidates and reject with the same file-root diagnostics, package-mode R4 parity for both reproducers, and ambiguous `left.key()` retains both method candidates.
- Package-mode R4 diagnostic matched between legacy and candidate: `UNKNOWN_SYMBOL_OR_TYPE` at `token.moss:3` for `node.kind_num`; the second same-leaf package probe matches at `outer.moss:3`.
- `check_phase27_est.py` passed. The focused EST contract checks covered shadow shape comparisons, context-scoped type/effect/direct-target facts, injected disagreement rejection, distinct occurrence IDs, copied/rebuilt occurrences, stale-handle rejection, and off-mode arena construction. EST facts are keyed by occurrence ID and specialization/body context; a fresh occurrence uses a fresh ID and does not pick up the prior occurrence's fact.
- `check_phase27_est_functional_tree.py` passed, including Fast Debug output `14` in both EST off and shadow, plus the regex apostrophe non-pipeline regression in both modes.
- `check_phase27_untyped_pipeline_return.py` passed the deferred-pipeline regressions in off and shadow.
- `check_phase27_three_engine.py` passed with identical stdout and callback count 3. `check_phase27_accepts_rustc.py` passed 236 accepted standalone roots, native Margo test, and six active seeds.
- On a valid qualified module fixture, `moss calls main` and `moss impact main` results were byte-identical between off and shadow. Legacy versus candidate `moss calls` both resolve the same direct/transitive target, `fn:math__leaf`.

Focused logs: `tmp/cpc/est-contract.log`, `tmp/cpc/named.log`, `tmp/cpc/functional-tree.log`, `tmp/cpc/deferred-pipeline.log`, `tmp/cpc/three-engine.log`, `tmp/cpc/accepts-rustc.log`; query JSON is under `tmp/cpc/calls-*.json` and `tmp/cpc/impact-*.json`.

## Sanitizer scan

An independent ASan+UBSan compiler build scanned all 2,088 tracked sources in shadow mode. It reported zero sanitizer, signal, timeout, or no-JSON findings. Report/log/build evidence: `tmp/cpc/sanitized-corpus.json`, `tmp/cpc/sanitized-corpus.log`, `tmp/cpc/sanitizer-build.log`, `tmp/cpc/sanitizer-binary.sha256`.

No semantic change, package-mode verdict change, user-visible query output change, unexplained edge discrepancy, sanitizer finding, or source edit was found. The frozen candidate is GO for CP-C.
