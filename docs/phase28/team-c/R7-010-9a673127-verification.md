# Team C independent checkpoint — `9a673127`

**Scoped verdict: PASS** for the declared-None method ReturnValue authority
repair and its focused consumers. **Overall Phase 28 remains NO-GO.** The
frozen oracle still reports 41 provenance failures, and no full suite was run.

## Exact source, parent, and build

- Candidate source SHA: `9a6731276c9dd9705b9034c071a81f5b4cb3ae95`.
- Parent intake: `9772cf31cc4817d9288076a1f059ce057f7aa210`.
- Fresh detached worktree: `tmp/team-c-verify-9a6731`.
- Candidate diff from parent changes three files: `src/semantic_checker.cpp`,
  `tests/tooling/check_phase28_return_none_authority.py`, and one developer-
  experience observation. It does not change the oracle, expectations,
  diagnostic mapping, or runtime clone allowlist.
- `git diff --check 9772cf31 9a673127` passed.
- Build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-9a6731-build moss moss-opt`
  passed. Compiler hashes: dev
  `229bfeb4ad4920738ab05734014b0c25cddda7d6cc348541664681bca302dbd8`, opt
  `d4465bb6eb8c899a77d397533c155d9d9c451889acad7cdf47fe9c686628e93c`.

The parent source `9772cf31` is byte-identical in source/tests to previously
verified `3ead174e42b5954f27e7d24113bc7ec5643a8fe9`; `git diff --name-only`
between those commits lists only `.codex/CURRENT_STATUS.md` and documentation.
Therefore the exact-source oracle artifact from `3ead174e` is a valid parent
source comparison; it was independently regenerated here for `9a673127`.

## Focused gates

All checks below passed in Default and EST-shadow, run sequentially in the fresh
worktree to avoid shared Margo runtime-build cache races.

- Source-free proof edges:
  `tests/tooling/check_phase21_source_free_proof_edges.py` — O1/O8 source-free
  proof-edge harness passed in both modes.
- ReturnValue authority:
  `tests/tooling/check_phase28_return_none_authority.py` — passed in both
  modes. Declared `None` method ReturnValue authority passed; `Choice.next`
  overloads retain separate `option[int]`/`option[string]` contexts; dropped,
  perturbed, and wrong-context/type facts fail closed. Native declared-None
  method output is `empty`; the injected native wrong-context mutation fails
  closed with `MOSS_INTERNAL_INVARIANT`.
- X1 v9:
  `tests/tooling/check_phase26_1_1_x1.py` — passed in both modes (`ok: true`),
  covering check-only/native parity, handler/method ReturnValue artifact and
  consumer corruption hooks, and expected native/Fast Debug output
  `4\n7\n9\n`.
- X1 v10:
  `tests/tooling/check_phase26_1_1_x1_v10.py` — passed in both modes. Generic
  Section 1, ABI-v10 Section 2 binding, private helper execution, and E1
  provider-only recheck pass; Fast Debug and native output both equal
  `3\n7\n`. Drop, perturb, duplicate, and wrong-specialization hooks fail
  closed.
- Phase 27 `.mossi` EST:
  `tests/tooling/check_phase27_est_mossi.py` — passed in both modes, with one
  generic and two imported Section 2 dumps.
- Isolated C3 `-Werror`:
  `env -u MOSS_EST python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py tmp/team-c-9a6731-build/moss-dev`
  passed, including the helper compilation with `-Werror` and negative
  call-parse cache replay checks.

## Fresh frozen v2 oracle

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler tmp/team-c-9a6731-build/moss-dev \
  --json-out tmp/team-c-9a6731-oracle.json \
  --ledger-out tmp/team-c-9a6731-oracle.md
```

Result: 60/60 semantic PASS; zero engine disagreements; 19/60 all-applicable-
gate PASS; 41/60 provenance-only failures. Compared with the parent-source
`3ead174e` oracle, fixture status and semantic status are unchanged. Clone
inventory totals are unchanged:

- 258 audited observations across 84 native mode sidecars.
- 252 unclassified and unauthorized observations.
- 2 explicit-copy-classified rows and 4 root-ingress-capture-classified rows.
- No copy/clone classification count changed; no runtime site was authorized.

Three clone-audit rows have evidence digest changes, with classifications,
source spans, and counts unchanged: ORACLE-10A's explicit-copy semantic
identity, Rust context digest, and generated-function-variant ID; ORACLE-43
and ORACLE-45 root-ingress capture identities and Rust context digests. The
only engine-result JSON difference is ORACLE-54's absolute `source_file` path
reflecting the fresh worktree; its checker/Fast Debug/native outputs match.

Frozen inputs are unchanged:

- expectations: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- diagnostic mapping: `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`
- runtime allowlist: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`
- certification: `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`

Fresh artifacts:

- Oracle JSON SHA-256: `6ab34c952a17323e780cdfa60d9eaaca3a73143c0fd38abf934c0938f97574eb`
- Oracle ledger SHA-256: `7de47fd7991e6bc65a49c3ca2a8d34f199ec47de03bc5c724de426a267593754`
- Parent-source oracle JSON: `../team-c-verify-3ead174e/tmp/team-c-3ead174e-oracle.json`,
  SHA-256 `a0cbbbd695fdc2c83c8fc3fb6e7fff51cde9e4f28d70bbb31e1bb089e5da3e6e`.

## Disposition

The declared-None method repair passes focused source-free proof edges,
ReturnValue authority, X1 v9/v10, Phase 27 `.mossi`, C3 `-Werror`, parity, and
fail-closed corruption cases. Oracle semantic and engine results are unchanged
from parent; clone totals remain unresolved and no new authorization is
introduced. No full suite was run and no Phase 28 completion GO is claimed.
