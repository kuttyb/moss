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

moss::checked_ir::Expr parameter(moss::TypeRef type, std::uint32_t ordinal) {
  moss::checked_ir::Expr expression;
  expression.kind = moss::checked_ir::ExprKind::Parameter;
  expression.type = std::move(type);
  expression.ordinal = ordinal;
  return expression;
}

moss::checked_ir::Expr integer(std::string value) {
  moss::checked_ir::Expr expression;
  expression.kind = moss::checked_ir::ExprKind::IntegerLiteral;
  expression.type = moss::Type::primitive("int");
  expression.literal_value = std::move(value);
  return expression;
}

moss::checked_ir::Expr binary(moss::TypeRef type,
                              moss::checked_ir::BinaryOp operation,
                              moss::checked_ir::Expr left,
                              moss::checked_ir::Expr right) {
  moss::checked_ir::Expr expression;
  expression.kind = moss::checked_ir::ExprKind::Binary;
  expression.type = std::move(type);
  expression.binary_op = operation;
  expression.operands = {std::move(left), std::move(right)};
  return expression;
}

moss::abi_v10::WireType bound_zero() {
  moss::abi_v10::WireType type;
  type.kind = moss::abi_v10::WireTypeKind::BoundVar;
  type.ordinal = 0;
  return type;
}

moss::abi_v10::WireType primitive(std::string name) {
  moss::abi_v10::WireType type;
  type.kind = moss::abi_v10::WireTypeKind::Primitive;
  type.identity = std::move(name);
  return type;
}

moss::abi_v10::SourceProvenance add_provenance(
    moss::abi_v10::Interface& interface, const std::string& module_id,
    const std::string& declaration_id, std::uint32_t line) {
  moss::abi_v10::SourceProvenance provenance;
  provenance.module_id = module_id;
  provenance.logical_source_id = moss::abi_v10::hash(
      "moss-abi-v10-logical-source", module_id + "\nmain.moss");
  provenance.source_file_display = "main.moss";
  provenance.start_line = line;
  provenance.start_column = 1;
  provenance.end_line = line;
  provenance.end_column = 1;
  provenance.declaration_id = declaration_id;
  provenance.provenance_id = moss::abi_v10::hash(
      "moss-abi-v10-provenance", declaration_id + "\n" +
          provenance.logical_source_id + "\n" + std::to_string(line));
  const auto record = moss::abi_v10::provenance_record(provenance);
  const auto existing = std::find_if(interface.provenance.begin(),
      interface.provenance.end(), [&](const auto& candidate) {
    return candidate.id == record.id;
  });
  if (existing == interface.provenance.end())
    interface.provenance.push_back(record);
  else
    assert(existing->payload == record.payload);
  return provenance;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3 || argc == 4);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string shape = argc == 4 ? argv[3] : "if-join";
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable = "callable-v1:" + sealed.value.module_id +
      ":fn:fresh";
  const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
  const auto bool_type = moss::Type::primitive("bool");

  std::vector<moss::checked_ir::Stmt> statements;
  if (shape == "if-join" || shape == "if-join-bad-lineage") {
    moss::checked_ir::Stmt join;
    join.kind = moss::checked_ir::StmtKind::IfJoin;
    join.local_ordinal = 0;
    join.type = variable;
    join.expr = parameter(bool_type, 1);
    join.operands = {
        binary(variable, moss::checked_ir::BinaryOp::Mul,
               parameter(variable, 0), integer("2")),
        binary(variable, moss::checked_ir::BinaryOp::Mul,
               parameter(variable, 0), integer("3"))};
    statements.push_back(std::move(join));
  } else if (shape == "if-assign") {
    moss::checked_ir::Stmt initial;
    initial.kind = moss::checked_ir::StmtKind::Var;
    initial.local_ordinal = 0;
    initial.type = variable;
    initial.expr = binary(variable, moss::checked_ir::BinaryOp::Mul,
                          parameter(variable, 0), integer("2"));
    statements.push_back(std::move(initial));
    moss::checked_ir::Stmt conditional;
    conditional.kind = moss::checked_ir::StmtKind::If;
    conditional.expr = parameter(bool_type, 1);
    moss::checked_ir::Stmt assignment;
    assignment.kind = moss::checked_ir::StmtKind::Assign;
    assignment.local_ordinal = 0;
    assignment.expr = binary(variable, moss::checked_ir::BinaryOp::Mul,
                             parameter(variable, 0), integer("3"));
    conditional.then_branch.push_back(std::move(assignment));
    statements.push_back(std::move(conditional));
  } else {
    assert(false && "unknown IfJoin artifact shape");
  }
  moss::checked_ir::Expr local;
  local.kind = moss::checked_ir::ExprKind::LocalRead;
  local.type = variable;
  local.ordinal = 0;
  moss::checked_ir::Stmt returned;
  returned.kind = moss::checked_ir::StmtKind::Return;
  returned.expr = binary(variable, moss::checked_ir::BinaryOp::Add,
                         std::move(local), integer("1"));
  statements.push_back(std::move(returned));
  const auto body_bytes = moss::checked_ir::encode_checked_body_v1(statements);
  const auto body_hash =
      moss::checked_ir::compute_checked_body_hash_v1(statements);

  const std::string callable_provenance = add_provenance(
      sealed.value, sealed.value.module_id, callable, 3).provenance_id;
  moss::abi_v10::Summary summary;
  summary.callable_id = callable;
  summary.variable_count = 1;
  summary.parameters = {bound_zero(), primitive("bool")};
  summary.parameter_access = {moss::abi_v10::Access::Read,
                              moss::abi_v10::Access::Read};
  summary.result = bound_zero();
  summary.body_semantic_hash = body_hash;
  summary.provenance_id = callable_provenance;
  sealed.value.provenance.erase(std::remove_if(
      sealed.value.provenance.begin(), sealed.value.provenance.end(),
      [&](const auto& record) {
    if (record.payload.empty() || record.payload.front() != 1) return false;
    return moss::abi_v10::read_provenance(record.payload).declaration_id.find(
        callable + ":requirement:") == 0;
  }), sealed.value.provenance.end());
  for (std::uint32_t index = 0; index < 3; ++index) {
    moss::abi_v10::Requirement requirement;
    requirement.receiver = bound_zero();
    requirement.operation = index == 2 ? "operator:+" : "operator:*";
    requirement.access = moss::abi_v10::Access::Read;
    requirement.arguments = {primitive("int")};
    requirement.argument_access = {moss::abi_v10::Access::Read};
    requirement.result = bound_zero();
    requirement.provenance_id = add_provenance(sealed.value,
        sealed.value.module_id, callable + ":requirement:" +
            std::to_string(index), 4 + index).provenance_id;
    if (index == 2) {
      requirement.dependencies = shape == "if-join-bad-lineage"
          ? std::vector<std::uint32_t>{1}
          : std::vector<std::uint32_t>{1, 2};
      if (shape == "if-join-bad-lineage") requirement.parent = 1;
    }
    summary.requirements.push_back(std::move(requirement));
  }
  auto remove_existing_callable_records = [&](auto& records) {
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const auto& record) {
      if (record.id == "checked-body-v1:" + callable) return true;
      if (record.payload.empty() ||
          (static_cast<unsigned char>(record.payload.front()) != 1 &&
           static_cast<unsigned char>(record.payload.front()) != 2 &&
           static_cast<unsigned char>(record.payload.front()) != 3) ||
          !moss::abi_v10::hex_hash(record.id)) return false;
      return moss::abi_v10::read_summary(record.payload).callable_id == callable;
    }), records.end());
  };
  remove_existing_callable_records(sealed.value.semantic_contract);
  remove_existing_callable_records(sealed.value.private_semantic_dependencies);
  sealed.value.semantic_contract.push_back(
      moss::abi_v10::summary_record(summary));
  moss::abi_v10::CheckedBodyRecord body;
  body.callable_id = callable;
  body.semantic_hash = body_hash;
  body.checked_ir_bytes = body_bytes;
  sealed.value.private_semantic_dependencies.push_back(
      moss::abi_v10::checked_body_record(body));

  auto sort_records = [](auto& records) {
    std::sort(records.begin(), records.end(), [](const auto& left,
                                                  const auto& right) {
      return left.id < right.id;
    });
  };
  sort_records(sealed.value.semantic_contract);
  sort_records(sealed.value.private_semantic_dependencies);
  sort_records(sealed.value.provenance);
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker("Service", old_digest);
  const auto new_marker = moss::abi_v10::provider_marker("Service", new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, "Service", new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
