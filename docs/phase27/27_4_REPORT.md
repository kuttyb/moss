# Phase 27.4 report (in progress)

PASS optimizer reader sites = 0
PASS text-path calls in functional_optimizer.cpp = 0
PASS -O differences: 0, all adjudicated
PASS zero unadjudicated differences vs phase27-3-m1 = 0

## Iteration 1 — implementation and bulk differential

Base: `phase27-3-m1`, merge `ce5f836e06ce93909c358e6e6455402417d14991`. The optimizer now resolves plans and expressions through owning EST handles and ExprIds. Existing source strings and `functional_pipeline_ids` remain and are dual-written. Pattern work is out of scope.

- Optimizer reader lint: `python3 tests/tooling/check_phase27_string_debt.py --require-zero optimizer` — PASS, zero readers.
- Mechanical forbidden-call scan in `src/functional_optimizer.cpp` (`est::parse_view`, fingerprint lookup, `tree_expression_text`, `word_occurrences`, `plain_identifier`, `split_top_level`, `functional_pipeline_ids`, `callable_expression`) — zero matches.
- Full source corpus, baseline off vs candidate off and candidate shadow: 2,226 sources; zero verdict/diagnostic differences and zero incomplete runs.
- Default and `-O` Rust emission: 1,424 common accepted sources, 2,848 profile comparisons; zero hash mismatches among 2,826 successful matched emissions, 22 identical paired emitter failures, zero unpaired failures/timeouts.
- Top-level examples: 31; zero check, Fast Debug, default Rust, or optimized Rust output differences; zero incomplete runs. Three shared Fast Debug nonzero cases matched their paired status/output.
- Three-engine conformance gate: baseline and candidate both pass; outputs and callback count match, count 3.

Raw results and command logs: `tests/phase27/adversarial/phase27-4-iteration-1/`. The bulk harness reused the committed Phase 27.3 corpus, Rust emission, and examples parity scripts against the exact `phase27-3-m1` baseline.

## Gates before Agent 2

- `MOSS_EST=off make --jobs="$(nproc)" check` — PASS. Log: `tests/phase27/adversarial/phase27-4-gates-iteration-1/check-off.log`.
- `MOSS_EST=shadow make --jobs="$(nproc)" check` — PASS. Log: `tests/phase27/adversarial/phase27-4-gates-iteration-1/check-shadow.log`.
- EST `.mossi` off/shadow raw parity and generic/Section2 import checks — PASS. Log: `tests/phase27/adversarial/phase27-4-gates-iteration-1/est-mossi.log`.
- Phase 27.3 `.mossi` baseline parity — PASS; normalized library and app bytes match. Log: `tests/phase27/adversarial/phase27-4-gates-iteration-1/mossi-baseline.log`.
- Source-free package acceptance (`--abi v9 --profile debug`) — PASS. Log: `tests/phase27/adversarial/phase27-4-gates-iteration-1/source-free-candidate.log`.
- First-parent performance against `ce5f836e06ce93909c358e6e6455402417d14991`: aggregate 5.447 s → 5.611 s (+3.0%). No `rw_*` workload crossed the beyond-noise alert. The only alert was torture fixture `w2_spec_distinct_n032` (+7.9%, +150.06 ms); recorded and nonblocking per the Phase 27.4 ruling. No repeat was required by the aggregate/`rw_*` gate. Historical comparison vs `phase27-1-m1` is not a gate and is pending report extraction. Raw report and CSVs: `tests/phase27/adversarial/phase27-4-perf-iteration-1/`.
- Agent 2 verdict: **GO**. Review SHA `d0f5358449ac1f73f0c3bfa8d273b6a2e620d31f`, recorded unchanged at `docs/phase27/adversarial/phase27-4-agent2-audit.md` with machine-readable results at `tests/phase27/adversarial/phase27-4-agent2-audit.json`. The independent audit found zero optimizer expression-text paths, zero `-O` hash mismatches across 2,226 sources, and matching three-engine outputs with callback count 3. One paired w2 timeout and eleven paired emitter failures were recorded; none were one-sided.
- Implementation SHA audited: `7dd0468540359447c5c1fff55e5eadaa2f66f174`. Merge and merge-commit gates remain pending.

## L7 review follow-up

L7 approved the `7dd04685` review and requested two additions. `statement_expression_roots` now reports missing expected EST handles as internal-invariant diagnostics in debug/test builds; incomplete dependencies conservatively block fusion. A new conformance fixture checks that `p.total` does not count as a use of a pipeline binding named `total`; it asserts the materialization reason and compares Fast Debug/native/`-O` output and callback counts. Focused build and regression passed (callback count 3). Full bulk parity and merge-commit gates must be rerun after this source change. Agent 2 has been asked whether it requires another audit.

Peer follow-up iteration 1 at source candidate before the bare-return completeness correction:

- Bulk corpus: 2,226; baseline-off/candidate-off and candidate-off/shadow verdict/diagnostic deltas 0, incomplete 0. Rust emission 2,848 comparisons: 0 mismatches, 22 identical paired failures, no unpaired failures. Examples: 31, zero mismatches. Baseline/candidate three-engine checks passed. The six focused expression-use fixtures passed (baseline and candidate three-engine paths also matched on the field-name reproducer).
- Decision parity (`-O --dump-functional-ir`): 2,232 source paths, 1,419 accepted in all three modes. One baseline/candidate delta, with candidate off/shadow identical: `tests/phase27/conformance/field_name_not_binding_use.moss`. Baseline says `multiple consumers`; candidate says `escapes optimization scope`. Both three-engine paths pass with identical output and callback count 3. **LEGACY MISPARSE**, independently decided by EST Identifier nodes and reproduced on both paths. Exact report and repro log: `tests/phase27/adversarial/phase27-4-followup-iteration-1/`.
- The debug invariant probe reported 376 missing-handle events across 64 source paths because bare Return/Reply statements were incorrectly treated as carrying a ReturnValue slot. This is a migration bug in the expected-slot inventory, not an accepted incompleteness. The collector now treats empty Return/Reply/Raise values as having no expected expression slot. This correction invalidates the decision parity and gates above; the full bulk suite is being rerun.
