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

moss::TypeRef checked_nominal_result(
    const moss::abi_v10::NominalTemplate& nominal,
    const std::shared_ptr<const moss::NominalDeclaration>& declaration,
    moss::TypeRef variable) {
  assert(nominal.fields.size() == 1 &&
         declaration->fields.size() == nominal.fields.size());
  std::vector<std::pair<std::string, moss::TypeRef>> fields;
  fields.emplace_back(declaration->fields[0].name, std::move(variable));
  return moss::Type::nominal_bound(declaration, declaration->program_name,
                                   std::move(fields));
}

void rehash_nominal(moss::abi_v10::NominalTemplate& nominal) {
  moss::abi_v10::WireWriter representation;
  representation.string(nominal.template_id);
  representation.number(nominal.variable_count);
  for (const auto& field : nominal.fields) {
    representation.string(field.name);
    representation.byte(field.exported ? 1 : 0);
    representation.string(moss::abi_v10::type_bytes(
        field.type, nominal.variable_count));
  }
  nominal.representation_hash = moss::abi_v10::hash(
      "moss-abi-v10-nominal-representation", representation.finish());
}

void reseal_nominal_dependencies(
    std::vector<moss::abi_v10::Record>& records,
    const std::string& template_id, const std::string& representation_hash) {
  for (auto& record : records) {
    if (record.id.rfind("checked-body-v1:", 0) != 0) continue;
    auto body = moss::abi_v10::read_checked_body_record(record.payload);
    bool changed = false;
    for (auto& dependency : body.dependencies)
      if (dependency.kind ==
              moss::abi_v10::CheckedBodyRecord::Dependency::Kind::NominalType &&
          dependency.target_id == template_id) {
        dependency.expected_hash = representation_hash;
        changed = true;
      }
    if (changed) record = moss::abi_v10::checked_body_record(body);
  }
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
  const bool field_mutation = mode == "field-ordinal" || mode == "field-type";
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      (field_mutation ? ":fn:unbox" : ":fn:fresh");
  const std::string original_template_id =
      "nominal-template-v1:" + sealed.value.module_id + ":type:Box";

  auto& binary = sealed.value.binary_contract;
  auto template_at = std::find_if(binary.begin(), binary.end(),
      [&](const auto& record) { return record.id == original_template_id; });
  assert(template_at != binary.end());
  auto nominal = moss::abi_v10::read_template(template_at->payload);

  auto& summaries = sealed.value.semantic_contract;
  auto summary_at = std::find_if(summaries.begin(), summaries.end(),
      [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_at != summaries.end());
  auto summary = moss::abi_v10::read_summary(summary_at->payload);

  auto& private_records = sealed.value.private_semantic_dependencies;
  const std::string body_id = "checked-body-v1:" + callable_id;
  auto body_at = std::find_if(private_records.begin(), private_records.end(),
      [&](const auto& record) { return record.id == body_id; });
  assert(body_at != private_records.end());
  auto body = moss::abi_v10::read_checked_body_record(body_at->payload);

  if (mode == "nominal-identity") {
    nominal.module_id = "module-v1:forged_provider@0.1.0::Forged";
    nominal.declaration_name = "Box";
    nominal.template_id = "nominal-template-v1:" + nominal.module_id +
        ":type:Box";
    rehash_nominal(nominal);
    *template_at = moss::abi_v10::template_record(nominal);
  } else if (mode == "field-ordinal" || mode == "field-type") {
    moss::TypeContext context;
    moss::NominalDeclaration source_declaration;
    source_declaration.kind = moss::DeclarationKind::Record;
    source_declaration.program_name = "Service__Box";
    source_declaration.module_id = sealed.value.module_id;
    source_declaration.declaration_name = "Box";
    source_declaration.fields = {{"value", "", true}};
    auto declaration = context.declare(std::move(source_declaration));
    auto variable = moss::Type::var({0, 0, 0});
    const auto box_type = checked_nominal_result(nominal, declaration, variable);
    const auto parameter_types = field_mutation
        ? std::vector<moss::TypeRef>{box_type}
        : std::vector<moss::TypeRef>{variable};
    const auto result_type = field_mutation ? variable : box_type;
    auto statements = moss::checked_ir::decode_h3_checked_body_v1(
        body.checked_ir_bytes, context, parameter_types, result_type,
        body.semantic_hash);
    assert(!statements.empty() && statements.front().expr);
    auto& expression = *statements.front().expr;
    if (mode == "field-ordinal") {
      assert(expression.kind == moss::checked_ir::ExprKind::Field);
      expression.member_ordinal = 1;
    } else {
      assert(expression.kind == moss::checked_ir::ExprKind::Field);
      auto forged_type = moss::Type::primitive("string");
      expression.type = std::move(forged_type);
    }
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
  } else if (mode == "visibility") {
    assert(nominal.fields.size() == 1 && nominal.fields.front().exported);
    nominal.fields.front().exported = false;
    rehash_nominal(nominal);
    *template_at = moss::abi_v10::template_record(nominal);
    for (auto& dependency : body.dependencies)
      if (dependency.target_id == nominal.template_id)
        dependency.expected_hash = nominal.representation_hash;
    reseal_nominal_dependencies(private_records, nominal.template_id,
                                nominal.representation_hash);
  } else if (mode == "template-field-type") {
    assert(nominal.fields.size() == 1);
    nominal.fields.front().type = {
        moss::abi_v10::WireTypeKind::Primitive, "string", {}, 0};
    rehash_nominal(nominal);
    *template_at = moss::abi_v10::template_record(nominal);
    for (auto& dependency : body.dependencies)
      if (dependency.target_id == nominal.template_id)
        dependency.expected_hash = nominal.representation_hash;
    reseal_nominal_dependencies(private_records, nominal.template_id,
                                nominal.representation_hash);
  } else if (mode == "missing-dependency") {
    assert(body.dependencies.size() == 1);
    body.dependencies.clear();
  } else if (mode == "dependency-hash") {
    assert(body.dependencies.size() == 1);
    body.dependencies.front().expected_hash =
        "sha256:" + std::string(64, '0');
  } else {
    assert(false && "unknown nominal mutation");
  }

  summaries.erase(std::remove_if(summaries.begin(), summaries.end(),
      [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  }), summaries.end());
  summaries.push_back(moss::abi_v10::summary_record(summary));
  private_records.erase(std::remove_if(private_records.begin(), private_records.end(),
      [&](const auto& record) { return record.id == body_id; }),
      private_records.end());
  private_records.push_back(moss::abi_v10::checked_body_record(body));
  std::sort(summaries.begin(), summaries.end(), [](const auto& left,
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
