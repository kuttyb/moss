# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.88720703125, 2.5263671875, 3.5205078125)`; after profiling `(1.4169921875, 2.310546875, 3.3994140625)`.
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

- Baseline: `5627727657ad7d26d860d4824b4ba03764252b0e` resolved to `5627727657ad7d26d860d4824b4ba03764252b0e`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.099 s | 5.445 s | +6.8% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.39 ms | 4.16 ms | +22.6% | 0.00 ms | 0.00 ms | 9636 KiB | 10436 KiB |
| `rw_packages_regex` | 650.97 ms | 687.75 ms | +5.7% | 640.00 ms | 680.00 ms | 24560 KiB | 30020 KiB |
| `rw_projects_rolling_window` | 43.16 ms | 50.16 ms | +16.2% | 40.00 ms | 40.00 ms | 13932 KiB | 16672 KiB |
| `rw_sdk_datastructures` | 230.01 ms | 258.35 ms | +12.3% | 220.00 ms | 250.00 ms | 18896 KiB | 23080 KiB |
| `w2_spec_distinct_n032` | 1764.97 ms | 1909.03 ms | +8.2% | 1760.00 ms | 1890.00 ms | 35128 KiB | 59324 KiB |
| `w3_spec_repeated_n032` | 892.41 ms | 892.43 ms | +0.0% | 880.00 ms | 890.00 ms | 22872 KiB | 27876 KiB |
| `w4_infer_deep_d018` | 24.93 ms | 28.24 ms | +13.3% | 20.00 ms | 20.00 ms | 10660 KiB | 11812 KiB |
| `w5_infer_wide_w100` | 111.80 ms | 132.70 ms | +18.7% | 100.00 ms | 120.00 ms | 16940 KiB | 20056 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.39 ms | 4.16 ms | +22.6% | 0.00 ms | 0.00 ms | 9636 KiB | 10436 KiB |
| `example_collections` | 3.33 ms | 4.64 ms | +39.3% | 0.00 ms | 0.00 ms | 10232 KiB | 10920 KiB |
| `example_counter` | 1.67 ms | 2.53 ms | +51.3% | 0.00 ms | 0.00 ms | 9416 KiB | 10076 KiB |
| `example_domain_typing` | 3.69 ms | 4.17 ms | +12.8% | 0.00 ms | 0.00 ms | 9724 KiB | 10560 KiB |
| `example_domains_and_messages` | 1.88 ms | 2.13 ms | +13.6% | 0.00 ms | 0.00 ms | 9580 KiB | 10156 KiB |
| `example_functional_basics` | 2.66 ms | 3.10 ms | +16.5% | 0.00 ms | 0.00 ms | 9428 KiB | 10448 KiB |
| `example_functional_dataflow` | 2.81 ms | 3.34 ms | +18.8% | 0.00 ms | 0.00 ms | 9644 KiB | 10484 KiB |
| `example_functional_pipeline` | 1.50 ms | 1.69 ms | +12.8% | 0.00 ms | 0.00 ms | 9472 KiB | 10196 KiB |
| `example_shared_memory` | 2.07 ms | 2.33 ms | +12.4% | 0.00 ms | 0.00 ms | 9668 KiB | 10232 KiB |
| `example_static_duck_typing` | 2.56 ms | 2.93 ms | +14.6% | 0.00 ms | 0.00 ms | 9920 KiB | 10704 KiB |
| `mg1_multi_file` | 4.81 ms | 5.42 ms | +12.9% | 0.00 ms | 0.00 ms | 10692 KiB | 11468 KiB |
| `mg2_multi_module` | 5.20 ms | 5.97 ms | +14.9% | 0.00 ms | 0.00 ms | 10596 KiB | 11560 KiB |
| `mg3_deep_chain` | 4.04 ms | 4.60 ms | +13.7% | 0.00 ms | 0.00 ms | 10452 KiB | 11388 KiB |
| `mg4_fan_out` | 7.92 ms | 9.33 ms | +17.7% | 0.00 ms | 0.00 ms | 10820 KiB | 11776 KiB |
| `mg5_diamond` | 3.52 ms | 3.90 ms | +10.8% | 0.00 ms | 0.00 ms | 10304 KiB | 11216 KiB |
| `negative_await_retired` | 1.02 ms | 1.01 ms | -0.8% | 0.00 ms | 0.00 ms | 7220 KiB | 7144 KiB |
| `negative_domain_route_cycle` | 1.26 ms | 1.33 ms | +5.1% | 0.00 ms | 0.00 ms | 9360 KiB | 9596 KiB |
| `negative_duck_missing_method` | 1.68 ms | 1.80 ms | +6.7% | 0.00 ms | 0.00 ms | 9976 KiB | 10340 KiB |
| `negative_heterogeneous_collection` | 1.26 ms | 1.42 ms | +12.9% | 0.00 ms | 0.00 ms | 9532 KiB | 9868 KiB |
| `negative_mutually_recursive_functions` | 1.36 ms | 1.38 ms | +1.2% | 0.00 ms | 0.00 ms | 9336 KiB | 9432 KiB |
| `negative_self_send_rejected` | 1.25 ms | 1.31 ms | +4.8% | 0.00 ms | 0.00 ms | 9300 KiB | 9888 KiB |
| `negative_spawn_retired` | 1.20 ms | 1.35 ms | +13.0% | 0.00 ms | 0.00 ms | 9600 KiB | 9644 KiB |
| `negative_trait_missing_method` | 2.76 ms | 3.03 ms | +9.7% | 0.00 ms | 0.00 ms | 9940 KiB | 10432 KiB |
| `negative_write_write_alias` | 1.75 ms | 2.16 ms | +23.5% | 0.00 ms | 0.00 ms | 10316 KiB | 10440 KiB |
| `rw_example_checkout` | 3.48 ms | 3.74 ms | +7.4% | 0.00 ms | 0.00 ms | 9736 KiB | 10536 KiB |
| `rw_example_collections` | 2.87 ms | 3.38 ms | +18.0% | 0.00 ms | 0.00 ms | 11608 KiB | 10788 KiB |
| `rw_example_counter` | 2.11 ms | 1.89 ms | -10.7% | 0.00 ms | 0.00 ms | 9432 KiB | 10336 KiB |
| `rw_example_domains` | 2.00 ms | 2.04 ms | +2.2% | 0.00 ms | 0.00 ms | 9388 KiB | 10236 KiB |
| `rw_example_pipeline` | 1.54 ms | 1.73 ms | +12.2% | 0.00 ms | 0.00 ms | 9260 KiB | 10068 KiB |
| `rw_packages_regex` | 650.97 ms | 687.75 ms | +5.7% | 640.00 ms | 680.00 ms | 24560 KiB | 30020 KiB |
| `rw_phase106d1_borrowed_reads` | 11.67 ms | 12.88 ms | +10.4% | 0.00 ms | 10.00 ms | 10340 KiB | 11336 KiB |
| `rw_phase106d1_read_projection` | 8.83 ms | 10.28 ms | +16.4% | 0.00 ms | 0.00 ms | 10308 KiB | 11344 KiB |
| `rw_phase106d_handler_2pl` | 6.79 ms | 7.09 ms | +4.5% | 0.00 ms | 0.00 ms | 9792 KiB | 10680 KiB |
| `rw_phase15_15_enum_positive` | 5.02 ms | 5.85 ms | +16.4% | 0.00 ms | 0.00 ms | 11588 KiB | 10852 KiB |
| `rw_phase20_fileio_helper` | 2.27 ms | 2.42 ms | +6.8% | 0.00 ms | 0.00 ms | 9444 KiB | 10348 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.75 ms | 2.84 ms | +3.6% | 0.00 ms | 0.00 ms | 9668 KiB | 10492 KiB |
| `rw_phase24_string_concat` | 4.97 ms | 5.81 ms | +17.0% | 0.00 ms | 0.00 ms | 10060 KiB | 11032 KiB |
| `rw_phase25_integer_overflow` | 5.38 ms | 6.22 ms | +15.6% | 0.00 ms | 0.00 ms | 9788 KiB | 10844 KiB |
| `rw_phase46_semantic_barriers` | 5.49 ms | 7.61 ms | +38.6% | 0.00 ms | 0.00 ms | 9684 KiB | 10964 KiB |
| `rw_phase47_iteration` | 7.00 ms | 8.02 ms | +14.7% | 0.00 ms | 0.00 ms | 9908 KiB | 10808 KiB |
| `rw_phase4_functional` | 10.57 ms | 16.82 ms | +59.1% | 10.00 ms | 10.00 ms | 10440 KiB | 11476 KiB |
| `rw_projects_cache` | 26.25 ms | 29.76 ms | +13.4% | 20.00 ms | 20.00 ms | 12488 KiB | 14664 KiB |
| `rw_projects_rolling_window` | 43.16 ms | 50.16 ms | +16.2% | 40.00 ms | 40.00 ms | 13932 KiB | 16672 KiB |
| `rw_sdk_datastructures` | 230.01 ms | 258.35 ms | +12.3% | 220.00 ms | 250.00 ms | 18896 KiB | 23080 KiB |
| `rw_swarm_047_reduce_map` | 7.06 ms | 7.80 ms | +10.5% | 0.00 ms | 0.00 ms | 10076 KiB | 10748 KiB |
| `rw_swarm_066_for_range` | 7.50 ms | 8.35 ms | +11.4% | 0.00 ms | 0.00 ms | 9852 KiB | 10892 KiB |
| `rw_tests_branch_confined_open` | 2.62 ms | 3.41 ms | +30.0% | 0.00 ms | 0.00 ms | 9768 KiB | 10300 KiB |
| `test_phase45_short_circuit_failure_barrier` | 1.85 ms | 2.24 ms | +20.9% | 0.00 ms | 0.00 ms | 9656 KiB | 10328 KiB |
| `test_trait_open_forwarding` | 2.61 ms | 2.90 ms | +11.1% | 0.00 ms | 0.00 ms | 10660 KiB | 10948 KiB |
| `test_unused_inferred_field_template` | 1.29 ms | 1.44 ms | +11.1% | 0.00 ms | 0.00 ms | 9268 KiB | 9880 KiB |
| `w2_spec_distinct_n008` | 90.99 ms | 92.19 ms | +1.3% | 80.00 ms | 80.00 ms | 11784 KiB | 13820 KiB |
| `w2_spec_distinct_n016` | 374.55 ms | 383.23 ms | +2.3% | 370.00 ms | 370.00 ms | 17528 KiB | 23260 KiB |
| `w2_spec_distinct_n032` | 1764.97 ms | 1909.03 ms | +8.2% | 1760.00 ms | 1890.00 ms | 35128 KiB | 59324 KiB |
| `w3_spec_repeated_n008` | 61.20 ms | 63.63 ms | +4.0% | 50.00 ms | 60.00 ms | 10952 KiB | 11952 KiB |
| `w3_spec_repeated_n016` | 226.55 ms | 235.40 ms | +3.9% | 220.00 ms | 230.00 ms | 13316 KiB | 15104 KiB |
| `w3_spec_repeated_n032` | 892.41 ms | 892.43 ms | +0.0% | 880.00 ms | 890.00 ms | 22872 KiB | 27876 KiB |
| `w4_infer_deep_d005` | 6.06 ms | 6.53 ms | +7.7% | 0.00 ms | 0.00 ms | 10128 KiB | 10704 KiB |
| `w4_infer_deep_d010` | 12.06 ms | 12.06 ms | -0.1% | 10.00 ms | 10.00 ms | 10212 KiB | 11028 KiB |
| `w4_infer_deep_d015` | 19.38 ms | 22.00 ms | +13.5% | 10.00 ms | 20.00 ms | 10448 KiB | 11512 KiB |
| `w4_infer_deep_d018` | 24.93 ms | 28.24 ms | +13.3% | 20.00 ms | 20.00 ms | 10660 KiB | 11812 KiB |
| `w5_infer_wide_w010` | 12.23 ms | 16.62 ms | +35.9% | 10.00 ms | 10.00 ms | 10480 KiB | 11520 KiB |
| `w5_infer_wide_w025` | 30.28 ms | 32.41 ms | +7.0% | 20.00 ms | 30.00 ms | 11496 KiB | 12760 KiB |
| `w5_infer_wide_w050` | 57.35 ms | 68.79 ms | +20.0% | 50.00 ms | 60.00 ms | 13116 KiB | 15156 KiB |
| `w5_infer_wide_w100` | 111.80 ms | 132.70 ms | +18.7% | 100.00 ms | 120.00 ms | 16940 KiB | 20056 KiB |
| `w6_types_count_t010` | 6.59 ms | 7.46 ms | +13.2% | 0.00 ms | 0.00 ms | 10164 KiB | 11116 KiB |
| `w6_types_count_t025` | 15.90 ms | 18.63 ms | +17.2% | 10.00 ms | 10.00 ms | 11112 KiB | 12416 KiB |
| `w6_types_count_t050` | 35.60 ms | 39.84 ms | +11.9% | 30.00 ms | 30.00 ms | 13224 KiB | 15024 KiB |
| `w6_types_count_t100` | 89.53 ms | 95.89 ms | +7.1% | 80.00 ms | 90.00 ms | 17900 KiB | 21884 KiB |
| `w7_collections_nested_k1` | 1.55 ms | 1.75 ms | +12.8% | 0.00 ms | 0.00 ms | 9360 KiB | 10064 KiB |
| `w7_collections_nested_k2` | 1.89 ms | 1.93 ms | +2.5% | 0.00 ms | 0.00 ms | 9288 KiB | 10040 KiB |
| `w7_collections_nested_k3` | 2.00 ms | 2.78 ms | +39.1% | 0.00 ms | 0.00 ms | 9360 KiB | 10056 KiB |
| `w7_collections_nested_k4` | 2.41 ms | 2.76 ms | +14.4% | 0.00 ms | 0.00 ms | 9528 KiB | 10260 KiB |
| `w7_collections_nested_k5` | 2.97 ms | 3.88 ms | +30.7% | 0.00 ms | 0.00 ms | 9532 KiB | 10340 KiB |
| `w8_traits_dispatch_c010` | 8.58 ms | 10.29 ms | +19.9% | 0.00 ms | 0.00 ms | 10472 KiB | 11308 KiB |
| `w8_traits_dispatch_c025` | 17.51 ms | 20.47 ms | +16.9% | 10.00 ms | 20.00 ms | 11152 KiB | 12344 KiB |
| `w8_traits_dispatch_c050` | 34.90 ms | 40.88 ms | +17.1% | 30.00 ms | 40.00 ms | 12380 KiB | 14348 KiB |
| `w8_traits_dispatch_c100` | 74.26 ms | 77.41 ms | +4.2% | 60.00 ms | 70.00 ms | 15140 KiB | 18176 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 43.16 ms | 50.16 ms | +16.2% | 40.00 ms | 40.00 ms | 13932 KiB | 16672 KiB |
| `example_checkout` | 3.39 ms | 4.16 ms | +22.6% | 0.00 ms | 0.00 ms | 9636 KiB | 10436 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `rw_phase4_functional` | 10.57 [10.42–10.66] ms | 16.82 [14.85–16.97] ms | +6.25 ms | +59.1% | 10.00 ms | 11476 KiB |
| `rw_phase46_semantic_barriers` | 5.49 [5.27–5.52] ms | 7.61 [7.31–10.34] ms | +2.12 ms | +38.6% | 0.00 ms | 10964 KiB |
| `w5_infer_wide_w010` | 12.23 [11.87–12.29] ms | 16.62 [14.25–22.70] ms | +4.39 ms | +35.9% | 10.00 ms | 11520 KiB |
| `w5_infer_wide_w050` | 57.35 [55.03–59.61] ms | 68.79 [66.72–76.61] ms | +11.45 ms | +20.0% | 60.00 ms | 15156 KiB |
| `w5_infer_wide_w100` | 111.80 [110.47–123.20] ms | 132.70 [129.11–133.32] ms | +20.90 ms | +18.7% | 120.00 ms | 20056 KiB |
| `w6_types_count_t025` | 15.90 [14.64–16.07] ms | 18.63 [18.31–18.84] ms | +2.74 ms | +17.2% | 10.00 ms | 12416 KiB |
| `w8_traits_dispatch_c050` | 34.90 [33.93–36.57] ms | 40.88 [39.23–42.60] ms | +5.98 ms | +17.1% | 40.00 ms | 14348 KiB |
| `w8_traits_dispatch_c025` | 17.51 [17.13–17.82] ms | 20.47 [19.54–22.77] ms | +2.95 ms | +16.9% | 20.00 ms | 12344 KiB |
| `rw_projects_rolling_window` | 43.16 [42.82–44.34] ms | 50.16 [50.13–53.57] ms | +7.01 ms | +16.2% | 40.00 ms | 16672 KiB |
| `w4_infer_deep_d018` | 24.93 [23.10–26.77] ms | 28.24 [27.76–28.68] ms | +3.31 ms | +13.3% | 20.00 ms | 11812 KiB |
| `rw_sdk_datastructures` | 230.01 [223.22–234.89] ms | 258.35 [253.76–267.10] ms | +28.34 ms | +12.3% | 250.00 ms | 23080 KiB |
| `w2_spec_distinct_n032` | 1764.97 [1738.30–1817.64] ms | 1909.03 [1882.81–1931.86] ms | +144.06 ms | +8.2% | 1890.00 ms | 59324 KiB |
| `w5_infer_wide_w025` | 30.28 [29.44–31.27] ms | 32.41 [32.26–35.62] ms | +2.13 ms | +7.0% | 30.00 ms | 12760 KiB |
| `rw_packages_regex` | 650.97 [632.34–663.11] ms | 687.75 [680.09–727.80] ms | +36.78 ms | +5.7% | 680.00 ms | 30020 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `example_counter` | 1.67 ms | 2.53 ms | +0.86 ms | +51.3% |
| `example_collections` | 3.33 ms | 4.64 ms | +1.31 ms | +39.3% |
| `w7_collections_nested_k3` | 2.00 ms | 2.78 ms | +0.78 ms | +39.1% |
| `w7_collections_nested_k5` | 2.97 ms | 3.88 ms | +0.91 ms | +30.7% |
| `rw_tests_branch_confined_open` | 2.62 ms | 3.41 ms | +0.79 ms | +30.0% |
| `negative_write_write_alias` | 1.75 ms | 2.16 ms | +0.41 ms | +23.5% |
| `example_checkout` | 3.39 ms | 4.16 ms | +0.77 ms | +22.6% |
| `test_phase45_short_circuit_failure_barrier` | 1.85 ms | 2.24 ms | +0.39 ms | +20.9% |
| `w8_traits_dispatch_c010` | 8.58 ms | 10.29 ms | +1.71 ms | +19.9% |
| `example_functional_dataflow` | 2.81 ms | 3.34 ms | +0.53 ms | +18.8% |
| `rw_example_collections` | 2.87 ms | 3.38 ms | +0.52 ms | +18.0% |
| `mg4_fan_out` | 7.92 ms | 9.33 ms | +1.41 ms | +17.7% |
| `rw_phase24_string_concat` | 4.97 ms | 5.81 ms | +0.84 ms | +17.0% |
| `example_functional_basics` | 2.66 ms | 3.10 ms | +0.44 ms | +16.5% |
| `rw_phase106d1_read_projection` | 8.83 ms | 10.28 ms | +1.45 ms | +16.4% |
| `rw_phase15_15_enum_positive` | 5.02 ms | 5.85 ms | +0.82 ms | +16.4% |
| `rw_phase25_integer_overflow` | 5.38 ms | 6.22 ms | +0.84 ms | +15.6% |
| `mg2_multi_module` | 5.20 ms | 5.97 ms | +0.78 ms | +14.9% |
| `rw_phase47_iteration` | 7.00 ms | 8.02 ms | +1.03 ms | +14.7% |
| `example_static_duck_typing` | 2.56 ms | 2.93 ms | +0.37 ms | +14.6% |
| `w7_collections_nested_k4` | 2.41 ms | 2.76 ms | +0.35 ms | +14.4% |
| `mg3_deep_chain` | 4.04 ms | 4.60 ms | +0.55 ms | +13.7% |
| `example_domains_and_messages` | 1.88 ms | 2.13 ms | +0.26 ms | +13.6% |
| `w4_infer_deep_d015` | 19.38 ms | 22.00 ms | +2.62 ms | +13.5% |
| `rw_projects_cache` | 26.25 ms | 29.76 ms | +3.51 ms | +13.4% |
| `w6_types_count_t010` | 6.59 ms | 7.46 ms | +0.87 ms | +13.2% |
| `negative_spawn_retired` | 1.20 ms | 1.35 ms | +0.16 ms | +13.0% |
| `negative_heterogeneous_collection` | 1.26 ms | 1.42 ms | +0.16 ms | +12.9% |
| `mg1_multi_file` | 4.81 ms | 5.42 ms | +0.62 ms | +12.9% |
| `example_functional_pipeline` | 1.50 ms | 1.69 ms | +0.19 ms | +12.8% |
| `example_domain_typing` | 3.69 ms | 4.17 ms | +0.47 ms | +12.8% |
| `w7_collections_nested_k1` | 1.55 ms | 1.75 ms | +0.20 ms | +12.8% |
| `example_shared_memory` | 2.07 ms | 2.33 ms | +0.26 ms | +12.4% |
| `rw_example_pipeline` | 1.54 ms | 1.73 ms | +0.19 ms | +12.2% |
| `w6_types_count_t050` | 35.60 ms | 39.84 ms | +4.24 ms | +11.9% |
| `rw_swarm_066_for_range` | 7.50 ms | 8.35 ms | +0.85 ms | +11.4% |
| `test_unused_inferred_field_template` | 1.29 ms | 1.44 ms | +0.14 ms | +11.1% |
| `test_trait_open_forwarding` | 2.61 ms | 2.90 ms | +0.29 ms | +11.1% |
| `mg5_diamond` | 3.52 ms | 3.90 ms | +0.38 ms | +10.8% |
| `rw_swarm_047_reduce_map` | 7.06 ms | 7.80 ms | +0.74 ms | +10.5% |
| `rw_phase106d1_borrowed_reads` | 11.67 ms | 12.88 ms | +1.21 ms | +10.4% |
| `negative_trait_missing_method` | 2.76 ms | 3.03 ms | +0.27 ms | +9.7% |
| `w4_infer_deep_d005` | 6.06 ms | 6.53 ms | +0.47 ms | +7.7% |
| `rw_example_checkout` | 3.48 ms | 3.74 ms | +0.26 ms | +7.4% |
| `w6_types_count_t100` | 89.53 ms | 95.89 ms | +6.36 ms | +7.1% |
| `rw_phase20_fileio_helper` | 2.27 ms | 2.42 ms | +0.15 ms | +6.8% |
| `negative_duck_missing_method` | 1.68 ms | 1.80 ms | +0.11 ms | +6.7% |
| `negative_domain_route_cycle` | 1.26 ms | 1.33 ms | +0.06 ms | +5.1% |

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
