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
}

int main(int argc, char** argv) {
  assert(argc == 4);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable = "callable-v1:" + sealed.value.module_id +
      ":fn:fresh";
  moss::abi_v10::Summary summary;
  bool found_summary = false;
  for (const auto& record : sealed.value.semantic_contract) {
    if (!moss::abi_v10::hex_hash(record.id) || record.payload.empty() ||
        !moss::abi_v10::typed_summary_payload(record.payload)) continue;
    auto candidate = moss::abi_v10::read_summary(record.payload);
    if (candidate.callable_id == callable) {
      summary = std::move(candidate);
      found_summary = true;
      break;
    }
  }
  assert(found_summary);
  auto body_at = std::find_if(sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable;
  });
  assert(body_at != sealed.value.private_semantic_dependencies.end());

  auto replace_summary = [&] {
    const auto replacement = moss::abi_v10::summary_record(summary);
    auto found = false;
    for (auto& record : sealed.value.semantic_contract) {
      if (!moss::abi_v10::hex_hash(record.id) || record.payload.empty() ||
          !moss::abi_v10::typed_summary_payload(record.payload)) continue;
      if (moss::abi_v10::read_summary(record.payload).callable_id != callable)
        continue;
      record = replacement;
      found = true;
      break;
    }
    assert(found);
  };
  auto replace_body_and_hash = [&](moss::checked_ir::Stmt statement) {
    const std::vector<moss::checked_ir::Stmt> statements{std::move(statement)};
    moss::abi_v10::CheckedBodyRecord body =
        moss::abi_v10::read_checked_body_record(body_at->payload);
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    *body_at = moss::abi_v10::checked_body_record(body);
  };
  auto replace_generic_source = [&](const std::string& replacement) {
    const std::string old = "  generic_source \"provider.moss\"";
    bool found = false;
    for (auto* records : {&sealed.value.binary_contract,
                          &sealed.value.semantic_contract,
                          &sealed.value.private_semantic_dependencies})
      for (auto& record : *records) {
        auto position = record.payload.find(old);
        if (position == std::string::npos) continue;
        record.payload.replace(position, old.size(), replacement);
        found = true;
      }
    assert(found && "legacy generic_source record was not found");
  };

  if (mode == "generic-source-missing") {
    replace_generic_source("");
  } else if (mode == "generic-source-forged") {
    replace_generic_source("  generic_source \"evil.moss\"");
  } else if (mode == "missing-summary") {
    auto& records = sealed.value.semantic_contract;
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const auto& record) {
      return moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
          moss::abi_v10::typed_summary_payload(record.payload) &&
          moss::abi_v10::read_summary(record.payload).callable_id == callable;
    }), records.end());
  } else if (mode == "missing-body") {
    sealed.value.private_semantic_dependencies.erase(body_at);
  } else if (mode == "mismatch-hash") {
    summary.body_semantic_hash = "body-semantic-v1:sha256:" +
        std::string(64, '8');
    replace_summary();
  } else if (mode == "identity") {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    body.callable_id += "-altered";
    *body_at = moss::abi_v10::checked_body_record(body);
  } else if (mode == "invalid-provenance") {
    auto& records = sealed.value.provenance;
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const auto& record) { return record.id == summary.provenance_id; }),
        records.end());
  } else if (mode == "mismatched-callable-provenance") {
    auto& records = sealed.value.provenance;
    auto provenance_at = std::find_if(records.begin(), records.end(),
        [&](const auto& record) { return record.id == summary.provenance_id; });
    assert(provenance_at != records.end());
    auto provenance = moss::abi_v10::read_provenance(provenance_at->payload);
    provenance.declaration_id += ":forged";
    const auto replacement = moss::abi_v10::provenance_record(provenance);
    summary.provenance_id = replacement.id;
    replace_summary();
    *provenance_at = replacement;
  } else if (mode == "unbound") {
    moss::checked_ir::Expr literal;
    literal.kind = moss::checked_ir::ExprKind::VectorLiteral;
    literal.type = moss::Type::builtin("vector", {
        moss::Type::var(moss::TypeVarId{0, 0, 1})});
    moss::checked_ir::Stmt returned;
    returned.kind = moss::checked_ir::StmtKind::Return;
    returned.type = literal.type;
    returned.expr = literal;
    replace_body_and_hash(std::move(returned));
  } else if (mode == "nonempty") {
    moss::checked_ir::Expr child;
    child.kind = moss::checked_ir::ExprKind::IntegerLiteral;
    child.type = moss::Type::primitive("int");
    child.literal_value = "1";
    moss::checked_ir::Expr literal;
    literal.kind = moss::checked_ir::ExprKind::VectorLiteral;
    literal.type = moss::Type::builtin("vector", {
        moss::Type::var(moss::TypeVarId{0, 0, 0})});
    literal.operands.push_back(child);
    moss::checked_ir::Stmt returned;
    returned.kind = moss::checked_ir::StmtKind::Return;
    returned.type = literal.type;
    returned.expr = literal;
    replace_body_and_hash(std::move(returned));
  } else if (mode == "unsupported-shape") {
    moss::checked_ir::Expr scalar;
    scalar.kind = moss::checked_ir::ExprKind::IntegerLiteral;
    scalar.type = moss::Type::primitive("int");
    scalar.literal_value = "1";
    moss::checked_ir::Stmt returned;
    returned.kind = moss::checked_ir::StmtKind::Return;
    returned.type = scalar.type;
    returned.expr = scalar;
    replace_body_and_hash(std::move(returned));
  } else {
    assert(false && "unknown R2c artifact mutation mode");
  }

  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(
      "Provider", old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(
      "Provider", new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, "Provider", new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
