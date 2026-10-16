# Phase 25 Team A typed semantic interface for Teams B and I

**Status:** Team A implementation contract. This document describes the compiler-internal handoff required by the frozen [Type IR contract](TYPE_IR_CONTRACT.md), [ABI-v10 design](ABI_V10_DESIGN.md), and [Team S/Team G agreement](TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md). It does not claim that every record below is available at the baseline `5b29352`.

**Version:** `typed-semantic-v1`. This version names the logical C++ records and invariants; ABI-v10 serialization and its physical encoding belong to Team B. The interface is compiler-internal and may add fields without changing Moss source syntax. Consumers must reject an unsupported major version, an open value passed as closed, or an absent required field. They must never recover semantic facts from diagnostics or formatted type names.

## Ownership and use

Team A owns the resolved semantic records, inference, nominal identity, requirements, specialization closure, checked effects, and the projection below. Team S owns `admit_member` admission policy and exact target selection; Team A stores and solves its results. Team B owns ABI-v10 encoding, provider binding, runtime/linker work, and validation of the selected binary contract. Team I connects the interfaces and performs joint integration. `ModuleIdentity` identifies the declaration; `ProviderBinaryContract` binds the selected artifact. Neither substitutes for the other.

The handoff occurs **after name resolution and TypeSolver closure**, before Fast Debug preparation, native lowering, or ABI emission. An open generic declaration may expose an `OpenCallableSummary`; a reachable concrete body may expose only closed value types and resolved call targets. `TYPE_AMBIGUOUS_OPEN_RESULT` is the fail-closed result for an unresolved reachable value.

## Logical records

The compiler-facing `TypedSemanticModuleV1` contains the following typed tables, keyed by interned IDs. References within a table are IDs or `TypeRef`, never display strings.

| Record | Required semantic fields | Invariant and consumer use |
| --- | --- | --- |
| `ModuleIdentity` | `module-v1:<package>@<version>::<module>` | Exact defining package/version/module, independent of path and Rust crate name. |
| `NominalTemplateV1` | `template_id`, defining module ID, declaration name, ordered field slots with declared `TypeRef` or variable ordinal, method IDs, visibility, provenance | `template_id = nominal-template-v1:<ModuleIdentity>:type:<declaration>`. Source order determines slot order. Same layout in another declaration has another ID. |
| `ClosedTypeV1` | `TypeRef`, `type_key`, optional nominal `template_id`/`instance_key`, ordered resolved field types | `TypeRef` uses the frozen algebra. It contains no `InferenceVar`; keys use domain-separated, length-prefixed SHA-256. Display text is a derived field only. |
| `ResolvedNominalInstanceV1` | template ID, ordered substitution, ordered resolved fields, nominal instance key, provider binary binding | The instance key is derived from the template ID and **ordered resolved field type keys**. Constructor argument order has no effect. |
| `OpenCallableSummaryV1` | summary/callable IDs, dense bound-variable ordinals, receiver/parameter/result `OpenTypeExpr`, requirements, access/effects, dependencies, semantic hash, provenance | A call allocates fresh inference IDs for every bound ordinal. A stored local binding keeps one monotype. Source-backed and source-free calls use the same summary. |
| `MemberDispatchRequirementV1` | requirement ID, receiver type, member name, ordered argument types, result type, receiver access, ordered argument access, origin, parent requirement ID, admitted family, selected callable ID when resolved | Team S supplies admission/target selection; Team A unifies contributions, retains the provenance chain, and rejects unresolved requirements at closure. |
| `SpecializationV1` | `specialization-v2` key, exact callable ID, optional receiver type/key, ordered argument type keys, callable argument IDs, open-result bindings, result type, selected call edges, checked access/effects, dependencies, provenance | One key governs registration, deduplication, call edges, module projection, semantic queries, and native references. Effects are checked outputs, not key inputs. Rechecking a key with incompatible outputs fails. |
| `ObservableEffectsV1` | receiver/parameter access, leaf effects, raise/failure/divergence and other observable effects | One checked fact is consumed by ownership, ABI and native lowering. Imported summaries cannot be weakened. |
| `SemanticDependencyV1` | exact referenced callable/summary/template ID, expected semantic hash, source/private visibility, provenance parent | Private dependencies permit specialization without making a private name importable. Source-free chains retain the defining provider and each call origin. |
| `SourceProvenanceV1` | physical source location when available, defining module ID, enclosing callable ID, reason/parent ID | Locations explain an error; they do not affect type, instance, or specialization identity. |

`OpenTypeExpr` uses summary-local `BoundVar(ordinal)`; `ClosedTypeV1` forbids it. The `TypeRef` representation also distinguishes primitive, built-in application, nominal instance, enum, domain, trait constraint, inference variable, and unit. Built-in arity and recursive closure are validated before a `ClosedTypeV1` is returned: `Map[Int]` and `Vector[]` cannot be exported as concrete types.

## Required API operations

The internal consumer can enumerate `TypedSemanticModuleV1` by ID; query `closed_type(TypeRef)` and `nominal_instance(template_id, ordered_resolved_fields)`; instantiate an open summary with fresh caller variables; resolve a requirement chain; and obtain a specialization by its complete key. Each operation returns typed data or a structured diagnostic. There is no parsing of `canonical_type_name`, diagnostic prose, `module__Name`, or generated Rust symbols on this path.

Team B maps these records into ABI-v10 `Module`, `NominalTemplate`, `TypeExpr`, `ConcreteInstance`, `OpenCallableSummary`, `Specialization`, requirements, and dependency/provenance records. Team B computes and validates the `binary_contract_hash`, `semantic_contract_hash`, `interface_hash`, provider digest and selected `.rlib` marker. The exported module ID is checked against its bound provider binary identity, while preserving their separate purposes. Team I verifies two independent consumers of one provider obtain the same declaration and instance keys.

## Baseline availability and remaining Team A work

At `5b29352`, `src/type_ir.hpp` has `TypeRef`, a standalone `TypeSolver`, and partial nominal/requirement records. `src/ast.hpp` stores some structured specialization type references. The checker still uses `unordered_map<string,string>` for its type environment (`src/moss.cpp:2219`) and never instantiates `TypeSolver`; nominal instances and many joins/compatibility checks remain string-backed. The current requirement has one access mode and lacks a member name, per-argument modes, parent ID, and selected callable ID. The existing `specialization-v2` helper does not yet encode the frozen receiver, callable-argument, and open-result fields. ABI-v10 staging is not evidence that this typed compiler handoff is complete.

Team A must finish those compiler paths and prove this API is generated from the checker-owned TypeRef/TypeSolver state. Team B can use this shape to prepare its consumer without inventing a second inference pass. Team I should gate integration on compiler-path tests for distinct nominal IDs, per-call freshening and one-binding monotypes, source-free summary identity, complete requirement/access records, exact specialization keys, and closed facts shared by Fast Debug and native lowering.
