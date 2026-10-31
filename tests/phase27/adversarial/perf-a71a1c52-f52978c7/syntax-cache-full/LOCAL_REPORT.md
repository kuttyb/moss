# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.876953125, 1.54736328125, 2.27880859375)`; after profiling `(1.44873046875, 1.47705078125, 2.2236328125)`.
- Candidate benchmark checkout SHA: `e35b193c4f8ffca14fd820f6c938f53e1c1a0744`
- Candidate compiler source SHA: `e35b193c4f8ffca14fd820f6c938f53e1c1a0744`
- Candidate binary SHA256: `c0b0e2c4109405a8bc37c575d8cfa496e2793cea2223141244d6150ea3431695`
- Certification mode: `False`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `origin/main` resolved to `a71a1c52fd7f6e99ba33d9dde70e3531a033def6`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.151 s | 5.204 s | +1.0% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.40 ms | 4.11 ms | +20.8% | 0.00 ms | 0.00 ms | 9928 KiB | 10492 KiB |
| `rw_packages_regex` | 637.32 ms | 652.39 ms | +2.4% | 630.00 ms | 640.00 ms | 25520 KiB | 30164 KiB |
| `rw_projects_rolling_window` | 48.04 ms | 50.27 ms | +4.6% | 40.00 ms | 40.00 ms | 14728 KiB | 16368 KiB |
| `rw_sdk_datastructures` | 227.49 ms | 253.62 ms | +11.5% | 220.00 ms | 250.00 ms | 19548 KiB | 22864 KiB |
| `w2_spec_distinct_n032` | 1816.19 ms | 1812.20 ms | -0.2% | 1800.00 ms | 1800.00 ms | 36920 KiB | 59344 KiB |
| `w3_spec_repeated_n032` | 879.19 ms | 850.06 ms | -3.3% | 870.00 ms | 840.00 ms | 24672 KiB | 27668 KiB |
| `w4_infer_deep_d018` | 26.55 ms | 29.20 ms | +10.0% | 20.00 ms | 20.00 ms | 11040 KiB | 11712 KiB |
| `w5_infer_wide_w100` | 117.77 ms | 126.61 ms | +7.5% | 110.00 ms | 120.00 ms | 17180 KiB | 20080 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.40 ms | 4.11 ms | +20.8% | 0.00 ms | 0.00 ms | 9928 KiB | 10492 KiB |
| `example_collections` | 2.94 ms | 3.21 ms | +9.3% | 0.00 ms | 0.00 ms | 9996 KiB | 10804 KiB |
| `example_counter` | 1.77 ms | 1.80 ms | +1.6% | 0.00 ms | 0.00 ms | 9276 KiB | 10192 KiB |
| `example_domain_typing` | 3.79 ms | 3.76 ms | -0.9% | 0.00 ms | 0.00 ms | 10108 KiB | 10612 KiB |
| `example_domains_and_messages` | 1.82 ms | 1.97 ms | +8.0% | 0.00 ms | 0.00 ms | 9772 KiB | 10144 KiB |
| `example_functional_basics` | 2.70 ms | 3.02 ms | +11.9% | 0.00 ms | 0.00 ms | 9788 KiB | 10420 KiB |
| `example_functional_dataflow` | 3.07 ms | 3.33 ms | +8.5% | 0.00 ms | 0.00 ms | 9784 KiB | 10492 KiB |
| `example_functional_pipeline` | 2.08 ms | 1.78 ms | -14.3% | 0.00 ms | 0.00 ms | 9548 KiB | 10108 KiB |
| `example_shared_memory` | 2.17 ms | 2.31 ms | +6.5% | 0.00 ms | 0.00 ms | 11284 KiB | 10192 KiB |
| `example_static_duck_typing` | 2.52 ms | 2.75 ms | +9.0% | 0.00 ms | 0.00 ms | 9916 KiB | 10680 KiB |
| `mg1_multi_file` | 5.02 ms | 5.00 ms | -0.4% | 0.00 ms | 0.00 ms | 10620 KiB | 11352 KiB |
| `mg2_multi_module` | 6.43 ms | 6.37 ms | -1.0% | 0.00 ms | 0.00 ms | 10556 KiB | 11680 KiB |
| `mg3_deep_chain` | 4.47 ms | 4.45 ms | -0.6% | 0.00 ms | 0.00 ms | 10772 KiB | 11260 KiB |
| `mg4_fan_out` | 8.42 ms | 9.52 ms | +13.2% | 0.00 ms | 0.00 ms | 11028 KiB | 11568 KiB |
| `mg5_diamond` | 3.61 ms | 3.78 ms | +4.8% | 0.00 ms | 0.00 ms | 10684 KiB | 11160 KiB |
| `negative_await_retired` | 1.01 ms | 1.07 ms | +6.0% | 0.00 ms | 0.00 ms | 7068 KiB | 7124 KiB |
| `negative_domain_route_cycle` | 1.33 ms | 1.34 ms | +1.3% | 0.00 ms | 0.00 ms | 9012 KiB | 9596 KiB |
| `negative_duck_missing_method` | 1.63 ms | 1.71 ms | +4.7% | 0.00 ms | 0.00 ms | 10132 KiB | 10260 KiB |
| `negative_heterogeneous_collection` | 1.42 ms | 1.33 ms | -6.6% | 0.00 ms | 0.00 ms | 9628 KiB | 9872 KiB |
| `negative_mutually_recursive_functions` | 1.46 ms | 1.84 ms | +25.6% | 0.00 ms | 0.00 ms | 9248 KiB | 9472 KiB |
| `negative_self_send_rejected` | 1.32 ms | 1.27 ms | -3.9% | 0.00 ms | 0.00 ms | 9672 KiB | 9684 KiB |
| `negative_spawn_retired` | 1.24 ms | 1.25 ms | +0.6% | 0.00 ms | 0.00 ms | 9448 KiB | 9336 KiB |
| `negative_trait_missing_method` | 2.16 ms | 2.14 ms | -1.0% | 0.00 ms | 0.00 ms | 10260 KiB | 10368 KiB |
| `negative_write_write_alias` | 1.85 ms | 1.92 ms | +3.7% | 0.00 ms | 0.00 ms | 10144 KiB | 10404 KiB |
| `rw_example_checkout` | 3.43 ms | 3.59 ms | +4.8% | 0.00 ms | 0.00 ms | 9980 KiB | 10436 KiB |
| `rw_example_collections` | 3.06 ms | 3.31 ms | +8.1% | 0.00 ms | 0.00 ms | 10124 KiB | 10888 KiB |
| `rw_example_counter` | 1.85 ms | 1.87 ms | +0.7% | 0.00 ms | 0.00 ms | 9488 KiB | 10188 KiB |
| `rw_example_domains` | 1.94 ms | 2.13 ms | +9.8% | 0.00 ms | 0.00 ms | 9752 KiB | 10260 KiB |
| `rw_example_pipeline` | 1.82 ms | 2.20 ms | +21.0% | 0.00 ms | 0.00 ms | 9520 KiB | 9960 KiB |
| `rw_packages_regex` | 637.32 ms | 652.39 ms | +2.4% | 630.00 ms | 640.00 ms | 25520 KiB | 30164 KiB |
| `rw_phase106d1_borrowed_reads` | 11.32 ms | 12.81 ms | +13.2% | 10.00 ms | 10.00 ms | 10416 KiB | 11444 KiB |
| `rw_phase106d1_read_projection` | 8.64 ms | 9.47 ms | +9.5% | 0.00 ms | 0.00 ms | 12052 KiB | 11448 KiB |
| `rw_phase106d_handler_2pl` | 6.79 ms | 8.55 ms | +26.0% | 0.00 ms | 0.00 ms | 9896 KiB | 10624 KiB |
| `rw_phase15_15_enum_positive` | 5.30 ms | 6.59 ms | +24.2% | 0.00 ms | 0.00 ms | 10160 KiB | 10876 KiB |
| `rw_phase20_fileio_helper` | 2.47 ms | 2.42 ms | -2.0% | 0.00 ms | 0.00 ms | 9744 KiB | 10232 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.91 ms | 2.80 ms | -3.8% | 0.00 ms | 0.00 ms | 9876 KiB | 10284 KiB |
| `rw_phase24_string_concat` | 5.23 ms | 5.80 ms | +11.0% | 0.00 ms | 0.00 ms | 10280 KiB | 11068 KiB |
| `rw_phase25_integer_overflow` | 5.71 ms | 5.70 ms | -0.1% | 0.00 ms | 0.00 ms | 9948 KiB | 10700 KiB |
| `rw_phase46_semantic_barriers` | 6.03 ms | 8.23 ms | +36.4% | 0.00 ms | 0.00 ms | 10060 KiB | 10808 KiB |
| `rw_phase47_iteration` | 7.54 ms | 8.14 ms | +7.9% | 0.00 ms | 0.00 ms | 10284 KiB | 10728 KiB |
| `rw_phase4_functional` | 11.91 ms | 13.55 ms | +13.8% | 10.00 ms | 10.00 ms | 10640 KiB | 11440 KiB |
| `rw_projects_cache` | 28.20 ms | 28.78 ms | +2.1% | 20.00 ms | 20.00 ms | 12604 KiB | 14684 KiB |
| `rw_projects_rolling_window` | 48.04 ms | 50.27 ms | +4.6% | 40.00 ms | 40.00 ms | 14728 KiB | 16368 KiB |
| `rw_sdk_datastructures` | 227.49 ms | 253.62 ms | +11.5% | 220.00 ms | 250.00 ms | 19548 KiB | 22864 KiB |
| `rw_swarm_047_reduce_map` | 7.21 ms | 7.88 ms | +9.3% | 0.00 ms | 0.00 ms | 10164 KiB | 10652 KiB |
| `rw_swarm_066_for_range` | 7.19 ms | 8.15 ms | +13.3% | 0.00 ms | 0.00 ms | 9948 KiB | 10972 KiB |
| `rw_tests_branch_confined_open` | 2.64 ms | 3.04 ms | +15.2% | 0.00 ms | 0.00 ms | 9692 KiB | 10576 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.29 ms | 2.53 ms | +10.6% | 0.00 ms | 0.00 ms | 9660 KiB | 10112 KiB |
| `test_trait_open_forwarding` | 2.90 ms | 3.42 ms | +18.1% | 0.00 ms | 0.00 ms | 10424 KiB | 10760 KiB |
| `test_unused_inferred_field_template` | 1.29 ms | 1.43 ms | +10.4% | 0.00 ms | 0.00 ms | 9456 KiB | 9816 KiB |
| `w2_spec_distinct_n008` | 85.15 ms | 93.44 ms | +9.7% | 80.00 ms | 80.00 ms | 12060 KiB | 13808 KiB |
| `w2_spec_distinct_n016` | 372.19 ms | 379.26 ms | +1.9% | 360.00 ms | 370.00 ms | 17924 KiB | 23168 KiB |
| `w2_spec_distinct_n032` | 1816.19 ms | 1812.20 ms | -0.2% | 1800.00 ms | 1800.00 ms | 36920 KiB | 59344 KiB |
| `w3_spec_repeated_n008` | 62.38 ms | 57.54 ms | -7.8% | 60.00 ms | 50.00 ms | 10996 KiB | 11676 KiB |
| `w3_spec_repeated_n016` | 236.00 ms | 214.38 ms | -9.2% | 230.00 ms | 210.00 ms | 15396 KiB | 15044 KiB |
| `w3_spec_repeated_n032` | 879.19 ms | 850.06 ms | -3.3% | 870.00 ms | 840.00 ms | 24672 KiB | 27668 KiB |
| `w4_infer_deep_d005` | 5.54 ms | 5.69 ms | +2.6% | 0.00 ms | 0.00 ms | 10200 KiB | 10584 KiB |
| `w4_infer_deep_d010` | 11.15 ms | 11.44 ms | +2.6% | 10.00 ms | 10.00 ms | 10524 KiB | 11068 KiB |
| `w4_infer_deep_d015` | 18.92 ms | 19.35 ms | +2.3% | 10.00 ms | 10.00 ms | 10748 KiB | 11352 KiB |
| `w4_infer_deep_d018` | 26.55 ms | 29.20 ms | +10.0% | 20.00 ms | 20.00 ms | 11040 KiB | 11712 KiB |
| `w5_infer_wide_w010` | 12.50 ms | 13.37 ms | +7.0% | 10.00 ms | 10.00 ms | 10752 KiB | 11480 KiB |
| `w5_infer_wide_w025` | 30.35 ms | 33.64 ms | +10.8% | 20.00 ms | 30.00 ms | 11716 KiB | 12760 KiB |
| `w5_infer_wide_w050` | 59.79 ms | 62.02 ms | +3.7% | 50.00 ms | 60.00 ms | 13560 KiB | 15152 KiB |
| `w5_infer_wide_w100` | 117.77 ms | 126.61 ms | +7.5% | 110.00 ms | 120.00 ms | 17180 KiB | 20080 KiB |
| `w6_types_count_t010` | 7.18 ms | 7.65 ms | +6.5% | 0.00 ms | 0.00 ms | 10256 KiB | 11072 KiB |
| `w6_types_count_t025` | 15.44 ms | 16.79 ms | +8.7% | 10.00 ms | 10.00 ms | 11396 KiB | 12348 KiB |
| `w6_types_count_t050` | 35.63 ms | 38.68 ms | +8.6% | 30.00 ms | 30.00 ms | 13484 KiB | 15224 KiB |
| `w6_types_count_t100` | 87.68 ms | 92.32 ms | +5.3% | 80.00 ms | 80.00 ms | 18608 KiB | 21912 KiB |
| `w7_collections_nested_k1` | 1.84 ms | 1.78 ms | -3.0% | 0.00 ms | 0.00 ms | 9260 KiB | 9940 KiB |
| `w7_collections_nested_k2` | 1.68 ms | 2.43 ms | +44.3% | 0.00 ms | 0.00 ms | 9416 KiB | 10208 KiB |
| `w7_collections_nested_k3` | 2.07 ms | 2.29 ms | +11.0% | 0.00 ms | 0.00 ms | 11200 KiB | 10056 KiB |
| `w7_collections_nested_k4` | 2.63 ms | 3.07 ms | +16.6% | 0.00 ms | 0.00 ms | 9324 KiB | 10216 KiB |
| `w7_collections_nested_k5` | 3.08 ms | 4.65 ms | +51.0% | 0.00 ms | 0.00 ms | 9744 KiB | 10316 KiB |
| `w8_traits_dispatch_c010` | 8.32 ms | 9.12 ms | +9.6% | 0.00 ms | 0.00 ms | 10512 KiB | 11220 KiB |
| `w8_traits_dispatch_c025` | 20.61 ms | 19.10 ms | -7.3% | 20.00 ms | 10.00 ms | 11316 KiB | 12284 KiB |
| `w8_traits_dispatch_c050` | 37.97 ms | 39.66 ms | +4.5% | 30.00 ms | 30.00 ms | 12712 KiB | 14380 KiB |
| `w8_traits_dispatch_c100` | 74.82 ms | 78.92 ms | +5.5% | 70.00 ms | 70.00 ms | 15284 KiB | 18136 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 48.04 ms | 50.27 ms | +4.6% | 40.00 ms | 40.00 ms | 14728 KiB | 16368 KiB |
| `example_checkout` | 3.40 ms | 4.11 ms | +20.8% | 0.00 ms | 0.00 ms | 9928 KiB | 10492 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `rw_phase46_semantic_barriers` | 6.03 [5.98–6.17] ms | 8.23 [7.10–9.30] ms | +2.20 ms | +36.4% | 0.00 ms | 10808 KiB |
| `rw_sdk_datastructures` | 227.49 [213.50–233.30] ms | 253.62 [238.54–260.12] ms | +26.13 ms | +11.5% | 250.00 ms | 22864 KiB |
| `w5_infer_wide_w025` | 30.35 [29.54–30.41] ms | 33.64 [31.46–34.73] ms | +3.29 ms | +10.8% | 30.00 ms | 12760 KiB |
| `w2_spec_distinct_n008` | 85.15 [84.30–88.84] ms | 93.44 [92.75–95.03] ms | +8.29 ms | +9.7% | 80.00 ms | 13808 KiB |
| `w6_types_count_t050` | 35.63 [33.95–35.89] ms | 38.68 [37.93–39.87] ms | +3.05 ms | +8.6% | 30.00 ms | 15224 KiB |
| `w6_types_count_t100` | 87.68 [81.56–89.78] ms | 92.32 [91.23–94.65] ms | +4.64 ms | +5.3% | 80.00 ms | 21912 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `w7_collections_nested_k5` | 3.08 ms | 4.65 ms | +1.57 ms | +51.0% |
| `w7_collections_nested_k2` | 1.68 ms | 2.43 ms | +0.75 ms | +44.3% |
| `rw_phase106d_handler_2pl` | 6.79 ms | 8.55 ms | +1.77 ms | +26.0% |
| `negative_mutually_recursive_functions` | 1.46 ms | 1.84 ms | +0.38 ms | +25.6% |
| `rw_phase15_15_enum_positive` | 5.30 ms | 6.59 ms | +1.29 ms | +24.2% |
| `rw_example_pipeline` | 1.82 ms | 2.20 ms | +0.38 ms | +21.0% |
| `example_checkout` | 3.40 ms | 4.11 ms | +0.71 ms | +20.8% |
| `test_trait_open_forwarding` | 2.90 ms | 3.42 ms | +0.52 ms | +18.1% |
| `w7_collections_nested_k4` | 2.63 ms | 3.07 ms | +0.44 ms | +16.6% |
| `rw_tests_branch_confined_open` | 2.64 ms | 3.04 ms | +0.40 ms | +15.2% |
| `rw_phase4_functional` | 11.91 ms | 13.55 ms | +1.65 ms | +13.8% |
| `rw_swarm_066_for_range` | 7.19 ms | 8.15 ms | +0.96 ms | +13.3% |
| `rw_phase106d1_borrowed_reads` | 11.32 ms | 12.81 ms | +1.49 ms | +13.2% |
| `mg4_fan_out` | 8.42 ms | 9.52 ms | +1.11 ms | +13.2% |
| `example_functional_basics` | 2.70 ms | 3.02 ms | +0.32 ms | +11.9% |
| `w7_collections_nested_k3` | 2.07 ms | 2.29 ms | +0.23 ms | +11.0% |
| `rw_phase24_string_concat` | 5.23 ms | 5.80 ms | +0.57 ms | +11.0% |
| `test_phase45_short_circuit_failure_barrier` | 2.29 ms | 2.53 ms | +0.24 ms | +10.6% |
| `test_unused_inferred_field_template` | 1.29 ms | 1.43 ms | +0.13 ms | +10.4% |
| `w4_infer_deep_d018` | 26.55 ms | 29.20 ms | +2.66 ms | +10.0% |
| `rw_example_domains` | 1.94 ms | 2.13 ms | +0.19 ms | +9.8% |
| `w8_traits_dispatch_c010` | 8.32 ms | 9.12 ms | +0.80 ms | +9.6% |
| `rw_phase106d1_read_projection` | 8.64 ms | 9.47 ms | +0.82 ms | +9.5% |
| `example_collections` | 2.94 ms | 3.21 ms | +0.27 ms | +9.3% |
| `rw_swarm_047_reduce_map` | 7.21 ms | 7.88 ms | +0.67 ms | +9.3% |
| `example_static_duck_typing` | 2.52 ms | 2.75 ms | +0.23 ms | +9.0% |
| `w6_types_count_t025` | 15.44 ms | 16.79 ms | +1.34 ms | +8.7% |
| `example_functional_dataflow` | 3.07 ms | 3.33 ms | +0.26 ms | +8.5% |
| `rw_example_collections` | 3.06 ms | 3.31 ms | +0.25 ms | +8.1% |
| `example_domains_and_messages` | 1.82 ms | 1.97 ms | +0.15 ms | +8.0% |
| `rw_phase47_iteration` | 7.54 ms | 8.14 ms | +0.60 ms | +7.9% |
| `w5_infer_wide_w100` | 117.77 ms | 126.61 ms | +8.84 ms | +7.5% |
| `w5_infer_wide_w010` | 12.50 ms | 13.37 ms | +0.87 ms | +7.0% |
| `w6_types_count_t010` | 7.18 ms | 7.65 ms | +0.47 ms | +6.5% |
| `example_shared_memory` | 2.17 ms | 2.31 ms | +0.14 ms | +6.5% |
| `negative_await_retired` | 1.01 ms | 1.07 ms | +0.06 ms | +6.0% |
| `w8_traits_dispatch_c100` | 74.82 ms | 78.92 ms | +4.10 ms | +5.5% |

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
