# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.42236328125, 0.44775390625, 0.28564453125)`; after profiling `(1.00439453125, 0.908203125, 0.58740234375)`.
- Candidate benchmark checkout SHA: `6817b59ab37711fd4e1ec8c942e07652a25d5b23`
- Candidate compiler source SHA: `bdf973af651deb2d3a4deac2b6c4bcc60893a776`
- Candidate binary SHA256: `82027513646fc4339147a6cbefe0dda0332b92d4711564fc7c236f0f16dd2caf`
- Certification mode: `True`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Baseline work counters are sparse because the pre-Phase 26 revision does not emit the current full set of structured counters; absent baseline counters mean unavailable, not zero.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `c66fb76c` resolved to `c66fb76ca0012b8125d70d404adbff13daa1cf4e`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 97.078 s | 14.877 s | -84.7% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 10.71 ms | 3.54 ms | -66.9% | 10.00 ms | 0.00 ms | 9320 KiB | 9616 KiB |
| `rw_packages_regex` | 20234.58 ms | 1477.17 ms | -92.7% | 20220.00 ms | 1470.00 ms | 25352 KiB | 28100 KiB |
| `rw_projects_rolling_window` | 149.36 ms | 94.70 ms | -36.6% | 140.00 ms | 90.00 ms | 14360 KiB | 15380 KiB |
| `rw_sdk_datastructures` | 22228.29 ms | 552.00 ms | -97.5% | 22210.00 ms | 550.00 ms | 21528 KiB | 23504 KiB |
| `w2_spec_distinct_n032` | 3446.13 ms | 1925.03 ms | -44.1% | 3430.00 ms | 1920.00 ms | 26464 KiB | 34396 KiB |
| `w3_spec_repeated_n032` | 2434.48 ms | 986.68 ms | -59.5% | 2430.00 ms | 980.00 ms | 22144 KiB | 22212 KiB |
| `w4_infer_deep_d018` | 26688.92 ms | 7248.72 ms | -72.8% | 26680.00 ms | 7240.00 ms | 10168 KiB | 10452 KiB |
| `w5_infer_wide_w100` | 11280.27 ms | 126.74 ms | -98.9% | 11270.00 ms | 120.00 ms | 13508 KiB | 16272 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 10.71 ms | 3.54 ms | -66.9% | 10.00 ms | 0.00 ms | 9320 KiB | 9616 KiB |
| `example_collections` | 5.68 ms | 3.33 ms | -41.4% | 0.00 ms | 0.00 ms | 9644 KiB | 9828 KiB |
| `example_counter` | 2.41 ms | 1.79 ms | -25.9% | 0.00 ms | 0.00 ms | 9164 KiB | 9232 KiB |
| `example_domain_typing` | 9.05 ms | 3.95 ms | -56.4% | 0.00 ms | 0.00 ms | 9556 KiB | 9652 KiB |
| `example_domains_and_messages` | 2.95 ms | 2.08 ms | -29.6% | 0.00 ms | 0.00 ms | 9128 KiB | 9268 KiB |
| `example_functional_basics` | 4.33 ms | 2.91 ms | -32.8% | 0.00 ms | 0.00 ms | 9172 KiB | 9348 KiB |
| `example_functional_dataflow` | 6.63 ms | 3.26 ms | -50.8% | 0.00 ms | 0.00 ms | 9380 KiB | 9644 KiB |
| `example_functional_pipeline` | 2.27 ms | 1.56 ms | -31.2% | 0.00 ms | 0.00 ms | 9104 KiB | 9132 KiB |
| `example_shared_memory` | 3.90 ms | 2.31 ms | -40.7% | 0.00 ms | 0.00 ms | 9176 KiB | 9476 KiB |
| `example_static_duck_typing` | 3.56 ms | 2.83 ms | -20.5% | 0.00 ms | 0.00 ms | 9564 KiB | 9788 KiB |
| `mg1_multi_file` | 32.82 ms | 6.90 ms | -79.0% | 20.00 ms | 0.00 ms | 10700 KiB | 10856 KiB |
| `mg2_multi_module` | 26.45 ms | 6.25 ms | -76.4% | 20.00 ms | 0.00 ms | 10440 KiB | 10816 KiB |
| `mg3_deep_chain` | 16.24 ms | 4.96 ms | -69.5% | 10.00 ms | 0.00 ms | 10380 KiB | 10392 KiB |
| `mg4_fan_out` | 60.45 ms | 9.90 ms | -83.6% | 50.00 ms | 0.00 ms | 10428 KiB | 10812 KiB |
| `mg5_diamond` | 9.78 ms | 4.14 ms | -57.6% | 0.00 ms | 0.00 ms | 10252 KiB | 10416 KiB |
| `negative_await_retired` | 1.16 ms | 1.10 ms | -5.1% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_domain_route_cycle` | 1.91 ms | 1.30 ms | -32.0% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_duck_missing_method` | 2.41 ms | 1.81 ms | -25.0% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_heterogeneous_collection` | 1.52 ms | 1.37 ms | -9.8% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_mutually_recursive_functions` | 2.31 ms | 1.42 ms | -38.5% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_self_send_rejected` | 1.76 ms | 1.35 ms | -23.5% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_spawn_retired` | 1.57 ms | 1.20 ms | -23.4% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_trait_missing_method` | 2.74 ms | 2.09 ms | -23.9% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `negative_write_write_alias` | 2.44 ms | 2.02 ms | -17.2% | 0.00 ms | 0.00 ms | 0 KiB | 0 KiB |
| `rw_example_checkout` | 10.31 ms | 3.71 ms | -64.0% | 10.00 ms | 0.00 ms | 9148 KiB | 9544 KiB |
| `rw_example_collections` | 5.09 ms | 3.24 ms | -36.4% | 0.00 ms | 0.00 ms | 9640 KiB | 9788 KiB |
| `rw_example_counter` | 2.46 ms | 1.79 ms | -27.2% | 0.00 ms | 0.00 ms | 9136 KiB | 9120 KiB |
| `rw_example_domains` | 2.83 ms | 1.90 ms | -32.9% | 0.00 ms | 0.00 ms | 9132 KiB | 9424 KiB |
| `rw_example_pipeline` | 2.26 ms | 1.87 ms | -17.4% | 0.00 ms | 0.00 ms | 9104 KiB | 9300 KiB |
| `rw_packages_regex` | 20234.58 ms | 1477.17 ms | -92.7% | 20220.00 ms | 1470.00 ms | 25352 KiB | 28100 KiB |
| `rw_phase106d1_borrowed_reads` | 44.59 ms | 12.97 ms | -70.9% | 40.00 ms | 10.00 ms | 9584 KiB | 10084 KiB |
| `rw_phase106d1_read_projection` | 37.29 ms | 11.29 ms | -69.7% | 30.00 ms | 10.00 ms | 9800 KiB | 10220 KiB |
| `rw_phase106d_handler_2pl` | 28.14 ms | 7.96 ms | -71.7% | 20.00 ms | 0.00 ms | 9444 KiB | 9644 KiB |
| `rw_phase15_15_enum_positive` | 20.03 ms | 6.46 ms | -67.7% | 10.00 ms | 0.00 ms | 9300 KiB | 9688 KiB |
| `rw_phase20_fileio_helper` | 3.17 ms | 2.37 ms | -25.3% | 0.00 ms | 0.00 ms | 9040 KiB | 9516 KiB |
| `rw_phase21_fileio_root_cleanup` | 4.01 ms | 2.92 ms | -27.1% | 0.00 ms | 0.00 ms | 9248 KiB | 9516 KiB |
| `rw_phase24_string_concat` | 22.68 ms | 5.61 ms | -75.3% | 20.00 ms | 0.00 ms | 9424 KiB | 9856 KiB |
| `rw_phase25_integer_overflow` | 15.97 ms | 5.69 ms | -64.4% | 10.00 ms | 0.00 ms | 9536 KiB | 9780 KiB |
| `rw_phase46_semantic_barriers` | 24.39 ms | 6.71 ms | -72.5% | 20.00 ms | 0.00 ms | 9436 KiB | 9528 KiB |
| `rw_phase47_iteration` | 20.02 ms | 8.23 ms | -58.9% | 10.00 ms | 0.00 ms | 9532 KiB | 9768 KiB |
| `rw_phase4_functional` | 42.43 ms | 12.52 ms | -70.5% | 40.00 ms | 0.00 ms | 9656 KiB | 10320 KiB |
| `rw_projects_cache` | 80.93 ms | 60.10 ms | -25.7% | 80.00 ms | 50.00 ms | 12640 KiB | 13316 KiB |
| `rw_projects_rolling_window` | 149.36 ms | 94.70 ms | -36.6% | 140.00 ms | 90.00 ms | 14360 KiB | 15380 KiB |
| `rw_sdk_datastructures` | 22228.29 ms | 552.00 ms | -97.5% | 22210.00 ms | 550.00 ms | 21528 KiB | 23504 KiB |
| `rw_swarm_047_reduce_map` | 30.35 ms | 8.39 ms | -72.4% | 20.00 ms | 0.00 ms | 9536 KiB | 9660 KiB |
| `rw_swarm_066_for_range` | 30.34 ms | 8.60 ms | -71.6% | 30.00 ms | 0.00 ms | 9396 KiB | 9788 KiB |
| `rw_tests_branch_confined_open` | 4.09 ms | 2.83 ms | -30.8% | 0.00 ms | 0.00 ms | 9440 KiB | 9428 KiB |
| `test_phase45_short_circuit_failure_barrier` | 3.08 ms | 2.27 ms | -26.1% | 0.00 ms | 0.00 ms | 9196 KiB | 9256 KiB |
| `test_trait_open_forwarding` | 3.85 ms | 2.84 ms | -26.2% | 0.00 ms | 0.00 ms | 10312 KiB | 10352 KiB |
| `test_unused_inferred_field_template` | 1.34 ms | 1.35 ms | +1.2% | 0.00 ms | 0.00 ms | 9184 KiB | 9092 KiB |
| `w2_spec_distinct_n008` | 172.08 ms | 94.91 ms | -44.8% | 160.00 ms | 90.00 ms | 10620 KiB | 11540 KiB |
| `w2_spec_distinct_n016` | 724.64 ms | 399.43 ms | -44.9% | 720.00 ms | 390.00 ms | 13652 KiB | 17016 KiB |
| `w2_spec_distinct_n032` | 3446.13 ms | 1925.03 ms | -44.1% | 3430.00 ms | 1920.00 ms | 26464 KiB | 34396 KiB |
| `w3_spec_repeated_n008` | 141.10 ms | 70.19 ms | -50.3% | 130.00 ms | 60.00 ms | 10240 KiB | 10656 KiB |
| `w3_spec_repeated_n016` | 570.19 ms | 263.57 ms | -53.8% | 560.00 ms | 250.00 ms | 12448 KiB | 12816 KiB |
| `w3_spec_repeated_n032` | 2434.48 ms | 986.68 ms | -59.5% | 2430.00 ms | 980.00 ms | 22144 KiB | 22212 KiB |
| `w4_infer_deep_d005` | 18.30 ms | 7.27 ms | -60.3% | 10.00 ms | 0.00 ms | 9800 KiB | 9788 KiB |
| `w4_infer_deep_d010` | 147.04 ms | 39.63 ms | -73.0% | 140.00 ms | 30.00 ms | 9888 KiB | 10180 KiB |
| `w4_infer_deep_d015` | 3367.54 ms | 906.29 ms | -73.1% | 3360.00 ms | 900.00 ms | 10372 KiB | 10412 KiB |
| `w4_infer_deep_d018` | 26688.92 ms | 7248.72 ms | -72.8% | 26680.00 ms | 7240.00 ms | 10168 KiB | 10452 KiB |
| `w5_infer_wide_w010` | 131.08 ms | 14.57 ms | -88.9% | 130.00 ms | 10.00 ms | 9928 KiB | 10344 KiB |
| `w5_infer_wide_w025` | 738.98 ms | 35.34 ms | -95.2% | 730.00 ms | 30.00 ms | 10292 KiB | 11248 KiB |
| `w5_infer_wide_w050` | 2836.26 ms | 66.48 ms | -97.7% | 2830.00 ms | 60.00 ms | 11468 KiB | 13020 KiB |
| `w5_infer_wide_w100` | 11280.27 ms | 126.74 ms | -98.9% | 11270.00 ms | 120.00 ms | 13508 KiB | 16272 KiB |
| `w6_types_count_t010` | 15.30 ms | 7.35 ms | -52.0% | 10.00 ms | 0.00 ms | 9384 KiB | 9996 KiB |
| `w6_types_count_t025` | 43.63 ms | 17.35 ms | -60.2% | 40.00 ms | 10.00 ms | 10036 KiB | 10988 KiB |
| `w6_types_count_t050` | 123.76 ms | 38.90 ms | -68.6% | 110.00 ms | 30.00 ms | 11464 KiB | 12896 KiB |
| `w6_types_count_t100` | 387.69 ms | 91.84 ms | -76.3% | 380.00 ms | 80.00 ms | 15744 KiB | 18388 KiB |
| `w7_collections_nested_k1` | 1.85 ms | 1.64 ms | -11.3% | 0.00 ms | 0.00 ms | 9024 KiB | 8976 KiB |
| `w7_collections_nested_k2` | 2.27 ms | 1.80 ms | -20.6% | 0.00 ms | 0.00 ms | 8940 KiB | 9120 KiB |
| `w7_collections_nested_k3` | 3.00 ms | 2.30 ms | -23.4% | 0.00 ms | 0.00 ms | 9052 KiB | 9284 KiB |
| `w7_collections_nested_k4` | 3.99 ms | 2.84 ms | -29.0% | 0.00 ms | 0.00 ms | 9020 KiB | 9244 KiB |
| `w7_collections_nested_k5` | 5.18 ms | 3.52 ms | -32.0% | 0.00 ms | 0.00 ms | 9280 KiB | 9168 KiB |
| `w8_traits_dispatch_c010` | 20.33 ms | 10.26 ms | -49.5% | 10.00 ms | 0.00 ms | 9788 KiB | 10132 KiB |
| `w8_traits_dispatch_c025` | 53.30 ms | 19.99 ms | -62.5% | 50.00 ms | 10.00 ms | 10272 KiB | 10812 KiB |
| `w8_traits_dispatch_c050` | 121.33 ms | 38.02 ms | -68.7% | 120.00 ms | 30.00 ms | 10856 KiB | 12120 KiB |
| `w8_traits_dispatch_c100` | 332.22 ms | 77.14 ms | -76.8% | 320.00 ms | 70.00 ms | 12104 KiB | 14568 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 149.36 ms | 94.70 ms | -36.6% | 140.00 ms | 90.00 ms | 14360 KiB | 15380 KiB |
| `example_checkout` | 10.71 ms | 3.54 ms | -66.9% | 10.00 ms | 0.00 ms | 9320 KiB | 9616 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

None.

## Above 5% but below the noise floor

None.

## Profiling work counters

One separate profiling sample per fixture. This table shows candidate totals. Counter totals aggregate across checker instances; the CSV includes baseline and candidate plus each checker's individual records.

| Fixture | functions_analyzed | types_analyzed | specialization_requests | distinct_specialization_keys | checked_specializations | inference_rounds | inference_work_units | parse_cache_hits | parse_cache_misses | concrete_effect_evaluations | specialization_effect_cache_hits | specialization_effect_computations | effects_fixed_point_rounds |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 0 | 3 | 0 | 0 | 0 | 5 | 15 | 7630 | 2804 | 0 | 0 | 0 | 2 |
| `example_collections` | 1 | 0 | 1 | 1 | 1 | 5 | 5 | 6196 | 2019 | 3 | 0 | 2 | 2 |
| `example_counter` | 0 | 1 | 0 | 0 | 0 | 5 | 5 | 2172 | 841 | 0 | 0 | 0 | 2 |
| `example_domain_typing` | 2 | 1 | 3 | 1 | 1 | 5 | 15 | 10249 | 2698 | 17 | 0 | 2 | 2 |
| `example_domains_and_messages` | 0 | 1 | 0 | 0 | 0 | 5 | 5 | 2455 | 944 | 0 | 0 | 0 | 2 |
| `example_functional_basics` | 1 | 0 | 1 | 0 | 0 | 5 | 5 | 5308 | 2239 | 3 | 0 | 0 | 1 |
| `example_functional_dataflow` | 3 | 0 | 3 | 1 | 1 | 5 | 15 | 6792 | 2239 | 27 | 4 | 6 | 1 |
| `example_functional_pipeline` | 1 | 0 | 2 | 0 | 0 | 5 | 5 | 1714 | 520 | 2 | 0 | 0 | 1 |
| `example_shared_memory` | 0 | 2 | 0 | 0 | 0 | 5 | 10 | 3160 | 1201 | 0 | 0 | 0 | 2 |
| `example_static_duck_typing` | 1 | 2 | 2 | 2 | 2 | 5 | 7 | 4154 | 1053 | 6 | 0 | 4 | 2 |
| `mg1_multi_file` | 10 | 0 | 10 | 0 | 0 | 5 | 50 | 21043 | 4798 | 10 | 0 | 0 | 1 |
| `mg2_multi_module` | 8 | 8 | 8 | 0 | 0 | 5 | 48 | 20353 | 4558 | 8 | 0 | 0 | 1 |
| `mg3_deep_chain` | 8 | 0 | 8 | 0 | 0 | 5 | 40 | 16705 | 3883 | 22 | 0 | 0 | 1 |
| `mg4_fan_out` | 9 | 1 | 16 | 0 | 0 | 5 | 46 | 35481 | 7026 | 32 | 0 | 0 | 1 |
| `mg5_diamond` | 3 | 1 | 4 | 0 | 0 | 5 | 16 | 10439 | 2236 | 8 | 0 | 0 | 1 |
| `negative_await_retired` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_domain_route_cycle` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_duck_missing_method` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_heterogeneous_collection` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_mutually_recursive_functions` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_self_send_rejected` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_spawn_retired` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_trait_missing_method` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `negative_write_write_alias` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `rw_example_checkout` | 0 | 3 | 0 | 0 | 0 | 5 | 15 | 7630 | 2804 | 0 | 0 | 0 | 2 |
| `rw_example_collections` | 1 | 0 | 1 | 1 | 1 | 5 | 5 | 6196 | 2019 | 3 | 0 | 2 | 2 |
| `rw_example_counter` | 0 | 1 | 0 | 0 | 0 | 5 | 5 | 2172 | 841 | 0 | 0 | 0 | 2 |
| `rw_example_domains` | 0 | 1 | 0 | 0 | 0 | 5 | 5 | 2455 | 944 | 0 | 0 | 0 | 2 |
| `rw_example_pipeline` | 1 | 0 | 2 | 0 | 0 | 5 | 5 | 1714 | 520 | 2 | 0 | 0 | 1 |
| `rw_packages_regex` | 98 | 8 | 226 | 0 | 0 | 15 | 741 | 4356244 | 1081378 | 1130 | 0 | 0 | 4 |
| `rw_phase106d1_borrowed_reads` | 2 | 4 | 5 | 0 | 0 | 5 | 22 | 38886 | 10038 | 35 | 0 | 0 | 2 |
| `rw_phase106d1_read_projection` | 4 | 3 | 5 | 1 | 1 | 5 | 27 | 27243 | 6720 | 38 | 0 | 2 | 2 |
| `rw_phase106d_handler_2pl` | 2 | 4 | 7 | 0 | 0 | 5 | 26 | 26740 | 7443 | 43 | 0 | 0 | 2 |
| `rw_phase15_15_enum_positive` | 4 | 2 | 9 | 0 | 0 | 5 | 21 | 13171 | 4599 | 13 | 0 | 0 | 2 |
| `rw_phase20_fileio_helper` | 1 | 1 | 1 | 0 | 0 | 5 | 5 | 4299 | 1148 | 3 | 0 | 0 | 2 |
| `rw_phase21_fileio_root_cleanup` | 0 | 2 | 0 | 0 | 0 | 5 | 5 | 6172 | 2323 | 0 | 0 | 0 | 2 |
| `rw_phase24_string_concat` | 6 | 0 | 8 | 0 | 0 | 5 | 30 | 17394 | 5505 | 7 | 0 | 0 | 2 |
| `rw_phase25_integer_overflow` | 1 | 4 | 2 | 2 | 2 | 5 | 25 | 16177 | 5367 | 6 | 0 | 4 | 2 |
| `rw_phase46_semantic_barriers` | 4 | 0 | 11 | 0 | 0 | 5 | 20 | 14500 | 5861 | 33 | 0 | 0 | 2 |
| `rw_phase47_iteration` | 3 | 1 | 5 | 3 | 3 | 5 | 16 | 29606 | 10708 | 11 | 0 | 6 | 2 |
| `rw_phase4_functional` | 3 | 0 | 9 | 1 | 1 | 5 | 15 | 27766 | 10488 | 32 | 2 | 4 | 2 |
| `rw_projects_cache` | 2 | 4 | 24 | 0 | 0 | 15 | 33 | 223120 | 54226 | 24 | 0 | 0 | 4 |
| `rw_projects_rolling_window` | 2 | 4 | 40 | 0 | 0 | 15 | 33 | 339290 | 80492 | 44 | 0 | 0 | 4 |
| `rw_sdk_datastructures` | 224 | 22 | 294 | 0 | 0 | 15 | 1710 | 2257224 | 586742 | 2020 | 0 | 0 | 6 |
| `rw_swarm_047_reduce_map` | 4 | 0 | 4 | 0 | 0 | 5 | 20 | 27250 | 6706 | 14 | 0 | 0 | 2 |
| `rw_swarm_066_for_range` | 7 | 0 | 7 | 0 | 0 | 5 | 35 | 40320 | 12798 | 7 | 0 | 0 | 2 |
| `rw_tests_branch_confined_open` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 4948 | 2269 | 0 | 0 | 0 | 1 |
| `test_phase45_short_circuit_failure_barrier` | 1 | 0 | 1 | 0 | 0 | 5 | 5 | 3751 | 1351 | 3 | 0 | 0 | 2 |
| `test_trait_open_forwarding` | 2 | 2 | 2 | 2 | 2 | 5 | 11 | 4909 | 1253 | 16 | 4 | 4 | 2 |
| `test_unused_inferred_field_template` | 0 | 1 | 0 | 0 | 0 | 5 | 1 | 296 | 201 | 0 | 0 | 0 | 1 |
| `w2_spec_distinct_n008` | 2 | 8 | 192 | 72 | 72 | 5 | 18 | 356113 | 49333 | 986 | 512 | 144 | 2 |
| `w2_spec_distinct_n016` | 2 | 16 | 768 | 272 | 272 | 5 | 26 | 1384673 | 185621 | 3882 | 2048 | 544 | 2 |
| `w2_spec_distinct_n032` | 2 | 32 | 3072 | 1056 | 1056 | 5 | 42 | 5463530 | 747756 | 15434 | 8192 | 2112 | 2 |
| `w3_spec_repeated_n008` | 2 | 8 | 72 | 6 | 6 | 5 | 18 | 286879 | 38377 | 374 | 32 | 12 | 2 |
| `w3_spec_repeated_n016` | 2 | 16 | 264 | 6 | 6 | 5 | 26 | 1096791 | 140457 | 1334 | 32 | 12 | 2 |
| `w3_spec_repeated_n032` | 2 | 32 | 1032 | 6 | 6 | 5 | 42 | 4326343 | 545833 | 5174 | 32 | 12 | 2 |
| `w4_infer_deep_d005` | 6 | 0 | 6 | 6 | 6 | 5 | 30 | 34869 | 8820 | 110 | 20 | 12 | 1 |
| `w4_infer_deep_d010` | 11 | 0 | 11 | 11 | 11 | 5 | 55 | 357191 | 86301 | 2139 | 40 | 22 | 1 |
| `w4_infer_deep_d015` | 16 | 0 | 16 | 16 | 16 | 5 | 80 | 9489435 | 2258943 | 65672 | 60 | 32 | 1 |
| `w4_infer_deep_d018` | 19 | 0 | 19 | 19 | 19 | 5 | 95 | 75130175 | 17866873 | 524451 | 72 | 38 | 1 |
| `w5_infer_wide_w010` | 20 | 0 | 20 | 20 | 20 | 5 | 100 | 76927 | 19189 | 140 | 40 | 40 | 1 |
| `w5_infer_wide_w025` | 50 | 0 | 50 | 50 | 50 | 5 | 250 | 191962 | 47644 | 350 | 100 | 100 | 1 |
| `w5_infer_wide_w050` | 100 | 0 | 100 | 100 | 100 | 5 | 500 | 383687 | 95069 | 700 | 200 | 200 | 1 |
| `w5_infer_wide_w100` | 200 | 0 | 200 | 200 | 200 | 5 | 1000 | 767137 | 189919 | 1400 | 400 | 400 | 1 |
| `w6_types_count_t010` | 0 | 10 | 0 | 0 | 0 | 5 | 10 | 24550 | 5261 | 0 | 0 | 0 | 1 |
| `w6_types_count_t025` | 0 | 25 | 0 | 0 | 0 | 5 | 25 | 61120 | 12761 | 0 | 0 | 0 | 1 |
| `w6_types_count_t050` | 0 | 50 | 0 | 0 | 0 | 5 | 50 | 122070 | 25261 | 0 | 0 | 0 | 1 |
| `w6_types_count_t100` | 0 | 100 | 0 | 0 | 0 | 5 | 100 | 243970 | 50261 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k1` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 1213 | 652 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k2` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 1894 | 1032 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k3` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 2581 | 1413 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k4` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 3274 | 1794 | 0 | 0 | 0 | 1 |
| `w7_collections_nested_k5` | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 3973 | 2175 | 0 | 0 | 0 | 1 |
| `w8_traits_dispatch_c010` | 1 | 10 | 10 | 10 | 10 | 5 | 15 | 30261 | 5124 | 30 | 0 | 20 | 2 |
| `w8_traits_dispatch_c025` | 1 | 25 | 25 | 25 | 25 | 5 | 30 | 74061 | 12234 | 75 | 0 | 50 | 2 |
| `w8_traits_dispatch_c050` | 1 | 50 | 50 | 50 | 50 | 5 | 55 | 147061 | 24084 | 150 | 0 | 100 | 2 |
| `w8_traits_dispatch_c100` | 1 | 100 | 100 | 100 | 100 | 5 | 105 | 293061 | 47784 | 300 | 0 | 200 | 2 |

Full per-fixture, per-checker records: `phase26_work_counters.csv`.
