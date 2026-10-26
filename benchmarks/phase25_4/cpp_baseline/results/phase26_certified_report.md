# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.40576171875, 0.45947265625, 0.291015625)`; after profiling `(1.00537109375, 0.9296875, 0.607421875)`.
- Candidate benchmark checkout SHA: `6deefa9677db4feb3f85de9ad389c5e6863412e4`
- Candidate compiler source SHA: `bdf973af651deb2d3a4deac2b6c4bcc60893a776`
- Candidate binary SHA256: `82027513646fc4339147a6cbefe0dda0332b92d4711564fc7c236f0f16dd2caf`
- Certification mode: `True`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `c66fb76c` resolved to `c66fb76ca0012b8125d70d404adbff13daa1cf4e`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 98.437 s | 15.071 s | -84.7% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 10.11 ms | 3.50 ms | -65.4% | 0.00 ms | 0.00 ms | 9296 KiB | 9464 KiB |
| `rw_packages_regex` | 20454.59 ms | 1513.26 ms | -92.6% | 20430.00 ms | 1500.00 ms | 25180 KiB | 28028 KiB |
| `rw_projects_rolling_window` | 151.50 ms | 97.01 ms | -36.0% | 150.00 ms | 90.00 ms | 14364 KiB | 15400 KiB |
| `rw_sdk_datastructures` | 22671.08 ms | 574.11 ms | -97.5% | 22660.00 ms | 560.00 ms | 21748 KiB | 23300 KiB |
| `w2_spec_distinct_n032` | 3533.22 ms | 1967.25 ms | -44.3% | 3520.00 ms | 1950.00 ms | 26524 KiB | 34428 KiB |
| `w3_spec_repeated_n032` | 2483.17 ms | 1009.88 ms | -59.3% | 2470.00 ms | 1000.00 ms | 22140 KiB | 22156 KiB |
| `w4_infer_deep_d018` | 26854.97 ms | 7290.55 ms | -72.9% | 26840.00 ms | 7280.00 ms | 10284 KiB | 10568 KiB |
| `w5_infer_wide_w100` | 11404.22 ms | 126.78 ms | -98.9% | 11390.00 ms | 120.00 ms | 13408 KiB | 16356 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 10.11 ms | 3.50 ms | -65.4% | 0.00 ms | 0.00 ms | 9296 KiB | 9464 KiB |
| `example_collections` | 5.10 ms | 3.22 ms | -37.0% | 0.00 ms | 0.00 ms | 9716 KiB | 9740 KiB |
| `example_counter` | 2.41 ms | 1.76 ms | -27.0% | 0.00 ms | 0.00 ms | 9164 KiB | 9012 KiB |
| `example_domain_typing` | 10.02 ms | 4.52 ms | -54.9% | 0.00 ms | 0.00 ms | 9548 KiB | 9712 KiB |
| `example_domains_and_messages` | 2.73 ms | 1.96 ms | -28.1% | 0.00 ms | 0.00 ms | 9160 KiB | 9404 KiB |
| `example_functional_basics` | 4.22 ms | 3.18 ms | -24.7% | 0.00 ms | 0.00 ms | 9152 KiB | 9344 KiB |
| `example_functional_dataflow` | 6.27 ms | 2.97 ms | -52.6% | 0.00 ms | 0.00 ms | 9264 KiB | 9564 KiB |
| `example_functional_pipeline` | 2.30 ms | 1.69 ms | -26.7% | 0.00 ms | 0.00 ms | 9092 KiB | 9264 KiB |
| `example_shared_memory` | 3.72 ms | 2.30 ms | -38.2% | 0.00 ms | 0.00 ms | 9060 KiB | 9432 KiB |
| `example_static_duck_typing` | 3.22 ms | 2.50 ms | -22.2% | 0.00 ms | 0.00 ms | 9516 KiB | 9712 KiB |
| `mg1_multi_file` | 32.48 ms | 6.17 ms | -81.0% | 30.00 ms | 0.00 ms | 10568 KiB | 10976 KiB |
| `mg2_multi_module` | 27.34 ms | 5.94 ms | -78.3% | 20.00 ms | 0.00 ms | 10308 KiB | 10692 KiB |
| `mg3_deep_chain` | 15.71 ms | 4.47 ms | -71.5% | 10.00 ms | 0.00 ms | 10352 KiB | 10488 KiB |
| `mg4_fan_out` | 59.27 ms | 9.51 ms | -84.0% | 50.00 ms | 0.00 ms | 10340 KiB | 10920 KiB |
| `mg5_diamond` | 9.33 ms | 3.95 ms | -57.6% | 0.00 ms | 0.00 ms | 10256 KiB | 10476 KiB |
| `negative_await_retired` | 1.13 ms | 1.11 ms | -2.1% | 0.00 ms | 0.00 ms | 6844 KiB | 6864 KiB |
| `negative_domain_route_cycle` | 1.92 ms | 1.34 ms | -30.2% | 0.00 ms | 0.00 ms | 8920 KiB | 9276 KiB |
| `negative_duck_missing_method` | 2.32 ms | 1.75 ms | -24.8% | 0.00 ms | 0.00 ms | 9800 KiB | 9936 KiB |
| `negative_heterogeneous_collection` | 1.42 ms | 1.31 ms | -7.7% | 0.00 ms | 0.00 ms | 9324 KiB | 9560 KiB |
| `negative_mutually_recursive_functions` | 1.90 ms | 1.38 ms | -27.3% | 0.00 ms | 0.00 ms | 8916 KiB | 9064 KiB |
| `negative_self_send_rejected` | 1.51 ms | 1.37 ms | -9.3% | 0.00 ms | 0.00 ms | 9256 KiB | 9420 KiB |
| `negative_spawn_retired` | 1.40 ms | 1.18 ms | -16.0% | 0.00 ms | 0.00 ms | 8904 KiB | 8936 KiB |
| `negative_trait_missing_method` | 2.57 ms | 1.99 ms | -22.7% | 0.00 ms | 0.00 ms | 9636 KiB | 9724 KiB |
| `negative_write_write_alias` | 2.48 ms | 1.89 ms | -23.8% | 0.00 ms | 0.00 ms | 9676 KiB | 9884 KiB |
| `rw_example_checkout` | 10.46 ms | 3.67 ms | -64.9% | 10.00 ms | 0.00 ms | 9252 KiB | 9572 KiB |
| `rw_example_collections` | 5.15 ms | 3.25 ms | -36.9% | 0.00 ms | 0.00 ms | 9500 KiB | 9616 KiB |
| `rw_example_counter` | 2.37 ms | 1.72 ms | -27.3% | 0.00 ms | 0.00 ms | 9192 KiB | 9448 KiB |
| `rw_example_domains` | 2.67 ms | 1.90 ms | -29.1% | 0.00 ms | 0.00 ms | 9132 KiB | 9420 KiB |
| `rw_example_pipeline` | 2.27 ms | 1.67 ms | -26.4% | 0.00 ms | 0.00 ms | 8992 KiB | 9172 KiB |
| `rw_packages_regex` | 20454.59 ms | 1513.26 ms | -92.6% | 20430.00 ms | 1500.00 ms | 25180 KiB | 28028 KiB |
| `rw_phase106d1_borrowed_reads` | 44.46 ms | 12.47 ms | -72.0% | 40.00 ms | 10.00 ms | 9596 KiB | 10124 KiB |
| `rw_phase106d1_read_projection` | 35.31 ms | 10.82 ms | -69.4% | 30.00 ms | 10.00 ms | 9920 KiB | 10068 KiB |
| `rw_phase106d_handler_2pl` | 28.20 ms | 7.56 ms | -73.2% | 20.00 ms | 0.00 ms | 9412 KiB | 9792 KiB |
| `rw_phase15_15_enum_positive` | 21.33 ms | 5.83 ms | -72.6% | 10.00 ms | 0.00 ms | 9488 KiB | 9660 KiB |
| `rw_phase20_fileio_helper` | 3.00 ms | 2.33 ms | -22.4% | 0.00 ms | 0.00 ms | 8976 KiB | 9352 KiB |
| `rw_phase21_fileio_root_cleanup` | 5.46 ms | 2.91 ms | -46.8% | 0.00 ms | 0.00 ms | 9232 KiB | 9564 KiB |
| `rw_phase24_string_concat` | 20.93 ms | 5.86 ms | -72.0% | 20.00 ms | 0.00 ms | 9332 KiB | 9792 KiB |
| `rw_phase25_integer_overflow` | 16.69 ms | 5.40 ms | -67.6% | 10.00 ms | 0.00 ms | 9544 KiB | 9796 KiB |
| `rw_phase46_semantic_barriers` | 23.52 ms | 6.19 ms | -73.7% | 20.00 ms | 0.00 ms | 9584 KiB | 9668 KiB |
| `rw_phase47_iteration` | 19.78 ms | 8.23 ms | -58.4% | 10.00 ms | 0.00 ms | 9432 KiB | 9744 KiB |
| `rw_phase4_functional` | 42.40 ms | 13.03 ms | -69.3% | 40.00 ms | 10.00 ms | 9728 KiB | 10128 KiB |
| `rw_projects_cache` | 81.22 ms | 55.45 ms | -31.7% | 70.00 ms | 50.00 ms | 12696 KiB | 13508 KiB |
| `rw_projects_rolling_window` | 151.50 ms | 97.01 ms | -36.0% | 150.00 ms | 90.00 ms | 14364 KiB | 15400 KiB |
| `rw_sdk_datastructures` | 22671.08 ms | 574.11 ms | -97.5% | 22660.00 ms | 560.00 ms | 21748 KiB | 23300 KiB |
| `rw_swarm_047_reduce_map` | 30.34 ms | 8.53 ms | -71.9% | 20.00 ms | 0.00 ms | 9712 KiB | 9620 KiB |
| `rw_swarm_066_for_range` | 31.69 ms | 8.40 ms | -73.5% | 30.00 ms | 0.00 ms | 9424 KiB | 9852 KiB |
| `rw_tests_branch_confined_open` | 3.58 ms | 2.97 ms | -16.9% | 0.00 ms | 0.00 ms | 9408 KiB | 9168 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.99 ms | 2.09 ms | -30.0% | 0.00 ms | 0.00 ms | 9060 KiB | 9212 KiB |
| `test_trait_open_forwarding` | 3.59 ms | 2.75 ms | -23.5% | 0.00 ms | 0.00 ms | 10356 KiB | 10408 KiB |
| `test_unused_inferred_field_template` | 1.30 ms | 1.28 ms | -1.9% | 0.00 ms | 0.00 ms | 8860 KiB | 8936 KiB |
| `w2_spec_distinct_n008` | 171.29 ms | 97.75 ms | -42.9% | 160.00 ms | 90.00 ms | 10560 KiB | 11520 KiB |
| `w2_spec_distinct_n016` | 744.29 ms | 408.88 ms | -45.1% | 740.00 ms | 400.00 ms | 13708 KiB | 17000 KiB |
| `w2_spec_distinct_n032` | 3533.22 ms | 1967.25 ms | -44.3% | 3520.00 ms | 1950.00 ms | 26524 KiB | 34428 KiB |
| `w3_spec_repeated_n008` | 142.12 ms | 70.85 ms | -50.1% | 140.00 ms | 70.00 ms | 10568 KiB | 10468 KiB |
| `w3_spec_repeated_n016` | 568.81 ms | 260.53 ms | -54.2% | 560.00 ms | 260.00 ms | 12528 KiB | 12692 KiB |
| `w3_spec_repeated_n032` | 2483.17 ms | 1009.88 ms | -59.3% | 2470.00 ms | 1000.00 ms | 22140 KiB | 22156 KiB |
| `w4_infer_deep_d005` | 17.67 ms | 6.28 ms | -64.4% | 10.00 ms | 0.00 ms | 9668 KiB | 9792 KiB |
| `w4_infer_deep_d010` | 145.85 ms | 40.83 ms | -72.0% | 140.00 ms | 40.00 ms | 9948 KiB | 10268 KiB |
| `w4_infer_deep_d015` | 3522.83 ms | 920.14 ms | -73.9% | 3520.00 ms | 910.00 ms | 10036 KiB | 10440 KiB |
| `w4_infer_deep_d018` | 26854.97 ms | 7290.55 ms | -72.9% | 26840.00 ms | 7280.00 ms | 10284 KiB | 10568 KiB |
| `w5_infer_wide_w010` | 138.87 ms | 13.88 ms | -90.0% | 130.00 ms | 10.00 ms | 9932 KiB | 10100 KiB |
| `w5_infer_wide_w025` | 753.89 ms | 32.91 ms | -95.6% | 740.00 ms | 30.00 ms | 10612 KiB | 11288 KiB |
| `w5_infer_wide_w050` | 2882.79 ms | 64.23 ms | -97.8% | 2870.00 ms | 60.00 ms | 11532 KiB | 13060 KiB |
| `w5_infer_wide_w100` | 11404.22 ms | 126.78 ms | -98.9% | 11390.00 ms | 120.00 ms | 13408 KiB | 16356 KiB |
| `w6_types_count_t010` | 14.60 ms | 9.95 ms | -31.8% | 10.00 ms | 0.00 ms | 9420 KiB | 9948 KiB |
| `w6_types_count_t025` | 44.66 ms | 18.60 ms | -58.4% | 40.00 ms | 10.00 ms | 10176 KiB | 10824 KiB |
| `w6_types_count_t050` | 122.84 ms | 38.70 ms | -68.5% | 110.00 ms | 30.00 ms | 11460 KiB | 12812 KiB |
| `w6_types_count_t100` | 413.54 ms | 100.42 ms | -75.7% | 410.00 ms | 90.00 ms | 15664 KiB | 18516 KiB |
| `w7_collections_nested_k1` | 1.72 ms | 1.64 ms | -4.6% | 0.00 ms | 0.00 ms | 8952 KiB | 9204 KiB |
| `w7_collections_nested_k2` | 2.25 ms | 1.78 ms | -21.0% | 0.00 ms | 0.00 ms | 9160 KiB | 8976 KiB |
| `w7_collections_nested_k3` | 2.90 ms | 2.21 ms | -23.8% | 0.00 ms | 0.00 ms | 9008 KiB | 9336 KiB |
| `w7_collections_nested_k4` | 3.75 ms | 2.70 ms | -28.1% | 0.00 ms | 0.00 ms | 9204 KiB | 9004 KiB |
| `w7_collections_nested_k5` | 5.25 ms | 3.44 ms | -34.5% | 0.00 ms | 0.00 ms | 9152 KiB | 9348 KiB |
| `w8_traits_dispatch_c010` | 17.59 ms | 9.67 ms | -45.0% | 10.00 ms | 0.00 ms | 9928 KiB | 10092 KiB |
| `w8_traits_dispatch_c025` | 48.86 ms | 20.64 ms | -57.8% | 40.00 ms | 20.00 ms | 10172 KiB | 10756 KiB |
| `w8_traits_dispatch_c050` | 121.91 ms | 40.81 ms | -66.5% | 110.00 ms | 40.00 ms | 10816 KiB | 12040 KiB |
| `w8_traits_dispatch_c100` | 346.56 ms | 80.32 ms | -76.8% | 340.00 ms | 70.00 ms | 12156 KiB | 14492 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 151.50 ms | 97.01 ms | -36.0% | 150.00 ms | 90.00 ms | 14364 KiB | 15400 KiB |
| `example_checkout` | 10.11 ms | 3.50 ms | -65.4% | 0.00 ms | 0.00 ms | 9296 KiB | 9464 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

None.

## Above 5% but below the noise floor

None.

## Profiling work counters

One separate profiling sample per fixture. This table shows candidate totals. Counter totals aggregate across checker instances; the CSV includes baseline and candidate plus each checker's individual records. Expected-negative fixtures can fail before any checker counters are emitted; those entries are n/a.

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
| `negative_await_retired` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_domain_route_cycle` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_duck_missing_method` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_heterogeneous_collection` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_mutually_recursive_functions` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_self_send_rejected` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_spawn_retired` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_trait_missing_method` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| `negative_write_write_alias` | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
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
