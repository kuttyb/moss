#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.good());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 5);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string provider_name = argv[4];
  const bool while_mutation = mode.rfind("while-", 0) == 0;
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      ":fn:" + (while_mutation ? "count_up" : "magnitude");
  auto summary_position = std::find_if(
      sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_position != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_position->payload);
  auto body_position = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable_id;
  });
  assert(body_position != sealed.value.private_semantic_dependencies.end());
  auto body = moss::abi_v10::read_checked_body_record(body_position->payload);

  if (mode == "operator-result") {
    assert(summary.requirements.size() == 2);
    auto& subtraction = summary.requirements[1];
    assert(subtraction.operation == "operator:-" && subtraction.result);
    subtraction.result = moss::abi_v10::WireType{};
    subtraction.result->kind = moss::abi_v10::WireTypeKind::Primitive;
    subtraction.result->identity = "int";
  } else if (mode == "operator-operand") {
    assert(summary.requirements.size() == 2);
    auto& subtraction = summary.requirements[1];
    assert(subtraction.arguments.size() == 1);
    subtraction.arguments.front() = moss::abi_v10::WireType{};
    subtraction.arguments.front().kind =
        moss::abi_v10::WireTypeKind::Primitive;
    subtraction.arguments.front().identity = "bool";
  } else if (while_mutation) {
    if (mode == "while-requirement-result") {
      assert(summary.requirements.size() == 1 &&
             summary.requirements.front().result);
      summary.requirements.front().result = moss::abi_v10::WireType{};
      summary.requirements.front().result->kind =
          moss::abi_v10::WireTypeKind::Primitive;
      summary.requirements.front().result->identity = "int";
    } else {
      const auto type = moss::Type::var({0, 0, 0});
      const auto integer = moss::Type::primitive("int");
      moss::TypeContext context;
      auto statements = moss::checked_ir::decode_checked_body_v1(
          body.checked_ir_bytes, context, {type}, integer, body.semantic_hash);
      assert(statements.size() == 4 &&
             statements[2].kind == moss::checked_ir::StmtKind::While &&
             statements[2].body.size() == 1 &&
             statements[2].body.front().kind ==
                 moss::checked_ir::StmtKind::Assign);
      if (mode == "while-condition") {
      assert(statements[2].expr->kind == moss::checked_ir::ExprKind::Binary &&
             statements[2].expr->binary_op ==
                 moss::checked_ir::BinaryOp::Less);
        statements[2].expr->binary_op = moss::checked_ir::BinaryOp::LessEqual;
      } else if (mode == "while-assignment-target") {
        statements[2].body.front().local_ordinal = 0;
      } else {
        assert(false && "unknown while mutation mode");
      }
      body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
      body.semantic_hash = moss::checked_ir::compute_checked_body_hash_v1(statements);
      summary.body_semantic_hash = body.semantic_hash;
      *body_position = moss::abi_v10::checked_body_record(body);
    }
  } else {
    const auto type = moss::Type::var({0, 0, 0});
    moss::TypeContext context;
    auto statements = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, context, {type}, type, body.semantic_hash);
    assert(statements.size() == 1 &&
           statements.front().kind == moss::checked_ir::StmtKind::If &&
           statements.front().then_branch.size() == 1 &&
           statements.front().else_branch.size() == 1);
    if (mode == "branch-return-identity") {
      auto& returned = *statements.front().else_branch.front().expr;
      assert(returned.kind == moss::checked_ir::ExprKind::Parameter);
      returned.ordinal = 1;
    } else if (mode == "return-result") {
      auto& returned = statements.front().then_branch.front();
      const auto integer = moss::Type::primitive("int");
      returned.type = integer;
      returned.expr->type = integer;
    } else if (mode == "numeric-semantics") {
      auto& operation = *statements.front().then_branch.front().expr;
      assert(operation.kind == moss::checked_ir::ExprKind::Binary &&
             operation.binary_op == moss::checked_ir::BinaryOp::Sub);
      operation.binary_op = moss::checked_ir::BinaryOp::Add;
    } else {
      assert(false && "unknown numeric mutation mode");
    }
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash = moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    *body_position = moss::abi_v10::checked_body_record(body);
  }
  *summary_position = moss::abi_v10::summary_record(summary);
  std::sort(sealed.value.semantic_contract.begin(),
            sealed.value.semantic_contract.end(), [](const auto& left,
                                                       const auto& right) {
    return left.id < right.id;
  });
  std::sort(sealed.value.private_semantic_dependencies.begin(),
            sealed.value.private_semantic_dependencies.end(),
            [](const auto& left, const auto& right) {
    return left.id < right.id;
  });
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(provider_name,
                                                          old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(provider_name,
                                                          new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, provider_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
