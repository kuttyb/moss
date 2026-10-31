# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.6611328125, 3.10498046875, 3.86572265625)`; after profiling `(1.34912109375, 2.82275390625, 3.73291015625)`.
- Candidate benchmark checkout SHA: `ce5f836e06ce93909c358e6e6455402417d14991`
- Candidate compiler source SHA: `ce5f836e06ce93909c358e6e6455402417d14991`
- Candidate binary SHA256: `36588000224913f59fc9ef5559a9039717841311f7ca9a1750b97fa3aa1b5dc9`
- Certification mode: `False`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `a71a1c52fd7f6e99ba33d9dde70e3531a033def6` resolved to `a71a1c52fd7f6e99ba33d9dde70e3531a033def6`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.163 s | 5.435 s | +5.3% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.48 ms | 3.85 ms | +10.5% | 0.00 ms | 0.00 ms | 9988 KiB | 10556 KiB |
| `rw_packages_regex` | 643.86 ms | 671.41 ms | +4.3% | 630.00 ms | 660.00 ms | 25544 KiB | 29856 KiB |
| `rw_projects_rolling_window` | 42.82 ms | 50.91 ms | +18.9% | 40.00 ms | 50.00 ms | 14404 KiB | 16616 KiB |
| `rw_sdk_datastructures` | 235.95 ms | 257.01 ms | +8.9% | 230.00 ms | 250.00 ms | 20752 KiB | 23144 KiB |
| `w2_spec_distinct_n032` | 1844.71 ms | 1918.54 ms | +4.0% | 1830.00 ms | 1910.00 ms | 36872 KiB | 59444 KiB |
| `w3_spec_repeated_n032` | 869.81 ms | 890.40 ms | +2.4% | 850.00 ms | 880.00 ms | 24492 KiB | 27880 KiB |
| `w4_infer_deep_d018` | 24.72 ms | 27.83 ms | +12.6% | 20.00 ms | 20.00 ms | 10980 KiB | 11816 KiB |
| `w5_infer_wide_w100` | 111.74 ms | 127.07 ms | +13.7% | 110.00 ms | 120.00 ms | 17072 KiB | 20084 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.48 ms | 3.85 ms | +10.5% | 0.00 ms | 0.00 ms | 9988 KiB | 10556 KiB |
| `example_collections` | 2.90 ms | 3.41 ms | +17.7% | 0.00 ms | 0.00 ms | 10244 KiB | 10744 KiB |
| `example_counter` | 1.64 ms | 1.85 ms | +12.8% | 0.00 ms | 0.00 ms | 9656 KiB | 10288 KiB |
| `example_domain_typing` | 3.52 ms | 3.98 ms | +13.3% | 0.00 ms | 0.00 ms | 9940 KiB | 10612 KiB |
| `example_domains_and_messages` | 1.90 ms | 2.01 ms | +6.0% | 0.00 ms | 0.00 ms | 9616 KiB | 10252 KiB |
| `example_functional_basics` | 2.65 ms | 3.20 ms | +20.6% | 0.00 ms | 0.00 ms | 9740 KiB | 10460 KiB |
| `example_functional_dataflow` | 3.14 ms | 3.42 ms | +8.9% | 0.00 ms | 0.00 ms | 9804 KiB | 10596 KiB |
| `example_functional_pipeline` | 1.65 ms | 1.89 ms | +14.2% | 0.00 ms | 0.00 ms | 9504 KiB | 10236 KiB |
| `example_shared_memory` | 1.98 ms | 2.21 ms | +11.6% | 0.00 ms | 0.00 ms | 9548 KiB | 10248 KiB |
| `example_static_duck_typing` | 2.80 ms | 2.82 ms | +0.9% | 0.00 ms | 0.00 ms | 9976 KiB | 10520 KiB |
| `mg1_multi_file` | 5.74 ms | 5.55 ms | -3.2% | 0.00 ms | 0.00 ms | 10928 KiB | 11648 KiB |
| `mg2_multi_module` | 5.47 ms | 5.88 ms | +7.3% | 0.00 ms | 0.00 ms | 10856 KiB | 11600 KiB |
| `mg3_deep_chain` | 4.38 ms | 4.92 ms | +12.2% | 0.00 ms | 0.00 ms | 10740 KiB | 11388 KiB |
| `mg4_fan_out` | 8.51 ms | 9.00 ms | +5.7% | 0.00 ms | 0.00 ms | 10836 KiB | 11524 KiB |
| `mg5_diamond` | 3.72 ms | 3.80 ms | +2.3% | 0.00 ms | 0.00 ms | 10448 KiB | 10888 KiB |
| `negative_await_retired` | 1.09 ms | 1.01 ms | -7.1% | 0.00 ms | 0.00 ms | 7180 KiB | 7168 KiB |
| `negative_domain_route_cycle` | 1.26 ms | 1.41 ms | +11.4% | 0.00 ms | 0.00 ms | 9348 KiB | 9560 KiB |
| `negative_duck_missing_method` | 1.58 ms | 1.74 ms | +10.1% | 0.00 ms | 0.00 ms | 9944 KiB | 10368 KiB |
| `negative_heterogeneous_collection` | 1.25 ms | 1.39 ms | +11.6% | 0.00 ms | 0.00 ms | 9680 KiB | 9720 KiB |
| `negative_mutually_recursive_functions` | 1.30 ms | 1.39 ms | +6.8% | 0.00 ms | 0.00 ms | 9168 KiB | 9444 KiB |
| `negative_self_send_rejected` | 1.41 ms | 1.52 ms | +7.8% | 0.00 ms | 0.00 ms | 9656 KiB | 9876 KiB |
| `negative_spawn_retired` | 1.19 ms | 1.25 ms | +5.0% | 0.00 ms | 0.00 ms | 9292 KiB | 9380 KiB |
| `negative_trait_missing_method` | 2.11 ms | 2.19 ms | +3.8% | 0.00 ms | 0.00 ms | 10272 KiB | 10516 KiB |
| `negative_write_write_alias` | 1.78 ms | 1.88 ms | +5.6% | 0.00 ms | 0.00 ms | 10104 KiB | 10332 KiB |
| `rw_example_checkout` | 3.38 ms | 3.82 ms | +13.0% | 0.00 ms | 0.00 ms | 9660 KiB | 10500 KiB |
| `rw_example_collections` | 3.10 ms | 3.42 ms | +10.3% | 0.00 ms | 0.00 ms | 10188 KiB | 10720 KiB |
| `rw_example_counter` | 1.80 ms | 1.98 ms | +9.9% | 0.00 ms | 0.00 ms | 9616 KiB | 10176 KiB |
| `rw_example_domains` | 1.88 ms | 2.16 ms | +15.0% | 0.00 ms | 0.00 ms | 9536 KiB | 10240 KiB |
| `rw_example_pipeline` | 1.61 ms | 1.80 ms | +11.6% | 0.00 ms | 0.00 ms | 9428 KiB | 10108 KiB |
| `rw_packages_regex` | 643.86 ms | 671.41 ms | +4.3% | 630.00 ms | 660.00 ms | 25544 KiB | 29856 KiB |
| `rw_phase106d1_borrowed_reads` | 12.18 ms | 12.26 ms | +0.6% | 10.00 ms | 10.00 ms | 10512 KiB | 11400 KiB |
| `rw_phase106d1_read_projection` | 10.06 ms | 10.30 ms | +2.3% | 0.00 ms | 0.00 ms | 10440 KiB | 11404 KiB |
| `rw_phase106d_handler_2pl` | 6.55 ms | 7.17 ms | +9.6% | 0.00 ms | 0.00 ms | 9936 KiB | 10832 KiB |
| `rw_phase15_15_enum_positive` | 5.02 ms | 5.91 ms | +17.7% | 0.00 ms | 0.00 ms | 10040 KiB | 10824 KiB |
| `rw_phase20_fileio_helper` | 2.24 ms | 2.78 ms | +24.0% | 0.00 ms | 0.00 ms | 9716 KiB | 10128 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.84 ms | 2.99 ms | +5.1% | 0.00 ms | 0.00 ms | 9696 KiB | 10452 KiB |
| `rw_phase24_string_concat` | 5.06 ms | 5.78 ms | +14.0% | 0.00 ms | 0.00 ms | 10256 KiB | 11048 KiB |
| `rw_phase25_integer_overflow` | 5.36 ms | 5.90 ms | +10.0% | 0.00 ms | 0.00 ms | 10048 KiB | 10684 KiB |
| `rw_phase46_semantic_barriers` | 6.06 ms | 7.28 ms | +20.1% | 0.00 ms | 0.00 ms | 10088 KiB | 10780 KiB |
| `rw_phase47_iteration` | 7.46 ms | 8.00 ms | +7.2% | 0.00 ms | 0.00 ms | 10084 KiB | 10848 KiB |
| `rw_phase4_functional` | 11.81 ms | 14.32 ms | +21.2% | 10.00 ms | 10.00 ms | 10704 KiB | 11512 KiB |
| `rw_projects_cache` | 25.35 ms | 30.67 ms | +21.0% | 20.00 ms | 20.00 ms | 12960 KiB | 14388 KiB |
| `rw_projects_rolling_window` | 42.82 ms | 50.91 ms | +18.9% | 40.00 ms | 50.00 ms | 14404 KiB | 16616 KiB |
| `rw_sdk_datastructures` | 235.95 ms | 257.01 ms | +8.9% | 230.00 ms | 250.00 ms | 20752 KiB | 23144 KiB |
| `rw_swarm_047_reduce_map` | 8.65 ms | 8.89 ms | +2.8% | 0.00 ms | 0.00 ms | 10172 KiB | 10868 KiB |
| `rw_swarm_066_for_range` | 7.93 ms | 9.22 ms | +16.2% | 0.00 ms | 0.00 ms | 10288 KiB | 10956 KiB |
| `rw_tests_branch_confined_open` | 2.96 ms | 3.45 ms | +16.7% | 0.00 ms | 0.00 ms | 9744 KiB | 10520 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.09 ms | 2.43 ms | +16.3% | 0.00 ms | 0.00 ms | 9692 KiB | 10220 KiB |
| `test_trait_open_forwarding` | 2.68 ms | 2.98 ms | +11.0% | 0.00 ms | 0.00 ms | 10508 KiB | 10964 KiB |
| `test_unused_inferred_field_template` | 1.34 ms | 1.43 ms | +7.0% | 0.00 ms | 0.00 ms | 9332 KiB | 9880 KiB |
| `w2_spec_distinct_n008` | 88.05 ms | 92.46 ms | +5.0% | 80.00 ms | 90.00 ms | 12036 KiB | 13988 KiB |
| `w2_spec_distinct_n016` | 381.49 ms | 401.67 ms | +5.3% | 370.00 ms | 390.00 ms | 17924 KiB | 23340 KiB |
| `w2_spec_distinct_n032` | 1844.71 ms | 1918.54 ms | +4.0% | 1830.00 ms | 1910.00 ms | 36872 KiB | 59444 KiB |
| `w3_spec_repeated_n008` | 59.63 ms | 61.87 ms | +3.8% | 50.00 ms | 50.00 ms | 11048 KiB | 11872 KiB |
| `w3_spec_repeated_n016` | 228.90 ms | 226.21 ms | -1.2% | 220.00 ms | 220.00 ms | 13556 KiB | 15096 KiB |
| `w3_spec_repeated_n032` | 869.81 ms | 890.40 ms | +2.4% | 850.00 ms | 880.00 ms | 24492 KiB | 27880 KiB |
| `w4_infer_deep_d005` | 6.07 ms | 5.97 ms | -1.7% | 0.00 ms | 0.00 ms | 10212 KiB | 10644 KiB |
| `w4_infer_deep_d010` | 11.29 ms | 12.39 ms | +9.8% | 10.00 ms | 10.00 ms | 10464 KiB | 11128 KiB |
| `w4_infer_deep_d015` | 21.67 ms | 20.30 ms | -6.3% | 20.00 ms | 20.00 ms | 10864 KiB | 11480 KiB |
| `w4_infer_deep_d018` | 24.72 ms | 27.83 ms | +12.6% | 20.00 ms | 20.00 ms | 10980 KiB | 11816 KiB |
| `w5_infer_wide_w010` | 12.41 ms | 13.82 ms | +11.4% | 10.00 ms | 10.00 ms | 10564 KiB | 11488 KiB |
| `w5_infer_wide_w025` | 30.84 ms | 33.27 ms | +7.9% | 30.00 ms | 30.00 ms | 11816 KiB | 12696 KiB |
| `w5_infer_wide_w050` | 57.65 ms | 66.00 ms | +14.5% | 50.00 ms | 60.00 ms | 13476 KiB | 15216 KiB |
| `w5_infer_wide_w100` | 111.74 ms | 127.07 ms | +13.7% | 110.00 ms | 120.00 ms | 17072 KiB | 20084 KiB |
| `w6_types_count_t010` | 6.98 ms | 8.10 ms | +16.1% | 0.00 ms | 0.00 ms | 10056 KiB | 11084 KiB |
| `w6_types_count_t025` | 15.22 ms | 17.84 ms | +17.2% | 10.00 ms | 10.00 ms | 11444 KiB | 12372 KiB |
| `w6_types_count_t050` | 33.69 ms | 41.85 ms | +24.3% | 30.00 ms | 30.00 ms | 13248 KiB | 15220 KiB |
| `w6_types_count_t100` | 86.35 ms | 103.44 ms | +19.8% | 80.00 ms | 90.00 ms | 18400 KiB | 21848 KiB |
| `w7_collections_nested_k1` | 1.51 ms | 1.78 ms | +18.3% | 0.00 ms | 0.00 ms | 9420 KiB | 9896 KiB |
| `w7_collections_nested_k2` | 1.64 ms | 1.87 ms | +14.3% | 0.00 ms | 0.00 ms | 9516 KiB | 10112 KiB |
| `w7_collections_nested_k3` | 1.97 ms | 2.25 ms | +14.6% | 0.00 ms | 0.00 ms | 9652 KiB | 10132 KiB |
| `w7_collections_nested_k4` | 2.44 ms | 2.82 ms | +15.4% | 0.00 ms | 0.00 ms | 9396 KiB | 10192 KiB |
| `w7_collections_nested_k5` | 3.08 ms | 3.38 ms | +9.9% | 0.00 ms | 0.00 ms | 9744 KiB | 10324 KiB |
| `w8_traits_dispatch_c010` | 8.35 ms | 9.10 ms | +8.9% | 0.00 ms | 0.00 ms | 10428 KiB | 11264 KiB |
| `w8_traits_dispatch_c025` | 18.22 ms | 19.60 ms | +7.6% | 10.00 ms | 10.00 ms | 11088 KiB | 12388 KiB |
| `w8_traits_dispatch_c050` | 35.17 ms | 39.55 ms | +12.5% | 30.00 ms | 30.00 ms | 12636 KiB | 14456 KiB |
| `w8_traits_dispatch_c100` | 72.61 ms | 79.53 ms | +9.5% | 60.00 ms | 70.00 ms | 15236 KiB | 18196 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 42.82 ms | 50.91 ms | +18.9% | 40.00 ms | 50.00 ms | 14404 KiB | 16616 KiB |
| `example_checkout` | 3.48 ms | 3.85 ms | +10.5% | 0.00 ms | 0.00 ms | 9988 KiB | 10556 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `w6_types_count_t050` | 33.69 [32.71–38.50] ms | 41.85 [38.72–42.36] ms | +8.17 ms | +24.3% | 30.00 ms | 15220 KiB |
| `rw_phase4_functional` | 11.81 [11.64–11.88] ms | 14.32 [13.96–14.63] ms | +2.51 ms | +21.2% | 10.00 ms | 11512 KiB |
| `rw_projects_cache` | 25.35 [25.16–26.37] ms | 30.67 [28.93–31.00] ms | +5.32 ms | +21.0% | 20.00 ms | 14388 KiB |
| `w6_types_count_t100` | 86.35 [84.62–94.17] ms | 103.44 [100.03–106.48] ms | +17.09 ms | +19.8% | 90.00 ms | 21848 KiB |
| `rw_projects_rolling_window` | 42.82 [42.19–48.67] ms | 50.91 [49.10–51.63] ms | +8.09 ms | +18.9% | 50.00 ms | 16616 KiB |
| `w5_infer_wide_w050` | 57.65 [54.48–58.62] ms | 66.00 [65.02–69.94] ms | +8.35 ms | +14.5% | 60.00 ms | 15216 KiB |
| `w5_infer_wide_w100` | 111.74 [111.07–112.37] ms | 127.07 [126.17–133.22] ms | +15.33 ms | +13.7% | 120.00 ms | 20084 KiB |
| `w8_traits_dispatch_c100` | 72.61 [68.92–73.11] ms | 79.53 [76.50–84.95] ms | +6.92 ms | +9.5% | 70.00 ms | 18196 KiB |
| `rw_sdk_datastructures` | 235.95 [228.66–239.77] ms | 257.01 [253.29–261.44] ms | +21.06 ms | +8.9% | 250.00 ms | 23144 KiB |
| `w2_spec_distinct_n016` | 381.49 [368.49–382.51] ms | 401.67 [393.10–405.11] ms | +20.19 ms | +5.3% | 390.00 ms | 23340 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `rw_phase20_fileio_helper` | 2.24 ms | 2.78 ms | +0.54 ms | +24.0% |
| `example_functional_basics` | 2.65 ms | 3.20 ms | +0.55 ms | +20.6% |
| `rw_phase46_semantic_barriers` | 6.06 ms | 7.28 ms | +1.22 ms | +20.1% |
| `w7_collections_nested_k1` | 1.51 ms | 1.78 ms | +0.28 ms | +18.3% |
| `rw_phase15_15_enum_positive` | 5.02 ms | 5.91 ms | +0.89 ms | +17.7% |
| `example_collections` | 2.90 ms | 3.41 ms | +0.51 ms | +17.7% |
| `w6_types_count_t025` | 15.22 ms | 17.84 ms | +2.62 ms | +17.2% |
| `rw_tests_branch_confined_open` | 2.96 ms | 3.45 ms | +0.49 ms | +16.7% |
| `test_phase45_short_circuit_failure_barrier` | 2.09 ms | 2.43 ms | +0.34 ms | +16.3% |
| `rw_swarm_066_for_range` | 7.93 ms | 9.22 ms | +1.29 ms | +16.2% |
| `w6_types_count_t010` | 6.98 ms | 8.10 ms | +1.12 ms | +16.1% |
| `w7_collections_nested_k4` | 2.44 ms | 2.82 ms | +0.38 ms | +15.4% |
| `rw_example_domains` | 1.88 ms | 2.16 ms | +0.28 ms | +15.0% |
| `w7_collections_nested_k3` | 1.97 ms | 2.25 ms | +0.29 ms | +14.6% |
| `w7_collections_nested_k2` | 1.64 ms | 1.87 ms | +0.24 ms | +14.3% |
| `example_functional_pipeline` | 1.65 ms | 1.89 ms | +0.23 ms | +14.2% |
| `rw_phase24_string_concat` | 5.06 ms | 5.78 ms | +0.71 ms | +14.0% |
| `example_domain_typing` | 3.52 ms | 3.98 ms | +0.47 ms | +13.3% |
| `rw_example_checkout` | 3.38 ms | 3.82 ms | +0.44 ms | +13.0% |
| `example_counter` | 1.64 ms | 1.85 ms | +0.21 ms | +12.8% |
| `w4_infer_deep_d018` | 24.72 ms | 27.83 ms | +3.11 ms | +12.6% |
| `w8_traits_dispatch_c050` | 35.17 ms | 39.55 ms | +4.38 ms | +12.5% |
| `mg3_deep_chain` | 4.38 ms | 4.92 ms | +0.54 ms | +12.2% |
| `negative_heterogeneous_collection` | 1.25 ms | 1.39 ms | +0.15 ms | +11.6% |
| `rw_example_pipeline` | 1.61 ms | 1.80 ms | +0.19 ms | +11.6% |
| `example_shared_memory` | 1.98 ms | 2.21 ms | +0.23 ms | +11.6% |
| `negative_domain_route_cycle` | 1.26 ms | 1.41 ms | +0.14 ms | +11.4% |
| `w5_infer_wide_w010` | 12.41 ms | 13.82 ms | +1.41 ms | +11.4% |
| `test_trait_open_forwarding` | 2.68 ms | 2.98 ms | +0.30 ms | +11.0% |
| `example_checkout` | 3.48 ms | 3.85 ms | +0.37 ms | +10.5% |
| `rw_example_collections` | 3.10 ms | 3.42 ms | +0.32 ms | +10.3% |
| `negative_duck_missing_method` | 1.58 ms | 1.74 ms | +0.16 ms | +10.1% |
| `rw_phase25_integer_overflow` | 5.36 ms | 5.90 ms | +0.54 ms | +10.0% |
| `w7_collections_nested_k5` | 3.08 ms | 3.38 ms | +0.30 ms | +9.9% |
| `rw_example_counter` | 1.80 ms | 1.98 ms | +0.18 ms | +9.9% |
| `w4_infer_deep_d010` | 11.29 ms | 12.39 ms | +1.11 ms | +9.8% |
| `rw_phase106d_handler_2pl` | 6.55 ms | 7.17 ms | +0.63 ms | +9.6% |
| `example_functional_dataflow` | 3.14 ms | 3.42 ms | +0.28 ms | +8.9% |
| `w8_traits_dispatch_c010` | 8.35 ms | 9.10 ms | +0.74 ms | +8.9% |
| `w5_infer_wide_w025` | 30.84 ms | 33.27 ms | +2.43 ms | +7.9% |
| `negative_self_send_rejected` | 1.41 ms | 1.52 ms | +0.11 ms | +7.8% |
| `w8_traits_dispatch_c025` | 18.22 ms | 19.60 ms | +1.38 ms | +7.6% |
| `mg2_multi_module` | 5.47 ms | 5.88 ms | +0.40 ms | +7.3% |
| `rw_phase47_iteration` | 7.46 ms | 8.00 ms | +0.54 ms | +7.2% |
| `test_unused_inferred_field_template` | 1.34 ms | 1.43 ms | +0.09 ms | +7.0% |
| `negative_mutually_recursive_functions` | 1.30 ms | 1.39 ms | +0.09 ms | +6.8% |
| `example_domains_and_messages` | 1.90 ms | 2.01 ms | +0.11 ms | +6.0% |
| `mg4_fan_out` | 8.51 ms | 9.00 ms | +0.49 ms | +5.7% |
| `negative_write_write_alias` | 1.78 ms | 1.88 ms | +0.10 ms | +5.6% |
| `rw_phase21_fileio_root_cleanup` | 2.84 ms | 2.99 ms | +0.15 ms | +5.1% |
| `negative_spawn_retired` | 1.19 ms | 1.25 ms | +0.06 ms | +5.0% |
| `w2_spec_distinct_n008` | 88.05 ms | 92.46 ms | +4.41 ms | +5.0% |

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
