# Phase 27.4 frontend and parser reader inventory

Measured from the current `phase27-3-work` tree with the widened lint's API
catalogue. “Locations” counts distinct source lines; the compiler frontend has
45 locations and 48 invocations because three lines contain multiple calls.
The parser has 23 locations and 23 invocations. These consumers are outside
Phase 27.3's checker/emitter scope.

## `src/moss.cpp` compiler frontend: 45 locations

| Pattern | Locations (invocations) | Functions and text interpreted |
|---|---:|---|
| Call shape and arguments | 4 (4) `parse_simple_call`; 4 (4) `parse_member_call` | `typed_empty_vector_constructor`, `parse_executor_chain`, `parse_functional_pipeline`, `normalize_pipeline`, `parse_fileio_open`, `parse_fileio_chunks_source`, `parse_enum_case_expression`; extracts callee/receiver/member and argument slices. |
| Top-level comma splitting | 9 (9) `split_top_level` | `canonical_type_name`, `contains_internal_placeholder`, `parse_simple_call`, `parse_member_call`, `parse_vector_literal`, `resolve_phase21_owned_type` (2 sites), `run_project_benchmarks`, `replace_call_argument_on_line`; separates type arguments, call/list elements, and serialized benchmark/edit arguments. |
| Identifier classification | 22 (25) `plain_identifier` | `parse_enum_case_expression` (2), `parse_bound_method_callable`, `parse_named_argument`, `project_imported_h3_checked_bodies` (8), `semantic_target_facts`, `rewrite_module_program` (5), `run_semantic_edit`, `scan_body` (3); validates enum/case/method/label names, imported record fields, source binding names, and edit operands. |
| Outer grouping | 1 (1) `strip_redundant_outer_parentheses` | `parse_vector_literal`; removes enclosing expression parentheses before recognizing a vector literal. |
| Delimiter position | 4 (4) `top_level_assignment`; 1 (1) `top_level_colon` | `parse_named_argument`, `completion_pipeline_expression`, `rewrite_module_program`, `scan_body`; finds top-level assignment/field delimiters in expression or retained statement text. |

The `plain_identifier` row includes frontend metadata, imported checked-body
validation, semantic target collection, and source-line scanners as well as
expression-related classifiers; those functions are listed separately so
27.4 can decide their ownership.

## `src/parser.cpp`: 23 locations

| Pattern | Locations | Functions and text interpreted |
|---|---:|---|
| Identifier classification | 6 | `Parser::parse` (module/import names), `parse_domain_routes` (route name), `parse_state_field` (field name), `parse_handler` (recovery binding), `parse_stmt` (loop binding); validates identifier spellings. |
| Call shape | 2 `parse_simple_call`; 2 `parse_member_call` | `Parser::parse` (main expression body), `parse_message_call`, `parse_stmt`; identifies calls and projects receiver/handler/arguments. |
| Comma-separated lists | 5 | `parse_params`, `parse_domain_routes`, `parse_handler`, `parse_stmt` (echo arguments and enum payload bindings); separates list elements and pattern payload bindings. |
| Assignment and colon delimiters | 3 `top_level_assignment`; 2 `top_level_colon` | `parse_state_field`, `parse_stmt`, `parse_domain_routes`; partitions declaration/statement text. |
| Trimmed syntax comparisons | 3 | `parse_object` compares a declaration suffix with `object`; `parse_domain_routes` recognizes its closing `)` on two lines. |

## Pattern forms present in `examples/**/*.moss` and `tests/**/*.moss`

Counts below are source-line occurrences, using regular `.moss` files. The six
checker reader sites split recovery selectors; they are not valid lint
exemptions and remain counted until a tree representation is approved.

| Form | Count | Example |
|---|---:|---|
| Match enum tag | 15 | `case Empty:` in `tests/phase15_15_enum_message.moss` |
| Match enum case with payload bindings | 31 | `case Pair(code, value):` in `tests/phase15_15_enum_message.moss` |
| Recovery qualified enum case | 83 | `recover Error.Bad:` in `tests/tooling/fixtures/phase21_field_projection_root.moss` |
| Recovery enum case with payload binding | 4 | `recover WorkError.Failed(code):` in `tests/tooling/fixtures/phase21_recovery_native.moss` |
| Grouped recovery alternatives | 6 lines (12 alternatives) | `recover Failure.A, Failure.B:` in `tests/tooling/fixtures/phase21_addendum_grouped_b.moss` |
| Recovery catch-all | 76 | `recover:` in `tests/phase21_fileio_root_cleanup.moss` |
| Recovery binding | 5 | `recover err:` in `tests/tooling/fixtures/phase21_peer_re_raise.moss` |
| Match literal or wildcard | 0 accepted-source occurrences | The parser explicitly rejects wildcard enum patterns; no literal match pattern occurs in the source corpus. |

The proposed EST Pattern family covers the accepted forms observed here if it
represents bare recovery catch-all and binding selectors alongside enum tags,
payload bindings, and alternatives. It is a new node family, so implementing it
requires the requested L7 ruling under the round's escalation rule.
