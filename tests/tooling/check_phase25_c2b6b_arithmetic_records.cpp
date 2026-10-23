#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  assert(input.good());
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  const auto sealed = moss::abi_v10::decode(bytes);
  moss::abi_v10::validate_complete_interface(sealed);
  const auto& iface = sealed.value;

  const std::string compute_id =
      "callable-v1:" + iface.module_id + ":fn:compute";
  const std::string subtraction_id =
      "callable-v1:" + iface.module_id + ":fn:subtract";
  const std::string reverse_id =
      "callable-v1:" + iface.module_id + ":fn:unsupported_reverse";

  std::vector<moss::abi_v10::Summary> compute_summaries;
  std::vector<moss::abi_v10::Summary> subtraction_summaries;
  std::vector<moss::abi_v10::Summary> reverse_summaries;
  for (const auto& record : iface.semantic_contract) {
    if (record.payload.empty() || !moss::abi_v10::typed_summary_payload(record.payload) ||
        !moss::abi_v10::hex_hash(record.id)) continue;
    moss::abi_v10::validate_summary(record);
    auto summary = moss::abi_v10::read_summary(record.payload);
    if (summary.callable_id == compute_id)
      compute_summaries.push_back(std::move(summary));
    if (summary.callable_id == subtraction_id)
      subtraction_summaries.push_back(std::move(summary));
    if (summary.callable_id == reverse_id)
      reverse_summaries.push_back(std::move(summary));
  }
  assert(compute_summaries.size() == 1);
  assert(subtraction_summaries.size() == 1);
  const auto& summary = compute_summaries.front();
  assert(summary.variable_count == 1 && summary.parameters.size() == 1);
  assert(summary.parameters.front().kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         summary.parameters.front().ordinal == 0);
  assert(summary.parameter_access ==
         std::vector<moss::abi_v10::Access>{moss::abi_v10::Access::Read});
  assert(summary.result.kind == moss::abi_v10::WireTypeKind::BoundVar &&
         summary.result.ordinal == 0);
  assert(summary.requirements.size() == 2);
  for (const auto& requirement : summary.requirements) {
    assert(requirement.receiver.kind == moss::abi_v10::WireTypeKind::BoundVar &&
           requirement.receiver.ordinal == 0);
    assert(requirement.arguments.size() == 1 &&
           requirement.arguments.front().kind ==
               moss::abi_v10::WireTypeKind::Primitive &&
           requirement.arguments.front().identity == "int");
    assert(requirement.result &&
           requirement.result->kind == moss::abi_v10::WireTypeKind::BoundVar &&
           requirement.result->ordinal == 0);
    assert(requirement.access == moss::abi_v10::Access::Read);
    assert(requirement.argument_access ==
           std::vector<moss::abi_v10::Access>{moss::abi_v10::Access::Read});
    assert(!requirement.provenance_id.empty());
  }
  assert(summary.requirements[0].operation == "operator:*" &&
         summary.requirements[0].parent == 0);
  assert(summary.requirements[1].operation == "operator:+" &&
         summary.requirements[1].parent == 1);
  const auto& subtraction_summary = subtraction_summaries.front();
  assert(subtraction_summary.requirements.size() == 2 &&
         subtraction_summary.requirements[0].operation == "operator:*" &&
         subtraction_summary.requirements[0].parent == 0 &&
         subtraction_summary.requirements[1].operation == "operator:-" &&
         subtraction_summary.requirements[1].parent == 1 &&
         subtraction_summary.requirements[1].dependencies ==
             std::vector<std::uint32_t>{1});

  std::vector<moss::abi_v10::CheckedBodyRecord> compute_bodies;
  std::vector<moss::abi_v10::CheckedBodyRecord> subtraction_bodies;
  std::vector<moss::abi_v10::CheckedBodyRecord> reverse_bodies;
  for (const auto& record : iface.private_semantic_dependencies) {
    if (record.id.rfind("checked-body-v1:", 0) != 0) continue;
    moss::abi_v10::validate_checked_body_record(record);
    auto body = moss::abi_v10::read_checked_body_record(record.payload);
    if (body.callable_id == compute_id)
      compute_bodies.push_back(std::move(body));
    if (body.callable_id == subtraction_id)
      subtraction_bodies.push_back(std::move(body));
    if (body.callable_id == reverse_id)
      reverse_bodies.push_back(std::move(body));
  }
  assert(compute_bodies.size() == 1);
  assert(subtraction_bodies.size() == 1);
  const auto& body = compute_bodies.front();
  assert(body.semantic_hash == summary.body_semantic_hash);
  assert(body.dependencies.empty());

  std::map<std::string, std::size_t> provenance_counts;
  for (const auto& record : iface.provenance) {
    moss::abi_v10::validate_provenance(record);
    auto provenance = moss::abi_v10::read_provenance(record.payload);
    ++provenance_counts[provenance.provenance_id];
  }
  assert(provenance_counts[summary.provenance_id] == 1);
  for (const auto& requirement : summary.requirements)
    assert(provenance_counts[requirement.provenance_id] == 1);
  assert(summary.requirements[0].provenance_id !=
         summary.requirements[1].provenance_id);

  moss::TypeContext context;
  auto type_t = moss::Type::var(moss::TypeVarId{0, 0, 0});
  const auto decoded = moss::checked_ir::decode_h3_checked_body_v1(
      body.checked_ir_bytes, context, {type_t}, type_t, body.semantic_hash);
  assert(decoded.size() == 1);
  const auto& returned = decoded.front();
  assert(returned.kind == moss::checked_ir::StmtKind::Return &&
         returned.type->structurally_equal(*type_t) && returned.expr);
  const auto& add = *returned.expr;
  assert(add.kind == moss::checked_ir::ExprKind::Binary &&
         add.binary_op == moss::checked_ir::BinaryOp::Add &&
         add.type->structurally_equal(*type_t) && add.operands.size() == 2);
  const auto& multiply = add.operands.front();
  const auto& add_literal = add.operands.back();
  assert(multiply.kind == moss::checked_ir::ExprKind::Binary &&
         multiply.binary_op == moss::checked_ir::BinaryOp::Mul &&
         multiply.type->structurally_equal(*type_t) &&
         multiply.operands.size() == 2);
  const auto& parameter = multiply.operands.front();
  const auto& multiply_literal = multiply.operands.back();
  assert(parameter.kind == moss::checked_ir::ExprKind::Parameter &&
         parameter.ordinal == 0 &&
         parameter.type->structurally_equal(*type_t));
  for (const auto* literal : {&multiply_literal, &add_literal})
    assert(literal->kind == moss::checked_ir::ExprKind::IntegerLiteral &&
           literal->type->structurally_equal(*moss::Type::primitive("int")));
  assert(multiply_literal.literal_value == "2" &&
         add_literal.literal_value == "20");
  const auto& subtraction_body = subtraction_bodies.front();
  assert(subtraction_body.semantic_hash == subtraction_summary.body_semantic_hash);
  const auto subtraction_decoded = moss::checked_ir::decode_h3_checked_body_v1(
      subtraction_body.checked_ir_bytes, context, {type_t}, type_t,
      subtraction_body.semantic_hash);
  assert(subtraction_decoded.size() == 1 &&
         subtraction_decoded.front().expr &&
         subtraction_decoded.front().expr->kind ==
             moss::checked_ir::ExprKind::Binary &&
         subtraction_decoded.front().expr->binary_op ==
             moss::checked_ir::BinaryOp::Sub &&
         subtraction_decoded.front().expr->operands.size() == 2 &&
         subtraction_decoded.front().expr->operands.front().kind ==
             moss::checked_ir::ExprKind::Binary &&
         subtraction_decoded.front().expr->operands.front().binary_op ==
             moss::checked_ir::BinaryOp::Mul);
  assert(moss::checked_ir::encode_checked_body_v1(decoded) ==
         body.checked_ir_bytes);
  assert(moss::checked_ir::compute_checked_body_hash_v1(decoded) ==
         summary.body_semantic_hash);

  assert(reverse_summaries.size() == 1 && reverse_bodies.size() == 1);
  const auto& reverse_summary = reverse_summaries.front();
  const auto& reverse_body = reverse_bodies.front();
  assert(reverse_summary.requirements.size() == 2);
  assert(reverse_summary.requirements[0].operation == "operator:*" &&
         reverse_summary.requirements[0].parent == 0 &&
         reverse_summary.requirements[0].dependencies.empty());
  assert(reverse_summary.requirements[1].operation == "operator:+" &&
         reverse_summary.requirements[1].parent == 1 &&
         reverse_summary.requirements[1].dependencies ==
             std::vector<std::uint32_t>{1});
  assert(reverse_summary.requirements[1].receiver.kind ==
             moss::abi_v10::WireTypeKind::Primitive &&
         reverse_summary.requirements[1].receiver.identity == "int" &&
         reverse_summary.requirements[1].arguments.size() == 1 &&
         reverse_summary.requirements[1].arguments.front().kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         reverse_summary.requirements[1].arguments.front().ordinal == 0);
  assert(reverse_body.semantic_hash == reverse_summary.body_semantic_hash &&
         reverse_body.dependencies.empty());
  const auto reverse_decoded = moss::checked_ir::decode_h3_checked_body_v1(
      reverse_body.checked_ir_bytes, context, {type_t}, type_t,
      reverse_body.semantic_hash);
  assert(reverse_decoded.size() == 1 &&
         reverse_decoded.front().kind == moss::checked_ir::StmtKind::Return &&
         reverse_decoded.front().expr &&
         reverse_decoded.front().expr->binary_op ==
             moss::checked_ir::BinaryOp::Add);
  const auto& reverse_add = *reverse_decoded.front().expr;
  assert(reverse_add.operands[0].kind ==
             moss::checked_ir::ExprKind::IntegerLiteral &&
         reverse_add.operands[0].literal_value == "20" &&
         reverse_add.operands[0].type->structurally_equal(
             *moss::Type::primitive("int")) &&
         reverse_add.operands[1].kind == moss::checked_ir::ExprKind::Binary &&
         reverse_add.operands[1].binary_op ==
             moss::checked_ir::BinaryOp::Mul &&
         reverse_add.operands[1].type->structurally_equal(*type_t));
  assert(moss::checked_ir::encode_checked_body_v1(reverse_decoded) ==
         reverse_body.checked_ir_bytes);
  assert(moss::checked_ir::compute_checked_body_hash_v1(reverse_decoded) ==
         reverse_summary.body_semantic_hash);

  std::cout << "PASS actual ABI-v10 compute summary/body counts 1/1; hash="
            << body.semantic_hash
            << "; requirement provenance records=2 valid; callable provenance valid; "
               "canonical interface and decoded tree verified; reverse operand order "
               "and T-Int subtraction have checked occurrence lineage\n";
}
