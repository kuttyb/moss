# Phase 25.1 execution status

Updated: 2026-10-20 20:02 UTC  
Branch: `phase25-1-work`  
Pinned instruction commit: `3de64dc633312267c37c4e4990b28c2a73cab104`

## Gate status

| Gate | Status | Evidence |
|---|---|---|
| T0 | PASS | Annotated tags published and remote peeled commits verified below; `origin/main` unchanged. |
| R0 | REVIEW REMEDIATION | First pushed checkpoint `19ffd63822f08c255cde231b590828a986e9a00b` received NO-GO; its findings are addressed in the uncommitted R0 revision. Expanded 18-case oracle and strict corpus now pass their declared outcomes. Revised checkpoint and second review are pending. |
| R1–R5 | NOT STARTED | ABI-v9 remains default; no `main` or `phase25-integration` changes. |

## T0 transcript and verification

Starting checkout: clean `phase25-1-work` at pinned instruction commit `3de64dc...`; local authorization clarification commit `fcd91dab` was then made on the task branch without pushing a branch. Release lineage checks passed:

- `origin/main` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- `origin/recovery/pre-phase25-main` = `fa3a340a0a6652568b97869489da1a84038b9857`
- `origin/releases/phase25-delivered-v9` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- Both target commits exist locally. Neither tag name existed locally or remotely before creation.

Commands executed, in order:

```sh
git fetch --prune --tags origin
git ls-remote origin refs/heads/main refs/heads/recovery/pre-phase25-main refs/heads/releases/phase25-delivered-v9
git cat-file -e 'fa3a340a0a6652568b97869489da1a84038b9857^{commit}'
git cat-file -e 'dd45e30722bcc2d62d4bb46ba184d426cbaea066^{commit}'
git tag -a phase25-pre-main fa3a340a0a6652568b97869489da1a84038b9857 -m "Phase 25 pre-main recovery baseline"
git push origin refs/tags/phase25-pre-main
git tag -a phase25-delivered-v9 dd45e30722bcc2d62d4bb46ba184d426cbaea066 -m "Phase 25 delivered ABI-v9 baseline"
git push origin refs/tags/phase25-delivered-v9
git ls-remote origin 'refs/tags/phase25-pre-main^{}' 'refs/tags/phase25-delivered-v9^{}'
git ls-remote origin refs/heads/main
```

Remote peeled-tag verification:

- `phase25-pre-main^{}` = `fa3a340a0a6652568b97869489da1a84038b9857`
- `phase25-delivered-v9^{}` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`
- Post-push `origin/main` = `dd45e30722bcc2d62d4bb46ba184d426cbaea066`

Both refs are annotated tags. Tag object SHAs: `phase25-pre-main` = `c899b084e6cdefe9fcc7cf40af6555e281ef3a02`; `phase25-delivered-v9` = `71ce8fbd2a7efa300f51e925787b1f8b90cc4ea2`. T0 passed before R0 work began. The branch authorization wording in `AGENTS.md` is clarified in commit `fcd91dab`; that file already permitted explicitly authorized normal pushes and contained no prohibition.

## Next work

## R0 inventory, oracle and review checkpoint

Inventory and oracle are at compiler SHA `43973e1adfdf015d5588e85f41d986925a85a565` (before the R0 evidence commit). `tests/phase25/compatibility/inventory.py` enumerated 26 production package roots: 25 package builds passed and `packages/time` failed at `time__pad_digit` (inferred `char`, annotated `string`). The successful builds contain four open generic exports: `bytes.from_range` and `collections.index_of`, `contains`, `count_equal`. Exact exported facts, artifact hashes and harness path candidates are in `docs/phase25/PHASE_25_1_GENERIC_INVENTORY.json`. The package failure remains P251-GEN-008 and is not counted as an empty export list.

The source-backed/v9/v10 oracle ran six corpus workloads plus the real `bytes.from_range` / Range-copy consumer in Debug and Release. Source-backed and v9: 14/14 profiles passed; v10: 10/14 profiles passed for already supported parameter-return, scalar literal, field-read, direct private call and collection loop; generic Vector iteration and actual bytes returned `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` for both profiles. Passing v10 rows removed provider source, poisoned legacy body records and required a checked-projection trace. The range-as-helper-argument negative probe passed as `invalid-by-language` with `MOSS_COMPILE_ERROR`.

Additional evidence passed: `check_phase25_m2b2_source_free_corpus.py` (including all artifact mutation negatives and v9 controls), the 14-provider advanced-data source-free v9 gate (5 Team A providers; no provider source), foundations `validate_source_free.py` (actual Range-copy output `329`), collections source-free v9 smoke, and D4 actual SDK checked-body source-free tests for all three collection callables (legacy removals 7/7/8). The existing corpus gate was corrected to validate unsupported vector iteration at source-free consumer selection when the provider emits an ABI-v10 interface.

The R0 architecture note proposes one checker-retained typed traversal/codec/projector and preserves ABI-v9 as default; it adds no compiler implementation. `adversarial_architect` is the only additional team member and is assigned to inspect the exact pushed R0 SHA before R1. Feedback validation passed. R0 review and checkpoint delivery remain unfinished. No R1 implementation has started. All scratch output is under `tmp/`.


## R0 review remediation in progress

The first independent review of `19ffd63822f08c255cde231b590828a986e9a00b`
was NO-GO. The revision expands marker-based source-free harness discovery
(71 paths), maps selected SDK and advanced-data calls, and makes the
generic-Vector-iteration corpus assert v10 provider artifact emission before
removing source and requiring consumer-stage rejection.

`PHASE_25_1_R0_FULL_ORACLE.json` records 19 cases / 38 profiles per mode at
compiler SHA `19ffd63822f08c255cde231b590828a986e9a00b`: source-backed 38/38,
ABI-v9 36/38, ABI-v10 12/38. ABI-v9's two failures are the valid generic
raise/recover consumer, with the exact diagnostic retained. ABI-v10's 26
unsupported valid profiles fail closed at consumer selection. Three invalid
source probes pass with structured diagnostics. `check_phase25_m2b2_source_free_corpus.py`,
feedback validation, and `git diff --check` pass on the current revision.

Still open: independent review of the frozen revised checkpoint; `packages/time`
source-build gap (P2); the actual bytes `examples/range_copy` source-free run is
reserved for R3. R1 has not started.
