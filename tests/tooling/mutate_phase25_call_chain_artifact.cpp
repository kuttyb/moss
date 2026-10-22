#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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
moss::TypeRef type_from_wire(const moss::abi_v10::WireType& wire) {
  using moss::abi_v10::WireTypeKind;
  switch (wire.kind) {
    case WireTypeKind::Primitive:
      return moss::Type::primitive(wire.identity);
    case WireTypeKind::Builtin: {
      std::vector<moss::TypeRef> arguments;
      for (const auto& argument : wire.arguments) {
        auto type = type_from_wire(argument);
        if (!type) return nullptr;
        arguments.push_back(std::move(type));
      }
      return moss::Type::builtin(wire.identity, std::move(arguments));
    }
    case WireTypeKind::BoundVar:
      return moss::Type::var({0, 0, wire.ordinal});
    case WireTypeKind::Unit:
      return moss::Type::unit();
    case WireTypeKind::Nominal:
    case WireTypeKind::Enum:
    case WireTypeKind::Domain:
      return nullptr;
  }
  return nullptr;
}
void rewrite_expr(moss::checked_ir::Expr& expression,
                  const std::function<void(moss::checked_ir::Expr&)>& rewrite) {
  rewrite(expression);
  for (auto& child : expression.operands) rewrite_expr(child, rewrite);
}
void rewrite_block(std::vector<moss::checked_ir::Stmt>& statements,
                   const std::function<void(moss::checked_ir::Expr&)>& rewrite) {
  for (auto& statement : statements) {
    if (statement.expr) rewrite_expr(*statement.expr, rewrite);
    rewrite_block(statement.body, rewrite);
    rewrite_block(statement.then_branch, rewrite);
    rewrite_block(statement.else_branch, rewrite);
  }
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
  const auto callable_id = [&](const std::string& name) {
    return "callable-v1:" + sealed.value.module_id + ":fn:" + name;
  };
  const auto summary_position = [&](const std::string& name) {
    auto& records = sealed.value.private_semantic_dependencies;
    return std::find_if(records.begin(), records.end(), [&](const auto& record) {
      return moss::abi_v10::hex_hash(record.id) &&
          moss::abi_v10::typed_summary_payload(record.payload) &&
          moss::abi_v10::read_summary(record.payload).callable_id ==
              callable_id(name);
    });
  };
  const auto body_position = [&](const std::string& name) {
    auto& records = sealed.value.private_semantic_dependencies;
    return std::find_if(records.begin(), records.end(), [&](const auto& record) {
      return record.id == "checked-body-v1:" + callable_id(name);
    });
  };
  auto summary_at = summary_position("middle");
  auto body_at = body_position("middle");
  assert(summary_at != sealed.value.private_semantic_dependencies.end());
  assert(body_at != sealed.value.private_semantic_dependencies.end());
  auto summary = moss::abi_v10::read_summary(summary_at->payload);
  auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
  auto save_summary = [&] {
    auto& records = sealed.value.private_semantic_dependencies;
    const auto current = summary_position("middle");
    assert(current != records.end());
    records.erase(current);
    records.push_back(moss::abi_v10::summary_record(summary));
  };
  auto save_body = [&] {
    auto& records = sealed.value.private_semantic_dependencies;
    const auto current = body_position("middle");
    assert(current != records.end());
    records.erase(current);
    records.push_back(moss::abi_v10::checked_body_record(body));
  };
  auto sort_records = [](auto& records) {
    std::sort(records.begin(), records.end(), [](const auto& left,
                                                  const auto& right) {
      return left.id < right.id;
    });
  };
  auto reseal_relay_to_middle = [&] {
    auto& records = sealed.value.private_semantic_dependencies;
    const auto middle_body_at = body_position("middle");
    if (middle_body_at == records.end()) return;
    const auto middle_body =
        moss::abi_v10::read_checked_body_record(middle_body_at->payload);
    auto& public_records = sealed.value.semantic_contract;
    const auto relay_summary_at = std::find_if(
        public_records.begin(), public_records.end(), [&](const auto& record) {
      return moss::abi_v10::hex_hash(record.id) &&
          moss::abi_v10::typed_summary_payload(record.payload) &&
          moss::abi_v10::read_summary(record.payload).callable_id ==
              callable_id("relay");
    });
    const auto relay_body_at = body_position("relay");
    assert(relay_summary_at != public_records.end() &&
           relay_body_at != records.end());
    auto relay_summary = moss::abi_v10::read_summary(
        relay_summary_at->payload);
    auto relay_body = moss::abi_v10::read_checked_body_record(
        relay_body_at->payload);
    bool summary_changed = false;
    for (auto& dependency : relay_summary.dependencies) {
      if (dependency.first == callable_id("middle")) {
        dependency.second = middle_body.semantic_hash;
        summary_changed = true;
      }
    }
    bool body_changed = false;
    for (auto& dependency : relay_body.dependencies) {
      if (dependency.target_id == callable_id("middle")) {
        dependency.expected_hash = middle_body.semantic_hash;
        body_changed = true;
      }
    }
    assert(summary_changed && body_changed);
    public_records.erase(std::remove_if(public_records.begin(), public_records.end(),
        [&](const moss::abi_v10::Record& record) {
      return moss::abi_v10::hex_hash(record.id) &&
           moss::abi_v10::typed_summary_payload(record.payload) &&
           moss::abi_v10::read_summary(record.payload).callable_id ==
               callable_id("relay");
    }), public_records.end());
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const moss::abi_v10::Record& record) {
      return record.id == "checked-body-v1:" + callable_id("relay");
    }), records.end());
    public_records.push_back(moss::abi_v10::summary_record(relay_summary));
    records.push_back(moss::abi_v10::checked_body_record(relay_body));
  };

  try {
    if (mode == "forged-intermediate-identity") {
      moss::TypeContext context;
      std::vector<moss::TypeRef> parameters;
      for (const auto& wire : summary.parameters)
        parameters.push_back(type_from_wire(wire));
      const auto result = type_from_wire(summary.result);
      assert(result && std::all_of(parameters.begin(), parameters.end(),
                                   [](const auto& item) { return !!item; }));
      auto statements = moss::checked_ir::decode_checked_body_v1(
          body.checked_ir_bytes, context, parameters, result, body.semantic_hash);
      bool changed = false;
      rewrite_block(statements, [&](auto& expression) {
        if (expression.kind == moss::checked_ir::ExprKind::Call &&
            expression.semantic_target_id == callable_id("fresh")) {
          expression.semantic_target_id += ":forged";
          changed = true;
        }
      });
      assert(changed);
      body.checked_ir_bytes =
          moss::checked_ir::encode_checked_body_v1(statements);
      body.semantic_hash =
          moss::checked_ir::compute_checked_body_hash_v1(statements);
      summary.body_semantic_hash = body.semantic_hash;
      save_summary();
      save_body();
    } else if (mode == "wrong-result-type") {
      moss::TypeContext context;
      std::vector<moss::TypeRef> parameters;
      for (const auto& wire : summary.parameters)
        parameters.push_back(type_from_wire(wire));
      const auto result = type_from_wire(summary.result);
      assert(result && std::all_of(parameters.begin(), parameters.end(),
                                   [](const auto& item) { return !!item; }));
      auto statements = moss::checked_ir::decode_checked_body_v1(
          body.checked_ir_bytes, context, parameters, result, body.semantic_hash);
      bool changed = false;
      rewrite_block(statements, [&](auto& expression) {
        if (expression.kind == moss::checked_ir::ExprKind::Call &&
            expression.semantic_target_id == callable_id("fresh")) {
          expression.type = moss::Type::builtin(
              "vector", {moss::Type::primitive("bool")});
          changed = true;
        }
      });
      assert(changed);
      body.checked_ir_bytes =
          moss::checked_ir::encode_checked_body_v1(statements);
      body.semantic_hash =
          moss::checked_ir::compute_checked_body_hash_v1(statements);
      summary.body_semantic_hash = body.semantic_hash;
      save_summary();
      save_body();
      save_summary();
    } else if (mode == "missing-intermediate-summary" ||
               mode == "missing-deep-summary") {
      const std::string target = mode == "missing-intermediate-summary"
          ? "middle" : "fresh";
      auto& records = sealed.value.private_semantic_dependencies;
      const auto found = summary_position(target);
      assert(found != records.end());
      records.erase(found);
    } else if (mode == "missing-intermediate-body" ||
               mode == "missing-deep-body") {
      const std::string target = mode == "missing-intermediate-body"
          ? "middle" : "fresh";
      auto& records = sealed.value.private_semantic_dependencies;
      const auto found = body_position(target);
      assert(found != records.end());
      records.erase(found);
    } else if (mode == "missing-dependency") {
      summary.dependencies.clear();
      body.dependencies.clear();
      save_summary();
      save_body();
    } else if (mode == "dependency-hash") {
      assert(summary.dependencies.size() == 1 && body.dependencies.size() == 1);
      const auto forged = "body-semantic-v1:sha256:" + std::string(64, '7');
      summary.dependencies.front().second = forged;
      body.dependencies.front().expected_hash = forged;
      save_summary();
      save_body();
    } else if (mode == "duplicate-summary") {
      auto duplicate = summary;
      duplicate.body_semantic_hash =
          "body-semantic-v1:sha256:" + std::string(64, '8');
      sealed.value.private_semantic_dependencies.push_back(
          moss::abi_v10::summary_record(duplicate));
    } else if (mode == "duplicate-body") {
      sealed.value.private_semantic_dependencies.push_back(
          moss::abi_v10::checked_body_record(body));
    } else if (mode == "duplicate-dependency") {
      assert(!summary.dependencies.empty());
      summary.dependencies.push_back(summary.dependencies.front());
      (void)moss::abi_v10::summary_record(summary);
      assert(false && "duplicate dependency unexpectedly serialized");
    } else if (mode == "missing-dependency-hash") {
      assert(!summary.dependencies.empty());
      summary.dependencies.front().second.clear();
      save_summary();
    } else {
      assert(false && "unknown call-chain mutation");
    }
    reseal_relay_to_middle();
    sort_records(sealed.value.private_semantic_dependencies);
    const auto rewritten = moss::abi_v10::encode(sealed.value);
    const auto new_digest = moss::abi_v10::parse(rewritten).digest;
    auto archive = read_file(archive_path);
    const auto old_marker = moss::abi_v10::provider_marker(
        provider_name, old_digest);
    const auto new_marker = moss::abi_v10::provider_marker(
        provider_name, new_digest);
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
    std::cout << "mutated " << mode << "\n";
  } catch (const moss::abi_v10::Error&) {
    if (mode == "duplicate-body" || mode == "duplicate-dependency" ||
        mode == "missing-dependency-hash") {
      std::cout << "rejected " << mode << " by canonical ABI-v10 validation\n";
      return 0;
    }
    throw;
  }
}
