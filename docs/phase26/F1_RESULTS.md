# Phase 26.1 F1 — interface freshness and source boundaries

Implementation: `b0744694e08ddf2809bf4dd6705d616f6c6c28ba` on
`phase26-1-m1`, based on main `d109b8f17276f70b591432fd345d57e727f17501`.

F1 records a content hash of each package's Moss sources in `.mossi`, outside
does not use `MOSS_FAST_DEBUG_SOURCE_ROOTS`. It follows the requested package
module's local source closure and resolves other packages only through current
`.mossi` files. Every imported interface load, including direct `moss build`,
validates freshness before trusting its records. Candidate discovery excludes
the checking package's own build tree. Margo and Fast Debug keep their
package-aware source handoff.

Phase 26.1 retires binary-only source-free provider packages under R2. Imported
semantic interfaces and native linking remain available when the provider
package source is present. The rule and scope are recorded in
[`PHASE_26_1_RULES.md`](PHASE_26_1_RULES.md), and every modified test file is
classified in [`F1_TEST_MIGRATION.md`](F1_TEST_MIGRATION.md).

## Validation

| Gate | Result |
| --- | --- |
| Full corpus differential vs `d109b8f1` | 2,064 sources; 1,325 accepted on both sides; 0 verdict changes; 0 diagnostic-code changes; 0 generated-Rust hash changes |
| Fast Debug parity | Passed in `shard_fast_debug` within the full regression suite |
| Full corpus TSV comparison | Baseline and candidate outputs byte-identical |
| `make --jobs="$(nproc)" check` | 142/142 jobs passed on `b0744694` |
| F1 and borrowed-read focused checks | Passed, including direct-build stale `.mossi` rejection |
| Agent feedback, Python compilation, and whitespace validation | Passed |
| Architect review | Focused follow-up GO; final exact-SHA review requested |

## Performance

Run `20261026-192030` used WSL2 host `DESKTOP-0T772KP` for baseline `d109b8f1`
and candidate `b0744694`. It included all 77 fixtures, three interleaved
untraced timing repetitions, and one profiling sample per side. The sum of
fixture wall-time medians was 5.258 s baseline and 5.292 s candidate (+0.7%).
No fixture exceeded the noise floor.

| Attribution workload | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| `example_checkout` | 3.54 ms | 3.77 ms | +6.5% |
| `rw_packages_regex` | 711.08 ms | 715.91 ms | +0.7% |
| `rw_projects_rolling_window` | 49.43 ms | 48.59 ms | -1.7% |
| `rw_sdk_datastructures` | 275.80 ms | 283.29 ms | +2.7% |
| `w2_spec_distinct_n032` | 1788.43 ms | 1798.09 ms | +0.5% |
| `w3_spec_repeated_n032` | 869.34 ms | 879.89 ms | +1.2% |
| `w4_infer_deep_d018` | 26.80 ms | 25.80 ms | -3.7% |
| `w5_infer_wide_w100` | 121.81 ms | 123.90 ms | +1.7% |

Small-fixture latency: checkout was 3.54 ms baseline / 3.77 ms candidate;
rolling window was 49.43 ms / 48.59 ms. Full per-fixture wall, CPU, peak RSS,
ranges, and work counters are in `tmp/compiler-perf/20261026-192030/`.

No GitHub regression jobs were launched.
