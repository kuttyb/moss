#include "typed_semantic_api.hpp"

#include <cassert>

using namespace moss;

static Program provider(SpecId id, const string& module) {
  Program result;
  ProgramSpecializationIdentity row;
  row.id = id;
  row.semantic_callable_identity = "callable-v1:provider:fn:f";
  row.diagnostic_key = "display-only:f";
  row.module_identity = module;
  result.specialization_table.push_back(row);
  Function function;
  function.name = "f";
  function.spec_id = id;
  Stmt statement;
  statement.semantic_types_by_spec_id[id] = "int";
  statement.joined_types_by_spec_id[id]["x"] = "int";
  function.body.push_back(statement);
  FunctionSpecialization specialization;
  specialization.spec_id = id;
  specialization.generated_name = "f__specialized";
  StaticSpecializationDependency dependency;
  dependency.callee_spec_id = id;
  specialization.dependencies.push_back(dependency);
  function.specializations.push_back(specialization);
  result.functions.push_back(function);
  DomainSpecialization domain;
  domain.handler_spec_ids.push_back(id);
  result.domain_specializations.push_back(domain);
  SemanticCallEdge edge;
  edge.caller_spec_id = id;
  edge.callee_spec_id = id;
  edge.argument_callable_spec_ids = {InvalidSpecId, id};
  result.semantic_call_edges.push_back(edge);
  result.typed_semantics = std::make_shared<TypedSemanticModuleV1>();
  TypedCopyFactV1 fact;
  fact.specialization_id = id;
  result.typed_semantics->copy_facts.push_back(fact);
  TypedCopyRequirementV1 requirement;
  requirement.specialization_id = id;
  result.typed_semantics->required_copy_occurrences.push_back(requirement);
  result.typed_semantics->local_ownership_specialization_identities[id] =
      "identity:f";
  return result;
}

int main() {
  Program destination;
  ProgramSpecializationIdentity occupied;
  occupied.id = 0;
  occupied.semantic_callable_identity = "callable-v1:root:fn:main";
  occupied.module_identity = "module-v1:root";
  destination.specialization_table.push_back(occupied);

  auto first = provider(0, "module-v1:first");
  auto second = provider(0, "module-v1:second");
  const auto first_map = remap_imported_program_spec_ids(
      destination, first, "module-v1:first");
  const auto second_map = remap_imported_program_spec_ids(
      destination, second, "module-v1:second");
  assert(first_map.at(0) == 1);
  assert(second_map.at(0) == 2);
  assert(destination.specialization_table.size() == 3);
  assert(destination.specialization_table[1].provider_module_identity ==
         "module-v1:first");
  assert(destination.specialization_table[1].module_identity ==
         "module-v1:first");
  assert(destination.specialization_table[1].provider_spec_id == 0);
  assert(destination.specialization_table[2].provider_module_identity ==
         "module-v1:second");
  assert(destination.specialization_table[2].module_identity ==
         "module-v1:second");
  assert(first.functions.front().spec_id == 1);
  assert(first.functions.front().specializations.front().spec_id == 1);
  assert(first.functions.front().specializations.front().dependencies.front()
             .callee_spec_id == 1);
  assert(first.functions.front().body.front().semantic_types_by_spec_id.at(1) ==
         "int");
  assert(first.functions.front().body.front().joined_types_by_spec_id.at(1)
             .at("x") == "int");
  assert(first.domain_specializations.front().handler_spec_ids.front() == 1);
  assert(first.semantic_call_edges.front().caller_spec_id == 1);
  assert(first.semantic_call_edges.front().callee_spec_id == 1);
  assert(first.semantic_call_edges.front().argument_callable_spec_ids ==
         std::vector<SpecId>({InvalidSpecId, 1}));
  assert(first.typed_semantics->copy_facts.front().specialization_id == 1);
  assert(first.typed_semantics->required_copy_occurrences.front()
             .specialization_id == 1);
  assert(first.typed_semantics->local_ownership_specialization_identities
             .count(1) == 1);

  Program dependency;
  ProgramSpecializationIdentity dependency_row;
  dependency_row.id = 3;
  dependency_row.semantic_callable_identity = "callable-v1:module-b:fn:g";
  dependency_row.module_identity = "module-v1:dependency";
  dependency_row.provider_module_identity = "module-v1:dependency";
  dependency_row.provider_spec_id = 0;
  destination.specialization_table.push_back(dependency_row);
  Program composite;
  ProgramSpecializationIdentity local_row;
  local_row.id = 0;
  local_row.semantic_callable_identity = "callable-v1:module-a:fn:a";
  local_row.module_identity = "module-v1:composite";
  composite.specialization_table.push_back(local_row);
  ProgramSpecializationIdentity imported_row = dependency_row;
  imported_row.id = 1;
  composite.specialization_table.push_back(imported_row);
  SemanticCallEdge external_edge;
  external_edge.caller_spec_id = 0;
  external_edge.callee_spec_id = 1;
  external_edge.target_module_identity = "module-v1:dependency";
  composite.semantic_call_edges.push_back(external_edge);
  const auto composite_map = remap_imported_program_spec_ids(
      destination, composite, "module-v1:composite");
  assert(composite_map.at(0) == 4);
  assert(composite_map.at(1) == 3);
  assert(composite.semantic_call_edges.front().caller_spec_id == 4);
  assert(composite.semantic_call_edges.front().callee_spec_id == 3);
  assert(composite.semantic_call_edges.front().target_module_identity ==
         "module-v1:dependency");

  auto duplicate = provider(0, "module-v1:duplicate");
  duplicate.specialization_table.push_back(duplicate.specialization_table.front());
  const auto size_before_duplicate = destination.specialization_table.size();
  bool duplicate_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        destination, duplicate, "module-v1:duplicate");
  } catch (const std::invalid_argument&) {
    duplicate_rejected = true;
  }
  assert(duplicate_rejected);
  assert(destination.specialization_table.size() == size_before_duplicate);

  auto unknown = provider(0, "module-v1:unknown");
  unknown.semantic_call_edges.front().callee_spec_id = 7;
  const auto size_before_unknown = destination.specialization_table.size();
  bool unknown_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        destination, unknown, "module-v1:unknown");
  } catch (const std::invalid_argument&) {
    unknown_rejected = true;
  }
  assert(unknown_rejected);
  assert(destination.specialization_table.size() == size_before_unknown);
}
