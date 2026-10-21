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
    assert(remaining == 3);
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
  assert(templates.size() == 1 && summaries.size() == 6 &&
         bodies.size() == 6 && iface.provenance.size() == 7);
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
  const std::string identity_id = base + "private_identity";
  const auto& identity_summary = summaries.at(identity_id);
  const auto& identity_body = bodies.at(identity_id);
  assert(identity_summary.variable_count == 1 &&
         identity_summary.parameters.size() == 1 &&
         identity_summary.parameters.front().kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         identity_summary.parameters.front().ordinal == 0 &&
         identity_summary.result.kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         identity_summary.result.ordinal == 0 &&
         identity_summary.body_semantic_hash == identity_body.semantic_hash &&
         identity_body.dependencies.empty());
  const auto identity_decoded = moss::checked_ir::decode_checked_body_v1(
      identity_body.checked_ir_bytes, context, {variable}, variable,
      identity_body.semantic_hash);
  assert(identity_decoded.size() == 1 &&
         identity_decoded.front().kind == moss::checked_ir::StmtKind::Return &&
         identity_decoded.front().expr &&
         identity_decoded.front().expr->kind ==
             moss::checked_ir::ExprKind::Parameter &&
         identity_decoded.front().expr->ordinal == 0 &&
         moss::checked_ir::encode_checked_body_v1(identity_decoded) ==
             identity_body.checked_ir_bytes &&
         moss::checked_ir::compute_checked_body_hash_v1(identity_decoded) ==
             identity_body.semantic_hash);
  const std::string identity_chain_id = base + "identity_chain";
  const auto& identity_chain_summary = summaries.at(identity_chain_id);
  const auto& identity_chain_body = bodies.at(identity_chain_id);
  assert(identity_chain_summary.dependencies.size() == 1 &&
         identity_chain_summary.dependencies.front().first == identity_id &&
         identity_chain_summary.dependencies.front().second ==
             identity_summary.body_semantic_hash &&
         identity_chain_body.dependencies.size() == 1 &&
         identity_chain_body.dependencies.front().kind ==
             moss::abi_v10::CheckedBodyRecord::Dependency::Kind::Callable &&
         identity_chain_body.dependencies.front().target_id == identity_id &&
         identity_chain_body.dependencies.front().expected_hash ==
             identity_summary.body_semantic_hash);
  const auto identity_chain_decoded =
      moss::checked_ir::decode_checked_body_v1(
          identity_chain_body.checked_ir_bytes, context, {variable}, variable,
          identity_chain_body.semantic_hash);
  assert(identity_chain_decoded.size() == 1 &&
         identity_chain_decoded.front().kind ==
             moss::checked_ir::StmtKind::Return &&
         identity_chain_decoded.front().expr &&
         identity_chain_decoded.front().expr->kind ==
             moss::checked_ir::ExprKind::Call &&
         identity_chain_decoded.front().expr->semantic_target_id == identity_id &&
         moss::checked_ir::encode_checked_body_v1(identity_chain_decoded) ==
             identity_chain_body.checked_ir_bytes &&
         moss::checked_ir::compute_checked_body_hash_v1(identity_chain_decoded) ==
             identity_chain_body.semantic_hash);
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
    auto decoded = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, context, {variable}, result,
        body.semantic_hash);
    auto extracted = moss::extract_semantic_dependencies(decoded);
    assert(extracted.size() == 1 &&
           extracted[0].target_id == body.dependencies[0].target_id);
    if (id == base + "private_box") {
      assert(summary.dependencies.empty() &&
             decoded.size() == 2 &&
             decoded[0].kind == moss::checked_ir::StmtKind::Var &&
             decoded[0].expr &&
             decoded[0].expr->kind ==
                 moss::checked_ir::ExprKind::NominalConstruct &&
             decoded[0].expr->operands.size() == 1 &&
             decoded[0].expr->operands[0].kind ==
                 moss::checked_ir::ExprKind::Parameter &&
             decoded[1].kind == moss::checked_ir::StmtKind::Return &&
             body.dependencies[0].kind ==
                 moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType &&
             body.dependencies[0].target_id == nominal_id &&
             body.dependencies[0].expected_hash ==
                 nominal.representation_hash);
    } else {
      const std::string callee = id == base + "fresh"
          ? base + "nested_private_box" : base + "private_box";
      assert(decoded.size() == 2 &&
             decoded[0].kind == moss::checked_ir::StmtKind::Var &&
             decoded[0].expr &&
             decoded[0].expr->kind == moss::checked_ir::ExprKind::Call &&
             decoded[0].expr->semantic_target_id == callee &&
             decoded[1].kind == moss::checked_ir::StmtKind::Return &&
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
  auto relay_decoded = moss::checked_ir::decode_checked_body_v1(
      relay_body.checked_ir_bytes, context, {result}, variable,
      relay_body.semantic_hash);
  assert(relay_decoded.size() == 3 &&
         relay_decoded[0].kind == moss::checked_ir::StmtKind::Var &&
         relay_decoded[0].expr &&
         relay_decoded[0].expr->kind == moss::checked_ir::ExprKind::Field &&
         relay_decoded[0].expr->semantic_target_id == nominal_id &&
         relay_decoded[0].expr->member_ordinal == 0 &&
         relay_decoded[0].expr->type->structurally_equal(*variable) &&
         relay_decoded[1].kind == moss::checked_ir::StmtKind::Var &&
         relay_decoded[1].expr &&
         relay_decoded[1].expr->kind == moss::checked_ir::ExprKind::LocalRead &&
         relay_decoded[2].kind == moss::checked_ir::StmtKind::Return);
  std::cout << "H3 checked private chain records: PASS\n";
}
