#include <cassert>
#include <stdexcept>
#include "typed_semantic_api.hpp"

using namespace moss;

static TypedIndexedCallArgumentReadRequirementV1 requirement() {
  TypedIndexedCallArgumentReadRequirementV1 row;
  row.availability = TypedIndexedCallArgumentReadFactV1::Availability::Present;
  row.est_arena_index = 2;
  row.call_argument_expression_occurrence_id = 21;
  row.collection_index_expression_occurrence_id = 21;
  row.receiver_expression_occurrence_id = 19;
  row.index_expression_occurrence_id = 20;
  row.statement_occurrence_id = 8;
  row.argument_index = 0;
  row.target_parameter_index = 0;
  row.argument_occurrence_span = {7, 14, 1, 80, 89};
  row.index_occurrence_span = row.argument_occurrence_span;
  row.target_callable_identity = "builtin:assertEqual-v1";
  row.target_resolution =
      TypedIndexedCallArgumentReadFactV1::TargetResolution::Resolved;
  row.target_parameter_access = Effect::Read;
  row.receiver_binding_identity = "binding:test:parts";
  row.receiver_definition_identity = "definition:test:parts";
  row.receiver_type = Type::builtin("vector", {Type::primitive("string")});
  row.index_type = Type::primitive("int");
  row.indexed_element_type = Type::primitive("string");
  row.required_access = Effect::Read;
  row.temporary_nonescaping_borrow = true;
  row.permits_copy = false;
  row.specialization_context = "test:indexed-call-read";
  row.est_fact_context = row.specialization_context;
  row.provenance.source_file = "tests/indexed_call_read.moss";
  row.provenance.line = 7;
  row.provenance.defining_module_id = "module-v1:test@0.1.0::test";
  row.provenance.enclosing_callable_id = "test:indexed-call-read";
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
  auto fact = static_cast<TypedIndexedCallArgumentReadFactV1&>(req);
  TypedSemanticModuleV1 module;
  module.required_indexed_call_argument_read_occurrences = {req};
  module.indexed_call_argument_read_facts = {fact};
  assert(fact.is_authoritative());
  assert(&module.find_indexed_call_argument_read_fact(req) ==
         &module.indexed_call_argument_read_facts.front());
  module.require_all_phase28_authority_facts();

  auto missing = module;
  missing.indexed_call_argument_read_facts.clear();
  reject(missing);

  auto duplicate_fact = module;
  duplicate_fact.indexed_call_argument_read_facts.push_back(fact);
  reject(duplicate_fact);

  auto duplicate_requirement = module;
  duplicate_requirement.required_indexed_call_argument_read_occurrences
      .push_back(req);
  reject(duplicate_requirement);

  auto perturbed_requirement = module;
  perturbed_requirement.required_indexed_call_argument_read_occurrences.front()
      .target_callable_identity += ":other";
  reject(perturbed_requirement);

  auto perturbed_fact = module;
  ++perturbed_fact.indexed_call_argument_read_facts.front()
        .collection_index_expression_occurrence_id;
  reject(perturbed_fact);
  perturbed_fact = module;
  ++perturbed_fact.indexed_call_argument_read_facts.front()
        .receiver_expression_occurrence_id;
  reject(perturbed_fact);
  perturbed_fact = module;
  ++perturbed_fact.indexed_call_argument_read_facts.front()
        .index_expression_occurrence_id;
  reject(perturbed_fact);
  perturbed_fact = module;
  ++perturbed_fact.indexed_call_argument_read_facts.front()
        .index_occurrence_span.source_end;
  reject(perturbed_fact);
  perturbed_fact = module;
  perturbed_fact.indexed_call_argument_read_facts.front().indexed_element_type =
      Type::primitive("int");
  reject(perturbed_fact);
  perturbed_fact = module;
  perturbed_fact.indexed_call_argument_read_facts.front()
      .receiver_definition_identity += ":other";
  reject(perturbed_fact);
  perturbed_fact = module;
  perturbed_fact.indexed_call_argument_read_facts.front()
      .target_parameter_access = Effect::Consume;
  reject(perturbed_fact);
  perturbed_fact = module;
  perturbed_fact.indexed_call_argument_read_facts.front()
      .temporary_nonescaping_borrow = false;
  reject(perturbed_fact);
  perturbed_fact = module;
  perturbed_fact.indexed_call_argument_read_facts.front().permits_copy = true;
  reject(perturbed_fact);

  auto unsupported = fact;
  unsupported.availability =
      TypedIndexedCallArgumentReadFactV1::Availability::UnsupportedTarget;
  unsupported.target_callable_identity.clear();
  unsupported.target_resolution =
      TypedIndexedCallArgumentReadFactV1::TargetResolution::Unresolved;
  assert(!unsupported.is_authoritative());
  assert(unsupported.is_intentionally_unsupported());
}
