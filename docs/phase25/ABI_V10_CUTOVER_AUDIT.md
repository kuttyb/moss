# Phase 25 ABI-v10 Cutover Audit

**Audit baseline:** `phase25-integration` at
`46bb9cc8c7c1a74ee3ca45f43b03420fb8515443`

**Audit branch:** `phase25-abi-v10-cutover-audit`
**Scope:** investigation and this report only; no compiler implementation or
full repository regression run.

## Executive findings

ABI-v10 is a sealed artifact format, but generic body consumption is still a
hybrid. The loader validates the v10 seal, ABI-v10 records, their identities,
hashes, dependencies, and provenance references. It then projects declaration
payloads into the same text parser used by ABI-v9. That parser still rebuilds
`Function::body` from `generic_stmt`. A few matching typed-summary/body pairs
are later decoded and replace that reconstructed body before checking; other
open/static-dispatch callables keep using the legacy reconstruction path.

Consequently, retaining `generic_stmt` as compatibility data is safe only while
the ABI-v10 consumer has an explicit checked-body selection rule and does not
silently treat that legacy body as authoritative. That rule is currently
implemented for selected producer subsets, not for every source-free generic
callable. A cutover must close the remaining subsets before making legacy
reconstruction unreachable in ABI-v10 mode.

The checked-body wire encoder is much broader than its source-free consumer.
The encoder can encode several statements and expression categories, but the
H3 decoder accepts only a single return or a two-statement `let`/`return`
shape, and accepts only a narrow set of expression nodes. The provider summary
and checked-body emitters are narrower still.

## 1. Legacy and typed-record paths

### Legacy generic records

`write_module_interfaces()` serializes legacy generic declarations in
`src/moss.cpp:40720-40769`; private generic helper closures are serialized at
`src/moss.cpp:40772-40814`. The records include `generic_ir_version 1`,
`generic_entity_id`, `generic_source`, generic parameters/constraints,
`generic_stmt`, and `generic_result` where applicable. The same declaration
text is the ABI-v9 interface format and is also embedded as declaration
payloads in ABI-v10 semantic records.

The reader is `parse_generic_ir_line()` at `src/moss.cpp:36411-36467`:

- `generic_stmt` rebuilds the executable `Function::body` at lines 36444-36458.
- `generic_source` sets `provider_origin_file` at lines 36459-36460; it is also
  used when constructing the imported summary origin (`src/moss.cpp:4437-4440`).
- `generic_param`, `generic_constraint`, and `generic_result` rebuild
  specialization metadata or the result expression.
- `generic_ir_version` is recognized by the interface loop at
  `src/moss.cpp:37889-37897`, but `parse_generic_ir_line()` has no branch for
  it. The version marker is emitted but not checked by this parser.

The interface loop parses these records for both ABI modes. In ABI-v9 they are
the source-free generic reconstruction format. In ABI-v10 they remain a
fallback for open/static-dispatch bodies that are not projected from matching
checked-body records. The arithmetic writer selectively removes `generic_stmt`
for matched arithmetic callables at `src/moss.cpp:41010-41132`; the empty
vector writer deliberately retains it, and other records remain as emitted.

**Dependency classification:** `generic_stmt` is required by ABI-v9 generic
source-free reconstruction and remains required by ABI-v10 for uncovered
callables today. `generic_source` is required by current ABI-v9/v10 diagnostic
origin projection. `generic_ir_version` is emitted in both and ignored during
decoding. Retaining these records is not itself a blocker; executing from them
in ABI-v10 is the cutover dependency.

### Typed summaries, checked bodies, and provenance

The checker owns the in-memory facts. `Checker::retain_checked_body_ir()` at
`src/moss.cpp:4234-4377` builds a narrow checked-body projection from completed
open summaries; `Checker::build_open_callable_summaries()` begins at
`src/moss.cpp:4380`. `TypedSemanticApi::collect` copies complete summary facts,
optional checked body, requirements, dependencies, effects, and provenance at
`src/typed_semantic_api.hpp:317-335`. An authoritative in-memory open summary
does not imply that an ABI-v10 typed-v2 summary or body is emitted.

The ABI-v10 codecs live in `src/abi_v10_records.hpp`:

- Typed summary v2 encoding/decoding and record IDs:
  `summary_payload`, `read_summary`, `summary_record`, and `validate_summary`
  at lines 324-420.
- Checked-body record framing, body hash verification, and callable ID binding:
  `checked_body_record_payload`, `read_checked_body_record`, and
  `validate_checked_body_record` at lines 422-508.
- Source provenance schema and canonical decoding at lines 561-623.
- Whole-interface validation validates record schemas, uniqueness, hashes,
  dependencies, and summary/template provenance references at lines 1245-1367
  and 1539-1595.

The writer calls four bounded producer functions from `write_module_interfaces`
at `src/moss.cpp:41006-41009`:

1. `append_abi_v10_direct_checked_records()` (`39826-39946`): a completed pure
   summary with no operator/member requirements and a two-statement checked
   constructor body tied to an exported inferred-field nominal template.
2. `append_abi_v10_arithmetic_records()` (`39951-40070`): the exact checked
   generic return `T * Int + Int`, with its ordered operator requirements.
3. `append_abi_v10_empty_vector_records()` (`40073-40213`): opt-in only via
   `MOSS_PHASE25_EMPTY_VECTOR_EMIT=1`; zero-argument `return []`, result
   `Vector[BoundVar(0)]`, pure effects and no requirements/dependencies.
4. `append_abi_v10_private_chain_records()` (`40218-40471`): a closed graph of
   pure two-statement local-result call/constructor bodies with supported
   callable/nominal dependencies.

`abi_v10_checked_provenance()` at `src/moss.cpp:39802-39820` creates callable,
requirement-call-site, and nominal declaration origins. Summary/body writers
store those provenance IDs; whole-interface validation checks their
referential integrity. The importer explicitly loads and validates requirement
provenance into `Function::imported_checked_provenance_records` at
`src/moss.cpp:38179-38190`. It does not hydrate the function's provider source
file and declaration line from the summary's callable `provenance_id`; those
diagnostic fields currently come from legacy declaration metadata
(`generic_source` / `generic_entity_id`) and the declaration payload. This is a
remaining provenance cutover dependency even though provenance records
themselves are validated.

ABI-v9 has no ABI-v10 typed-summary, checked-body, or callable-provenance
records. ABI-v10 requires these records only for the selected producer shapes
above; it does not yet require them for every imported generic callable.

### ABI-v10 consumer path

`load_module_interface()` at `src/moss.cpp:36796-36814` checks staged mode,
then `verified_abi_v10_interface_records()` at `36746-36794` verifies the seal,
provider `.rlib` marker, canonical encoding, complete interface, and hashes.
That projection skips typed-v2 summary payloads from the declaration text but
retains other declaration text, including legacy generic records. The regular
text parser runs next.

The ABI-v10 record importer at `src/moss.cpp:38045-38197` decodes public and
private summaries and checked-body records, checks identity/hash pairing, and
attaches matched records to imported `Function`s. It fails closed for missing
or mismatched records in its specific arithmetic, empty-vector, and general
body-record cases. `project_imported_h3_checked_bodies()` at
`src/moss.cpp:3357-3621` then decodes checked IR, validates dependency edges,
projects supported bodies into the existing Moss AST, and sets
`imported_checked_body_projected`. The normal checker and `Generator` perform
specialization and native lowering. No separate generic specialization engine
is used.

The decoded summary is also closed against the checker at
`validate_imported_h3_summary_closure()` (`src/moss.cpp:3623-3680`) and
`Checker::build_open_callable_summaries()` (`4380-4465`). Summary result and
parameter TypeRefs are reconstructed from the typed wire summary and closed by
the normal type solver; they are not inferred from `generic_result` strings.

## 2. Checked-body coverage matrix

“Summary” below distinguishes checker-owned in-memory facts from a serialized
typed-v2 ABI summary. “Legacy needed” means that a current ABI-v10 artifact or
consumer still relies on its legacy payload for the stated body shape; it does
not mean the record must be deleted.

| Generic body category | Checker authoritative summary | ABI-v10 body emitted | Consumer decode/lower | Source-free native execution evidence | Legacy reconstruction today |
|---|---|---|---|---|---|
| Identity/parameter return (`return x`) | In-memory open summary when closure is complete; typed API can expose a retained parameter expression. | No matching general producer. | H3 reader can decode a parameter, but projector has no general parameter-return lowering case. | No checked-body execution test found. | **Required** for current v9 and v10 imports.
| Scalar literals and ordinary simple expressions (Int/Float/Bool/String, unary, comparisons, general `+`) | In-memory summaries are available for closed open-callable facts; retained-body support is narrower (parameter, Int, `+`, nominal field, vector, simple call). | No general typed-v2/body producer. Arithmetic producer is only exact `T * Int + Int`. | H3 reader handles Int literal and limited Add/integer Binary; Float/Bool/String/Unary/comparison nodes are not decoded. Projector only lowers its listed body cases. | Exact arithmetic is tested; other generic literal/expression forms are not proven through checked bodies. | **Required** for current ABI-v10 source-free generic execution outside the arithmetic special case.
| Exact open arithmetic `T * Int + Int` | Yes; complete summary with ordered `operator:*` then `operator:+` requirements. | Yes, v2 summary + body + provenance. | Yes; `decode_h3_checked_body_v1` and the arithmetic case in `project_imported_h3_checked_bodies()` synthesize an AST expression. | Yes: source-removed Int and Float consumers return `30` and `25.0`; malformed or unsupported variants reject. | No for the emitted `compute` callable in ABI-v10; generic statements are removed after matching records. ABI-v9 retains/uses them.
| Direct inferred-field record constructor (`result = Box(field: param); return result`) | Yes, when complete/pure. | Yes under the direct checked-record producer; nominal template and dependency hash accompany body. | Yes for `NominalConstruct` with parameter operands; ordinary constructor/type lowering is reused. | Yes: source-removed Int and String consumers pass. | No for this checked callable; acceptance asserts its interface has no `generic_stmt`. ABI-v9 remains legacy based.
| Local-result constructor/call and private helper chain | Yes for each complete pure callable in the supported dependency closure. | Yes only for the two-statement assignment + return form and closed dependencies. | Yes for `Let`/`Return`, `NominalConstruct`, and `Call` with parameter operands; closure and hash checks apply. | Yes: H3 chain test removes `generic_stmt` for exported `fresh` and both private helpers; two source-free consumers execute. It leaves a legacy statement for the separate `relay` callable. | Not for the checked `fresh`/helper closure; **still required** by `relay` in that test. Other call forms fall back or reject.
| Generic field read / projection (`return b.value`) | In-memory complete summary can exist; retained typed API can encode a field node. | No general producer for this body. | H3 body reader does not decode `ExprKind::Field`; projector has no field-read case. | Existing H3 chain test exercises `relay` source-free, but only with its retained legacy statement; no checked-body field execution. | **Required** for ABI-v10 `relay` today.
| Empty `VectorLiteral([])` with open element type | Yes: `Vector[BoundVar(0)]`, zero params/requirements, pure. | Yes only with both `MOSS_STAGED_ABI=v10` and `MOSS_PHASE25_EMPTY_VECTOR_EMIT=1`. | Yes: narrow `decode_empty_vector_return_checked_body_v1()` (`src/checked_body_ir.hpp:764-780`) and existing empty-vector expression/native lowering. | Yes: source-removed `Vector[i32]` and `Vector[bool]` execute; test trace proves checked-body projection. ABI-v9 control passes. | Record retained but not used for this checked callable. With the experimental switch off, no v2 summary/body is emitted and legacy is the ABI-v10 path.
| Nonempty vector literal; Map/Queue literal | Checker can infer types when source use closes them; retained typed-body builder recognizes vector literal children only, not Map/Queue literals. | No supported producer. | Empty-vector decoder rejects nonempty literals; H3 decoder rejects vector/map literal nodes. | Negative nonempty checked-body mutation rejects. No positive checked-body execution evidence for nonempty vectors or Map/Queue literals. | **Required or the import is unsupported** in current ABI-v10; fallback behavior is not a checked-body guarantee.
| Collection operations (push/pop, indexing, map/queue get/put, vector indexing/assignment) | Checker can produce normal concrete types/effects; open generic summary coverage is limited to what existing inference can close. | No operation-specific generic body producer. | Checked-body encoder has statement/node tags, but decoder does not read collection mutation/index operations; projector has no such lowering case. | SDK collection tests exercise concrete, ordinarily compiled library code; they do not prove generic source-free checked-body execution. | **Required or unsupported** for open generic bodies that perform these operations.
| Multiple locals, mutation, `if`/`while`/`for`/`match`, message/effectful operations | Checker has ordinary semantic facts where source program is valid; open summaries can be incomplete where facts/effects/requirements cannot be closed. | Private-chain writer only handles its narrow two-statement local-result form and rejects some unrecorded multi-statement private chains at provider build. | Encoder can represent many statements, but H3 decoder only reads one return or two statements (`let`, `return`). It rejects control flow, mutation, reply/message, and arbitrary effects. | No source-free checked-body execution evidence for these forms. | They either retain legacy ABI-v10 reconstruction where allowed or fail closed during provider/import processing; checked-body support is absent.

### Important scope note about the SDK

Most SDK exported functions have concrete parameter/result types and execute
from the provider's compiled `.rlib`; that is not generic body reconstruction
and does not establish checked-body generic coverage. The relevant extant
source-free generic evidence is concentrated in the H3 direct constructor,
private-chain, checked arithmetic, and R2c empty-vector suites. This audit did
not run the SDK compatibility matrix or the full repository suite.

## 3. Remaining cutover blockers

| Priority | Blocker | Evidence / impact | Smallest useful closure |
|---|---|---|---|
| **P0** | ABI-v10 can still reconstruct executable generic bodies from `generic_stmt`. | Generic fields are parsed before checked-body projection (`36444-36458`, `37889-37897`); projection is conditional on attached records. Current emitters cover only four narrow shapes. | Keep compatibility records serialized, but make ABI-v10 import select a validated typed summary/body for every generic callable admitted to source-free specialization. Reject unsupported/missing pairs deterministically; add a poisoned-legacy test for each admitted class and a negative missing-record test. Do this only after P1 coverage for required classes is in place.
| **P0** | Callable source location still comes from legacy fields even when summary provenance is validated. | `summary.provenance_id` has interface referential validation, but importer uses `generic_source` / declaration line to build `OpenCallableSummaryFacts::origin` (`4437-4440`). Removing or corrupting compatibility metadata can lose/misattribute source locations. | Resolve the summary's callable provenance record during import and use its module/source display/line/columns for diagnostics; retain legacy metadata for v9 and compatibility. Test absent/tampered legacy source fields with valid provenance, and mismatched provenance identity.
| **P1** | Typed-v2 summary/body emission is not generalized to the normal open generic surface. | Emitters reject member requirements/effects and most expression/body forms. Empty-vector emission remains opt-in. | Expand producers in bounded semantic slices, preserving checker TypeRefs, access, requirements, effects, dependencies, body hash, and provenance; do not infer wire types from strings.
| **P1** | Checked-body decoder and importer do not cover the body constructors already representable by the codec or common generic usage. | `ExprKind`/`StmtKind` and encoder cover more forms than `H3BodyReader`; decoder only reads return or let/return and selected expressions. Field reads, literals beyond Int, generic collection ops, and control flow are unsupported. | Add selected expression/statement decoders and lower each into existing AST/IR paths with executable source-free tests; stage broader control-flow only if needed for Phase 25 generic/SDK requirements.
| **P1** | Typed requirement/member-method and effect summaries are not all emitted/imported as generic body contracts. | `abi_v10_checked_requirements()` only serializes `operator:*` and `operator:+` (`39727-39791`); current body producers require pure effects and often no member requirements. | Establish which Phase 25 generic tests/SDK exports need method/index/collection requirements and effects; serialize/validate those with provenance and dependency closure before allowing their bodies through v10.
| **Deferred** | General-purpose checked serialization/lowering of every Moss AST construct. | Encoder enums include messages, Map literals, assignments, branches, loops and matches, but neither checker retention nor H3 source-free import covers all of them. No evidence makes all forms necessary for the semantic ABI cutover. | Keep out of the initial cutover. Add categories only when the accepted generic source-free workload requires them; do not turn H3 into a second complete compiler IR.
| **Deferred** | ABI-v9 migration/default change and deletion of compatibility records. | ABI-v9 remains the default and regression path; task scope prohibits default change or record removal. | Separate L7 authorization after ABI-v10 acceptance and migration criteria are frozen.

P0 here means required to make ABI-v10's generic-body authority safe and
explicit, not a request to remove the compatibility records. P1 describes
functional work needed before ABI-v10 can serve the normal Phase 25 generic
surface. The current staging guard is useful: it blocks some multi-statement
private chains without a checked closure (`src/moss.cpp:3982-4016`), and the
private-chain writer rejects an unrecorded private multi-statement path
(`40296-40329`). Those guards do not prevent every uncovered single-return or
single-call form from arriving through legacy fields.

## 4. Recommended bounded execution sequence

### Task A — Freeze an ABI-v10 generic admission contract (P0 prerequisite)

- **Entry points:** `load_module_interface()` and v10 record selection at
  `src/moss.cpp:36796-36814`, `38045-38197`; whole-interface validation in
  `src/abi_v10_records.hpp:1245-1595`.
- **Change:** Define the exact callable classes that v10 admits for source-free
  specialization. Keep legacy fields in artifacts, but ensure they cannot
  decide a callable's executable body in ABI-v10 mode. Missing, duplicate,
  unsupported, mismatched, or unbound typed summary/body pairs fail closed.
- **Tests:** For each admitted class, remove/poison that callable's
  `generic_stmt` and compile/run a source-free consumer. Remove summary/body,
  alter hashes/identity/dependency/provenance, and require stable diagnostics.
  Verify ABI-v9 still reconstructs the same legacy fixture.
- **Dependency / exit:** Depends on the coverage tasks below for every class
  admitted. Exit when the ABI-v10 loader contains no generic-statement fallback
  for admitted callables and an uncovered generic cannot silently specialize.

### Task B — Scalar and field expression bodies (P1)

- **Entry points:** `Checker::retain_checked_body_ir()` at
  `src/moss.cpp:4234-4377`; `H3BodyReader::read_expr()` at
  `src/checked_body_ir.hpp:621-703`; expression projection at
  `src/moss.cpp:3444-3595`.
- **Change:** Reconcile checker-retained expression tags with the wire decoder
  and existing AST lowering. Prioritize identity/parameter return, supported
  scalar literals, general arithmetic covered by summary requirements, and
  field reads. Keep unsupported tags rejected.
- **Tests:** Per-shape unit round trips plus separately compiled provider,
  removed source, poisoned legacy record, two distinct concrete
  specializations, correct runtime output; malformed node/type/hash negatives.
- **Dependency / exit:** Can proceed against the current ABI schema without
  touching `src/abi_v10_records.hpp`; must complete before Task A admits those
  shapes.

### Task C — Generic call and nominal dependency closure (P1)

- **Entry points:** `checked_private_chain_body()` at
  `src/moss.cpp:2965-3082`; `append_abi_v10_direct_checked_records()` and
  `append_abi_v10_private_chain_records()` at `39826-39946` and `40218-40471`;
  call/constructor projection at `3550-3592`.
- **Change:** Extend only call/constructor shapes required by Phase 25 generic
  acceptance, with exact callable/nominal dependency hashes and summary/body
  parity. Do not accept calls based on source spelling alone.
- **Tests:** Single return call, nested private helper chain, constructor with
  supported argument forms; remove source and poison legacy body for every
  callable; remove a dependency or alter its expected hash and require failure.
- **Dependency / exit:** Can be developed separately from Task B's scalar
  expression decoder only if `src/moss.cpp` has one assigned writer. Exit when
  dependency closure and both concrete consumers pass.

### Task D — Collection and method requirements (P1)

- **Entry points:** `abi_v10_checked_requirements()` at
  `src/moss.cpp:39727-39791`; typed summary codec in
  `src/abi_v10_records.hpp:324-420`; body producer and decoder; existing normal
  vector/map/queue checker and generator paths.
- **Change:** Add only the method/index/access requirement forms shown by the
  accepted Phase 25 generic tests and SDK inventory. Preserve receiver,
  argument/result TypeRefs, access, origin, provenance, and effect closure.
- **Tests:** Generic push/pop or indexing cases actually selected for cutover,
  two concrete element/key types, missing/reordered requirement and altered
  provenance negatives, source-free native execution with legacy poisoned.
- **Dependency / exit:** Depends on Task B's decoder/lowering foundation and
  exact accepted-use inventory. Exit when all selected operations have typed
  summaries, checked bodies and negative closure tests.

### Task E — Provenance hydration and final no-fallback gate (P0)

- **Entry points:** `read_provenance()` / `validate_provenance()` at
  `src/abi_v10_records.hpp:596-623`; importer at `src/moss.cpp:38177-38190`;
  `Checker::build_open_callable_summaries()` at `4431-4440`; diagnostic
  origin consumers.
- **Change:** Hydrate callable origin from `Summary::provenance_id`, verify
  declaration/callable identity, and use record source spans in ABI-v10
  diagnostics. Then apply Task A's no-legacy-body admission rule to the full
  selected Phase 25 generic body set.
- **Tests:** Valid provenance with compatibility source metadata removed or
  forged; wrong callable/module/line identity; existing provider-origin versus
  local-conflict assertions; all admitted checked-body tests with generic
  statements poisoned.
- **Dependency / exit:** Provenance hydration can proceed independently in
  the ABI codec/import ownership area; final gate depends on Tasks B-D for the
  selected functionality. Exit when v10 source-free generic execution and
  diagnostics derive from typed records, and v9 controls remain unchanged.

### Parallel ownership recommendation

No implementation teams were started for this audit. For a later authorized
phase, the safest independent lanes are:

1. ABI record/provenance contract tests and importer provenance mapping
   (`src/abi_v10_records.hpp`, isolated tests, plus a single assigned owner for
   its importer hunk).
2. Checked-body expression codec/decoder cases and their unit tests
   (`src/checked_body_ir.hpp`, isolated codec tests).
3. Source-free provider/consumer corpus inventory and poison/fault-injection
   tests (tests-only files, with no edits to compiler sources).

`src/moss.cpp` contains both all four body producers and the consumer
projection/admission logic. It must have one writer at a time; producer,
projector, summary, and provenance edits should be serialized or integrated
behind agreed interfaces, not assigned as concurrent changes to that file.

## 5. Focused evidence gathered

On the audit baseline, the following focused tests passed:

- `python3 tests/tooling/check_phase25_checked_body_ir.py ./moss` — codec,
  hash, dependency closure, canonical/fail-closed unit coverage passed.
- `python3 tests/tooling/check_phase25_h3_direct_records.py ./moss` — direct
  inferred-field constructor imported and executed after source removal and
  with no legacy generic statement.
- `python3 tests/tooling/check_phase25_h3_source_free_chain.py ./moss` — nested
  private helper closure executed in two source-free consumers after poisoning
  the three legacy statements; missing private body rejected.
- `python3 tests/tooling/check_phase25_c2b6c_source_free_arithmetic.py ./moss`
  — Int/Float checked arithmetic, bad substitutions, missing/mismatched body,
  unsupported body and ABI-v9 control passed.
- `python3 tests/tooling/check_phase25_r2c_empty_vector_consumer.py ./moss` —
  source-free i32/bool empty vectors, checked-body trace, legacy exclusion,
  malformed records/shapes, ABI-mode rejection, provenance acceptance and v9
  control passed.

The first checked-body unit-runner invocation failed because this baseline's
existing `TypeSolver::Undo` missing-field-initializer warnings are promoted to
errors by the test's direct `g++ -Werror` command. Rerunning through a temporary
repo-local `tmp/` wrapper that adds only
`-Wno-missing-field-initializers` passed. No repository source or test output
was altered to get the pass. No full repository regression, SDK matrix, or
Phase 25 release ledger was run.

## Exit recommendation

Do not change ABI defaults or remove legacy compatibility records as part of
the next assignment. First authorize Tasks B-D for the minimum generic forms
required by Phase 25 acceptance, in single-writer `src/moss.cpp` ownership.
Then complete provenance hydration and enforce ABI-v10 no-fallback admission
(Tasks E/A). This gives L7 a bounded cutover: generic artifact meaning comes
from typed summaries and checked bodies; ABI-v9 continues to use its legacy
interface path until separately authorized migration.
