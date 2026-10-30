# Independent CP-C review

Verdict: CP-C: GO. No blocking migration mismatch was found.

## Frozen inputs and environment

- Frozen source commit: `14c2ffbc09638c4810e9acd79ac9f2c3ba284d6f`.
- Candidate implementation binary: `build/compiler/moss-dev`, SHA-256 `2013359db00105fa0d0f8276fec2650cdfeb7c8d05a9640ec849ce7bb414ec3e`.
- Pinned legacy binary: `/home/owner/daji/code/moss/tmp/phase27-1-base/moss`, SHA-256 `4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd` (pin matched).
- Bootstrap language version: `moss-0.1`.
- Candidate source at this commit is identical to `ff7a37db` for `src/` and the differential/pipeline/sanitizer harnesses; the only commit delta is `.codex/CURRENT_STATUS.md`. This relates the prior exact-candidate sanitizer artifact below to the reviewed code.
- All commands ran from isolated worktree `tmp/phase27-2/cp-c-ff7a37db/`; no `src/` paths changed.

## Exact commands and artifacts

- `./moss agent bootstrap --json` (recorded only `result.language_version`).
- `make --jobs="$(nproc)" moss` (repository binary absent in new worktree).
- `python3 tests/tooling/check_phase27_reach_differential.py /home/owner/daji/code/moss/tmp/phase27-1-base/moss build/compiler/moss-dev --rev HEAD --out tmp/cpc-reach-differential.json`.
- `python3 tests/tooling/check_phase27_reach_named_forms.py /home/owner/daji/code/moss/tmp/phase27-1-base/moss build/compiler/moss-dev`.
- `python3 tests/tooling/check_phase27_untyped_pipeline_return.py build/compiler/moss-dev`.
- Differential stdout/stderr: `tmp/cpc-reach.log`; named forms and focused pipeline output: `tmp/cpc-named.log`, `tmp/cpc-pipeline.log`.

## Corpus differential

2,088 tracked `.moss` source entries were checked from matching archived roots: legacy `MOSS_EST=off`, candidate `MOSS_EST=shadow`, `moss check <source> --json`.

- 0 unexplained verdict mismatches. Five baseline-only SIGSEGVs on deferred generic pipelines are the exact previously classified baseline crashes repaired by the candidate; candidate accepts all five. The specific `02_untyped_pipeline_return.moss` now passes on both pinned baseline and candidate (both JSON `ok: true`).
- 8 legacy-only checked declarations: each matches the qualified-module same-leaf splitter artifact set. These are the eight per-instance examples detailed by the differential report and the adjudication record in `docs/phase27/REACH_WALK_DIFFERENTIAL.json`; no unexpected omission.
- 2 candidate-only checked methods: `Ast.size` and `Env.size`, from the receiver-name candidate rule for `p.ast.size()`. This is a deliberate same-name method over-approximation, not a verdict change in the corpus.
- Edge rows: 6,634 legacy-only and 965 candidate-only. The adjudication record classifies all 7,599 rows across seven causes: qualified/unqualified spelling (6,385), legacy keyword/pipeline-placeholder false edges (302), pattern/enum labels (66), shadowed locals (120), built-in constructors (680), qualified same-leaf misparse (42), and receiver method candidate over-approximation (4). Concrete examples for each class are preserved in the record. The named-form focused checks independently confirmed qualified `node.kind_num` and `inner.make` candidates, package diagnostics, and `left.key()` same-name candidates.
- Consumer checks found no user-visible change: `moss calls`/`moss impact` are recorded equal after path normalization; the 11 generated `natural_full` `.mossi` interfaces match byte-for-byte in off vs shadow for one compiler. Margo package-mode diagnostic parity was rerun and passed: both modes report `UNKNOWN_SYMBOL_OR_TYPE` at `token.moss:3`.

## Harness coverage assessment

The strict harness fails on verdict differences except its explicit five exact baseline pipeline crashes, unexpected legacy-only checked declarations, or trace-completeness disagreement. It also compares per-entry edges and checked identities. 1,429 entries have no trace on either path, so reach-edge parity is not measurable for those entries; verdict parity is still checked. The other 659 entries have matched trace-completeness. This is a coverage boundary, not a hidden candidate mismatch: the no-trace rows include stand-alone fixture forms with no scope trace, and both sides agree on trace availability. No verdict or checked-set anomaly outside the adjudicated cases appeared.

## Pipeline and sanitizer evidence

Focused regression script passed in off and shadow. A direct check of `examples/swarm/expression_surface_2026/parser_evaluator/failed_attempts/02_untyped_pipeline_return.moss` returned JSON `ok: true` on both pinned legacy and candidate. The full sanitizer record for code-identical `ff7a37db` is `tmp/phase27-2/gates/sanitized-corpus-ff7a37db.json`: 2,088 sources, 0 ASan/UBSan/process findings, compiler SHA-256 `17eaf1484132fde35458829f558aaa396f4d85f45154924f2cd3238f9ba57db1`. Its full log is `tmp/phase27-2/gates/sanitized-corpus-ff7a37db.log`; focused sanitizer and build logs are likewise in the gates directory. The prior full sanitized run is attributable because the reviewed code and harness are byte-for-byte unchanged from that reviewed source checkpoint.

## Worktree evidence

- Final HEAD: `14c2ffbc09638c4810e9acd79ac9f2c3ba284d6f`.
- Final `git status --short --branch`: `## HEAD (no branch)` (clean).
- `git diff --name-only HEAD -- src`: empty.
