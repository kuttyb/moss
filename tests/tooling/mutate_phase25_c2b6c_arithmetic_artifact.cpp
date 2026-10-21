#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return std::string((std::istreambuf_iterator<char>(input)), {});
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
  const std::string module_name = argv[3];
  const std::string mode = argv[4];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  const std::string callable_id =
      "callable-v1:" + sealed.value.module_id + ":fn:compute";

  if (mode == "unsupported-body") {
    moss::abi_v10::Summary summary;
    bool found_summary = false;
    for (auto& record : sealed.value.semantic_contract) {
      if (!moss::abi_v10::hex_hash(record.id) || record.payload.empty() ||
          !moss::abi_v10::typed_summary_payload(record.payload))
        continue;
      auto candidate = moss::abi_v10::read_summary(record.payload);
      if (candidate.callable_id != callable_id) continue;
      summary = std::move(candidate);
      found_summary = true;
      break;
    }
    assert(found_summary);
    moss::abi_v10::CheckedBodyRecord body;
    bool found_body = false;
    for (auto& record : sealed.value.private_semantic_dependencies) {
      if (record.id != "checked-body-v1:" + callable_id) continue;
      body = moss::abi_v10::read_checked_body_record(record.payload);
      found_body = true;
      break;
    }
    assert(found_body);

    moss::TypeContext context;
    auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
    const auto statements = moss::checked_ir::decode_h3_checked_body_v1(
        body.checked_ir_bytes, context, {variable}, variable,
        body.semantic_hash);
    assert(statements.size() == 1 && statements[0].expr &&
           statements[0].expr->kind == moss::checked_ir::ExprKind::Binary &&
           statements[0].expr->binary_op == moss::checked_ir::BinaryOp::Add);
    auto unsupported = statements;
    auto& multiply = unsupported[0].expr->operands[0];
    assert(multiply.kind == moss::checked_ir::ExprKind::Binary &&
           multiply.binary_op == moss::checked_ir::BinaryOp::Mul);
    multiply.binary_op = moss::checked_ir::BinaryOp::Sub;
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(unsupported);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(unsupported);
    summary.body_semantic_hash = body.semantic_hash;

    auto summary_record = moss::abi_v10::summary_record(summary);
    auto body_record = moss::abi_v10::checked_body_record(body);
    bool replaced_summary = false;
    bool replaced_body = false;
    for (auto& record : sealed.value.semantic_contract)
      if (record.id == summary_record.id ||
          (moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
           moss::abi_v10::typed_summary_payload(record.payload) &&
           moss::abi_v10::read_summary(record.payload).callable_id ==
               callable_id)) {
        record = summary_record;
        replaced_summary = true;
      }
    for (auto& record : sealed.value.private_semantic_dependencies)
      if (record.id == "checked-body-v1:" + callable_id) {
        record = body_record;
        replaced_body = true;
      }
    assert(replaced_summary && replaced_body);
  } else if (mode == "drop-body-alternative-legacy" ||
             mode == "drop-body-no-legacy") {
    auto& records = sealed.value.private_semantic_dependencies;
    const auto before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const moss::abi_v10::Record& record) {
      return record.id == "checked-body-v1:" + callable_id;
    }), records.end());
    assert(before == records.size() + 1);
    if (mode == "drop-body-alternative-legacy") {
      const std::string alternative =
          "  generic_stmt 15 2 0 \"return x * 2 + 21\" "
          "\"x * 2 + 21\" \"\" \"\" 0";
      bool injected = false;
      for (auto& record : sealed.value.semantic_contract) {
        if (record.payload.rfind("export fn compute ", 0) != 0) continue;
        std::istringstream lines(record.payload);
        std::string line, replacement;
        while (std::getline(lines, line)) {
          if (line == "  generic_ir_end") {
            replacement += alternative + "\n";
            injected = true;
          }
          replacement += line + "\n";
        }
        record.payload = std::move(replacement);
      }
      assert(injected);
    }
  } else if (mode == "mismatch-body-hash") {
    bool replaced = false;
    for (auto& record : sealed.value.semantic_contract) {
      if (!moss::abi_v10::hex_hash(record.id) || record.payload.empty() ||
          !moss::abi_v10::typed_summary_payload(record.payload))
        continue;
      auto summary = moss::abi_v10::read_summary(record.payload);
      if (summary.callable_id != callable_id) continue;
      summary.body_semantic_hash =
          "body-semantic-v1:sha256:" + std::string(64, '8');
      record = moss::abi_v10::summary_record(summary);
      replaced = true;
    }
    assert(replaced);
  } else {
    assert(false && "unknown mutation mode");
  }

  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto decoded = moss::abi_v10::decode(rewritten);
  if (mode != "mismatch-body-hash")
    moss::abi_v10::validate_complete_interface(decoded);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker =
      moss::abi_v10::provider_marker(module_name, old_digest);
  const auto new_marker =
      moss::abi_v10::provider_marker(module_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements >= 1);
  moss::abi_v10::validate_selected_rlib(archive, module_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
  std::cout << "mutated checked arithmetic provider artifact: " << mode << '\n';
}
