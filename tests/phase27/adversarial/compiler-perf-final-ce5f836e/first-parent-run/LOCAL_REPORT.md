# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(1.91552734375, 4.5068359375, 4.40380859375)`; after profiling `(1.5625, 4.04541015625, 4.2509765625)`.
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
| 5.163 s | 5.441 s | +5.4% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.53 ms | 3.66 ms | +3.6% | 0.00 ms | 0.00 ms | 9628 KiB | 10516 KiB |
| `rw_packages_regex` | 641.06 ms | 673.98 ms | +5.1% | 640.00 ms | 670.00 ms | 25460 KiB | 29952 KiB |
| `rw_projects_rolling_window` | 46.70 ms | 51.03 ms | +9.3% | 40.00 ms | 40.00 ms | 14368 KiB | 16356 KiB |
| `rw_sdk_datastructures` | 234.49 ms | 249.59 ms | +6.4% | 230.00 ms | 240.00 ms | 19512 KiB | 23200 KiB |
| `w2_spec_distinct_n032` | 1804.64 ms | 1919.69 ms | +6.4% | 1790.00 ms | 1900.00 ms | 37012 KiB | 59384 KiB |
| `w3_spec_repeated_n032` | 885.95 ms | 895.34 ms | +1.1% | 870.00 ms | 890.00 ms | 24604 KiB | 27824 KiB |
| `w4_infer_deep_d018` | 26.57 ms | 28.68 ms | +8.0% | 20.00 ms | 20.00 ms | 10880 KiB | 11644 KiB |
| `w5_infer_wide_w100` | 116.88 ms | 128.85 ms | +10.2% | 110.00 ms | 120.00 ms | 17208 KiB | 20028 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.53 ms | 3.66 ms | +3.6% | 0.00 ms | 0.00 ms | 9628 KiB | 10516 KiB |
| `example_collections` | 3.06 ms | 3.44 ms | +12.4% | 0.00 ms | 0.00 ms | 10216 KiB | 10696 KiB |
| `example_counter` | 1.67 ms | 1.83 ms | +10.0% | 0.00 ms | 0.00 ms | 9424 KiB | 10252 KiB |
| `example_domain_typing` | 3.56 ms | 3.97 ms | +11.6% | 0.00 ms | 0.00 ms | 10076 KiB | 10516 KiB |
| `example_domains_and_messages` | 1.93 ms | 2.00 ms | +3.8% | 0.00 ms | 0.00 ms | 9648 KiB | 10180 KiB |
| `example_functional_basics` | 2.76 ms | 3.22 ms | +16.8% | 0.00 ms | 0.00 ms | 9744 KiB | 10440 KiB |
| `example_functional_dataflow` | 3.11 ms | 3.52 ms | +12.9% | 0.00 ms | 0.00 ms | 9644 KiB | 10616 KiB |
| `example_functional_pipeline` | 1.67 ms | 1.95 ms | +16.8% | 0.00 ms | 0.00 ms | 9284 KiB | 10188 KiB |
| `example_shared_memory` | 2.09 ms | 2.31 ms | +10.6% | 0.00 ms | 0.00 ms | 9704 KiB | 10236 KiB |
| `example_static_duck_typing` | 2.60 ms | 2.99 ms | +14.8% | 0.00 ms | 0.00 ms | 10000 KiB | 10668 KiB |
| `mg1_multi_file` | 5.61 ms | 6.95 ms | +24.0% | 0.00 ms | 0.00 ms | 10928 KiB | 11600 KiB |
| `mg2_multi_module` | 5.51 ms | 5.76 ms | +4.6% | 0.00 ms | 0.00 ms | 10832 KiB | 11496 KiB |
| `mg3_deep_chain` | 4.90 ms | 4.49 ms | -8.3% | 0.00 ms | 0.00 ms | 10772 KiB | 11180 KiB |
| `mg4_fan_out` | 8.78 ms | 9.06 ms | +3.2% | 0.00 ms | 0.00 ms | 11028 KiB | 11752 KiB |
| `mg5_diamond` | 3.60 ms | 3.80 ms | +5.6% | 0.00 ms | 0.00 ms | 10336 KiB | 11200 KiB |
| `negative_await_retired` | 1.09 ms | 1.10 ms | +1.0% | 0.00 ms | 0.00 ms | 6972 KiB | 7232 KiB |
| `negative_domain_route_cycle` | 1.34 ms | 1.43 ms | +7.0% | 0.00 ms | 0.00 ms | 9292 KiB | 9700 KiB |
| `negative_duck_missing_method` | 1.58 ms | 1.75 ms | +10.9% | 0.00 ms | 0.00 ms | 10076 KiB | 10340 KiB |
| `negative_heterogeneous_collection` | 1.28 ms | 1.41 ms | +10.0% | 0.00 ms | 0.00 ms | 9880 KiB | 10148 KiB |
| `negative_mutually_recursive_functions` | 1.30 ms | 1.50 ms | +15.1% | 0.00 ms | 0.00 ms | 9164 KiB | 9364 KiB |
| `negative_self_send_rejected` | 1.32 ms | 1.28 ms | -2.4% | 0.00 ms | 0.00 ms | 9436 KiB | 9908 KiB |
| `negative_spawn_retired` | 1.26 ms | 1.39 ms | +9.9% | 0.00 ms | 0.00 ms | 9284 KiB | 9664 KiB |
| `negative_trait_missing_method` | 1.95 ms | 2.13 ms | +9.4% | 0.00 ms | 0.00 ms | 10180 KiB | 10524 KiB |
| `negative_write_write_alias` | 1.80 ms | 2.01 ms | +11.6% | 0.00 ms | 0.00 ms | 10092 KiB | 10468 KiB |
| `rw_example_checkout` | 3.44 ms | 3.77 ms | +9.6% | 0.00 ms | 0.00 ms | 9820 KiB | 10576 KiB |
| `rw_example_collections` | 2.99 ms | 3.51 ms | +17.2% | 0.00 ms | 0.00 ms | 10268 KiB | 10760 KiB |
| `rw_example_counter` | 1.73 ms | 1.82 ms | +5.1% | 0.00 ms | 0.00 ms | 9548 KiB | 10224 KiB |
| `rw_example_domains` | 1.78 ms | 1.94 ms | +8.8% | 0.00 ms | 0.00 ms | 9676 KiB | 10252 KiB |
| `rw_example_pipeline` | 1.88 ms | 1.88 ms | +0.2% | 0.00 ms | 0.00 ms | 9488 KiB | 10104 KiB |
| `rw_packages_regex` | 641.06 ms | 673.98 ms | +5.1% | 640.00 ms | 670.00 ms | 25460 KiB | 29952 KiB |
| `rw_phase106d1_borrowed_reads` | 11.10 ms | 13.48 ms | +21.4% | 10.00 ms | 10.00 ms | 10512 KiB | 11280 KiB |
| `rw_phase106d1_read_projection` | 10.20 ms | 12.80 ms | +25.5% | 0.00 ms | 0.00 ms | 10472 KiB | 11388 KiB |
| `rw_phase106d_handler_2pl` | 6.38 ms | 10.97 ms | +71.9% | 0.00 ms | 10.00 ms | 10052 KiB | 10768 KiB |
| `rw_phase15_15_enum_positive` | 5.39 ms | 6.00 ms | +11.2% | 0.00 ms | 0.00 ms | 10024 KiB | 10824 KiB |
| `rw_phase20_fileio_helper` | 2.29 ms | 2.42 ms | +5.9% | 0.00 ms | 0.00 ms | 9736 KiB | 10200 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.83 ms | 2.95 ms | +4.3% | 0.00 ms | 0.00 ms | 9816 KiB | 10416 KiB |
| `rw_phase24_string_concat` | 5.49 ms | 6.12 ms | +11.4% | 0.00 ms | 0.00 ms | 10224 KiB | 10972 KiB |
| `rw_phase25_integer_overflow` | 5.46 ms | 6.05 ms | +10.9% | 0.00 ms | 0.00 ms | 10052 KiB | 10896 KiB |
| `rw_phase46_semantic_barriers` | 6.08 ms | 7.80 ms | +28.3% | 0.00 ms | 0.00 ms | 9940 KiB | 10872 KiB |
| `rw_phase47_iteration` | 7.32 ms | 8.16 ms | +11.4% | 0.00 ms | 0.00 ms | 10068 KiB | 10828 KiB |
| `rw_phase4_functional` | 11.68 ms | 13.86 ms | +18.6% | 10.00 ms | 10.00 ms | 10660 KiB | 11528 KiB |
| `rw_projects_cache` | 26.60 ms | 30.10 ms | +13.2% | 20.00 ms | 20.00 ms | 12740 KiB | 14668 KiB |
| `rw_projects_rolling_window` | 46.70 ms | 51.03 ms | +9.3% | 40.00 ms | 40.00 ms | 14368 KiB | 16356 KiB |
| `rw_sdk_datastructures` | 234.49 ms | 249.59 ms | +6.4% | 230.00 ms | 240.00 ms | 19512 KiB | 23200 KiB |
| `rw_swarm_047_reduce_map` | 7.38 ms | 8.48 ms | +14.9% | 0.00 ms | 0.00 ms | 10128 KiB | 10804 KiB |
| `rw_swarm_066_for_range` | 7.31 ms | 8.02 ms | +9.7% | 0.00 ms | 0.00 ms | 10056 KiB | 10928 KiB |
| `rw_tests_branch_confined_open` | 2.72 ms | 3.27 ms | +20.2% | 0.00 ms | 0.00 ms | 9668 KiB | 10324 KiB |
| `test_phase45_short_circuit_failure_barrier` | 1.98 ms | 2.24 ms | +13.2% | 0.00 ms | 0.00 ms | 9744 KiB | 10240 KiB |
| `test_trait_open_forwarding` | 2.80 ms | 3.11 ms | +11.2% | 0.00 ms | 0.00 ms | 10588 KiB | 10856 KiB |
| `test_unused_inferred_field_template` | 1.23 ms | 1.33 ms | +8.6% | 0.00 ms | 0.00 ms | 9304 KiB | 9892 KiB |
| `w2_spec_distinct_n008` | 92.53 ms | 94.23 ms | +1.8% | 90.00 ms | 90.00 ms | 11868 KiB | 13944 KiB |
| `w2_spec_distinct_n016` | 383.49 ms | 394.56 ms | +2.9% | 380.00 ms | 390.00 ms | 17900 KiB | 23260 KiB |
| `w2_spec_distinct_n032` | 1804.64 ms | 1919.69 ms | +6.4% | 1790.00 ms | 1900.00 ms | 37012 KiB | 59384 KiB |
| `w3_spec_repeated_n008` | 63.53 ms | 62.02 ms | -2.4% | 50.00 ms | 60.00 ms | 10820 KiB | 11876 KiB |
| `w3_spec_repeated_n016` | 229.77 ms | 228.34 ms | -0.6% | 220.00 ms | 220.00 ms | 13852 KiB | 15124 KiB |
| `w3_spec_repeated_n032` | 885.95 ms | 895.34 ms | +1.1% | 870.00 ms | 890.00 ms | 24604 KiB | 27824 KiB |
| `w4_infer_deep_d005` | 5.32 ms | 5.89 ms | +10.8% | 0.00 ms | 0.00 ms | 10220 KiB | 10536 KiB |
| `w4_infer_deep_d010` | 10.82 ms | 12.76 ms | +18.0% | 10.00 ms | 10.00 ms | 10300 KiB | 11168 KiB |
| `w4_infer_deep_d015` | 19.10 ms | 21.03 ms | +10.1% | 10.00 ms | 20.00 ms | 10888 KiB | 11520 KiB |
| `w4_infer_deep_d018` | 26.57 ms | 28.68 ms | +8.0% | 20.00 ms | 20.00 ms | 10880 KiB | 11644 KiB |
| `w5_infer_wide_w010` | 14.36 ms | 14.01 ms | -2.4% | 10.00 ms | 10.00 ms | 10728 KiB | 11500 KiB |
| `w5_infer_wide_w025` | 31.60 ms | 33.16 ms | +4.9% | 20.00 ms | 30.00 ms | 11752 KiB | 12696 KiB |
| `w5_infer_wide_w050` | 60.67 ms | 67.28 ms | +10.9% | 50.00 ms | 60.00 ms | 13504 KiB | 15164 KiB |
| `w5_infer_wide_w100` | 116.88 ms | 128.85 ms | +10.2% | 110.00 ms | 120.00 ms | 17208 KiB | 20028 KiB |
| `w6_types_count_t010` | 7.37 ms | 7.54 ms | +2.4% | 0.00 ms | 0.00 ms | 10024 KiB | 11108 KiB |
| `w6_types_count_t025` | 15.27 ms | 18.92 ms | +23.9% | 10.00 ms | 10.00 ms | 11380 KiB | 12392 KiB |
| `w6_types_count_t050` | 34.82 ms | 38.31 ms | +10.0% | 30.00 ms | 30.00 ms | 13476 KiB | 15212 KiB |
| `w6_types_count_t100` | 89.12 ms | 96.98 ms | +8.8% | 80.00 ms | 80.00 ms | 18600 KiB | 21832 KiB |
| `w7_collections_nested_k1` | 1.50 ms | 1.85 ms | +23.1% | 0.00 ms | 0.00 ms | 9428 KiB | 10120 KiB |
| `w7_collections_nested_k2` | 1.64 ms | 1.88 ms | +14.5% | 0.00 ms | 0.00 ms | 9296 KiB | 10052 KiB |
| `w7_collections_nested_k3` | 2.06 ms | 2.27 ms | +9.9% | 0.00 ms | 0.00 ms | 9648 KiB | 10132 KiB |
| `w7_collections_nested_k4` | 2.59 ms | 2.85 ms | +10.4% | 0.00 ms | 0.00 ms | 9592 KiB | 10272 KiB |
| `w7_collections_nested_k5` | 3.03 ms | 3.41 ms | +12.6% | 0.00 ms | 0.00 ms | 9744 KiB | 10404 KiB |
| `w8_traits_dispatch_c010` | 8.20 ms | 9.30 ms | +13.4% | 0.00 ms | 0.00 ms | 10320 KiB | 11220 KiB |
| `w8_traits_dispatch_c025` | 17.43 ms | 21.52 ms | +23.5% | 10.00 ms | 20.00 ms | 11404 KiB | 12164 KiB |
| `w8_traits_dispatch_c050` | 35.54 ms | 40.01 ms | +12.6% | 30.00 ms | 30.00 ms | 12688 KiB | 14276 KiB |
| `w8_traits_dispatch_c100` | 73.32 ms | 82.58 ms | +12.6% | 70.00 ms | 70.00 ms | 15096 KiB | 18324 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 46.70 ms | 51.03 ms | +9.3% | 40.00 ms | 40.00 ms | 14368 KiB | 16356 KiB |
| `example_checkout` | 3.53 ms | 3.66 ms | +3.6% | 0.00 ms | 0.00 ms | 9628 KiB | 10516 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `rw_phase106d_handler_2pl` | 6.38 [6.31–6.85] ms | 10.97 [8.24–11.03] ms | +4.59 ms | +71.9% | 10.00 ms | 10768 KiB |
| `w6_types_count_t025` | 15.27 [15.24–17.06] ms | 18.92 [18.00–20.32] ms | +3.65 ms | +23.9% | 10.00 ms | 12392 KiB |
| `rw_phase106d1_borrowed_reads` | 11.10 [10.95–12.59] ms | 13.48 [13.28–17.00] ms | +2.38 ms | +21.4% | 10.00 ms | 11280 KiB |
| `rw_phase4_functional` | 11.68 [11.62–12.19] ms | 13.86 [13.84–14.69] ms | +2.17 ms | +18.6% | 10.00 ms | 11528 KiB |
| `rw_projects_cache` | 26.60 [26.20–27.70] ms | 30.10 [29.37–32.03] ms | +3.51 ms | +13.2% | 20.00 ms | 14668 KiB |
| `w8_traits_dispatch_c100` | 73.32 [73.20–73.54] ms | 82.58 [80.80–85.20] ms | +9.26 ms | +12.6% | 70.00 ms | 18324 KiB |
| `w5_infer_wide_w050` | 60.67 [58.44–64.44] ms | 67.28 [65.47–69.95] ms | +6.61 ms | +10.9% | 60.00 ms | 15164 KiB |
| `w5_infer_wide_w100` | 116.88 [110.04–122.21] ms | 128.85 [126.91–131.14] ms | +11.97 ms | +10.2% | 120.00 ms | 20028 KiB |
| `w6_types_count_t050` | 34.82 [33.27–35.73] ms | 38.31 [37.85–43.38] ms | +3.49 ms | +10.0% | 30.00 ms | 15212 KiB |
| `rw_projects_rolling_window` | 46.70 [44.70–47.54] ms | 51.03 [48.99–51.32] ms | +4.33 ms | +9.3% | 40.00 ms | 16356 KiB |
| `w6_types_count_t100` | 89.12 [85.77–89.55] ms | 96.98 [91.63–104.57] ms | +7.86 ms | +8.8% | 80.00 ms | 21832 KiB |
| `w2_spec_distinct_n032` | 1804.64 [1789.91–1832.47] ms | 1919.69 [1893.99–1957.89] ms | +115.05 ms | +6.4% | 1900.00 ms | 59384 KiB |
| `rw_packages_regex` | 641.06 [639.23–651.97] ms | 673.98 [673.77–683.55] ms | +32.92 ms | +5.1% | 670.00 ms | 29952 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `rw_phase46_semantic_barriers` | 6.08 ms | 7.80 ms | +1.72 ms | +28.3% |
| `rw_phase106d1_read_projection` | 10.20 ms | 12.80 ms | +2.60 ms | +25.5% |
| `mg1_multi_file` | 5.61 ms | 6.95 ms | +1.35 ms | +24.0% |
| `w8_traits_dispatch_c025` | 17.43 ms | 21.52 ms | +4.09 ms | +23.5% |
| `w7_collections_nested_k1` | 1.50 ms | 1.85 ms | +0.35 ms | +23.1% |
| `rw_tests_branch_confined_open` | 2.72 ms | 3.27 ms | +0.55 ms | +20.2% |
| `w4_infer_deep_d010` | 10.82 ms | 12.76 ms | +1.94 ms | +18.0% |
| `rw_example_collections` | 2.99 ms | 3.51 ms | +0.52 ms | +17.2% |
| `example_functional_pipeline` | 1.67 ms | 1.95 ms | +0.28 ms | +16.8% |
| `example_functional_basics` | 2.76 ms | 3.22 ms | +0.46 ms | +16.8% |
| `negative_mutually_recursive_functions` | 1.30 ms | 1.50 ms | +0.20 ms | +15.1% |
| `rw_swarm_047_reduce_map` | 7.38 ms | 8.48 ms | +1.10 ms | +14.9% |
| `example_static_duck_typing` | 2.60 ms | 2.99 ms | +0.39 ms | +14.8% |
| `w7_collections_nested_k2` | 1.64 ms | 1.88 ms | +0.24 ms | +14.5% |
| `w8_traits_dispatch_c010` | 8.20 ms | 9.30 ms | +1.10 ms | +13.4% |
| `test_phase45_short_circuit_failure_barrier` | 1.98 ms | 2.24 ms | +0.26 ms | +13.2% |
| `example_functional_dataflow` | 3.11 ms | 3.52 ms | +0.40 ms | +12.9% |
| `w7_collections_nested_k5` | 3.03 ms | 3.41 ms | +0.38 ms | +12.6% |
| `w8_traits_dispatch_c050` | 35.54 ms | 40.01 ms | +4.47 ms | +12.6% |
| `example_collections` | 3.06 ms | 3.44 ms | +0.38 ms | +12.4% |
| `example_domain_typing` | 3.56 ms | 3.97 ms | +0.41 ms | +11.6% |
| `negative_write_write_alias` | 1.80 ms | 2.01 ms | +0.21 ms | +11.6% |
| `rw_phase47_iteration` | 7.32 ms | 8.16 ms | +0.84 ms | +11.4% |
| `rw_phase24_string_concat` | 5.49 ms | 6.12 ms | +0.62 ms | +11.4% |
| `rw_phase15_15_enum_positive` | 5.39 ms | 6.00 ms | +0.61 ms | +11.2% |
| `test_trait_open_forwarding` | 2.80 ms | 3.11 ms | +0.31 ms | +11.2% |
| `negative_duck_missing_method` | 1.58 ms | 1.75 ms | +0.17 ms | +10.9% |
| `rw_phase25_integer_overflow` | 5.46 ms | 6.05 ms | +0.59 ms | +10.9% |
| `w4_infer_deep_d005` | 5.32 ms | 5.89 ms | +0.58 ms | +10.8% |
| `example_shared_memory` | 2.09 ms | 2.31 ms | +0.22 ms | +10.6% |
| `w7_collections_nested_k4` | 2.59 ms | 2.85 ms | +0.27 ms | +10.4% |
| `w4_infer_deep_d015` | 19.10 ms | 21.03 ms | +1.93 ms | +10.1% |
| `example_counter` | 1.67 ms | 1.83 ms | +0.17 ms | +10.0% |
| `negative_heterogeneous_collection` | 1.28 ms | 1.41 ms | +0.13 ms | +10.0% |
| `negative_spawn_retired` | 1.26 ms | 1.39 ms | +0.12 ms | +9.9% |
| `w7_collections_nested_k3` | 2.06 ms | 2.27 ms | +0.20 ms | +9.9% |
| `rw_swarm_066_for_range` | 7.31 ms | 8.02 ms | +0.71 ms | +9.7% |
| `rw_example_checkout` | 3.44 ms | 3.77 ms | +0.33 ms | +9.6% |
| `negative_trait_missing_method` | 1.95 ms | 2.13 ms | +0.18 ms | +9.4% |
| `rw_example_domains` | 1.78 ms | 1.94 ms | +0.16 ms | +8.8% |
| `test_unused_inferred_field_template` | 1.23 ms | 1.33 ms | +0.11 ms | +8.6% |
| `w4_infer_deep_d018` | 26.57 ms | 28.68 ms | +2.11 ms | +8.0% |
| `negative_domain_route_cycle` | 1.34 ms | 1.43 ms | +0.09 ms | +7.0% |
| `rw_sdk_datastructures` | 234.49 ms | 249.59 ms | +15.09 ms | +6.4% |
| `rw_phase20_fileio_helper` | 2.29 ms | 2.42 ms | +0.13 ms | +5.9% |
| `mg5_diamond` | 3.60 ms | 3.80 ms | +0.20 ms | +5.6% |
| `rw_example_counter` | 1.73 ms | 1.82 ms | +0.09 ms | +5.1% |

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
