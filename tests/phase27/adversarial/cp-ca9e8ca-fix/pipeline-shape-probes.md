# Pipeline shape adversarial probes

Candidate: `b908f34f057e879ffcdd035449a90d84dac61b1d`.

The comparator's relevant shape model is `pipeline[input, stage...]`. Probe
pairs and expected comparator result:

| Pair | Result | Reason |
| --- | --- | --- |
| `pipeline[pipeline[a, f], g]` / `pipeline[a, f, g]` | equal | Only repeated left-spine nodes are removed. |
| `pipeline[a, call(g, [pipeline[b, h]])]` / `pipeline[a, call(g, [b]), h]` | unequal | The argument is a child of `call`, not the pipeline input. |
| `pipeline[a, call(f, [pipeline[b, h]]) , g]` / `pipeline[a, call(f, [b]), h, g]` | unequal | Neither argument nesting nor its stages move to the outer spine. |
| `pipeline[a, call(map, [lambda[pipeline[r, filter(ok)]]])]` / `pipeline[a, call(map, [lambda[r]]), filter(ok)]` | unequal | Normalization is not recursively applied to lambda bodies or across their parent. |
| `pipeline[a, f, g]` / `pipeline[a, f, k]` | unequal | A different stage remains a different child. |

Also considered nested association within an argument:
`pipeline[a, call(g, [pipeline[pipeline[b, h], k]])]` normalizes only the
argument's own left spine to `pipeline[a, call(g, [pipeline[b, h, k]])]`.
It does not become either outer pair above. The same locality follows for
pipeline nodes below `binary`, `index`, `collection`, `constructor`, and other
shape children: `shape_of` canonicalizes each Pipeline node when it visits it,
while the canonicalizer itself visits only the top pipeline's first child when
that child is directly a pipeline.

The repository's seeded `check_pipeline_shape_contract` exercises the first,
fourth, second, and fifth cases (the lambda case uses a synthetic Shape because
Moss v0.1 has no general first-class lambda syntax). The third and nested
argument association were checked against the implementation's explicit shape
construction and canonicalizer loop.
