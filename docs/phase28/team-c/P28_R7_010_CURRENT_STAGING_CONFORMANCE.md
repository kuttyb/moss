# P28-R7-010 Team C — Current Staging Conformance

**Result: NO-GO.** This report is an independent Team C run against the
registered checker staging candidate. It does not authorize compiler behavior
or alter frozen oracle expectations.

## Frozen source and inputs

- Branch: `phase28-r7-010-team-c-current-conformance`
- Exact source SHA: `1d3be42dbf99fef855f58ed9cf19946f2926fcad`
- Compiler: `moss-0.1`; built from this exact worktree with
  `make --jobs="$(nproc)" moss`.
- Compiler binary SHA-256: `86d0acdba671c223393d287fd782b65455e480dfc414b28a08f81f616b510c5e`.
- Frozen v2 expectations SHA-256:
  `7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2`.
- Certified v1 expectations SHA-256 (unchanged; not used as the acceptance
  oracle): `223a15436046767175de9c1f30637bb80c76505cb402f5dadb700c64520f4431`.
- v2 results, ledger, generated artifacts, and clone inventory are under
  `tmp/phase28-r7-010-team-c-current/oracle-v2/`.
- Results JSON SHA-256:
  `d82183c4424cc4182a531759a35dbb35d63df9188d92e1b8e76c50fa2f7bbec5`.
- Clone inventory JSON SHA-256:
  `1025910340b4ca663d1e79ca526cf61e4e3b8de8df16b8aba16e50981208ba8b`.
- Manifest SHA-256 over the 84 Rust artifacts and 84 sidecars, sorted by
  artifact-root-relative path and hashed as `path + NUL + 8-byte big-endian
  content length + content`:
  `f418e859e667ad93ed92f83f8350d28aceb2f3b5606206bbc089e23af46cc7ed`.

## Failure ledger

| Category | Result | Evidence |
| --- | --- | --- |
| Semantic expectations | 60/60 pass; zero semantic failures | `results.json`, `ledger.md` |
| Fast Debug / Native Debug / Native Optimized parity | Zero disagreements. 39 ordinary execution cases agree; ORACLE-47's expected panic agrees in all three modes. Twenty negative fixtures reject during checking and have no execution result. | `results.json` engine rows |
| Clone provenance | 19/60 pass all applicable gates; 41/60 fail only provenance. There are 252 direct runtime clone rows still unclassified/unauthorized. | `ledger.md`, sidecars |
| Oracle infrastructure | No oracle build, fixture-run, or artifact-generation failures. | Harness completed all 60 fixtures |
| Targeted provenance tooling | Root-ingress provenance gate: 42 checks pass. Runtime clone evidence gate: passes after the narrow root-derived identifier normalization below. | Commands below |

The full oracle result is **19/60 all-applicable PASS**, **60/60 semantic
PASS**, **41/60 provenance-only failures**, and **zero engine disagreements**.
The 41 count is a clone-provenance gate result, not a semantic failure count.

## Generated duplication inventory

The scanner examined 84 Rust outputs with sidecars (96 raw `.rs` files also
include 12 Cargo `build.rs` files) and found 1,876 candidate operations in 36
source-site groups. Direct `.clone()` observations:

| Site | Count | Authorization status |
| --- | ---: | --- |
| Runtime active executor template | 84 | Unclassified and unauthorized |
| Runtime affinity template | 84 | Unclassified and unauthorized |
| Runtime branch-scope template | 84 | Unclassified and unauthorized |
| Root-ingress capture for ORACLE-43, Debug/Optimized | 2 | Exact checker capture fact; `permits_moss_value_copy=false` |
| Root-ingress capture for ORACLE-45, Debug/Optimized | 2 | Exact checker capture fact; `permits_moss_value_copy=false` |
| Explicit `copy(v[0])`, Debug/Optimized | 2 | Exact `TypedCopyFactV1` rows |
| **Total direct `.clone()`** | **258** | **252 remain unclassified/unauthorized** |

The inventory also reports 588 `Arc::clone` calls across seven runtime sites;
all remain unapproved. Other lexical counts are `.copied()` 6,
`.to_string()` 756, `std::mem::replace` 252, `std::mem::take` 14, and
`String::from`/`Vec::from` 2. These counts do not establish semantic copy
authority. The scanner found 408 generated `Clone` derive declarations, which
are capabilities rather than invocations.

## Worktree-root identity and runtime evidence test

The compiler binary has the same SHA in L7 and Team C worktrees, but checked
fact IDs include source provenance. Generating ORACLE-10A using each worktree's
fixture path therefore changes the explicit-copy helper/fact identifier and
the whole-file hash, even though the runtime template code is identical:

| Worktree root | Marker-stripped raw Rust SHA-256 | After normalizing the two helper-name and one explicit-copy fact-ID occurrences |
| --- | --- | --- |
| L7 `phase28-r7-010-l7-query` | `113847626e46a1ec7e92107fa9c45d207f35664b27eccafcb6c5315f844e45e6` | `5038578ee5995567c7ea9d273c4ba63be94aa7a4a58e975d045c4ea905b22717` |
| Team C `phase28-r7-010-team-c-current` | `f19f94863587540830d93b4ffef219bfe83da4b0ebf04ef50dc86e68d0480088` | `5038578ee5995567c7ea9d273c4ba63be94aa7a4a58e975d045c4ea905b22717` |

The Team C change to
`tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py` normalizes only
the explicit-copy helper/fact identifiers for this one-copy fixture. It still
requires the exact count and shape of those identifiers, checks every runtime
marker/operator pairing, queries live copy and Root-ingress authority, and
audits the exact sidecar. The Team C-root invocation now passes with ten marked
runtime sites, one checked explicit copy, and three runtime direct clones left
unclassified/unauthorized. The exact L7-root invocation also passes; no source
or oracle expectation was changed. Independent review of the Team C test change
is pending.

## Reproduction commands

```sh
python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
  --compiler ./moss \
  --tmp tmp/phase28-r7-010-team-c-current/oracle-v2/artifacts \
  --json-out tmp/phase28-r7-010-team-c-current/oracle-v2/results.json \
  --ledger-out tmp/phase28-r7-010-team-c-current/oracle-v2/ledger.md
python3 tests/tooling/check_phase28_r7_010_clone_inventory.py \
  tmp/phase28-r7-010-team-c-current/oracle-v2/artifacts \
  --json-out tmp/phase28-r7-010-team-c-current/oracle-v2/clone-inventory.json \
  --markdown-out tmp/phase28-r7-010-team-c-current/oracle-v2/clone-inventory.md
python3 tests/tooling/check_phase28_root_ingress_provenance.py ./moss
python3 tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py \
  ./moss tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss
MOSS_PHASE28_QUERY_COMPILER=./moss PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest tests.phase28.oracle_r7_005_1.test_oracle_harness \
  tests.tooling.test_phase28_r7_010_clone_inventory -q
python3 tests/tooling/check_agent_feedback.py
```

The oracle harness and inventory tests passed **46 tests**; the inventory-only
run passed **7 tests**. The full repository default and EST-shadow suites were
not run by Team C; L7 owns those final gates.
