# Developer-experience summary: `phase24/team-t-text-encoding`

## Scope and evidence

- Reviewed workstreams: text primitives, text builder, UTF-8 codec, String
  interoperability, cross-package integration, validation, and performance.
- Observation files: seven workstream-local `observations.jsonl` files under
  this project directory.
- Implementation commits: `016715d` (`moss.text`) and `fff79e2`
  (`moss.encoding`), integrated by `e8df1dd` and `6be21a0`.
- Validation command: `python3 tests/tooling/check_agent_feedback.py`
- Validation result: passed before the independent candidate review.

This summary synthesizes the raw observations; it does not replace or rewrite
them. Optional String interoperability was investigated but could not be
implemented with the current Moss surface. No observation was reconstructed to
meet a quota.

## Recurring themes

The strongest recurring boundary is cross-module lowering rather than the pure
algorithms. The builder record attempt and imported assert both checked at one
stage but failed during project/native handling
(`phase24-team-t-text-encoding-text-builder-001` and
`phase24-team-t-text-encoding-utf8-codec-001`). A Unicode escape similarly
passed checking before generated native code rejected it
(`phase24-team-t-text-encoding-text-primitives-001`). These are tooling/backend
observations, not evidence that UTF-8 arithmetic or text searching is hard to
express in Moss.

The current String abstraction is coherent for code-point text algorithms but
does not cross the byte/scalar construction boundary
(`phase24-team-t-text-encoding-string-interop-001`). This is a demonstrated API
limit after bootstrap discovery, completion, documentation review, diagnostics,
and probes—not a conclusion from one guessed spelling.

## Strong positive findings

Moss inferred the intended ownership boundary without annotations:
`builder_append` is `WRITE` for its vector and `READ` for the segment, while
`decode_utf8` is `READ` for its input and exposes its typed `Utf8Error` raise
set. The same SDK calls survived external path dependencies and compiled-only
ABI-v9 consumption (`phase24-team-t-text-encoding-cross-package-integration-001`).

The independent Python corpus passed native debug, native release, and Fast
Debug for 66 valid sequences and 15 deliberately malformed inputs
(`phase24-team-t-text-encoding-validation-001`). This was a concrete use of
Margo and Fast Debug parity, rather than praise based on tool availability.

## Friction, surprises, and unresolved questions

Layer B String interoperability remains unresolved until Moss provides explicit
String-to-UTF-8 bytes and strict UTF-8-bytes-to-String conversions. The SDK does
not advertise a partial substitute. The vector-backed builder is a deliberate
workaround for the failed public record-field mutation path. Imported assert,
Unicode escape lowering, and benchmark typed-error lowering remain candid
tooling findings; ordinary algorithm mistakes and a self-induced concurrent
build-directory race were excluded from feedback.

## Performance and workflow

The initial performance record is preserved but invalidated: plain
`margo bench --json` optimized the benchmark root while leaving path providers
in debug profile, and its Rust comparison moved equivalent setup outside the
timed region. The corrected harness uses `margo bench --release --json`,
recursively verifies all six path providers' release manifests, and embeds
assertions for identical contents, lengths, search positions, slice ranges,
and results (`phase24-team-t-text-encoding-performance-004`). No timing was
invented for the investigation.

On the final corrected 128-segment workload, repeated `String +` measured
8,400 ns median and the vector-backed builder 5,926 ns. This is a workload-specific
result, not a general quadratic-complexity claim. Profiling also separated
representation cost from SDK work: Moss `chars()` and slices materialize owned
`String` vectors, while the matched Rust owned-String baselines were materially
slower than scalar Rust references. Replacing the SDK's manual `find` scan with
the built-in split path cut the late-single-match repeated-search median from
290,986 ns to 14,501 ns in the final run; it also removed repeated alphabet-vector
construction from ASCII
case conversion. Direct UTF-8 validation no longer allocates a decoded scalar
vector. The independent review also established that split still scans and
materializes the complete input, so this is not an early-exit search claim
(`phase24-team-t-text-encoding-performance-005` and `-006`).

Typed-error encode/decode could not run inside the normal benchmark lowering;
a documented process-amortized harness was used instead, without subtracting
startup or claiming direct Rust parity
(`phase24-team-t-text-encoding-performance-002`).

The first exact candidate exposed a separate reproducibility failure: the
repository-wide `*.rs` ignore silently omitted the maintained Rust baseline.
The Architect rejected that SHA; the source was then force-tracked and
reproduced from the corrected Git tree
(`phase24-team-t-text-encoding-performance-003`).

## Candidate improvements

1. Add strict owned String/UTF-8 conversion primitives with typed malformed
   input evidence (`phase24-team-t-text-encoding-string-interop-001`).
2. Make concrete public record collection fields usable through checked module
   interfaces, or support opaque public SDK types
   (`phase24-team-t-text-encoding-text-builder-001`).
3. Carry typed-error outcome contexts and reachable helper functions through
   benchmark lowering (`phase24-team-t-text-encoding-performance-002`).
4. Align accepted Unicode escapes and imported assertion lowering across check
   and native stages (`phase24-team-t-text-encoding-text-primitives-001`,
   `phase24-team-t-text-encoding-utf8-codec-001`).
5. Narrow generated-Rust ignore rules or explicitly exempt maintained Rust
   reference sources (`phase24-team-t-text-encoding-performance-003`).
6. Make Margo's cross-package benchmark profile behavior explicit or diagnose
   a release benchmark whose path providers are still debug-built
   (`phase24-team-t-text-encoding-performance-004`).

These observations do not duplicate SWARM records; no matching existing SWARM
issue was identified during this work.

## Original review disposition

- Reviewer role: Independent Team T L6 Architect.
- Exact reviewed commit: `1c01a899819d495347fd99e05500bb50e50e7da1`.
- Classification or evidence corrections: the original builder classification
  is preserved, while this synthesis identifies the demonstrated boundary more
  narrowly as cross-module lowering. The Architect required the missing Rust
  baseline to become a tracked artifact before approval.
- Preserved disagreements or uncertainty: String interoperability and
  benchmark lowering remain explicit limitations.
- Overall assessment: **GO** for the exact implementation candidate. The prior
  `a2f0cd7` candidate remains NO-GO because it was not reproducible from Git.

This disposition predates the L7 stabilization request. A new independently
attributable exact-SHA performance and error-contract review was therefore
required before delivery.

## Stabilization review disposition

- Reviewer role: Independent Team T L6 Architect.
- Rejected stabilization commit: `817ec4f5e59110a5bf71139e2226e7cdd3f403e1`
  (**NO-GO**) for unmatched reference work/barriers and verifier provenance.
- Exact corrected commit: `c78184c736e8da8228a5057de21d4fa292d6705f`.
- Fresh checks: release root and six linked provider rlibs, strict Rust
  reference, native debug/release/Fast Debug exact error payloads, and a fresh
  compiled-only ABI-v9 consumer.
- Preserved limitations: split materializes the whole input; scalar Rust rows
  are diagnostic; typed-error benchmarks use the process-amortized workaround;
  String interoperability remains unavailable.
- Overall assessment: **GO** for the exact corrected implementation candidate.
