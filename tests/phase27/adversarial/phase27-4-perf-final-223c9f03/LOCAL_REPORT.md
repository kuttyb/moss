# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(2.2685546875, 5.74267578125, 6.42529296875)`; after profiling `(1.67822265625, 5.09423828125, 6.17138671875)`.
- Candidate benchmark checkout SHA: `223c9f03505e348a2c26fab3c76eaddd9c238ede`
- Candidate compiler source SHA: `223c9f03505e348a2c26fab3c76eaddd9c238ede`
- Candidate binary SHA256: `d4c35d419c7c4143ff20e810b625ae94ccb028dada936d24a719da3195572633`
- Certification mode: `False`; working tree dirty at start: `True`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `38056e74c049bcb4170e15a2288ab975abca3158` resolved to `38056e74c049bcb4170e15a2288ab975abca3158`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.467 s | 5.573 s | +1.9% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.95 ms | 3.99 ms | +0.9% | 0.00 ms | 0.00 ms | 10440 KiB | 10580 KiB |
| `rw_packages_regex` | 671.20 ms | 667.11 ms | -0.6% | 660.00 ms | 660.00 ms | 30188 KiB | 30484 KiB |
| `rw_projects_rolling_window` | 51.21 ms | 51.72 ms | +1.0% | 40.00 ms | 40.00 ms | 16764 KiB | 16620 KiB |
| `rw_sdk_datastructures` | 249.38 ms | 254.99 ms | +2.2% | 240.00 ms | 250.00 ms | 23028 KiB | 23360 KiB |
| `w2_spec_distinct_n032` | 1897.02 ms | 2046.30 ms | +7.9% | 1890.00 ms | 2030.00 ms | 59620 KiB | 59888 KiB |
| `w3_spec_repeated_n032` | 890.37 ms | 891.60 ms | +0.1% | 880.00 ms | 880.00 ms | 28148 KiB | 28104 KiB |
| `w4_infer_deep_d018` | 28.96 ms | 28.58 ms | -1.3% | 20.00 ms | 20.00 ms | 11660 KiB | 11736 KiB |
| `w5_infer_wide_w100` | 141.05 ms | 129.43 ms | -8.2% | 130.00 ms | 120.00 ms | 20132 KiB | 20144 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.95 ms | 3.99 ms | +0.9% | 0.00 ms | 0.00 ms | 10440 KiB | 10580 KiB |
| `example_collections` | 3.28 ms | 3.48 ms | +5.9% | 0.00 ms | 0.00 ms | 10888 KiB | 10800 KiB |
| `example_counter` | 1.76 ms | 1.80 ms | +2.3% | 0.00 ms | 0.00 ms | 10068 KiB | 10176 KiB |
| `example_domain_typing` | 4.02 ms | 3.86 ms | -3.9% | 0.00 ms | 0.00 ms | 10656 KiB | 12052 KiB |
| `example_domains_and_messages` | 2.07 ms | 2.09 ms | +0.6% | 0.00 ms | 0.00 ms | 10104 KiB | 9984 KiB |
| `example_functional_basics` | 3.25 ms | 3.26 ms | +0.3% | 0.00 ms | 0.00 ms | 10556 KiB | 10428 KiB |
| `example_functional_dataflow` | 3.47 ms | 3.68 ms | +5.9% | 0.00 ms | 0.00 ms | 10428 KiB | 10528 KiB |
| `example_functional_pipeline` | 1.73 ms | 1.70 ms | -2.1% | 0.00 ms | 0.00 ms | 10192 KiB | 10068 KiB |
| `example_shared_memory` | 2.24 ms | 2.13 ms | -4.8% | 0.00 ms | 0.00 ms | 10312 KiB | 10316 KiB |
| `example_static_duck_typing` | 2.82 ms | 2.94 ms | +4.2% | 0.00 ms | 0.00 ms | 10608 KiB | 10708 KiB |
| `mg1_multi_file` | 5.69 ms | 5.39 ms | -5.4% | 0.00 ms | 0.00 ms | 11344 KiB | 11400 KiB |
| `mg2_multi_module` | 6.12 ms | 6.40 ms | +4.6% | 0.00 ms | 0.00 ms | 11364 KiB | 11528 KiB |
| `mg3_deep_chain` | 5.67 ms | 5.03 ms | -11.2% | 0.00 ms | 0.00 ms | 11256 KiB | 11388 KiB |
| `mg4_fan_out` | 9.43 ms | 9.98 ms | +5.8% | 0.00 ms | 0.00 ms | 11492 KiB | 11448 KiB |
| `mg5_diamond` | 4.15 ms | 4.49 ms | +8.1% | 0.00 ms | 0.00 ms | 11088 KiB | 11092 KiB |
| `negative_await_retired` | 1.18 ms | 1.19 ms | +0.8% | 0.00 ms | 0.00 ms | 7204 KiB | 7324 KiB |
| `negative_domain_route_cycle` | 1.40 ms | 1.35 ms | -3.4% | 0.00 ms | 0.00 ms | 9448 KiB | 9576 KiB |
| `negative_duck_missing_method` | 1.77 ms | 1.69 ms | -4.4% | 0.00 ms | 0.00 ms | 10232 KiB | 10232 KiB |
| `negative_heterogeneous_collection` | 1.35 ms | 1.34 ms | -0.4% | 0.00 ms | 0.00 ms | 9836 KiB | 10016 KiB |
| `negative_mutually_recursive_functions` | 1.34 ms | 1.47 ms | +9.4% | 0.00 ms | 0.00 ms | 9296 KiB | 9292 KiB |
| `negative_self_send_rejected` | 1.47 ms | 1.54 ms | +4.5% | 0.00 ms | 0.00 ms | 9868 KiB | 9812 KiB |
| `negative_spawn_retired` | 1.38 ms | 1.29 ms | -6.7% | 0.00 ms | 0.00 ms | 9480 KiB | 9460 KiB |
| `negative_trait_missing_method` | 2.18 ms | 2.27 ms | +4.3% | 0.00 ms | 0.00 ms | 10256 KiB | 10344 KiB |
| `negative_write_write_alias` | 2.11 ms | 1.98 ms | -6.2% | 0.00 ms | 0.00 ms | 10340 KiB | 10392 KiB |
| `rw_example_checkout` | 3.92 ms | 3.88 ms | -0.9% | 0.00 ms | 0.00 ms | 10364 KiB | 10552 KiB |
| `rw_example_collections` | 3.49 ms | 3.31 ms | -5.3% | 0.00 ms | 0.00 ms | 10680 KiB | 10888 KiB |
| `rw_example_counter` | 1.81 ms | 1.81 ms | +0.3% | 0.00 ms | 0.00 ms | 10220 KiB | 11848 KiB |
| `rw_example_domains` | 1.97 ms | 2.29 ms | +16.1% | 0.00 ms | 0.00 ms | 10260 KiB | 10120 KiB |
| `rw_example_pipeline` | 2.06 ms | 1.83 ms | -11.1% | 0.00 ms | 0.00 ms | 10196 KiB | 10044 KiB |
| `rw_packages_regex` | 671.20 ms | 667.11 ms | -0.6% | 660.00 ms | 660.00 ms | 30188 KiB | 30484 KiB |
| `rw_phase106d1_borrowed_reads` | 12.38 ms | 13.53 ms | +9.2% | 0.00 ms | 0.00 ms | 11508 KiB | 11312 KiB |
| `rw_phase106d1_read_projection` | 10.00 ms | 10.37 ms | +3.7% | 0.00 ms | 0.00 ms | 11412 KiB | 11304 KiB |
| `rw_phase106d_handler_2pl` | 7.52 ms | 7.10 ms | -5.7% | 0.00 ms | 0.00 ms | 10676 KiB | 10704 KiB |
| `rw_phase15_15_enum_positive` | 6.36 ms | 5.65 ms | -11.1% | 0.00 ms | 0.00 ms | 10812 KiB | 10860 KiB |
| `rw_phase20_fileio_helper` | 2.88 ms | 2.51 ms | -12.9% | 0.00 ms | 0.00 ms | 10124 KiB | 10196 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.96 ms | 2.92 ms | -1.3% | 0.00 ms | 0.00 ms | 10208 KiB | 10440 KiB |
| `rw_phase24_string_concat` | 6.01 ms | 5.95 ms | -1.0% | 0.00 ms | 0.00 ms | 11004 KiB | 11024 KiB |
| `rw_phase25_integer_overflow` | 6.17 ms | 6.27 ms | +1.7% | 0.00 ms | 0.00 ms | 10724 KiB | 10800 KiB |
| `rw_phase46_semantic_barriers` | 7.53 ms | 7.41 ms | -1.6% | 0.00 ms | 0.00 ms | 10876 KiB | 10628 KiB |
| `rw_phase47_iteration` | 8.49 ms | 9.09 ms | +7.1% | 0.00 ms | 0.00 ms | 10816 KiB | 10732 KiB |
| `rw_phase4_functional` | 14.58 ms | 15.12 ms | +3.7% | 10.00 ms | 10.00 ms | 11580 KiB | 11364 KiB |
| `rw_projects_cache` | 30.83 ms | 31.83 ms | +3.2% | 20.00 ms | 20.00 ms | 14464 KiB | 14728 KiB |
| `rw_projects_rolling_window` | 51.21 ms | 51.72 ms | +1.0% | 40.00 ms | 40.00 ms | 16764 KiB | 16620 KiB |
| `rw_sdk_datastructures` | 249.38 ms | 254.99 ms | +2.2% | 240.00 ms | 250.00 ms | 23028 KiB | 23360 KiB |
| `rw_swarm_047_reduce_map` | 7.83 ms | 7.99 ms | +2.0% | 0.00 ms | 0.00 ms | 10740 KiB | 10652 KiB |
| `rw_swarm_066_for_range` | 9.26 ms | 8.30 ms | -10.3% | 0.00 ms | 0.00 ms | 10836 KiB | 10820 KiB |
| `rw_tests_branch_confined_open` | 3.12 ms | 3.06 ms | -2.2% | 0.00 ms | 0.00 ms | 10624 KiB | 10428 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.23 ms | 2.31 ms | +3.6% | 0.00 ms | 0.00 ms | 10192 KiB | 10204 KiB |
| `test_trait_open_forwarding` | 3.07 ms | 2.84 ms | -7.5% | 0.00 ms | 0.00 ms | 10900 KiB | 10948 KiB |
| `test_unused_inferred_field_template` | 1.39 ms | 1.46 ms | +5.0% | 0.00 ms | 0.00 ms | 9912 KiB | 9804 KiB |
| `w2_spec_distinct_n008` | 94.28 ms | 94.12 ms | -0.2% | 90.00 ms | 90.00 ms | 13888 KiB | 14048 KiB |
| `w2_spec_distinct_n016` | 406.14 ms | 396.37 ms | -2.4% | 400.00 ms | 390.00 ms | 23396 KiB | 23328 KiB |
| `w2_spec_distinct_n032` | 1897.02 ms | 2046.30 ms | +7.9% | 1890.00 ms | 2030.00 ms | 59620 KiB | 59888 KiB |
| `w3_spec_repeated_n008` | 62.97 ms | 61.85 ms | -1.8% | 60.00 ms | 60.00 ms | 11840 KiB | 11876 KiB |
| `w3_spec_repeated_n016` | 236.58 ms | 231.54 ms | -2.1% | 230.00 ms | 220.00 ms | 15088 KiB | 14932 KiB |
| `w3_spec_repeated_n032` | 890.37 ms | 891.60 ms | +0.1% | 880.00 ms | 880.00 ms | 28148 KiB | 28104 KiB |
| `w4_infer_deep_d005` | 6.17 ms | 7.49 ms | +21.3% | 0.00 ms | 0.00 ms | 10628 KiB | 10700 KiB |
| `w4_infer_deep_d010` | 12.83 ms | 13.06 ms | +1.8% | 10.00 ms | 10.00 ms | 11128 KiB | 11048 KiB |
| `w4_infer_deep_d015` | 22.93 ms | 21.59 ms | -5.9% | 20.00 ms | 20.00 ms | 11440 KiB | 11536 KiB |
| `w4_infer_deep_d018` | 28.96 ms | 28.58 ms | -1.3% | 20.00 ms | 20.00 ms | 11660 KiB | 11736 KiB |
| `w5_infer_wide_w010` | 13.81 ms | 14.66 ms | +6.2% | 10.00 ms | 10.00 ms | 11468 KiB | 11500 KiB |
| `w5_infer_wide_w025` | 32.31 ms | 34.76 ms | +7.6% | 30.00 ms | 30.00 ms | 12792 KiB | 12708 KiB |
| `w5_infer_wide_w050` | 71.67 ms | 66.33 ms | -7.5% | 70.00 ms | 60.00 ms | 15100 KiB | 15244 KiB |
| `w5_infer_wide_w100` | 141.05 ms | 129.43 ms | -8.2% | 130.00 ms | 120.00 ms | 20132 KiB | 20144 KiB |
| `w6_types_count_t010` | 7.72 ms | 8.28 ms | +7.3% | 0.00 ms | 0.00 ms | 11092 KiB | 10988 KiB |
| `w6_types_count_t025` | 19.02 ms | 18.37 ms | -3.4% | 10.00 ms | 10.00 ms | 12280 KiB | 12316 KiB |
| `w6_types_count_t050` | 40.99 ms | 40.10 ms | -2.2% | 30.00 ms | 30.00 ms | 15128 KiB | 15148 KiB |
| `w6_types_count_t100` | 103.85 ms | 98.17 ms | -5.5% | 90.00 ms | 90.00 ms | 22008 KiB | 22096 KiB |
| `w7_collections_nested_k1` | 1.89 ms | 1.97 ms | +3.9% | 0.00 ms | 0.00 ms | 10100 KiB | 9888 KiB |
| `w7_collections_nested_k2` | 1.97 ms | 2.10 ms | +6.3% | 0.00 ms | 0.00 ms | 10044 KiB | 10172 KiB |
| `w7_collections_nested_k3` | 2.99 ms | 2.57 ms | -14.2% | 0.00 ms | 0.00 ms | 10256 KiB | 9860 KiB |
| `w7_collections_nested_k4` | 2.94 ms | 3.67 ms | +25.0% | 0.00 ms | 0.00 ms | 10216 KiB | 10120 KiB |
| `w7_collections_nested_k5` | 3.41 ms | 3.73 ms | +9.5% | 0.00 ms | 0.00 ms | 10344 KiB | 10312 KiB |
| `w8_traits_dispatch_c010` | 9.34 ms | 9.72 ms | +4.1% | 0.00 ms | 0.00 ms | 11280 KiB | 11320 KiB |
| `w8_traits_dispatch_c025` | 21.88 ms | 20.19 ms | -7.7% | 10.00 ms | 10.00 ms | 12260 KiB | 12348 KiB |
| `w8_traits_dispatch_c050` | 47.92 ms | 40.04 ms | -16.4% | 40.00 ms | 30.00 ms | 14300 KiB | 14184 KiB |
| `w8_traits_dispatch_c100` | 84.88 ms | 81.78 ms | -3.7% | 80.00 ms | 70.00 ms | 18416 KiB | 18328 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 51.21 ms | 51.72 ms | +1.0% | 40.00 ms | 40.00 ms | 16764 KiB | 16620 KiB |
| `example_checkout` | 3.95 ms | 3.99 ms | +0.9% | 0.00 ms | 0.00 ms | 10440 KiB | 10580 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `w2_spec_distinct_n032` | 1897.02 [1894.54–1904.06] ms | 2046.30 [2029.07–2070.87] ms | +149.28 ms | +7.9% | 2030.00 ms | 59888 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `w7_collections_nested_k4` | 2.94 ms | 3.67 ms | +0.74 ms | +25.0% |
| `w4_infer_deep_d005` | 6.17 ms | 7.49 ms | +1.31 ms | +21.3% |
| `rw_example_domains` | 1.97 ms | 2.29 ms | +0.32 ms | +16.1% |
| `w7_collections_nested_k5` | 3.41 ms | 3.73 ms | +0.32 ms | +9.5% |
| `negative_mutually_recursive_functions` | 1.34 ms | 1.47 ms | +0.13 ms | +9.4% |
| `rw_phase106d1_borrowed_reads` | 12.38 ms | 13.53 ms | +1.14 ms | +9.2% |
| `mg5_diamond` | 4.15 ms | 4.49 ms | +0.34 ms | +8.1% |
| `w5_infer_wide_w025` | 32.31 ms | 34.76 ms | +2.45 ms | +7.6% |
| `w6_types_count_t010` | 7.72 ms | 8.28 ms | +0.56 ms | +7.3% |
| `rw_phase47_iteration` | 8.49 ms | 9.09 ms | +0.60 ms | +7.1% |
| `w7_collections_nested_k2` | 1.97 ms | 2.10 ms | +0.12 ms | +6.3% |
| `w5_infer_wide_w010` | 13.81 ms | 14.66 ms | +0.86 ms | +6.2% |
| `example_collections` | 3.28 ms | 3.48 ms | +0.19 ms | +5.9% |
| `example_functional_dataflow` | 3.47 ms | 3.68 ms | +0.20 ms | +5.9% |
| `mg4_fan_out` | 9.43 ms | 9.98 ms | +0.55 ms | +5.8% |
| `test_unused_inferred_field_template` | 1.39 ms | 1.46 ms | +0.07 ms | +5.0% |

## Profiling work counters

One separate profiling sample per fixture. This table shows candidate totals. Counter totals aggregate across checker instances; the CSV includes baseline and candidate plus each checker's individual records. Expected-negative fixtures can fail before any checker counters are emitted; those entries are n/a.

| Fixture | functions_analyzed | types_analyzed | specialization_requests | distinct_specialization_keys | checked_specializations | inference_rounds | inference_work_units | parse_cache_hits | parse_cache_misses | concrete_effect_evaluations | specialization_effect_cache_hits | specialization_effect_computations | effects_fixed_point_rounds |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 0 | 3 | 0 | 0 | 0 | 4 | 12 | 9458 | 606 | 0 | 0 | 0 | 2 |
| `example_collections` | 1 | 0 | 1 | 1 | 1 | 3 | 3 | 6788 | 481 | 3 | 0 | 2 | 2 |
| `example_counter` | 0 | 1 | 0 | 0 | 0 | 4 | 4 | 2661 | 187 | 0 | 0 | 0 | 2 |
| `example_domain_typing` | 2 | 1 | 3 | 1 | 1 | 4 | 12 | 11586 | 403 | 17 | 0 | 2 | 2 |
| `example_domains_and_messages` | 0 | 1 | 0 | 0 | 0 | 4 | 4 | 3030 | 203 | 0 | 0 | 0 | 2 |
| `example_functional_basics` | 1 | 0 | 1 | 0 | 0 | 3 | 3 | 6249 | 493 | 3 | 0 | 0 | 1 |
| `example_functional_dataflow` | 3 | 0 | 3 | 1 | 1 | 3 | 9 | 7369 | 343 | 27 | 4 | 6 | 1 |
| `example_functional_pipeline` | 1 | 0 | 2 | 0 | 0 | 3 | 3 | 1747 | 143 | 2 | 0 | 0 | 1 |
| `example_shared_memory` | 0 | 2 | 0 | 0 | 0 | 4 | 8 | 3859 | 272 | 0 | 0 | 0 | 2 |
| `example_static_duck_typing` | 1 | 2 | 2 | 2 | 2 | 3 | 5 | 4538 | 372 | 6 | 0 | 4 | 2 |
| `mg1_multi_file` | 10 | 0 | 10 | 0 | 0 | 3 | 30 | 20927 | 1002 | 10 | 0 | 0 | 1 |
| `mg2_multi_module` | 8 | 8 | 8 | 0 | 0 | 3 | 32 | 20370 | 1027 | 8 | 0 | 0 | 1 |
| `mg3_deep_chain` | 8 | 0 | 8 | 0 | 0 | 3 | 24 | 16417 | 585 | 22 | 0 | 0 | 1 |
| `mg4_fan_out` | 9 | 1 | 16 | 0 | 0 | 3 | 28 | 34238 | 1035 | 32 | 0 | 0 | 1 |
| `mg5_diamond` | 3 | 1 | 4 | 0 | 0 | 3 | 10 | 9937 | 398 | 8 | 0 | 0 | 1 |
| `negative_await_retired` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_domain_route_cycle` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_duck_missing_method` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_heterogeneous_collection` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_mutually_recursive_functions` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_self_send_rejected` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_spawn_retired` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_trait_missing_method` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_write_write_alias` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `rw_example_checkout` | 0 | 3 | 0 | 0 | 0 | 4 | 12 | 9458 | 606 | 0 | 0 | 0 | 2 |
| `rw_example_collections` | 1 | 0 | 1 | 1 | 1 | 3 | 3 | 6788 | 481 | 3 | 0 | 2 | 2 |
| `rw_example_counter` | 0 | 1 | 0 | 0 | 0 | 4 | 4 | 2661 | 187 | 0 | 0 | 0 | 2 |
| `rw_example_domains` | 0 | 1 | 0 | 0 | 0 | 4 | 4 | 3030 | 203 | 0 | 0 | 0 | 2 |
| `rw_example_pipeline` | 1 | 0 | 2 | 0 | 0 | 3 | 3 | 1747 | 143 | 2 | 0 | 0 | 1 |
| `rw_packages_regex` | 49 | 4 | 113 | 0 | 0 | 3 | 149 | 2446216 | 15090 | 565 | 0 | 0 | 2 |
| `rw_phase106d1_borrowed_reads` | 2 | 4 | 5 | 0 | 0 | 3 | 14 | 42141 | 1615 | 35 | 0 | 0 | 2 |
| `rw_phase106d1_read_projection` | 4 | 3 | 5 | 1 | 1 | 3 | 17 | 28934 | 1030 | 38 | 0 | 2 | 2 |
| `rw_phase106d_handler_2pl` | 2 | 4 | 7 | 0 | 0 | 3 | 16 | 29735 | 846 | 43 | 0 | 0 | 2 |
| `rw_phase15_15_enum_positive` | 4 | 2 | 9 | 0 | 0 | 3 | 13 | 14311 | 981 | 13 | 0 | 0 | 2 |
| `rw_phase20_fileio_helper` | 1 | 1 | 1 | 0 | 0 | 3 | 3 | 4150 | 271 | 3 | 0 | 0 | 2 |
| `rw_phase21_fileio_root_cleanup` | 0 | 2 | 0 | 0 | 0 | 4 | 4 | 5928 | 428 | 0 | 0 | 0 | 2 |
| `rw_phase24_string_concat` | 6 | 0 | 8 | 0 | 0 | 3 | 18 | 18670 | 1328 | 7 | 0 | 0 | 2 |
| `rw_phase25_integer_overflow` | 1 | 4 | 2 | 2 | 2 | 5 | 25 | 20739 | 856 | 6 | 0 | 4 | 2 |
| `rw_phase46_semantic_barriers` | 4 | 0 | 11 | 0 | 0 | 3 | 12 | 17181 | 791 | 33 | 0 | 0 | 2 |
| `rw_phase47_iteration` | 3 | 1 | 5 | 3 | 3 | 3 | 10 | 34944 | 835 | 11 | 0 | 6 | 2 |
| `rw_phase4_functional` | 3 | 0 | 9 | 1 | 1 | 3 | 9 | 33221 | 1635 | 32 | 2 | 4 | 2 |
| `rw_projects_cache` | 1 | 2 | 12 | 0 | 0 | 3 | 7 | 131281 | 3021 | 12 | 0 | 0 | 2 |
| `rw_projects_rolling_window` | 1 | 2 | 20 | 0 | 0 | 3 | 7 | 198002 | 4743 | 22 | 0 | 0 | 2 |
| `rw_sdk_datastructures` | 112 | 11 | 147 | 0 | 0 | 3 | 346 | 1193086 | 10166 | 1010 | 0 | 0 | 3 |
| `rw_swarm_047_reduce_map` | 4 | 0 | 4 | 0 | 0 | 3 | 12 | 27692 | 609 | 14 | 0 | 0 | 2 |
| `rw_swarm_066_for_range` | 7 | 0 | 7 | 0 | 0 | 3 | 21 | 42146 | 1106 | 7 | 0 | 0 | 2 |
| `rw_tests_branch_confined_open` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 6133 | 464 | 0 | 0 | 0 | 1 |
| `test_phase45_short_circuit_failure_barrier` | 1 | 0 | 1 | 0 | 0 | 3 | 3 | 3943 | 272 | 3 | 0 | 0 | 2 |
| `test_trait_open_forwarding` | 2 | 2 | 2 | 2 | 2 | 4 | 9 | 5538 | 259 | 16 | 4 | 4 | 2 |
| `test_unused_inferred_field_template` | 0 | 1 | 0 | 0 | 0 | 3 | 1 | 405 | 50 | 0 | 0 | 0 | 1 |
| `w2_spec_distinct_n008` | 2 | 8 | 192 | 72 | 72 | 3 | 14 | 373623 | 3963 | 986 | 512 | 144 | 2 |
| `w2_spec_distinct_n016` | 2 | 16 | 768 | 272 | 272 | 3 | 22 | 1452119 | 13691 | 3882 | 2048 | 544 | 2 |
| `w2_spec_distinct_n032` | 2 | 32 | 3072 | 1056 | 1056 | 3 | 38 | 5747991 | 51195 | 15434 | 8192 | 2112 | 2 |
| `w3_spec_repeated_n008` | 2 | 8 | 72 | 6 | 6 | 3 | 14 | 284121 | 891 | 134 | 32 | 12 | 2 |
| `w3_spec_repeated_n016` | 2 | 16 | 264 | 6 | 6 | 3 | 22 | 1080209 | 1259 | 326 | 32 | 12 | 2 |
| `w3_spec_repeated_n032` | 2 | 32 | 1032 | 6 | 6 | 3 | 38 | 4252161 | 1995 | 1094 | 32 | 12 | 2 |
| `w4_infer_deep_d005` | 6 | 0 | 6 | 6 | 6 | 3 | 18 | 31080 | 346 | 58 | 20 | 12 | 1 |
| `w4_infer_deep_d010` | 11 | 0 | 11 | 11 | 11 | 3 | 33 | 81035 | 576 | 113 | 40 | 22 | 1 |
| `w4_infer_deep_d015` | 16 | 0 | 16 | 16 | 16 | 3 | 48 | 152265 | 806 | 168 | 60 | 32 | 1 |
| `w4_infer_deep_d018` | 19 | 0 | 19 | 19 | 19 | 3 | 57 | 205215 | 944 | 201 | 72 | 38 | 1 |
| `w5_infer_wide_w010` | 20 | 0 | 20 | 20 | 20 | 3 | 60 | 82082 | 1462 | 140 | 40 | 40 | 1 |
| `w5_infer_wide_w025` | 50 | 0 | 50 | 50 | 50 | 3 | 150 | 204692 | 3547 | 350 | 100 | 100 | 1 |
| `w5_infer_wide_w050` | 100 | 0 | 100 | 100 | 100 | 3 | 300 | 409042 | 7022 | 700 | 200 | 200 | 1 |
| `w5_infer_wide_w100` | 200 | 0 | 200 | 200 | 200 | 3 | 600 | 817742 | 13972 | 1400 | 400 | 400 | 1 |
| `w6_types_count_t010` | 0 | 10 | 0 | 0 | 0 | 3 | 10 | 26216 | 1503 | 0 | 0 | 0 | 1 |
| `w6_types_count_t025` | 0 | 25 | 0 | 0 | 0 | 3 | 25 | 65156 | 3558 | 0 | 0 | 0 | 1 |
| `w6_types_count_t050` | 0 | 50 | 0 | 0 | 0 | 3 | 50 | 130056 | 6983 | 0 | 0 | 0 | 1 |
| `w6_types_count_t100` | 0 | 100 | 0 | 0 | 0 | 3 | 100 | 259856 | 13833 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k1` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 1509 | 146 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k2` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 2357 | 223 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k3` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 3212 | 300 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k4` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 4073 | 377 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k5` | 0 | 0 | 0 | 0 | 0 | 3 | 0 | 4940 | 454 | 0 | 0 | 0 | 1 |
| `w8_traits_dispatch_c010` | 1 | 10 | 10 | 10 | 10 | 3 | 13 | 32469 | 1268 | 30 | 0 | 20 | 2 |
| `w8_traits_dispatch_c025` | 1 | 25 | 25 | 25 | 25 | 3 | 28 | 79569 | 2978 | 75 | 0 | 50 | 2 |
| `w8_traits_dispatch_c050` | 1 | 50 | 50 | 50 | 50 | 3 | 53 | 158069 | 5828 | 150 | 0 | 100 | 2 |
| `w8_traits_dispatch_c100` | 1 | 100 | 100 | 100 | 100 | 3 | 103 | 315069 | 11528 | 300 | 0 | 200 | 2 |

Full per-fixture, per-checker records: `phase26_work_counters.csv`.
