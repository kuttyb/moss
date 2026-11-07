# R7-010 TypedCopyFact identity collision controls

This is a test-only follow-up from `9c13c2daaa640fa9704c20c255e7ad4559c9f981`.
The reviewed implementation is unchanged. It adds two registered adversarial
unit tests in `tests/phase28/oracle_r7_005_1/test_oracle_harness.py`:

1. `test_copy_identity_distinguishes_same_filename_across_modules` places
   equivalent source content at the same relative path and filename under two
   checkout roots. Both source hashes and copy spans match, but the facts carry
   distinct `defining_module_id` values; `typed_copy_fact_identity()` must
   produce different IDs.
2. `test_copy_identity_distinguishes_contents_at_same_relative_path` uses the
   same relative path and filename under two checkout roots with different
   source contents. The copy occurrence span and callable/module context match,
   but the source digests and resulting fact IDs must differ.

The suite also retains
`test_stable_identity_does_not_relax_exact_source_path_validation`, proving
that a fact whose stable identity matches but whose source path is wrong is
rejected by the production oracle. The C++ provenance builder test continues
to assert that source-path relocation alone does not alter identity while its
serialized evidence row preserves the path.

## Validation

- The two new tests passed directly.
- Registered `tests.phase28.oracle_r7_005_1.test_oracle_harness` and
  `tests.tooling.test_phase28_r7_010_clone_inventory` matrix: **50 tests, 1
  skipped** in Default and EST-shadow.
- Clone provenance v3 builder and focused runtime evidence gates passed in
  Default and EST-shadow.
- Python compilation, feedback validation, and `git diff --check` passed.

No compiler/source behavior, schema, runtime clone authorization, full suite,
or frozen oracle result changed in this follow-up. Fresh exact-SHA Team A and
Team C review remains required.
