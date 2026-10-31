# CP-F blind review — Phase 27.3

**Verdict: GO**

Candidate source: `6363ed49` (source tree matches freeze `f45c1517`; this candidate SHA adds only the root review-status clarification). Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4` (`phase27-2-m1`). The audit used a scrubbed independent checkout and did not modify `src/`.

## Differential results

- The tracked Moss corpus had 2,213 sources. Baseline and candidate each checked 1,418 as accepted. Off and shadow had no accept/reject changes. Exact diagnostic payload comparison covered all four combinations (baseline/candidate × off/shadow), 8,852 compiler invocations. Only one unique source text differed, at `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss:57`; the same fixture is copied once under a phase adversarial test path. Baseline standalone reports `RECOVERY_PATTERN_REQUIRES_VARIANT` while candidate reports `UNKNOWN_ERROR_TYPE`; both reject standalone with the same line, column, and span. **BENIGN**: the independent source-free package oracle passed for baseline and candidate in off and shadow, all 14 provider interfaces built, and the consumer executed with identical expected output.
- Default generated Rust hashes matched for all 1,407 programs successfully emitted in both runs, off and shadow. With `-O`, all 1,407 emitted Rust hashes matched as well. The remaining accepted corpus sources had the same non-emission result on both sides; the corpus contains pre-existing package-context blockers.
- Fast Debug long-timeout fixture `examples/functional_pipeline.moss`: baseline off and candidate shadow each hit the one-time 600-second limit. Record as **BOTH-TIMEOUT**, excluded fixture per L7; no parity conclusion is drawn for it.
- `.mossi` ABI-v9 library and app artifacts were byte-equal after recomputing the baseline package fingerprint; execution-body hashes matched exactly for both artifacts.
- Full ASan/UBSan shadow scan: 2,213 sources, zero sanitizer, signal, timeout, or JSON-process findings.
- Focused Phase 27.1 rejected-source sweep: 34/34 exact code/message/line/column matches in baseline and candidate, both off and shadow. The standalone `fn main(): echo 2 +` probe and package-mode equivalent all reject with `MOSS_COMPILE_ERROR: invalid arithmetic expression` at 2:1.

## Reach delta and inherited rows

A same-revision off-vs-shadow reach probe over the 2,213 tracked sources produced 160 differing entries for baseline and 160 for candidate. The complete row sets are identical: 1,546 missing and 1,546 added edge records, four checked declarations missing from shadow, and none added. Thus there is no candidate-only reach delta. The four inherited checked-closure drops are the `heap__Entry.key_of@9` method in the `heapkit` order sources/tests. They are a **LEGACY MISPARSE**: the source at `order.moss:9` declares top-level `fn key_of(item: Keyed)` and calls it at line 18; the same-leaf `heap.Entry.key_of` method is a distinct receiver method. A minimal independent Moss calls oracle resolves `key_of` to `fn:key_of@11` and reports its direct call as `method:Entry.key`, confirming the top-level function binding. Baseline and candidate off/shadow trace rows match exactly for all four inherited cases.

## Findings and classifications

1. Standalone diagnostic refinement on the source-free `ndarray.NdArrayError.InvalidAxis` recovery line: **BENIGN**, supported by the four-way package-aware build/run oracle and unchanged standalone rejection verdict.
2. Four inherited same-leaf checked-declaration closure rows: **LEGACY MISPARSE**, supported by source-level call resolution and the identical baseline/candidate two-mode reach records.
3. Fast Debug long run: **BOTH-TIMEOUT**, excluded fixture; both binaries timed out at 600 seconds.
4. No other tree/legacy, verdict, diagnostic, generated Rust, package-mode, or sanitizer differences were found in the audited scope.

## Evidence and reproduction

The paired corpus TSVs, full diagnostic comparison, focused probes, package `.mossi` comparison, reach rows and oracle, 600-second Fast Debug record, and sanitizer output are in `tests/phase27/adversarial/cp-f-final-blind-agent2-20261030/`. `CHECKSUMS.sha256` covers every file in that directory except itself. The candidate is a frozen review checkout; no implementation change is requested by this review.
