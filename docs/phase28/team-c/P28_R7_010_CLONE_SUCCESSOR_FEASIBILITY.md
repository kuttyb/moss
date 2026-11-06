# P28-R7-010 Team C — Clone-Provenance Successor Feasibility

**Read-only design assessment. This is not runtime-clone authorization and not
a conformance pass.** No compiler, test, fixture, expectation, certification,
or approval-registry file was changed; no test or build was run.

## Finding

A separately versioned successor is feasible from the existing v3 prototype
in `tests/phase28/oracle_r7_005_1/oracle_harness.py`. Its manifest validator
already checks exact generated-Rust and Moss-source digests, occurrence spans,
Rust context spans and hashes, type/ownership/identity evidence, checker facts,
and equality with a separately reviewed runtime-callsite registry. The
existing registry is empty, so the prototype currently authorizes no runtime
clone. The active `tests/phase28/oracle/oracle_harness.py` path accepts v2
sidecars; it is not switched to v3 by this assessment.

## Exact schema gap and immutable inputs

The v3 runtime row/sidecar contract binds source path, whole-file digest, exact
source span, semantic type, ownership fact, semantic identity, reviewer, and a
digest-pinned review record. **It has no first-class ABI contract identity or
digest.** Do not overload `semantic_identity` for ABI identity. A successor
needs an explicit ABI contract identifier and immutable ABI evidence digest
(and span/path when the ABI is source-defined), with matching validation in
both the registry and sidecar.

Keep the complete v1 oracle tree byte-for-byte intact. At the audited Team B
source, its Git tree is `59bbc4ea51f9abb6035deffdc4816f49476b4ce7`; its input
file digests are:

| v1 input | SHA-256 |
|---|---|
| `tests/phase28/oracle/expectations.json` | `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431` |
| `tests/phase28/oracle/certification.json` | `a7887706c550b14f19500da5c54c55c1ebd1f4de54d89d4bc19d25c7eb8d4a94` |
| `tests/phase28/oracle/runtime_callsite_allowlist.json` | `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d` |

The v2 package's Git tree at that source is
`cb637bc21744633d38f8ac418aa2710ba6edd83f`. Preserve these v2 input-file
digests:

| v2 input | SHA-256 |
|---|---|
| `tests/phase28/oracle_r7_005_1/expectations.json` | `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2` |
| `tests/phase28/oracle_r7_005_1/certification.json` | `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9` |
| `tests/phase28/oracle_r7_005_1/runtime_callsite_allowlist.json` | `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d` |

The v1 registry has the same empty-registry digest; its expectation/certification
files also remain untouched. Put any populated registry and successor
certification in a separately named successor package. The successor may
reference the unchanged v2 expectation digest and must pin its own new registry
and review evidence digests. Do not rewrite or recertify v1/v2 inputs in place.

## Site evidence, scope, and limits

The Agent-2-GO-reviewed Team B audit (`da1ad5511897a60a510981906be2097c51463397`,
delivered with docs closeout at `921f70d75fde31ce8197289ffba6e60409e8e565`)
records three direct runtime `.clone()` templates in source SHA
`3417bedcd523dbeff187014de18d4a699ab53b10`: the active-executor `Arc` handle,
the executor config's affinity `Vec<usize>`, and a branch-scope handle. It
reports 252 emitted rows across 84 artifacts, all unclassified. This is
candidate evidence for per-site review, not approval. Each entry still needs an
independent reviewer to verify the source-to-Rust occurrence, concrete runtime
type and use, ABI contract, generated artifact/context hashes, and that the
operation cannot duplicate a Moss payload.

The same audit separately records seven `Arc::clone` generation sites (588
observed rows across the artifacts), also unapproved. The v3 `.clone()` scanner
does not cover qualified `Arc::clone`, macros, `to_owned`, `copied`, `mem` and
pointer operations, or Rust `Copy` loads. Its contract therefore requires an
additional independent duplication audit; authorizing the three direct
templates would not close those scanner limits or the seven `Arc` sites.

## Required next gates

1. L7 explicitly authorizes a separately versioned successor and the scope of
   any runtime-support classifications. The reviewed v2 package remains frozen.
2. Extend the successor schema with explicit ABI identity/digest fields; add
   strict validation and corruption tests for absent, stale, forged, duplicate,
   relocated, and mismatched source/Rust/ABI evidence. Keep existing v2 gates
   running unchanged against their original inputs.
3. An independent reviewer, not the author of the runtime changes, reviews
   every exact callsite and its data-flow/type/ABI evidence, the generated Rust
   binding, registry row, and digest-pinned review record. The successor
   certification pins those reviewed artifacts and passes fresh independent
   holdouts.
4. Re-audit all emitted artifacts and modes, including alternate duplication
   APIs; report every remaining unclassified site. Run the frozen 60-fixture
   expectations against the exact successor implementation without changing
   any expected outcomes.

If review confirms only implementation-level handle/config duplication and no
Moss payload copy, recording that evidence under the existing runtime-support
concept need not alter v1/v2 semantic expectations. Adding the explicit ABI
binding is a provenance-schema revision that must be versioned and reviewed.
Any policy that permits untracked Moss-value copies or weakens the frozen
`copy(x)` authority rule would be a specification change and is outside this
assessment.
