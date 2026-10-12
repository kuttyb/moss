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

The Margo release harness measured text search, slicing, concatenation,
segment-building, and UTF-8 validation with five warmups, 31 samples, and 1,000
iterations. At 128 segments, repeated `String +` measured 7,252 ns median and
the segment builder 14,184 ns, so the builder documentation makes no speed or
quadratic-avoidance claim (`phase24-team-t-text-encoding-performance-001`).

Typed-error encode/decode could not run inside the normal benchmark lowering;
a documented process-amortized harness was used instead, without subtracting
startup or claiming direct Rust parity
(`phase24-team-t-text-encoding-performance-002`).

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

These observations do not duplicate SWARM records; no matching existing SWARM
issue was identified during this work.

## Review disposition

- Reviewer role: Independent Team T L6 Architect.
- Exact reviewed commit: pending candidate freeze.
- Classification or evidence corrections: pending independent review.
- Preserved disagreements or uncertainty: String interoperability and
  benchmark lowering remain explicit limitations.
- Overall assessment: pending exact-SHA GO/NO-GO verdict.
