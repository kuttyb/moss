# P28-R7-010 Team C — L7 Frozen Baseline Clone Audit

**Result: NO-GO.** This is a refreshed, targeted audit of the frozen L7 staging
source. It is not the final combined conformance run and authorizes no runtime
clone or Moss-value duplication.

## Frozen source and oracle integrity

- Staging branch/source: `phase28-r7-010-l7-copy-authority`, implementation
  source SHA `d1bb2303c0cfa8a9020b34a5a302da0f934385f9`; the staging branch
  currently also has status-only commit `636a5a06439fe8ed447500341f8cc71620a9c724`.
- Exact-worktree `moss-dev` build: PASS; compiler SHA-256
  `f421cd2f9d63f8757f80c6f2dfe4ce15a69ee8c2fc713f37bba1d408081378fc`.
- v1 expectations SHA-256 remains
  `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`;
  v1 certification file SHA-256 is
  `a7887706c550b14f19500da5c54c55c1ebd1f4de54d89d4bc19d25c7eb8d4a94`.
- v2 expectations SHA-256 remains
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`;
  v2 certification file SHA-256 is
  `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`.
- Both oracle versions retain the runtime-callsite allowlist digest
  `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`.
  No expectation, mapping, certification, or allowlist file was edited.

## Exact-source targeted artifact

No full 60-fixture oracle was run during Team B's native consumer work. I built
the compiler from this worktree and generated only the frozen ORACLE-10A
explicit-copy fixture into `tmp/team-c-refresh/oracle_10a.rs` (Rust SHA-256
`e153f3e3ad009b83a8093ba7bfc90f6b601def02cab8eed10493955d88ba28ab`). The
artifact contains ten marked runtime sites: three direct runtime `.clone()`
templates and seven qualified `Arc::clone` sites. Its sidecar classifies the
one source `copy(...)` as `explicit_copy` and the three direct runtime clones as
`unclassified`.

The clone scanner found 23 candidates in this one generated artifact: four
`.clone()` calls (one exact explicit copy and three runtime candidates), seven
`Arc::clone` calls, nine `.to_string()` calls, and three `mem::replace` calls.
There were no `.cloned()`, `.copied()`, `.to_owned()`, `.to_vec()`,
`mem::take`, `String::from`, `Vec::from`, or `ptr::read` tokens in this
artifact. The scanner also found five generated Moss `Clone` derives; these
are capabilities, not invocations. Candidate counts do not establish Moss
copy semantics.

The targeted `check_phase28_r7_010_runtime_clone_evidence.py` gate passes on
this exact compiler and source. It verifies the current ten runtime markers,
the exact explicit-copy sidecar row, and missing/duplicated/swapped/relocated
marker rejection. Its root-path normalization is restricted to the two
explicit-copy helper identifiers and one fact ID; the independent fact and
sidecar checks remain active. The same normalization was independently
reviewed GO on Team C commit `bbe0c6bd8ba5ce598463159e5794f342ea34f7b9`.
The harness plus clone-inventory unit tests pass **46 tests**; the inventory
unit subset passes **7 tests**.

## Full-corpus counts and source-site reconciliation

The last full frozen-v2 artifact run recorded in the L7 checkpoint was made
before the current Team A checker-only module-provenance repair. Its outcome
was 19/60 all-applicable PASS, 60/60 semantic PASS, 41/60 provenance-only
failures, and zero engine disagreements. It counted **252 direct runtime
`.clone()` observations** in 84 Rust artifacts: three repeated runtime
templates, 84 observations each. Those 252 observations remain unclassified
and unauthorized. Four additional Root-ingress captures have checker facts
with `permits_moss_value_copy=false`; two explicit-copy rows are linked to
`TypedCopyFactV1`. Neither capture fact authorizes Moss-value copying.

The current source bytes for `src/native_generator.cpp`,
`src/executor_runtime.cpp`, and `src/char_string_ordering_runtime.cpp` are each
identical to the source bytes at Team B audit base `ae57a3e3`; this was verified
by exact file SHA-256. The exact-current ORACLE-10A artifact confirms the same
three direct runtime sites and seven Arc sites. The **252** full-corpus total
is therefore a carried-forward full-run measurement supported by unchanged
emitter/runtime sources and a fresh targeted artifact; it is not represented
as a newly rerun 60-fixture result. Team C must rerun the complete oracle after
Team A and B freeze and L7 integrates their reviewed source.

The earlier **524** direct `.clone()` count belongs to the older WIP checkpoint
and is stale for this staging generation. It must not be carried into the
current ledger. The previous Team C full-corpus audit also recorded 588
qualified `Arc::clone` observations across seven runtime sites, 1,876 candidate
operations in 36 groups, 756 `.to_string()` observations, 252
`mem::replace`, 14 `mem::take`, six `.copied()`, and zero `.cloned()` calls.
Those inventory totals are prior full-artifact observations, not fresh counts
on the current exact source. The focused current artifact confirms the seven
Arc sites and the presence of mixed-semantics conversion/replace operations.

The prior Team C independent GO report at
[`P28_R7_010_CURRENT_STAGING_CONFORMANCE.md`](P28_R7_010_CURRENT_STAGING_CONFORMANCE.md)
and generation-site report remain preserved. Their exact full-run source SHAs
and counts are historical evidence; this report does not rewrite them.

## Commands and limits

```sh
make --jobs="$(nproc)" moss-dev
python3 tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py \
  ./build/compiler/moss-dev \
  tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss
python3 tests/tooling/check_phase28_r7_010_clone_inventory.py tmp/team-c-refresh \
  --json-out tmp/team-c-refresh/inventory.json \
  --markdown-out tmp/team-c-refresh/inventory.md
MOSS_PHASE28_QUERY_COMPILER=./build/compiler/moss-dev PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest tests.phase28.oracle_r7_005_1.test_oracle_harness \
  tests.tooling.test_phase28_r7_010_clone_inventory -q
```

The scanner does not expand macros, inspect Rust AST/MIR or trait resolution,
analyze `Copy` loads, or prove absence of custom duplication helpers. Its
generation-site grouping is an output-source locator, not a compiler semantic
authority. Full oracle, engine parity, default/shadow suites, and a fresh
independent review on the frozen refreshed report are pending. **Acceptance
remains NO-GO.**
