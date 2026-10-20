#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2 || (argc == 3 && std::string(argv[2]) == "--poisoned"));
  std::ifstream input(argv[1], std::ios::binary);
  assert(input.good());
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  const auto sealed = moss::abi_v10::decode(bytes);
  moss::abi_v10::validate_complete_interface(sealed);
  const auto& iface = sealed.value;
  const std::string base = "callable-v1:" + iface.module_id + ":fn:";
  const std::string nominal_id =
      "nominal-template-v1:" + iface.module_id + ":type:Box";
  std::map<std::string, moss::abi_v10::Summary> summaries;
  std::map<std::string, moss::abi_v10::CheckedBodyRecord> bodies;
  std::map<std::string, moss::abi_v10::NominalTemplate> templates;
  for (const auto& record : iface.binary_contract)
    if (record.id == nominal_id)
      templates.emplace(record.id, moss::abi_v10::read_template(record.payload));
  for (const auto& record : iface.semantic_contract)
    if (moss::abi_v10::hex_hash(record.id) &&
        !moss::abi_v10::is_key_proof_payload(record.payload)) {
      auto summary = moss::abi_v10::read_summary(record.payload);
      summaries.emplace(summary.callable_id, std::move(summary));
    }
  if (argc == 3) {
    std::size_t remaining = 0;
    for (const auto& record : iface.semantic_contract) {
      std::size_t at = 0;
      while ((at = record.payload.find("generic_stmt ", at)) !=
             std::string::npos) {
        ++remaining;
        at += std::string("generic_stmt ").size();
      }
    }
    assert(remaining == 1);
  }
  for (const auto& record : iface.private_semantic_dependencies) {
    if (record.id.rfind("checked-body-v1:", 0) == 0) {
      auto body = moss::abi_v10::read_checked_body_record(record.payload);
      bodies.emplace(body.callable_id, std::move(body));
    } else if (moss::abi_v10::hex_hash(record.id)) {
      auto summary = moss::abi_v10::read_summary(record.payload);
      summaries.emplace(summary.callable_id, std::move(summary));
    }
  }
  assert(templates.size() == 1 && summaries.size() == 4 &&
         bodies.size() == 4 && iface.provenance.size() == 5);
  const auto& nominal = templates.at(nominal_id);
  assert(nominal.variable_count == 1 && nominal.fields.size() == 1 &&
         nominal.fields[0].name == "value");
  moss::TypeContext context;
  moss::NominalDeclaration declaration;
  declaration.kind = moss::DeclarationKind::Record;
  declaration.program_name = "Service__Box";
  declaration.module_id = iface.module_id;
  declaration.declaration_name = "Box";
  declaration.fields = {{"value", "", true}};
  auto box = context.declare(std::move(declaration));
  assert(box->id == nominal_id);
  auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
  auto result = moss::Type::nominal_bound(box, box->program_name,
                                           {{"value", variable}});
  for (const char* name : {"private_box", "nested_private_box", "fresh"}) {
    const std::string id = base + name;
    const auto& summary = summaries.at(id);
    const auto& body = bodies.at(id);
    assert(summary.variable_count == 1 && summary.parameters.size() == 1 &&
           summary.parameters[0].kind ==
               moss::abi_v10::WireTypeKind::BoundVar &&
           summary.parameters[0].ordinal == 0 &&
           summary.result.kind == moss::abi_v10::WireTypeKind::Nominal &&
           summary.result.identity == nominal_id &&
           summary.result.arguments.size() == 1 &&
           summary.result.arguments[0].ordinal == 0 &&
           summary.body_semantic_hash == body.semantic_hash &&
           body.dependencies.size() == 1);
    auto decoded = moss::checked_ir::decode_h3_checked_body_v1(
        body.checked_ir_bytes, context, {variable}, result,
        body.semantic_hash);
    auto extracted = moss::extract_semantic_dependencies(decoded);
    assert(extracted.size() == 1 &&
           extracted[0].target_id == body.dependencies[0].target_id);
    if (id == base + "private_box") {
      assert(summary.dependencies.empty() &&
             decoded[0].expr->kind ==
                 moss::checked_ir::ExprKind::NominalConstruct &&
             body.dependencies[0].kind ==
                 moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType &&
             body.dependencies[0].target_id == nominal_id &&
             body.dependencies[0].expected_hash ==
                 nominal.representation_hash);
    } else {
      const std::string callee = id == base + "fresh"
          ? base + "nested_private_box" : base + "private_box";
      assert(decoded[0].expr->kind == moss::checked_ir::ExprKind::Call &&
             decoded[0].expr->semantic_target_id == callee &&
             summary.dependencies.size() == 1 &&
             summary.dependencies[0].first == callee &&
             summary.dependencies[0].second ==
                 summaries.at(callee).body_semantic_hash &&
             body.dependencies[0].kind ==
                 moss::abi_v10::CheckedBodyRecord::Dependency::Kind::Callable &&
             body.dependencies[0].target_id == callee &&
             body.dependencies[0].expected_hash ==
                 summaries.at(callee).body_semantic_hash);
    }
  }
  const std::string relay_id = base + "relay";
  const auto& relay_summary = summaries.at(relay_id);
  const auto& relay_body = bodies.at(relay_id);
  assert(relay_summary.variable_count == 1 &&
         relay_summary.parameters.size() == 1 &&
         relay_summary.parameters.front().kind ==
             moss::abi_v10::WireTypeKind::Nominal &&
         relay_summary.parameters.front().identity == nominal_id &&
         relay_summary.parameters.front().arguments.size() == 1 &&
         relay_summary.parameters.front().arguments.front().kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         relay_summary.parameters.front().arguments.front().ordinal == 0 &&
         relay_summary.result.kind == moss::abi_v10::WireTypeKind::BoundVar &&
         relay_summary.result.ordinal == 0 &&
         relay_summary.body_semantic_hash == relay_body.semantic_hash &&
         relay_body.dependencies.size() == 1 &&
         relay_body.dependencies.front().kind ==
             moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType &&
         relay_body.dependencies.front().target_id == nominal_id &&
         relay_body.dependencies.front().expected_hash ==
             nominal.representation_hash);
  auto relay_decoded = moss::checked_ir::decode_h3_checked_body_v1(
      relay_body.checked_ir_bytes, context, {result}, variable,
      relay_body.semantic_hash);
  assert(relay_decoded.size() == 1 && relay_decoded.front().expr &&
         relay_decoded.front().expr->kind ==
             moss::checked_ir::ExprKind::Field &&
         relay_decoded.front().expr->semantic_target_id == nominal_id &&
         relay_decoded.front().expr->member_ordinal == 0 &&
         relay_decoded.front().expr->type->structurally_equal(*variable));
  std::cout << "H3 checked private chain records: PASS\n";
}
