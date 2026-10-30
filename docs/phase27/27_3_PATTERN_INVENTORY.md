# Phase 27.3 expression-reader pattern inventory

Baseline: `phase27-2-m1` (`04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4`), fetched `main` at `a71a1c52fd7f6e99ba33d9dde70e3531a033def6`. Counts are widened-lint call sites grouped by operation pattern, not source lines. The 454 checker and 124 emitter sites sum to 578. Optimizer-owned reads (9) remain 27.4 and are not included.

| Pattern read from text | Checker | Emitter | Total | Tree/fact replacement |
|---|---:|---:|---:|---|
| Pipeline normalization and detection (`normalize_pipeline`, `parse_functional_pipeline`) | 12 | 2 | 14 | `Pipeline` input/stage IDs; use the retained expression occurrence and `FunctionalNodeKind`/side facts. |
| Redundant grouping (`strip_expression_parens`, `strip_redundant_outer_parentheses`) | 20 | 9 | 29 | `Parenthesized.inner`; preserve grouping where it is semantically relevant and otherwise descend structurally. |
| Bare-name recognition (`plain_identifier`, `simple_identifier`) | 77 | 10 | 87 | `Identifier.name`, `Member.name`, or the syntactic callee node; use resolved-target facts for semantic identity. |
| Identifier occurrence scans (`expression_mentions_identifier`) | 0 | 7 | 7 | Recursive EST visitor over identifier/member/call/named-argument children, with binding/scope facts where the current check is scope-sensitive. |
| Trimmed source equality (`trim_compare`) | 75 | 23 | 98 | Compare node kind and child IDs/names, literal classification, or side-table target/type/effect facts. Diagnostic/source rendering may retain text but cannot decide semantics from it. |
| Direct call shape (`parse_simple_call`) | 54 | 17 | 71 | `Call.callee` and ordered `Call.arguments`; resolved target and specialization facts from the context-indexed side table. |
| Member call shape (`parse_member_call`) | 40 | 8 | 48 | `Call(Member(base, name), arguments)`; receiver is `Member.base`, method is `Member.name`, target is the resolved-target fact. |
| Bound method callable (`parse_bound_method_callable`) | 8 | 2 | 10 | `Member` callable node, its receiver/name, and resolved target fact. |
| Named argument shape (`parse_named_argument`) | 36 | 6 | 42 | `NamedArgument.name/value` children, preserving source order and duplicate/unsupported-form behavior. |
| Binary operator/operand extraction (`split_binary`, `generated_split_binary`) | 56 | 15 | 71 | `Binary.op/left/right`; unary cases use `Unary.op/operand`. Operator precedence comes from tree construction. |
| Index extraction (`parse_index`) | 25 | 10 | 35 | `Index.base/index` child IDs. |
| Vector literal extraction (`parse_vector_literal`) | 11 | 2 | 13 | `Collection` delimiter and ordered element IDs. |
| Delimited child-list splitting (`split_top_level`) | 23 | 9 | 32 | Existing child vectors for calls, constructors, messages, collections, tuples, named fields, and pipeline stages. Six observed match-arm alternative splits are pattern syntax rather than expression text; verify their exact sites and, if they remain outside this phase, exempt each with a one-line justification. |
| Enum case extraction (`parse_enum_case_expression`) | 7 | 2 | 9 | `Constructor` type/case member plus ordered argument IDs; named payloads use `NamedArgument`; case/type resolution uses checker facts. |
| FileIO call/chunk source extraction (`parse_fileio_open`, `parse_fileio_chunks_source`) | 5 | 2 | 7 | `Call`/`Member`/`NamedArgument` structure for open; `Pipeline` stages and child calls for chunks; FileIO mode and receiver semantics from node/fact data. |
| Executor-chain extraction (`parse_executor_chain`) | 2 | 0 | 2 | Nested `Call`/`Member` chain and `Message` target/handler nodes; resolve each callable through side facts. |
| Top-level assignment/colon scanning (`top_level_assignment`, `top_level_colon`) | 3 | 0 | 3 | `NamedArgument` or ordinary `Call` argument nodes; distinguish supported labels structurally rather than locating delimiters. |
| **Total** | **454** | **124** | **578** | |

## Inventory method

The counts come from the same `sites()` call-site scanner used by `tests/tooling/check_phase27_string_debt.py`. The script classifies by caller/API; descriptions and replacements above are the migration plan. Each pattern will be migrated and gated as a unit. Shadow parity will compare the EST result with the legacy helper result at the existing semantic consumer. Any case that cannot be represented by existing nodes/facts without changing their meaning is an escalation, not a silent tree/API extension.

The six candidate non-expression sites are the five checker calls splitting `MatchArm` alternatives in `src/semantic_checker.cpp` and one emitter call in `src/native_generator.cpp`. They remain provisional until individually confirmed and recorded in the exemptions file, if needed.
