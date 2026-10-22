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
moss::TypeRef from_wire(const moss::abi_v10::WireType& wire) {
  using moss::abi_v10::WireTypeKind;
  switch (wire.kind) {
    case WireTypeKind::Primitive: return moss::Type::primitive(wire.identity);
    case WireTypeKind::Builtin: {
      std::vector<moss::TypeRef> arguments;
      for (const auto& argument : wire.arguments) {
        auto type = from_wire(argument);
        if (!type) return nullptr;
        arguments.push_back(std::move(type));
      }
      return moss::Type::builtin(wire.identity, std::move(arguments));
    }
    case WireTypeKind::BoundVar:
      return moss::Type::var({0, 0, wire.ordinal});
    case WireTypeKind::Unit: return moss::Type::unit();
    case WireTypeKind::Nominal:
    case WireTypeKind::Enum:
    case WireTypeKind::Domain: return nullptr;
  }
  return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 5);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string module_name = argv[4];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  const std::string callable_id =
      "callable-v1:" + sealed.value.module_id + ":fn:size";
  auto& public_records = sealed.value.semantic_contract;
  auto summary_at = std::find_if(public_records.begin(), public_records.end(),
      [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_at != public_records.end());
  auto summary = moss::abi_v10::read_summary(summary_at->payload);
  assert(summary.requirements.size() == 1);

  auto& private_records = sealed.value.private_semantic_dependencies;
  const std::string body_id = "checked-body-v1:" + callable_id;
  auto body_at = std::find_if(private_records.begin(), private_records.end(),
      [&](const auto& record) { return record.id == body_id; });
  assert(body_at != private_records.end());
  auto body = moss::abi_v10::read_checked_body_record(body_at->payload);

  if (mode == "member-identity" || mode == "summary-member-identity" ||
      mode == "ordering") {
    moss::TypeContext context;
    std::vector<moss::TypeRef> parameters;
    for (const auto& type : summary.parameters)
      parameters.push_back(from_wire(type));
    auto result = from_wire(summary.result);
    assert(result && std::all_of(parameters.begin(), parameters.end(),
                                 [](const auto& type) { return !!type; }));
    auto statements = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, context, parameters, result, body.semantic_hash);
    assert(statements.size() == 1 && statements.front().expr &&
           statements.front().expr->kind ==
               moss::checked_ir::ExprKind::MemberCall);
    if (mode == "member-identity") {
      statements.front().expr->member_name = "count";
    } else if (mode == "summary-member-identity") {
      auto& expression = *statements.front().expr;
      const auto forged_identity = callable_id + ":member:1";
      expression.semantic_target_id =
          std::string(moss::checked_ir::kMemberRequirementPrefix) +
          forged_identity;
      expression.member_ordinal = 1;
      expression.member_name = "forged";
      summary.requirements.front().operation = "member-v1:forged";
      auto provenance_at = std::find_if(sealed.value.provenance.begin(),
          sealed.value.provenance.end(), [&](const auto& record) {
        return record.id == summary.requirements.front().provenance_id;
      });
      assert(provenance_at != sealed.value.provenance.end());
      auto provenance = moss::abi_v10::read_provenance(provenance_at->payload);
      provenance.declaration_id = forged_identity;
      *provenance_at = moss::abi_v10::provenance_record(provenance);
    } else {
      statements.front().expr->member_ordinal = 1;
    }
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
  } else if (mode == "receiver-type") {
    summary.requirements.front().receiver = {
        moss::abi_v10::WireTypeKind::Primitive, "int", {}, 0};
  } else if (mode == "result-type") {
    summary.requirements.front().result =
        moss::abi_v10::WireType{moss::abi_v10::WireTypeKind::Primitive,
                                "bool", {}, 0};
  } else if (mode == "access") {
    summary.requirements.front().access = moss::abi_v10::Access::Write;
  } else if (mode == "effect") {
    summary.observable_effect_tags = {"member-dispatch-v1:forged"};
  } else if (mode == "duplicate-requirement") {
    summary.requirements.push_back(summary.requirements.front());
  } else {
    assert(false && "unknown member mutation");
  }

  public_records.erase(std::remove_if(public_records.begin(), public_records.end(),
      [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  }), public_records.end());
  public_records.push_back(moss::abi_v10::summary_record(summary));
  private_records.erase(std::remove_if(private_records.begin(), private_records.end(),
      [&](const auto& record) { return record.id == body_id; }),
      private_records.end());
  private_records.push_back(moss::abi_v10::checked_body_record(body));
  std::sort(public_records.begin(), public_records.end(), [](const auto& left,
                                                              const auto& right) {
    return left.id < right.id;
  });
  std::sort(private_records.begin(), private_records.end(), [](const auto& left,
                                                                const auto& right) {
    return left.id < right.id;
  });

  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(module_name, old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(module_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, module_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
