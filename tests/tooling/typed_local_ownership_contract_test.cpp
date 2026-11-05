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
  result.value_type = req.value_type;
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
  auto require_throw = [](auto&& operation) {
    bool threw = false;
    try { operation(); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
  };
  const auto never_type = Type::never();
  assert(never_type->is_closed());
  assert(never_type->kind == TypeKind::Never);
  assert(never_type->to_canonical_string() == "never");
  assert(never_type->stable_type_key() == Type::never()->stable_type_key());
  TypedCopyRequirementV1 copy_req;
  copy_req.est_arena_index = 2;
  copy_req.copy_occurrence_id = 19;
  copy_req.source_occurrence_id = 20;
  copy_req.statement_occurrence_id = 7;
  copy_req.specialization_identity = "spec:copy";
  copy_req.specialization_context = "fn:copy";
  copy_req.est_fact_context = "ctx:copy";
  copy_req.provenance = {"fixture.moss", 8, "module:fixture", "fn:copy"};
  copy_req.copy_occurrence_span = {8, 3, 1, 31, 38};
  copy_req.source_occurrence_span = {8, 8, 1, 35, 36};
  copy_req.source_type = Type::builtin("vector", {Type::primitive("int")});
  copy_req.result_type = copy_req.source_type;
  TypedCopyFactV1 copy_fact;
  copy_fact.est_arena_index = copy_req.est_arena_index;
  copy_fact.copy_occurrence_id = copy_req.copy_occurrence_id;
  copy_fact.source_occurrence_id = copy_req.source_occurrence_id;
  copy_fact.statement_occurrence_id = copy_req.statement_occurrence_id;
  copy_fact.specialization_identity = copy_req.specialization_identity;
  copy_fact.specialization_context = copy_req.specialization_context;
  copy_fact.est_fact_context = copy_req.est_fact_context;
  copy_fact.provenance = copy_req.provenance;
  copy_fact.copy_occurrence_span = copy_req.copy_occurrence_span;
  copy_fact.source_occurrence_span = copy_req.source_occurrence_span;
  copy_fact.source_type = copy_req.source_type;
  copy_fact.result_type = copy_req.result_type;
  copy_fact.availability = TypedCopyFactV1::Availability::Present;
  TypedSemanticModuleV1 copies;
  copies.required_copy_occurrences = {copy_req};
  copies.copy_facts = {copy_fact};
  const auto resolved_copies = copies.require_all_copy_facts();
  assert(resolved_copies.size() == 1 && resolved_copies.front()->is_authoritative());
  auto missing_copy_authority = copies;
  missing_copy_authority.copy_facts.clear();
  require_throw([&] { missing_copy_authority.require_all_copy_facts(); });
  auto duplicate_copy_authority = copies;
  duplicate_copy_authority.copy_facts.push_back(copy_fact);
  require_throw([&] { duplicate_copy_authority.require_all_copy_facts(); });
  auto perturbed_copy_span = copies;
  perturbed_copy_span.copy_facts.front().source_occurrence_span.source_begin++;
  require_throw([&] { perturbed_copy_span.require_all_copy_facts(); });
  auto incomplete_copy_set = copies;
  incomplete_copy_set.required_copy_occurrences.push_back(copy_req);
  require_throw([&] { incomplete_copy_set.require_all_copy_facts(); });

  TypedCollectionOutcomeRequirementV1 queue_outcome;
  queue_outcome.operation = TypedCollectionOutcomeFactV1::Operation::QueuePop;
  queue_outcome.outcome = TypedCollectionOutcomeFactV1::Outcome::AlwaysRaises;
  queue_outcome.availability = TypedCollectionOutcomeFactV1::Availability::Present;
  queue_outcome.operation_est_arena_index = 4;
  queue_outcome.operation_expression_occurrence_id = 28;
  queue_outcome.operation_occurrence_span = {9, 7, 2, 46, 53};
  queue_outcome.statement_occurrence_id = 12;
  queue_outcome.specialization_identity = "spec:queue";
  queue_outcome.specialization_context = "fn:queue";
  queue_outcome.est_fact_context = "ctx:queue";
  queue_outcome.provenance = {"fixture.moss", 9, "module:fixture", "fn:queue"};
  queue_outcome.receiver_binding_identity = "binding:q";
  queue_outcome.receiver_definition_identity = "definition:q:fresh";
  queue_outcome.fresh_empty_definition_identity = "definition:q:fresh";
  queue_outcome.receiver_type = Type::builtin("queue", {Type::never()});
  queue_outcome.result_type = Type::never();
  queue_outcome.error_type_identity = "std__CollectionError";
  queue_outcome.error_variant_identity = "Empty";
  TypedSemanticModuleV1 collection_outcomes;
  collection_outcomes.required_collection_outcome_occurrences = {queue_outcome};
  collection_outcomes.collection_outcome_facts = {queue_outcome};
  const auto resolved_outcomes =
      collection_outcomes.require_all_collection_outcome_facts();
  assert(resolved_outcomes.size() == 1 &&
         resolved_outcomes.front()->is_authoritative());
  auto missing_outcome = collection_outcomes;
  missing_outcome.collection_outcome_facts.clear();
  require_throw([&] { missing_outcome.require_all_collection_outcome_facts(); });
  auto missing_outcome_requirement = collection_outcomes;
  missing_outcome_requirement.required_collection_outcome_occurrences.clear();
  require_throw([&] {
    missing_outcome_requirement.require_all_collection_outcome_facts();
  });
  auto duplicate_outcome_fact = collection_outcomes;
  duplicate_outcome_fact.collection_outcome_facts.push_back(queue_outcome);
  require_throw([&] {
    duplicate_outcome_fact.require_all_collection_outcome_facts();
  });
  auto duplicate_outcome_requirement = collection_outcomes;
  duplicate_outcome_requirement.required_collection_outcome_occurrences
      .push_back(queue_outcome);
  require_throw([&] {
    duplicate_outcome_requirement.require_all_collection_outcome_facts();
  });
  auto perturbed_empty_definition = collection_outcomes;
  perturbed_empty_definition.collection_outcome_facts.front()
      .fresh_empty_definition_identity += ":other";
  require_throw([&] {
    perturbed_empty_definition.require_all_collection_outcome_facts();
  });
  auto perturbed_collection_error = collection_outcomes;
  perturbed_collection_error.collection_outcome_facts.front()
      .error_variant_identity = "MissingKey";
  require_throw([&] {
    perturbed_collection_error.require_all_collection_outcome_facts();
  });
  auto perturbed_collection_type = collection_outcomes;
  perturbed_collection_type.collection_outcome_facts.front().receiver_type =
      Type::builtin("queue", {Type::primitive("int")});
  require_throw([&] {
    perturbed_collection_type.require_all_collection_outcome_facts();
  });
  auto forged_collection_outcome = collection_outcomes;
  forged_collection_outcome.collection_outcome_facts.front().outcome =
      static_cast<TypedCollectionOutcomeFactV1::Outcome>(99);
  forged_collection_outcome.required_collection_outcome_occurrences.front().outcome =
      static_cast<TypedCollectionOutcomeFactV1::Outcome>(99);
  require_throw([&] {
    forged_collection_outcome.require_all_collection_outcome_facts();
  });
  auto forged_collection_operation = collection_outcomes;
  forged_collection_operation.collection_outcome_facts.front().operation =
      static_cast<TypedCollectionOutcomeFactV1::Operation>(99);
  forged_collection_operation.required_collection_outcome_occurrences.front().operation =
      static_cast<TypedCollectionOutcomeFactV1::Operation>(99);
  require_throw([&] {
    forged_collection_operation.require_all_collection_outcome_facts();
  });

  TypedMatchOwnershipRequirementV1 match_req;
  match_req.statement_occurrence_id = 8;
  match_req.est_arena_index = 3;
  match_req.scrutinee_occurrence_id = 21;
  match_req.specialization_identity = "spec:match";
  match_req.specialization_context = "fn:match";
  match_req.scrutinee_binding_identity = "binding:state";
  match_req.scrutinee_name = "state";
  match_req.scrutinee_type = Type::primitive("int");
  match_req.scrutinee_kind = TypedMatchScrutineeKindV1::Enum;
  TypedMatchArmFactV1 match_arm;
  match_arm.case_name = "Active";
  match_arm.payload_binding_names = {"payload"};
  match_arm.payload_binding_identities = {"fn:match@9:payload"};
  match_arm.payload_types = {Type::primitive("string")};
  match_arm.payload_access = Effect::Write;
  match_req.arms = {match_arm};
  TypedMatchOwnershipFactV1 match_fact;
  match_fact.statement_occurrence_id = match_req.statement_occurrence_id;
  match_fact.est_arena_index = match_req.est_arena_index;
  match_fact.scrutinee_occurrence_id = match_req.scrutinee_occurrence_id;
  match_fact.specialization_identity = match_req.specialization_identity;
  match_fact.specialization_context = match_req.specialization_context;
  match_fact.scrutinee_binding_identity = match_req.scrutinee_binding_identity;
  match_fact.scrutinee_name = match_req.scrutinee_name;
  match_fact.scrutinee_type = match_req.scrutinee_type;
  match_fact.scrutinee_kind = match_req.scrutinee_kind;
  match_fact.arms = match_req.arms;
  match_fact.availability =
      TypedMatchOwnershipFactV1::Availability::Present;
  TypedSemanticModuleV1 matches;
  matches.required_match_occurrences = {match_req};
  matches.match_ownership_facts = {match_fact};
  const auto* resolved_match = matches.find_match_ownership_fact(match_req);
  assert(resolved_match && resolved_match->is_authoritative());
  auto reject_perturbed_match_arm = [&](const auto& mutate) {
    auto corrupted = matches;
    mutate(corrupted.match_ownership_facts.front().arms.front());
    require_throw([&] {
      corrupted.find_match_ownership_fact(match_req);
    });
  };
  reject_perturbed_match_arm([](auto& arm) { arm.case_name += "_forged"; });
  reject_perturbed_match_arm(
      [](auto& arm) { arm.payload_binding_names.front() += "_forged"; });
  reject_perturbed_match_arm(
      [](auto& arm) { arm.payload_binding_identities.front() += "_forged"; });
  reject_perturbed_match_arm(
      [](auto& arm) { arm.payload_types.front() = Type::primitive("int"); });
  reject_perturbed_match_arm([](auto& arm) { arm.payload_access = Effect::Read; });
  reject_perturbed_match_arm(
      [](auto& arm) { arm.transfers_ownership = true; });
  auto missing_match_fact = matches;
  missing_match_fact.match_ownership_facts.clear();
  require_throw([&] { missing_match_fact.find_match_ownership_fact(match_req); });
  auto duplicate_match_fact = matches;
  duplicate_match_fact.match_ownership_facts.push_back(match_fact);
  require_throw([&] {
    duplicate_match_fact.find_match_ownership_fact(match_req);
  });
  auto duplicate_match_requirement = matches;
  duplicate_match_requirement.required_match_occurrences.push_back(match_req);
  require_throw([&] {
    duplicate_match_requirement.find_match_ownership_fact(match_req);
  });
  auto forged_ordinary_match_access = matches;
  forged_ordinary_match_access.match_ownership_facts.front().arms.front()
      .payload_access = Effect::Consume;
  forged_ordinary_match_access.match_ownership_facts.front().arms.front()
      .transfers_ownership = true;
  forged_ordinary_match_access.required_match_occurrences.front().arms.front()
      .payload_access = Effect::Consume;
  forged_ordinary_match_access.required_match_occurrences.front().arms.front()
      .transfers_ownership = true;
  require_throw([&] {
    forged_ordinary_match_access.find_match_ownership_fact(
        forged_ordinary_match_access.required_match_occurrences.front());
  });
  auto consuming_match = matches;
  consuming_match.required_match_occurrences.front().is_consuming = true;
  consuming_match.match_ownership_facts.front().is_consuming = true;
  consuming_match.required_match_occurrences.front().arms.front()
      .payload_access = Effect::Consume;
  consuming_match.match_ownership_facts.front().arms.front()
      .payload_access = Effect::Consume;
  consuming_match.required_match_occurrences.front().arms.front()
      .transfers_ownership = true;
  consuming_match.match_ownership_facts.front().arms.front()
      .transfers_ownership = true;
  assert(consuming_match.find_match_ownership_fact(
             consuming_match.required_match_occurrences.front())
             ->is_authoritative());
  consuming_match.match_ownership_facts.front().arms.front()
      .transfers_ownership = false;
  require_throw([&] {
    consuming_match.find_match_ownership_fact(
        consuming_match.required_match_occurrences.front());
  });

  auto map_outcome = queue_outcome;
  map_outcome.operation = TypedCollectionOutcomeFactV1::Operation::MapRemove;
  map_outcome.operation_expression_occurrence_id = 35;
  map_outcome.operation_occurrence_span = {10, 7, 2, 54, 74};
  map_outcome.statement_occurrence_id = 13;
  map_outcome.specialization_identity = "spec:map";
  map_outcome.specialization_context = "fn:map";
  map_outcome.est_fact_context = "ctx:map";
  map_outcome.provenance.line = 10;
  map_outcome.receiver_binding_identity = "binding:m";
  map_outcome.receiver_definition_identity = "definition:m:fresh";
  map_outcome.fresh_empty_definition_identity = "definition:m:fresh";
  map_outcome.receiver_type = Type::builtin(
      "map", {Type::primitive("string"), Type::never()});
  map_outcome.argument_expression_occurrence_id = 36;
  map_outcome.argument_occurrence_span = {10, 16, 2, 63, 71};
  map_outcome.argument_type = Type::primitive("string");
  map_outcome.error_variant_identity = "MissingKey";
  TypedSemanticModuleV1 map_outcomes;
  map_outcomes.required_collection_outcome_occurrences = {map_outcome};
  map_outcomes.collection_outcome_facts = {map_outcome};
  assert(map_outcomes.require_all_collection_outcome_facts().front()
             ->operation == TypedCollectionOutcomeFactV1::Operation::MapRemove);
  auto missing_map_argument = map_outcomes;
  missing_map_argument.collection_outcome_facts.front()
      .argument_expression_occurrence_id = UINT32_MAX;
  require_throw([&] { missing_map_argument.require_all_collection_outcome_facts(); });

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

  auto fresh_req = requirement("fn:inspect<Rebind>", "spec:rebind",
                               "definition:new");
  fresh_req.definition_identity = "definition:new";
  fresh_req.binding_identity = "binding:inspect:alias";
  fresh_req.killed_definition_identity = "definition:borrowed";
  fresh_req.join_definition_identity.clear();
  fresh_req.source_binding_identity.clear();
  fresh_req.source_kind =
      TypedLocalOwnershipFactV1::SourceKind::FreshOwnedValue;
  fresh_req.access = Effect::Read;
  fresh_req.value_type = Type::builtin("vector", {Type::primitive("int")});
  auto fresh_value_fact = fact(fresh_req);
  TypedSemanticModuleV1 fresh_module;
  fresh_module.required_local_ownership_occurrences = {fresh_req};
  fresh_module.local_ownership_facts = {fresh_value_fact};
  fresh_module.local_ownership_specialization_contexts = {
      {fresh_req.specialization_context, fresh_req.specialization_identity}};
  fresh_module.specializations.emplace(fresh_req.specialization_identity,
                                       TypedSpecializationV1{});
  const auto* fresh_found = fresh_module.find_local_ownership_requirement(
      fresh_req.est_arena_index, fresh_req.expression_occurrence_id,
      fresh_req.specialization_context);
  assert(fresh_found);
  const auto& fresh_checked =
      fresh_module.find_local_ownership_fact(*fresh_found);
  assert(fresh_checked.is_authoritative());
  assert(fresh_checked.source_kind ==
         TypedLocalOwnershipFactV1::SourceKind::FreshOwnedValue);
  assert(fresh_checked.source_binding_identity.empty());
  assert(fresh_checked.killed_definition_identity == "definition:borrowed");

  auto dropped_fresh_requirement = fresh_module;
  dropped_fresh_requirement.required_local_ownership_occurrences.clear();
  require_throw([&] {
    dropped_fresh_requirement.find_local_ownership_fact(fresh_req);
  });
  auto dropped_fresh_fact = fresh_module;
  dropped_fresh_fact.local_ownership_facts.clear();
  require_throw([&] {
    dropped_fresh_fact.find_local_ownership_fact(fresh_req);
  });
  auto duplicate_fresh_fact = fresh_module;
  duplicate_fresh_fact.local_ownership_facts.push_back(fresh_value_fact);
  require_throw([&] {
    duplicate_fresh_fact.find_local_ownership_fact(fresh_req);
  });
  auto perturbed_fresh_kill = fresh_module;
  perturbed_fresh_kill.local_ownership_facts[0]
      .killed_definition_identity = "definition:other";
  require_throw([&] {
    perturbed_fresh_kill.find_local_ownership_fact(fresh_req);
  });
  auto perturbed_fresh_type = fresh_module;
  perturbed_fresh_type.local_ownership_facts[0].value_type =
      Type::builtin("vector", {Type::primitive("string")});
  require_throw([&] {
    perturbed_fresh_type.find_local_ownership_fact(fresh_req);
  });
  auto source_forged_fresh_fact = fresh_module;
  source_forged_fresh_fact.local_ownership_facts[0]
      .source_binding_identity = "binding:invented";
  require_throw([&] {
    source_forged_fresh_fact.find_local_ownership_fact(fresh_req);
  });
  auto untyped_fresh_fact = fresh_value_fact;
  untyped_fresh_fact.value_type.reset();
  assert(!untyped_fresh_fact.is_authoritative());

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
