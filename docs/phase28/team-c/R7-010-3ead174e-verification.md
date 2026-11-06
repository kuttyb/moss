# Team C independent checkpoint — `3ead174e`

**Scoped verdict: PASS** for the narrow generic imported ReturnValue Section 1
EST repair and its checked consumers. **Phase 28 remains NO-GO**: this is not a
full-suite run, and the frozen oracle still has 41 provenance failures.

## Identity and isolation

- Candidate source SHA: `3ead174e42b5954f27e7d24113bc7ec5643a8fe9`.
- Parent intake SHA: `bf5fc4616aa4dadd7ca4ef30a967e80237fef884`.
- Fresh detached worktree: `tmp/team-c-verify-3ead174e`; clean at setup.
- Candidate diff from intake is limited to four files: `src/semantic_checker.cpp`,
  `src/interpreter.hpp`, `tests/tooling/check_phase26_1_1_x1_v10.py`, and one
  developer-experience observation. `git diff --check` passed.
- Build command: `make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-3ead174e-build moss moss-opt`.
  Both builds passed. Binary hashes: dev
  `8be6036d61573063032e7c5feecfd02e8257a02e775095ecff480a3ac74e0f0b`, opt
  `021fcd13963922dfb5912da157a9e45714d9065bcf5522f6ccaeeb6d93f54b36`.

The semantic change is bounded to projected imported generic ReturnValue
occurrences: checker materializes a closed specialization ReturnValue type in
the exact imported EST context, and Fast Debug rejects ambiguous/missing
projected imported owner identity. Test-only drop, duplicate, perturb, and
consumer-specialization hooks check fail-closed behavior.

## Focused verification

### X1 v9

Commands, each run in Default and then EST-shadow with distinct work dirs:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1.py \
  tmp/team-c-3ead174e-build/moss-dev tmp/team-c-3ead174e-x1-v9-default-final
MOSS_EST=shadow python3 tests/tooling/check_phase26_1_1_x1.py \
  tmp/team-c-3ead174e-build/moss-dev tmp/team-c-3ead174e-x1-v9-shadow-final
```

Both passed (`ok: true`). The check-only and native dependency traces match;
native and Fast Debug output is `4\n7\n9\n`. Handler/method artifact corruption
and native consumer corruption hooks fail closed, including dropped,
duplicated, perturbed, wrong owner/module/source/type/context facts. The
overloaded `Choice.next` method retains separate `option[int]` and
`option[string]` ReturnValue EST contexts; drop/perturb hooks fail closed, and
Fast Debug prints `empty-int\nempty-string\n`.

### X1 v10, Phase 27 `.mossi`, and ReturnValue authority

All listed checks passed in Default and EST-shadow, run sequentially to avoid
Margo's shared runtime build cache:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase26_1_1_x1_v10.py tmp/team-c-3ead174e-build/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase26_1_1_x1_v10.py tmp/team-c-3ead174e-build/moss-dev
env -u MOSS_EST python3 tests/tooling/check_phase27_est_mossi.py tmp/team-c-3ead174e-build/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase27_est_mossi.py tmp/team-c-3ead174e-build/moss-dev
env -u MOSS_EST python3 tests/tooling/check_phase28_return_none_authority.py tmp/team-c-3ead174e-build/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase28_return_none_authority.py tmp/team-c-3ead174e-build/moss-dev
```

X1 v10 verified generic Section 1 and ABI-v10 Section 2 binding, private helper
execution, and E1 provider-only recheck. Fast Debug and generated native output
both equal `3\n7\n`. Dropped generic EST fact, perturbed context, duplicated
fact, and wrong consumer specialization each failed closed with
`MOSS_INTERNAL_INVARIANT`. Phase 27 `.mossi` EST checks passed with one generic
and two imported Section 2 dumps. ReturnValue authority, including overloaded
methods, passed in both modes.

The isolated Phase 26 C3 `-Werror` gate passed:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase26_0_1_c3_parse_cache.py \
  tmp/team-c-3ead174e-build/moss-dev
```

Its helper compiles with `-Wall -Wextra -Werror -Wno-missing-field-initializers`
and the negative parse-cache replay checks pass.

One initial X1 v10 Default attempt overlapped other package builds and failed
because the shared runtime archive could not be opened. The sequential rerun
above passed; the transient cache race is not counted as a candidate semantic
failure.

## Fresh frozen v2 oracle and clone comparison

Command:

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler tmp/team-c-3ead174e-build/moss-dev \
  --json-out tmp/team-c-3ead174e-oracle.json \
  --ledger-out tmp/team-c-3ead174e-oracle.md
```

Result: 60/60 semantic PASS, zero engine disagreements, 19/60 all-applicable-
gate PASS, 41/60 provenance-only failures. The candidate and parent-intake
oracle JSON compare exactly on fixture statuses and semantic outcomes. Clone
totals also did not change:

- 258 audited clone observations across 84 applicable native mode sidecars.
- 252 unclassified and unauthorized observations.
- 2 explicit-copy-classified observations and 4 root-ingress-capture-classified
  observations; these are existing classifications, not new exceptions.
- No new clone site was authorized; the frozen runtime allowlist still has zero
  entries.

Three rows have provenance evidence digest changes with no classification or
count change: `ORACLE-10A-EXPLICIT-COPY-ONLY` (explicit-copy semantic identity,
Rust context digest, and generated variant ID), plus
`ORACLE-43-HANDLER-PRIMITIVE-REPLY` and
`ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` (root-ingress capture identity and
Rust context digest). The source spans and origin kinds are unchanged. The
only engine-result JSON difference is ORACLE-54's absolute source path changing
with the verification worktree; checker/Fast Debug/native outputs match.

Frozen input digests remained unchanged:

- expectations: `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`
- diagnostic mapping: `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`
- runtime allowlist: `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`
- certification: `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`

Fresh oracle artifacts:

- JSON SHA-256: `a0cbbbd695fdc2c83c8fc3fb6e7fff51cde9e4f28d70bbb31e1bb089e5da3e6e`
- ledger SHA-256: `3451ff99e5473e2b59c8f538b65de42dc77e23002258188007001a43efdb8cb4`

## Disposition

The generic imported ReturnValue fix passes focused Default/Shadow authority,
X1 v9/v10, Phase 27 `.mossi`, C3 `-Werror`, native/Fast Debug parity, and
fail-closed corruption checks. Frozen semantic behavior and clone totals match
the parent intake; provenance remains unacceptable at 41 fixtures. No full
suite was run, no clone exception was approved, and no whole-Phase-28 GO is
claimed.
