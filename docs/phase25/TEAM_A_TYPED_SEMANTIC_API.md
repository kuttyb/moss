# Phase 25 — Team A / Team B Typed Semantic API Agreement

**Author:** Team B L6 Coordinator
**To:** Team A L6 Coordinator, Team I Coordinator
**Branch:** `phase25-g-abi-runtime`
**Authorities:**
- `docs/phase25/ABI_V10_DESIGN.md`
- `docs/phase25/TYPE_IR_CONTRACT.md`
- `docs/phase25/TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`
- `docs/phase25/TEAM_G_ABI_V10_STAGING.md`

---

## 1. Executive Summary & Ownership Boundaries

This agreement formalizes the semantic handoff boundary between **Team A** (Type System, Checker, Inference, Specialization) and **Team B** (ABI Codec, Package Artifacts, Shared Runtime, Linker Infrastructure).

### Ownership Division
- **Team A owns:**
  - The authoritative semantic type model (`TypeRef`, `TypeVarId`, `InferenceVar`).
  - Nominal template representation and field layout inference.
  - Requirement inference and requirement-parent DAG chains.
  - Open result inference and contextual type specialization.
  - Observable effect derivation (`READ`, `WRITE`, `CONSUME` access modes, effect bits, effect tags).
  - Private semantic dependency identification.
  - Source provenance extraction.
- **Team B owns:**
  - Typed ABI-v10 serialization and deserialization (`src/abi_v10_records.hpp`).
  - Source-free container framing, digest calculation, and complete 10-step fail-closed verification (`src/source_free_abi_v10.hpp`).
  - Shared compiler-owned runtime (`runtime/moss_runtime/`) with deterministic `MossMap<K, V>` and `MossSet<K>` storage.
  - Deterministic content-addressed artifact resolution, compatibility checks, and rustc linkage flag generation (`src/moss_runtime_linkage.hpp`).
  - Built-in `KeyProof` emission (`Int`, `Float`, `Char`, `String`, `Bool`) and proof coherence validation.
- **Team I owns:**
  - Joint multi-team integration and final production ABI-v10 default switching.
  - Wiring the compiler driver to pass Team A's semantic structures directly into Team B's ABI codec.

Team B does **not** implement a secondary type engine or semantic solver. Team B consumes Team A's canonical structures according to the frozen contracts.

---

## 2. Typed Semantic Interface Contract

### 2.1 WireType / TypeExpr Encoding
Team B's `WireType` mirrors the frozen normative algebra in `TYPE_IR_CONTRACT.md` §2 and `ABI_V10_DESIGN.md` §3.1:

```text
enum class WireTypeKind : uint8_t {
  Primitive = 1,  // int, float, bool, char, string
  Builtin   = 2,  // Vector[T], Map[K, V], Set[T]
  Nominal   = 3,  // user-defined records
  Enum      = 4,  // user-defined enum variants
  Domain    = 5,  // domain instances
  BoundVar  = 6,  // closed type parameter ordinal (0..variable_count-1)
  Unit      = 7   // unit / void
};
```

#### Rules & Constraints:
1. **No Session-Local IDs:** Unbound or session-local `TypeVarId`s must never appear in exported records. All generic parameters must be normalized to dense `BoundVar` ordinals indexed from `0` to `variable_count - 1`.
2. **Deterministic Nesting:** Type argument trees are serialized recursively up to a maximum depth of 64.
3. **Interned Identities:** `identity` must be valid UTF-8 and non-empty for non-unit types.

### 2.2 NominalTemplate Record
Emitted by Team A when a module exports or references a generic nominal type:

```cpp
struct NominalTemplate {
  std::string template_id;              // sha256 of canonical template structure
  std::string defining_module;          // e.g. "module-v1:pkg@1.0.0::Mod"
  std::string declaration_name;         // source declaration name
  std::string provenance_id;            // link to SourceProvenance record
  std::vector<FieldSlot> fields;        // source-ordered fields
  std::vector<std::string> method_template_ids; // lexicographically sorted method IDs
  uint8_t visibility;                   // 1 = Public, 2 = Internal, 3 = Private
};
```

### 2.3 OpenCallableSummary Record
Emitted by Team A for functions with generic parameters, open returns, or requirements:

```cpp
struct Summary {
  std::string callable_id;              // canonical qualified callable ID
  uint32_t variable_count;              // count of bound type variables (1..4096)
  std::vector<WireType> parameters;     // parameter types with BoundVar references
  std::vector<Access> parameter_access; // Read (1), Write (2), Consume (3)
  WireType result;                      // return type with BoundVar references
  std::vector<Requirement> requirements; // DAG of structural requirements
  std::vector<std::pair<std::string, std::string>> dependencies; // (dep_id, hash)
  uint32_t observable_effect_bits;      // bitset of effects
  std::vector<std::string> observable_effect_tags; // canonical sorted effect names
  std::string body_semantic_hash;       // deterministic hash of callable body
  std::string provenance_id;            // link to SourceProvenance record
};
```

#### Requirement DAG Structure:
- Each `Requirement` contains:
  - `receiver`: `WireType` (typically a `BoundVar`).
  - `operation`: method/member name.
  - `access`: `Read`, `Write`, or `Consume`.
  - `parent`: uint32 index of parent requirement in the summary list (`parent <= index` to ensure acyclic DAG).
  - `provenance_id`: non-empty reference to a valid `SourceProvenance` record.

### 2.4 ConcreteInstance Record
Emitted for concrete specializations of nominal templates:

```cpp
struct ConcreteInstance {
  std::string template_id;              // references NominalTemplate.template_id
  std::vector<WireType> substitution;   // concrete types for bound variables
  std::vector<WireType> resolved_fields; // concrete field types
  std::string provider_binary_id;       // provider rlib object identifier
  std::string provider_rust_type_symbol;// mangled Rust type symbol
  std::string representation_hash;      // layout and field hash
};
```

### 2.5 Specialization Record
Emitted for concrete specializations of generic functions:

```cpp
struct Specialization {
  std::string specialization_key;       // sha256(callable_id + substitution)
  std::string template_id;              // generic callable ID
  std::vector<WireType> substitution;   // concrete types for bound variables
  std::vector<WireType> parameter_types;// concrete parameter types
  std::vector<Access> argument_access;  // access modes
  WireType result;                      // concrete return type
  uint32_t observable_effect_bits;      // derived effect bits
  std::string materialization_owner;    // module owning the concrete compilation
  std::string native_symbol;            // exported native symbol name
  std::string semantic_hash;            // semantic hash of specialized body
};
```

### 2.6 MapKeyProof Record
Formal proof of admissibility for Map/Set keys (`ABI_V10_DESIGN.md` §3.5):

- **Built-in Keys:** Provided directly by Team B via `moss::abi_v10::make_builtin_key_proof("Int"|"Float"|"Char"|"String"|"Bool")`.
- **Nominal Keys:** To be emitted once Team S admits user records with `isequal` and `hash` methods meeting the purity, completeness, and effect requirements.

---

## 3. Referential Integrity and Fail-Closed Validation

Team B has implemented and verified exhaustive referential integrity checks:

1. **Unconditional Provenance Integrity:** Every `provenance_id` on templates, fields, methods, summaries, requirements, and proofs must resolve to an extant record in the interface's `provenance` section. An empty provenance section when records contain provenance IDs fails closed with `SOURCE_FREE_CONTRACT_INVALID`.
2. **Template-Method Binding:** Every method template referenced in `NominalTemplate.method_template_ids` must exist in either the public or private methods table.
3. **Template Identity:** Every `MethodTemplate` must reference a valid `template_id` present in `known_templates`.
4. **Private Dependency Closure:** Every dependency referenced by a public summary or method must be present in `private_semantic_dependencies` or transitively reachable.
5. **No Weakened Summaries:** When a consumer checks against an imported summary, parameter access may not be weakened (e.g. `Write` downgraded to `Read`), requirements may not be omitted, and effect tags may not be stripped.

---

## 4. Outstanding Dependencies & Team I Handoff

The following items are ready for Team I multi-team integration:

1. **Compiler Driver Integration:**
   - Call `moss::abi_v10::build_or_resolve_moss_runtime()` before native compilation.
   - Pass `moss::abi_v10::generate_compiler_runtime_flags()` to `rustc`.
   - Use `moss::abi_v10::validate_complete_interface()` during `.mossi` import.
2. **Team A Type Model Bridge:**
   - Map `moss::type_ir::TypeExpr` -> `moss::abi_v10::WireType`.
   - Populate `moss::abi_v10::Summary` from Team A's checked AST / Callable summaries.
3. **Team S Key Admittance Bridge:**
   - Activate custom nominal Map key verification once Team S merges `admit_member` protocol (Gate 10).
