# Phase 28 staging regression triage — exact 9cad0f78

This is a read-only classification of the 53 failing jobs from `make check` and
`make check-shadow` on `9cad0f78bea157ffee1a3838fdd41373f95dfa5d`. Both modes
reported 114/167 PASS, 53 FAIL, 0 SKIP. Findings below are failure families,
not 53 independent defects; jobs can expose more than one family.

## Exact parent comparison

A clean `make check` control on parent `e3c52c8ed7ec4a1088dd1cd61918b63ab0740c8e`
reported 113 PASS, 53 FAIL, 1 SKIP. The Phase 4 materialized-local message
case failed there with `root message payload needs a checked copy(x) fact or
exact consume authority`; it passes on 9cad0f78. Its dependency, `shard_phase5_tooling`,
was skipped on the parent and now runs but fails three dependent Emacs tests.
This explains the suite-count change without crediting the candidate for the
newly observable Phase 5 failures. The broad remaining failures predate the
local-transfer integration. Do not infer the exact full failed-job set from
counts alone.

## Root-cause families evidenced by per-job logs

1. **ReturnValue fact retention / exact callable identity.** Several Native or
   Fast Debug paths fail closed with `return-value requirement/fact missing or
   ambiguous`, `return-value checker fact is missing, duplicate, or
   contradictory`, `checker could not retain return binding identity`, or
   `missing exact Echo specialization context`. Seen in
   `check_phase20_executor_fileio`, `check_phase21_on_fail_capture`,
   `check_phase21_recover_reinit`, `check_phase21_standard_error_namespace`,
   `check_phase25_team_a_semantics`, `check_phase25_sb_check_display`,
   `check_phase26_w2_inference_worklist`, `shard_concurrency`, `shard_showcase`,
   and the Rustc acceptance basket. Root action: identify the exact missing
   checker producer/context projection per occurrence; add checker-side facts
   and drop/duplicate/perturb tests. Do not infer facts in consumers.

2. **Root-ingress argument classification/type retention.** Logs show missing
   exact closed TypeRef or missing checked copy/fresh-transfer authority in
   `check_phase25_s1_checker_authority`, `check_phase25_s4_fast_debug_adversarial`,
   `check_phase21c_branch_errors`, and `phase20_review_probes`. ORACLE-45's
   primitive nested argument TypeRef is a specific instance now under repair;
   other nonprimitive/copy paths are separate and must remain fail-closed.

3. **Collection-view and indexed-read authority.** Distinct diagnostics identify
   missing Map receiver typing, conditional Map READ authority, collection view
   authority, or indexed READ/projection authority. Affected jobs include
   `check_generic_specialization`, `check_phase15_15_string_api`,
   `check_phase25_char_string_sdk_packages`, `check_phase25_s2_char_string`,
   `check_formatter_indexing`, `shard_enum_modules`, `sdk_foundations`,
   `sdk_text_encoding`, and `sdk_adv_data_source_free`. Root action: split by
   checker fact class (Map view receiver/context, conditional Map read, indexed
   element projection, non-escaping `keys`/`values` view); add exact producer,
   consumer authority checks, and per-class mutation tests. These are not one
   generic “collection” fix.

4. **Ownership contract or unsupported source pattern.** Logs include
   `OWNERSHIP_ILLEGAL_CONSUME`, `OWNERSHIP_COLLECTION_ELEMENT_CONSUME`, and
   `OWNERSHIP_USE_AFTER_CONSUME` in `check_phase24_adversarial_architect`,
   `check_phase25_r2_wp3_nominal`, `check_phase26_1_c1_corpus_cases`,
   `check_phase15_8_cross_package_fast_debug`, and SDK Adv Data jobs. Root
   action: inspect each failing Moss expression against the frozen ownership
   contract, then either correct the fixture/library to explicit `take`/`copy`
   or repair the exact checker proof if the operation is contract-valid. No
   implicit nonprimitive copy is acceptable.

5. **Raise/effect/Root-totality edge mismatches.** Examples are unexpected
   `ROOT_UNHANDLED_RAISE`, `bare raise transfer source edge missing`, and a
   ParseError effect mismatch. Seen in `check_phase21_original_af`,
   `check_phase21_lock_fileio_adversarial`, `check_phase21_numeric_parse`,
   `phase25_key_protocol_fixtures`, `shard_negative`, `shard_fast_debug`,
   `shard_phase25_int_payloads`, and `shard_swarm_cases`. Root action: trace
   the checker CFG/source edge and inferred raise set for each fixture; separate
   expected-diagnostic drift from missing/contradictory facts.

6. **Generic specialization and `.mossi` projection/serialization.** Failures
   include unavailable exact call specialization, inability to serialize an
   exact checked Copy fact to a callable projection, source-free consumer
   mismatches, and wrong specialization context. Exposed in
   `check_phase25_conformance`, `check_phase25_l7_repairs`,
   `check_phase27_accepts_rustc`, `phase25_key_protocol_fixtures`, plus portions
   of the Phase 21 source-free and SDK suites. Root action: trace producer
   callable identity through serialization/import and compare source-backed vs
   source-free facts. Do not waive consumer checks to make a package build.

7. **Downstream package/test fallout.** Many SDK modes are repeated consumers of
   the collection, ownership, Root-ingress, and `.mossi` failures above; the
   repeated debug/release/fast-debug jobs are not separate compiler roots.
   `check_phase27_accepts_rustc` and package conformance jobs are aggregate
   baskets with multiple independent failing sources and must be triaged by
   source case.

8. **Independent stale validation fixtures / tooling.**
   `check_phase25_4_performance_baseline` reports three stale SHA pins for
   `packages/regex/src/main.moss`, `tests/phase106d1_borrowed_reads.moss`, and
   `tests/phase106d1_read_projection.moss`. This is fixture-integrity drift,
   not a clone-provenance result; update pins only through their owning review.
   `shard_phase5_tooling` now executes and reports three Emacs dependent-package
   failures. Its exact stderr is in the per-job log and should be handled as a
   Phase 5 tooling root after the upstream shard dependency is green.

## Triage order after ORACLE-45

1. Confirm the frozen candidate's exact failing-job delta against 9cad0f78.
2. Fix broad ReturnValue fact/context retention and `.mossi` callable projection,
   as these recur across old phases and native/Fast Debug consumers.
3. Split collection/index authority failures into their distinct fact classes;
   preserve fail-closed behavior while repairing source-backed and serialized
   consumers.
4. Resolve raise/effect/Root edge and ownership-contract mismatches by fixture,
   without interpreting a diagnostic alone as a missing language feature.
5. Triage downstream SDK and Rustc aggregate cases after their semantic
   dependencies are corrected.
6. Refresh stale performance fixture pins and fix Phase 5 dependent-package
   tests under their respective owners.

No clone exception is approved by this triage. Clone-site remediation and
provenance remain a separate acceptance gate.

## Frozen ORACLE-45 candidate delta

Candidate `dce8e519238e89bce228f4db90e18c782d1f4def` (isolated child of
9cad0f78) passes the previously failing `check_phase25_s1_checker_authority`
case after adding checker-owned exact Root-ingress argument TypeRef. Its Default
and Shadow summaries are 115/167 PASS, 52 FAIL, 0 SKIP. The exact 52 names and
per-job logs are in
`tmp/phase28-r7-010-root-ingress-primitive-type-results/make-check.log`.
The same Phase 21/25/26/27, SDK, ownership, collection, serialization, and
fixture/tooling families remain. Its Phase 4 diff shard passes; Phase 5 tooling
runs and fails three dependent Emacs tests. These results belong to dce8e519,
not staging 9cad0f78 until the candidate is independently reviewed and
integrated.
