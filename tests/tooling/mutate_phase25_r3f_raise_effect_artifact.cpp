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
  assert(argc == 5);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string provider_name = argv[4];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      ":fn:unhandled";
  auto summary_position = std::find_if(
      sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_position != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_position->payload);
  if (mode == "erase-legacy-raise-effect" ||
      mode == "conflict-legacy-raise-effect") {
    std::size_t erased = 0;
    for (auto* records : {&sealed.value.binary_contract,
                          &sealed.value.semantic_contract})
      for (auto& record : *records) {
        if (record.payload.find("unhandled") == std::string::npos) continue;
        auto effect = record.payload.find("raise_set=");
        while (effect != std::string::npos) {
          const auto end = record.payload.find(';', effect);
          assert(end != std::string::npos);
          const auto value_begin = effect + std::string("raise_set=").size();
          if (mode == "erase-legacy-raise-effect") {
            if (record.payload.compare(value_begin, 5, "enum:") != 0) {
              effect = record.payload.find("raise_set=", end + 1);
              continue;
            }
            record.payload.replace(value_begin, end - value_begin, "-");
          } else {
            if (record.payload.compare(value_begin, 1, "-") == 0) {
              record.payload.replace(value_begin, 1,
                  "enum:Service__Failure:Alternate");
            } else {
              auto detailed = record.payload.find("Detailed", value_begin);
              assert(detailed != std::string::npos && detailed < end);
              record.payload.replace(detailed,
                  std::string("Detailed").size(), "Alternate");
            }
          }
          ++erased;
          effect = record.payload.find("raise_set=", effect + 10);
        }
      }
    assert(erased > 0 && "expected legacy effect metadata for unhandled");
  }
  auto body_position = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable_id;
  });
  assert(body_position != sealed.value.private_semantic_dependencies.end());
  auto body = moss::abi_v10::read_checked_body_record(body_position->payload);
  assert(body.dependencies.size() == 1 &&
      body.dependencies.front().kind ==
          moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType);

  constexpr std::string_view type_prefix = "nominal-enum-v1:";
  constexpr std::string_view enum_marker = ":enum:";
  const auto& enum_id = body.dependencies.front().target_id;
  assert(enum_id.rfind(type_prefix, 0) == 0);
  const auto marker = enum_id.find(enum_marker, type_prefix.size());
  assert(marker != std::string::npos);
  const auto module_id = enum_id.substr(type_prefix.size(), marker - type_prefix.size());
  const auto enum_name = enum_id.substr(marker + enum_marker.size());
  auto context = std::make_shared<moss::TypeContext>();
  moss::NominalDeclaration nominal;
  nominal.kind = moss::DeclarationKind::Enum;
  nominal.module_id = module_id;
  nominal.program_name = enum_name;
  nominal.declaration_name = enum_name;
  (void)context->declare(std::move(nominal));
  auto statements = moss::checked_ir::decode_checked_body_v1(
      body.checked_ir_bytes, *context, {moss::Type::primitive("int")},
      moss::Type::unit(), body.semantic_hash, false, false, false, false,
      false, true);
  assert(statements.size() == 1 &&
      statements.front().kind == moss::checked_ir::StmtKind::Raise &&
      statements.front().expr &&
      statements.front().expr->kind ==
          moss::checked_ir::ExprKind::EnumConstruct);

  if (mode == "erase-legacy-raise-effect" ||
      mode == "conflict-legacy-raise-effect") {
    // This mutation deliberately leaves the checked v10 tag and body intact.
  } else if (mode == "missing-raise-effect") {
    summary.observable_effect_tags.clear();
  } else if (mode == "wrong-raise-effect") {
    assert(summary.observable_effect_tags.size() == 1);
    summary.observable_effect_tags.front().replace(
        summary.observable_effect_tags.front().rfind("Detailed"), 8,
        "Alternate");
  } else if (mode == "wrong-raise-variant") {
    auto& raised = statements.front();
    assert(raised.exceptional_alternatives.size() == 1);
    raised.exceptional_alternatives.front().replace(
        raised.exceptional_alternatives.front().rfind("Detailed"), 8,
        "Alternate");
    raised.expr->semantic_target_id.replace(
        raised.expr->semantic_target_id.rfind("Detailed"), 8, "Alternate");
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    *body_position = moss::abi_v10::checked_body_record(body);
  } else if (mode == "wrong-payload-type") {
    statements.front().expr->operands.front().type =
        moss::Type::primitive("float");
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    *body_position = moss::abi_v10::checked_body_record(body);
  } else if (mode == "wrong-branch-identity") {
    statements.front().branch_identity += ":forged";
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    *body_position = moss::abi_v10::checked_body_record(body);
  } else if (mode == "wrong-private-dependency-hash") {
    body.dependencies.front().expected_hash =
        "sha256:" + moss::source_free_contract_sha256("forged enum hash");
    *body_position = moss::abi_v10::checked_body_record(body);
  } else {
    assert(false && "unknown R3F escaping raise mutation mode");
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
