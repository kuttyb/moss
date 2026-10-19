#include "abi_v10_records.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  assert(input.good());
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  const auto sealed = moss::abi_v10::decode(bytes);
  moss::abi_v10::validate_complete_interface(sealed);

  const auto& iface = sealed.value;
  const std::string nominal_id =
      "nominal-template-v1:" + iface.module_id + ":type:Box";
  const std::string callable_id =
      "callable-v1:" + iface.module_id + ":fn:fresh";
  const moss::abi_v10::NominalTemplate* nominal = nullptr;
  const moss::abi_v10::Summary* summary = nullptr;
  const moss::abi_v10::CheckedBodyRecord* body = nullptr;
  moss::abi_v10::NominalTemplate nominal_value;
  moss::abi_v10::Summary summary_value;
  moss::abi_v10::CheckedBodyRecord body_value;
  for (const auto& record : iface.binary_contract)
    if (record.id == nominal_id) {
      nominal_value = moss::abi_v10::read_template(record.payload);
      nominal = &nominal_value;
    }
  for (const auto& record : iface.semantic_contract)
    if (!record.payload.empty() && record.payload.front() == 1 &&
        moss::abi_v10::hex_hash(record.id)) {
      summary_value = moss::abi_v10::read_summary(record.payload);
      if (summary_value.callable_id == callable_id) summary = &summary_value;
    }
  for (const auto& record : iface.private_semantic_dependencies)
    if (record.id == "checked-body-v1:" + callable_id) {
      body_value = moss::abi_v10::read_checked_body_record(record.payload);
      body = &body_value;
    }
  assert(nominal && summary && body);
  assert(nominal->variable_count == 1 && nominal->fields.size() == 1);
  assert(nominal->fields[0].name == "value" &&
         nominal->fields[0].type.kind == moss::abi_v10::WireTypeKind::BoundVar &&
         nominal->fields[0].type.ordinal == 0);
  assert(summary->variable_count == 1 && summary->parameters.size() == 1 &&
         summary->parameters[0].kind == moss::abi_v10::WireTypeKind::BoundVar &&
         summary->parameters[0].ordinal == 0);
  assert(summary->result.kind == moss::abi_v10::WireTypeKind::Nominal &&
         summary->result.identity == nominal_id &&
         summary->result.arguments.size() == 1 &&
         summary->result.arguments[0].kind ==
             moss::abi_v10::WireTypeKind::BoundVar &&
         summary->result.arguments[0].ordinal == 0);
  assert(body->semantic_hash == summary->body_semantic_hash &&
         body->dependencies.size() == 1 &&
         body->dependencies[0].kind ==
             moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType &&
         body->dependencies[0].target_id == nominal_id &&
         body->dependencies[0].expected_hash == nominal->representation_hash);
  assert(iface.provenance.size() == 2);
  std::cout << "H3 direct checked ABI-v10 records: PASS\n";
}
