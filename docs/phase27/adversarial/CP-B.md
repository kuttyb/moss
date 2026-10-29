# CP-B adversarial review — Phase 27.1

## Verdict

**NO-GO — 1 confirmed defect.**

The required `MOSS_EST=off` comparison fails for `.mossi` byte identity against
baseline `4d6d0c8c2fcb3bb6e255bcaee80174b6b928a576`. Other reviewed invariants
passed in the focused probes listed below. No implementation source was changed.

## Frozen revision and setup

- Reviewed `a12cb1aced5d6fbd5fa24f1ecab5a2ee5c46f437` in a detached worktree.
- Built the frozen compiler and the baseline separately with
  `make --jobs="$(nproc)" moss-dev`.
- Live `./moss agent bootstrap --json` reported language version `moss-0.1`.
- Focused scripts all passed:
  - `python3 tests/tooling/check_phase27_est.py ./moss`
  - `python3 tests/tooling/check_phase27_est_mossi.py ./moss`
  - `python3 tests/tooling/check_phase27_accepts_rustc.py ./moss`
  - `python3 tests/tooling/check_phase27_three_engine.py ./moss`

The accepted-source gate reported 236 accepted standalone roots and six active
allowlist seeds. The three-engine gate reported identical output and three
callback invocations.

## Confirmed defect 1: `.mossi` hashes differ with EST off

I built the same library and app at the same absolute project path, first with
the frozen compiler and then with the baseline compiler, setting `MOSS_EST=off`
for both. `MOSS_EST=off` and `MOSS_EST=shadow` produced identical interface
hashes with the frozen compiler, so the observed difference is between the
frozen compiler output and the requested baseline, rather than a mode toggle.

| Interface | Frozen `a12cb1ac`, EST off | Baseline `4d6d0c8`, EST off |
| --- | --- | --- |
| `library/build/debug/cp_b_abi_library.mossi` | `24367d8bf9316438edf455688be73bb1a47768014c4521486471d2e19b059ecf` | `07d73352b2d6928e8e08bc7c7e1dca0326678f6688856da2f94620b42612436e` |
| `app/build/debug/cp_b_abi_app.mossi` | `667734e0f1e1aa7a01276e3362c9d6d319f68a2495a8a903699d2ee56906af5b` | `5c2ef0c43d7e830a35e1a3e091f480e3f45a6940e0804dc5c0567bda32d28d35` |

The schema and format markers themselves match: both outputs declare
`moss-module-interface-v2`, `native_abi 9`, and `execution_body_version 1`.
For each corresponding module, `interface_digest` also matches. The textual
diff is confined to the embedded execution-body hash and its
`package_fingerprint` payload value:

- Library frozen/baseline `package_fingerprint`:
  `5af93c04c86c004aff5667769588f87e4a6f064f978bc1b1510b375eff32254a` /
  `71feab9144f91160dc63857064db8ce1e65c8e55f97da278d104e3c289028eb4`.
- App frozen/baseline `package_fingerprint`:
  `c8ada0914bbb85666dfe007fe3917349c5e6a4672416f0f52230f9257c329810` /
  `7b2cd348804c00b56920f464b73b079777edb5fbd5a7ec1c104ff034cee61f7d`.

The enclosing `execution_body_hash` differs accordingly. There is no `.mossi`
schema or ABI-version change that explains away the failed byte-identity
invariant. Reproduce with:

```sh
python3 tests/phase27/adversarial/cp-b/review_probes.py ./moss \
  tmp/cp-b-baseline/moss
```

This is the one confirmed defect and blocks GO under the stated invariant. I did
not attempt a format or ABI change, per review scope.

## Reviewed invariants that passed

### EST population and origins for imported ABI-v9 bodies

`check_phase27_est_mossi.py` passed. I inspected the emitted dump records as
well: imported generic and concrete execution bodies use EST grammar nodes,
and the source-free provider body records have nonzero `origin` values. The
Section 2 `debug` path also loaded two imported provider bodies with nonzero
origins and binary-expression nodes. The app's own source expressions retain
origin zero, as expected for source-local handles.

### Rebinding during module rewriting and optimized lowering

An independent package probe rewrote `local(value)` to
`cp_b_library__local(value)` in an exported generic function. With
`MOSS_EST=shadow MOSS_EST_CONTRACT_TEST=1`, the rewrite completed and produced
the rewritten call handle with a nonzero provider origin. The contract hook
checks that the previous node is still readable and the replacement handle
uses a different ExprId. Imported Fast Debug returned `4`. The
`syntax_27_1b.moss` optimized `-O` lowering probe also completed with the EST
contract hook enabled, covering its collection/pipeline/constructor rewrites.

The focused raw-place gate passed its copy/rebuild and stale-source-fingerprint
checks. These are positive probes for representative rewrites and optimized
lowering; they do not claim exhaustive coverage of every possible transform.

### Conformance gate mutation behavior

Both named ratchets were exercised directly with a valid seed, an injected new
failure, and a fixed fixture left on the allowlist. Both accept the seeded
failure and reject the injected and stale cases. The gates also passed on the
frozen checkout.

### Other EST-off baseline comparisons

For the byte-identical source fixture
`tests/tooling/fixtures/moss_language_skill_example.moss`, both compilers
returned the same accepted verdict. After normalizing the checkout path in the
JSON, the full check response matched. Both also rejected
`tests/tooling/fixtures/builtin_generic_typing/assert_bare_comparison.moss`
with the same `INVALID_ASSERT_SYNTAX` diagnostic and message; the normalized
JSON matched.

Default and `-O` generated Rust for the accepted example was byte-identical
between compilers (SHA-256
`30e99c1748df17e08e07175cc89c3cdc1f62c4207504198294f9283d845af3f7`). Fast
Debug output also matched exactly (`5\n`). Thus the one failing comparison
surface is `.mossi` hashes; the checked standalone verdict/diagnostic, emitted
Rust, and Fast Debug samples passed.

## Artifacts

- Probe: `tests/phase27/adversarial/cp-b/review_probes.py`
- Review: `docs/phase27/adversarial/CP-B.md`
