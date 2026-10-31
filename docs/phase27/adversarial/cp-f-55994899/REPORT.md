# CP-F final blind review

Frozen candidate: `55994899539532c763d6489b16b859f419b12205`

Baseline: `phase27-2-m1`, `04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`

**CP-F verdict: GO**

## Review setup

The review used the isolated candidate worktree and the exact Phase 27.2
baseline. Bootstrap returned `language_version: moss-0.1`. The worktree initially
had no `./moss`, so the repository compiler was built before bootstrap. No source
files were changed. Baseline and candidate compiler identities are recorded in
`tests/phase27/adversarial/cp-f-55994899/raw/gate_results.json`.

The candidate source tree contains 2,225 tracked `.moss` files; the baseline tree
contains 2,118. The diagnostic matrix runs baseline/off, candidate/off, and
candidate/shadow on the same 2,225 candidate source inputs.

## Verdicts and diagnostics

All 2,225 inputs returned structured JSON in all three cells, with no timeout or
no-JSON result. Baseline/off and candidate/off matched except for two paths at
line 57. Both files have the same SHA-256 and report the same standalone-only
source-free diagnostic change:

| Input | Phase 27.2 | Candidate | Classification |
| --- | --- | --- | --- |
| `sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss` | `RECOVERY_PATTERN_REQUIRES_VARIANT` | `UNKNOWN_ERROR_TYPE` | BENIGN standalone source-free context |
| `tests/phase27/adversarial/cp-e-phase27-3-22a9709a/source_free_consumer.moss` | `RECOVERY_PATTERN_REQUIRES_VARIANT` | `UNKNOWN_ERROR_TYPE` | Byte-identical copy of the first input |

Candidate/off and candidate/shadow have zero verdict or diagnostic differences.
Both revisions pass the actual ABI-v9 source-free package build and execution
using the 14 provider interfaces, including the five Team A modules. This
confirms the standalone difference is caused by the missing source-free package
context in those individual checks. The two rows are one logical delta repeated
by two tracked copies of the fixture.

## Rust emission and native examples

All 1,423 commonly accepted inputs were emitted with default Moss settings and
with Moss `-O`. Of 2,846 baseline/candidate comparisons, 2,824 generated Rust
files match both raw and root-normalized SHA-256. The remaining 22 are 11
shared, context-dependent standalone emitter failures in each profile. Baseline
and candidate return the same diagnostics for every one; there are no unpaired
emitter failures or Rust hash mismatches.

The 31 non-long top-level examples have zero output mismatches across baseline
and candidate Fast Debug, default and Moss `-O` Rust, and rustc default and `-O`.
One rejected example has the same diagnostic on both revisions. Three accepted
examples return the same Fast Debug nonzero diagnostic on both revisions
(`functional_reductions`, `functional_static_callables`, and
`mini_application`); their generated native outputs still match. The other 27
accepted examples complete Fast Debug with matching output. The two long-running
examples are covered by the paired 600-second probes below.

The specific wrapping message-argument regression passes on baseline and
candidate: all ten Fast Debug/native outputs print
`-9223372036854775808\n`, and baseline/off, candidate/off, and candidate/shadow
checks accept it.

## `.mossi` and package gates

- Baseline/candidate synthetic package `.mossi` bodies are byte-identical after
  normalizing toolchain-derived fingerprints and hashes.
- The imported ABI-v9/Section 2 `.mossi` EST gate passes in off/shadow on both
  revisions.
- The ABI-v9 source-free consumer package builds and executes successfully on
  both revisions (14 provider interfaces; five Team A modules).
- `check_phase27_three_engine.py` passes on both compilers, with identical output
  and three callback invocations.
- `check_phase27_accepts_rustc.py` passes on both compilers: 236 accepted
  standalone roots and the native Margo smoke test pass; all six active seeds
  match their allowlist.

Raw `.mossi` fingerprint-normalization evidence and gate results are under
`tests/phase27/adversarial/cp-f-55994899/raw/`.

## Reach differential

The prior L7-adjudicated checkpoint records 160 Phase 27.2 self off/shadow rows
and 161 Phase 27.2-off/candidate-shadow rows on its 2,198-entry corpus. Those
prior rows remain adjudicated and are not reclassified here.

For this blind review, reach was replayed on the full 2,225-source frozen
candidate tree. Its scratch corpus contains the tracked `.moss` sources and
non-review `Moss.toml`/`Moss.lock` inputs; removed review-only manifests and
reports were not restored. On this replay, Phase 27.2 self has 158 failure rows
and Phase 27.2-off/candidate-shadow has 160. All 158 inherited rows match exactly
in edge sets, checked declarations, verdicts, and trace completion. The two
candidate-only rows are the duplicate line-57 source-free diagnostic delta
described above. They have no reach-edge or checked-declaration change, and the
package-mode oracle passes on both compilers.

The replay therefore reproduces the prior classifications but not its exact
row count: this full current source set and blind-clean package context differ
from the earlier 2,198-entry review corpus. The two prior rows that are absent
from the clean replay are retained as previously adjudicated history; this
review does not claim to have reproduced them from the stripped inputs. The
full 158/160 replay rows are preserved in the raw JSON.

## Sanitizers and paired Fast Debug timeouts

The candidate ASan+UBSan shadow compiler scanned all 2,225 tracked `.moss`
sources with zero sanitizer, signal, no-JSON, or timeout findings. Scratch
materialization copied source files and non-review manifests only.

Both required `moss debug <fixture> --trace` pairs reached the full timeout with
empty stdout and stderr on both revisions:

| Fixture | Baseline | Candidate | Classification |
| --- | ---: | ---: | --- |
| `examples/functional_pipeline.moss` | 600.002 s | 600.002 s | BOTH-TIMEOUT; excluded from output comparison |
| `examples/frontend_syntax.moss` | 600.001 s | 600.001 s | BOTH-TIMEOUT; excluded from output comparison |

These matched timeouts are coverage exclusions, not candidate deltas. No source
fix was indicated. No compiler-perf run was part of this CP-F review.

## Evidence map

- `tests/phase27/adversarial/cp-f-55994899/corpus_differential.py` and
  `raw/corpus_differential.json`
- `rust_emission_differential.py` and `raw/rust_emission_differential.json`
- `examples_engine_parity.py` and `raw/examples_engine_parity.json`
- `reach_differential.py` and `raw/reach_differential.json`
- `sanitized_candidate_scan.py` and `raw/asan_ubsan_candidate.json`
- `paired_fast_debug_timeouts.py`, `raw/fast_debug_600s.json`, and the four
  empty-output log paths recorded there
- `overflow_probe.py`, `probes/wrapping_message_argument.moss`, and
  `raw/wrapping_message_argument.json`
- `raw/mossi_baseline_report.json` and `raw/gate_results.json`
