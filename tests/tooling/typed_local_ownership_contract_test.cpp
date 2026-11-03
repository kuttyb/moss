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
  result.access = Effect::Consume;
  result.source_kind = TypedLocalOwnershipFactV1::SourceKind::LocalBinding;
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
  result.source_binding_identity = "parameter:0";
  result.access = req.access;
  result.source_kind = req.source_kind;
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

  auto require_throw = [](auto&& operation) {
    bool threw = false;
    try { operation(); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
  };
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
  return 0;
}
