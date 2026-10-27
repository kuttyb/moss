# Phase 26.1 F1 — interface freshness and source boundaries

Implementation: `e9186f2ca6c5f64c18e419f6b079746444276612` on
`phase26-1-m1`, based on main `d109b8f17276f70b591432fd345d57e727f17501`.

F1 records a content hash of each package's Moss sources in `.mossi`, outside
the semantic interface hash. `moss check` validates that hash using source
bytes, reports the R2 rebuild guidance for a missing or stale interface, and
does not use `MOSS_FAST_DEBUG_SOURCE_ROOTS`. It follows the requested package
module's local source closure and resolves other packages only through current
`.mossi` files. Candidate discovery excludes the checking package's own build
tree. Margo and Fast Debug keep their package-aware source handoff.

Phase 26.1 retires binary-only source-free provider packages under R2. Imported
semantic interfaces and native linking remain available when the provider
package source is present. The rule and scope are recorded in
[`PHASE_26_1_RULES.md`](PHASE_26_1_RULES.md), and every modified test file is
classified in [`F1_TEST_MIGRATION.md`](F1_TEST_MIGRATION.md).

## Validation

| Gate | Result |
| --- | --- |
| Full corpus differential vs `d109b8f1` | 2,064 sources; 1,325 accepted on both sides; 0 verdict changes; 0 diagnostic-code changes; 0 generated-Rust hash changes |
| Fast Debug parity | Passed in `shard_fast_debug` within the full regression suite |
| `make --jobs="$(nproc)" check` | 142/142 jobs passed on the implementation tree |
| Agent feedback and whitespace validation | Passed |
| Architect review | Final exact-SHA review requested; result pending |

## Performance

Run `20261026-190550` used the same WSL2 host (`DESKTOP-0T772KP`) for baseline
`d109b8f1` and candidate `e9186f2c`. It included all 77 fixtures, three
interleaved untraced timing repetitions, and one profiling sample per side.
The sum of fixture wall-time medians was 5.447 s for each revision. No fixture
exceeded the report's slowdown noise floor. In particular, the eight
attribution workloads had no noise-floor slowdown. Full per-fixture timing,
CPU, peak RSS, and counters are in the local run under
`tmp/compiler-perf/20261026-190550/`.

No GitHub regression jobs were launched.
