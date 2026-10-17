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
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Effect> parameter_access;
  ObservableEffects observable_effects;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<TypedSemanticDependencyV1> dependencies;
  TypedSourceProvenanceV1 provenance;
};

// A missing or incomplete checker summary is represented as unavailable,
// never guessed from the function's display result or source text.
struct TypedOpenCallableSummaryV1 {
  std::string callable_id;
  uint32_t bound_variable_count{0};
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Requirement> requirements;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<std::string> semantic_dependencies;
  std::vector<AccessMode> parameter_access;
  ObservableEffects observable_effects;
  TypedSourceProvenanceV1 provenance;
};

struct TypedSemanticModuleV1 {
  static constexpr const char* version = "typed-semantic-v1";
  std::map<std::string, std::shared_ptr<const std::string>> modules;
  std::map<std::string, TypedNominalTemplateV1> nominal_templates;
  std::map<std::string, TypedClosedTypeV1> closed_types;
  std::map<std::string, TypedNominalInstanceV1> nominal_instances;
  std::map<std::string, TypedOpenCallableSummaryV1> open_summaries;
  // Source requirements remain addressable when a generic summary cannot
  // publish its unresolved observable effects yet. This table does not make
  // such a summary callable through the typed API.
  std::map<std::string, MemberDispatchRequirement> source_member_requirements;
  std::map<std::string, std::string> unavailable_open_summaries;
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

inline TypedSemanticModuleV1 project_typed_semantics_v1(const Program& program) {
  if (!program.type_context)
    throw std::invalid_argument("typed semantic projection requires a checked type context");
  TypeContext::Scope scope(program.type_context.get());
  TypedSemanticModuleV1 result;
  for (const auto& entry : program.module_identities)
    result.modules.emplace(entry.second, program.type_context->intern_module(entry.second));
  auto add_nominal = [&](const std::string& name, bool exported,
                         const std::string& source_file, int line) {
    auto declaration = program.type_context->find(name);
    if (!declaration)
      throw std::invalid_argument("checked nominal declaration has no interned identity");
    result.modules.emplace(declaration->module_id, declaration->interned_module_id);
    std::vector<TypedNominalTemplateV1::Field> fields;
    uint32_t variable_ordinal = 0;
    if (declaration->kind == DeclarationKind::Record) {
      const auto& annotations = program.type_context->field_annotations(declaration->id);
      if (annotations.size() != declaration->fields.size())
        throw std::invalid_argument("checked nominal field annotation count mismatch");
      for (size_t index = 0; index < annotations.size(); ++index) {
        const auto& slot = declaration->fields[index];
        if (slot.inferred == static_cast<bool>(annotations[index]))
          throw std::invalid_argument("checked nominal field annotation state mismatch");
        fields.push_back({slot.name, annotations[index],
            slot.inferred ? std::optional<uint32_t>(variable_ordinal++) : std::nullopt});
      }
    }
    result.nominal_templates.emplace(declaration->id, TypedNominalTemplateV1{
        declaration, std::move(fields), exported,
        {source_file, line, declaration->module_id, {}}});
  };
  for (const auto& object : program.objects)
    add_nominal(object.name, object.exported, object.source_file, object.line);
  for (const auto& enumeration : program.enums)
    add_nominal(enumeration.name, enumeration.exported,
                enumeration.source_file, enumeration.line);
  for (const auto& domain : program.domains)
    add_nominal(domain.name, domain.exported, domain.source_file, domain.line);
  for (const auto& function : program.functions) {
    if (function.semantic_callable_id.empty())
      throw std::invalid_argument("checked function has no semantic callable identity");
    if (function.open_summary && function.open_summary->complete)
      for (const auto& requirement : function.open_summary->member_requirements) {
        if (requirement.requirement_id.empty() ||
            requirement.origin.module_id.empty() ||
            requirement.origin.enclosing_callable != function.semantic_callable_id ||
            !result.source_member_requirements.emplace(
                requirement.requirement_id, requirement).second)
          throw std::invalid_argument(
              "checked source member requirement lacks exact provenance");
      }
    if (function.open_summary && function.open_summary->complete &&
        !function.open_summary->observable_effects.unresolved &&
        function.open_summary->parameter_access.size() ==
            function.params.size()) {
      const auto& checked = *function.open_summary;
      if (checked.callable_id != function.semantic_callable_id ||
          !checked.result_type ||
          checked.parameter_types.size() != function.params.size())
        throw std::invalid_argument("complete open summary lacks checked type or effect facts");
      auto valid_term = [&](const TypeRef& type, const auto& self) -> bool {
        if (!type) return false;
        if (type->kind == TypeKind::InferenceVar)
          return type->var_id.session_id == 0 &&
                 type->var_id.owner_id == 0 &&
                 type->var_id.ordinal < checked.bound_variable_count;
        for (const auto& arg : type->type_args)
          if (!self(arg, self)) return false;
        for (const auto& field : type->field_substitutions)
          if (!self(field.second, self)) return false;
        return (type->kind != TypeKind::NominalInstance &&
                type->kind != TypeKind::Enum &&
                type->kind != TypeKind::Domain) || type->declaration;
      };
      if (!valid_term(checked.result_type, valid_term) ||
          std::any_of(checked.parameter_types.begin(),
                      checked.parameter_types.end(),
                      [&](const TypeRef& type) {
                        return !valid_term(type, valid_term);
                      }))
        throw std::invalid_argument("complete open summary contains invalid bound variable");
      TypedOpenCallableSummaryV1 summary;
      summary.callable_id = checked.callable_id;
      summary.bound_variable_count = checked.bound_variable_count;
      summary.parameter_types = checked.parameter_types;
      summary.result_type = checked.result_type;
      summary.requirements = checked.requirements;
      summary.member_requirements = checked.member_requirements;
      summary.semantic_dependencies = checked.semantic_dependencies;
      summary.parameter_access = checked.parameter_access;
      summary.observable_effects = checked.observable_effects;
      summary.provenance = {checked.origin.source_file, checked.origin.line,
          checked.origin.module_id, checked.origin.enclosing_callable};
      if (summary.provenance.defining_module_id.empty() ||
          summary.provenance.enclosing_callable_id != summary.callable_id)
        throw std::invalid_argument("complete open summary lacks bound provenance");
      result.modules.emplace(summary.provenance.defining_module_id,
          program.type_context->intern_module(summary.provenance.defining_module_id));
      if (!result.open_summaries.emplace(summary.callable_id,
                                         std::move(summary)).second)
        throw std::invalid_argument("duplicate typed semantic open summary");
    } else if (function.generic || function.static_dispatch) {
      result.open_summaries_complete = false;
      result.unavailable_open_summaries.emplace(
          function.semantic_callable_id,
          function.open_summary && function.open_summary->complete
              ? "checker result or effect facts remain unresolved"
              : "checker has no complete typed open summary");
    }
    for (const auto& specialization : function.specializations) {
      const bool open_parameter = std::any_of(
          specialization.parameter_type_refs.begin(),
          specialization.parameter_type_refs.end(),
          [](const TypeRef& type) { return !type || !type->is_closed(); });
      if (open_parameter || (specialization.return_type_ref &&
                             !specialization.return_type_ref->is_closed())) {
        result.unavailable_specializations.emplace(
            specialization.stable_type_key.empty()
                ? function.semantic_callable_id : specialization.stable_type_key,
            "open imported or generic specialization requires a typed summary");
        continue;
      }
      if (specialization.stable_type_key.empty() ||
          specialization.semantic_key.stable_key() !=
              std::optional<std::string>(specialization.stable_type_key) ||
          specialization.parameter_type_refs.size() != function.params.size() ||
          !specialization.return_type_ref ||
          specialization.parameter_effects.size() != function.params.size())
        throw std::invalid_argument("checked specialization lacks typed result or access facts");
      TypedSpecializationV1 record;
      record.key = specialization.stable_type_key;
      record.identity = specialization.semantic_key;
      record.callable_id = function.semantic_callable_id;
      record.parameter_types = specialization.parameter_type_refs;
      record.result_type = specialization.return_type_ref;
      record.parameter_access = specialization.parameter_effects;
      record.observable_effects = specialization.observable_effects;
      record.member_requirements = specialization.member_requirements;
      const auto bound_module = program.module_identities.find(function.defining_module);
      std::string defining_module_id;
      if (bound_module != program.module_identities.end()) {
        defining_module_id = bound_module->second;
      } else if (function.defining_module == "std") {
        defining_module_id = module_identity("moss-std", "0.1.0", "std");
      } else if (!program.package_identities) {
        defining_module_id = module_identity("__standalone__", "0.0.0",
            function.defining_module.empty() ? "main" : function.defining_module);
      } else {
        throw std::invalid_argument("checked function has no bound defining module identity");
      }
      result.modules.emplace(defining_module_id,
          program.type_context->intern_module(defining_module_id));
      record.provenance = {function.source_file, function.line,
          defining_module_id, function.semantic_callable_id};
      for (const auto& type : record.parameter_types) result.closed_type(type);
      result.closed_type(record.result_type);
      for (const auto& requirement : record.member_requirements) {
        if (!member_requirement_is_closed(requirement))
          throw std::invalid_argument("checked specialization has open member requirement");
        result.closed_type(requirement.receiver);
        for (const auto& argument : requirement.argument_types)
          result.closed_type(argument);
        if (requirement.result) result.closed_type(*requirement.result);
      }
      for (const auto& dependency : specialization.dependencies) {
        auto target = std::find_if(program.functions.begin(), program.functions.end(),
            [&](const Function& candidate) { return candidate.name == dependency.function_name; });
        if (target == program.functions.end() || target->semantic_callable_id.empty() ||
            dependency.specialization_key.empty())
          throw std::invalid_argument("checked specialization has unresolved dependency");
        record.dependencies.push_back({target->semantic_callable_id,
            dependency.specialization_key, record.provenance});
      }
      if (!result.specializations.emplace(record.key, std::move(record)).second)
        throw std::invalid_argument("duplicate typed semantic specialization key");
    }
  }
  return result;
}

}  // namespace moss
