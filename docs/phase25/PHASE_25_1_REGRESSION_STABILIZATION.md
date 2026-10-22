# Phase 25.1 regression stabilization ledger

**Branch:** `phase25-1-work`
**Starting checkpoint:** `68a8ce39b06967d827b3801838ba4c92ee74828d`
**Main comparison:** `dd45e30722bcc2d62d4bb46ba184d426cbaea066`

The initial source-built run on the starting branch reported **94/132 pass and
38 fail**. A source-built run of the isolated main checkout completed with
**132/132 pass**, and the repaired branch completed with **132/132 pass**. The
previously supplied `main: 131/132` result was not reproduced in this run, so
there is no current main failure signature to report; the historical/current
result discrepancy is recorded explicitly rather than assigning a guessed
failure.

| Root cause and evidence | Affected jobs / reproducer | Main behavior | Suspected introduction | Correction and status |
|---|---|---|---|---|
| Field traversal used the last textual dot even when it was inside a quoted string or nested constructor. | Initial failures included `check_phase21_fileio_close_full`, `check_phase20_cross_crate_range_abi`, `check_generic_specialization`, `sdk_adv_data_oracle_debug`, `sdk_adv_data_source_free`, and SDK string jobs. Reduced cases were `echo "tmp/phase21-fileio/input.dat"`, an escaped punctuation string, and `Item(name: fields[i].name)`. | All reduced cases passed on source-built main. | `0ec85848` changed the field receiver walk to `rfind('.')`; the later expression receiver traversal at `11fbf47e` exposed more contexts. | Scan only top-level dots and return early for complete string/char literals. The four-case reproducer harness passes, as do the full repaired SDK and specialization jobs. |
| Static scalar type names were recursively checked as value receivers before scalar-protocol dispatch. | `check_phase25_g3_ordering`, `check_phase25_l7_repairs`, `check_phase25_s3_key_protocol`; minimized `Int.hash(1)` / `Float.isequal(...)` probes. | Main passed these protocol calls. | `11fbf47e` added ordinary receiver traversal, excluding only `FileIO`. | Skip recognized static scalar type names while preserving protocol dispatch. All three jobs and the full 132-job suite pass. |
| A domain handle used as a direct handler-call receiver was checked as an ordinary value before handler resolution. | `check_teaching_diagnostics`, fixture `benchmarks/agent/tasks/AB021_repair_cross_domain_call/starter/main.moss`; expected `DOMAIN_HANDLER_REQUIRES_MESSAGE`, observed `MOSS_COMPILE_ERROR` on the first repaired run. | Main reports the handler-specific teaching diagnostic. | Same receiver traversal introduced by `11fbf47e`. | Skip ordinary expression checking when the receiver binding is a domain handle. Focused diagnostic test and final 132-job suite pass. |
| `assert(not 1)` reported a generic assertion configuration error instead of the unary operand's `TYPE_MISMATCH`. | `shard_negative` and `shard_swarm_cases`; minimal source is `assert(not 1)`. | Main reports `TYPE_MISMATCH: not operand must have type 'Bool'`. | Statement-call occurrence validation in the Phase 25.1 call traversal, around `783f47eae` and `11fbf47e` (suspected). | Check the assert operand once before applying the builtin signature check; defer the statement-form assert signature until operand checking completes. Negative and swarm shards pass. |
| The ABI adversarial fixture used summary version 3 as an “unknown version” even though v3 is supported. This was test-contract drift, not decoder fail-open evidence. | `shard_abi_v10_units`, case `unknown_private_version`. | Main still rejects v3 and its old fixture passes. | v3 support landed at `6d412d7e9`; fixture was not advanced. | Added a positive v3 control and moved the malformed mutation to version 4. The ABI shard and hostile M2b2 corpus pass. |
| Typed API projection treated body occurrence totality as a prerequisite for a valid generic scheme. Separately, a nullary open-result call such as `fresh(): return []` did not retain a caller-owned result TypeRef in `relay(): return fresh()`. | `check_phase25_typed_semantic_api`; `total(values)` also demonstrates a valid `Int` scheme and iterator requirement while its generic-For body is unavailable. | Main exposes the typed `total` and `relay` summaries. | `b1238b52d` coupled `open_summary.complete` to occurrence/dependency totality; nullary return substitution gap is in the R1 returned-call path around `869b6187d` (suspected). | Typed projection now uses checker-owned `scheme_complete` plus resolved effects and valid access facts. Nullary result variables are allocated from the selected callee scheme. `total` remains explicitly body-unavailable. Exported `relay → fresh` is kept outside the private-chain producer because the source-free importer does not support exported-call dependencies; it continues to fail closed as unavailable. Typed API, provenance and M2b2 tests pass. |
| The returned-call verifier matched semantic call edges only by target and source line; nested `helper(helper(1))` has two same-target edges on one line. | `check_phase25_checked_body_ir` / returned generic `inc` with `helper(helper(1))`. The retained outer, inner, and literal occurrences had `Int` TypeRefs, but common-body verification rejected the returned call. | Main accepts the checked-body probe. | Returned-call verification around `d8b85055e` (suspected). | Match the checker-owned occurrence ordinal to the semantic edge occurrence. The original checked-body suite and nested-call probe pass. |
| A private-chain producer could select an exported root whose body calls another exported callable, while the source-free importer only supports private callable dependencies for this path. Overlapping occurrence/private-chain emission could also duplicate records. | `check_phase25_m2b1_provenance.py` (`ambiguous_relay`) and the M2b2 empty-vector source-free consumer. | Main preserves the expected `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` rejection. | The private-chain producer around `e0569279` / `d8b85055e` was expanded during R1 (suspected). | Limit this producer to roots with an actual private callable dependency; reuse an overlapping record only when the canonical summary and body payloads match exactly. Final provenance and hostile/source-free corpus pass; ABI-v10 remains fail-closed. |

## Validation record

- Initial starting-branch run: 94 pass, 38 fail, 0 skip; output under
  `tmp/regression-current/full-initial/`.
- Isolated source-built main run at `dd45e30722bcc2d62d4bb46ba184d426cbaea066`:
  132 pass, 0 fail, 0 skip; 1244.51 seconds wall time; output under
  `tmp/regression-main/tmp/regression-main/full-main/`. The previously reported
  131/1 split was not reproduced.
- Final source-built stabilized-branch run: 132 pass, 0 fail, 0 skip; 976.83
  seconds wall time; output under `tmp/regression-current/full-final/`.
- Focused final checks: negative diagnostics; 40 generic-specialization probes;
  Phase 24 adversarial architecture; typed semantic API; checked-body IR and
  dependency closure; statement/call occurrence roles; H3 source-free chain;
  M2b1 provenance; full M2b2 source-free/hostile corpus including ABI-v9
  controls; ABI-v10 adversarial shard; and the minimized baseline expressions.
- The only intermediate failure after the first repair batch was
  `check_teaching_diagnostics` on the domain-handle receiver case. Its cause and
  correction are above; the final full suite and focused diagnostic check pass.
- Generic-For body lowering and shadowed branch returns remain deferred until
  stabilization is independently reviewed. This record does not claim overall
  R1 acceptance or authorize R2.
