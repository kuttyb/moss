# CP-F Phase 27.3 Review After Checker Cache

Frozen candidate: `420b31ab6a267c4c8200a81660eb2e2b36fc449d`

Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

Review branch: `cp-f-phase27-3-after-cache-420b31ab`

CP-F verdict: GO

## Findings

The independent paired check sweep covered 2,082 common tracked `.moss` sources in both off and shadow modes. Baseline and candidate had identical exit status, verdict, and structured diagnostics in each mode: 0 differences and 0 timeouts. Checkout-specific source paths are removed before comparing JSON diagnostics. Complete paired outputs are committed in `tests/phase27/adversarial/cp-f-phase27-3-after-cache/checks-off.jsonl` and `checks-shadow.jsonl`.

The ASan/UBSan shadow scanner covered all 2,082 tracked Moss sources for both revisions, with `ASAN_OPTIONS=abort_on_error=1:symbolize=1:detect_leaks=0`, `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`, and a 90-second per-source limit. Candidate and baseline each report zero sanitizer, signal, malformed-JSON, or timeout findings. Raw scan reports are committed as `sanitizer-candidate.json` and `sanitizer-baseline.json`.

The root-ingress overflow reproducer `wrapping-message-argument.moss` passed `moss check`, interpreter, Fast Debug (`moss debug`), default Rust, and `-O` Rust under both revisions. All native/interpreter runs print `-9223372036854775808`. The harness and captured results are `run_cross_engine.py` and `cross-engine.json`.

The standard Phase 27 `.mossi` EST fixture passed in off and shadow for baseline and candidate, including imported generic and Section 2 EST dumps. Cross-revision generated package bodies are byte-equal after recomputing/normalizing identity-derived package fingerprint and execution-body hash fields; both library and app compare equal. Raw off/shadow hashes and normalized package comparison are in `mossi-summary.json`.

No CP-F differential or sanitizer finding remains unadjudicated. This review did not run compiler performance benchmarks; the separate off performance gate remains the coordinator's workstream.

## Reproduction

The scripts under `tests/phase27/adversarial/cp-f-phase27-3-after-cache/` were copied without modification from the previous CP-F harness where noted; the source fixture is the previously committed overflow regression probe. Cross-engine runner and review summary are new for this frozen candidate.

```sh
python3 tests/phase27/adversarial/cp-f-phase27-3-after-cache/check_diagnostics.py \
  /path/to/candidate-worktree /path/to/baseline-worktree checks-off.jsonl
MOSS_EST=shadow python3 tests/phase27/adversarial/cp-f-phase27-3-after-cache/check_diagnostics.py \
  /path/to/candidate-worktree /path/to/baseline-worktree checks-shadow.jsonl
python3 tests/phase27/adversarial/cp-f-phase27-3-after-cache/scan_sanitized.py \
  /path/to/compiler --rev <frozen-sha> --jobs 8 --timeout 90 --out sanitizer.json
```

Both frozen worktrees built with `make --jobs="$(nproc)"`; their bootstrap output reported `language_version: moss-0.1`. No `src/` files were modified by this reviewer.
