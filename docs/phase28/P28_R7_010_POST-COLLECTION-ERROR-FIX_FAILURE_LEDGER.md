# Phase 28 Independent Semantic Oracle — Failure Ledger

Evaluated against: `/home/owner/daji/code/moss/tmp/phase28-r7-010/moss`
Specification Baseline: P28-R7-005 expectation table (CERTIFIED_FROZEN) and L7-approved language addendum.

## Summary Statistics
- **Total Fixtures:** 60
- **Passing all applicable gates:** 13
- **Failing at least one semantic or infrastructure gate:** 47
- **Semantic failures excluding clone provenance:** 15
- **Fixtures blocked by missing/invalid provenance:** 35
- **Unverified generated `.clone()` occurrences:** 524
- **Unauthorized rows with submitted provenance:** 0

## Detailed Defect Entries

| Failure ID | Fixture ID | Category | Mode | Description | Specification Citation |
| --- | --- | --- | --- | --- | --- |
| `DEFECT-ORACLE-01-VEC-WRITE-THROUGH` | `ORACLE-01-VEC-WRITE-THROUGH` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = v[0]; b.cost = 9', §1.2 ('One owner, temporary borrows'), §5.3 |
| `DEFECT-ORACLE-02-INDEXED-FIELD-READ-NO-CLONE` | `ORACLE-02-INDEXED-FIELD-READ-NO-CLONE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | Proposal §1.2, §1.3 ('Layout cannot change ownership'), §10, §12 ('Zero hidden clones') |
| `DEFECT-ORACLE-04-MAP-WRITE-THROUGH` | `ORACLE-04-MAP-WRITE-THROUGH` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10, §5.3 ('Collection ownership APIs'), §12 |
| `DEFECT-ORACLE-05-MAP-GET-BORROW` | `ORACLE-05-MAP-GET-BORROW` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | P28-R7-005 addendum: Map.get(k) borrows V; missing keys raise CollectionError.MissingKey. |
| `DEFECT-ORACLE-06-MATCH-CONSUME-OPT-NATIVE` | `ORACLE-06-MATCH-CONSUME-OPT-NATIVE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §6.2 ('match consume: transfer payloads from an owned enum'), §10, §12 |
| `DEFECT-ORACLE-07-CATCHABLE-COLL-ERRORS` | `ORACLE-07-CATCHABLE-COLL-ERRORS` | oracle | `checker:UNEXPECTED_REJECT, fast_debug:CRASH, native_debug:EMIT_FAIL, native_opt:EMIT_FAIL` | Checker unexpectedly REJECTED valid program: {   "protocol_version": 1,   "schema_version": "moss-agent-1",   "compiler_ | L7-approved language addendum: positive and negative invalid remove/swap_remove, empty pops, missing Map operations raise typed CollectionError variants; insertion succeeds. |
| `DEFECT-ORACLE-09-USER-ENUM-SOME-NONE` | `ORACLE-09-USER-ENUM-SOME-NONE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=; Native optimized | Proposal §6 ('Applying Rule 4 to enums'), §10, §12 |
| `DEFECT-ORACLE-10A-EXPLICIT-COPY-ONLY` | `ORACLE-10A-EXPLICIT-COPY-ONLY` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=; Native optimized | Proposal §10 row 'copy(v[0]) for an eligible value', §11 Phase D |
| `DEFECT-ORACLE-11-ASSIGN-READ-BORROW` | `ORACLE-11-ASSIGN-READ-BORROW` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = a; echo b; echo a', §3 |
| `DEFECT-ORACLE-12-ASSIGN-WRITE-BORROW` | `ORACLE-12-ASSIGN-WRITE-BORROW` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = a; b.field = 1', §3 |
| `DEFECT-ORACLE-13-ASSIGN-CONSUME-RETURN` | `ORACLE-13-ASSIGN-CONSUME-RETURN` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = a; return b', §3 |
| `DEFECT-ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` | `ORACLE-15-PLAIN-FIELD-CONSUME-REJECT` | oracle | `checker:UNEXPECTED_ACCEPT` | Checker unexpectedly ACCEPTED invalid program; expected the oracle-mapped structured rejection | Proposal §10 row 'b = state.frontier; archive(b)', §5.2 |
| `DEFECT-ORACLE-16-TAKE-FRONTIER-ARCHIVE` | `ORACLE-16-TAKE-FRONTIER-ARCHIVE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = take(state.frontier); archive(b)', §5.1, §5.2 |
| `DEFECT-ORACLE-17-TAKE-OPTION-FIELD` | `ORACLE-17-TAKE-OPTION-FIELD` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = take(job.pending)', §5.1 |
| `DEFECT-ORACLE-18-TAKE-QUEUE-FIELD` | `ORACLE-18-TAKE-QUEUE-FIELD` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'take(state.queue)', §5.1 |
| `DEFECT-ORACLE-19-TAKE-NESTED-VEC-OPTION` | `ORACLE-19-TAKE-NESTED-VEC-OPTION` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=; Native optimized | Proposal §10 row 'take(v[i].pending)', §5.1 |
| `DEFECT-ORACLE-20-TAKE-NESTED-MAP-VECTOR` | `ORACLE-20-TAKE-NESTED-MAP-VECTOR` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=; Native optimized | Proposal §10 row 'take(m[k].items)', §5.1 |
| `DEFECT-ORACLE-25-TAKE-BEFORE-RAISE-MUTATION` | `ORACLE-25-TAKE-BEFORE-RAISE-MUTATION` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=; Native optimized | Proposal §10 row 'b = take(state.frontier); may_raise(); use(b)', §8, §9 |
| `DEFECT-ORACLE-26-VEC-REMOVE-API` | `ORACLE-26-VEC-REMOVE-API` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'nxt.push(head.remove(0))', §5.3 |
| `DEFECT-ORACLE-27-REBIND-AFTER-BORROWS` | `ORACLE-27-REBIND-AFTER-BORROWS` | oracle | `native_debug:RUSTC_FAIL, native_opt:RUSTC_FAIL` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; rustc compilatio | Proposal §10 row 'b = a; b = new_value', §3.1 |
| `DEFECT-ORACLE-28-REBIND-WHILE-BORROWED-REJECT` | `ORACLE-28-REBIND-WHILE-BORROWED-REJECT` | oracle | `checker:WRONG_DIAGNOSTIC` | Checker rejection was unrelated or malformed (diagnostic message does not match 'borrow|rebind|ownership'); output: {    | Proposal §10 row 'c = b; b = make2(); echo c', §3.2 |
| `DEFECT-ORACLE-29-LOOP-CARRIED-OWNERSHIP` | `ORACLE-29-LOOP-CARRIED-OWNERSHIP` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | Proposal §10 row 'frontier = expand(frontier) in a loop', §4.2, §7.1 |
| `DEFECT-ORACLE-30-JOIN-BORROW-FRESH-REJECT` | `ORACLE-30-JOIN-BORROW-FRESH-REJECT` | oracle | `checker:WRONG_DIAGNOSTIC` | Checker rejection was unrelated or malformed (diagnostic line 13 does not identify the rejected construct); output: {    | Proposal §10 row 'if c: b = left[0] else: b = Fresh(); echo b', §4 |
| `DEFECT-ORACLE-31-JOIN-PROMOTED-CONSUME` | `ORACLE-31-JOIN-PROMOTED-CONSUME` | oracle | `checker:UNEXPECTED_REJECT, fast_debug:CRASH, native_debug:EMIT_FAIL, native_opt:EMIT_FAIL` | Checker unexpectedly REJECTED valid program: {   "protocol_version": 1,   "schema_version": "moss-agent-1",   "compiler_ | Proposal §10 row 'if c: b = left else: b = Fresh(); echo b', §4 |
| `DEFECT-ORACLE-32-MATCH-PAYLOAD-WRITE` | `ORACLE-32-MATCH-PAYLOAD-WRITE` | oracle | `checker:UNEXPECTED_REJECT, fast_debug:CRASH, native_debug:EMIT_FAIL, native_opt:EMIT_FAIL` | Checker unexpectedly REJECTED valid program: {   "protocol_version": 1,   "schema_version": "moss-agent-1",   "compiler_ | Proposal §10 row 'match job.state and mutate a payload', §6.1 |
| `DEFECT-ORACLE-33-MATCH-LAST-USE-OVERWRITE` | `ORACLE-33-MATCH-LAST-USE-OVERWRITE` | oracle | `checker:UNEXPECTED_REJECT, fast_debug:CRASH, native_debug:EMIT_FAIL, native_opt:EMIT_FAIL` | Checker unexpectedly REJECTED valid program: {   "protocol_version": 1,   "schema_version": "moss-agent-1",   "compiler_ | Proposal §10 row 'match job.state then overwrite state after last pattern use', §6.1, §8 |
| `DEFECT-ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT` | `ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT` | oracle | `checker:WRONG_DIAGNOSTIC` | Checker rejection was unrelated or malformed (diagnostic message does not match 'borrow|consum|match'); output: {   "pro | Proposal §10 row 'match job.state and transfer payload', §6.1 |
| `DEFECT-ORACLE-36-MATCH-CONSUME-QUEUE-POP` | `ORACLE-36-MATCH-CONSUME-QUEUE-POP` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'match consume queue.pop()', §6.2 |
| `DEFECT-ORACLE-38-MAP-VALUES-BORROWED-VIEW` | `ORACLE-38-MAP-VALUES-BORROWED-VIEW` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=; Native optimized | Proposal §10 row 'Map.values() bound temporarily then source mutated after last use', §5.3 |
| `DEFECT-ORACLE-39-VEC-FOR-READ` | `ORACLE-39-VEC-FOR-READ` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'for item in items: echo item for a Vector', §5.3, §12 |
| `DEFECT-ORACLE-40-VEC-FOR-WRITE` | `ORACLE-40-VEC-FOR-WRITE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'for item in items: item.bump() for writable Vector elements', §5.3, §12 |
| `DEFECT-ORACLE-42-CUSTOM-WRAPPER-REMOVE` | `ORACLE-42-CUSTOM-WRAPPER-REMOVE` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | Proposal §10 row 'User-defined wrapper fn remove_at(a, i): return a.elements.remove(i)', §9 |
| `DEFECT-ORACLE-43-HANDLER-PRIMITIVE-REPLY` | `ORACLE-43-HANDLER-PRIMITIVE-REPLY` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'reply n where n: Int', §7.5 |
| `DEFECT-ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` | `ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | Proposal §10 row 'result = n; result = result + 1; reply result', §7.5 |
| `DEFECT-ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` | `ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT` | oracle | `checker:UNEXPECTED_ACCEPT` | Checker unexpectedly ACCEPTED invalid program; expected the oracle-mapped structured rejection | Proposal §10 row 'reply payload where payload: Report', §7.5 |
| `DEFECT-ORACLE-47-VECTOR-INDEX-PANICS` | `ORACLE-47-VECTOR-INDEX-PANICS` | oracle | `fast_debug:EXPECTED_PANIC, native_debug:EXPECTED_PANIC, native_opt:EXPECTED_PANIC` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | P28-R7-005 addendum: Vector indexing out of range panics and does not enter recover. |
| `DEFECT-ORACLE-48-ROOT-MISSING-COLLECTION-ERROR` | `ORACLE-48-ROOT-MISSING-COLLECTION-ERROR` | oracle | `checker:WRONG_DIAGNOSTIC` | Checker rejection was unrelated or malformed (diagnostic code mismatch: 'MOSS_COMPILE_ERROR'); output: {   "protocol_ver | P28-R7-005 addendum: Root totality rejects an unhandled propagated CollectionError. |
| `DEFECT-ORACLE-49-OWNERSHIP-AT-RECOVER` | `ORACLE-49-OWNERSHIP-AT-RECOVER` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | P28-R7-005-09: successful destructive remove precedes typed raise; recover observes the already-mutated source Vector. |
| `DEFECT-ORACLE-50-EAGER-PIPELINE-CAPTURES` | `ORACLE-50-EAGER-PIPELINE-CAPTURES` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=; Native optimized | P28-R7-005 coverage: pipeline captures are eager and retain checked ownership effects. |
| `DEFECT-ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS` | `ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS` | oracle | `native_debug:RUSTC_FAIL, native_opt:RUSTC_FAIL` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; rustc compilatio | P28-R7-005 coverage: joins of borrowed provenance sets preserve all possible sources. |
| `DEFECT-ORACLE-53-MAP-REMOVE-MISSING` | `ORACLE-53-MAP-REMOVE-MISSING` | oracle | `checker:UNEXPECTED_REJECT, fast_debug:CRASH, native_debug:EMIT_FAIL, native_opt:EMIT_FAIL` | Checker unexpectedly REJECTED valid program: {   "protocol_version": 1,   "schema_version": "moss-agent-1",   "compiler_ | P28-R7-005 addendum: Map.remove(k) missing key raises CollectionError.MissingKey. |
| `DEFECT-ORACLE-54-IMPORTED-MOSSI-EFFECTS` | `ORACLE-54-IMPORTED-MOSSI-EFFECTS` | oracle | `imported_mossi_effects:MISSING_EFFECT` | Imported .mossi omitted CollectionError.MissingKey raise effect: {   "protocol_version": 1,   "schema_version": "moss-ag | P28-R7-005 coverage: CollectionError raise summaries survive imported .mossi interfaces and can be recovered by the consumer. |
| `DEFECT-ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE` | `ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE` | oracle | `inferred_effects:RAISE_SET_MISMATCH` | Inferred raise-set mismatch: expected ['enum:CollectionError:MissingKey'], got ['enum:std__CollectionError:MissingKey']; | P28-R7-005: CollectionError.MissingKey appears in the inferred raise set of a helper that performs strict Map indexing. |
| `DEFECT-ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ` | `ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=; Native optimized | R7-009 primitive element contract: scalar indexing and Vector iteration copy primitive values without consuming the Vector owner; preserve nonprimitive element consume rejection. |
| `DEFECT-ORACLE-57-MAP-GET-WRITE-THROUGH` | `ORACLE-57-MAP-GET-WRITE-THROUGH` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=; Native optimized | P28-R7-005 frozen Language Addendum: one-argument Map.get(key) returns a borrowed V and writes through to the stored value. |
| `DEFECT-ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION` | `ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=10, unauthorized=0, error=; Native optimize | P28 EST/specialization ownership contract: the same generic callable instantiated at distinct concrete types must retain isolated per-specialization checked facts. |
| `DEFECT-ORACLE-59-SHADOWED-BINDING-IDENTITY` | `ORACLE-59-SHADOWED-BINDING-IDENTITY` | oracle | `CLONES` | Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=; Native optimized | Moss binding identity contract: a block-local shadow of a sibling function name is distinct from the function binding; the function remains callable outside that local scope. |

## Per-Fixture Detail

### ORACLE-01-VEC-WRITE-THROUGH: ORACLE-01-VEC-WRITE-THROUGH (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = v[0]; b.cost = 9', §1.2 ('One owner, temporary borrows'), §5.3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3415}, "start": {"column": 75, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3415}, "start": {"column": 75, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-02-INDEXED-FIELD-READ-NO-CLONE: ORACLE-02-INDEXED-FIELD-READ-NO-CLONE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §1.2, §1.3 ('Layout cannot change ownership'), §10, §12 ('Zero hidden clones')
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3419}, "start": {"column": 73, "line": 3419}}, {"end": {"column": 125, "line": 3419}, "start": {"column": 117, "line": 3419}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3419}, "start": {"column": 73, "line": 3419}}, {"end": {"column": 125, "line": 3419}, "start": {"column": 117, "line": 3419}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-03-COLL-ELEM-CONSUME-REJECT: ORACLE-03-COLL-ELEM-CONSUME-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'nxt.push(head[0])', §5.3, §12
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-04-MAP-WRITE-THROUGH: ORACLE-04-MAP-WRITE-THROUGH (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10, §5.3 ('Collection ownership APIs'), §12
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3415}, "start": {"column": 75, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3415}, "start": {"column": 75, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-05-MAP-GET-BORROW: ORACLE-05-MAP-GET-BORROW (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 addendum: Map.get(k) borrows V; missing keys raise CollectionError.MissingKey.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-06-MATCH-CONSUME-OPT-NATIVE: ORACLE-06-MATCH-CONSUME-OPT-NATIVE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §6.2 ('match consume: transfer payloads from an owned enum'), §10, §12
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3415}, "start": {"column": 73, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3415}, "start": {"column": 73, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-07-CATCHABLE-COLL-ERRORS: ORACLE-07-CATCHABLE-COLL-ERRORS (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** L7-approved language addendum: positive and negative invalid remove/swap_remove, empty pops, missing Map operations raise typed CollectionError variants; insertion succeeds.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: UNEXPECTED_REJECT | fast_debug: CRASH | native_debug: EMIT_FAIL | native_opt: EMIT_FAIL
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly REJECTED valid program: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "MOSS_INTERNAL_OR_IO_ERROR", "severity": "error", "message": "unknown type variable", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_07_catchable_collection_errors.moss", "line": 0, "column": 1, "span": null, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_07_catchable_collection_errors.moss", "line": 0, "column": 1, "span": null}, "semantic_identity": null, "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`
  - `Fast Debug observable mutation expectation failed`
  - `Fast Debug crashed (exit 1): moss: error: unknown type variable`
  - `Native emission failed: moss: error: unknown type variable`
  - `Native -O emission failed: moss: error: unknown type variable`

### ORACLE-08-USER-COPY-RESOLUTION: ORACLE-08-USER-COPY-RESOLUTION (✅ PASS)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 addendum: copy is reserved; user-defined fn copy is rejected.
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-09-USER-ENUM-SOME-NONE: ORACLE-09-USER-ENUM-SOME-NONE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §6 ('Applying Rule 4 to enums'), §10, §12
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`

### ORACLE-10A-EXPLICIT-COPY-ONLY: ORACLE-10A-EXPLICIT-COPY-ONLY (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'copy(v[0]) for an eligible value', §11 Phase D
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3415}, "start": {"column": 77, "line": 3415}}, {"end": {"column": 54, "line": 3449}, "start": {"column": 46, "line": 3449}}, {"end": {"column": 63, "line": 3449}, "start": {"column": 55, "line": 3449}}], "source_copy_spans": [{"copy_id": "copy@7:16", "end": {"column": 26, "line": 7}, "start": {"column": 16, "line": 7}}], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3415}, "start": {"column": 77, "line": 3415}}, {"end": {"column": 54, "line": 3449}, "start": {"column": 46, "line": 3449}}, {"end": {"column": 63, "line": 3449}, "start": {"column": 55, "line": 3449}}], "source_copy_spans": [{"copy_id": "copy@7:16", "end": {"column": 26, "line": 7}, "start": {"column": 16, "line": 7}}], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`

### ORACLE-10B-CLONE-ALIAS-REJECTED: ORACLE-10B-CLONE-ALIAS-REJECTED (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'v[0].clone() as a source-language duplication alias', §11 Phase D
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-11-ASSIGN-READ-BORROW: ORACLE-11-ASSIGN-READ-BORROW (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = a; echo b; echo a', §3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3419}, "start": {"column": 71, "line": 3419}}, {"end": {"column": 113, "line": 3419}, "start": {"column": 105, "line": 3419}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3419}, "start": {"column": 71, "line": 3419}}, {"end": {"column": 113, "line": 3419}, "start": {"column": 105, "line": 3419}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-12-ASSIGN-WRITE-BORROW: ORACLE-12-ASSIGN-WRITE-BORROW (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = a; b.field = 1', §3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3415}, "start": {"column": 83, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3415}, "start": {"column": 83, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-13-ASSIGN-CONSUME-RETURN: ORACLE-13-ASSIGN-CONSUME-RETURN (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = a; return b', §3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}, {"end": {"column": 23, "line": 3450}, "start": {"column": 15, "line": 3450}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}, {"end": {"column": 23, "line": 3450}, "start": {"column": 15, "line": 3450}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT: ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = a; return b' (reject borrowed non-movable place), §3
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-15-PLAIN-FIELD-CONSUME-REJECT: ORACLE-15-PLAIN-FIELD-CONSUME-REJECT (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = state.frontier; archive(b)', §5.2
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: UNEXPECTED_ACCEPT
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly ACCEPTED invalid program; expected the oracle-mapped structured rejection`

### ORACLE-16-TAKE-FRONTIER-ARCHIVE: ORACLE-16-TAKE-FRONTIER-ARCHIVE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = take(state.frontier); archive(b)', §5.1, §5.2
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-17-TAKE-OPTION-FIELD: ORACLE-17-TAKE-OPTION-FIELD (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = take(job.pending)', §5.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-18-TAKE-QUEUE-FIELD: ORACLE-18-TAKE-QUEUE-FIELD (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(state.queue)', §5.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 93, "line": 3415}, "start": {"column": 85, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-19-TAKE-NESTED-VEC-OPTION: ORACLE-19-TAKE-NESTED-VEC-OPTION (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(v[i].pending)', §5.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3419}, "start": {"column": 71, "line": 3419}}, {"end": {"column": 125, "line": 3419}, "start": {"column": 117, "line": 3419}}, {"end": {"column": 53, "line": 3477}, "start": {"column": 45, "line": 3477}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3419}, "start": {"column": 71, "line": 3419}}, {"end": {"column": 125, "line": 3419}, "start": {"column": 117, "line": 3419}}, {"end": {"column": 53, "line": 3477}, "start": {"column": 45, "line": 3477}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`

### ORACLE-20-TAKE-NESTED-MAP-VECTOR: ORACLE-20-TAKE-NESTED-MAP-VECTOR (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(m[k].items)', §5.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3419}, "start": {"column": 77, "line": 3419}}, {"end": {"column": 127, "line": 3419}, "start": {"column": 119, "line": 3419}}, {"end": {"column": 124, "line": 3481}, "start": {"column": 116, "line": 3481}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3419}, "start": {"column": 77, "line": 3419}}, {"end": {"column": 127, "line": 3419}, "start": {"column": 119, "line": 3419}}, {"end": {"column": 124, "line": 3481}, "start": {"column": 116, "line": 3481}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`

### ORACLE-21-TAKE-LOCAL-REJECTED: ORACLE-21-TAKE-LOCAL-REJECTED (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(local)', §5.1
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-22-TAKE-UNIFIED-FIELD-REJECT: ORACLE-22-TAKE-UNIFIED-FIELD-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(job.report)', §5.1, §9
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-23-TAKE-VEC-ELEM-REJECTED: ORACLE-23-TAKE-VEC-ELEM-REJECTED (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(v[0])', §5.1
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-24-TAKE-CUSTOM-SLOT-REJECT: ORACLE-24-TAKE-CUSTOM-SLOT-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'take(my_array.slots[i])', §9
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-25-TAKE-BEFORE-RAISE-MUTATION: ORACLE-25-TAKE-BEFORE-RAISE-MUTATION (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = take(state.frontier); may_raise(); use(b)', §8, §9
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 101, "line": 3440}, "start": {"column": 93, "line": 3440}}, {"end": {"column": 108, "line": 3519}, "start": {"column": 100, "line": 3519}}, {"end": {"column": 108, "line": 3578}, "start": {"column": 100, "line": 3578}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 101, "line": 3440}, "start": {"column": 93, "line": 3440}}, {"end": {"column": 108, "line": 3512}, "start": {"column": 100, "line": 3512}}, {"end": {"column": 108, "line": 3557}, "start": {"column": 100, "line": 3557}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`

### ORACLE-26-VEC-REMOVE-API: ORACLE-26-VEC-REMOVE-API (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'nxt.push(head.remove(0))', §5.3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3415}, "start": {"column": 73, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 81, "line": 3415}, "start": {"column": 73, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-27-REBIND-AFTER-BORROWS: ORACLE-27-REBIND-AFTER-BORROWS (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'b = a; b = new_value', §3.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: RUSTC_FAIL | native_opt: RUSTC_FAIL
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3415}, "start": {"column": 71, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3415}, "start": {"column": 71, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `rustc compilation failed: error[E0308]: mismatched types
    --> /home/owner/daji/code/moss/tmp/phase28-r7-010/tmp/phase28_oracle_r7_005_1/ORACLE-27-REBIND-AFTER-BORROWS.rs:3454:9
     |
3450 |     let mut b = &(a);
     |                 ---- expected due to this value
...
3454 |     b = Box { num: 2_i64 };
     |         ^`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `rustc -O compilation failed: error[E0308]: mismatched types
    --> /home/owner/daji/code/moss/tmp/phase28-r7-010/tmp/phase28_oracle_r7_005_1/ORACLE-27-REBIND-AFTER-BORROWS_opt.rs:3454:9
     |
3450 |     let mut b = &(a);
     |                 ---- expected due to this value
...
3454 |     b = Box { num: 2_i64 };
     |      `

### ORACLE-28-REBIND-WHILE-BORROWED-REJECT: ORACLE-28-REBIND-WHILE-BORROWED-REJECT (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'c = b; b = make2(); echo c', §3.2
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: WRONG_DIAGNOSTIC
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker rejection was unrelated or malformed (diagnostic message does not match 'borrow|rebind|ownership'); output: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "OWNERSHIP_SOURCE_BORROWED", "severity": "error", "message": "cannot overwrite or consume 'b' while READ alias 'c' remains live", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_28_rebind_while_borrowed_rejected.moss", "line": 11, "column": 1, "span": {"start_line": 11, "start_column": 1, "end_line": 11, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_28_rebind_while_borrowed_rejected.moss", "line": 11, "column": 1, "span": {"start_line": 11, "start_column": 1, "end_line": 11, "end_column": 1}}, "semantic_identity": "main@8", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`

### ORACLE-29-LOOP-CARRIED-OWNERSHIP: ORACLE-29-LOOP-CARRIED-OWNERSHIP (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'frontier = expand(frontier) in a loop', §4.2, §7.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 58, "line": 3435}, "start": {"column": 50, "line": 3435}}, {"end": {"column": 58, "line": 3437}, "start": {"column": 50, "line": 3437}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 58, "line": 3435}, "start": {"column": 50, "line": 3435}}, {"end": {"column": 58, "line": 3437}, "start": {"column": 50, "line": 3437}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-30-JOIN-BORROW-FRESH-REJECT: ORACLE-30-JOIN-BORROW-FRESH-REJECT (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'if c: b = left[0] else: b = Fresh(); echo b', §4
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: WRONG_DIAGNOSTIC
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker rejection was unrelated or malformed (diagnostic line 13 does not identify the rejected construct); output: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "OWNERSHIP_INCOMPATIBLE_JOIN", "severity": "error", "message": "cannot merge owned and borrowed values for 'b' at control flow join", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_30_join_borrowed_and_fresh_rejected.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_30_join_borrowed_and_fresh_rejected.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}}, "semantic_identity": "fn:test@8", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`

### ORACLE-31-JOIN-PROMOTED-CONSUME: ORACLE-31-JOIN-PROMOTED-CONSUME (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'if c: b = left else: b = Fresh(); echo b', §4
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: UNEXPECTED_REJECT | fast_debug: CRASH | native_debug: EMIT_FAIL | native_opt: EMIT_FAIL
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly REJECTED valid program: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "OWNERSHIP_INCOMPATIBLE_JOIN", "severity": "error", "message": "cannot merge owned and borrowed values for 'b' at control flow join", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_31_join_promoted_consume.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_31_join_promoted_consume.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}}, "semantic_identity": "fn:test@8", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`
  - `Fast Debug crashed (exit 1): moss:13: error: [OWNERSHIP_INCOMPATIBLE_JOIN] cannot merge owned and borrowed values for 'b' at control flow join`
  - `Native emission failed: moss:13: error: [OWNERSHIP_INCOMPATIBLE_JOIN] cannot merge owned and borrowed values for 'b' at control flow join`
  - `Native -O emission failed: moss:13: error: [OWNERSHIP_INCOMPATIBLE_JOIN] cannot merge owned and borrowed values for 'b' at control flow join`

### ORACLE-32-MATCH-PAYLOAD-WRITE: ORACLE-32-MATCH-PAYLOAD-WRITE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'match job.state and mutate a payload', §6.1
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: UNEXPECTED_REJECT | fast_debug: CRASH | native_debug: EMIT_FAIL | native_opt: EMIT_FAIL
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly REJECTED valid program: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "BORROWED_ENUM_PAYLOAD_ACCESS", "severity": "error", "message": "payload 'buf' is a READ view of 's' and cannot be written or transferred", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_32_match_payload_write_borrow.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_32_match_payload_write_borrow.moss", "line": 13, "column": 1, "span": {"start_line": 13, "start_column": 1, "end_line": 13, "end_column": 1}}, "semantic_identity": "main@9", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`
  - `Fast Debug crashed (exit 1): moss:13: error: [BORROWED_ENUM_PAYLOAD_ACCESS] payload 'buf' is a READ view of 's' and cannot be written or transferred`
  - `Native emission failed: moss:13: error: [BORROWED_ENUM_PAYLOAD_ACCESS] payload 'buf' is a READ view of 's' and cannot be written or transferred`
  - `Native -O emission failed: moss:13: error: [BORROWED_ENUM_PAYLOAD_ACCESS] payload 'buf' is a READ view of 's' and cannot be written or transferred`

### ORACLE-33-MATCH-LAST-USE-OVERWRITE: ORACLE-33-MATCH-LAST-USE-OVERWRITE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'match job.state then overwrite state after last pattern use', §6.1, §8
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: UNEXPECTED_REJECT | fast_debug: CRASH | native_debug: EMIT_FAIL | native_opt: EMIT_FAIL
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly REJECTED valid program: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "MATCH_READ_BORROW_ACTIVE", "severity": "error", "message": "cannot write 's' while 's' is READ-borrowed by a match or traversal", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_33_match_payload_last_use_overwrite.moss", "line": 14, "column": 1, "span": {"start_line": 14, "start_column": 1, "end_line": 14, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_33_match_payload_last_use_overwrite.moss", "line": 14, "column": 1, "span": {"start_line": 14, "start_column": 1, "end_line": 14, "end_column": 1}}, "semantic_identity": "main@9", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`
  - `Fast Debug crashed (exit 1): moss:14: error: [MATCH_READ_BORROW_ACTIVE] cannot write 's' while 's' is READ-borrowed by a match or traversal`
  - `Native emission failed: moss:14: error: [MATCH_READ_BORROW_ACTIVE] cannot write 's' while 's' is READ-borrowed by a match or traversal`
  - `Native -O emission failed: moss:14: error: [MATCH_READ_BORROW_ACTIVE] cannot write 's' while 's' is READ-borrowed by a match or traversal`

### ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT: ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'Overwrite matched state while its payload is still borrowed', §6.1
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT: ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'match job.state and transfer payload', §6.1
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: WRONG_DIAGNOSTIC
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker rejection was unrelated or malformed (diagnostic message does not match 'borrow|consum|match'); output: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "BORROWED_ENUM_PAYLOAD_ACCESS", "severity": "error", "message": "payload 'buf' is a READ view of 's' and cannot be written or transferred", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_35_match_ordinary_consume_rejected.moss", "line": 16, "column": 1, "span": {"start_line": 16, "start_column": 1, "end_line": 16, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_35_match_ordinary_consume_rejected.moss", "line": 16, "column": 1, "span": {"start_line": 16, "start_column": 1, "end_line": 16, "end_column": 1}}, "semantic_identity": "main@12", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`

### ORACLE-36-MATCH-CONSUME-QUEUE-POP: ORACLE-36-MATCH-CONSUME-QUEUE-POP (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'match consume queue.pop()', §6.2
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3439}, "start": {"column": 75, "line": 3439}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 83, "line": 3439}, "start": {"column": 75, "line": 3439}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT: ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'match consume job.state', §6.2, §9
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-38-MAP-VALUES-BORROWED-VIEW: ORACLE-38-MAP-VALUES-BORROWED-VIEW (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'Map.values() bound temporarily then source mutated after last use', §5.3
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`

### ORACLE-39-VEC-FOR-READ: ORACLE-39-VEC-FOR-READ (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'for item in items: echo item for a Vector', §5.3, §12
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3415}, "start": {"column": 71, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 79, "line": 3415}, "start": {"column": 71, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-40-VEC-FOR-WRITE: ORACLE-40-VEC-FOR-WRITE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'for item in items: item.bump() for writable Vector elements', §5.3, §12
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3415}, "start": {"column": 77, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 85, "line": 3415}, "start": {"column": 77, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-41-VEC-FOR-CONSUME-REJECT: ORACLE-41-VEC-FOR-CONSUME-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'for item in items: consume(item)', §5.3, §12
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-42-CUSTOM-WRAPPER-REMOVE: ORACLE-42-CUSTOM-WRAPPER-REMOVE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'User-defined wrapper fn remove_at(a, i): return a.elements.remove(i)', §9
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 89, "line": 3415}, "start": {"column": 81, "line": 3415}}, {"end": {"column": 62, "line": 3488}, "start": {"column": 54, "line": 3488}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 89, "line": 3415}, "start": {"column": 81, "line": 3415}}, {"end": {"column": 62, "line": 3488}, "start": {"column": 54, "line": 3488}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-43-HANDLER-PRIMITIVE-REPLY: ORACLE-43-HANDLER-PRIMITIVE-REPLY (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'reply n where n: Int', §7.5
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2015}, "start": {"column": 42, "line": 2015}}, {"end": {"column": 46, "line": 2242}, "start": {"column": 38, "line": 2242}}, {"end": {"column": 59, "line": 2358}, "start": {"column": 51, "line": 2358}}, {"end": {"column": 69, "line": 2908}, "start": {"column": 61, "line": 2908}}, {"end": {"column": 66, "line": 2950}, "start": {"column": 58, "line": 2950}}, {"end": {"column": 42, "line": 3113}, "start": {"column": 34, "line": 3113}}, {"end": {"column": 57, "line": 3475}, "start": {"column": 49, "line": 3475}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2015}, "start": {"column": 42, "line": 2015}}, {"end": {"column": 46, "line": 2242}, "start": {"column": 38, "line": 2242}}, {"end": {"column": 59, "line": 2358}, "start": {"column": 51, "line": 2358}}, {"end": {"column": 69, "line": 2908}, "start": {"column": 61, "line": 2908}}, {"end": {"column": 66, "line": 2950}, "start": {"column": 58, "line": 2950}}, {"end": {"column": 42, "line": 3113}, "start": {"column": 34, "line": 3113}}, {"end": {"column": 57, "line": 3475}, "start": {"column": 49, "line": 3475}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT: ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT (✅ PASS)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'n = n + 1 where n: Int', §7.5
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL: ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'result = n; result = result + 1; reply result', §7.5
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2015}, "start": {"column": 42, "line": 2015}}, {"end": {"column": 46, "line": 2242}, "start": {"column": 38, "line": 2242}}, {"end": {"column": 59, "line": 2358}, "start": {"column": 51, "line": 2358}}, {"end": {"column": 69, "line": 2908}, "start": {"column": 61, "line": 2908}}, {"end": {"column": 66, "line": 2950}, "start": {"column": 58, "line": 2950}}, {"end": {"column": 42, "line": 3113}, "start": {"column": 34, "line": 3113}}, {"end": {"column": 58, "line": 3480}, "start": {"column": 50, "line": 3480}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2015}, "start": {"column": 42, "line": 2015}}, {"end": {"column": 46, "line": 2242}, "start": {"column": 38, "line": 2242}}, {"end": {"column": 59, "line": 2358}, "start": {"column": 51, "line": 2358}}, {"end": {"column": 69, "line": 2908}, "start": {"column": 61, "line": 2908}}, {"end": {"column": 66, "line": 2950}, "start": {"column": 58, "line": 2950}}, {"end": {"column": 42, "line": 3113}, "start": {"column": 34, "line": 3113}}, {"end": {"column": 58, "line": 3480}, "start": {"column": 50, "line": 3480}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT: ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Proposal §10 row 'reply payload where payload: Report', §7.5
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: UNEXPECTED_ACCEPT
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly ACCEPTED invalid program; expected the oracle-mapped structured rejection`

### ORACLE-47-VECTOR-INDEX-PANICS: ORACLE-47-VECTOR-INDEX-PANICS (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 addendum: Vector indexing out of range panics and does not enter recover.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: EXPECTED_PANIC | native_debug: EXPECTED_PANIC | native_opt: EXPECTED_PANIC
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 60, "line": 3427}, "start": {"column": 52, "line": 3427}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 60, "line": 3427}, "start": {"column": 52, "line": 3427}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-48-ROOT-MISSING-COLLECTION-ERROR: ORACLE-48-ROOT-MISSING-COLLECTION-ERROR (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 addendum: Root totality rejects an unhandled propagated CollectionError.
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: WRONG_DIAGNOSTIC
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker rejection was unrelated or malformed (diagnostic code mismatch: 'MOSS_COMPILE_ERROR'); output: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "MOSS_COMPILE_ERROR", "severity": "error", "message": "main may not let typed errors escape; handle every raised variant with try/recover", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_48_root_missing_collection_error.moss", "line": 8, "column": 1, "span": {"start_line": 8, "start_column": 1, "end_line": 8, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_48_root_missing_collection_error.moss", "line": 8, "column": 1, "span": {"start_line": 8, "start_column": 1, "end_line": 8, "end_column": 1}}, "semantic_identity": "main@8", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`

### ORACLE-49-OWNERSHIP-AT-RECOVER: ORACLE-49-OWNERSHIP-AT-RECOVER (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005-09: successful destructive remove precedes typed raise; recover observes the already-mutated source Vector.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3421}, "start": {"column": 83, "line": 3421}}, {"end": {"column": 108, "line": 3468}, "start": {"column": 100, "line": 3468}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3421}, "start": {"column": 83, "line": 3421}}, {"end": {"column": 108, "line": 3468}, "start": {"column": 100, "line": 3468}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-50-EAGER-PIPELINE-CAPTURES: ORACLE-50-EAGER-PIPELINE-CAPTURES (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 coverage: pipeline captures are eager and retain checked ownership effects.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 57, "line": 3438}, "start": {"column": 49, "line": 3438}}, {"end": {"column": 57, "line": 3440}, "start": {"column": 49, "line": 3440}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 57, "line": 3438}, "start": {"column": 49, "line": 3438}}, {"end": {"column": 57, "line": 3440}, "start": {"column": 49, "line": 3440}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 8}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=8, unauthorized=0, error=`

### ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS: ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 coverage: joins of borrowed provenance sets preserve all possible sources.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: RUSTC_FAIL | native_opt: RUSTC_FAIL
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3415}, "start": {"column": 83, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 91, "line": 3415}, "start": {"column": 83, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `rustc compilation failed: error[E0308]: mismatched types
    --> /home/owner/daji/code/moss/tmp/phase28-r7-010/tmp/phase28_oracle_r7_005_1/ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS.rs:3459:23
     |
3459 |         (*borrowed) = &mut ((right)[(0_i64) as usize]);
     |         -----------   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ expe`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `rustc -O compilation failed: error[E0308]: mismatched types
    --> /home/owner/daji/code/moss/tmp/phase28-r7-010/tmp/phase28_oracle_r7_005_1/ORACLE-51-JOIN-BORROWED-PROVENANCE-SETS_opt.rs:3459:23
     |
3459 |         (*borrowed) = &mut ((right)[(0_i64) as usize]);
     |         -----------   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ `

### ORACLE-52-MUTATE-DURING-ITERATION-REJECTED: ORACLE-52-MUTATE-DURING-ITERATION-REJECTED (✅ PASS)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 coverage: structural mutation conflicts with a live iteration borrow.
- **Expected Outcome:** REJECTED
- **Engine Modes:** checker: PASS
- **Clone Provenance:** `{}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `NOT_APPLICABLE`

### ORACLE-53-MAP-REMOVE-MISSING: ORACLE-53-MAP-REMOVE-MISSING (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 addendum: Map.remove(k) missing key raises CollectionError.MissingKey.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: UNEXPECTED_REJECT | fast_debug: CRASH | native_debug: EMIT_FAIL | native_opt: EMIT_FAIL
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Checker unexpectedly REJECTED valid program: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "check",
  "ok": false,
  "error": {"code": "TYPE_AMBIGUOUS_OPEN_RESULT", "severity": "error", "message": "unbound open result type for variable 'values'; heterogeneous or unresolved collection element type", "source_file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_53_map_remove_missing.moss", "line": 3, "column": 1, "span": {"start_line": 3, "start_column": 1, "end_line": 3, "end_column": 1}, "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tests/phase28/oracle_r7_005_1/fixtures/oracle_53_map_remove_missing.moss", "line": 3, "column": 1, "span": {"start_line": 3, "start_column": 1, "end_line": 3, "end_column": 1}}, "semantic_identity": "main@2", "symbol": null, "rule": null, "cause": null, "related": [], "guidance": null, "details": {"ownership_expected": null, "ownership_actual": null, "consume_line": null, "recursion_witness": null, "resolution": null}, "fixes": [], "legal_alternatives": []}
}`
  - `Fast Debug crashed (exit 1): moss:3: error: [TYPE_AMBIGUOUS_OPEN_RESULT] unbound open result type for variable 'values'; heterogeneous or unresolved collection element type`
  - `Native emission failed: moss:3: error: [TYPE_AMBIGUOUS_OPEN_RESULT] unbound open result type for variable 'values'; heterogeneous or unresolved collection element type`
  - `Native -O emission failed: moss:3: error: [TYPE_AMBIGUOUS_OPEN_RESULT] unbound open result type for variable 'values'; heterogeneous or unresolved collection element type`

### ORACLE-54-IMPORTED-MOSSI-EFFECTS: ORACLE-54-IMPORTED-MOSSI-EFFECTS (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 coverage: CollectionError raise summaries survive imported .mossi interfaces and can be recovered by the consumer.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | imported_mossi_effects: MISSING_EFFECT
- **Clone Provenance:** `{}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `NOT_APPLICABLE`
- **Observed Failures:**
  - `Imported .mossi omitted CollectionError.MissingKey raise effect: {
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "0.1.0",
  "command": "effects",
  "ok": true,
  "result": {"status": "resolved", "target": {"id": "entity-v1:function:provider__lookup", "kind": "function", "semantic_identity": "fn:provider__lookup@2", "durable_identity": "entity-v1:function:provider__lookup", "identity_version": "entity-v1", "construct_kind": "function", "source_identity": "fn:provider__lookup@2", "specialization_identity": null, "name": "provider__lookup", "enclosing_entity": null, "module_identity": "provider", "module": "provider", "export_visibility": "exported", "export_kind": "concrete", "source": {"file": "/home/owner/daji/code/moss/tmp/phase28-r7-010/tmp/phase28_oracle_r7_005_1/projects/ORACLE-54-IMPORTED-MOSSI-EFFECTS/provider/src/provider.moss", "line": 2, "column": 1, "span": {"start_line": 2, "start_column": 1, "end_line": 2, "end_column": 1}, "range_precision": "line"}, "type": "int", "specialization_fields": [], "provenance": ["fn:provider__lookup@2"], "implementation_hash": "953abba83d36fb12", "semantic_interface_hash": "3cd76dfdc88344f9", "aliases": ["fn:provider__lookup", "function:provider__lookup"]}, "ownership": [{"name": "values", "type": "map[string,int]", "effect": "READ"}], "observable_effects": {"local_capture_read": false, "local_mutation": false, "domain_read": false, "domain_write": false, "message": false, "external_io": false, "may_panic": true, "raise_set": ["enum:std__CollectionError:MissingKey"], "may_diverge": false, "unresolved": false}, "enclosing_callable_effects": null, "effect_summary": {"status": "available", "direct": null, "direct_status": "not_retained", "transitive": {"local_capture_read": false, "local_mutation": false, "domain_read": false, "domain_write": false, "message": false, "external_io": false, "may_panic": true, "raise_set": ["enum:std__CollectionError:MissingKey"], "may_diverge": false, "unresolved": false}, "transitive_scope": "target", "certainty": "resolved"}, "ownership_facts": {"status": "available", "parameters": [{"name": "values", "type": "map[string,int]", "effect": "READ"}], "value_boundary": null, "move_provenance": {"status": "not_available", "reason": "the checker does not retain per-use move provenance after validation"}}, "synchronization": {"status": "not_applicable", "reason": "target is not a domain operation"}, "synchronization_plan":{"graph_identity":"domain-graph:cbf29ce484222325","physical_lowering":"handler_2pl","domains":[]}, "synchronization_dump":"SynchronizationPlan domain-graph:cbf29ce484222325\n"}
}
`

### ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE: ORACLE-55-COLLECTION-ERROR-INFERRED-RAISE (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005: CollectionError.MissingKey appears in the inferred raise set of a helper that performs strict Map indexing.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | inferred_effects: RAISE_SET_MISMATCH | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 108, "line": 3439}, "start": {"column": 100, "line": 3439}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 108, "line": 3439}, "start": {"column": 100, "line": 3439}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `FAIL`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Inferred raise-set mismatch: expected ['enum:CollectionError:MissingKey'], got ['enum:std__CollectionError:MissingKey']; `
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ: ORACLE-56-PRIMITIVE-VECTOR-ELEMENT-READ (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** R7-009 primitive element contract: scalar indexing and Vector iteration copy primitive values without consuming the Vector owner; preserve nonprimitive element consume rejection.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 64, "line": 3421}, "start": {"column": 56, "line": 3421}}, {"end": {"column": 57, "line": 3424}, "start": {"column": 49, "line": 3424}}, {"end": {"column": 59, "line": 3437}, "start": {"column": 51, "line": 3437}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 64, "line": 3421}, "start": {"column": 56, "line": 3421}}, {"end": {"column": 57, "line": 3424}, "start": {"column": 49, "line": 3424}}, {"end": {"column": 59, "line": 3437}, "start": {"column": 51, "line": 3437}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 9}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=9, unauthorized=0, error=`

### ORACLE-57-MAP-GET-WRITE-THROUGH: ORACLE-57-MAP-GET-WRITE-THROUGH (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28-R7-005 frozen Language Addendum: one-argument Map.get(key) returns a borrowed V and writes through to the stored value.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3415}, "start": {"column": 79, "line": 3415}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 7}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=7, unauthorized=0, error=`

### ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION: ORACLE-58-SPECIALIZATION-IDENTITY-COLLISION (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** P28 EST/specialization ownership contract: the same generic callable instantiated at distinct concrete types must retain isolated per-specialization checked facts.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3428}, "start": {"column": 79, "line": 3428}}, {"end": {"column": 89, "line": 3457}, "start": {"column": 81, "line": 3457}}, {"end": {"column": 27, "line": 3492}, "start": {"column": 19, "line": 3492}}, {"end": {"column": 27, "line": 3507}, "start": {"column": 19, "line": 3507}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 10}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}, {"end": {"column": 87, "line": 3428}, "start": {"column": 79, "line": 3428}}, {"end": {"column": 89, "line": 3457}, "start": {"column": 81, "line": 3457}}, {"end": {"column": 27, "line": 3492}, "start": {"column": 19, "line": 3492}}, {"end": {"column": 27, "line": 3507}, "start": {"column": 19, "line": 3507}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 10}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=10, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=10, unauthorized=0, error=`

### ORACLE-59-SHADOWED-BINDING-IDENTITY: ORACLE-59-SHADOWED-BINDING-IDENTITY (❌ FAIL)
- **Category:** oracle
- **Spec Rule:** Moss binding identity contract: a block-local shadow of a sibling function name is distinct from the function binding; the function remains callable outside that local scope.
- **Expected Outcome:** ACCEPTED
- **Engine Modes:** checker: PASS | fast_debug: PASS | native_debug: PASS | native_opt: PASS
- **Clone Provenance:** `{"native_debug": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}, "native_optimized": {"authorized": 0, "occurrences": [{"end": {"column": 50, "line": 2014}, "start": {"column": 42, "line": 2014}}, {"end": {"column": 46, "line": 2241}, "start": {"column": 38, "line": 2241}}, {"end": {"column": 59, "line": 2357}, "start": {"column": 51, "line": 2357}}, {"end": {"column": 69, "line": 2907}, "start": {"column": 61, "line": 2907}}, {"end": {"column": 66, "line": 2949}, "start": {"column": 58, "line": 2949}}, {"end": {"column": 42, "line": 3112}, "start": {"column": 34, "line": 3112}}], "source_copy_spans": [], "status": "PROVENANCE_MISSING", "unauthorized": 0, "unclassified": 6}}`
- **Semantic Status:** `PASS`
- **Clone Provenance Gate:** `BLOCKED_MISSING_OR_INVALID`
- **Observed Failures:**
  - `Native debug clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`
  - `Native optimized clone provenance audit: status=PROVENANCE_MISSING, unclassified=6, unauthorized=0, error=`
