# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.4521484375, 1.2392578125, 1.9853515625)`; after profiling `(1.173828125, 1.189453125, 1.93212890625)`.
- Candidate benchmark checkout SHA: `2445ec08951cdf4bbd4af5d755cd887b1d89c468`
- Candidate compiler source SHA: `2445ec08951cdf4bbd4af5d755cd887b1d89c468`
- Candidate binary SHA256: `c0b0e2c4109405a8bc37c575d8cfa496e2793cea2223141244d6150ea3431695`
- Certification mode: `False`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `phase27-1-m1` resolved to `5627727657ad7d26d860d4824b4ba03764252b0e`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.130 s | 5.251 s | +2.4% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.40 ms | 3.77 ms | +10.9% | 0.00 ms | 0.00 ms | 9800 KiB | 10516 KiB |
| `rw_packages_regex` | 636.76 ms | 661.55 ms | +3.9% | 620.00 ms | 650.00 ms | 24588 KiB | 30148 KiB |
| `rw_projects_rolling_window` | 43.61 ms | 46.79 ms | +7.3% | 40.00 ms | 40.00 ms | 13772 KiB | 16500 KiB |
| `rw_sdk_datastructures` | 228.17 ms | 244.11 ms | +7.0% | 220.00 ms | 230.00 ms | 19020 KiB | 23088 KiB |
| `w2_spec_distinct_n032` | 1818.93 ms | 1858.94 ms | +2.2% | 1800.00 ms | 1850.00 ms | 34920 KiB | 59324 KiB |
| `w3_spec_repeated_n032` | 864.46 ms | 847.75 ms | -1.9% | 850.00 ms | 840.00 ms | 22952 KiB | 27628 KiB |
| `w4_infer_deep_d018` | 25.57 ms | 26.57 ms | +3.9% | 20.00 ms | 20.00 ms | 10820 KiB | 11712 KiB |
| `w5_infer_wide_w100` | 116.00 ms | 132.10 ms | +13.9% | 110.00 ms | 120.00 ms | 16920 KiB | 20080 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.40 ms | 3.77 ms | +10.9% | 0.00 ms | 0.00 ms | 9800 KiB | 10516 KiB |
| `example_collections` | 3.02 ms | 3.40 ms | +12.3% | 0.00 ms | 0.00 ms | 10188 KiB | 10724 KiB |
| `example_counter` | 1.76 ms | 1.88 ms | +7.2% | 0.00 ms | 0.00 ms | 9392 KiB | 10172 KiB |
| `example_domain_typing` | 3.41 ms | 3.85 ms | +12.7% | 0.00 ms | 0.00 ms | 9932 KiB | 10528 KiB |
| `example_domains_and_messages` | 1.94 ms | 1.89 ms | -2.3% | 0.00 ms | 0.00 ms | 9396 KiB | 10236 KiB |
| `example_functional_basics` | 2.51 ms | 3.02 ms | +20.3% | 0.00 ms | 0.00 ms | 9428 KiB | 10280 KiB |
| `example_functional_dataflow` | 2.88 ms | 3.45 ms | +19.5% | 0.00 ms | 0.00 ms | 9668 KiB | 10512 KiB |
| `example_functional_pipeline` | 1.60 ms | 1.74 ms | +8.9% | 0.00 ms | 0.00 ms | 9260 KiB | 10136 KiB |
| `example_shared_memory` | 2.04 ms | 2.19 ms | +7.4% | 0.00 ms | 0.00 ms | 9544 KiB | 10332 KiB |
| `example_static_duck_typing` | 2.66 ms | 2.78 ms | +4.6% | 0.00 ms | 0.00 ms | 9892 KiB | 10716 KiB |
| `mg1_multi_file` | 4.99 ms | 4.98 ms | -0.2% | 0.00 ms | 0.00 ms | 10552 KiB | 11340 KiB |
| `mg2_multi_module` | 5.42 ms | 5.87 ms | +8.3% | 0.00 ms | 0.00 ms | 10452 KiB | 11440 KiB |
| `mg3_deep_chain` | 4.01 ms | 4.88 ms | +21.7% | 0.00 ms | 0.00 ms | 10412 KiB | 11260 KiB |
| `mg4_fan_out` | 7.96 ms | 9.04 ms | +13.6% | 0.00 ms | 0.00 ms | 10712 KiB | 11644 KiB |
| `mg5_diamond` | 3.47 ms | 4.15 ms | +19.6% | 0.00 ms | 0.00 ms | 10156 KiB | 10952 KiB |
| `negative_await_retired` | 1.08 ms | 1.04 ms | -3.6% | 0.00 ms | 0.00 ms | 6972 KiB | 7320 KiB |
| `negative_domain_route_cycle` | 1.25 ms | 1.41 ms | +12.9% | 0.00 ms | 0.00 ms | 9084 KiB | 9440 KiB |
| `negative_duck_missing_method` | 1.62 ms | 1.68 ms | +3.3% | 0.00 ms | 0.00 ms | 10204 KiB | 10120 KiB |
| `negative_heterogeneous_collection` | 1.23 ms | 1.33 ms | +7.6% | 0.00 ms | 0.00 ms | 9584 KiB | 10076 KiB |
| `negative_mutually_recursive_functions` | 1.36 ms | 1.44 ms | +5.9% | 0.00 ms | 0.00 ms | 9360 KiB | 9344 KiB |
| `negative_self_send_rejected` | 1.66 ms | 1.41 ms | -15.2% | 0.00 ms | 0.00 ms | 9392 KiB | 9816 KiB |
| `negative_spawn_retired` | 1.24 ms | 1.31 ms | +6.3% | 0.00 ms | 0.00 ms | 9368 KiB | 9252 KiB |
| `negative_trait_missing_method` | 1.94 ms | 2.13 ms | +9.8% | 0.00 ms | 0.00 ms | 9884 KiB | 10376 KiB |
| `negative_write_write_alias` | 1.76 ms | 1.91 ms | +8.4% | 0.00 ms | 0.00 ms | 10332 KiB | 10404 KiB |
| `rw_example_checkout` | 3.40 ms | 3.88 ms | +14.1% | 0.00 ms | 0.00 ms | 9780 KiB | 10388 KiB |
| `rw_example_collections` | 2.97 ms | 3.24 ms | +9.1% | 0.00 ms | 0.00 ms | 10188 KiB | 10848 KiB |
| `rw_example_counter` | 1.78 ms | 1.87 ms | +5.3% | 0.00 ms | 0.00 ms | 9440 KiB | 10200 KiB |
| `rw_example_domains` | 1.79 ms | 1.99 ms | +11.5% | 0.00 ms | 0.00 ms | 9516 KiB | 10192 KiB |
| `rw_example_pipeline` | 1.64 ms | 2.19 ms | +32.9% | 0.00 ms | 0.00 ms | 9252 KiB | 10040 KiB |
| `rw_packages_regex` | 636.76 ms | 661.55 ms | +3.9% | 620.00 ms | 650.00 ms | 24588 KiB | 30148 KiB |
| `rw_phase106d1_borrowed_reads` | 11.72 ms | 12.09 ms | +3.2% | 10.00 ms | 10.00 ms | 10196 KiB | 11400 KiB |
| `rw_phase106d1_read_projection` | 8.46 ms | 9.57 ms | +13.2% | 0.00 ms | 0.00 ms | 10160 KiB | 11332 KiB |
| `rw_phase106d_handler_2pl` | 6.56 ms | 7.51 ms | +14.5% | 0.00 ms | 0.00 ms | 9884 KiB | 10728 KiB |
| `rw_phase15_15_enum_positive` | 4.93 ms | 5.59 ms | +13.5% | 0.00 ms | 0.00 ms | 10088 KiB | 10924 KiB |
| `rw_phase20_fileio_helper` | 2.20 ms | 2.38 ms | +8.2% | 0.00 ms | 0.00 ms | 9576 KiB | 10136 KiB |
| `rw_phase21_fileio_root_cleanup` | 3.74 ms | 2.94 ms | -21.3% | 0.00 ms | 0.00 ms | 9708 KiB | 10360 KiB |
| `rw_phase24_string_concat` | 5.13 ms | 5.61 ms | +9.4% | 0.00 ms | 0.00 ms | 9896 KiB | 10992 KiB |
| `rw_phase25_integer_overflow` | 7.15 ms | 6.23 ms | -12.9% | 0.00 ms | 0.00 ms | 9892 KiB | 10688 KiB |
| `rw_phase46_semantic_barriers` | 5.40 ms | 7.33 ms | +35.8% | 0.00 ms | 0.00 ms | 9948 KiB | 10744 KiB |
| `rw_phase47_iteration` | 7.05 ms | 7.91 ms | +12.2% | 0.00 ms | 0.00 ms | 9908 KiB | 10836 KiB |
| `rw_phase4_functional` | 10.76 ms | 13.88 ms | +28.9% | 10.00 ms | 10.00 ms | 10512 KiB | 11400 KiB |
| `rw_projects_cache` | 26.92 ms | 30.56 ms | +13.5% | 20.00 ms | 30.00 ms | 12456 KiB | 14580 KiB |
| `rw_projects_rolling_window` | 43.61 ms | 46.79 ms | +7.3% | 40.00 ms | 40.00 ms | 13772 KiB | 16500 KiB |
| `rw_sdk_datastructures` | 228.17 ms | 244.11 ms | +7.0% | 220.00 ms | 230.00 ms | 19020 KiB | 23088 KiB |
| `rw_swarm_047_reduce_map` | 7.79 ms | 7.82 ms | +0.3% | 0.00 ms | 0.00 ms | 10020 KiB | 10788 KiB |
| `rw_swarm_066_for_range` | 7.02 ms | 8.03 ms | +14.5% | 0.00 ms | 0.00 ms | 10164 KiB | 10736 KiB |
| `rw_tests_branch_confined_open` | 2.85 ms | 2.98 ms | +4.5% | 0.00 ms | 0.00 ms | 9712 KiB | 10452 KiB |
| `test_phase45_short_circuit_failure_barrier` | 1.99 ms | 2.58 ms | +30.0% | 0.00 ms | 0.00 ms | 9420 KiB | 10156 KiB |
| `test_trait_open_forwarding` | 2.79 ms | 2.76 ms | -1.1% | 0.00 ms | 0.00 ms | 10564 KiB | 10740 KiB |
| `test_unused_inferred_field_template` | 1.25 ms | 1.44 ms | +14.5% | 0.00 ms | 0.00 ms | 9152 KiB | 9792 KiB |
| `w2_spec_distinct_n008` | 85.03 ms | 88.99 ms | +4.7% | 80.00 ms | 80.00 ms | 11752 KiB | 13796 KiB |
| `w2_spec_distinct_n016` | 374.83 ms | 382.71 ms | +2.1% | 370.00 ms | 370.00 ms | 17600 KiB | 23132 KiB |
| `w2_spec_distinct_n032` | 1818.93 ms | 1858.94 ms | +2.2% | 1800.00 ms | 1850.00 ms | 34920 KiB | 59324 KiB |
| `w3_spec_repeated_n008` | 59.96 ms | 55.99 ms | -6.6% | 50.00 ms | 50.00 ms | 10720 KiB | 11784 KiB |
| `w3_spec_repeated_n016` | 235.11 ms | 223.46 ms | -5.0% | 230.00 ms | 220.00 ms | 13256 KiB | 15160 KiB |
| `w3_spec_repeated_n032` | 864.46 ms | 847.75 ms | -1.9% | 850.00 ms | 840.00 ms | 22952 KiB | 27628 KiB |
| `w4_infer_deep_d005` | 5.61 ms | 5.79 ms | +3.1% | 0.00 ms | 0.00 ms | 10028 KiB | 10648 KiB |
| `w4_infer_deep_d010` | 11.09 ms | 12.28 ms | +10.7% | 10.00 ms | 10.00 ms | 10476 KiB | 10964 KiB |
| `w4_infer_deep_d015` | 17.90 ms | 20.29 ms | +13.3% | 10.00 ms | 10.00 ms | 10568 KiB | 11476 KiB |
| `w4_infer_deep_d018` | 25.57 ms | 26.57 ms | +3.9% | 20.00 ms | 20.00 ms | 10820 KiB | 11712 KiB |
| `w5_infer_wide_w010` | 12.38 ms | 13.09 ms | +5.8% | 10.00 ms | 10.00 ms | 10488 KiB | 11584 KiB |
| `w5_infer_wide_w025` | 29.13 ms | 32.11 ms | +10.3% | 20.00 ms | 20.00 ms | 11596 KiB | 12564 KiB |
| `w5_infer_wide_w050` | 61.44 ms | 65.06 ms | +5.9% | 50.00 ms | 60.00 ms | 13100 KiB | 15288 KiB |
| `w5_infer_wide_w100` | 116.00 ms | 132.10 ms | +13.9% | 110.00 ms | 120.00 ms | 16920 KiB | 20080 KiB |
| `w6_types_count_t010` | 6.56 ms | 7.09 ms | +8.1% | 0.00 ms | 0.00 ms | 9928 KiB | 10968 KiB |
| `w6_types_count_t025` | 15.04 ms | 20.27 ms | +34.8% | 10.00 ms | 10.00 ms | 11216 KiB | 12212 KiB |
| `w6_types_count_t050` | 35.52 ms | 37.18 ms | +4.7% | 30.00 ms | 30.00 ms | 13280 KiB | 15296 KiB |
| `w6_types_count_t100` | 91.51 ms | 89.98 ms | -1.7% | 90.00 ms | 80.00 ms | 18104 KiB | 21628 KiB |
| `w7_collections_nested_k1` | 1.65 ms | 1.97 ms | +19.6% | 0.00 ms | 0.00 ms | 9172 KiB | 9944 KiB |
| `w7_collections_nested_k2` | 1.71 ms | 1.99 ms | +16.4% | 0.00 ms | 0.00 ms | 9136 KiB | 10048 KiB |
| `w7_collections_nested_k3` | 2.12 ms | 2.23 ms | +4.9% | 0.00 ms | 0.00 ms | 9520 KiB | 10060 KiB |
| `w7_collections_nested_k4` | 2.44 ms | 2.79 ms | +14.4% | 0.00 ms | 0.00 ms | 9568 KiB | 10072 KiB |
| `w7_collections_nested_k5` | 4.38 ms | 3.38 ms | -22.9% | 0.00 ms | 0.00 ms | 9436 KiB | 11560 KiB |
| `w8_traits_dispatch_c010` | 8.85 ms | 9.27 ms | +4.8% | 0.00 ms | 0.00 ms | 10472 KiB | 11348 KiB |
| `w8_traits_dispatch_c025` | 20.25 ms | 18.71 ms | -7.6% | 10.00 ms | 10.00 ms | 11080 KiB | 12248 KiB |
| `w8_traits_dispatch_c050` | 39.36 ms | 37.23 ms | -5.4% | 30.00 ms | 30.00 ms | 12340 KiB | 14140 KiB |
| `w8_traits_dispatch_c100` | 75.50 ms | 80.63 ms | +6.8% | 70.00 ms | 70.00 ms | 14892 KiB | 18164 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 43.61 ms | 46.79 ms | +7.3% | 40.00 ms | 40.00 ms | 13772 KiB | 16500 KiB |
| `example_checkout` | 3.40 ms | 3.77 ms | +10.9% | 0.00 ms | 0.00 ms | 9800 KiB | 10516 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `w6_types_count_t025` | 15.04 [14.97–15.08] ms | 20.27 [16.78–20.27] ms | +5.23 ms | +34.8% | 10.00 ms | 12212 KiB |
| `rw_phase4_functional` | 10.76 [10.40–11.17] ms | 13.88 [13.53–16.37] ms | +3.11 ms | +28.9% | 10.00 ms | 11400 KiB |
| `w5_infer_wide_w100` | 116.00 [107.78–117.53] ms | 132.10 [121.38–132.28] ms | +16.11 ms | +13.9% | 120.00 ms | 20080 KiB |
| `rw_projects_cache` | 26.92 [26.84–27.46] ms | 30.56 [28.59–31.47] ms | +3.65 ms | +13.5% | 30.00 ms | 14580 KiB |
| `w5_infer_wide_w025` | 29.13 [28.30–29.40] ms | 32.11 [31.18–32.72] ms | +2.99 ms | +10.3% | 20.00 ms | 12564 KiB |
| `rw_sdk_datastructures` | 228.17 [225.51–229.22] ms | 244.11 [243.72–254.74] ms | +15.94 ms | +7.0% | 230.00 ms | 23088 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `rw_phase46_semantic_barriers` | 5.40 ms | 7.33 ms | +1.93 ms | +35.8% |
| `rw_example_pipeline` | 1.64 ms | 2.19 ms | +0.54 ms | +32.9% |
| `test_phase45_short_circuit_failure_barrier` | 1.99 ms | 2.58 ms | +0.60 ms | +30.0% |
| `mg3_deep_chain` | 4.01 ms | 4.88 ms | +0.87 ms | +21.7% |
| `example_functional_basics` | 2.51 ms | 3.02 ms | +0.51 ms | +20.3% |
| `w7_collections_nested_k1` | 1.65 ms | 1.97 ms | +0.32 ms | +19.6% |
| `mg5_diamond` | 3.47 ms | 4.15 ms | +0.68 ms | +19.6% |
| `example_functional_dataflow` | 2.88 ms | 3.45 ms | +0.56 ms | +19.5% |
| `w7_collections_nested_k2` | 1.71 ms | 1.99 ms | +0.28 ms | +16.4% |
| `rw_phase106d_handler_2pl` | 6.56 ms | 7.51 ms | +0.95 ms | +14.5% |
| `test_unused_inferred_field_template` | 1.25 ms | 1.44 ms | +0.18 ms | +14.5% |
| `rw_swarm_066_for_range` | 7.02 ms | 8.03 ms | +1.02 ms | +14.5% |
| `w7_collections_nested_k4` | 2.44 ms | 2.79 ms | +0.35 ms | +14.4% |
| `rw_example_checkout` | 3.40 ms | 3.88 ms | +0.48 ms | +14.1% |
| `mg4_fan_out` | 7.96 ms | 9.04 ms | +1.08 ms | +13.6% |
| `rw_phase15_15_enum_positive` | 4.93 ms | 5.59 ms | +0.66 ms | +13.5% |
| `w4_infer_deep_d015` | 17.90 ms | 20.29 ms | +2.39 ms | +13.3% |
| `rw_phase106d1_read_projection` | 8.46 ms | 9.57 ms | +1.12 ms | +13.2% |
| `negative_domain_route_cycle` | 1.25 ms | 1.41 ms | +0.16 ms | +12.9% |
| `example_domain_typing` | 3.41 ms | 3.85 ms | +0.43 ms | +12.7% |
| `example_collections` | 3.02 ms | 3.40 ms | +0.37 ms | +12.3% |
| `rw_phase47_iteration` | 7.05 ms | 7.91 ms | +0.86 ms | +12.2% |
| `rw_example_domains` | 1.79 ms | 1.99 ms | +0.21 ms | +11.5% |
| `example_checkout` | 3.40 ms | 3.77 ms | +0.37 ms | +10.9% |
| `w4_infer_deep_d010` | 11.09 ms | 12.28 ms | +1.19 ms | +10.7% |
| `negative_trait_missing_method` | 1.94 ms | 2.13 ms | +0.19 ms | +9.8% |
| `rw_phase24_string_concat` | 5.13 ms | 5.61 ms | +0.48 ms | +9.4% |
| `rw_example_collections` | 2.97 ms | 3.24 ms | +0.27 ms | +9.1% |
| `example_functional_pipeline` | 1.60 ms | 1.74 ms | +0.14 ms | +8.9% |
| `negative_write_write_alias` | 1.76 ms | 1.91 ms | +0.15 ms | +8.4% |
| `mg2_multi_module` | 5.42 ms | 5.87 ms | +0.45 ms | +8.3% |
| `rw_phase20_fileio_helper` | 2.20 ms | 2.38 ms | +0.18 ms | +8.2% |
| `w6_types_count_t010` | 6.56 ms | 7.09 ms | +0.53 ms | +8.1% |
| `negative_heterogeneous_collection` | 1.23 ms | 1.33 ms | +0.09 ms | +7.6% |
| `example_shared_memory` | 2.04 ms | 2.19 ms | +0.15 ms | +7.4% |
| `rw_projects_rolling_window` | 43.61 ms | 46.79 ms | +3.17 ms | +7.3% |
| `example_counter` | 1.76 ms | 1.88 ms | +0.13 ms | +7.2% |
| `w8_traits_dispatch_c100` | 75.50 ms | 80.63 ms | +5.13 ms | +6.8% |
| `negative_spawn_retired` | 1.24 ms | 1.31 ms | +0.08 ms | +6.3% |
| `w5_infer_wide_w050` | 61.44 ms | 65.06 ms | +3.62 ms | +5.9% |
| `negative_mutually_recursive_functions` | 1.36 ms | 1.44 ms | +0.08 ms | +5.9% |
| `w5_infer_wide_w010` | 12.38 ms | 13.09 ms | +0.72 ms | +5.8% |
| `rw_example_counter` | 1.78 ms | 1.87 ms | +0.09 ms | +5.3% |

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
