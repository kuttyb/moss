#include <cassert>
#include <stdexcept>
#include <string>
#include "typed_semantic_api.hpp"

using namespace moss;

static TypedLocalOwnershipRequirementV1 requirement(
    const std::string& context, const std::string& specialization,
    const std::string& definition) {
  TypedLocalOwnershipRequirementV1 result;
  result.est_arena_index = 3;
  result.expression_occurrence_id = 17;
  result.statement_occurrence_id = 9;
  result.specialization_context = context;
  result.specialization_identity = specialization;
  result.definition_identity = definition;
  result.binding_identity = "binding:inspect:alias";
  result.killed_definition_identity = "definition:prior";
  result.join_definition_identity = "join:inspect:alias";
  result.source_binding_identity = "parameter:0";
  result.source_definition_identity = "parameter:0:definition";
  result.access = Effect::Consume;
  result.source_kind = TypedLocalOwnershipFactV1::SourceKind::LocalBinding;
  result.disposition = TypedLocalOwnershipFactV1::Disposition::Transferred;
  result.availability = TypedLocalOwnershipFactV1::Availability::Present;
  return result;
}

static TypedLocalOwnershipFactV1 fact(
    const TypedLocalOwnershipRequirementV1& req) {
  TypedLocalOwnershipFactV1 result;
  result.est_arena_index = req.est_arena_index;
  result.expression_occurrence_id = req.expression_occurrence_id;
  result.statement_occurrence_id = req.statement_occurrence_id;
  result.specialization_context = req.specialization_context;
  result.specialization_identity = req.specialization_identity;
  result.definition_identity = req.definition_identity;
  result.binding_identity = req.binding_identity;
  result.killed_definition_identity = req.killed_definition_identity;
  result.join_definition_identity = req.join_definition_identity;
  result.source_binding_identity = req.source_binding_identity;
  result.source_definition_identity = req.source_definition_identity;
  result.access = req.access;
  result.source_kind = req.source_kind;
  result.disposition = req.disposition;
  result.availability = req.availability;
  return result;
}

static TypedLoopOwnershipRequirementV1 loop_requirement(
    const std::string& context, const std::string& specialization) {
  TypedLoopOwnershipRequirementV1 result;
  result.loop_statement_occurrence_id = 7;
  result.source_statement_occurrence_id = 11;
  result.target_statement_occurrence_id = 12;
  result.definition_statement_occurrence_id = 10;
  result.loop_est_arena_index = 3;
  result.loop_expression_occurrence_id = 17;
  result.definition_est_arena_index = 3;
  result.definition_expression_occurrence_id = 15;
  result.definition_identity = "fn:inspect@10:slot";
  result.binding_identity = "binding:inspect:slot";
  result.killed_definition_identities = {"fn:inspect@5:slot"};
  result.source_binding_identity = "parameter:0";
  result.specialization_context = context;
  result.specialization_identity = specialization;
  result.edge_kind = TypedLoopOwnershipFactV1::EdgeKind::Backedge;
  result.converged_access = Effect::Consume;
  result.value_state = TypedLoopOwnershipFactV1::ValueState::Available;
  result.availability = TypedLoopOwnershipFactV1::Availability::Present;
  return result;
}

static TypedLoopOwnershipFactV1 loop_fact(
    const TypedLoopOwnershipRequirementV1& req) {
  TypedLoopOwnershipFactV1 result;
  result.loop_statement_occurrence_id = req.loop_statement_occurrence_id;
  result.source_statement_occurrence_id = req.source_statement_occurrence_id;
  result.target_statement_occurrence_id = req.target_statement_occurrence_id;
  result.definition_statement_occurrence_id =
      req.definition_statement_occurrence_id;
  result.loop_est_arena_index = req.loop_est_arena_index;
  result.loop_expression_occurrence_id = req.loop_expression_occurrence_id;
  result.definition_est_arena_index = req.definition_est_arena_index;
  result.definition_expression_occurrence_id =
      req.definition_expression_occurrence_id;
  result.definition_identity = req.definition_identity;
  result.binding_identity = req.binding_identity;
  result.killed_definition_identities = req.killed_definition_identities;
  result.source_binding_identity = req.source_binding_identity;
  result.specialization_context = req.specialization_context;
  result.specialization_identity = req.specialization_identity;
  result.edge_kind = req.edge_kind;
  result.converged_access = req.converged_access;
  result.value_state = req.value_state;
  result.reachable = true;
  result.availability = req.availability;
  return result;
}

int main() {
  const auto first = requirement("fn:inspect<First>", "spec:first", "fn:inspect@8:alias");
  const auto second = requirement("fn:inspect<Second>", "spec:second", "fn:inspect@8:alias");
  TypedSemanticModuleV1 module;
  module.required_local_ownership_occurrences = {first, second};
  module.local_ownership_facts = {fact(first), fact(second)};
  module.local_ownership_specialization_contexts = {
      {first.specialization_context, first.specialization_identity},
      {second.specialization_context, second.specialization_identity}};
  module.specializations.emplace(first.specialization_identity,
                                  TypedSpecializationV1{});
  module.specializations.emplace(second.specialization_identity,
                                  TypedSpecializationV1{});

  const auto* first_req = module.find_local_ownership_requirement(
      3, 17, "fn:inspect<First>");
  const auto* second_req = module.find_local_ownership_requirement(
      3, 17, "fn:inspect<Second>");
  assert(first_req && first_req->specialization_identity == "spec:first");
  assert(second_req && second_req->specialization_identity == "spec:second");
  assert(module.local_ownership_specialization_identity(
             "fn:inspect<First>") == "spec:first");
  assert(module.local_ownership_specialization_identity(
             "fn:inspect<Second>") == "spec:second");
  assert(module.find_local_ownership_requirement(
      3, 17, 9, first.specialization_context, first.specialization_identity,
      first.definition_identity) == first_req);
  assert(module.find_local_ownership_requirement(
      3, 17, 9, first.specialization_context, second.specialization_identity,
      first.definition_identity) == nullptr);
  assert(module.find_local_ownership_fact(*first_req).specialization_identity ==
         "spec:first");
  assert(module.find_local_ownership_fact(*second_req).specialization_identity ==
         "spec:second");

  auto borrowed_read_req = first;
  borrowed_read_req.specialization_context = "fn:borrowed";
  borrowed_read_req.specialization_identity.clear();
  borrowed_read_req.access = Effect::Read;
  borrowed_read_req.disposition =
      TypedLocalOwnershipFactV1::Disposition::Borrowed;
  TypedSemanticModuleV1 borrowed_read_module;
  borrowed_read_module.required_local_ownership_occurrences = {borrowed_read_req};
  borrowed_read_module.local_ownership_facts = {fact(borrowed_read_req)};
  assert(borrowed_read_module.find_local_ownership_fact(borrowed_read_req)
             .is_authoritative());
  assert(borrowed_read_module.require_all_local_ownership_facts().size() == 1);

  auto borrowed_write_req = borrowed_read_req;
  borrowed_write_req.access = Effect::Write;
  TypedSemanticModuleV1 borrowed_write_module;
  borrowed_write_module.required_local_ownership_occurrences = {borrowed_write_req};
  borrowed_write_module.local_ownership_facts = {fact(borrowed_write_req)};
  assert(borrowed_write_module.find_local_ownership_fact(borrowed_write_req)
             .is_authoritative());

  auto require_throw = [](auto&& operation) {
    bool threw = false;
    try { operation(); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
  };
  auto dropped_borrow_fact = borrowed_read_module;
  dropped_borrow_fact.local_ownership_facts.clear();
  require_throw([&] {
    dropped_borrow_fact.require_all_local_ownership_facts();
  });
  auto missing_borrow_requirement = borrowed_read_module;
  missing_borrow_requirement.required_local_ownership_occurrences.clear();
  require_throw([&] {
    missing_borrow_requirement.find_local_ownership_fact(borrowed_read_req);
  });
  auto perturbed_borrow_disposition = borrowed_read_module;
  perturbed_borrow_disposition.local_ownership_facts.front().disposition =
      TypedLocalOwnershipFactV1::Disposition::Transferred;
  require_throw([&] {
    perturbed_borrow_disposition.find_local_ownership_fact(borrowed_read_req);
  });
  auto contradictory_borrow_access = borrowed_read_module;
  contradictory_borrow_access.local_ownership_facts.front().access =
      Effect::Consume;
  contradictory_borrow_access.local_ownership_facts.front().disposition =
      TypedLocalOwnershipFactV1::Disposition::Transferred;
  require_throw([&] {
    contradictory_borrow_access.find_local_ownership_fact(borrowed_read_req);
  });
  auto duplicate_borrow_fact = borrowed_read_module;
  duplicate_borrow_fact.local_ownership_facts.push_back(
      duplicate_borrow_fact.local_ownership_facts.front());
  require_throw([&] {
    duplicate_borrow_fact.find_local_ownership_fact(borrowed_read_req);
  });
  auto duplicate_borrow_requirement = borrowed_read_module;
  duplicate_borrow_requirement.required_local_ownership_occurrences.push_back(
      borrowed_read_req);
  require_throw([&] {
    duplicate_borrow_requirement.find_local_ownership_fact(borrowed_read_req);
  });
  auto perturbed_borrow_requirement = borrowed_read_module;
  perturbed_borrow_requirement.required_local_ownership_occurrences.front()
      .disposition = TypedLocalOwnershipFactV1::Disposition::Transferred;
  require_throw([&] {
    perturbed_borrow_requirement.find_local_ownership_fact(
        perturbed_borrow_requirement.required_local_ownership_occurrences.front());
  });
  auto missing = module;
  missing.local_ownership_facts.erase(missing.local_ownership_facts.begin());
  require_throw([&] { missing.find_local_ownership_fact(*first_req); });
  require_throw([&] {
    missing.find_local_ownership_requirement(3, 17, "fn:inspect<First>");
  });

  auto perturbed_effect = module;
  perturbed_effect.local_ownership_facts[0].access = Effect::Read;
  require_throw([&] { perturbed_effect.find_local_ownership_fact(*first_req); });

  auto perturbed_identity = module;
  perturbed_identity.local_ownership_facts[0].specialization_identity = "spec:second";
  require_throw([&] { perturbed_identity.find_local_ownership_fact(*first_req); });

  auto perturbed_source = module;
  perturbed_source.local_ownership_facts[0].source_binding_identity = "parameter:1";
  require_throw([&] { perturbed_source.find_local_ownership_fact(*first_req); });

  auto perturbed_join = module;
  perturbed_join.local_ownership_facts[0].join_definition_identity = "join:other";
  require_throw([&] { perturbed_join.find_local_ownership_fact(*first_req); });

  auto perturbed_binding = module;
  perturbed_binding.local_ownership_facts[0].binding_identity = "binding:other";
  require_throw([&] { perturbed_binding.find_local_ownership_fact(*first_req); });

  auto perturbed_kill = module;
  perturbed_kill.local_ownership_facts[0].killed_definition_identity =
      "definition:other";
  require_throw([&] { perturbed_kill.find_local_ownership_fact(*first_req); });

  auto missing_specialization_identity = module;
  missing_specialization_identity.required_local_ownership_occurrences[0]
      .specialization_identity.clear();
  missing_specialization_identity.local_ownership_facts[0]
      .specialization_identity.clear();
  require_throw([&] {
    missing_specialization_identity.find_local_ownership_requirement(
        3, 17, "fn:inspect<First>");
  });

  auto perturbed_mapping = module;
  perturbed_mapping.local_ownership_specialization_contexts[
      first.specialization_context] = second.specialization_identity;
  require_throw([&] {
    perturbed_mapping.find_local_ownership_requirement(
        3, 17, first.specialization_context);
  });

  auto duplicate = module;
  duplicate.required_local_ownership_occurrences.push_back(first);
  require_throw([&] {
    duplicate.find_local_ownership_requirement(3, 17, first.specialization_context);
  });

  auto unsupported_req = first;
  unsupported_req.inference_status =
      TypedLocalOwnershipFactV1::InferenceStatus::Unsupported;
  unsupported_req.unsupported_reason =
      TypedLocalOwnershipFactV1::UnsupportedReason::AmbiguousUse;
  unsupported_req.availability =
      TypedLocalOwnershipFactV1::Availability::NotRequired;
  auto unsupported_fact = fact(unsupported_req);
  unsupported_fact.inference_status = unsupported_req.inference_status;
  unsupported_fact.unsupported_reason = unsupported_req.unsupported_reason;
  unsupported_fact.availability = unsupported_req.availability;
  TypedSemanticModuleV1 unsupported;
  unsupported.required_local_ownership_occurrences = {unsupported_req};
  unsupported.local_ownership_facts = {unsupported_fact};
  unsupported.local_ownership_specialization_contexts = {
      {unsupported_req.specialization_context,
       unsupported_req.specialization_identity}};
  unsupported.specializations.emplace(unsupported_req.specialization_identity,
                                       TypedSpecializationV1{});
  assert(unsupported.find_local_ownership_fact(unsupported_req)
             .is_intentionally_unsupported());

  const auto loop_req = loop_requirement("fn:inspect<Loop>", "spec:loop");
  TypedSemanticModuleV1 loop_module;
  loop_module.required_loop_ownership_occurrences = {loop_req};
  loop_module.loop_ownership_facts = {loop_fact(loop_req)};
  loop_module.local_ownership_specialization_contexts = {
      {loop_req.specialization_context, loop_req.specialization_identity}};
  loop_module.specializations.emplace(loop_req.specialization_identity,
                                      TypedSpecializationV1{});
  assert(loop_module.find_loop_ownership_fact(loop_req)->definition_identity ==
         loop_req.definition_identity);

  auto dropped_loop_requirement = loop_module;
  dropped_loop_requirement.required_loop_ownership_occurrences.clear();
  require_throw([&] {
    dropped_loop_requirement.find_loop_ownership_fact(loop_req);
  });

  auto duplicate_loop_requirement = loop_module;
  duplicate_loop_requirement.required_loop_ownership_occurrences.push_back(
      loop_req);
  require_throw([&] {
    duplicate_loop_requirement.find_loop_ownership_fact(loop_req);
  });

  auto perturbed_loop_requirement = loop_module;
  auto changed_loop_req = loop_req;
  changed_loop_req.definition_identity = "fn:inspect@99:slot";
  require_throw([&] {
    perturbed_loop_requirement.find_loop_ownership_fact(changed_loop_req);
  });

  auto missing_loop_specialization_mapping = loop_module;
  missing_loop_specialization_mapping.local_ownership_specialization_contexts[
      loop_req.specialization_context] = "spec:other";
  missing_loop_specialization_mapping.specializations.emplace(
      "spec:other", TypedSpecializationV1{});
  require_throw([&] {
    missing_loop_specialization_mapping.find_loop_ownership_fact(loop_req);
  });

  auto missing_loop_fact = loop_module;
  missing_loop_fact.loop_ownership_facts.clear();
  require_throw([&] { missing_loop_fact.find_loop_ownership_fact(loop_req); });

  auto perturbed_loop_effect = loop_module;
  perturbed_loop_effect.loop_ownership_facts[0].converged_access =
      Effect::Read;
  require_throw([&] {
    perturbed_loop_effect.find_loop_ownership_fact(loop_req);
  });
  return 0;
}
