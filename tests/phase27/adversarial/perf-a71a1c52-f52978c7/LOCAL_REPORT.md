# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(2.61279296875, 7.142578125, 6.1591796875)`; after profiling `(1.7587890625, 6.28173828125, 5.9130859375)`.
- Candidate benchmark checkout SHA: `f52978c7fa8ee7c98a1cf8b8ff657fa0a6610fc7`
- Candidate compiler source SHA: `f52978c7fa8ee7c98a1cf8b8ff657fa0a6610fc7`
- Candidate binary SHA256: `e58deafa2df7c87ca05bd4da7b430bbca2be2f928057a8531cb69f5951f3d24a`
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
| 5.141 s | 5.606 s | +9.0% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.77 ms | 3.80 ms | +0.7% | 0.00 ms | 0.00 ms | 9836 KiB | 10652 KiB |
| `rw_packages_regex` | 632.37 ms | 673.76 ms | +6.5% | 630.00 ms | 660.00 ms | 25524 KiB | 29848 KiB |
| `rw_projects_rolling_window` | 45.27 ms | 51.33 ms | +13.4% | 40.00 ms | 40.00 ms | 14260 KiB | 16628 KiB |
| `rw_sdk_datastructures` | 228.86 ms | 262.89 ms | +14.9% | 220.00 ms | 260.00 ms | 19440 KiB | 22992 KiB |
| `w2_spec_distinct_n032` | 1802.21 ms | 2014.67 ms | +11.8% | 1800.00 ms | 2010.00 ms | 36728 KiB | 59216 KiB |
| `w3_spec_repeated_n032` | 891.92 ms | 899.78 ms | +0.9% | 880.00 ms | 890.00 ms | 24604 KiB | 27948 KiB |
| `w4_infer_deep_d018` | 26.79 ms | 31.68 ms | +18.3% | 20.00 ms | 30.00 ms | 10964 KiB | 11732 KiB |
| `w5_infer_wide_w100` | 112.31 ms | 129.70 ms | +15.5% | 100.00 ms | 120.00 ms | 17212 KiB | 20116 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.77 ms | 3.80 ms | +0.7% | 0.00 ms | 0.00 ms | 9836 KiB | 10652 KiB |
| `example_collections` | 3.71 ms | 3.40 ms | -8.4% | 0.00 ms | 0.00 ms | 10040 KiB | 10700 KiB |
| `example_counter` | 1.85 ms | 1.82 ms | -1.6% | 0.00 ms | 0.00 ms | 9652 KiB | 10324 KiB |
| `example_domain_typing` | 3.51 ms | 3.77 ms | +7.2% | 0.00 ms | 0.00 ms | 10100 KiB | 10600 KiB |
| `example_domains_and_messages` | 1.81 ms | 2.14 ms | +18.1% | 0.00 ms | 0.00 ms | 9784 KiB | 10040 KiB |
| `example_functional_basics` | 2.54 ms | 3.09 ms | +21.7% | 0.00 ms | 0.00 ms | 9500 KiB | 10360 KiB |
| `example_functional_dataflow` | 2.89 ms | 3.30 ms | +14.1% | 0.00 ms | 0.00 ms | 9828 KiB | 10584 KiB |
| `example_functional_pipeline` | 1.61 ms | 1.78 ms | +10.1% | 0.00 ms | 0.00 ms | 9552 KiB | 10124 KiB |
| `example_shared_memory` | 2.03 ms | 2.14 ms | +5.6% | 0.00 ms | 0.00 ms | 9696 KiB | 10216 KiB |
| `example_static_duck_typing` | 2.59 ms | 2.80 ms | +8.0% | 0.00 ms | 0.00 ms | 9936 KiB | 10780 KiB |
| `mg1_multi_file` | 5.03 ms | 5.71 ms | +13.7% | 0.00 ms | 0.00 ms | 10696 KiB | 11428 KiB |
| `mg2_multi_module` | 5.51 ms | 6.19 ms | +12.3% | 0.00 ms | 0.00 ms | 10724 KiB | 11372 KiB |
| `mg3_deep_chain` | 4.35 ms | 4.47 ms | +2.7% | 0.00 ms | 0.00 ms | 10580 KiB | 11352 KiB |
| `mg4_fan_out` | 8.87 ms | 9.62 ms | +8.4% | 0.00 ms | 0.00 ms | 11000 KiB | 11688 KiB |
| `mg5_diamond` | 3.91 ms | 3.87 ms | -1.0% | 0.00 ms | 0.00 ms | 10680 KiB | 11104 KiB |
| `negative_await_retired` | 1.02 ms | 1.00 ms | -2.0% | 0.00 ms | 0.00 ms | 7136 KiB | 7124 KiB |
| `negative_domain_route_cycle` | 1.25 ms | 1.35 ms | +7.9% | 0.00 ms | 0.00 ms | 9456 KiB | 9428 KiB |
| `negative_duck_missing_method` | 1.61 ms | 1.74 ms | +7.9% | 0.00 ms | 0.00 ms | 10212 KiB | 10056 KiB |
| `negative_heterogeneous_collection` | 1.79 ms | 1.43 ms | -20.0% | 0.00 ms | 0.00 ms | 9560 KiB | 10144 KiB |
| `negative_mutually_recursive_functions` | 1.33 ms | 1.30 ms | -1.8% | 0.00 ms | 0.00 ms | 9232 KiB | 9204 KiB |
| `negative_self_send_rejected` | 1.29 ms | 1.28 ms | -1.2% | 0.00 ms | 0.00 ms | 9548 KiB | 9836 KiB |
| `negative_spawn_retired` | 1.19 ms | 1.26 ms | +5.4% | 0.00 ms | 0.00 ms | 9352 KiB | 9456 KiB |
| `negative_trait_missing_method` | 2.00 ms | 2.18 ms | +8.9% | 0.00 ms | 0.00 ms | 10236 KiB | 10352 KiB |
| `negative_write_write_alias` | 1.85 ms | 1.86 ms | +0.6% | 0.00 ms | 0.00 ms | 10096 KiB | 10428 KiB |
| `rw_example_checkout` | 3.34 ms | 3.73 ms | +11.5% | 0.00 ms | 0.00 ms | 9800 KiB | 10604 KiB |
| `rw_example_collections` | 2.90 ms | 3.31 ms | +13.9% | 0.00 ms | 0.00 ms | 10136 KiB | 10800 KiB |
| `rw_example_counter` | 1.69 ms | 1.72 ms | +1.9% | 0.00 ms | 0.00 ms | 9468 KiB | 10240 KiB |
| `rw_example_domains` | 1.90 ms | 2.12 ms | +12.1% | 0.00 ms | 0.00 ms | 9540 KiB | 10272 KiB |
| `rw_example_pipeline` | 1.55 ms | 1.85 ms | +19.1% | 0.00 ms | 0.00 ms | 9412 KiB | 10028 KiB |
| `rw_packages_regex` | 632.37 ms | 673.76 ms | +6.5% | 630.00 ms | 660.00 ms | 25524 KiB | 29848 KiB |
| `rw_phase106d1_borrowed_reads` | 13.19 ms | 13.09 ms | -0.8% | 10.00 ms | 10.00 ms | 10556 KiB | 11240 KiB |
| `rw_phase106d1_read_projection` | 8.70 ms | 10.90 ms | +25.2% | 0.00 ms | 10.00 ms | 10472 KiB | 11536 KiB |
| `rw_phase106d_handler_2pl` | 6.15 ms | 7.74 ms | +25.8% | 0.00 ms | 0.00 ms | 9956 KiB | 10696 KiB |
| `rw_phase15_15_enum_positive` | 5.21 ms | 5.75 ms | +10.2% | 0.00 ms | 0.00 ms | 10028 KiB | 10948 KiB |
| `rw_phase20_fileio_helper` | 2.73 ms | 2.35 ms | -14.0% | 0.00 ms | 0.00 ms | 9580 KiB | 10120 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.79 ms | 2.85 ms | +2.2% | 0.00 ms | 0.00 ms | 9696 KiB | 10080 KiB |
| `rw_phase24_string_concat` | 5.18 ms | 6.00 ms | +15.8% | 0.00 ms | 0.00 ms | 10024 KiB | 11168 KiB |
| `rw_phase25_integer_overflow` | 5.45 ms | 6.37 ms | +16.8% | 0.00 ms | 0.00 ms | 10108 KiB | 10936 KiB |
| `rw_phase46_semantic_barriers` | 6.06 ms | 7.33 ms | +21.1% | 0.00 ms | 0.00 ms | 10088 KiB | 10964 KiB |
| `rw_phase47_iteration` | 7.15 ms | 8.58 ms | +20.0% | 0.00 ms | 0.00 ms | 10188 KiB | 10844 KiB |
| `rw_phase4_functional` | 12.11 ms | 14.30 ms | +18.1% | 10.00 ms | 10.00 ms | 10564 KiB | 11376 KiB |
| `rw_projects_cache` | 29.83 ms | 33.64 ms | +12.8% | 20.00 ms | 30.00 ms | 12728 KiB | 14500 KiB |
| `rw_projects_rolling_window` | 45.27 ms | 51.33 ms | +13.4% | 40.00 ms | 40.00 ms | 14260 KiB | 16628 KiB |
| `rw_sdk_datastructures` | 228.86 ms | 262.89 ms | +14.9% | 220.00 ms | 260.00 ms | 19440 KiB | 22992 KiB |
| `rw_swarm_047_reduce_map` | 7.26 ms | 10.59 ms | +45.8% | 0.00 ms | 10.00 ms | 10096 KiB | 10664 KiB |
| `rw_swarm_066_for_range` | 7.52 ms | 8.79 ms | +17.0% | 0.00 ms | 0.00 ms | 10128 KiB | 11024 KiB |
| `rw_tests_branch_confined_open` | 2.85 ms | 3.23 ms | +13.4% | 0.00 ms | 0.00 ms | 9656 KiB | 10432 KiB |
| `test_phase45_short_circuit_failure_barrier` | 1.97 ms | 2.29 ms | +16.0% | 0.00 ms | 0.00 ms | 9560 KiB | 10256 KiB |
| `test_trait_open_forwarding` | 2.84 ms | 2.83 ms | -0.0% | 0.00 ms | 0.00 ms | 10380 KiB | 10896 KiB |
| `test_unused_inferred_field_template` | 1.37 ms | 1.41 ms | +3.0% | 0.00 ms | 0.00 ms | 9168 KiB | 9968 KiB |
| `w2_spec_distinct_n008` | 86.88 ms | 97.60 ms | +12.3% | 80.00 ms | 90.00 ms | 12032 KiB | 14008 KiB |
| `w2_spec_distinct_n016` | 392.93 ms | 403.51 ms | +2.7% | 390.00 ms | 400.00 ms | 18036 KiB | 23144 KiB |
| `w2_spec_distinct_n032` | 1802.21 ms | 2014.67 ms | +11.8% | 1800.00 ms | 2010.00 ms | 36728 KiB | 59216 KiB |
| `w3_spec_repeated_n008` | 64.13 ms | 64.76 ms | +1.0% | 60.00 ms | 60.00 ms | 10852 KiB | 11748 KiB |
| `w3_spec_repeated_n016` | 224.92 ms | 236.92 ms | +5.3% | 220.00 ms | 230.00 ms | 13852 KiB | 14968 KiB |
| `w3_spec_repeated_n032` | 891.92 ms | 899.78 ms | +0.9% | 880.00 ms | 890.00 ms | 24604 KiB | 27948 KiB |
| `w4_infer_deep_d005` | 5.42 ms | 5.88 ms | +8.6% | 0.00 ms | 0.00 ms | 10204 KiB | 10632 KiB |
| `w4_infer_deep_d010` | 11.25 ms | 13.22 ms | +17.5% | 10.00 ms | 10.00 ms | 10412 KiB | 11132 KiB |
| `w4_infer_deep_d015` | 21.46 ms | 20.56 ms | -4.2% | 10.00 ms | 20.00 ms | 10696 KiB | 11400 KiB |
| `w4_infer_deep_d018` | 26.79 ms | 31.68 ms | +18.3% | 20.00 ms | 30.00 ms | 10964 KiB | 11732 KiB |
| `w5_infer_wide_w010` | 14.11 ms | 13.94 ms | -1.2% | 10.00 ms | 10.00 ms | 10568 KiB | 11480 KiB |
| `w5_infer_wide_w025` | 29.08 ms | 37.22 ms | +28.0% | 20.00 ms | 30.00 ms | 11800 KiB | 12588 KiB |
| `w5_infer_wide_w050` | 56.88 ms | 69.24 ms | +21.7% | 50.00 ms | 60.00 ms | 13544 KiB | 15172 KiB |
| `w5_infer_wide_w100` | 112.31 ms | 129.70 ms | +15.5% | 100.00 ms | 120.00 ms | 17212 KiB | 20116 KiB |
| `w6_types_count_t010` | 6.92 ms | 7.81 ms | +12.8% | 0.00 ms | 0.00 ms | 10048 KiB | 11088 KiB |
| `w6_types_count_t025` | 15.88 ms | 18.86 ms | +18.8% | 10.00 ms | 10.00 ms | 11408 KiB | 12348 KiB |
| `w6_types_count_t050` | 35.78 ms | 40.55 ms | +13.3% | 30.00 ms | 30.00 ms | 13280 KiB | 15060 KiB |
| `w6_types_count_t100` | 84.64 ms | 101.25 ms | +19.6% | 80.00 ms | 90.00 ms | 18408 KiB | 21688 KiB |
| `w7_collections_nested_k1` | 1.62 ms | 1.97 ms | +21.6% | 0.00 ms | 0.00 ms | 9440 KiB | 10176 KiB |
| `w7_collections_nested_k2` | 1.88 ms | 1.98 ms | +4.9% | 0.00 ms | 0.00 ms | 9280 KiB | 9976 KiB |
| `w7_collections_nested_k3` | 2.08 ms | 2.30 ms | +10.3% | 0.00 ms | 0.00 ms | 9568 KiB | 10168 KiB |
| `w7_collections_nested_k4` | 2.59 ms | 2.96 ms | +14.3% | 0.00 ms | 0.00 ms | 9272 KiB | 10096 KiB |
| `w7_collections_nested_k5` | 3.06 ms | 3.59 ms | +17.3% | 0.00 ms | 0.00 ms | 9744 KiB | 10368 KiB |
| `w8_traits_dispatch_c010` | 8.85 ms | 9.75 ms | +10.2% | 0.00 ms | 0.00 ms | 10340 KiB | 11288 KiB |
| `w8_traits_dispatch_c025` | 20.19 ms | 22.99 ms | +13.9% | 10.00 ms | 20.00 ms | 11088 KiB | 12288 KiB |
| `w8_traits_dispatch_c050` | 34.28 ms | 45.07 ms | +31.5% | 30.00 ms | 40.00 ms | 12708 KiB | 14180 KiB |
| `w8_traits_dispatch_c100` | 69.74 ms | 88.03 ms | +26.2% | 60.00 ms | 80.00 ms | 15280 KiB | 18048 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 45.27 ms | 51.33 ms | +13.4% | 40.00 ms | 40.00 ms | 14260 KiB | 16628 KiB |
| `example_checkout` | 3.77 ms | 3.80 ms | +0.7% | 0.00 ms | 0.00 ms | 9836 KiB | 10652 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `rw_swarm_047_reduce_map` | 7.26 [7.09–7.74] ms | 10.59 [8.62–11.11] ms | +3.33 ms | +45.8% | 10.00 ms | 10664 KiB |
| `w8_traits_dispatch_c050` | 34.28 [33.92–34.53] ms | 45.07 [43.09–46.78] ms | +10.79 ms | +31.5% | 40.00 ms | 14180 KiB |
| `w5_infer_wide_w025` | 29.08 [28.54–31.97] ms | 37.22 [32.73–39.01] ms | +8.14 ms | +28.0% | 30.00 ms | 12588 KiB |
| `w8_traits_dispatch_c100` | 69.74 [68.46–77.46] ms | 88.03 [86.47–88.30] ms | +18.28 ms | +26.2% | 80.00 ms | 18048 KiB |
| `rw_phase106d1_read_projection` | 8.70 [8.57–8.90] ms | 10.90 [9.70–13.88] ms | +2.20 ms | +25.2% | 10.00 ms | 11536 KiB |
| `w5_infer_wide_w050` | 56.88 [53.22–64.94] ms | 69.24 [65.51–73.02] ms | +12.36 ms | +21.7% | 60.00 ms | 15172 KiB |
| `w6_types_count_t100` | 84.64 [82.48–90.27] ms | 101.25 [98.39–103.05] ms | +16.62 ms | +19.6% | 90.00 ms | 21688 KiB |
| `rw_phase4_functional` | 12.11 [11.58–13.32] ms | 14.30 [14.17–14.65] ms | +2.19 ms | +18.1% | 10.00 ms | 11376 KiB |
| `w5_infer_wide_w100` | 112.31 [106.76–115.37] ms | 129.70 [126.09–152.68] ms | +17.39 ms | +15.5% | 120.00 ms | 20116 KiB |
| `rw_sdk_datastructures` | 228.86 [221.35–240.83] ms | 262.89 [252.22–265.19] ms | +34.03 ms | +14.9% | 260.00 ms | 22992 KiB |
| `rw_projects_rolling_window` | 45.27 [43.92–47.27] ms | 51.33 [49.59–53.87] ms | +6.05 ms | +13.4% | 40.00 ms | 16628 KiB |
| `rw_projects_cache` | 29.83 [27.53–32.63] ms | 33.64 [33.23–34.41] ms | +3.81 ms | +12.8% | 30.00 ms | 14500 KiB |
| `w2_spec_distinct_n008` | 86.88 [84.80–87.42] ms | 97.60 [93.76–99.47] ms | +10.72 ms | +12.3% | 90.00 ms | 14008 KiB |
| `w2_spec_distinct_n032` | 1802.21 [1756.01–1807.20] ms | 2014.67 [1987.56–2017.39] ms | +212.46 ms | +11.8% | 2010.00 ms | 59216 KiB |
| `rw_packages_regex` | 632.37 [632.09–640.32] ms | 673.76 [654.35–680.87] ms | +41.40 ms | +6.5% | 660.00 ms | 29848 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `rw_phase106d_handler_2pl` | 6.15 ms | 7.74 ms | +1.59 ms | +25.8% |
| `example_functional_basics` | 2.54 ms | 3.09 ms | +0.55 ms | +21.7% |
| `w7_collections_nested_k1` | 1.62 ms | 1.97 ms | +0.35 ms | +21.6% |
| `rw_phase46_semantic_barriers` | 6.06 ms | 7.33 ms | +1.28 ms | +21.1% |
| `rw_phase47_iteration` | 7.15 ms | 8.58 ms | +1.43 ms | +20.0% |
| `rw_example_pipeline` | 1.55 ms | 1.85 ms | +0.30 ms | +19.1% |
| `w6_types_count_t025` | 15.88 ms | 18.86 ms | +2.99 ms | +18.8% |
| `w4_infer_deep_d018` | 26.79 ms | 31.68 ms | +4.90 ms | +18.3% |
| `example_domains_and_messages` | 1.81 ms | 2.14 ms | +0.33 ms | +18.1% |
| `w4_infer_deep_d010` | 11.25 ms | 13.22 ms | +1.96 ms | +17.5% |
| `w7_collections_nested_k5` | 3.06 ms | 3.59 ms | +0.53 ms | +17.3% |
| `rw_swarm_066_for_range` | 7.52 ms | 8.79 ms | +1.28 ms | +17.0% |
| `rw_phase25_integer_overflow` | 5.45 ms | 6.37 ms | +0.92 ms | +16.8% |
| `test_phase45_short_circuit_failure_barrier` | 1.97 ms | 2.29 ms | +0.32 ms | +16.0% |
| `rw_phase24_string_concat` | 5.18 ms | 6.00 ms | +0.82 ms | +15.8% |
| `w7_collections_nested_k4` | 2.59 ms | 2.96 ms | +0.37 ms | +14.3% |
| `example_functional_dataflow` | 2.89 ms | 3.30 ms | +0.41 ms | +14.1% |
| `rw_example_collections` | 2.90 ms | 3.31 ms | +0.40 ms | +13.9% |
| `w8_traits_dispatch_c025` | 20.19 ms | 22.99 ms | +2.80 ms | +13.9% |
| `mg1_multi_file` | 5.03 ms | 5.71 ms | +0.69 ms | +13.7% |
| `rw_tests_branch_confined_open` | 2.85 ms | 3.23 ms | +0.38 ms | +13.4% |
| `w6_types_count_t050` | 35.78 ms | 40.55 ms | +4.77 ms | +13.3% |
| `w6_types_count_t010` | 6.92 ms | 7.81 ms | +0.89 ms | +12.8% |
| `mg2_multi_module` | 5.51 ms | 6.19 ms | +0.68 ms | +12.3% |
| `rw_example_domains` | 1.90 ms | 2.12 ms | +0.23 ms | +12.1% |
| `rw_example_checkout` | 3.34 ms | 3.73 ms | +0.38 ms | +11.5% |
| `w7_collections_nested_k3` | 2.08 ms | 2.30 ms | +0.22 ms | +10.3% |
| `rw_phase15_15_enum_positive` | 5.21 ms | 5.75 ms | +0.53 ms | +10.2% |
| `w8_traits_dispatch_c010` | 8.85 ms | 9.75 ms | +0.90 ms | +10.2% |
| `example_functional_pipeline` | 1.61 ms | 1.78 ms | +0.16 ms | +10.1% |
| `negative_trait_missing_method` | 2.00 ms | 2.18 ms | +0.18 ms | +8.9% |
| `w4_infer_deep_d005` | 5.42 ms | 5.88 ms | +0.46 ms | +8.6% |
| `mg4_fan_out` | 8.87 ms | 9.62 ms | +0.75 ms | +8.4% |
| `example_static_duck_typing` | 2.59 ms | 2.80 ms | +0.21 ms | +8.0% |
| `negative_domain_route_cycle` | 1.25 ms | 1.35 ms | +0.10 ms | +7.9% |
| `negative_duck_missing_method` | 1.61 ms | 1.74 ms | +0.13 ms | +7.9% |
| `example_domain_typing` | 3.51 ms | 3.77 ms | +0.25 ms | +7.2% |
| `example_shared_memory` | 2.03 ms | 2.14 ms | +0.11 ms | +5.6% |
| `negative_spawn_retired` | 1.19 ms | 1.26 ms | +0.06 ms | +5.4% |
| `w3_spec_repeated_n016` | 224.92 ms | 236.92 ms | +12.00 ms | +5.3% |

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
