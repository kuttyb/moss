#include <cassert>
#include <stdexcept>
#include <string>
#include "typed_semantic_api.hpp"

using namespace moss;

static TypeRef make_container_type() {
  static auto decl = [] {
    auto d = std::make_shared<NominalDeclaration>();
    d->id = "nominal-template-v1:test:type:Container";
    return d;
  }();
  return Type::nominal_bound(decl, "Container", {});
}

static TypedTakeRequirementV1 make_take_requirement(
    const std::string& context, const std::string& specialization,
    const std::string& root) {
  TypedTakeRequirementV1 result;
  result.est_arena_index = 2;
  result.take_occurrence_id = 42;
  result.place_occurrence_id = 41;
  result.statement_occurrence_id = 10;
  result.specialization_context = context;
  result.specialization_identity = specialization;
  result.est_fact_context = context;
  result.root_binding_identity = "binding:" + root;
  result.root_name = root;
  result.terminal_type = moss::Type::builtin("vector", {moss::Type::primitive("int")});
  result.result_type = result.terminal_type;
  result.replacement = TypedTakeReplacementV1::VectorEmpty;
  result.required_access = Effect::Write;
  result.transfers_original_value = true;
  result.exclusive_projection_access = true;
  TypedTakeProjectionV1 proj;
  proj.kind = TypedTakeProjectionKindV1::RecordField;
  proj.expression_occurrence_id = 41;
  proj.selector_occurrence_id = UINT32_MAX;
  proj.ordinal = 0;
  proj.input_type = make_container_type();
  proj.output_type = result.terminal_type;
  proj.semantic_identity = "record:Container:field:items@5";
  result.projection_path.push_back(proj);
  return result;
}

static TypedTakeFactV1 make_take_fact(
    const TypedTakeRequirementV1& req) {
  TypedTakeFactV1 result;
  result.est_arena_index = req.est_arena_index;
  result.take_occurrence_id = req.take_occurrence_id;
  result.place_occurrence_id = req.place_occurrence_id;
  result.statement_occurrence_id = req.statement_occurrence_id;
  result.specialization_context = req.specialization_context;
  result.specialization_identity = req.specialization_identity;
  result.est_fact_context = req.est_fact_context;
  result.root_binding_identity = req.root_binding_identity;
  result.root_name = req.root_name;
  result.terminal_type = req.terminal_type;
  result.result_type = req.result_type;
  result.projection_path = req.projection_path;
  result.replacement = req.replacement;
  result.required_access = req.required_access;
  result.transfers_original_value = req.transfers_original_value;
  result.exclusive_projection_access = req.exclusive_projection_access;
  result.availability = TypedTakeFactV1::Availability::Present;
  return result;
}

int main() {
  const auto first = make_take_requirement("fn:extract<BoxA>", "spec:box_a", "c");
  const auto second = make_take_requirement("fn:extract<BoxB>", "spec:box_b", "c");

  TypedSemanticModuleV1 module;
  module.required_take_occurrences = {first, second};
  module.take_facts = {make_take_fact(first), make_take_fact(second)};
  module.local_ownership_specialization_contexts = {
      {first.specialization_context, first.specialization_identity},
      {second.specialization_context, second.specialization_identity}};
  module.specializations.emplace(first.specialization_identity, TypedSpecializationV1{});
  module.specializations.emplace(second.specialization_identity, TypedSpecializationV1{});

  // 1. Positive multi-specialization lookup: both succeed with exact identity
  // 1. Positive multi-specialization lookup: both succeed with exact identity
  const auto& first_req = module.find_take_requirement(
      2, 42, "fn:extract<BoxA>", "spec:box_a");
  const auto& second_req = module.find_take_requirement(
      2, 42, "fn:extract<BoxB>", "spec:box_b");
  assert(first_req.specialization_identity == "spec:box_a");
  assert(second_req.specialization_identity == "spec:box_b");

  const auto* first_fact = module.find_take_fact(first_req);
  const auto* second_fact = module.find_take_fact(second_req);
  assert(first_fact && first_fact->specialization_identity == "spec:box_a");
  assert(second_fact && second_fact->specialization_identity == "spec:box_b");

  auto require_throw = [](auto&& operation) {
    bool threw = false;
    try { operation(); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
  };

  // 2. Mismatched specialization identity: looking up first with second's identity fails closed
  require_throw([&] {
    module.find_take_requirement(2, 42, "fn:extract<BoxA>", "spec:box_b");
  });

  // 3. Targeted corruption: perturb specialization_identity of first only
  {
    auto perturbed = module;
    perturbed.take_facts[0].specialization_identity = "spec:perturbed";
    // Corrupted first instance fails closed:
    require_throw([&] { perturbed.find_take_fact(first_req); });
    // Preserved second instance continues to succeed:
    const auto* preserved_second = perturbed.find_take_fact(second_req);
    assert(preserved_second && preserved_second->specialization_identity == "spec:box_b");
  }

  // 4. Targeted corruption: perturb required access of first only (Write -> Read)
  {
    auto perturbed = module;
    perturbed.take_facts[0].required_access = Effect::Read;
    // Corrupted first instance fails closed:
    require_throw([&] { perturbed.find_take_fact(first_req); });
    // Preserved second instance continues to succeed:
    const auto* preserved_second = perturbed.find_take_fact(second_req);
    assert(preserved_second && preserved_second->required_access == Effect::Write);
  }

  // 5. Targeted corruption: delete fact for first only
  {
    auto missing = module;
    missing.take_facts.erase(missing.take_facts.begin());
    // Corrupted first instance fails closed:
    require_throw([&] { missing.find_take_fact(first_req); });
    // Preserved second instance continues to succeed:
    const auto* preserved_second = missing.find_take_fact(second_req);
    assert(preserved_second && preserved_second->specialization_identity == "spec:box_b");
  }

  // 6. Targeted corruption: duplicate requirement for first only
  {
    auto dup = module;
    dup.required_take_occurrences.push_back(first);
    // Duplicate first requirement fails closed:
    require_throw([&] { dup.find_take_requirement(2, 42, "fn:extract<BoxA>", "spec:box_a"); });
    // Preserved second requirement continues to succeed:
    const auto& preserved_second = dup.find_take_requirement(2, 42, "fn:extract<BoxB>", "spec:box_b");
    assert(preserved_second.specialization_identity == "spec:box_b");
  }

  // 7. Targeted corruption: perturb mapping context for first only
  {
    auto perturbed_map = module;
    perturbed_map.local_ownership_specialization_contexts["fn:extract<BoxA>"] = "spec:wrong";
    // Corrupted first mapping fails closed:
    require_throw([&] { perturbed_map.find_take_fact(first_req); });
    // Preserved second instance continues to succeed:
    const auto* preserved_second = perturbed_map.find_take_fact(second_req);
    assert(preserved_second && preserved_second->specialization_identity == "spec:box_b");
  }

  return 0;
}
