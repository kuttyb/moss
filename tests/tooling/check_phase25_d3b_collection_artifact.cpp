#include "../../src/abi_v10_records.hpp"
#include "../../src/checked_body_ir.hpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  assert(input.good() || input.eof());
  const auto sealed = moss::abi_v10::parse(bytes);
  moss::abi_v10::validate_complete_interface(sealed);

  const std::string callable_suffix = ":fn:index_of";
  const moss::abi_v10::Summary* summary = nullptr;
  const moss::abi_v10::CheckedBodyRecord* body = nullptr;
  std::vector<moss::abi_v10::SourceProvenance> provenance;
  for (const auto& record : sealed.value.semantic_contract) {
    if (record.payload.empty() ||
        static_cast<unsigned char>(record.payload.front()) != 2) continue;
    const auto decoded = moss::abi_v10::read_summary(record.payload);
    if (decoded.callable_id.size() >= callable_suffix.size() &&
        decoded.callable_id.compare(decoded.callable_id.size() -
            callable_suffix.size(), callable_suffix.size(), callable_suffix) == 0) {
      assert(summary == nullptr);
      summary = new moss::abi_v10::Summary(decoded);
    }
  }
  for (const auto& record : sealed.value.private_semantic_dependencies) {
    if (record.id.rfind("checked-body-v1:", 0) != 0) continue;
    const auto decoded = moss::abi_v10::read_checked_body_record(record.payload);
    if (decoded.callable_id.size() >= callable_suffix.size() &&
        decoded.callable_id.compare(decoded.callable_id.size() -
            callable_suffix.size(), callable_suffix.size(), callable_suffix) == 0) {
      assert(body == nullptr);
      body = new moss::abi_v10::CheckedBodyRecord(decoded);
    }
  }
  for (const auto& record : sealed.value.provenance)
    provenance.push_back(moss::abi_v10::read_provenance(record.payload));
  assert(summary && body);
  assert(summary->variable_count == 1 && summary->parameters.size() == 2 &&
         summary->parameter_access.size() == 2 &&
         summary->parameter_access[0] == moss::abi_v10::Access::Read &&
         summary->parameter_access[1] == moss::abi_v10::Access::Read);
  assert(summary->parameters[0].kind == moss::abi_v10::WireTypeKind::Builtin &&
         summary->parameters[0].identity == "vector" &&
         summary->parameters[0].arguments.size() == 1 &&
         summary->parameters[0].arguments[0].kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         summary->parameters[0].arguments[0].ordinal == 0 &&
         summary->parameters[1].kind == moss::abi_v10::WireTypeKind::BoundVar &&
         summary->parameters[1].ordinal == 0 &&
         summary->result.kind == moss::abi_v10::WireTypeKind::Primitive &&
         summary->result.identity == "int");
  assert(summary->requirements.size() == 2 && summary->dependencies.empty());
  assert(summary->observable_effect_bits == 0 &&
         summary->observable_effect_tags == std::vector<std::string>{"may_panic"});
  assert(body->callable_id == summary->callable_id &&
         body->semantic_hash == summary->body_semantic_hash &&
         body->dependencies.empty());
  assert(std::count_if(provenance.begin(), provenance.end(), [&](const auto& item) {
    return item.provenance_id == summary->provenance_id &&
        item.declaration_id == summary->callable_id;
  }) == 1);
  for (const auto& requirement : summary->requirements) {
    assert(std::any_of(provenance.begin(), provenance.end(), [&](const auto& item) {
      return item.provenance_id == requirement.provenance_id &&
          !item.declaration_id.empty();
    }));
  }

  const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
  const auto vector_type = moss::Type::builtin("vector", {variable});
  moss::TypeContext context;
  auto decoded_body = moss::checked_ir::decode_collection_loop_checked_body_v1(
      body->checked_ir_bytes, context, {vector_type, variable},
      moss::Type::primitive("int"), body->semantic_hash);
  assert(decoded_body.size() == 4 &&
         decoded_body[0].kind == moss::checked_ir::StmtKind::Var &&
         decoded_body[0].local_ordinal == 0 &&
         decoded_body[1].kind == moss::checked_ir::StmtKind::Var &&
         decoded_body[1].local_ordinal == 1 &&
         decoded_body[2].kind == moss::checked_ir::StmtKind::While &&
         decoded_body[2].body.size() == 2 &&
         decoded_body[2].body[0].kind == moss::checked_ir::StmtKind::If &&
         decoded_body[2].body[0].then_branch.size() == 1 &&
         decoded_body[2].body[0].then_branch[0].kind ==
             moss::checked_ir::StmtKind::Return &&
         decoded_body[2].body[1].kind == moss::checked_ir::StmtKind::Assign &&
         decoded_body[2].body[1].local_ordinal == 0 &&
         decoded_body[3].kind == moss::checked_ir::StmtKind::Return);
  assert(moss::checked_ir::encode_checked_body_v1(decoded_body) ==
         body->checked_ir_bytes);
  delete body;
  delete summary;
  return 0;
}
