# Phase 26.1 F1 test migration audit

R2 requires every provider package to retain its source and puts its current
source hash in `.mossi`. Consumers still load exported semantic data from the
compiled interface. They do not parse or semantically check provider Moss
source. The table classifies every test file changed for F1 against that
contract.

| Test file | Classification and retained coverage |
| --- | --- |
| `sdk/advanced-data-acceptance/check_source_free.py` | **A — imported interface retained:** uses real producer build outputs with provider sources available for freshness hashing. |
| `sdk/encoding/tests/check_source_free.py` | **A — imported interface retained:** consumers select the producer `.mossi` and artifacts; source removal is no longer part of the fixture. |
| `sdk/text/tests/check_source_free.py` | **A — imported interface retained:** resolves the producer's current interfaces in place and preserves check/build/run assertions. |
| `tests/phase25/check_phase25_b_acceptance.py` | **B — source-removal contract retired:** verifies provider source exists and that the consumer uses `.mossi` semantics with source bytes reserved for R2 freshness. |
| `tests/phase25/integration/cross_crate_harness.py` | **A — imported interface retained:** providers remain complete packages. Scenario 11 replaces source and artifacts together so the new interface is current and still invalidates the warm consumer. |
| `tests/phase25/integration/live_acceptance.py` | **B — acceptance wording updated:** distinguishes source-backed package freshness from imported-interface checking. |
| `tests/phase25/key_protocol/03-imported-nominal-keys-source-free/EXPECTED` | **D — prior pass retired:** the compiled-only package case now records the existing `RECORD_MAP_KEY_UNSUPPORTED` fail-closed limitation; it is not counted as source-free runtime coverage. |
| `tests/phase25/key_protocol/test_proof_transport.py` | **A — imported interface retained:** producer source stays under its manifest; mutated interface copies are kept under that producer root for freshness validation. |
| `tests/run_parallel.py` | **E — registration:** adds the focused F1 interface freshness regression to the local suite. |
| `tests/tooling/check_domain_handle_closure.py` | **B — source-removal assertion retired:** keeps provider source and continues checking that the package's own build output cannot replace it. |
| `tests/tooling/check_handler_2pl.py` | **A — interface case separated:** ABI-v9 imported-interface validation uses an external consumer; the other handler behavior remains source-backed. |
| `tests/tooling/check_phase20c_executor.py` | **B — source-removal assertion retired:** the explicit-module provider wrapper/runtime test keeps package sources; cross-package interface freshness is covered separately. |
| `tests/tooling/check_phase21_abi_v9_external_cache.py` | **A — imported interface retained:** uses complete provider and consumer projects; artifact tampering still tests warm-cache binding. |
| `tests/tooling/check_phase21_abi_v9_migration.py` | **A — imported interface retained:** legacy ABI migration and consumer checks use a separate source-backed provider package. |
| `tests/tooling/check_phase21_absolute_root_boundary.py` | **B — source-removal setup retired:** retains provider package sources while preserving the absolute-root boundary assertions. |
| `tests/tooling/check_phase21_borrowed_view_write.py` | **B — source-removal setup retired:** keeps the provider package intact; borrowed-view diagnostics and runtime checks remain. |
| `tests/tooling/check_phase21_imported_effects.py` | **A — imported interface retained:** separates provider and consumer; effect summaries are read from `.mossi` while provider source backs freshness. |
| `tests/tooling/check_phase21_lock_fileio_adversarial.py` | **B — source-removal setup retired:** retains package source so adversarial lock/FileIO assertions reach their intended checks. |
| `tests/tooling/check_phase21_root_expression_ingress.py` | **B — source-removal setup retired:** keeps the provider source; package source-free execution is no longer claimed. |
| `tests/tooling/check_phase21_source_free_abi.py` | **A/C — interface and hostile-record coverage retained:** metadata probes run from a separate consumer; resealing removes the out-of-band freshness field before recomputing the ABI digest. Same-package runtime cases retain provider source. |
| `tests/tooling/check_phase21_source_free_cache_binding.py` | **A — imported interface retained:** provider source remains available while consumer cache binding is tested against selected artifacts. |
| `tests/tooling/check_phase21_source_free_proof_edges.py` | **A — imported interface retained:** source-backed producer artifacts feed a separate consumer; proof-edge mutations remain covered. |
| `tests/tooling/check_phase21_source_free_standard_write_recovery.py` | **B — source-removal setup retired:** preserves provider source and the standard-write recovery checks. |
| `tests/tooling/check_phase21_sourcefree_noop_warning.py` | **B — source-removal setup retired:** maintains source-backed package input while testing no-op warning behavior. |
| `tests/tooling/check_phase21_standard_error_namespace.py` | **B — source-removal setup retired:** keeps the provider package available; standard error identity and namespace checks remain. |
| `tests/tooling/check_phase21_standard_write_order.py` | **B — source-removal setup retired:** retains source while preserving the write-order assertions. |
| `tests/tooling/check_phase24_adversarial_architect.py` | **B — source-removal setup retired:** retains package source so adversarial semantic checks run against current package state. |
| `tests/tooling/check_phase24_bitwise_operations.py` | **A — imported interface retained:** consumer resolution uses producer build outputs with producer source present. |
| `tests/tooling/check_phase24_imported_generic_expressions.py` | **A — imported interface retained:** source-backed provider artifacts still exercise imported generic specialization. |
| `tests/tooling/check_phase25_conformance.py` | **A/C — interface and hostile-record coverage retained:** consumer resolves the producer build directory; hostile copies live under the producer manifest and digest resealing excludes freshness metadata. |
| `tests/tooling/check_phase25_indexed_string.py` | **B — source-removal setup retired:** keeps source available while preserving indexed-string behavior checks. |
| `tests/tooling/check_phase25_l7_repairs.py` | **B — source-removal setup retired:** preserves the provider source and L7 repair assertions. |
| `tests/tooling/check_phase25_m2b1_provenance.py` | **B — source-removal setup retired:** retains the producer source for provenance and freshness checks. |
| `tests/tooling/check_phase25_nominal_identity.py` | **A — imported interface retained:** distinct provider and consumer packages verify stable nominal identity through current `.mossi` outputs. |
| `tests/tooling/check_phase25_r2_wp1_call_chain.py` | **A — imported interface retained:** copies full provider package trees, points consumers at producer build directories, and keeps ABI-v9 and hostile-body cases. |
| `tests/tooling/check_phase25_r2_wp2_member.py` | **A — imported interface retained:** preserves producer manifests/source and directs consumers to current interface outputs. |
| `tests/tooling/check_phase25_r2_wp3_nominal.py` | **A — imported interface retained:** preserves producer package roots and tests imported nominal behavior through `.mossi`. |
| `tests/tooling/check_phase25_r2c_empty_vector_consumer.py` | **A — imported interface retained:** provider source and manifest remain while the consumer exercises the compiled interface. |
| `tests/tooling/check_phase25_r3f_raise_recover.py` | **B — source-removal expectation retired:** retains source-backed provider inputs for typed-error recovery assertions. |
| `tests/tooling/check_phase25_s3_key_protocol.py` | **B — source-removal setup retired:** keeps key module sources available for package freshness. |
| `tests/tooling/check_phase25_sc_safe_arithmetic.py` | **B — source-removal setup retired:** retains key module source while preserving arithmetic and diagnostic coverage. |
| `tests/tooling/check_phase25_team_a_semantics.py` | **A/B — mixed coverage:** cross-package identity checks retain producer source; strict provider checks no longer delete source before consumer validation. |
| `tests/tooling/check_phase25_typed_semantic_api.py` | **B — source-removal setup retired:** preserves provider package source for semantic API assertions. |
| `tests/tooling/check_phase8_separate_compilation.py` | **B — source-removal setup retired:** keeps sources available and continues checking separate compilation behavior. |
| `tests/tooling/check_synchronization_plan.py` | **B — same-package coverage narrowed:** keeps source in the package and corrupts its own old build artifact to prove it is ignored. |
| `tests/tooling/repro_phase21_mossi_pattern_forgery.py` | **A/C — imported interface and hostile-record coverage retained:** uses separate provider/consumer packages; forged artifact digest calculations exclude the source freshness line. |
| `tests/tooling/check_borrowed_reads.py` | **A — imported interface retained:** separate provider and consumer packages exercise helper, method, and domain views through the producer `.mossi`, with provider source kept for R2. |
| `tests/tooling/check_phase26_1_f1_interface_rules.py` | **E — new contract coverage:** tests stale/missing interfaces, hash-only source reads, Fast Debug root isolation, own-build-output exclusion, and ABI-v10 freshness metadata. |

Classification key: **A** keeps imported `.mossi`/native interface coverage with
provider source retained; **B** retires a source-removal premise while keeping
the remaining source-backed behavior; **C** adapts hostile digest helpers to
exclude out-of-band freshness metadata; **D** records a pre-existing
unsupported compiled-only case as fail-closed; **E** adds or registers F1
contract tests. These labels describe fixture behavior, not production
coverage claims. The source-free package premise is retired; imported semantic
interfaces and native linking remain supported for source-backed packages.
