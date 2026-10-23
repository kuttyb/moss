#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
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
  assert(argc == 6);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string provider_name = argv[4];
  const std::string callable_name = argv[5];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      ":fn:" + callable_name;
  auto summary_record = std::find_if(
      sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_record != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_record->payload);
  auto body_record = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable_id;
  });
  assert(body_record != sealed.value.private_semantic_dependencies.end());
  auto body = moss::abi_v10::read_checked_body_record(body_record->payload);
  assert(body.dependencies.size() == 1 &&
      body.dependencies.front().kind ==
          moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType);
  constexpr std::string_view type_prefix = "nominal-enum-v1:";
  constexpr std::string_view enum_marker = ":enum:";
  const auto& enum_id = body.dependencies.front().target_id;
  assert(enum_id.rfind(type_prefix, 0) == 0);
  const auto marker = enum_id.find(enum_marker, type_prefix.size());
  assert(marker != std::string::npos);
  const auto module_id = enum_id.substr(type_prefix.size(),
      marker - type_prefix.size());
  const auto enum_name = enum_id.substr(marker + enum_marker.size());
  auto context = std::make_shared<moss::TypeContext>();
  moss::NominalDeclaration nominal;
  nominal.kind = moss::DeclarationKind::Enum;
  nominal.module_id = module_id;
  nominal.program_name = enum_name;
  nominal.declaration_name = enum_name;
  const auto declaration = context->declare(std::move(nominal));
  const auto enum_type = moss::Type::enumeration_bound(declaration, enum_name);
  auto statements = moss::checked_ir::decode_checked_body_v1(
      body.checked_ir_bytes, *context, {moss::Type::primitive("int")},
      moss::Type::primitive("int"), body.semantic_hash, false, false, false,
      false, false, true);
  assert(statements.size() == 1 &&
      statements.front().kind == moss::checked_ir::StmtKind::Try &&
      statements.front().exceptional_cases.size() == 1);
  auto& attempt = statements.front();
  if (mode == "private-enum-id") {
    body.dependencies.front().target_id += ":forged";
  } else if (mode == "recovery-selection") {
    assert(!attempt.exceptional_cases.front().variant_ids.empty());
    attempt.exceptional_cases.front().variant_ids.front() += ":forged";
  } else if (mode == "return-lineage") {
    auto& returned = *attempt.exceptional_cases.front().body.front().expr;
    if (returned.kind == moss::checked_ir::ExprKind::IntegerLiteral) {
      returned.kind = moss::checked_ir::ExprKind::Parameter;
      returned.literal_value.clear();
      returned.ordinal = 0;
    } else {
      assert(returned.kind == moss::checked_ir::ExprKind::LocalRead);
      returned.ordinal += 1;
    }
  } else if (mode == "branch-identity") {
    attempt.exceptional_cases.front().branch_identity += ":forged";
  } else if (mode == "payload-ordinal") {
    assert(!attempt.exceptional_cases.front().payload_ordinals.empty());
    attempt.exceptional_cases.front().payload_ordinals.front() += 1;
  } else if (mode == "payload-type") {
    assert(!attempt.exceptional_cases.front().payload_types.empty());
    attempt.exceptional_cases.front().payload_types.front() =
        moss::Type::primitive("float");
    auto& raised = *attempt.body.front().expr;
    assert(raised.kind == moss::checked_ir::ExprKind::EnumConstruct &&
        !raised.operands.empty());
    raised.operands.front().type = moss::Type::primitive("float");
  } else if (mode == "raise-variant") {
    auto& raised = *(attempt.body.front().kind ==
            moss::checked_ir::StmtKind::Raise
        ? attempt.body.front().expr
        : attempt.body.front().then_branch.front().expr);
    assert(raised.kind == moss::checked_ir::ExprKind::EnumConstruct);
    raised.semantic_target_id += ":forged";
  } else if (mode == "return-result") {
    auto& recovered = attempt.exceptional_cases.front().body.front();
    recovered.expr->type = moss::Type::primitive("float");
    recovered.type = moss::Type::primitive("float");
  } else {
    assert(false && "unknown R3F exception mutation mode");
  }
  body.checked_ir_bytes =
      moss::checked_ir::encode_checked_body_v1(statements);
  body.semantic_hash =
      moss::checked_ir::compute_checked_body_hash_v1(statements);
  summary.body_semantic_hash = body.semantic_hash;
  *body_record = moss::abi_v10::checked_body_record(body);
  *summary_record = moss::abi_v10::summary_record(summary);
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
