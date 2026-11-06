# P28-R7-010 Team C — Combined Conformance at `3417bedc`

**Result: NO-GO.** This is the first full v2 oracle and independent clone
inventory after integrating Team A's direct indexed-call READ fact and Team
B's matching native consumer. The oracle remains frozen; this report grants
no runtime clone authorization.

## Exact run identity and frozen inputs

- Implementation source SHA: `3417bedcd523dbeff187014de18d4a699ab53b10`
  (`phase28-r7-010-l7-copy-authority`). Team C ran from a separate worktree
  and branch `phase28-r7-010-team-c-combined-conformance` at that exact commit.
- Build command: `make --jobs="$(nproc)" moss-dev` — PASS.
- Compiler SHA-256: `bd758867f614aa9b8329d0dd5397b0979525c6b5fc3d8b5ad869543c9ca349d8`.
- Full oracle command:

  ```sh
  python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
    --compiler ./build/compiler/moss-dev \
    --tmp tmp/team-c-combined-3417/artifacts \
    --json-out tmp/team-c-combined-3417/results.json \
    --ledger-out tmp/team-c-combined-3417/ledger.md
  ```

- Results JSON SHA-256: `ead9cad35b758fa624fdb15df72ef4b919c5cfef9637e8f164417e808de81f1e`.
- Harness ledger SHA-256: `cd54100485f2b1b5566961f9f55d3f9759e0f5a05ba4418636c8db754ff0c862`.
- Clone inventory JSON SHA-256: `cbbd462d21eacfa628e83fa8f2d3be22934cf9704d5614d63c7dda0c1537add3`.
- Manifest SHA-256 over 84 sorted Rust artifacts and their 84 paired sidecars,
  using `path + NUL + 8-byte big-endian content length + content`:
  `7e2b4b9b6001d9aa4ec022256e6da8dc9507fb40c05c44bf4c089a8a91499152`.
- Frozen inputs are unchanged: v2 expectations SHA-256
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`,
  diagnostic mapping `38eb36e06d4df980181174299680e3db360da0c8c54eeb95ec19273ccc9de1cd`,
  certification `00e9d648286a90de7266622f18dad6f2b53693c5b3645cb37cb2b30bb4c4ccc9`,
  shared runtime allowlist `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`.
  The certified v1 expectations remain `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`.

## Failure ledger

| Category | Result | Interpretation |
| --- | --- | --- |
| Semantic expectations | **60/60 PASS; 0 semantic failures** | Every fixture matches its frozen checker/execution expectation. |
| Native/Fast Debug disagreement | **0 disagreements** | 40 ordinary execution fixtures pass in Fast Debug and Native Debug; 39 pass in Native Optimized; the remaining executed fixture, ORACLE-47, produces its expected panic in all three. The other 19 fixtures are expected checker outcomes without execution artifacts. |
| Clone provenance | **19/60 all-applicable PASS; 41/60 provenance-only FAIL** | All 41 artifact-bearing fixtures fail only because runtime clone observations remain unauthorized/unclassified. No sidecar is missing or invalid. |
| Infrastructure | **0 fixture failures** | Compiler build, oracle execution, Rust artifact generation and applicable native builds completed. |

The all-gates result is **19/60**, semantic result **60/60**, with no engine
disagreement. No semantic or native/Fast Debug issue is hidden inside the 41
provenance failures. Default and EST-shadow full repository suites were not run;
they remain L7's later validation gate.

## Per-fixture comparison

The previous comparison is Team C's independently reviewed full run at source
`1d3be42d` (results SHA-256
`d82183c4424cc4182a531759a35dbb35d63df9188d92e1b8e76c50fa2f7bbec5`). The
60-row comparison is in
[`P28_R7_010_FIXTURE_DELTAS_3417BEDC.md`](P28_R7_010_FIXTURE_DELTAS_3417BEDC.md).
Every fixture has the same all-gates, semantic and provenance verdict as the
prior ledger. The sole raw-JSON difference is ORACLE-54's absolute
`source_file` path in checker stdout, which follows the isolated worktree root;
diagnostic, checker verdict, semantic result, engine outcomes and provenance
outcomes are identical.

## Direct `.clone()` occurrences and authority

The inventory scanned **84 Rust artifacts** (96 raw `.rs` files included 12
Cargo `build.rs` files that the scanner excludes). It found **258 direct
`.clone()` occurrences** in six generated-site groups:

| Site | Count | Evidence | Disposition |
| --- | ---: | --- | --- |
| Runtime active executor (`MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-01`) | 84 | `self.lock_inner().active_executor.clone()` | Unclassified and unauthorized |
| Runtime affinity (`...runtime-02`) | 84 | `self.affinity.clone()` (`Option<Vec<usize>>`) | Unclassified and unauthorized |
| Runtime branch-scope handle (`...runtime-03`) | 84 | `s.clone().into_raw_owned()` | Unclassified and unauthorized |
| Root-ingress `w.Process(42)` capture, ORACLE-43 | 2 | Exact fact ID `root-ingress-domain-capture-fact-v1:742fa3d62ed31f496a4ad8b9d1ff33435a4d887b4ef548b96c88416e50c631bb` | Capture-only; `permits_moss_value_copy=false`; not Moss copy authority |
| Root-ingress `w.Increment(10)` capture, ORACLE-45 | 2 | Exact fact ID `root-ingress-domain-capture-fact-v1:56b26210213ca7a86ea7cc7ab1176e8e09ae4a0c3037bc6a22c2a3be8fd09bf0` | Capture-only; `permits_moss_value_copy=false`; not Moss copy authority |
| Explicit `copy(v[0])`, ORACLE-10A | 2 | Present `TypedCopyFactV1`, source `copy@7:16`, exact ID `typed-copy-fact-v1:d28651f281792b4a13db2d162e5d1819b290399fab0ba98d1280f6bd0d7d8015`, required access READ, independent owner true | Checker-authorized explicit copy |

Thus **252 rows remain unverified and unauthorized** (three runtime templates
repeated over 84 artifacts); four rows have exact Root-ingress capture facts
that explicitly deny Moss-value-copy permission; two rows are exact checked
`copy(...)` calls. The 12 ORACLE-54 consumer/provider runtime observations are
included in the 252: the 84 provenance audit entries are 40 Native Debug, 40
Native Optimized, and ORACLE-54's four consumer/provider mode artifacts, each
with three unclassified runtime rows. They are not 12 additional observations.
The inventory total is therefore `252 + 4 + 2 = 258`.

The independent inventory reports **1,876 candidates in 36 site groups**:
588 `Arc::clone` calls across seven runtime sites (84 per site), 756
`.to_string()` calls across nine sites, 252 `mem::replace`, 14 `mem::take`,
six `.copied()`, and two `String::from`/`Vec::from` candidates, in addition to
the 258 direct `.clone()` calls. The 588 Arc calls remain unapproved; no
checker authority or runtime-mechanical exception was found for them. The
other operators have mixed semantics and are not reclassified as Moss copies
by lexical counting. The scanner found 408 generated Moss `Clone` derive
declarations; these are capabilities, not invocations or authority.

## Targeted authority and audit gates

- `check_phase28_indexed_call_argument_read.py`: PASS on exact compiler.
- Team B `check_indexed_call_argument_read_consumer.py`: PASS for Native
  Debug/Optimized, Fast Debug and six missing/duplicate/perturb corruption
  cases.
- `check_phase28_root_ingress_provenance.py`: **42 checks PASS**.
- The raw-path version of `check_phase28_r7_010_runtime_clone_evidence.py`
  in the staging tree failed at its whole-artifact SHA assertion because the
  generated explicit-copy helper/fact IDs include the worktree source path.
  This was a tooling path assumption, not an oracle or provenance result. The
  previously Agent-2-GO-reviewed normalized script from Team C commit
  `bbe0c6bd8ba5ce598463159e5794f342ea34f7b9`, run with the exact `3417` compiler
  and unchanged fixture content, **PASS**: ten runtime markers, one checked
  explicit copy, three direct runtime clones still unclassified; marker drop,
  duplicate, swap and relocation checks reject. The harness bytes used by
  both worktrees are identical (SHA-256
  `0737ca98607bb4b133567a6f40215088adbef5534acea40f875c975f3e3f4bdd`).
- Oracle harness unit tests: **39 PASS**; clone-inventory unit tests: **7
  PASS**; total **46 PASS**.

The normalized runtime gate does not classify runtime clones as authorized.
The staging-tree raw hash assertion failure is recorded separately from the
full oracle's zero infrastructure failures.

## Limits and disposition

This is not a Rust AST/MIR, trait-resolution, macro-expansion or complete
`Copy`-load audit; custom helpers could duplicate values without a scanned
spelling. The audit makes no authorization inference from emitted markers,
runtime type names, `Arc` behavior, derives or repeated code. Team C's full
oracle delivery remains **NO-GO** until the 252 runtime rows and the 588 Arc
sites receive acceptable independently checked provenance or are removed.
Final default/EST-shadow suites and fresh independent review on this exact
implementation are pending.
