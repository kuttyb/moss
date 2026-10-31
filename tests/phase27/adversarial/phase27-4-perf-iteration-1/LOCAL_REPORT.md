# Local compiler performance run

- Host: `DESKTOP-0T772KP`; `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`; CPU `x86_64` (16 logical CPUs).
- Load average (1, 5, 15 min): before timing `(2.62109375, 6.84765625, 6.13232421875)`; after profiling `(1.7529296875, 6.02734375, 5.88720703125)`.
- Candidate benchmark checkout SHA: `7dd0468540359447c5c1fff55e5eadaa2f66f174`
- Candidate compiler source SHA: `7dd0468540359447c5c1fff55e5eadaa2f66f174`
- Candidate binary SHA256: `739ce9a627527c0374ceb44c3c60fea488eed8e87c264a033d64a9335e85cf67`
- Certification mode: `False`; working tree dirty at start: `False`.
- Timing repetitions: 3, untraced and interleaved per fixture; order reverses across repetitions.
- Profiling repetitions: 1 separate traced sample per fixture; profiling data is not used in timing summaries.
- Fixtures: 77 (54 torture plus selected curated workloads; 77 when all are enabled).
- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.
- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.
- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.

- Baseline: `ce5f836e06ce93909c358e6e6455402417d14991` resolved to `ce5f836e06ce93909c358e6e6455402417d14991`, built on this host.

## Aggregate timing

Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.

| Baseline total | Candidate total | Change |
|---:|---:|---:|
| 5.447 s | 5.611 s | +3.0% |

## Eight attribution workloads

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.91 ms | 3.82 ms | -2.4% | 0.00 ms | 0.00 ms | 10360 KiB | 10528 KiB |
| `rw_packages_regex` | 678.99 ms | 683.49 ms | +0.7% | 670.00 ms | 670.00 ms | 30116 KiB | 30564 KiB |
| `rw_projects_rolling_window` | 54.06 ms | 52.13 ms | -3.6% | 50.00 ms | 40.00 ms | 16472 KiB | 16724 KiB |
| `rw_sdk_datastructures` | 251.07 ms | 261.04 ms | +4.0% | 240.00 ms | 250.00 ms | 23256 KiB | 23340 KiB |
| `w2_spec_distinct_n032` | 1900.65 ms | 2050.72 ms | +7.9% | 1890.00 ms | 2040.00 ms | 59612 KiB | 59968 KiB |
| `w3_spec_repeated_n032` | 874.74 ms | 904.81 ms | +3.4% | 870.00 ms | 900.00 ms | 28200 KiB | 28212 KiB |
| `w4_infer_deep_d018` | 29.63 ms | 27.82 ms | -6.1% | 20.00 ms | 20.00 ms | 11756 KiB | 11768 KiB |
| `w5_infer_wide_w100` | 134.60 ms | 130.09 ms | -3.4% | 130.00 ms | 120.00 ms | 20204 KiB | 20124 KiB |

## All fixture medians and resource use

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `example_checkout` | 3.91 ms | 3.82 ms | -2.4% | 0.00 ms | 0.00 ms | 10360 KiB | 10528 KiB |
| `example_collections` | 3.54 ms | 3.34 ms | -5.6% | 0.00 ms | 0.00 ms | 10956 KiB | 10860 KiB |
| `example_counter` | 2.34 ms | 1.76 ms | -24.5% | 0.00 ms | 0.00 ms | 10204 KiB | 10124 KiB |
| `example_domain_typing` | 4.92 ms | 3.93 ms | -20.3% | 0.00 ms | 0.00 ms | 10620 KiB | 10544 KiB |
| `example_domains_and_messages` | 2.01 ms | 1.94 ms | -3.7% | 0.00 ms | 0.00 ms | 10200 KiB | 10232 KiB |
| `example_functional_basics` | 3.12 ms | 3.00 ms | -3.8% | 0.00 ms | 0.00 ms | 10428 KiB | 10444 KiB |
| `example_functional_dataflow` | 3.54 ms | 3.58 ms | +0.9% | 0.00 ms | 0.00 ms | 10484 KiB | 10616 KiB |
| `example_functional_pipeline` | 1.67 ms | 1.70 ms | +2.0% | 0.00 ms | 0.00 ms | 10016 KiB | 10268 KiB |
| `example_shared_memory` | 2.33 ms | 2.13 ms | -8.4% | 0.00 ms | 0.00 ms | 10196 KiB | 10428 KiB |
| `example_static_duck_typing` | 2.84 ms | 2.75 ms | -3.3% | 0.00 ms | 0.00 ms | 10740 KiB | 10768 KiB |
| `mg1_multi_file` | 5.67 ms | 5.43 ms | -4.2% | 0.00 ms | 0.00 ms | 11272 KiB | 11500 KiB |
| `mg2_multi_module` | 6.12 ms | 5.73 ms | -6.4% | 0.00 ms | 0.00 ms | 11508 KiB | 11736 KiB |
| `mg3_deep_chain` | 5.11 ms | 5.08 ms | -0.6% | 0.00 ms | 0.00 ms | 11180 KiB | 11340 KiB |
| `mg4_fan_out` | 10.56 ms | 8.70 ms | -17.6% | 10.00 ms | 0.00 ms | 11764 KiB | 11688 KiB |
| `mg5_diamond` | 5.72 ms | 3.68 ms | -35.7% | 0.00 ms | 0.00 ms | 10944 KiB | 11092 KiB |
| `negative_await_retired` | 1.09 ms | 1.01 ms | -7.7% | 0.00 ms | 0.00 ms | 7136 KiB | 7360 KiB |
| `negative_domain_route_cycle` | 1.36 ms | 1.27 ms | -6.5% | 0.00 ms | 0.00 ms | 9584 KiB | 9544 KiB |
| `negative_duck_missing_method` | 1.95 ms | 1.70 ms | -12.6% | 0.00 ms | 0.00 ms | 10112 KiB | 10244 KiB |
| `negative_heterogeneous_collection` | 1.43 ms | 1.32 ms | -7.8% | 0.00 ms | 0.00 ms | 10060 KiB | 9972 KiB |
| `negative_mutually_recursive_functions` | 1.50 ms | 1.36 ms | -9.4% | 0.00 ms | 0.00 ms | 9320 KiB | 9376 KiB |
| `negative_self_send_rejected` | 1.36 ms | 1.38 ms | +1.5% | 0.00 ms | 0.00 ms | 9656 KiB | 9708 KiB |
| `negative_spawn_retired` | 1.38 ms | 1.24 ms | -10.3% | 0.00 ms | 0.00 ms | 9344 KiB | 9324 KiB |
| `negative_trait_missing_method` | 2.44 ms | 2.13 ms | -12.6% | 0.00 ms | 0.00 ms | 10348 KiB | 10388 KiB |
| `negative_write_write_alias` | 1.95 ms | 1.95 ms | +0.0% | 0.00 ms | 0.00 ms | 10448 KiB | 10564 KiB |
| `rw_example_checkout` | 4.23 ms | 3.62 ms | -14.4% | 0.00 ms | 0.00 ms | 10508 KiB | 10580 KiB |
| `rw_example_collections` | 3.29 ms | 3.30 ms | +0.2% | 0.00 ms | 0.00 ms | 10848 KiB | 10724 KiB |
| `rw_example_counter` | 1.80 ms | 1.72 ms | -4.4% | 0.00 ms | 0.00 ms | 10112 KiB | 10324 KiB |
| `rw_example_domains` | 1.96 ms | 1.85 ms | -5.6% | 0.00 ms | 0.00 ms | 10080 KiB | 10232 KiB |
| `rw_example_pipeline` | 1.84 ms | 1.79 ms | -2.8% | 0.00 ms | 0.00 ms | 10172 KiB | 9956 KiB |
| `rw_packages_regex` | 678.99 ms | 683.49 ms | +0.7% | 670.00 ms | 670.00 ms | 30116 KiB | 30564 KiB |
| `rw_phase106d1_borrowed_reads` | 12.94 ms | 12.14 ms | -6.2% | 0.00 ms | 10.00 ms | 11340 KiB | 11236 KiB |
| `rw_phase106d1_read_projection` | 9.70 ms | 9.70 ms | +0.0% | 0.00 ms | 0.00 ms | 11296 KiB | 11476 KiB |
| `rw_phase106d_handler_2pl` | 7.14 ms | 7.70 ms | +7.8% | 0.00 ms | 0.00 ms | 10624 KiB | 10772 KiB |
| `rw_phase15_15_enum_positive` | 5.72 ms | 5.87 ms | +2.6% | 0.00 ms | 0.00 ms | 10984 KiB | 10956 KiB |
| `rw_phase20_fileio_helper` | 2.41 ms | 2.23 ms | -7.6% | 0.00 ms | 0.00 ms | 10120 KiB | 10144 KiB |
| `rw_phase21_fileio_root_cleanup` | 2.88 ms | 2.74 ms | -4.7% | 0.00 ms | 0.00 ms | 10272 KiB | 10360 KiB |
| `rw_phase24_string_concat` | 6.10 ms | 5.71 ms | -6.3% | 0.00 ms | 0.00 ms | 11080 KiB | 11020 KiB |
| `rw_phase25_integer_overflow` | 6.01 ms | 6.24 ms | +3.9% | 0.00 ms | 0.00 ms | 10768 KiB | 10976 KiB |
| `rw_phase46_semantic_barriers` | 8.03 ms | 7.43 ms | -7.5% | 0.00 ms | 0.00 ms | 10896 KiB | 10724 KiB |
| `rw_phase47_iteration` | 8.89 ms | 7.86 ms | -11.5% | 0.00 ms | 0.00 ms | 10756 KiB | 10828 KiB |
| `rw_phase4_functional` | 13.97 ms | 13.88 ms | -0.7% | 10.00 ms | 10.00 ms | 11452 KiB | 11588 KiB |
| `rw_projects_cache` | 32.53 ms | 29.29 ms | -10.0% | 20.00 ms | 20.00 ms | 14720 KiB | 14344 KiB |
| `rw_projects_rolling_window` | 54.06 ms | 52.13 ms | -3.6% | 50.00 ms | 40.00 ms | 16472 KiB | 16724 KiB |
| `rw_sdk_datastructures` | 251.07 ms | 261.04 ms | +4.0% | 240.00 ms | 250.00 ms | 23256 KiB | 23340 KiB |
| `rw_swarm_047_reduce_map` | 8.57 ms | 8.27 ms | -3.6% | 0.00 ms | 0.00 ms | 10748 KiB | 10656 KiB |
| `rw_swarm_066_for_range` | 8.67 ms | 8.40 ms | -3.1% | 0.00 ms | 0.00 ms | 10756 KiB | 10836 KiB |
| `rw_tests_branch_confined_open` | 3.19 ms | 3.11 ms | -2.5% | 0.00 ms | 0.00 ms | 10432 KiB | 10448 KiB |
| `test_phase45_short_circuit_failure_barrier` | 2.33 ms | 2.21 ms | -5.5% | 0.00 ms | 0.00 ms | 10180 KiB | 10320 KiB |
| `test_trait_open_forwarding` | 3.02 ms | 3.29 ms | +8.8% | 0.00 ms | 0.00 ms | 10900 KiB | 10872 KiB |
| `test_unused_inferred_field_template` | 1.52 ms | 1.33 ms | -12.3% | 0.00 ms | 0.00 ms | 9848 KiB | 10000 KiB |
| `w2_spec_distinct_n008` | 97.44 ms | 93.35 ms | -4.2% | 90.00 ms | 90.00 ms | 13916 KiB | 13780 KiB |
| `w2_spec_distinct_n016` | 406.30 ms | 411.79 ms | +1.4% | 390.00 ms | 410.00 ms | 23160 KiB | 23240 KiB |
| `w2_spec_distinct_n032` | 1900.65 ms | 2050.72 ms | +7.9% | 1890.00 ms | 2040.00 ms | 59612 KiB | 59968 KiB |
| `w3_spec_repeated_n008` | 68.17 ms | 61.46 ms | -9.8% | 60.00 ms | 60.00 ms | 11768 KiB | 11796 KiB |
| `w3_spec_repeated_n016` | 226.79 ms | 233.82 ms | +3.1% | 220.00 ms | 230.00 ms | 15088 KiB | 15096 KiB |
| `w3_spec_repeated_n032` | 874.74 ms | 904.81 ms | +3.4% | 870.00 ms | 900.00 ms | 28200 KiB | 28212 KiB |
| `w4_infer_deep_d005` | 6.78 ms | 5.75 ms | -15.2% | 0.00 ms | 0.00 ms | 10564 KiB | 10828 KiB |
| `w4_infer_deep_d010` | 12.69 ms | 11.86 ms | -6.5% | 10.00 ms | 10.00 ms | 10996 KiB | 11084 KiB |
| `w4_infer_deep_d015` | 24.29 ms | 21.74 ms | -10.5% | 20.00 ms | 20.00 ms | 11376 KiB | 11440 KiB |
| `w4_infer_deep_d018` | 29.63 ms | 27.82 ms | -6.1% | 20.00 ms | 20.00 ms | 11756 KiB | 11768 KiB |
| `w5_infer_wide_w010` | 14.26 ms | 14.36 ms | +0.7% | 10.00 ms | 0.00 ms | 11388 KiB | 11580 KiB |
| `w5_infer_wide_w025` | 34.36 ms | 32.47 ms | -5.5% | 30.00 ms | 30.00 ms | 12772 KiB | 12868 KiB |
| `w5_infer_wide_w050` | 65.67 ms | 68.39 ms | +4.1% | 60.00 ms | 60.00 ms | 15252 KiB | 15180 KiB |
| `w5_infer_wide_w100` | 134.60 ms | 130.09 ms | -3.4% | 130.00 ms | 120.00 ms | 20204 KiB | 20124 KiB |
| `w6_types_count_t010` | 7.61 ms | 7.23 ms | -4.9% | 0.00 ms | 0.00 ms | 11160 KiB | 11156 KiB |
| `w6_types_count_t025` | 20.01 ms | 19.53 ms | -2.4% | 10.00 ms | 10.00 ms | 12348 KiB | 12532 KiB |
| `w6_types_count_t050` | 40.37 ms | 40.82 ms | +1.1% | 30.00 ms | 30.00 ms | 15060 KiB | 15200 KiB |
| `w6_types_count_t100` | 97.67 ms | 94.68 ms | -3.1% | 90.00 ms | 90.00 ms | 21972 KiB | 22184 KiB |
| `w7_collections_nested_k1` | 1.71 ms | 1.61 ms | -5.7% | 0.00 ms | 0.00 ms | 10128 KiB | 10132 KiB |
| `w7_collections_nested_k2` | 1.84 ms | 2.02 ms | +9.7% | 0.00 ms | 0.00 ms | 10176 KiB | 9996 KiB |
| `w7_collections_nested_k3` | 3.19 ms | 2.37 ms | -25.9% | 0.00 ms | 0.00 ms | 10144 KiB | 9952 KiB |
| `w7_collections_nested_k4` | 2.87 ms | 2.75 ms | -4.0% | 0.00 ms | 0.00 ms | 10180 KiB | 10136 KiB |
| `w7_collections_nested_k5` | 3.52 ms | 3.29 ms | -6.4% | 0.00 ms | 0.00 ms | 10328 KiB | 10560 KiB |
| `w8_traits_dispatch_c010` | 9.16 ms | 9.07 ms | -1.0% | 0.00 ms | 0.00 ms | 11408 KiB | 11184 KiB |
| `w8_traits_dispatch_c025` | 21.37 ms | 21.14 ms | -1.1% | 20.00 ms | 20.00 ms | 12364 KiB | 12504 KiB |
| `w8_traits_dispatch_c050` | 39.75 ms | 40.96 ms | +3.0% | 30.00 ms | 40.00 ms | 14396 KiB | 14308 KiB |
| `w8_traits_dispatch_c100` | 82.39 ms | 80.58 ms | -2.2% | 70.00 ms | 70.00 ms | 18216 KiB | 18264 KiB |

## Small-fixture latency

| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `rw_projects_rolling_window` | 54.06 ms | 52.13 ms | -3.6% | 50.00 ms | 40.00 ms | 16472 KiB | 16724 KiB |
| `example_checkout` | 3.91 ms | 3.82 ms | -2.4% | 0.00 ms | 0.00 ms | 10360 KiB | 10528 KiB |

## Slowdowns beyond the noise floor

Alert rule: candidate median > baseline median by 5% and either >20 ms absolute or sample ranges do not overlap with an increase >2 ms (range comparison requires at least two trials per side).

| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |
|---|---:|---:|---:|---:|---:|---:|
| `w2_spec_distinct_n032` | 1900.65 [1896.51–1907.80] ms | 2050.72 [2050.58–2085.99] ms | +150.06 ms | +7.9% | 2040.00 ms | 59968 KiB |

## Above 5% but below the noise floor

| Fixture | Baseline median | Candidate median | Increase | Change |
|---|---:|---:|---:|---:|
| `w7_collections_nested_k2` | 1.84 ms | 2.02 ms | +0.18 ms | +9.7% |
| `test_trait_open_forwarding` | 3.02 ms | 3.29 ms | +0.27 ms | +8.8% |
| `rw_phase106d_handler_2pl` | 7.14 ms | 7.70 ms | +0.56 ms | +7.8% |

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
