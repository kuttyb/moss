#include "typed_semantic_api.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace moss {

TypedSemanticModuleV1 project_typed_semantics_v1(const Program& program) {
  program.assert_declaration_type_consistency();
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
    if (function.open_summary && function.open_summary->scheme_complete)
      for (const auto& requirement : function.open_summary->requirements) {
        if (requirement.id.empty() || requirement.origin.module_id.empty() ||
            requirement.origin.enclosing_callable != function.semantic_callable_id ||
            !result.source_requirements.emplace(requirement.id,
                                                requirement).second)
          throw std::invalid_argument(
              "checked source requirement lacks exact provenance");
      }
    if (function.open_summary && function.open_summary->scheme_complete)
      for (const auto& requirement : function.open_summary->member_requirements) {
        if (requirement.requirement_id.empty() ||
            requirement.origin.module_id.empty() ||
            requirement.origin.enclosing_callable != function.semantic_callable_id ||
            !result.source_member_requirements.emplace(
                requirement.requirement_id, requirement).second)
          throw std::invalid_argument(
              "checked source member requirement lacks exact provenance");
      }
    if (function.open_summary && function.open_summary->scheme_complete &&
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
        if (type->kind == TypeKind::Builtin) {
          auto arity = Type::collection_arity(type->name);
          if (arity && type->type_args.size() != *arity) return false;
        }
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
                      }) ||
          std::any_of(checked.requirements.begin(), checked.requirements.end(),
                      [&](const Requirement& requirement) {
                        if (requirement.id.empty() ||
                            requirement.origin.module_id.empty() ||
                            !valid_term(requirement.subject, valid_term) ||
                            (requirement.result &&
                             !valid_term(*requirement.result, valid_term)))
                          return true;
                        return std::any_of(requirement.arguments.begin(),
                            requirement.arguments.end(),
                            [&](const TypeRef& argument) {
                              return !valid_term(argument, valid_term);
                            });
                      }) ||
          std::any_of(checked.member_requirements.begin(),
                      checked.member_requirements.end(),
                      [&](const MemberDispatchRequirement& requirement) {
                        if (requirement.requirement_id.empty() ||
                            requirement.origin.module_id.empty() ||
                            !valid_term(requirement.receiver, valid_term) ||
                            (requirement.result &&
                             !valid_term(*requirement.result, valid_term)))
                          return true;
                        return std::any_of(requirement.argument_types.begin(),
                            requirement.argument_types.end(),
                            [&](const TypeRef& argument) {
                              return !valid_term(argument, valid_term);
                            });
                      }))
        throw std::invalid_argument("complete open summary contains invalid bound variable");
      TypedOpenCallableSummaryV1 summary;
      summary.callable_id = checked.callable_id;
      summary.exported = function.exported;
      summary.checked_body_ir_available = function.checked_body_v1.has_value() ||
          function.checked_body_ir_v1_bytes.has_value();
      summary.checked_body_v1 = function.checked_body_v1;
      summary.checked_body_ir_v1_hash = function.checked_body_ir_v1_hash;
      summary.bound_variable_count = checked.bound_variable_count;
      summary.parameter_types = checked.parameter_types;
      summary.result_type = checked.result_type;
      summary.requirements = checked.requirements;
      summary.member_requirements = checked.member_requirements;
      summary.semantic_dependencies = checked.semantic_dependencies;
      summary.parameter_access = checked.parameter_access;
      summary.parameter_leaf_effects = function.parameter_leaf_effects;
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
      const auto gap = !function.open_summary
          ? TypedOpenSummaryGapV1::MissingTypedFacts
          : function.open_summary->observable_effects.unresolved
              ? TypedOpenSummaryGapV1::UnresolvedEffects
              : !function.open_summary->complete
                  ? TypedOpenSummaryGapV1::MissingTypedFacts
                  : TypedOpenSummaryGapV1::MissingParameterAccess;
      result.unavailable_open_summaries.emplace(
          function.semantic_callable_id, gap);
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
      record.exported = function.exported;
      record.parameter_types = specialization.parameter_type_refs;
      record.result_type = specialization.return_type_ref;
      record.parameter_access = specialization.parameter_effects;
      record.parameter_leaf_effects = function.parameter_leaf_effects;
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
