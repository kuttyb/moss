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

template <typename Records, typename Predicate>
auto find_record(Records& records, Predicate predicate) {
  return std::find_if(records.begin(), records.end(), predicate);
}

moss::abi_v10::Record* find_summary_record(moss::abi_v10::Interface& value,
                                          const std::string& callable_id) {
  for (auto* records : {&value.semantic_contract,
                        &value.private_semantic_dependencies})
    for (auto& record : *records)
      if (moss::abi_v10::hex_hash(record.id) &&
          moss::abi_v10::typed_summary_payload(record.payload) &&
          moss::abi_v10::read_summary(record.payload).callable_id == callable_id)
        return &record;
  return nullptr;
}

moss::abi_v10::Record* find_body_record(moss::abi_v10::Interface& value,
                                       const std::string& callable_id) {
  const std::string id = "checked-body-v1:" + callable_id;
  for (auto* records : {&value.semantic_contract,
                        &value.private_semantic_dependencies})
    for (auto& record : *records)
      if (record.id == id) return &record;
  return nullptr;
}

void reseal_archive(const std::string& archive_path,
                    const std::string& module_name,
                    const std::string& old_digest,
                    const std::string& new_digest) {
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(module_name, old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(module_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + old_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, module_name, new_digest);
  write_file(archive_path, archive);
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
  const std::string root_id = "callable-v1:" + sealed.value.module_id +
      ":fn:invoke_action";
  const std::string leaf_id = "callable-v1:" + sealed.value.module_id +
      ":fn:private_action";
  auto* root_record = find_summary_record(sealed.value, root_id);
  auto* leaf_record = find_summary_record(sealed.value, leaf_id);
  auto* root_body_record = find_body_record(sealed.value, root_id);
  auto* leaf_body_record = find_body_record(sealed.value, leaf_id);
  assert(root_record && leaf_record && root_body_record && leaf_body_record);
  auto root = moss::abi_v10::read_summary(root_record->payload);
  auto leaf = moss::abi_v10::read_summary(leaf_record->payload);
  auto root_body = moss::abi_v10::read_checked_body_record(root_body_record->payload);
  auto leaf_body = moss::abi_v10::read_checked_body_record(leaf_body_record->payload);
  assert(root.requirements.size() == 1 && root.dependencies.size() == 1);
  assert(leaf.requirements.size() == 1 && leaf.requirements.front().operation ==
      root.requirements.front().operation);

  if (mode == "member-identity") {
    moss::TypeContext context;
    std::vector<moss::TypeRef> parameters;
    for (const auto& type : leaf.parameters) {
      assert(type.kind == moss::abi_v10::WireTypeKind::BoundVar);
      parameters.push_back(moss::Type::var({0, 0, type.ordinal}));
    }
    auto result = moss::Type::var({0, 0, leaf.result.ordinal});
    auto statements = moss::checked_ir::decode_checked_body_v1(
        leaf_body.checked_ir_bytes, context, parameters, result,
        leaf_body.semantic_hash);
    assert(statements.size() == 1 && statements.front().expr &&
        statements.front().expr->kind == moss::checked_ir::ExprKind::MemberCall);
    auto& expression = *statements.front().expr;
    expression.semantic_target_id =
        std::string(moss::checked_ir::kMemberRequirementPrefix) +
        leaf_id + ":member:forged";
    expression.member_name = "forged_action";
    leaf_body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    leaf_body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    leaf.body_semantic_hash = leaf_body.semantic_hash;
    root.dependencies.front().second = leaf_body.semantic_hash;
    root_body.dependencies.front().expected_hash = leaf_body.semantic_hash;
  } else if (mode == "result-type") {
    root.variable_count = 1;
    root.result = {moss::abi_v10::WireTypeKind::Primitive, "int", {}, 0};
    root.requirements.front().result = root.result;
  } else if (mode == "effect") {
    root.observable_effect_tags = {"member-dispatch-v1:forged"};
  } else if (mode == "private-dependency") {
    root.dependencies.front().first = root.callable_id;
  } else if (mode == "provenance") {
    auto provenance_at = find_record(sealed.value.provenance,
        [&](const moss::abi_v10::Record& record) {
      return record.id == root.requirements.front().provenance_id;
    });
    assert(provenance_at != sealed.value.provenance.end());
    auto provenance = moss::abi_v10::read_provenance(provenance_at->payload);
    provenance.source_file_display = "forged-provider.moss";
    provenance.logical_source_id = moss::abi_v10::hash(
        "moss-abi-v10-logical-source",
        provenance.module_id + "\n" + provenance.source_file_display);
    provenance.provenance_id = moss::abi_v10::hash(
        "moss-abi-v10-provenance",
        provenance.declaration_id + "\n" + provenance.logical_source_id +
            "\n" + std::to_string(provenance.start_line));
    root.requirements.front().provenance_id = provenance.provenance_id;
    *provenance_at = moss::abi_v10::provenance_record(provenance);
  } else if (mode != "prepare") {
    assert(false && "unknown member relay mutation");
  }

  *root_record = moss::abi_v10::summary_record(root);
  *leaf_record = moss::abi_v10::summary_record(leaf);
  *root_body_record = moss::abi_v10::checked_body_record(root_body);
  *leaf_body_record = moss::abi_v10::checked_body_record(leaf_body);
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  reseal_archive(archive_path, module_name, old_digest, new_digest);
  write_file(interface_path, rewritten);
}
