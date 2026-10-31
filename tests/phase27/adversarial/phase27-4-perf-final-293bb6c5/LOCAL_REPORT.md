# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(2.4560546875, 7.322265625, 7.65576171875)`; after profiling `(1.7265625, 6.451171875, 7.34375)`.
- Candidate benchmark checkout SHA: `293bb6c53988c099974b04a0a21c5df77e9ef77e`
- Candidate compiler source SHA: `293bb6c53988c099974b04a0a21c5df77e9ef77e`
- Candidate binary SHA256: `d4c35d419c7c4143ff20e810b625ae94ccb028dada936d24a719da3195572633`
- Certification mode: `False`; working tree dirty at start: `True`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `cc1d22172508cb9e35d0230898e4be4d2a452492` resolved to `cc1d22172508cb9e35d0230898e4be4d2a452492`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.416 s | 5.534 s | +2.2% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 5.30 ms | 4.72 ms | -11.0% | 0.00 ms | 0.00 ms | 10684 KiB | 10564 KiB |
| `rw_packages_regex` | 670.56 ms | 669.21 ms | -0.2% | 660.00 ms | 660.00 ms | 30360 KiB | 30352 KiB |
| `rw_projects_rolling_window` | 51.72 ms | 51.49 ms | -0.5% | 40.00 ms | 40.00 ms | 16744 KiB | 16400 KiB |
| `rw_sdk_datastructures` | 253.69 ms | 251.21 ms | -1.0% | 240.00 ms | 240.00 ms | 23292 KiB | 23308 KiB |
| `w2_spec_distinct_n032` | 1891.31 ms | 2031.79 ms | +7.4% | 1880.00 ms | 2020.00 ms | 59604 KiB | 60084 KiB |
| `w3_spec_repeated_n032` | 889.16 ms | 875.77 ms | -1.5% | 880.00 ms | 860.00 ms | 28076 KiB | 27988 KiB |
| `w4_infer_deep_d018` | 28.82 ms | 27.72 ms | -3.8% | 20.00 ms | 20.00 ms | 11652 KiB | 11608 KiB |
| `w5_infer_wide_w100` | 126.78 ms | 126.02 ms | -0.6% | 120.00 ms | 120.00 ms | 20148 KiB | 20144 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 5.30 ms | 4.72 ms | -11.0% | 0.00 ms | 0.00 ms | 10684 KiB | 10564 KiB |
| `example_collections` | 3.45 ms | 3.60 ms | +4.2% | 0.00 ms | 0.00 ms | 10972 KiB | 10832 KiB |
| `example_counter` | 2.21 ms | 1.92 ms | -13.4% | 0.00 ms | 0.00 ms | 10176 KiB | 10172 KiB |
| `example_domain_typing` | 3.97 ms | 3.89 ms | -2.0% | 0.00 ms | 0.00 ms | 10640 KiB | 10552 KiB |
| `example_domains_and_messages` | 2.31 ms | 2.16 ms | -6.8% | 0.00 ms | 0.00 ms | 10220 KiB | 10160 KiB |
| `example_functional_basics` | 3.25 ms | 3.35 ms | +3.1% | 0.00 ms | 0.00 ms | 10524 KiB | 10512 KiB |
| `example_functional_dataflow` | 3.65 ms | 3.62 ms | -1.0% | 0.00 ms | 0.00 ms | 10492 KiB | 10428 KiB |
| `example_functional_pipeline` | 1.87 ms | 1.89 ms | +1.2% | 0.00 ms | 0.00 ms | 10064 KiB | 10272 KiB |
| `example_shared_memory` | 2.68 ms | 2.34 ms | -12.8% | 0.00 ms | 0.00 ms | 10264 KiB | 10232 KiB |
| `example_static_duck_typing` | 2.95 ms | 2.92 ms | -1.0% | 0.00 ms | 0.00 ms | 10472 KiB | 10552 KiB |
| `mg1_multi_file` | 5.60 ms | 5.53 ms | -1.2% | 0.00 ms | 0.00 ms | 11456 KiB | 11588 KiB |
| `mg2_multi_module` | 6.54 ms | 6.20 ms | -5.3% | 0.00 ms | 0.00 ms | 11532 KiB | 11844 KiB |
| `mg3_deep_chain` | 5.05 ms | 4.76 ms | -5.8% | 0.00 ms | 0.00 ms | 11324 KiB | 11240 KiB |
| `mg4_fan_out` | 10.17 ms | 10.05 ms | -1.2% | 0.00 ms | 0.00 ms | 11516 KiB | 11596 KiB |
| `mg5_diamond` | 4.15 ms | 3.96 ms | -4.4% | 0.00 ms | 0.00 ms | 10944 KiB | 11180 KiB |
| `negative_await_retired` | 1.11 ms | 1.05 ms | -5.7% | 0.00 ms | 0.00 ms | 7116 KiB | 7160 KiB |
| `negative_domain_route_cycle` | 1.71 ms | 1.44 ms | -15.4% | 0.00 ms | 0.00 ms | 9456 KiB | 9272 KiB |
| `negative_duck_missing_method` | 1.84 ms | 1.82 ms | -0.9% | 0.00 ms | 0.00 ms | 10184 KiB | 10036 KiB |
| `negative_heterogeneous_collection` | 1.45 ms | 1.44 ms | -1.0% | 0.00 ms | 0.00 ms | 10000 KiB | 9976 KiB |
| `negative_mutually_recursive_functions` | 1.75 ms | 1.41 ms | -19.6% | 0.00 ms | 0.00 ms | 9316 KiB | 9412 KiB |
| `negative_self_send_rejected` | 1.48 ms | 1.35 ms | -8.7% | 0.00 ms | 0.00 ms | 9648 KiB | 9708 KiB |
| `negative_spawn_retired` | 1.44 ms | 1.37 ms | -4.8% | 0.00 ms | 0.00 ms | 9416 KiB | 9360 KiB |
| `negative_trait_missing_method` | 2.35 ms | 2.27 ms | -3.8% | 0.00 ms | 0.00 ms | 10288 KiB | 10560 KiB |
| `negative_write_write_alias` | 2.31 ms | 2.18 ms | -5.7% | 0.00 ms | 0.00 ms | 10356 KiB | 10376 KiB |
| `rw_example_checkout` | 3.87 ms | 4.01 ms | +3.8% | 0.00 ms | 0.00 ms | 10340 KiB | 10488 KiB |
| `rw_example_collections` | 3.70 ms | 3.43 ms | -7.3% | 0.00 ms | 0.00 ms | 10684 KiB | 10696 KiB |
| `rw_example_counter` | 1.97 ms | 1.89 ms | -4.4% | 0.00 ms | 0.00 ms | 10128 KiB | 10116 KiB |
| `rw_example_domains` | 2.15 ms | 2.10 ms | -2.5% | 0.00 ms | 0.00 ms | 10068 KiB | 10176 KiB |
| `rw_example_pipeline` | 1.81 ms | 1.82 ms | +0.8% | 0.00 ms | 0.00 ms | 10256 KiB | 10144 KiB |
| `rw_packages_regex` | 670.56 ms | 669.21 ms | -0.2% | 660.00 ms | 660.00 ms | 30360 KiB | 30352 KiB |
| `rw_phase106d1_borrowed_reads` | 12.70 ms | 13.21 ms | +4.0% | 10.00 ms | 10.00 ms | 11472 KiB | 11240 KiB |
| `rw_phase106d1_read_projection` | 9.73 ms | 10.48 ms | +7.6% | 0.00 ms | 10.00 ms | 11376 KiB | 11452 KiB |
| `rw_phase106d_handler_2pl` | 7.18 ms | 7.74 ms | +7.8% | 0.00 ms | 0.00 ms | 10756 KiB | 10728 KiB |
| `rw_phase15_15_enum_positive` | 5.95 ms | 6.07 ms | +2.0% | 0.00 ms | 0.00 ms | 10788 KiB | 10900 KiB |
| `rw_phase20_fileio_helper` | 2.43 ms | 2.35 ms | -3.2% | 0.00 ms | 0.00 ms | 10064 KiB | 10212 KiB |
| `rw_phase21_fileio_root_cleanup` | 3.05 ms | 3.08 ms | +0.9% | 0.00 ms | 0.00 ms | 10348 KiB | 10344 KiB |
| `rw_phase24_string_concat` | 6.35 ms | 6.20 ms | -2.4% | 0.00 ms | 0.00 ms | 10940 KiB | 11032 KiB |
| `rw_phase25_integer_overflow` | 5.93 ms | 6.34 ms | +6.9% | 0.00 ms | 0.00 ms | 10632 KiB | 10760 KiB |
| `rw_phase46_semantic_barriers` | 7.97 ms | 7.91 ms | -0.8% | 0.00 ms | 0.00 ms | 10944 KiB | 10924 KiB |
| `rw_phase47_iteration` | 10.19 ms | 8.41 ms | -17.4% | 0.00 ms | 0.00 ms | 10912 KiB | 10828 KiB |
| `rw_phase4_functional` | 15.41 ms | 15.03 ms | -2.5% | 10.00 ms | 10.00 ms | 11524 KiB | 11416 KiB |
| `rw_projects_cache` | 30.66 ms | 30.68 ms | +0.1% | 30.00 ms | 30.00 ms | 14572 KiB | 14584 KiB |
| `rw_projects_rolling_window` | 51.72 ms | 51.49 ms | -0.5% | 40.00 ms | 40.00 ms | 16744 KiB | 16400 KiB |
| `rw_sdk_datastructures` | 253.69 ms | 251.21 ms | -1.0% | 240.00 ms | 240.00 ms | 23292 KiB | 23308 KiB |
| `rw_swarm_047_reduce_map` | 9.18 ms | 8.02 ms | -12.6% | 0.00 ms | 0.00 ms | 10740 KiB | 10704 KiB |
| `rw_swarm_066_for_range` | 9.12 ms | 8.52 ms | -6.6% | 0.00 ms | 0.00 ms | 10712 KiB | 10972 KiB |
| `rw_tests_branch_confined_open` | 3.23 ms | 3.28 ms | +1.8% | 0.00 ms | 0.00 ms | 10364 KiB | 10396 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.33 ms | 2.40 ms | +3.0% | 0.00 ms | 0.00 ms | 10120 KiB | 10120 KiB |
| `test_trait_open_forwarding` | 3.03 ms | 3.08 ms | +1.7% | 0.00 ms | 0.00 ms | 10856 KiB | 11028 KiB |
| `test_unused_inferred_field_template` | 1.42 ms | 1.44 ms | +1.6% | 0.00 ms | 0.00 ms | 9832 KiB | 9876 KiB |
| `w2_spec_distinct_n008` | 94.10 ms | 94.75 ms | +0.7% | 80.00 ms | 90.00 ms | 13912 KiB | 13908 KiB |
| `w2_spec_distinct_n016` | 393.92 ms | 394.96 ms | +0.3% | 380.00 ms | 390.00 ms | 23188 KiB | 23340 KiB |
| `w2_spec_distinct_n032` | 1891.31 ms | 2031.79 ms | +7.4% | 1880.00 ms | 2020.00 ms | 59604 KiB | 60084 KiB |
| `w3_spec_repeated_n008` | 64.85 ms | 63.03 ms | -2.8% | 60.00 ms | 60.00 ms | 11780 KiB | 11844 KiB |
| `w3_spec_repeated_n016` | 223.42 ms | 228.01 ms | +2.1% | 220.00 ms | 220.00 ms | 15208 KiB | 14952 KiB |
| `w3_spec_repeated_n032` | 889.16 ms | 875.77 ms | -1.5% | 880.00 ms | 860.00 ms | 28076 KiB | 27988 KiB |
| `w4_infer_deep_d005` | 6.51 ms | 6.39 ms | -1.9% | 0.00 ms | 0.00 ms | 10768 KiB | 10692 KiB |
| `w4_infer_deep_d010` | 12.73 ms | 12.17 ms | -4.4% | 10.00 ms | 10.00 ms | 10928 KiB | 11164 KiB |
| `w4_infer_deep_d015` | 21.82 ms | 22.58 ms | +3.5% | 20.00 ms | 20.00 ms | 11392 KiB | 11476 KiB |
| `w4_infer_deep_d018` | 28.82 ms | 27.72 ms | -3.8% | 20.00 ms | 20.00 ms | 11652 KiB | 11608 KiB |
| `w5_infer_wide_w010` | 13.95 ms | 13.90 ms | -0.3% | 10.00 ms | 10.00 ms | 11484 KiB | 11392 KiB |
| `w5_infer_wide_w025` | 36.91 ms | 33.07 ms | -10.4% | 30.00 ms | 30.00 ms | 12824 KiB | 12788 KiB |
| `w5_infer_wide_w050` | 66.58 ms | 70.61 ms | +6.0% | 60.00 ms | 60.00 ms | 15188 KiB | 15168 KiB |
| `w5_infer_wide_w100` | 126.78 ms | 126.02 ms | -0.6% | 120.00 ms | 120.00 ms | 20148 KiB | 20144 KiB |
| `w6_types_count_t010` | 8.21 ms | 7.64 ms | -7.0% | 0.00 ms | 0.00 ms | 11064 KiB | 11052 KiB |
| `w6_types_count_t025` | 19.01 ms | 18.25 ms | -4.0% | 10.00 ms | 10.00 ms | 12356 KiB | 12476 KiB |
| `w6_types_count_t050` | 39.08 ms | 38.70 ms | -1.0% | 30.00 ms | 30.00 ms | 15228 KiB | 15056 KiB |
| `w6_types_count_t100` | 98.87 ms | 96.77 ms | -2.1% | 90.00 ms | 80.00 ms | 21776 KiB | 21984 KiB |
| `w7_collections_nested_k1` | 1.69 ms | 1.80 ms | +6.4% | 0.00 ms | 0.00 ms | 10088 KiB | 9964 KiB |
| `w7_collections_nested_k2` | 1.87 ms | 1.87 ms | +0.3% | 0.00 ms | 0.00 ms | 10092 KiB | 10048 KiB |
| `w7_collections_nested_k3` | 2.28 ms | 2.24 ms | -1.8% | 0.00 ms | 0.00 ms | 10200 KiB | 9948 KiB |
| `w7_collections_nested_k4` | 2.91 ms | 2.91 ms | -0.1% | 0.00 ms | 0.00 ms | 10144 KiB | 10172 KiB |
| `w7_collections_nested_k5` | 3.35 ms | 3.45 ms | +2.9% | 0.00 ms | 0.00 ms | 10380 KiB | 10384 KiB |
| `w8_traits_dispatch_c010` | 9.34 ms | 10.21 ms | +9.4% | 0.00 ms | 0.00 ms | 11280 KiB | 11204 KiB |
| `w8_traits_dispatch_c025` | 21.73 ms | 20.43 ms | -6.0% | 20.00 ms | 10.00 ms | 12360 KiB | 12272 KiB |
| `w8_traits_dispatch_c050` | 39.77 ms | 42.22 ms | +6.2% | 30.00 ms | 40.00 ms | 14252 KiB | 14384 KiB |
| `w8_traits_dispatch_c100` | 83.41 ms | 82.20 ms | -1.4% | 70.00 ms | 70.00 ms | 18328 KiB | 18308 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 51.72 ms | 51.49 ms | -0.5% | 40.00 ms | 40.00 ms | 16744 KiB | 16400 KiB |
| `example_checkout` | 5.30 ms | 4.72 ms | -11.0% | 0.00 ms | 0.00 ms | 10684 KiB | 10564 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `w2_spec_distinct_n032` | 1891.31 [1883.40–1894.48] ms | 2031.79 [2026.85–2049.97] ms | +140.47 ms | +7.4% | 2020.00 ms | 60084 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `w8_traits_dispatch_c010` | 9.34 ms | 10.21 ms | +0.87 ms | +9.4% |
| `rw_phase106d_handler_2pl` | 7.18 ms | 7.74 ms | +0.56 ms | +7.8% |
| `rw_phase106d1_read_projection` | 9.73 ms | 10.48 ms | +0.74 ms | +7.6% |
| `rw_phase25_integer_overflow` | 5.93 ms | 6.34 ms | +0.41 ms | +6.9% |
| `w7_collections_nested_k1` | 1.69 ms | 1.80 ms | +0.11 ms | +6.4% |
| `w8_traits_dispatch_c050` | 39.77 ms | 42.22 ms | +2.45 ms | +6.2% |
| `w5_infer_wide_w050` | 66.58 ms | 70.61 ms | +4.03 ms | +6.0% |

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
