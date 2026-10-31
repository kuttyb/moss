# CP-E post-taint blind review

**NO-GO**

Candidate: `3aa929be9f6413bb2864049d864ce4890a4f1bdf`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`  
Compiler SHA-256: candidate `c6d4b26f05459cfd0b4b44f8134367c2aefb7374d374dca1d9ab52736eb6b031`; baseline `4a334c71d9b5ea5b2925c311f7a462abd046e479fc4b52df4c0453e612ceb286`.

The NO-GO is due to one confirmed migration bug, reproduced below. There were no additional production expression-text readers in the bounded taint audit. The four-way checked-declaration scan found no candidate-specific closure delta.

## Reader audit

- Widened checker reader sites: **0**. Widened emitter reader sites: **0**. `python3 tests/tooling/check_phase27_string_debt.py --require-zero checker --require-zero emitter` passed.
- Exhaustive raw `Stmt.a` / `.b` / `.text` equality inventory: 51 sites total, 42 literal name/keyword/placeholder comparisons and 9 binding-name comparisons. Each is enumerated with its classification and source line in `raw/raw-string-equality.tsv`; all are accounted for by `27_3_STRING_READER_EXEMPTIONS.md` and the lint’s exact per-consumer counts.
- Checker/emitter `tree_expression_text(...)` uses: **0**. The exhaustive current-source search result is `raw/tree-expression-text-sites.txt`.
- Three type-argument exemptions remain keyed by function plus API. A fresh fail-closed mutation probe removed each function and added a second `split_top_level` site in each function; all six probes failed closed as required (`tests/phase27/adversarial/cp-e-post-taint-agent2-20261030/check_exemptions.py`; raw results in `raw/exemptions-fail-closed.json`).
- Bounded taint follow-through covered checker functional capture, grouping and unary paths, plus emitter token rewriting, placeholder replacement and literal classification. The inspected helper/local flows make semantic decisions with EST nodes or resolved facts. **No additional production character-inspecting helper reader was found.** The functional capture parity failure in the migration reproducer is recorded below.

## Migration bug and adjudication

Minimal two-path reproducer: `raw/domain-handle-map.moss`:

```moss
domain Worker:
  fn Ping():
    echo "pong"

fn main():
  w = Worker()
  workers = {"primary": w}
```

Oracle: the repository language contract says domain handles are static routing capabilities, not values, and cannot be stored in collections. Both baseline modes reject this source with `MOSS_COMPILE_ERROR` at line 7 and message `domain handle 'w' is not an ordinary value; use domainroutes and message targets only`.

Candidate off instead reports `TYPE_AMBIGUOUS_OPEN_RESULT` at line 7. Candidate shadow aborts. Its exact helper input is `<{"primary": w}>`; `functional_captures` reports tree capture set size 0 versus legacy set size 1 (the binding `w`) and hits its parity assertion. This is a **MIGRATION BUG**: the malformed collection value must retain the baseline rejection and diagnostic in both modes. Full stdout/stderr for both binaries and modes is in `raw/probe-*`.

## Corpus and checked-declaration closure

The same candidate-SHA source archive was used for all four compiler/mode runs. Each corpus run checked 2,215 tracked `.moss` files, with 1,419 accepted and **zero TIMEOUT rows**. Baseline/candidate verdicts matched within each mode except:

| Source | Baseline | Candidate | Classification |
|---|---|---|---|
| `examples/swarm/domain_torture/domain_boundaries/probe3e_store_map.moss:8` | `MOSS_COMPILE_ERROR` in off and shadow | `TYPE_AMBIGUOUS_OPEN_RESULT` in off; shadow abort | **MIGRATION BUG**, detailed above |
| `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57` | `RECOVERY_PATTERN_REQUIRES_VARIANT` standalone | `UNKNOWN_ERROR_TYPE` standalone | **BENIGN**, package-mode source-free oracle passes on both binaries |
| `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss:57` | `RECOVERY_PATTERN_REQUIRES_VARIANT` standalone | `UNKNOWN_ERROR_TYPE` standalone | **BENIGN**, package-mode source-free oracle passes on both binaries |

The source-free oracle passed on baseline and candidate: ABI-9 debug build, 14 provider interfaces, package consumer execution. Logs: `raw/source-free-baseline.log`, `raw/source-free-candidate.log`. The `.mossi` EST source-free oracle also passed on the candidate in off and shadow (covered by the Phase 27 focused EST harness).

Checked-declaration closure used one exported candidate source tree for baseline off/shadow and candidate off/shadow. It enumerated only regular `.moss` files (2,215 entries). No per-entry invocation timed out. Each of the four runs emitted 662 complete traces. All 221 accepted roots containing `import` had a complete closure trace in every run. Of accepted roots without imports, 823 had no closure to cover; 375 also emitted traces. The remaining no-trace entries were rejected sources (730 each, except candidate shadow’s probe3e abort). Thus all accepted import/package roots were covered.

Same-mode closures matched exactly: baseline off == candidate off, and baseline shadow == candidate shadow. Baseline off↔shadow and candidate off↔shadow each had the same four missing `method:heap__Entry.key_of@9` declarations, on the same two `order.moss` / `order_test.moss` source pairs and their natural-chain copies. The candidate introduced **zero** checked-declaration closure deltas. Per L7’s instruction, this pre-existing baseline delta is reported as a delta check, not treated as a new candidate finding. Full four-way records are in `raw/checked-closure-4way.json` and `raw/checked-closure-runs.json`.

The Phase 27 EST focused harness passed, including its Phase 27.1 malformed/rejected-source sweep in off and shadow; log `raw/phase27-rejected-sweep.log`.

No Fast Debug corpus scan was part of this CP-E assignment, so no Fast Debug fixture was observed timing out on both compilers; the matched 600-second follow-up rule was not triggered here.

## Fresh-agent bootstrap

`./moss agent bootstrap --json` succeeded with `language_version = moss-0.1`. See `raw/bootstrap.json`.


## Review-attempt note

An initial closure pass used a `*.moss` recursive glob that also selected three directories named `.moss`, producing 2,218 entries. That pass was superseded; the final four-way scan filters for regular files and covers the exact 2,215 tracked `.moss` files. No conclusion relies on the superseded pass.
