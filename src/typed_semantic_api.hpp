#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ast.hpp"

namespace moss {

// Compiler-internal handoff. These records own checked TypeRefs and exact IDs;
// the ABI-v10 serializer is a separate Team B consumer.
struct TypedSourceProvenanceV1 {
  std::string source_file;
  int line{0};
  std::string defining_module_id;
  std::string enclosing_callable_id;
};

struct TypedNominalTemplateV1 {
  struct Field {
    std::string name;
    TypeRef declared_type;  // null for an inferred slot
    std::optional<uint32_t> variable_ordinal;
  };
  NominalDeclarationRef declaration;
  std::vector<Field> fields;
  bool exported{false};
  TypedSourceProvenanceV1 provenance;
};

struct TypedClosedTypeV1 {
  TypeRef type;
  std::string type_key;
  std::optional<std::string> nominal_template_id;
  std::optional<std::string> nominal_instance_key;
  std::vector<TypeRef> ordered_resolved_fields;
};

struct TypedNominalInstanceV1 {
  std::string template_id;
  TypeRef type;
  std::string instance_key;
  std::vector<TypeRef> ordered_resolved_fields;
};

struct TypedSemanticDependencyV1 {
  std::string callable_id;
  std::string specialization_key;
  TypedSourceProvenanceV1 provenance;
};

struct TypedSpecializationV1 {
  std::string key;
  SpecializationKey identity;
  std::string callable_id;
  bool exported{false};
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Effect> parameter_access;
  std::optional<StateLeafEffects> parameter_leaf_effects;
  ObservableEffects observable_effects;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<TypedSemanticDependencyV1> dependencies;
  TypedSourceProvenanceV1 provenance;
};

// A missing or incomplete checker summary is represented as unavailable,
// never guessed from the function's display result or source text.
struct TypedOpenCallableSummaryV1 {
  std::string callable_id;
  bool exported{false};
  // A hash is present only when the checker retained supported semantic nodes.
  bool checked_body_ir_available{false};
  std::optional<CheckedBodyV1> checked_body_v1;
  std::optional<std::string> checked_body_ir_v1_hash;
  uint32_t bound_variable_count{0};
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Requirement> requirements;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<std::string> semantic_dependencies;
  std::vector<AccessMode> parameter_access;
  // Present only when the checker retained exact parameter leaf paths.
  std::optional<StateLeafEffects> parameter_leaf_effects;
  ObservableEffects observable_effects;
  TypedSourceProvenanceV1 provenance;
};

enum class TypedOpenSummaryGapV1 {
  MissingTypedFacts,
  UnresolvedEffects,
  MissingParameterAccess
};

inline const char* typed_open_summary_gap_name(TypedOpenSummaryGapV1 gap) {
  switch (gap) {
    case TypedOpenSummaryGapV1::MissingTypedFacts: return "missing_typed_facts";
    case TypedOpenSummaryGapV1::UnresolvedEffects: return "unresolved_effects";
    case TypedOpenSummaryGapV1::MissingParameterAccess:
      return "missing_parameter_access";
  }
  throw std::invalid_argument("unknown typed open summary gap");
}

inline const char* typed_open_summary_gap_message(TypedOpenSummaryGapV1 gap) {
  switch (gap) {
    case TypedOpenSummaryGapV1::MissingTypedFacts:
      return "checker has no complete typed open summary";
    case TypedOpenSummaryGapV1::UnresolvedEffects:
      return "checker result or effect facts remain unresolved";
    case TypedOpenSummaryGapV1::MissingParameterAccess:
      return "checker parameter access facts remain unresolved";
  }
  throw std::invalid_argument("unknown typed open summary gap");
}

struct TypedSemanticModuleV1 {
  static constexpr const char* version = "typed-semantic-v1";
  std::map<std::string, std::shared_ptr<const std::string>> modules;
  std::map<std::string, TypedNominalTemplateV1> nominal_templates;
  std::map<std::string, TypedClosedTypeV1> closed_types;
  std::map<std::string, TypedNominalInstanceV1> nominal_instances;
  std::map<std::string, TypedOpenCallableSummaryV1> open_summaries;
  // Checked source requirements remain inspectable when unresolved callback
  // effects prevent publication of their enclosing open summary. These facts
  // are not an ABI-publishable callable summary.
  std::map<std::string, Requirement> source_requirements;
  // Source requirements remain addressable when a generic summary cannot
  // publish its unresolved observable effects yet. This table does not make
  // such a summary callable through the typed API.
  std::map<std::string, MemberDispatchRequirement> source_member_requirements;
  // Structured absence keeps ABI consumers from parsing diagnostic prose.
  std::map<std::string, TypedOpenSummaryGapV1> unavailable_open_summaries;
  std::map<std::string, TypedSpecializationV1> specializations;
  // Legacy imported generic skeletons may appear in Function::specializations
  // with open type terms. They cannot be projected as closed specializations.
  // Keep their exact IDs visible to consumers until A2 supplies summaries.
  std::map<std::string, std::string> unavailable_specializations;
  // False until A2 emits all reachable open summaries directly from solver
  // terms. ABI consumers must call require_complete_open_summaries().
  bool open_summaries_complete{true};

  void require_complete_open_summaries() const {
    if (!open_summaries_complete)
      throw std::invalid_argument("typed semantic open callable summaries unavailable");
  }

  void require_complete_body_hashes() const {
    for (const auto& entry : open_summaries)
      if (!entry.second.checked_body_v1)
        throw std::invalid_argument(
            "checked semantic body hash unavailable for " + entry.first);
  }

  void require_complete_specializations() const {
    if (!unavailable_specializations.empty())
      throw std::invalid_argument("typed semantic closed specialization facts unavailable");
  }

  const TypedClosedTypeV1& closed_type(TypeRef type) {
    if (!type || !type->is_closed())
      throw std::invalid_argument("typed semantic projection requires a closed TypeRef: " +
          (type ? type->to_canonical_string() : std::string("<null>")));
    const std::string key = type->stable_type_key();
    auto found = closed_types.find(key);
    if (found != closed_types.end()) return found->second;
    TypedClosedTypeV1 record;
    record.type = type;
    record.type_key = key;
    if (type->kind == TypeKind::NominalInstance) {
      record.nominal_template_id = type->declaration->id;
      record.nominal_instance_key = type->nominal_instance_key();
      record.ordered_resolved_fields = type->resolved_fields();
      nominal_instances.emplace(*record.nominal_instance_key,
          TypedNominalInstanceV1{*record.nominal_template_id, type,
              *record.nominal_instance_key, record.ordered_resolved_fields});
    }
    for (const auto& argument : type->type_args) closed_type(argument);
    for (const auto& field : type->field_substitutions) closed_type(field.second);
    return closed_types.emplace(key, std::move(record)).first->second;
  }

  const TypedSpecializationV1& specialization(const std::string& key) const {
    auto found = specializations.find(key);
    if (found == specializations.end())
      throw std::invalid_argument("unknown typed semantic specialization key");
    return found->second;
  }
};

TypedSemanticModuleV1 project_typed_semantics_v1(const Program& program);

}  // namespace moss
