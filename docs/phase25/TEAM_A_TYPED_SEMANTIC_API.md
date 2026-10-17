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

## Current implementation and remaining work

At consolidated Team A implementation SHA `56a8b62`, `src/typed_semantic_api.hpp`
projects `typed-semantic-v1` directly from the checked `Program`. The compiler
retains exact module and nominal IDs, bound nominal field annotations, closed
`TypeRef` and instance keys, open summary variables and typed requirements,
`specialization-v2` inputs, checked access/effects, callable dependencies,
source requirement chains, visibility and optional parameter leaf effects.
`TypeSolver` is used on compiler paths for fresh open calls, local monotypes,
collection/field inference, and result closure. The projection rejects open
concrete specializations and invalid built-in arity. `moss inspect` exposes a
read-only JSON view for compiler-path tests.

An open summary is published only when its type and effect facts are complete.
`TypedOpenSummaryGapV1` reports `missing_typed_facts`, `unresolved_effects`, or
`missing_parameter_access` as structured status. An imported member requirement
can remain in `source_member_requirements` with its provider provenance when
the enclosing open summary lacks resolved observable effects; consumers must
not treat that source requirement as a callable, complete summary. A checked
generic body availability bit tells Team B whether versioned body IR is present;
it is not a body hash. Source-free `bytes.from_range(Range)` now uses the
solver's closed `Vector[Int]` body result rather than the stale legacy
`_generic:data` display relation.

| Remaining item | Owner and boundary |
| --- | --- |
| Complete TypeRef authority for checker, ownership and effects | Team A A2. Some legacy `TypeEnv` and compatibility paths still make semantic decisions from type spellings. The final Architect review must audit actual compiler paths. |
| Canonical `body_semantic_hash` and expected dependency hashes | Team A must expose a canonical digest of checked semantic body IR and exact dependency facts. Team B maps them to ABI-v10 SHA-256 records and validates them. The legacy `.mossi` `semantic_ir_body_hash` is a 64-bit hash of header and statement text and does not meet the frozen semantic-record contract. No ABI consumer may substitute it. |
| Ordered source-free nonzero member argument records | Team B owns the `.mossi` serializer/importer. It currently writes `generic_constraint` with kind, subject, detail and result only, losing `Constraint.arity`, ordered `arguments` and `result_expectations`. Team A retains those typed argument/access facts when supplied. `append_one(values): values.push(1)` is the required reduced source-free regression. |
| Imported unresolved observable effects | Team B must transport complete provider effect facts; Team A publishes the full typed summary only after the checker has resolved them. The source requirement table preserves provenance while the summary remains unavailable. |
| Map-key proof and final runtime/ABI cutover | Team S supplies admission/target policy; Team B and Team I own binary records, linker/validator, and joint integration. G2 Gate 10 may remain pending until that integration. |

The Team A compiler-path suite covers typed nominal IDs, independent open calls,
one-binding monotypes, exact specialization keys, structural requirements,
Fast Debug/Native parity, and source-free Range return closure. The exact
implementation SHA still requires consolidated acceptance and independent
Architect GO. No full ABI-v10 production cutover is claimed here.
