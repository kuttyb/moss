# CP-E Numeric Members Independent Final Review

Candidate source freeze: `f28f5f1713ee5da2e975fdd564c494841e6cbffc`

Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

Review branch: `review/cp-e-numeric-members-final`

Audit worktree: `tmp/cp-e-final-audit`

Compiler contract: `moss-0.1`

## Verdict

GO-with-defects

## Scope and evidence

This review used an isolated worktree at the frozen candidate and a separate baseline worktree. It did not inspect earlier CP-E review reports or raw artifacts. No files under `src/` were changed. The full source-level corpus result, including the baseline/off, candidate/off, and candidate/shadow diagnostic tuples, checked-declaration sets, and call edges, is in [corpus_differential.json](../../../tests/phase27/adversarial/cp-e-numeric-members-final/corpus_differential.json). Reproduction and classification are summarized in `review_summary.json`; hashes for the complete evidence and harnesses are in `CHECKSUMS.sha256`.

The compiler was built in both worktrees with `make --jobs="$(nproc)"`. Bootstrap reported `language_version: moss-0.1`.

## Reader lint and exemptions

`python3 tests/tooling/check_phase27_string_debt.py` passed. The checker, emitter, reach-walk, and Fast Debug `--require-zero` invocations also exited successfully. Raw Stmt NAME and binding counts were 40 and 9; widened shape checks passed. All three keyed exemptions resolve to unique functions and exactly one reader site apiece. Synthetic stale-function, stale-API, and duplicate-site probes were rejected by the linter.

The emitter zero-reader result is a false green. `CONSUMER_PATHS["emitter"]` omits `src/executor_invoke_codegen.inc`, where `gen_executor_statement` still calls `parse_executor_chain(s.b, calls)` at line 138. The existing `--require-zero emitter` command reports zero. The independent [lint_inventory_probe.py](../../../tests/phase27/adversarial/cp-e-numeric-members-final/lint_inventory_probe.py) attributes the omitted include to emitter and detects exactly one active reader. This is a **MIGRATION BUG — gate coverage defect**: the stated reader invariant is not enforced for an emitter include. The minimal two-path reproduction is:

1. Run `python3 tests/tooling/check_phase27_string_debt.py --require-zero emitter`: it exits 0.
2. Run `python3 tests/phase27/adversarial/cp-e-numeric-members-final/lint_inventory_probe.py`: it reports `current emitter sites: 0` and `omitted include sites: 1` after adding that include to the linter's emitter inventory in memory.

## Corpus and numeric-member checks

The independent harness checked all 2,212 tracked `.moss` files. Candidate off and shadow had no verdict/diagnostic differences. Baseline off matched candidate off and shadow on 2,210 sources. The remaining two sources are copies of the package-backed advanced-data source-free consumer; when checked standalone, candidate reports the unresolved imported `ndarray.NdArrayError` at line 57 while baseline reports the earlier malformed-pattern diagnostic. Both compilers pass the actual package-aware oracle, `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug`, with their provider interfaces supplied. This is a **BENIGN context difference**, not a source behavior regression.

All nine `tests/phase27/est/repro/numeric_member_*.moss` cases match across baseline off, candidate off, and candidate shadow. Every one retains `UNKNOWN_FIELD` with the baseline message and the same complete line-6 span. The complete per-case signatures are preserved in the corpus evidence.

Candidate off checked-declaration closure exactly matches baseline off. Shadow omits one checked declaration, `method:heap__Entry.key_of@9`, in four roots under the heapkit module fixtures; it adds no checked declarations. The same-leaf source has a module-local `order.key_of(item: Keyed)` call, while `heap.key_of(e)` is a distinct exported wrapper whose only call to `Entry.key_of` is in its own body and is not reached by the checked root call graph. Candidate off matches baseline, and the baseline-only target appears in no candidate shadow edge. I classify this as **LEGACY MISPARSE**: the legacy text lookup over-selects an unreachable qualified same-leaf method. The four roots and complete checked sets are recorded in the corpus evidence.
The two-path closure replay is `MOSS_EST=off MOSS_PHASE26_C1_SCOPE_TRACE=1 ./moss check examples/swarm/modules/algo_chain/heapkit/src/order.moss --json` versus the same command with `MOSS_EST=shadow`; the baseline/off edge/checked set is also captured for that exact source in the corpus file.

There were 1,546 shadow edge-name deltas before normalization; stripping the source/module prefix from each target produced identical owner/kind/leaf-target edge sets. Scope tracing completed for 662 roots; early-error roots without complete scope traces remain represented with their exact diagnostics in the evidence and are not counted as complete closure comparisons.

## Validation record

- `python3 tests/tooling/check_phase27_est.py ./moss` — passed.
- `python3 tests/tooling/check_phase27_string_debt.py` — passed, with the emitter inventory false green documented above.
- `python3 tests/tooling/check_phase27_string_debt.py --require-zero checker`, `--require-zero emitter`, `--require-zero reach_walk`, and `--require-zero fast_debug` — each invocation exited 0; emitter is incomplete because of the omitted include.
- Baseline and candidate `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` — passed.
- `python3 tests/tooling/check_agent_feedback.py` — passed.
- No performance testing was performed.
