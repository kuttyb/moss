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
  const std::string old_digest = sealed.digest;
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      ":fn:unwrap";
  auto summary_position = std::find_if(
      sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::hex_hash(record.id) &&
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

  if (mode == "provider-identity") {
    assert(summary.parameters.size() == 2 &&
           summary.parameters[1].kind == moss::abi_v10::WireTypeKind::Enum);
    summary.parameters[1].identity += ":forged";
  } else if (mode == "numeric-semantics") {
    assert(summary.requirements.size() == 1);
    summary.requirements.front().receiver = moss::abi_v10::WireType{};
    summary.requirements.front().receiver.kind =
        moss::abi_v10::WireTypeKind::Primitive;
    summary.requirements.front().receiver.identity = "float";
  } else {
    auto body = moss::abi_v10::read_checked_body_record(body_position->payload);
    const auto& wire_enum = summary.parameters.at(1);
    constexpr std::string_view prefix = "nominal-enum-v1:";
    constexpr std::string_view marker = ":enum:";
    assert(wire_enum.identity.rfind(prefix, 0) == 0);
    const auto marker_at = wire_enum.identity.find(marker, prefix.size());
    assert(marker_at != std::string::npos);
    const std::string module_id = wire_enum.identity.substr(
        prefix.size(), marker_at - prefix.size());
    const std::string enum_name = wire_enum.identity.substr(
        marker_at + marker.size());
    auto context = std::make_shared<moss::TypeContext>();
    moss::NominalDeclaration declaration;
    declaration.kind = moss::DeclarationKind::Enum;
    declaration.program_name = enum_name;
    declaration.module_id = module_id;
    declaration.declaration_name = enum_name;
    const auto enum_declaration = context->declare(std::move(declaration));
    const auto enum_type = moss::Type::enumeration_bound(
        enum_declaration, enum_name);
    const auto generic_type = moss::Type::var(moss::TypeVarId{0, 0, 0});
    auto statements = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, *context, {generic_type, enum_type},
        generic_type, body.semantic_hash, false, false, true);
    assert(statements.size() == 1 &&
           statements.front().kind == moss::checked_ir::StmtKind::Match &&
           statements.front().match_cases.size() == 2);
    auto& match = statements.front();
    auto ready = std::find_if(match.match_cases.begin(), match.match_cases.end(),
        [](const auto& branch) { return !branch.payload_ordinals.empty(); });
    auto empty = std::find_if(match.match_cases.begin(), match.match_cases.end(),
        [](const auto& branch) { return branch.payload_ordinals.empty(); });
    assert(ready != match.match_cases.end() && empty != match.match_cases.end());
    if (mode == "wrong-variant") {
      ready->variant_id += ":forged";
    } else if (mode == "payload-ordinal") {
      ready->payload_ordinals.front() += 3;
    } else if (mode == "payload-type") {
      ready->payload_types.front() = moss::Type::primitive("float");
    } else if (mode == "branch-identity") {
      ready->variant_tag = "NotAProviderVariant";
      ready->variant_id = "nominal-enum-variant-v1:" + enum_declaration->id +
          ":case:" + ready->variant_tag;
    } else if (mode == "return-lineage") {
      auto& expression = *empty->body.front().expr;
      expression.ordinal = 1;
      expression.type = enum_type;
    } else if (mode == "return-result") {
      empty->body.front().expr->type = moss::Type::primitive("int");
    } else {
      assert(false && "unknown R3C Match mutation mode");
    }
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash = moss::checked_ir::compute_checked_body_hash_v1(
        statements);
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
