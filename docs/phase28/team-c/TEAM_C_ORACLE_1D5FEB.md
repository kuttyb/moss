# Team C frozen v2 oracle verification — `1d5feb1e`

## Scope and exact checkout

Read-only independent run of the certified frozen Phase 28 v2 oracle against integrated source SHA `1d5feb1ea237e1d57297d38af0734cb65722c348`. A fresh detached worktree was used at `tmp/team-c-oracle-1d5feb-review`; its source tree remained clean. No production source, oracle expectation, fixture, diagnostic mapping, runtime registry, or allowlist was modified.

Compiler discovery returned `moss-0.1`. Build command: `make --jobs="$(nproc)"`. The independently built compiler was `build/compiler/moss-dev`, SHA-256 `143f1e8807ebd24f39be867f640d174b208decbe0c21c8b8960de124b44504e6`.

Oracle command:

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler build/compiler/moss-dev \
  --tmp tmp/team-c-oracle-1d5feb-review/oracle-work \
  --json-out tmp/team-c-oracle-1d5feb-review/oracle-results.json \
  --ledger-out tmp/team-c-oracle-1d5feb-review/oracle-ledger.md
```

## Results

- **60 fixtures; checker semantic PASS: 60/60.**
- **Engine parity: one disagreement.** `ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` passes native Debug and native Optimized with stdout `11`, while Fast Debug crashes with `MOSS_INTERNAL_INVARIANT Root-ingress argument lacks an exact closed TypeRef`. The remaining applicable Fast Debug/native outcomes match expectations; `ORACLE-47` is the expected panic in all three execution engines.
- **All applicable gates: 19 PASS, 41 FAIL.** The 41 failures are provenance-only except the one Fast Debug mismatch above (counted as a semantic/engine failure in the JSON). Clone provenance is therefore the dominant blocker.
- The run reports **258 raw generated-Rust `.clone()` observations** across debug and optimized outputs: 252 `unclassified`, 4 tagged `root_ingress_domain_capture`, and 2 tagged `explicit_copy`. Its aggregate audit fields report 252 unauthorized and unclassified observations, 2 authorized, and 4 root-ingress captures. The frozen runtime callsite allowlist has zero entries; no new clone exception was added or proposed. Do not read the two semantic `explicit_copy` tags as runtime allowlist exceptions.
- Oracle output summary: total 60, 19 all-applicable-gate PASS, 41 at least one gate FAIL, 252 unverified/unclassified clone observations, 252 unauthorized clone observations. The result JSON reports semantic status 59 PASS / 1 FAIL due to the Fast Debug crash.

## Frozen inputs and artifacts

Post-run hashes match the pre-run values:

- v2 expectations: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- certification: `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`
- diagnostic mapping: `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`
- runtime callsite allowlist: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d` (certified frozen, zero entries)
- fixture manifest: 59 `.moss` files; SHA-256 over sorted `name:file-sha256` rows is `b0a3846d0e4e09741ef1f9bf091b2f0b2f213e9a9a4b7195d412c1d74a99684c`; the frozen expectation table contains 60 fixture rows.

Fresh artifacts:

- `oracle-results.json`: SHA-256 `a5d97ef0d1396ec5c6a0e74acedfd9c43c29fd68b5fb05926e34f9017c29849e`
- `oracle-ledger.md`: SHA-256 `99246b911f499e618db998662a115e28d6836e79ba3ae267818357adb8a43170`

The coordinator's run `tmp/phase28-1d5feb-oracle.json`, SHA-256 `10a05d205bd76e64df1ae05aa5c4c37964c95f37d77871507800f1ab6aefac16`, is recorded only as a comparison reference; it was not used as evidence for this independent result.

## Scoped verdict

**NO-GO for this oracle checkpoint.** Checker semantic expectations pass 60/60, but one Fast Debug/native disagreement remains and 41 fixtures fail all-gate acceptance due primarily to unauthorized/unclassified clone provenance. This does not certify full Phase 28 acceptance or authorize any runtime clone exception.
