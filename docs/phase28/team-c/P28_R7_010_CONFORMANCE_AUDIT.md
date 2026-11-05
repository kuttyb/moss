# P28-R7-010 Team C — Checkpoint Conformance Audit

**Base SHA:** `0456446451e0df8dd11794eb2f37e438781dc926`  
**Branch:** `phase28-r7-010-team-c-conformance`  
**Scope:** independent oracle reproduction, clone-site inventory, and provenance
validator adversarial review. This report does not authorize compiler behavior.

## Reproducible baseline

- Bootstrap reports `language_version=moss-0.1`; the Team C worktree built from
  the immutable WIP base with `make --jobs="$(nproc)" moss`.
- The v2 oracle was run with isolated outputs and artifacts:

  ```sh
  python3 tests/phase28/oracle_r7_005_1/oracle_harness.py \
    --compiler ./moss \
    --tmp tmp/team-c/oracle-artifacts \
    --json-out tmp/team-c/oracle-isolated/results.json \
    --ledger-out tmp/team-c/oracle-isolated/ledger.md
  ```

- Result matches the parent checkpoint exactly: **13/60** all applicable
  gates pass; **15** semantic failures; **35** fixtures are provenance-blocked;
  the direct fixture artifacts contain **524** unverified `.clone()` calls.
  Categories overlap. No clone count is treated as a count of illegal Moss
  copies.
- Oracle harness unit tests: `PYTHONDONTWRITEBYTECODE=1 python3
  tests/phase28/oracle_r7_005_1/test_oracle_harness.py -q` — **26 passed** after
  adding exact checker-fact forgery, missing-authority, perturbation,
  duplicate-fact, and UTF-8 byte-column cases.
- Independent scanner unit tests: `python3 tests/tooling/test_phase28_r7_010_clone_inventory.py -v`
  — **4 passed**.

## Independent generated-Rust inventory

The scanner in `tests/tooling/check_phase28_r7_010_clone_inventory.py` scans
generated Rust artifacts outside comments and literals, records `.clone()` and
alternate duplication/move candidates, and groups candidates by Moss tooling
region/source line or Rust runtime function/snippet. It is a triage scanner,
not proof of authorization and not a complete Rust semantic audit.
The detailed site grouping and Team B emitter-inventory cross-check are in
[`P28_R7_010_GENERATION_SITE_AUDIT.md`](P28_R7_010_GENERATION_SITE_AUDIT.md).

- Scanned **71** generated Rust files: 70 direct fixture artifacts and
  ORACLE-54's generated `provider.rs`; Cargo `build.rs` artifacts are excluded.
- Found **530** `.clone()` calls in **52** grouped generation-site
  fingerprints: **524** in direct fixture artifacts plus **6** in the imported
  provider Rust. The six provider calls are absent from the current oracle
  result's `clone_audit` field, which is empty for ORACLE-54. They are an
  additional unverified artifact set, not an authorized exception.
- Of the 530 calls, 486 are in generated runtime/helper regions and 44 are
  under Moss source tooling markers. This is a source-location classification,
  not a semantic authorization.
- Six runtime helper sites repeat in each artifact (426 observations total).
  Twenty-five generated `__moss_value` type-specific sites account for 60
  observations. The `__moss_value` builder in `src/borrowed_views.inc:140-143`
  clones fields for non-viewable types while materializing an object from a
  borrowed view. Team B independently confirmed that String and collection
  leaves can pass through this mechanism. This is a confirmed value-duplication
  mechanism; each call's actual semantic intent and authority remain
  unverified. It must block approval unless replaced by a checker-authorized
  borrow/move path or exact `TypedCopyFactV1` authority. It is not eligible for
  runtime-mechanical classification.
- The 44 Moss-marked calls include four generated calls corresponding to the
  fixture's explicit `copy(v[0])` across debug/optimized artifacts. The other
  40 are also unverified; generated source markers alone do not grant copy
  authority.
- Alternate static candidates found: `.cloned()` 4, `.copied()` 6,
  `.to_string()` 639, qualified `Clone::clone`/`Arc::clone`/`Rc::clone` 639,
  `mem::replace` 213, `mem::take` 14, and `String::from`/`Vec::from` 2.
  These are candidates with mixed semantics (including moves and string
  conversion); counts do not imply duplication. Macros, Rust `Copy` loads and
  other semantic mechanisms still require independent AST/MIR review.

Raw inventory is in disposable `tmp/team-c/oracle-isolated/clone-inventory.json`
and `.md`; the deterministic scanner and tests are the reviewable deliverable.

## Provenance authority regression

The current `audit_clone_provenance` accepted a forged explicit-copy claim:

1. Build the existing unit manifest for Moss source `value = copy(item)` and
   Rust `value.clone()`.
2. Change only the sidecar evidence to `semantic_type="FileIO"`,
   `ownership_fact="CONSUME_NOT_READ"`, and
   `semantic_identity="forged:unrelated-binding"`.
3. Run `audit_clone_provenance(rust_path, source_path, "debug", 1)`.
4. Observed result: `status="PASS"`, `authorized=1`.

The public `moss inspect <source>:line --json` and `moss ownership
<source>:line --json` currently return a binding-level result, not copy facts.
`--emit-debug-map` also has only enclosing-statement line mappings. Thus a
sidecar's nonempty evidence strings cannot independently establish that a
checked `TypedCopyFactV1` authorized that exact source occurrence.

L7 approved a checker-owned `copy_authority.facts` query result and exact
sidecar join. The harness now validates the agreed `TypedCopyFactV1` schema,
source digest, byte and coordinate spans, copy/source ExprIds, EST arena,
statement occurrence, specialization identity/context and fact context, types,
READ access, availability, provenance and independent-owner flag. Sidecar facts
must equal the query row exactly; the frozen lexical `copy@line:column`
locator remains the occurrence key, while the canonical fact digest binds
`semantic_identity`. Type and access evidence are derived from that row.
Missing checker authority is classified `PROVENANCE_MISSING`; arbitrary
nonempty strings no longer authorize a clone. Unit coverage is green, but the
the harness runner now requests `moss ownership at:<line>:<column> --source
<file> --json` at the first copy occurrence and passes `result.copy_authority`
into both debug and optimized provenance audits. The checkpoint compiler does
not yet return `copy_authority`, so it correctly stays blocked. Team B's final
sidecar helper API is not frozen yet. No compiler clone provenance has been
independently verified by this change alone.

## Remaining Team C work

- Validate the live query against Team A's frozen fact API and Team B's frozen
  sidecar helper, then audit their frozen delivery SHAs.
- Independently audit the frozen Team A/B delivery SHAs, including all debug
  and optimized artifacts and ORACLE-54 provider output.
- Continue reporting alternate operator and macro/MIR coverage limits; no
  unverified occurrence may be marked authorized.
- Team C has not frozen a delivery SHA, received independent review, or pushed
  its branch yet.
