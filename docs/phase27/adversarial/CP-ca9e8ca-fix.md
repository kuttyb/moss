# CP-ca9e8ca pipeline shape normalization review

Candidate: `b908f34f057e879ffcdd035449a90d84dac61b1d`  
Review branch: `review/cp-ca9e8ca-fix`  

**Verdict: GO — defects: 0**

## Findings

The new helper flattens only when the current root is a pipeline and its first
child is itself a pipeline. It moves that left pipeline's ordered children into
the current pipeline, followed by the current root's stages. It does not walk
into call arguments, lambda bodies, or other child shapes. `shape_of` visits a
nested Pipeline independently, so local association within that nested pipeline
is normalized without lifting it into a containing call or expression. The
string comparator applies the same local rule to parsed pipeline shapes.

No structurally distinct pair beyond left-spine association was found. This
includes mixed nested pipeline shapes in call arguments and stage positions;
see the explicit shape pairs and expected equality in
[`pipeline-shape-probes.md`](../../../tests/phase27/adversarial/cp-ca9e8ca-fix/pipeline-shape-probes.md).

The four seeded assertions passed: left-spine equivalence, synthetic lambda
boundary preservation, call-argument boundary preservation, and a differing
stage remaining unequal. The lambda test is synthetic because current Moss
source does not represent general lambda bodies.

## Evidence

- Bootstrap: `./moss agent bootstrap --json`; `result.language_version` was
  `moss-0.1`.
- Seeded checks: `MOSS_EST=shadow MOSS_EST_PIPELINE_SHAPE_TEST=1 ./moss check tests/phase27/est/pipeline_shape.moss --json` — exit 0; log at
  `tmp/cp-ca9e8ca-seeded.log`.
- Focused regression script: `python3 tests/tooling/check_phase27_est.py ./moss` —
  passed; log at `tmp/cp-ca9e8ca-phase27-est.log`.

## Limitations

The source-level shadow path checks that the AST and legacy parser produce the
same shape for each expression. The seeded direct comparator assertions cover
boundary/non-equivalence cases, but this review did not exhaustively enumerate
all possible Shape values. The lambda boundary is covered with a synthetic
Shape rather than Moss syntax.
