# R7-011 Step 1 Team B consumer interface and removal inventory

Status: proposal pinned to Team B branch `phase28-r7-011-team-b-dynamic-fastdebug` at `0c571343`.
This document proposes no checker decision and assigns no specialization identity.

## Required producer contract

The checker owner must provide the following shared API before consumer wiring can
be completed. IDs are opaque table indexes assigned by the producer; consumers must
not derive them from generated names, parameter types, specialization strings,
callable names, line numbers, or context strings.

- `using SpecId = uint32_t;` plus an explicit invalid/sentinel representation or
  `std::optional<SpecId>`; valid IDs index a program-wide immutable specialization
  record table.
- Each specialization record identifies its semantic callable and owns its
  concrete checked specialization. `FunctionSpecialization` carries its `SpecId`.
  The readable specialization key remains diagnostics-only.
- Every EST `Call` fact distinguishes `Static(SpecId)` from `DynamicCallback`.
  Its exact key is `(caller SpecId, arena index, ExprId)`. Missing, duplicate,
  invalid, or contradictory rows are errors.
- The native-emission occurrence cursor must expose the EST arena/ExprId for the
  call being emitted. Existing `SemanticCallEdge` already carries an arena index
  and expression occurrence, but still also relies on source/context strings and
  has no caller/target SpecId.
- Every interpreter execution frame carries its active `SpecId` (root/test bodies
  need producer-assigned identities too). A call pushes the target SpecId obtained
  from the one call-fact lookup.
- `.mossi` serializes the specialization table and each occurrence call fact.
  References across a provider boundary are `(module identity, SpecId)`; loading
  validates bounds, uniqueness, table ownership, and referenced module identity.

Suggested consumer lookup API (names are provisional):

```cpp
CheckedCallSpec lookup_call_spec(
    const Program&, SpecId caller, uint32_t arena, est::ExprId occurrence);
```

The result is either one `Static(SpecId)` or `DynamicCallback`. No target-name
argument is accepted; consumer call resolution cannot influence selection.

## Consumer removals required by L8

- Delete `checked_call_edge_specialization` in `src/ast.hpp` (line 1100 in the
  pinned branch): name/line matching, `<...>` stripping, and `handler:` rewriting.
- Delete the Fast Debug `Missing && Identifier` callback-selection branch in
  `src/interpreter.hpp` around `specialization_for_call` lines 1867–1907. Dynamic
  callbacks must be represented by the checker fact, not inferred from the EST
  node shape by a consumer.
- Replace `Frame.functional_context` and `Frame.specialization_key` where they
  participate in selecting a callee or looking up specialization-scoped EST facts.
  They may remain diagnostic labels only.
- In `src/native_generator.cpp`, remove the `concrete_specialization(...)` fallback
  for resolved static calls at the pinned call lowering around lines 4784 and 6026.
  A missing call fact is an unconditional invariant failure in production.
- Remove selection/fact lookup based on `specialization_context` and
  `functional_context` strings from the native emitter and interpreter as each
  specialization-scoped fact class is migrated to SpecId. No rename/helper wrapper
  around string comparisons satisfies this requirement.

## Required mutation and regression coverage

Register one semantic-authority test job that runs both Fast Debug and native for
`nest_spec`, `dom_spec`, `meth_spec`, `try_spec`, `cb_spec`, `dead_tail2`, `o15b`,
`field_read`, and `field_consume`; outputs must agree, and the last case must reject
at line 9. In both engines, inject dropped, duplicated, and perturbed call facts
without an environment variable in production builds; each must fail closed.

Also cover: absent caller SpecId, invalid table index, duplicate SpecId record,
wrong module on imported `(module, SpecId)`, missing EST occurrence, duplicated
occurrence, and a target SpecId owned by a different callable. Static target
selection must never invoke `concrete_specialization` after a checker-resolved call.

## Current code locations observed

- `src/ast.hpp`: `FunctionSpecialization` has no SpecId; `SemanticCallEdge` has
  EST occurrence fields but its identity is still source/context strings and
  specialization keys.
- `src/est/expression_tree.hpp`: `SemanticFacts.specialization_result` is a
  string; arena fact keys are `(ExprId, specialization_context string)`.
- `src/interpreter.hpp`: `Frame` has string `functional_context` and
  `specialization_key`; `specialization_for_call` still receives them.
- `src/native_generator.cpp`: static-call resolution still queries the legacy
  helper and falls back to argument-type `concrete_specialization`.
- `src/moss.cpp`: module artifact serialization currently transports readable
  specialization keys; no SpecId table or occurrence-to-SpecId call facts exist.

This work is intentionally blocked from pretending to complete the interface until
Team A publishes producer-owned IDs and Team B can consume that exact table. No
string-derived IDs or consumer-side semantic inference are acceptable substitutes.

## Pinned source counts and additional identity-string sites

At `0c571343`, `rg -c` reports `specialization_context`: native emitter 62,
Fast Debug 50. `functional_context`: native emitter 115, Fast Debug 52. These counts
include both frame/body identity plumbing and lookup/selection uses; the whole
consumer-file count must not be reported as if every occurrence is a specialization
selection. During migration, every remaining use must be reviewed and classified;
L8's required endpoint for specialization-scoped fact selection/lookup is zero.

Additional `<...>` string-splitting sites found by `rg` (besides the legacy matcher
at `ast.hpp:1110`) are `interpreter.hpp:4382,4549`, and
`native_generator.cpp:1317,1361,2565,9842,11057`. These need individual review
against the L8 restriction: sites used to choose specialization, query facts, or
classify an active specialization must be replaced by SpecId/table data. Any display
or diagnostics-only use must be demonstrated as such and must not feed selection.

The static emitter selection paths are at `native_generator.cpp:4734,4769,4784`
and `:6026`; the latter two are the explicit concrete-specialization fallbacks.
Fast Debug call selection is at `interpreter.hpp:1854` and `:1932`; its
`Missing && Identifier` fallback begins at `:1867`.

### `.mossi` plumbing boundary

The pinned branch uses a sealed ABI-v10 module interface (`moss-module-interface-v3`)
with semantic Section 1 and versioned executable-body Section 2. The execution-body
loader binds Section 2 to the sealed Section 1 semantic digest. The current API has no
SpecId records or occurrence call facts in either section. Therefore implementation
cannot safely be isolated to just the reader or writer: producer record shape,
assignment order, dense-table validation, imported-module qualification, and the
Section 1 digest contract must be decided together. Once Team A supplies the table and
fact records, Team B can add symmetric writer/reader records and corruption tests
without deriving identity from names or strings. Do not advance the ABI version or
weaken sealed-interface admission as a workaround.

## Exhaustive consumer context and specialization-selection inventory

This appendix is the literal `rg -n` inventory at `00b54a51`, generated with:

```sh
rg -n 'specialization_context|functional_context|codegen_body_context_|codegen_active_specialization_key_|concrete_specialization\\(|functional_specialization\\(' src/interpreter.hpp src/native_generator.cpp
```

It enumerates every use, including facts/filter mutations and diagnostics, so the
migration can classify each occurrence rather than relying on a count. Any context
comparison/edit that selects a specialization or looks up specialization-scoped facts
must become an ID lookup; labels may remain only for diagnostics. The complete raw
343-line output is kept in the same checkout at
`tmp/team-b-consumer-full-inventory.txt`.

Call-site specialization selected from argument types, outside Rust syntax/type
rendering:

- `native_generator.cpp:4784`: `concrete_specialization(function, concrete_types)`; ordinary static-call fallback to delete.
- `native_generator.cpp:5171`: `functional_specialization(*fn, input_types)`; functional callable dispatch must resolve via exact checked call/callback fact and SpecId.
- `native_generator.cpp:6021`: `functional_specialization(*function_target, argument_types)`; callback path must read exact checked target SpecId.
- `native_generator.cpp:6026`: `concrete_specialization(*function_target, argument_types)`; static-call fallback to delete.
- `interpreter.hpp:1848-1853`: reads string `SemanticFacts.specialization_result` as a target selection input.
- `interpreter.hpp:1893-1902`: maps that string to a candidate specialization in the identifier fallback; delete with the entire `Missing && Identifier` fallback.
- `interpreter.hpp:1920-1926`: cross-checks the exact call-edge specialization key with the EST string fact; switch to one checker-issued SpecId authority.
- `ast.hpp:1037-1095` (called by Fast Debug): reconstructs selection by context strings, target callable names, argument-type spellings and source line/occurrence. Replace with exact checked-call-fact lookup.

The pointer-based `concrete_specialization` occurrence used for rendering a non-static
call at `native_generator.cpp:6026` must also be reviewed: once checker-resolved, all
callee specialization selection comes from the call fact. It may not remain as a
fallback for an unresolved static target.

## Frozen-design generic-call matrix

For every cell, Fast Debug and Native must select the same checker-issued
specialization and produce the stated output. `depth` is the count of helper-call
edges between the listed caller body and the generic identity function: 0 is a direct
call, 1 inserts one generic relay, 2 inserts two. For pipeline callbacks, that many
relays are inside the callback before returning the element. These are design
expectations, not claims about the current compiler.

| Caller body | Int depth 0 | Int depth 1 | Int depth 2 | Float depth 0 | Float depth 1 | Float depth 2 |
|---|---:|---:|---:|---:|---:|---:|
| `main` | `10` | `10` | `10` | `3.5` | `3.5` | `3.5` |
| ordinary function | `10` | `10` | `10` | `3.5` | `3.5` | `3.5` |
| object method | `10` | `10` | `10` | `3.5` | `3.5` | `3.5` |
| domain handler | `10` | `10` | `10` | `3.5` | `3.5` | `3.5` |
| `try` body | `10` | `10` | `10` | `3.5` | `3.5` | `3.5` |
| pipeline callback over one-element vector | `[10]` | `[10]` | `[10]` | `[3.5]` | `[3.5]` | `[3.5]` |

The ordinary-function, method, handler, and `try` rows use the same values as
`normalize(x) -> x`; the handler row replies with the result for native/Fast Debug
comparison. The callback row maps `normalize` (or a relay around it) over `[10]` or
`[3.5]`. Every row also needs a negative mutation: drop, duplicate, or perturb the
exact call fact and require both engines to fail closed without production env gates.

## Nested Map-key occurrence finding from Team C

At Team C snapshot `44ffcae8` + its preserved working diff, `copy(key)` in a Map LHS
is checker-authorized but native `resolve_copy_fact(string_view)` sees only top-level
handles. `gen_block` also pre-renders the Map LHS index before the Map insertion path,
then the final insertion implicitly clones a non-Copy key. The consumer fix belongs
in the same EST child-occurrence plumbing as call selection: bind the exact nested
`copy(key)` ExprId, skip the unused pre-render, and emit the existing checked copy
operation only with its exact TypedCopyFact. Team C separately found the Counter
sidecar omits `MossAccess_Counter::increment_by`; project that exact emitted variant
from compiler facts if still required, without loosening provenance matching.

### Raw context-use listing

```text
src/interpreter.hpp:150:        if (fact.specialization_context == "main") return &fact;
src/interpreter.hpp:156:        if (fact.specialization_context == "main") return &fact;
src/interpreter.hpp:162:        if (fact.specialization_context == "main") return &fact;
src/interpreter.hpp:201:        else if (field == "context") fact->specialization_context += ":wrong";
src/interpreter.hpp:206:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:215:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:241:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/interpreter.hpp:284:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:293:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:319:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/interpreter.hpp:361:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:370:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:396:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/interpreter.hpp:456:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:465:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/interpreter.hpp:653:    frame.functional_context = "main";
src/interpreter.hpp:695:      frame.functional_context = frame.function;
src/interpreter.hpp:855:    std::string functional_context;
src/interpreter.hpp:1072:    auto specialized = statement.semantic_types_by_context.find(frame.functional_context);
src/interpreter.hpp:1846:                                             caller.functional_context);
src/interpreter.hpp:1855:        program_, caller.functional_context, caller.specialization_key,
src/interpreter.hpp:1872:                                             caller.functional_context);
src/interpreter.hpp:1915:                                             caller.functional_context);
src/interpreter.hpp:1933:        program_, caller.functional_context, caller.specialization_key,
src/interpreter.hpp:2009:            *stage_expr, frame.functional_context);
src/interpreter.hpp:2012:            *stage_expr, frame.functional_context);
src/interpreter.hpp:2014:      stage_facts = tree->arena->facts(*stage_expr, frame.functional_context);
src/interpreter.hpp:2050:                                           frame.functional_context);
src/interpreter.hpp:2093:      if (pipeline.line != line || pipeline.context != frame.functional_context ||
src/interpreter.hpp:2222:                        << "|context=" << frame.functional_context << "\n";
src/interpreter.hpp:2393:                tree->arena_index, current_root, frame.functional_context,
src/interpreter.hpp:2397:                tree->arena_index, current_root, frame.functional_context);
src/interpreter.hpp:2623:              current_root, frame.functional_context);
src/interpreter.hpp:2785:            ? tree->arena->facts(current_root, frame.functional_context)
src/interpreter.hpp:3459:                        method->base, frame.functional_context.empty()
src/interpreter.hpp:3460:                            ? "main" : frame.functional_context);
src/interpreter.hpp:3935:                  << "|context=" << caller.functional_context
src/interpreter.hpp:3939:    frame.functional_context = functional_function_context(target, specialization);
src/interpreter.hpp:3947:      frame.specialization_identity = frame.functional_context + "@" +
src/interpreter.hpp:3986:    frame.functional_context = functional_method_context(target);
src/interpreter.hpp:4106:    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;
src/interpreter.hpp:4149:                                          found_fact->specialization_context);
src/interpreter.hpp:4174:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:4175:        ? "main" : frame.functional_context;
src/interpreter.hpp:4188:          candidate.specialization_context == context &&
src/interpreter.hpp:4230:    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;
src/interpreter.hpp:4236:          req.specialization_context == context &&
src/interpreter.hpp:4254:          fact.specialization_context == context &&
src/interpreter.hpp:4262:              req.specialization_context == fact.specialization_context &&
src/interpreter.hpp:4282:    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;
src/interpreter.hpp:4286:      if (req.specialization_context == context &&
src/interpreter.hpp:4299:      if (fact.specialization_context == context &&
src/interpreter.hpp:4306:              req.specialization_context == fact.specialization_context &&
src/interpreter.hpp:4331:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:4332:        ? "main" : frame.functional_context;
src/interpreter.hpp:4345:        if (requirement.specialization_context != context ||
src/interpreter.hpp:4375:    const std::string context = frame.functional_context.empty() ? "main" : frame.functional_context;
src/interpreter.hpp:4393:          req.specialization_context == context &&
src/interpreter.hpp:4427:                candidate.specialization_context == requirement->specialization_context;
src/interpreter.hpp:4456:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:4457:        ? "main" : frame.functional_context;
src/interpreter.hpp:4475:          candidate.specialization_context == context) {
src/interpreter.hpp:4536:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:4537:        ? "main" : frame.functional_context;
src/interpreter.hpp:4718:          candidate.specialization_context == query_context &&
src/interpreter.hpp:4900:    std::string context = frame.functional_context.empty()
src/interpreter.hpp:4901:        ? "main" : frame.functional_context;
src/interpreter.hpp:4951:                item.specialization_context == context;
src/interpreter.hpp:4967:                item.specialization_context == context;
src/interpreter.hpp:5030:            "/c=" + candidate.specialization_context;
src/interpreter.hpp:5047:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:5048:        ? "main" : frame.functional_context;
src/interpreter.hpp:5057:          candidate.specialization_context != context ||
src/interpreter.hpp:5110:          candidate.specialization_context == requirement->specialization_context &&
src/interpreter.hpp:5178:    const auto context = frame.functional_context.empty()
src/interpreter.hpp:5179:        ? std::string("main") : frame.functional_context;
src/interpreter.hpp:5190:          candidate.specialization_context == context &&
src/interpreter.hpp:5202:          candidate.specialization_context == context &&
src/interpreter.hpp:5214:                row.specialization_context == context &&
src/interpreter.hpp:5220:            row.specialization_context == context &&
src/interpreter.hpp:5246:            candidate.specialization_context == context &&
src/interpreter.hpp:5257:            candidate.specialization_context == context &&
src/interpreter.hpp:5283:          fact.specialization_context != context ||
src/interpreter.hpp:5304:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:5305:        ? "main" : frame.functional_context;
src/interpreter.hpp:5368:          candidate.specialization_context == context &&
src/interpreter.hpp:5388:            candidate.specialization_context == requirement->specialization_context &&
src/interpreter.hpp:5429:            candidate.specialization_context == requirement->specialization_context) {
src/interpreter.hpp:5467:      const std::string active_context = frame.functional_context.empty()
src/interpreter.hpp:5468:          ? "main" : frame.functional_context;
src/interpreter.hpp:5475:            candidate.specialization_context == active_context &&
src/interpreter.hpp:5493:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:5494:        ? "main" : frame.functional_context;
src/interpreter.hpp:5514:          candidate.specialization_context == context &&
src/interpreter.hpp:5523:          candidate.specialization_context != context ||
src/interpreter.hpp:5538:          candidate.specialization_context == context &&
src/interpreter.hpp:5547:          candidate.specialization_context == context &&
src/interpreter.hpp:5623:    const std::string context = frame.functional_context.empty()
src/interpreter.hpp:5624:        ? "main" : frame.functional_context;
src/interpreter.hpp:5671:          candidate.specialization_context != context ||
src/interpreter.hpp:5687:          candidate.specialization_context == context &&
src/interpreter.hpp:5737:    auto exact = statement.joined_types_by_context.find(frame.functional_context);
src/native_generator.cpp:247:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:256:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:266:             fact.specialization_context.find(target_spec) != std::string::npos))
src/native_generator.cpp:275:             requirement.specialization_context.find(target_spec) != std::string::npos))
src/native_generator.cpp:329:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/native_generator.cpp:372:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:381:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:407:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/native_generator.cpp:449:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:458:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:484:            if (it->specialization_context.find(target_spec) != std::string::npos) {
src/native_generator.cpp:556:          if (!target_spec || f.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:565:          if (!target_spec || r.specialization_context.find(target_spec) != std::string::npos)
src/native_generator.cpp:1104:  string codegen_body_context_;
src/native_generator.cpp:1105:  string codegen_active_specialization_key_;
src/native_generator.cpp:1241:          candidate.specialization_context == requirement.specialization_context) {
src/native_generator.cpp:1254:          candidate.specialization_context == requirement.specialization_context) {
src/native_generator.cpp:1294:      const Stmt& statement, const string& functional_context) const {
src/native_generator.cpp:1316:      string callable = functional_context;
src/native_generator.cpp:1353:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:1359:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:1543:          candidate.specialization_context == query_context &&
src/native_generator.cpp:1568:          candidate.specialization_context == query_context &&
src/native_generator.cpp:1718:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:1719:        ? "main" : codegen_body_context_;
src/native_generator.cpp:1720:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:1749:          candidate.specialization_context != context ||
src/native_generator.cpp:2101:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:2102:        ? "main" : codegen_body_context_;
src/native_generator.cpp:2181:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:2182:        ? "main" : codegen_body_context_;
src/native_generator.cpp:2196:          candidate.specialization_context == context) {
src/native_generator.cpp:2210:          candidate.specialization_context == context)
src/native_generator.cpp:2236:        else if (field == "context") corrupted.specialization_context += ":wrong-context";
src/native_generator.cpp:2289:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:2290:        ? "main" : codegen_body_context_;
src/native_generator.cpp:2306:          candidate.specialization_context == context) {
src/native_generator.cpp:2322:          candidate.specialization_context == context)
src/native_generator.cpp:2351:        else if (field == "context") corrupted.specialization_context += ":wrong";
src/native_generator.cpp:2365:          fact.specialization_context != context ||
src/native_generator.cpp:2408:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:2409:        ? "main" : codegen_body_context_;
src/native_generator.cpp:2424:          candidate.specialization_context == context &&
src/native_generator.cpp:2441:          candidate.specialization_context == context &&
src/native_generator.cpp:2517:      const string& functional_context) const {
src/native_generator.cpp:2522:    string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:2564:      string callable = functional_context;
src/native_generator.cpp:2604:                item.specialization_context == context;
src/native_generator.cpp:2620:                item.specialization_context == context;
src/native_generator.cpp:2680:      const string& functional_context) const {
src/native_generator.cpp:2738:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:2746:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:2756:          candidate.specialization_context != context ||
src/native_generator.cpp:2787:                row.specialization_context == exact_requirement.specialization_context &&
src/native_generator.cpp:2803:                row.specialization_context == exact_requirement.specialization_context &&
src/native_generator.cpp:2817:                row.specialization_context == exact_requirement.specialization_context &&
src/native_generator.cpp:2833:                row.specialization_context == exact_requirement.specialization_context &&
src/native_generator.cpp:2871:      const string& functional_context) const {
src/native_generator.cpp:2917:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:2936:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:2946:          candidate.specialization_context != context ||
src/native_generator.cpp:2972:          row.specialization_context == exact_requirement.specialization_context &&
src/native_generator.cpp:3066:    const auto specialization = codegen_active_specialization_key_;
src/native_generator.cpp:3072:          row.specialization_context != exact_context ||
src/native_generator.cpp:3093:          row.specialization_context == selected.specialization_context &&
src/native_generator.cpp:3124:          fact.specialization_context != exact_context ||
src/native_generator.cpp:3144:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:3145:        ? "main" : codegen_body_context_;
src/native_generator.cpp:3146:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:3155:          candidate.specialization_context != context ||
src/native_generator.cpp:3223:            candidate.specialization_context == requirement->specialization_context &&
src/native_generator.cpp:3269:            candidate.specialization_context == requirement->specialization_context &&
src/native_generator.cpp:3293:      const Stmt& statement, const string& functional_context) const {
src/native_generator.cpp:3315:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:3324:          requirement.specialization_context == context &&
src/native_generator.cpp:3326:              codegen_active_specialization_key_)
src/native_generator.cpp:3336:          fact.specialization_context == context &&
src/native_generator.cpp:3337:          fact.specialization_identity == codegen_active_specialization_key_)
src/native_generator.cpp:3446:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:3447:        ? "main" : codegen_body_context_;
src/native_generator.cpp:3455:          candidate.specialization_context == context &&
src/native_generator.cpp:3456:          candidate.specialization_identity == codegen_active_specialization_key_;
src/native_generator.cpp:3494:          candidate.specialization_context == context &&
src/native_generator.cpp:3495:          candidate.specialization_identity == codegen_active_specialization_key_) {
src/native_generator.cpp:3507:          candidate.specialization_context == context &&
src/native_generator.cpp:3508:          candidate.specialization_identity == codegen_active_specialization_key_)
src/native_generator.cpp:3664:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:3665:        ? "main" : codegen_body_context_;
src/native_generator.cpp:3666:    std::string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:3683:          candidate.specialization_context == context &&
src/native_generator.cpp:3695:          fact.specialization_context == context &&
src/native_generator.cpp:4682:          codegen_active_specialization_key_);
src/native_generator.cpp:4685:            codegen_body_context_);
src/native_generator.cpp:4707:          codegen_active_specialization_key_);
src/native_generator.cpp:4710:            codegen_body_context_);
src/native_generator.cpp:4735:          p_, codegen_body_context_, function.name,
src/native_generator.cpp:4752:                    << "|context=" << codegen_body_context_
src/native_generator.cpp:4763:            " context=" + codegen_body_context_);
src/native_generator.cpp:4770:          p_, codegen_body_context_, function.name,
src/native_generator.cpp:4779:                    << codegen_body_context_ << "|callee=" << function.name
src/native_generator.cpp:4784:    const auto* selected = concrete_specialization(function, concrete_types);
src/native_generator.cpp:4787:          p_, codegen_body_context_, function.name,
src/native_generator.cpp:4797:            " context=" + codegen_body_context_);
src/native_generator.cpp:5171:              functional_specialization(*fn, input_types))
src/native_generator.cpp:5313:        if (!codegen_active_specialization_key_.empty())
src/native_generator.cpp:5314:          checker_context = "specialization:" + codegen_active_specialization_key_;
src/native_generator.cpp:5316:          checker_context = codegen_body_context_;
src/native_generator.cpp:6021:      concrete = functional_specialization(*function_target, argument_types);
src/native_generator.cpp:6026:    } else concrete = concrete_specialization(*function_target, argument_types);
src/native_generator.cpp:7147:                    codegen_body_context_)
src/native_generator.cpp:7165:                  codegen_body_context_)
src/native_generator.cpp:7174:              *phase21_codegen_statement_, e, codegen_body_context_);
src/native_generator.cpp:8525:    } active_specialization_scope(codegen_active_specialization_key_,
src/native_generator.cpp:8700:    string functional_context = functional_function_context(f, specialization);
src/native_generator.cpp:8703:    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false, true, functional_context);
src/native_generator.cpp:8728:      auto plan = f.result_functional_pipeline_ids.find(functional_context);
src/native_generator.cpp:9221:      const Stmt& s, const string& functional_context) const {
src/native_generator.cpp:9238:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9286:                                          found_fact->specialization_context);
src/native_generator.cpp:9297:      const Stmt& statement, const string& functional_context) const {
src/native_generator.cpp:9303:    string context = functional_context.empty()
src/native_generator.cpp:9304:        ? "main" : functional_context;
src/native_generator.cpp:9313:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:9319:          candidate.specialization_context != context ||
src/native_generator.cpp:9343:      const Stmt& statement, const string& functional_context) const {
src/native_generator.cpp:9371:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9372:    const string specialization = codegen_active_specialization_key_;
src/native_generator.cpp:9379:          candidate.specialization_context == context &&
src/native_generator.cpp:9444:        fact->specialization_context != context ||
src/native_generator.cpp:9506:    const string context = codegen_body_context_.empty()
src/native_generator.cpp:9507:        ? "main" : codegen_body_context_;
src/native_generator.cpp:9612:      const Stmt& s, const string& functional_context) const {
src/native_generator.cpp:9626:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9632:          req.specialization_context == context &&
src/native_generator.cpp:9633:          req.specialization_identity == codegen_active_specialization_key_) {
src/native_generator.cpp:9650:          fact.specialization_context == context &&
src/native_generator.cpp:9651:          fact.specialization_identity == codegen_active_specialization_key_) {
src/native_generator.cpp:9658:              req.specialization_context == fact.specialization_context &&
src/native_generator.cpp:9675:      const Stmt& s, const string& functional_context) const {
src/native_generator.cpp:9678:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9682:      if (req.specialization_context == context &&
src/native_generator.cpp:9683:          req.specialization_identity == codegen_active_specialization_key_) {
src/native_generator.cpp:9695:      if (fact.specialization_context == context &&
src/native_generator.cpp:9696:          fact.specialization_identity == codegen_active_specialization_key_) {
src/native_generator.cpp:9702:              req.specialization_context == fact.specialization_context &&
src/native_generator.cpp:9722:      const string& functional_context) const {
src/native_generator.cpp:9728:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9735:      if (requirement.specialization_context != context ||
src/native_generator.cpp:9737:              codegen_active_specialization_key_ ||
src/native_generator.cpp:9748:      const Stmt& s, const string& functional_context,
src/native_generator.cpp:9782:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9786:      if (!codegen_active_specialization_key_.empty()) {
src/native_generator.cpp:9789:            codegen_active_specialization_key_);
src/native_generator.cpp:9820:      const Stmt& s, const string& functional_context) const {
src/native_generator.cpp:9835:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:9836:    string active_specialization_identity = codegen_active_specialization_key_;
src/native_generator.cpp:9853:          req.specialization_context == context &&
src/native_generator.cpp:9885:                candidate.specialization_context == requirement->specialization_context;
src/native_generator.cpp:9906:      const Stmt& s, const string& functional_context) const {
src/native_generator.cpp:9916:    const string context = functional_context.empty() ? "main" : functional_context;
src/native_generator.cpp:10264:                 const string& functional_context = "") {
src/native_generator.cpp:10273:              functional_context);
src/native_generator.cpp:10282:                 const string& functional_context = "") {
src/native_generator.cpp:10293:      auto concrete_binding = s.semantic_types_by_context.find(functional_context);
src/native_generator.cpp:10318:      } codegen_context_scope(codegen_body_context_, functional_context);
src/native_generator.cpp:10368:          s, functional_context, 0);
src/native_generator.cpp:10394:            functional_context);
src/native_generator.cpp:10415:              resolve_try_continuation_fact(s, functional_context);
src/native_generator.cpp:10416:          auto ex_facts = resolve_exceptional_ownership_facts(s, functional_context);
src/native_generator.cpp:10464:              s.joined_types_by_context.find(functional_context);
src/native_generator.cpp:10514:                      functional_context);
src/native_generator.cpp:10558:                    functional_context);
src/native_generator.cpp:10601:                        nested, arm, *edge, identity, functional_context);
src/native_generator.cpp:10717:                        functional_context);
src/native_generator.cpp:10775:            const auto* take_fact = resolve_take_fact(s, functional_context);
src/native_generator.cpp:10780:                        s, functional_context, 0));
src/native_generator.cpp:10829:          const auto* match_fact = resolve_match_ownership_fact(s, functional_context);
src/native_generator.cpp:10870:          const auto exact_join = s.joined_types_by_context.find(functional_context);
src/native_generator.cpp:11041:                      branch_join, functional_context);
src/native_generator.cpp:11056:          const auto exact_join = s.joined_types_by_context.find(functional_context);
src/native_generator.cpp:11057:          if (functional_context.find('<') != string::npos &&
src/native_generator.cpp:11061:                functional_context);
src/native_generator.cpp:11104:                        s, functional_context, 0), nullptr,
src/native_generator.cpp:11115:                    functional_context);
src/native_generator.cpp:11125:                      functional_context);
src/native_generator.cpp:11135:          auto loop_facts = resolve_loop_ownership_facts(s, functional_context);
src/native_generator.cpp:11140:                        s, functional_context, 0), nullptr,
src/native_generator.cpp:11149:                    functional_context);
src/native_generator.cpp:11154:          auto loop_facts = resolve_loop_ownership_facts(s, functional_context);
src/native_generator.cpp:11194:                      functional_context);
src/native_generator.cpp:11214:                      join_assignments, functional_context);
src/native_generator.cpp:11229:                      join_assignments, functional_context);
src/native_generator.cpp:11245:              const auto* vector_element_fact = resolve_vector_element_ownership_fact(s, functional_context);
src/native_generator.cpp:11277:                      functional_context);
src/native_generator.cpp:11319:                    functional_context);
src/native_generator.cpp:11335:                  s, static_cast<std::uint32_t>(argument), functional_context);
src/native_generator.cpp:11338:                  statement_functional_pipeline_id(s, functional_context, argument) == 0 &&
src/native_generator.cpp:11348:                      statement_functional_pipeline_id(s, functional_context,
src/native_generator.cpp:11363:                auto checked = s.echo_types_by_context.find(functional_context);
src/native_generator.cpp:11390:                  auto checked = s.echo_types_by_context.find(functional_context);
src/native_generator.cpp:11419:                          s, functional_context, 0), nullptr,
src/native_generator.cpp:11430:                         s, functional_context, 0), nullptr,
src/native_generator.cpp:11450:          if (auto map_write = resolve_map_write_through(s, functional_context)) {
src/native_generator.cpp:11453:                statement_functional_pipeline_id(s, functional_context, 0))
src/native_generator.cpp:11507:                  std::to_string(s.line) + " context=" + functional_context);
src/native_generator.cpp:11540:            const auto* take_fact = resolve_take_fact(s, functional_context);
src/native_generator.cpp:11541:            const auto* ownership_fact = take_fact ? nullptr : resolve_local_ownership_fact(s, functional_context);
src/native_generator.cpp:11558:                                  s, functional_context, 0), nullptr,
src/native_generator.cpp:11588:                            s, functional_context, 0), nullptr,
src/native_generator.cpp:11631:                  statement_functional_pipeline_id(s, functional_context, 0),
src/native_generator.cpp:11654:                  statement_functional_pipeline_id(s, functional_context, 0),
src/native_generator.cpp:11662:                  resolve_local_transfer_fact(s, functional_context);
src/native_generator.cpp:11664:                  ? nullptr : resolve_take_fact(s, functional_context);
src/native_generator.cpp:11666:                  ? nullptr : resolve_local_ownership_fact(s, functional_context);
src/native_generator.cpp:11731:                    statement_functional_pipeline_id(s, functional_context, 0),
src/native_generator.cpp:11781:          auto checked_receiver = s.receiver_types_by_context.find(functional_context);
src/native_generator.cpp:11817:                arg_facts = arena->facts(arg_handle->expr, functional_context);
src/native_generator.cpp:11845:                          s, functional_context, 0))
src/native_generator.cpp:11860:                    s, 0, functional_context);
src/native_generator.cpp:11863:                    s, 0, functional_context);
src/native_generator.cpp:11866:                      s, 1, functional_context)
src/native_generator.cpp:11870:                      s, 1, functional_context)
src/native_generator.cpp:11877:                                 s, functional_context, 0)))
src/native_generator.cpp:11884:                                 s, functional_context, 1)))
src/native_generator.cpp:11987:                            s, functional_context, k))
src/native_generator.cpp:12022:                                              s, functional_context, k),
src/native_generator.cpp:12030:                        s, functional_context, k));
src/native_generator.cpp:12070:                              s, functional_context, k));
src/native_generator.cpp:12074:                            s, functional_context, k));
src/native_generator.cpp:12115:                std::to_string(s.line) + " context=" + functional_context);
src/native_generator.cpp:12151:            const auto* take_fact = resolve_take_fact(s, functional_context);
src/native_generator.cpp:12152:            const auto* ownership_fact = take_fact ? nullptr : resolve_local_ownership_fact(s, functional_context);
src/native_generator.cpp:12204:                                        s, functional_context, 0), nullptr,
src/native_generator.cpp:12275:            argument_plans.push_back(statement_functional_pipeline_id(s, functional_context, argument));
src/native_generator.cpp:12291:          const auto* take_fact = resolve_take_fact(s, functional_context);
src/native_generator.cpp:12293:              s, functional_context);
src/native_generator.cpp:12300:                  statement_functional_pipeline_id(s, functional_context, 0),
src/native_generator.cpp:12315:            const auto* take_fact = resolve_take_fact(s, functional_context);
src/native_generator.cpp:12317:                s, functional_context);
src/native_generator.cpp:12324:                            s, functional_context, 0));
src/native_generator.cpp:12369:                        s, functional_context, 0));
```
