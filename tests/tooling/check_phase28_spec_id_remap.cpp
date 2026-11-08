#include "typed_semantic_api.hpp"

#include <cassert>
#include <cstdlib>

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
  function.execution_body = function.body;
  function.result_functional_pipeline_ids[id] = 7;
  function.result_functional_expression_plans_by_spec_id[id] = {};
  FunctionSpecialization specialization;
  specialization.spec_id = id;
  specialization.generated_name = "f__specialized";
  StaticSpecializationDependency dependency;
  dependency.callee_spec_id = id;
  specialization.dependencies.push_back(dependency);
  function.specializations.push_back(specialization);
  result.functions.push_back(function);
  Domain declaration;
  Handler handler;
  handler.spec_id = id;
  handler.body.push_back(statement);
  handler.execution_body = handler.body;
  FailureArm arm;
  arm.body.push_back(statement);
  arm.execution_body = arm.body;
  handler.failure_arms.push_back(arm);
  handler.execution_failure_arms = handler.failure_arms;
  declaration.handlers.push_back(handler);
  result.domains.push_back(declaration);
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

static Program method_provider(const string& module) {
  Program result;
  result.specialization_table = {
      {0, "method-v1:Holder.select.declaration", std::nullopt,
       "method declaration", module},
      {1, "method-v1:Holder.select", std::nullopt,
       "method body specialization", module},
      {2, "program:main", std::nullopt, "main", module}};
  ObjectType object;
  object.name = "Holder";
  Method method;
  method.owner = "Holder";
  method.name = "select";
  method.semantic_callable_id = "method-v1:Holder.select";
  method.spec_id = 0;
  method.specializations.push_back(
      {"method-body-v1:Holder.select(int)", {"int"}, 1});
  Stmt execution_loop;
  execution_loop.kind = Stmt::Kind::For;
  execution_loop.iterator_method_spec_ids_by_spec_id[1] = {1};
  method.execution_body = vector<Stmt>{execution_loop};
  object.methods.push_back(std::move(method));
  result.objects.push_back(std::move(object));
  result.main = MainProc{};
  result.main->spec_id = 2;
  Stmt loop;
  loop.kind = Stmt::Kind::For;
  loop.iterator_method_spec_ids_by_spec_id[2] = {1};
  result.main->body.push_back(std::move(loop));
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
  assert(first.functions.front().execution_body->front()
             .semantic_types_by_spec_id.at(1) == "int");
  assert(first.functions.front().result_functional_pipeline_ids.at(1) == 7);
  assert(first.functions.front().result_functional_expression_plans_by_spec_id.count(1));
  const auto& imported_handler = first.domains.front().handlers.front();
  assert(imported_handler.execution_body->front().semantic_types_by_spec_id.at(1) == "int");
  assert(imported_handler.failure_arms.front().execution_body->front()
             .semantic_types_by_spec_id.at(1) == "int");
  assert(imported_handler.execution_failure_arms->front().body.front()
             .semantic_types_by_spec_id.at(1) == "int");
  assert(imported_handler.execution_failure_arms->front().execution_body->front()
             .semantic_types_by_spec_id.at(1) == "int");
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

  // Dense artifact tables include unimported package modules. Their unused
  // rows do not create a dependency; any actual semantic reference still does.
  auto unrelated_table = provider(0, "module-v1:public-provider");
  ProgramSpecializationIdentity unrelated_row;
  unrelated_row.id = 1;
  unrelated_row.module_identity = "module-v1:unimported-app";
  unrelated_row.semantic_callable_identity = "program:main";
  unrelated_table.specialization_table.push_back(unrelated_row);
  Program unrelated_destination;
  const auto unrelated_map = remap_imported_program_spec_ids(
      unrelated_destination, unrelated_table, "module-v1:public-provider");
  assert(unrelated_destination.specialization_table.size() == 1);
  assert(unrelated_map.at(0) == 0);
  assert(unrelated_map.at(1) == InvalidSpecId);

  auto referenced_missing = provider(0, "module-v1:public-provider");
  referenced_missing.specialization_table.push_back(unrelated_row);
  referenced_missing.semantic_call_edges.front().callee_spec_id = 1;
  referenced_missing.semantic_call_edges.front().target_module_identity =
      unrelated_row.module_identity;
  Program missing_destination;
  bool referenced_missing_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        missing_destination, referenced_missing, "module-v1:public-provider");
  } catch (const std::invalid_argument& error) {
    referenced_missing_rejected = string(error.what()) ==
        "missing imported external module/SpecId pair";
  }
  assert(referenced_missing_rejected);
  assert(missing_destination.specialization_table.empty());

  const auto valid_call = select_exact_semantic_call_edge(
      first, first.semantic_call_edges.front().caller_spec_id,
      first.semantic_call_edges.front().call_est_arena_index,
      first.semantic_call_edges.front().call_expression_occurrence_id);
  assert(valid_call.kind == ExactSemanticCallEdgeLookup::Kind::Unique);
  assert(valid_call.edge);
#ifdef MOSS_TEST_BUILD
  auto assert_call_hook = [&](const char* hook,
                              ExactSemanticCallEdgeLookup::Kind expected) {
    setenv(hook, "1", 1);
    const auto injected = select_exact_semantic_call_edge(
        first, first.semantic_call_edges.front().caller_spec_id,
        first.semantic_call_edges.front().call_est_arena_index,
        first.semantic_call_edges.front().call_expression_occurrence_id);
    unsetenv(hook);
    assert(injected.kind == expected);
    assert(!injected.edge);
  };
  assert_call_hook("MOSS_TEST_CONSUMER_DROP_SEMANTIC_CALL_EDGE",
                   ExactSemanticCallEdgeLookup::Kind::Missing);
  assert_call_hook("MOSS_TEST_CONSUMER_STALE_SEMANTIC_CALL_EDGE",
                   ExactSemanticCallEdgeLookup::Kind::Missing);
  assert_call_hook("MOSS_TEST_CONSUMER_DUPLICATE_SEMANTIC_CALL_EDGE",
                   ExactSemanticCallEdgeLookup::Kind::Duplicate);
  assert_call_hook("MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_SPEC_ID",
                   ExactSemanticCallEdgeLookup::Kind::Contradictory);
  assert_call_hook(
      "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_ARGUMENT_SPEC_ID",
      ExactSemanticCallEdgeLookup::Kind::Contradictory);
#endif

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

  auto first_methods = method_provider("module-v1:methods-first");
  auto second_methods = method_provider("module-v1:methods-second");
  const auto first_method_map = remap_imported_program_spec_ids(
      destination, first_methods, "module-v1:methods-first");
  const auto second_method_map = remap_imported_program_spec_ids(
      destination, second_methods, "module-v1:methods-second");
  assert(first_method_map.at(0) != second_method_map.at(0));
  assert(first_method_map.at(1) != second_method_map.at(1));
  assert(first_methods.objects.front().methods.front().spec_id ==
         first_method_map.at(0));
  assert(first_methods.objects.front().methods.front().specializations.front()
             .spec_id == first_method_map.at(1));
  assert(first_methods.main->spec_id == first_method_map.at(2));
  const auto& execution_iterator_ids = first_methods.objects.front()
      .methods.front().execution_body->front().iterator_method_spec_ids_by_spec_id;
  assert(execution_iterator_ids.size() == 1);
  assert(execution_iterator_ids.at(first_method_map.at(1)) ==
         std::vector<SpecId>{first_method_map.at(1)});
  assert(first_method_map.at(1) != 1);  // Destination was already populated.
  const auto& first_iterator_ids = first_methods.main->body.front()
      .iterator_method_spec_ids_by_spec_id;
  assert(first_iterator_ids.size() == 1);
  assert(first_iterator_ids.at(first_method_map.at(2)) ==
         std::vector<SpecId>{first_method_map.at(1)});
  assert(second_methods.objects.front().methods.front().spec_id ==
         second_method_map.at(0));
  assert(second_methods.objects.front().methods.front().specializations.front()
             .spec_id == second_method_map.at(1));

  // Section 2 execution bodies retain live iterator authority. An actual
  // missing foreign target must not survive merely because its table row is
  // otherwise unused by the generic body view.
  auto missing_execution = method_provider("module-v1:execution-missing");
  ProgramSpecializationIdentity missing_target;
  missing_target.id = 3;
  missing_target.module_identity = "module-v1:unselected-iterator";
  missing_target.semantic_callable_identity = "method-v1:Foreign.next";
  missing_execution.specialization_table.push_back(missing_target);
  Stmt execution_loop;
  execution_loop.kind = Stmt::Kind::For;
  execution_loop.iterator_method_spec_ids_by_spec_id[1] = {3};
  missing_execution.objects.front().methods.front().execution_body =
      vector<Stmt>{execution_loop};
  Program execution_destination;
  bool missing_execution_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        execution_destination, missing_execution, "module-v1:execution-missing");
  } catch (const std::invalid_argument& error) {
    missing_execution_rejected = string(error.what()) ==
        "missing imported external module/SpecId pair";
  }
  assert(missing_execution_rejected);
  assert(execution_destination.specialization_table.empty());

  auto duplicated_iterator = method_provider("module-v1:methods-duplicate-iterator");
  duplicated_iterator.main->body.front().iterator_method_spec_ids_by_spec_id[2] =
      {1, 1};
  bool duplicate_iterator_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        destination, duplicated_iterator,
        "module-v1:methods-duplicate-iterator");
  } catch (const std::invalid_argument&) {
    duplicate_iterator_rejected = true;
  }
  assert(duplicate_iterator_rejected);

  auto missing_iterator = method_provider("module-v1:methods-missing-iterator");
  missing_iterator.main->body.front().iterator_method_spec_ids_by_spec_id[2] =
      {9};
  bool missing_iterator_rejected = false;
  try {
    (void)remap_imported_program_spec_ids(
        destination, missing_iterator,
        "module-v1:methods-missing-iterator");
  } catch (const std::invalid_argument&) {
    missing_iterator_rejected = true;
  }
  assert(missing_iterator_rejected);
}
