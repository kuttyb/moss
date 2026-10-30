# CP-F Phase 27.3 Final Cache Review

CP-F verdict: GO

Candidate: `2bdc2ea16cff57882dcfb45f99b5c2df39d6d98f`  
Baseline: `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

The candidate and baseline were built in separate detached worktrees with
`make --jobs="$(nproc)"`. The tested compiler version was `moss-0.1`. This
review did not edit `src/` or run compiler-performance benchmarks.

## Differential results

The paired standalone `moss check --json` sweeps covered 2,082 shared,
tracked Moss files in both `off` and `shadow` modes. The harness excludes
`tests/phase27/adversarial/`; those sources are included in the sanitizer scan.
Both modes had zero verdict/diagnostic differences and zero timeouts. The raw paired results are in
`tests/phase27/adversarial/cp-f-phase27-3-final-cache/checks-off.jsonl` and
`checks-shadow.jsonl`.

The overflow reproducer in
`tests/phase27/adversarial/cp-f-phase27-3-final-cache/wrapping-message-argument.moss`
passed check, interpreter, Fast Debug, default Rust, and optimized Rust on
both revisions. All execution paths returned `-9223372036854775808`. Full
captured command outputs and hashes are in `cross-engine.json`. Package-mode
`margo build --check` also matched in off and shadow; details are in
`package-overflow.json`.

The generated `.mossi` EST fixture passed in off and shadow for both
revisions, including imported generic and Section 2 execution bodies. After
normalizing identity-derived package fingerprint and execution-body hash,
both library and app artifacts compare byte-for-byte equal. Results are in
`mossi-est-candidate.txt`, `mossi-est-baseline.txt`, and
`mossi-normalized.json`.

## Sanitizer scan

ASan+UBSan shadow scans used `abort_on_error=1`, `detect_leaks=0`,
`halt_on_error=1`, eight workers, and a 90-second per-source timeout. The scan
included every tracked `.moss` source, including adversarial probes. Candidate
coverage: 2,141 sources, zero sanitizer/process findings. Baseline coverage:
2,118 sources, zero findings. Candidate-only files account for the coverage
count difference. Reports are in `sanitizer-candidate.json` and
`sanitizer-baseline.json`.

## Reproduction

From the repository root, with candidate and baseline worktrees available:

```sh
python3 tests/phase27/adversarial/cp-f-phase27-3-final-cache/check_diagnostics.py \
  /path/to/candidate /path/to/baseline checks-off.jsonl
MOSS_EST=shadow python3 tests/phase27/adversarial/cp-f-phase27-3-final-cache/check_diagnostics.py \
  /path/to/candidate /path/to/baseline checks-shadow.jsonl
python3 tests/phase27/adversarial/cp-f-phase27-3-final-cache/run_cross_engine.py \
  /path/to/baseline/moss /path/to/candidate/moss
python3 tests/phase27/adversarial/cp-f-phase27-3-final-cache/scan_sanitized.py \
  /path/to/sanitized/compiler --rev <frozen-sha> --jobs 8 --timeout 90 --out report.json
```
