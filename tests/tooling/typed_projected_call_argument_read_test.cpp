#include <cassert>
#include <stdexcept>
#include "typed_semantic_api.hpp"

using namespace moss;

static TypedProjectedCallArgumentReadRequirementV1 requirement() {
  TypedProjectedCallArgumentReadRequirementV1 row;
  row.availability = TypedProjectedCallArgumentReadFactV1::Availability::Present;
  row.est_arena_index = 4;
  row.call_argument_expression_occurrence_id = 41;
  row.receiver_expression_occurrence_id = 42;
  row.collection_index_expression_occurrence_id = 43;
  row.index_expression_occurrence_id = 44;
  row.member_projection_expression_occurrence_id = 45;
  row.statement_occurrence_id = 12;
  row.argument_index = 0;
  row.target_parameter_index = 0;
  row.argument_occurrence_span = {12, 8, 1, 100, 118};
  row.projection_occurrence_span = {12, 8, 1, 100, 116};
  row.target_callable_identity = "builtin:assertEqual-v1";
  row.target_resolution =
      TypedProjectedCallArgumentReadFactV1::TargetResolution::Resolved;
  row.target_parameter_access = Effect::Read;
  row.receiver_binding_identity = "binding:test:spans";
  row.receiver_definition_identity = "definition:test:spans";
  row.member_name = "text";
  row.receiver_type = Type::builtin("vector", {
      Type::builtin("vector", {Type::primitive("int")})});
  row.index_type = Type::primitive("int");
  row.indexed_element_type = Type::builtin("vector", {
      Type::primitive("int")});
  row.projection_base_type = row.indexed_element_type;
  row.projected_member_type = Type::primitive("string");
  row.required_access = Effect::Read;
  row.temporary_nonescaping_borrow = true;
  row.permits_copy = false;
  row.specialization_context = "test:projected-call-read";
  row.est_fact_context = row.specialization_context;
  row.provenance.source_file = "tests/projected_call_read.moss";
  row.provenance.line = 12;
  row.provenance.defining_module_id = "module-v1:test";
  row.provenance.enclosing_callable_id = "test:projected-call-read";
  return row;
}

static void reject(const TypedSemanticModuleV1& module) {
  bool threw = false;
  try { module.require_all_phase28_authority_facts(); }
  catch (const std::invalid_argument&) { threw = true; }
  assert(threw);
}

int main() {
  auto req = requirement();
  auto fact = static_cast<TypedProjectedCallArgumentReadFactV1&>(req);
  TypedSemanticModuleV1 module;
  module.required_projected_call_argument_read_occurrences = {req};
  module.projected_call_argument_read_facts = {fact};
  assert(fact.is_authoritative());
  assert(&module.find_projected_call_argument_read_fact(req) ==
         &module.projected_call_argument_read_facts.front());
  module.require_all_phase28_authority_facts();

  auto missing_fact = module;
  missing_fact.projected_call_argument_read_facts.clear();
  reject(missing_fact);

  auto duplicate_fact = module;
  duplicate_fact.projected_call_argument_read_facts.push_back(fact);
  reject(duplicate_fact);

  auto duplicate_requirement = module;
  duplicate_requirement.required_projected_call_argument_read_occurrences
      .push_back(req);
  reject(duplicate_requirement);

  auto perturbed_requirement = module;
  perturbed_requirement.required_projected_call_argument_read_occurrences.front()
      .target_callable_identity += ":other";
  reject(perturbed_requirement);

  auto perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .call_argument_expression_occurrence_id++;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .receiver_expression_occurrence_id++;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .collection_index_expression_occurrence_id++;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .index_expression_occurrence_id++;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .member_projection_expression_occurrence_id++;
  reject(perturbed_fact);

  perturbed_fact = module;
  ++perturbed_fact.projected_call_argument_read_facts.front()
        .argument_occurrence_span.source_end;
  reject(perturbed_fact);

  perturbed_fact = module;
  ++perturbed_fact.projected_call_argument_read_facts.front()
        .projection_occurrence_span.source_end;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .target_callable_identity += ":perturbed";
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .receiver_binding_identity += ":perturbed";
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .receiver_definition_identity += ":perturbed";
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front().projected_member_type =
      Type::primitive("int");
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .target_parameter_access = Effect::Consume;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front()
      .temporary_nonescaping_borrow = false;
  reject(perturbed_fact);

  perturbed_fact = module;
  perturbed_fact.projected_call_argument_read_facts.front().permits_copy = true;
  reject(perturbed_fact);

  auto unsupported_target = fact;
  unsupported_target.availability =
      TypedProjectedCallArgumentReadFactV1::Availability::UnsupportedTarget;
  unsupported_target.target_callable_identity.clear();
  unsupported_target.target_resolution =
      TypedProjectedCallArgumentReadFactV1::TargetResolution::Unresolved;
  assert(!unsupported_target.is_authoritative());
  assert(unsupported_target.is_intentionally_unsupported());

  auto nonread_target = fact;
  nonread_target.availability =
      TypedProjectedCallArgumentReadFactV1::Availability::UnsupportedParameterAccess;
  nonread_target.target_parameter_access = Effect::Consume;
  assert(!nonread_target.is_authoritative());
  assert(nonread_target.is_intentionally_unsupported());
}
