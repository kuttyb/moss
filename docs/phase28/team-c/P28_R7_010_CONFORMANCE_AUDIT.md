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
- Oracle harness unit tests with the live L7 checker query prototype:
  `MOSS_PHASE28_QUERY_COMPILER=/home/owner/daji/code/moss/tmp/phase28-r7-010-l7-query/moss
  PYTHONDONTWRITEBYTECODE=1 python3
  tests/phase28/oracle_r7_005_1/test_oracle_harness.py -q` — **38 passed**.
  Coverage includes exact checker-fact forgery, missing/perturbed/duplicated
  authority, multiple specializations at one source span, live query retries,
  UTF-8 byte columns, CRLF, two lexical copies sharing each emitted
  specialization context, and template fact sorting before/after concrete
  facts.
- Independent scanner unit tests:
  `PYTHONDONTWRITEBYTECODE=1 python3
  tests/tooling/test_phase28_r7_010_clone_inventory.py -q` — **5 passed**,
  including whitespace-separated `. clone ( )` token detection.
- With live query compiler configured: **38 passed**; without it: **38 passed,
  1 live integration test skipped**. The template sort-order regression uses
  source paths that deterministically put its fact before and after concrete
  facts, preventing absolute checkout paths from hiding order dependence.
- `python3 tests/tooling/check_agent_feedback.py` and `git diff --check` pass.

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

L7 approved a checker-owned `copy_authority.facts` query result and v3
provenance schema. The harness validates each exact `TypedCopyFactV1` row:
source digest, byte and coordinate spans, copy/source ExprIds, EST arena,
statement occurrence, specialization identity/context and fact context, types,
READ access, availability, provenance and independent-owner flag. The frozen
lexical `copy@line:column` locator remains the source occurrence key; the
canonical complete fact hash binds per-clone `semantic_identity`.

The v3 manifest has exactly `format`, `version`, `mode`, Rust/source digests,
`source_copy_occurrences`, top-level `checked_copy_facts`, and `occurrences`.
There is one lexical source-copy row and one fact-group wrapper per lexical
copy; each group carries every checker row for that occurrence, including
uninstantiated template contexts. Fact groups follow source order, and rows
sort by canonical fact ID. V1/v2 oracle expectation and certification files
remain untouched.

For each emitted fact-specific helper, the harness parses raw Rust call markers
and requires one exact-fact marker, one helper definition, one direct invocation
and no aliases or extra callers. It derives helper body clone spans and checks
them against sidecar occurrences and the source row's clone-span union. Shared
`__moss_value` materializers remain unclassified and blocking. An empty-ID
template fact may lack a helper only when raw Rust callable markers show its
exact base callable is absent; nonempty specialization facts without helpers
block. Callable normalization collisions and emitted source lines without a
mapping fail closed.

The runner retries the same exact ownership source location using documented
kind filters, accepting only identical source-complete authority rows across
successful retries. Live tests with the L7 query prototype passed for
ORACLE-10A, a same-line UTF-8 copy call, CRLF source, and the
multi-specialization fixture.
That fixture returns three facts at one lexical span: a base template and two
concrete specializations. The checkpoint compiler lacks `copy_authority`, so
it remains blocked; compiler clone provenance is not verified until Team B's
frozen v3 artifacts are independently audited.

## Independent review and current status

Initial candidate `ac89db6c7221e86747dcd611e7bafbee3a99b5da` received
independent Agent 2 **NO-GO**. The review found that the one-fact-per-lexical-
span join rejected valid specialization collisions, source-to-Rust links were
self-asserted by sidecar, and the clone scanner missed whitespace-separated
tokens. This follow-up adds multi-specialization fixtures with both one and
multiple lexical copy sites per specialization, plus v3 fact groups,
raw Rust helper derivation, callable inventory checks, shared token scanning,
same-location query retries, live compiler query coverage, strict occurrence
schemas keyed by source occurrence, and whitespace cleanup. The v3 serializer
changes are not yet frozen by Team B, and the follow-up has not received fresh
independent review.

The L7 integration rerun found a path-dependent stale-variable reference in
the template-fact loop. The current follow-up uses the current checked fact
throughout and tests both canonical sort orders using different source paths.
The full live-query and no-live-query test runs both pass; a fresh Agent 2
review of this repair remains pending.

## Frozen Team B vector-read artifact audit

The certified v2 IDs are ORACLE-39 Vector-for-read and ORACLE-42 custom-wrapper
remove. Team B's test-only correction at `44513837` now names and exercises
those fixture IDs correctly. Both Team B legacy fixture files have the same
SHA-256 bytes as the corresponding frozen v2 fixture files
(`oracle_39_vector_for_read.moss`: `338635a7...572cf64`;
`oracle_42_custom_wrapper_remove.moss`: `3104b223...e2c595`). The shared
expectations digest remains `7ca0d550...657f23a2`.

On Team B frozen candidate `44513837bea00ed5071342053f2f724d17ae617e`, the
independent v2 fixture evaluator observed checker, Fast Debug, Native Debug and
Native Optimized all match expected output for both fixtures. The complete
evaluation is nevertheless **FAIL** for both because isolated native outputs
have no `.mossclone.json`: each debug/optimized artifact has six unclassified
runtime `.clone()` calls, so strict clone audit returns `PROVENANCE_MISSING`.
This sidecar requirement is an L7 integration dependency for the isolated B
artifacts, not proof of a loop-element clone.

The corrected Team B gate script passes for both fixtures and a borrowed-read
then write lifetime probe. Generated ORACLE-39 code uses `(v).iter()` and
prints `(item).id`; ORACLE-42's final indexed echo prints the indexed place
directly. Raw application bodies show no clone call. Loop-fact drop, perturb
and duplicate hooks on ORACLE-39 fail Fast Debug and both native emitters with
`MOSS_INTERNAL_INVARIANT`; the ordinary checker remains successful because
these hooks perturb consumers, not checker output.

The expanded inventory separately reports generated Moss `Clone` derive
capabilities. Across the 71-file checkpoint set it found 344 declarations
(26 unique Moss type/enum names); on Team B's six focused artifacts it found
28 declarations, including `Node` and `Bag`. These are capabilities, not clone
occurrences. The ORACLE-10A `Record` derive at Rust line 3402 has one observed
fact-specific helper use at line 3448; the independent provenance validator
matches that call to its checker fact. No other derived-type call reachability
is established by the scanner, which remains an explicit audit limit.

## Remaining Team C work

- Audit Team B's frozen v3 helper artifacts against raw Rust derivation, then
  audit all frozen Team A/B delivery SHAs, including debug/optimized artifacts
  and ORACLE-54 provider output.
- Continue reporting alternate operator and macro/MIR coverage limits; no
  unverified occurrence may be marked authorized.
- Live Team A/B artifact validation remains pending until they freeze deliverables.
- This harness-only follow-up is being frozen as a local review candidate;
  there is no fresh independent verdict yet. Do not push until it receives
  independent GO.
