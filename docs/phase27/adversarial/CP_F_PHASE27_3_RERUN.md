# CP-F Phase 27.3 Independent Rerun

Frozen candidate: `dfdce0b1efabe5852cb065e6631fd19b8c4993a8`

Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

Review branch: `cp-f-phase27-3-rerun-1793394089`

NO-GO

## Findings

The all-source check comparison covered 2,082 common tracked Moss sources,
excluding `tests/phase27/adversarial/` so this rerun did not consume prior
adversarial fixtures. Baseline and candidate had identical exit status,
verdict, and structured diagnostics for all 2,082 sources. There were no
check timeouts. The durable JSONL records the complete paired results; its only
normalization is removing checkout-specific `source_file` paths.

The ASan/UBSan shadow scan used the same 2,082-source non-adversarial corpus,
`ASAN_OPTIONS=abort_on_error=1:symbolize=1:detect_leaks=0`,
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`, and a 90-second per-source
limit. Candidate produced nine timeouts; baseline had zero findings across its
2,118-source corpus (a superset of the common inputs). No sanitizer marker,
signal, or malformed JSON was reported. Candidate-only timeouts were:

- `benchmarks/compiler_torture/fixtures/specialization/w2_spec_distinct_n016.moss`
- `benchmarks/compiler_torture/fixtures/specialization/w2_spec_distinct_n032.moss`
- `benchmarks/compiler_torture/fixtures/specialization/w3_spec_repeated_n016.moss`
- `benchmarks/compiler_torture/fixtures/specialization/w3_spec_repeated_n032.moss`
- `packages/math/tests/math_test.moss`
- `packages/regex/benches/regex_bench.moss`
- `packages/regex/src/main.moss`
- `packages/regex/tests/regex_test.moss`
- `sdk/datastructures/tests/datastructures.moss`

These are candidate-only shadow-check timeouts on ordinary common-corpus
inputs, including the compiler torture specialization cases. They are a
material regression and fail the sanitizer corpus gate, so the verdict is
NO-GO even though the checks that completed had no sanitizer findings.

## Cross-engine and package checks

An independent message-argument overflow reproducer is in `wrap.moss`. Both
revisions passed `moss check`; interpreter, Fast Debug, default Rust, and
`-O` Rust all printed `-9223372036854775808`. This confirms the candidate's
root-ingress handle-span fix preserves the wrapping result.

The standard Phase 27 `.mossi` EST fixture passed in off and shadow modes for
both revisions. The imported generic package built and ran under default and
`--release` Margo modes; both revisions printed `4` and `9`. Exported interface
digests and the generic semantic IR body hash agree. The opaque execution-body
hash and package fingerprint differ between compiler revisions, while the
checked consumer behavior is identical; this is artifact identity churn, not
an observed semantic difference.

## Reproduction and evidence

- `tests/phase27/adversarial/cp-f-phase27-3-rerun/check_diagnostics.py` runs
  the paired full-source check sweep.
- `tests/phase27/adversarial/cp-f-phase27-3-rerun/scan_sanitized.py` runs the
  sanitized shadow corpus scan. It normalizes byte-valued timeout output so
  timeouts are recorded rather than aborting report serialization.
- `checks.jsonl` contains the 2,082 paired check results.
- `sanitizer-candidate.json` and `sanitizer-baseline.json` contain the raw
  sanitizer scan reports.
- Build logs, native/interpreter outputs, `.mossi` outputs, and the first
  scanner serialization failure are retained in `tmp/cp-f-phase27-3-rerun/`.

From this review worktree, reproduce the diagnostics comparison with:

```sh
python3 tests/phase27/adversarial/cp-f-phase27-3-rerun/check_diagnostics.py \
  /path/to/candidate-worktree /path/to/baseline-worktree \
  tmp/cp-f-phase27-3-rerun/checks.jsonl
```

Run the sanitizer scan against either frozen compiler with:

```sh
python3 tests/phase27/adversarial/cp-f-phase27-3-rerun/scan_sanitized.py \
  /path/to/worktree/build/compiler/moss-dev --rev <frozen-sha> \
  --jobs 8 --timeout 90 --out tmp/cp-f-phase27-3-rerun/sanitizer.json
```

The first stock scanner run completed all candidate inputs but failed to write
its report because `TimeoutExpired.stdout` was bytes. The independent rerun
used the corrected copy committed here and completed successfully.
