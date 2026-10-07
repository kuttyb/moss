# Phase 20 review probes

The original probe programs came from the independent review of
`phase-20-integration` at `2f62205`. The reviewer-completeness variants added
after `e009460` were written from the owner's reopened review findings; no
updated reviewer probe archive was supplied. Existing passing probe sources and
expectations were preserved. `manifest.json` is the current suite inventory;
the tables below retain the original review's historical observations.

```sh
make
python3 tests/tooling/phase20_review/run_review_probes.py ./moss            # gate: guards + bugs
python3 tests/tooling/phase20_review/run_review_probes.py ./moss --strict   # also pre-existing
python3 tests/tooling/phase20_review/run_review_probes.py ./moss --only domain_field
```

Each probe in `manifest.json` is checked with `moss check --json`. Accepted
probes that carry a run expectation are lowered to Rust, compiled with
`rustc -O -D warnings`, and run from the repository root against fresh data
that the runner writes to `tmp/phase20-review/`. Probe paths are relative to
the repository root. Exit status is 1 when any guard or bug probe fails.
At `2f62205` the full run takes about 25 seconds.

| Category | Meaning | Gates? |
| --- | --- | --- |
| `guard` | Fixed by the corrective pass; keeps it fixed | yes |
| `bug` | Crash, invalid generated Rust, wrong answer, or contradiction of the checked-in spec | yes |
| `pre-existing` | Reproduces on `d3cdc52` (before Phase 20); the Phase 20 surface makes it easy to hit | with `--strict` |
| `design` | Outcome depends on an open design decision; prints what the compiler does and what the Oct 4 proposal implied | never |

The new variants exercise lexical chunk bounds, RangeBatch iteration, the
Range operator matrix, pure constructor effects, and typed/untyped loop
ownership. `design_range_as_pipeline_source` remains an informational design
record.

The final reopened findings add six negative bounds probes: RHS mutations
in while/for/let, compound expressions and nested loops, and an indexed
request-field store. The read-only while/for guard retains bounds and runs
natively. The general
compiler fixture `tests/indexed_field_assignment.moss` separately checks
vector, nested field/index, and Map field stores, including WRITE helpers.
The Agent D suite checks a deterministic one-worker publication window and
a generated-code mutation that moves the join into the publication loop;
Agent C's separate concurrent Branch tests remain unchanged.

`bug_range_eq_string_binary` retains its historical expectation as a passing
review probe. The `Range` versus `String` equality rule itself is a provisional
owner decision, recorded separately in §29 of the Phase 20 spec and in
`.codex/CURRENT_STATUS.md`; passing this probe does not settle that decision.

Test data, rewritten before every probe: `input.txt` holds
`hello world this is a test file with some words` and a newline (48 bytes,
9 spaces); `input2.txt` holds `second file` and a newline; `binary.dat` is
the four bytes `FF FE 00 41`; `store.dat` starts empty. Outputs go to
`tmp/phase20-review/out/`.


## Guards — fixed by the corrective pass (21, all PASS)

| Probe | Spec | Checks | Observed at 2f62205 |
| --- | --- | --- | --- |
| `guard_domain_field_lifecycle` | §9.2 §11 | Domain-field open/write/sync, two overlapping Verify roots, close. | PASS |
| `guard_pipeline_in_main` | §13 | Eligible pipeline outside any Root (was a runtime panic). | PASS |
| `guard_pipeline_in_main_active` | §13 §5 | Same with an active executor. | PASS |
| `guard_pipeline_helper_from_main` | §13 | Same inside a helper called from main. | PASS |
| `guard_typed_pipeline_handler` | §13 front example | Typed pipeline in a Root admitted by top-level message. | PASS |
| `guard_untyped_map_index_only` | §13 | Untyped map callable that only indexes (the fixed shape). | PASS |
| `guard_range_byte_iteration` | Range API | `for b in chunk` inside an eligible map. | PASS |
| `guard_range_index_inline` | Range API | Range byte indexing inside an expression. | PASS |
| `guard_range_binary_echo` | Range API | Binary-safe echo of invalid UTF-8 and NUL. | PASS |
| `guard_range_copy` | §12.4 §12.7 | Byte-exact write of a direct read result. | PASS |
| `guard_nested_message_blocking_warning` | §22 | Blocking FileIO reached through a nested message is warned. | PASS |
| `guard_executor_invoke_loop` | §23 | Independent roots with root-local FileIO. | PASS |
| `guard_executor_config` | §3 | `threads`/`max_threads`/`queue_capacity`/`affinity`/`priority`. | PASS |
| `guard_sequential_executors` | §5.3 §5.4 | Two executors, never simultaneously active. | PASS |
| `guard_reject_fileio_alias` | §9 | No alias or move of a FileIO. | PASS |
| `guard_reject_fileio_return` | §9.1 | No return of a FileIO. | PASS |
| `guard_reject_fileio_in_vector` | §9.1 | No FileIO in collections. | PASS |
| `guard_reject_range_in_record` | §12.6 | No Range in records. | PASS |
| `guard_reject_range_return` | §12.6 | No Range escape by return. | PASS |
| `guard_reject_field_assign_local_owner` | §9.2 | A domain field cannot receive a local owner. | PASS |
| `guard_reject_map_returns_range` | §12.6 §13 | A map stage cannot return the chunk. | PASS |

## Bugs — still failing (14)

| Probe | Spec | Checks | Observed at 2f62205 |
| --- | --- | --- | --- |
| `bug_domain_field_chunk_pipeline` | §13.1 | Pipeline directly on a domain-field FileIO: generated `*state.file.read(...)` fails rustc. | FAIL: rustc failed: error[E0614]: type `moss_fileio::Range` cannot be dereferenced |
| `bug_domain_field_chunk_pipeline_executor` | §13.1 | Same with an active executor. | FAIL: rustc failed: error[E0614]: type `moss_fileio::Range` cannot be dereferenced |
| `bug_untyped_map_length_expr` | §13 front example | `fn summarize(chunk): chunk.length()` trips the checker assertion. | FAIL: checker crashed: assertion `!node.effects.unresolved && !node.callable_identity.empty()` at src/moss.cpp:7867 |
| `bug_untyped_map_length_return` | §13 front example | Same with an explicit return. | FAIL: checker crashed: assertion `!node.effects.unresolved && !node.callable_identity.empty()` at src/moss.cpp:7867 |
| `bug_untyped_map_delegates_spec_shape` | front example | `summarize(chunk)` delegating to a typed helper: TYPE_MISMATCH `_generic:chunk` vs `Range`. | FAIL: rejected TYPE_MISMATCH: argument 1 to function 'word_summary' has type '_generic:chunk', expected 'Range' |
| `bug_map_for_range_index_loop` | §13 | Typed map with an indexed for-range byte loop trips the assertion. | FAIL: checker crashed: assertion `!node.effects.unresolved && !node.callable_identity.empty()` at src/moss.cpp:7867 |
| `bug_write_batch_entry` | checked-in §12.7 | Batch-entry Range rejected as unbounded. | FAIL: rejected FILEIO_UNBOUNDED_REQUEST: FileIO write payload must have a statically provable finite bound |
| `bug_write_chunk_copy` | checked-in §12.7 | Chunked copy rejected as unbounded. | FAIL: rejected FILEIO_UNBOUNDED_REQUEST: FileIO write payload must have a statically provable finite bound |
| `bug_range_eq_string_binary` | Range API | Binary Range `== ""` is rejected as TYPE_MISMATCH (cross-type comparison prohibited). | PASS (rejected TYPE_MISMATCH) |
| `bug_range_eq_range` | Range API | Equal byte views compare unequal (derived PartialEq on buffer/offset). | FAIL: stdout 'false'; expected 'true' |
| `bug_batch_index_inline_main` | §12.5 §12.6 | `batch[i].length()` emits vector indexing; rustc fails. | FAIL: rustc failed: error[E0608]: cannot index into a value of type `RangeBatch` |
| `bug_batch_index_inline_handler` | §12.5 §12.6 | Same inside a handler. | FAIL: rustc failed: error[E0608]: cannot index into a value of type `RangeBatch` |
| `bug_slice_length_chained` | Range API | `slice(...).length()` emits `.length()` on the runtime Range; rustc fails. | FAIL: rustc failed: error[E0599]: no method named `length` found for struct `moss_fileio::Range` in the current scope |
| `bug_oob_index_no_message` | fail-closed | OOB index aborts (fixed) but prints nothing. | FAIL: no diagnostic on stderr |

## Pre-existing — reproduce before Phase 20 on d3cdc52 (2)

| Probe | Spec | Checks | Observed at 2f62205 |
| --- | --- | --- | --- |
| `pre_map_for_range_vector` | functional pipelines | Any map callable with for-range trips the assertion; reproduces on d3cdc52. | FAIL: checker crashed: assertion `!node.effects.unresolved && !node.callable_identity.empty()` at src/moss.cpp:7867 |
| `pre_untyped_merge_inference` | front example | Untyped reduce combiner `fn merge(a, b)` never infers. | FAIL: rejected TYPE_INFERENCE_FAILED: cannot infer type for parameter 'a' in function 'merge' |

## Design calls — reported only, never fail (8)

| Probe | Spec | Checks | Observed at 2f62205 |
| --- | --- | --- | --- |
| `design_read_size_param` | original §11 / §12.7 | Handler-parameter read size; checked-in §12.7 narrowed to constants and the example rewritten. Proposal: accept. | rejected FILEIO_UNBOUNDED_REQUEST: FileIO read size must have a statically provable finite bound |
| `design_read_size_clamp` | original §12.7 | Compiler-proven clamp; now deferred. Proposal: accept, prints 40. | rejected FILEIO_UNBOUNDED_REQUEST: FileIO read size must have a statically provable finite bound |
| `design_write_string_param` | §12.4 / §12.7 | Bounded String parameter (listed open by the corrective pass). Proposal: accept. | rejected FILEIO_UNBOUNDED_REQUEST: FileIO write payload must have a statically provable finite bound |
| `design_write_range_param` | §12.4 / §12.7 | Range parameter has no static source bound in the callee. Proposal: not specified (no static bound in the callee). | rejected FILEIO_UNBOUNDED_REQUEST: FileIO write payload must have a statically provable finite bound |
| `design_write_range_slice` | Range API | Does a slice inherit its source read's bound? Proposal: not specified (slice is not in the spec). | rejected FILEIO_UNBOUNDED_REQUEST: FileIO write payload must have a statically provable finite bound |
| `design_slice_oob_clamps` | Range API | `slice()` clamps silently while `r[i]` aborts. Proposal: not specified (slice is not in the spec). | accepted; exit 0, stdout '0 2' |
| `design_range_as_pipeline_source` | Moss idiom | Range is not a pipeline source; bytes are loop-only. Proposal: accept, prints 9. | rejected UNKNOWN_SYMBOL_OR_TYPE: unknown local function 'count' |
| `design_spec_multiline_layout` | §3 / front example | The proposal's own multi-line layout does not parse. Proposal: accept, prints 1. | rejected MOSS_COMPILE_ERROR: assignment requires a target and an expression |
