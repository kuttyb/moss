CP-C: GO

Frozen source: 24abc2b9e79f95c3a7f7f7e4222214691af32f56. The checkout is detached, and `git status --porcelain --untracked-files=all` is empty after the build and review. `git diff d6b416fbd5e5f64446fbc0eb419397ea7a547430 24abc2b9e79f95c3a7f7f7e4222214691af32f56 -- src` is empty, confirming source code identity with the supplied sanitizer evidence revision. No source files were edited.

Bootstrap: `./moss agent bootstrap --json` returned `language_version: moss-0.1`.

Pinned binaries:
- Legacy `/home/owner/daji/code/moss/tmp/phase27-1-base/moss`: SHA-256 `4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd` (matches required pin).
- Built frozen candidate `./moss`: SHA-256 `9beee692ac2053c95f9fd0b4d0d4ef9e4d38e32d23a9decb8c79749d3e23cd11`.
- Supplied sanitizer artifact `tmp/phase27-2/gates/sanitized-corpus-d6b416fb.json`: SHA-256 `1c8259b361ee822e4ebd4b7ac3d727c523d99151fbcc2e2f45c2fa6ada8b2be8`; reports ASan+UBSan shadow scan, 2,088 sources, zero findings, compiler binary hash `5b530c7896a8565c323e16d9d1ecab989f83552eb861abf4967f481c558d7dc6`. Its binary hash differs from this independently built binary; source code is identical at d6b416f and 24abc2b.

Full pinned shadow differential command:
`python3 tests/tooling/check_phase27_reach_differential.py /home/owner/daji/code/moss/tmp/phase27-1-base/moss ./moss --rev 24abc2b9e79f95c3a7f7f7e4222214691af32f56 --expected-legacy-sha256 4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd --out tmp/cpc/reach-differential.json`

Result: 2,088/2,088 tracked Moss sources, all traces complete; zero unexplained verdict differences. The five recorded verdict differences are the exact known legacy SIGSEGVs on deferred generic pipeline inputs; candidate shadow accepts all five after the crash fixes. There were no candidate-only rejection/crash discrepancies. Checked set differences are eight legacy-only declarations, each among the audited qualified module same-leaf splitter artifacts, zero unexpected omissions; and two tree-only `Ast.size`/`Env.size` receiver candidates. Edge differences total 6,634 legacy-only and 965 tree-only rows. These match the documented seven cause classes (two are combined in one class): same-leaf qualified spellings, legacy keywords/placeholders, legacy enum/pattern labels, shadowed locals/parameters, built-in constructor tree nodes, qualified module misparse, and receiver-name same-name candidates. The non-benign classes have runnable examples in the audit. `moss calls`, `moss impact`, and `.mossi` consumers showed no user-visible edge output change; all 11 natural_full interfaces matched byte-for-byte in same-binary off/shadow, and package-mode parity is preserved.

R1 audit review: the recorded initial audit has 13 candidate-only declaration instances; four have concrete call chains and nine have no valid chain and were classified as migration bugs. The earlier mistaken lexer-test-to-AST chain is explicitly corrected. Current differential has only two tree-only method candidates, both with the documented parser-test `p.ast.size()` receiver chain. The corpus report records both sides' exact checked sets/verdicts for all eight qualified-call legacy artifacts.

Focused commands all passed (logs are adjacent):
- `python3 tests/tooling/check_phase27_reach_named_forms.py <pinned-legacy> ./moss`: constructor and function-label behavior, `node.kind_num`, `inner.make`, package-mode R4, and `left.key()` ambiguous receiver retaining both `Left.key` and `Right.key` candidates.
- `python3 tests/tooling/check_phase27_est.py ./moss`: EST side facts, type/effect/direct-target retention, context-keyed facts, structural shadow mismatch detection, pipeline shapes, fresh occurrence IDs, copied/rebuilt/stale-handle contracts, and off-mode temporary arena behavior.
- `python3 tests/tooling/check_phase27_est_functional_tree.py ./moss`: functional pipeline plus regex apostrophe non-pipeline expression in both off/shadow.
- `python3 tests/tooling/check_phase27_untyped_pipeline_return.py ./moss`: deferred pipeline regressions in off/shadow, including `02_untyped_pipeline_return.moss`.

Review of side-fact and occurrence handling: facts are keyed by expression ID plus specialization/body context; shadow assertions compare type, specialization result, resolved target, and effects when recorded. Tree cursors resolve child occurrences structurally; `fresh_occurrence` deep-duplicates rather than reusing IDs. Functional optimizer produces semantic plans and does not rewrite expression source/tree occurrences. The targeted fact and occurrence contract checks passed.

Artifacts:
- `tmp/cpc/reach-differential.json` — full corpus report with every source entry and checked/edge sets.
- `tmp/cpc/reach-differential.log` — harness output.
- `tmp/cpc/named.log`, `functional-tree.log`, `deferred-pipeline.log`, `est-contract.log` — focused gate results.
