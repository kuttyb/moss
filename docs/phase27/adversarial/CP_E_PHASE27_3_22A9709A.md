# CP-E Phase 27.3 fresh review — 22a9709a

Frozen candidate: `22a9709a76539df7dc35c0d20e22582fc82f03a6`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Independent worktree branch: `cp-e-27-3-22a9709a`

## Verdict

CP-E verdict: GO

## Scope and startup

Loaded `moss-language` and `moss-agent-workflow`; built the missing repository compiler, then ran `./moss agent bootstrap --json`. Contract: `moss-0.1`. Read only the Phase 27.3 portion of `.codex/CURRENT_STATUS.md`, the pattern inventory, and the string-reader exemptions. No prior adversarial reports or fixtures were opened. No source files were changed.

## Reader and closure audit

Ran the fail-closed `check_phase27_string_debt.py` reader audit for checker, emitter, reach-walk, and Fast Debug. Each `--require-zero` invocation passed. The scanner finds no checker/emitter `tree_expression_text(...)` calls and no newly introduced legacy expression-reader sites in migrated consumers. It reports zero legacy reader sites in checker/emitter/reach-walk/Fast Debug; the 81 remaining calls are outside those consumers: optimizer 10, parser 23, compiler frontend 48. The retained optimizer sites are outside this phase's scope.

The widened raw-slot audit reports 40 literal NAME/keyword comparisons (all covered by the exact per-consumer allowlist) and nine checker binding-name comparisons. It found no unclassified raw comparisons, raw expression-shape tests, or `tree_expression_text` reads. The three function/API-keyed exemptions match exactly once each and are:

- `phase20_is_nontransferable_capability | split_top_level`
- `builtin_equality_type | split_top_level`
- `contains_float_type | split_top_level`

Each reads canonical type-argument spellings, not expression or pattern text. The exemption parser fails closed for missing functions, duplicate keys, stale keys, and more than one matching API site; exact fixture source and inventory are preserved under `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/`.

## Focused regressions and checked closure

The four EST fixtures (`message_expression.moss`, `bitwise_shift.moss`, `numeric_member.moss`, `syntax_27_1b.moss`) each passed `moss check` in baseline and candidate, with `MOSS_EST=off` and `shadow` (16 successful checks). The 24 explicitly named non-adversarial Phase 27 EST/27.1 repro fixtures were checked in baseline and candidate, off and shadow (96 checks total). The rejected-source subset includes all 17 named cases in the EST regression driver. No baseline/candidate diagnostic delta or off/shadow divergence occurred across these fixtures. The prohibited `check_phase27_est.py` script was not run.

Package and source-free closure was checked with `sdk/advanced-data-acceptance/check_source_free.py --abi v9 --profile debug` on baseline and candidate in both EST modes. All four invocations passed. The gate builds/checks/formats the compiled-only consumer against 14 provider interfaces, builds the executable, and verifies its expected output. This establishes checked declarations and source-free package closure under the supported package context.

## Corpus and diagnostic comparison

Enumerated Moss files under `tests/` and `sdk/` with `os.walk`, pruning directories named `adversarial` before descent. Compared structured diagnostics for all 934 files under baseline/candidate and off/shadow (3,736 checks). Candidate off and shadow agree on all corpus sources. There is one baseline-to-candidate standalone diagnostic delta, counted BENIGN because this fixture was run without its imported module interfaces, while the package-backed source-free oracle above passes in both modes:

` sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57:1 `

- Baseline off and shadow: `RECOVERY_PATTERN_REQUIRES_VARIANT`, `recovery patterns must name a qualified enum variant`.
- Candidate off and shadow: `UNKNOWN_ERROR_TYPE`, `unknown error enum 'ndarray.NdArrayError'`.
- Package-context oracle: baseline and candidate both pass the source-free compiled-only package gate in off and shadow.

The retained reproducer inputs are the two-path package pair, `Moss.toml` and the unchanged `source_free_consumer.moss`; the package runner is `sdk/advanced-data-acceptance/check_source_free.py`. Machine-readable corpus results, exact fixture mode results, and all four package-gate logs are preserved with SHA-256 manifests under the probe directory.

## Gate status

- Compiler build: passed.
- Bootstrap and language contract: passed (`moss-0.1`).
- Reader/exemption audit: passed.
- Four EST fixtures: passed in both modes on baseline and candidate.
- 24 non-adversarial Phase 27 EST/27.1 repro fixtures: 96 checks, no baseline/candidate delta or mode divergence.
- Source-free package/native build and execution gate: passed on baseline and candidate in both modes.
- Corpus: 934 sources; zero candidate off/shadow disagreements; one benign standalone diagnostic delta against baseline, as detailed above.
- Performance: not run; excluded from CP-E.

67fe43ae51daad75e3818bd1a47ca3d379624d417d15fe15ed07b82f9adf7b19  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/corpus-diags.jsonl
053dfad939b6c5807a617193ad7f8d9d4a6cbc83c02ecd6089f12cbfb58b7c93  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/fixtures-four-modes.tsv
35671b514d0bf188842ad723c3b79b641de44d1a41d85b49a9feb9be96fe37dc  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source-free-baseline-off.log
35671b514d0bf188842ad723c3b79b641de44d1a41d85b49a9feb9be96fe37dc  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source-free-baseline-shadow.log
35671b514d0bf188842ad723c3b79b641de44d1a41d85b49a9feb9be96fe37dc  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source-free-candidate-off.log
35671b514d0bf188842ad723c3b79b641de44d1a41d85b49a9feb9be96fe37dc  tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source-free-candidate-shadow.log
