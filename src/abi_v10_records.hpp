#pragma once

// Canonical ABI-v10 semantic record payloads. WireType is a serialized value,
// not an inference variable or a second type solver; compiler adapters must
// encode/decode the authoritative TypeRef at the integration boundary.

#include "source_free_abi_v10.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moss::abi_v10 {

class WireWriter {
 public:
  void byte(std::uint8_t value) { out_.push_back(static_cast<char>(value)); }
  void number(std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) byte((value >> shift) & 0xff);
  }
  void string(std::string_view value) {
    require(value.size() <= 1 << 20 && valid_utf8(value),
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 string is invalid or oversized");
    number(static_cast<std::uint32_t>(value.size()));
    out_.append(value);
  }
  std::string finish() { return std::move(out_); }
 private:
  std::string out_;
};

class WireReader {
 public:
  explicit WireReader(std::string_view input) : input_(input) {}
  std::uint8_t byte() {
    require(position_ < input_.size(), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 semantic record is truncated");
    return static_cast<std::uint8_t>(input_[position_++]);
  }
  std::uint32_t number() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | byte();
    return value;
  }
  std::string string() {
    const auto length = number();
    require(length <= 1 << 20 && length <= input_.size() - position_,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 semantic string is truncated");
    std::string value(input_.substr(position_, length));
    position_ += length;
    require(valid_utf8(value), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 semantic string is not UTF-8");
    return value;
  }
  bool done() const { return position_ == input_.size(); }
 private:
  std::string_view input_;
  std::size_t position_ = 0;
};

// Tags match ABI_V10_DESIGN.md TypeExpr, with BoundVar represented by a dense
// summary/template ordinal and never by a session-local TypeVarId.
enum class WireTypeKind : std::uint8_t {
  Primitive = 1, Builtin = 2, Nominal = 3, Enum = 4,
  Domain = 5, BoundVar = 6, Unit = 7
};

struct WireType {
  WireTypeKind kind = WireTypeKind::Unit;
  std::string identity;
  std::vector<WireType> arguments;
  std::uint32_t ordinal = 0;
};

inline void write_type(WireWriter& writer, const WireType& type,
                       std::uint32_t variable_count, unsigned depth = 0) {
  require(depth < 64 && type.arguments.size() <= 64,
          "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 type expression is too deep");
  require(static_cast<std::uint8_t>(type.kind) >= 1 &&
              static_cast<std::uint8_t>(type.kind) <= 7,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 type tag is unknown");
  writer.byte(static_cast<std::uint8_t>(type.kind));
  if (type.kind == WireTypeKind::BoundVar) {
    require(type.ordinal < variable_count && type.identity.empty() &&
                type.arguments.empty(), "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 bound variable escapes its scope");
    writer.number(type.ordinal);
    return;
  }
  if (type.kind == WireTypeKind::Unit) {
    require(type.identity.empty() && type.arguments.empty(),
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 unit type has payload");
    return;
  }
  require(!type.identity.empty() && valid_utf8(type.identity),
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 type identity is missing");
  if (type.kind != WireTypeKind::Builtin && type.kind != WireTypeKind::Nominal)
    require(type.arguments.empty(), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 scalar type has arguments");
  writer.string(type.identity);
  writer.number(static_cast<std::uint32_t>(type.arguments.size()));
  for (const auto& argument : type.arguments)
    write_type(writer, argument, variable_count, depth + 1);
}

inline WireType read_type(WireReader& reader, std::uint32_t variable_count,
                          unsigned depth = 0) {
  require(depth < 64, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 type expression is too deep");
  WireType type;
  const auto tag = reader.byte();
  require(tag >= 1 && tag <= 7, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 type tag is unknown");
  type.kind = static_cast<WireTypeKind>(tag);
  if (type.kind == WireTypeKind::BoundVar) {
    type.ordinal = reader.number();
    require(type.ordinal < variable_count, "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 bound variable escapes its scope");
    return type;
  }
  if (type.kind == WireTypeKind::Unit) return type;
  type.identity = reader.string();
  require(!type.identity.empty(), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 type identity is missing");
  const auto count = reader.number();
  require(count <= 64, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 type argument count exceeds the limit");
  if (type.kind != WireTypeKind::Builtin && type.kind != WireTypeKind::Nominal)
    require(count == 0, "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 scalar type has arguments");
  for (std::uint32_t i = 0; i < count; ++i)
    type.arguments.push_back(read_type(reader, variable_count, depth + 1));
  return type;
}

inline std::string type_bytes(const WireType& type,
                              std::uint32_t variable_count) {
  WireWriter writer;
  write_type(writer, type, variable_count);
  return writer.finish();
}

enum class Access : std::uint8_t { Read = 1, Write = 2, Consume = 3 };

inline Access read_access(WireReader& reader) {
  const auto value = reader.byte();
  require(value >= 1 && value <= 3, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 access mode is invalid");
  return static_cast<Access>(value);
}

struct Requirement {
  WireType receiver;
  std::string operation;
  Access access = Access::Read;
  std::uint32_t parent = 0;  // 0 = root; otherwise 1-based earlier index.
  std::string provenance_id;
};

struct Summary {
  std::string callable_id;
  std::uint32_t variable_count = 0;
  std::vector<WireType> parameters;
  WireType result;
  std::vector<Access> parameter_access;
  std::vector<Requirement> requirements;
  std::vector<std::pair<std::string, std::string>> dependencies;
  std::uint32_t observable_effect_bits = 0;
  std::string body_semantic_hash;
  std::string provenance_id;
};

inline std::string summary_payload(const Summary& summary) {
  require(!summary.callable_id.empty() && summary.variable_count <= 4096 &&
              summary.parameters.size() <= 256 &&
              summary.parameters.size() == summary.parameter_access.size() &&
              summary.requirements.size() <= 4096 &&
              summary.dependencies.size() <= 4096 &&
              hex_hash(summary.body_semantic_hash) &&
              !summary.provenance_id.empty(),
          "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary shape is invalid");
  WireWriter writer;
  writer.byte(1);  // OpenCallableSummary record version.
  writer.string(summary.callable_id);
  writer.number(summary.variable_count);
  writer.number(static_cast<std::uint32_t>(summary.parameters.size()));
  for (std::size_t i = 0; i < summary.parameters.size(); ++i) {
    write_type(writer, summary.parameters[i], summary.variable_count);
    require(static_cast<std::uint8_t>(summary.parameter_access[i]) >= 1 &&
                static_cast<std::uint8_t>(summary.parameter_access[i]) <= 3,
            "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 access mode is invalid");
    writer.byte(static_cast<std::uint8_t>(summary.parameter_access[i]));
  }
  write_type(writer, summary.result, summary.variable_count);
  writer.number(static_cast<std::uint32_t>(summary.requirements.size()));
  for (std::size_t i = 0; i < summary.requirements.size(); ++i) {
    const auto& requirement = summary.requirements[i];
    require(requirement.parent <= i && !requirement.operation.empty() &&
                !requirement.provenance_id.empty(),
            "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 requirement DAG is invalid");
    write_type(writer, requirement.receiver, summary.variable_count);
    writer.string(requirement.operation);
    require(static_cast<std::uint8_t>(requirement.access) >= 1 &&
                static_cast<std::uint8_t>(requirement.access) <= 3,
            "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 access mode is invalid");
    writer.byte(static_cast<std::uint8_t>(requirement.access));
    writer.number(requirement.parent);
    writer.string(requirement.provenance_id);
  }
  writer.number(static_cast<std::uint32_t>(summary.dependencies.size()));
  std::string previous;
  for (const auto& dependency : summary.dependencies) {
    require(!dependency.first.empty() && previous < dependency.first &&
                hex_hash(dependency.second), "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 semantic dependencies are not canonical");
    previous = dependency.first;
    writer.string(dependency.first);
    writer.string(dependency.second);
  }
  writer.number(summary.observable_effect_bits);
  writer.string(summary.body_semantic_hash);
  writer.string(summary.provenance_id);
  return writer.finish();
}

inline Summary read_summary(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 summary record version is unsupported");
  Summary summary;
  summary.callable_id = reader.string();
  summary.variable_count = reader.number();
  require(summary.variable_count <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 summary has too many variables");
  const auto parameters = reader.number();
  require(parameters <= 256, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 summary has too many parameters");
  for (std::uint32_t i = 0; i < parameters; ++i) {
    summary.parameters.push_back(read_type(reader, summary.variable_count));
    summary.parameter_access.push_back(read_access(reader));
  }
  summary.result = read_type(reader, summary.variable_count);
  const auto requirements = reader.number();
  require(requirements <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 summary has too many requirements");
  for (std::uint32_t i = 0; i < requirements; ++i) {
    Requirement requirement;
    requirement.receiver = read_type(reader, summary.variable_count);
    requirement.operation = reader.string();
    requirement.access = read_access(reader);
    requirement.parent = reader.number();
    requirement.provenance_id = reader.string();
    summary.requirements.push_back(std::move(requirement));
  }
  const auto dependencies = reader.number();
  require(dependencies <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 summary has too many dependencies");
  for (std::uint32_t i = 0; i < dependencies; ++i)
    summary.dependencies.push_back({reader.string(), reader.string()});
  summary.observable_effect_bits = reader.number();
  summary.body_semantic_hash = reader.string();
  summary.provenance_id = reader.string();
  require(reader.done() && summary_payload(summary) == payload,
          "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary is noncanonical");
  return summary;
}

inline Record summary_record(const Summary& summary) {
  auto payload = summary_payload(summary);
  return {hash("moss-abi-v10-open-summary", payload), std::move(payload)};
}

inline void validate_summary(const Record& record) {
  const auto value = read_summary(record.payload);
  require(summary_record(value).id == record.id,
          "SOURCE_FREE_SUMMARY_INVALID", "ABI-v10 summary ID is forged");
}

struct Field {
  std::string name;
  WireType type;
  bool exported = false;
  std::string provenance_id;
};

struct NominalTemplate {
  std::string template_id;
  std::string module_id;
  std::string declaration_name;
  std::uint32_t variable_count = 0;
  std::vector<Field> fields;
  std::string representation_hash;
  std::string provenance_id;
};

inline std::string template_payload(const NominalTemplate& value) {
  require(hex_hash(value.template_id) && module_id_valid(value.module_id) &&
              !value.declaration_name.empty() && value.variable_count <= 4096 &&
              value.fields.size() <= 4096 && hex_hash(value.representation_hash) &&
              !value.provenance_id.empty(),
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 nominal template shape is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.template_id);
  writer.string(value.module_id);
  writer.string(value.declaration_name);
  writer.number(value.variable_count);
  writer.number(static_cast<std::uint32_t>(value.fields.size()));
  std::set<std::string> names;
  for (const auto& field : value.fields) {
    require(!field.name.empty() && names.insert(field.name).second &&
                !field.provenance_id.empty(),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 nominal field identity is invalid");
    writer.string(field.name);
    write_type(writer, field.type, value.variable_count);
    writer.byte(field.exported ? 1 : 0);
    writer.string(field.provenance_id);
  }
  writer.string(value.representation_hash);
  writer.string(value.provenance_id);
  return writer.finish();
}

inline NominalTemplate read_template(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 nominal record version is unsupported");
  NominalTemplate value;
  value.template_id = reader.string();
  value.module_id = reader.string();
  value.declaration_name = reader.string();
  value.variable_count = reader.number();
  const auto count = reader.number();
  require(value.variable_count <= 4096 && count <= 4096,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 nominal shape exceeds the limit");
  for (std::uint32_t i = 0; i < count; ++i) {
    Field field;
    field.name = reader.string();
    field.type = read_type(reader, value.variable_count);
    const auto exported = reader.byte();
    require(exported <= 1, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 field visibility is invalid");
    field.exported = exported == 1;
    field.provenance_id = reader.string();
    value.fields.push_back(std::move(field));
  }
  value.representation_hash = reader.string();
  value.provenance_id = reader.string();
  require(reader.done() && template_payload(value) == payload,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH", "ABI-v10 nominal template is noncanonical");
  return value;
}

inline Record template_record(const NominalTemplate& value) {
  return {value.template_id, template_payload(value)};
}

inline void validate_template(const Record& record) {
  const auto value = read_template(record.payload);
  require(value.template_id == record.id,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 nominal template ID disagrees with its record");
}

struct ConcreteInstance {
  std::string template_id;
  std::vector<WireType> substitution;
  std::vector<WireType> resolved_fields;
  std::string provider_binary_id;
  std::string provider_rust_type_symbol;
  std::string representation_hash;
};

inline std::string instance_key(const ConcreteInstance& value) {
  require(hex_hash(value.template_id) && value.substitution.size() <= 4096,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance template is invalid");
  WireWriter writer;
  writer.string(value.template_id);
  writer.number(static_cast<std::uint32_t>(value.substitution.size()));
  for (const auto& type : value.substitution) write_type(writer, type, 0);
  return hash("moss-abi-v10-nominal-instance", writer.finish());
}

inline std::string instance_payload(const ConcreteInstance& value) {
  require(hex_hash(value.provider_binary_id) &&
              !value.provider_rust_type_symbol.empty() &&
              hex_hash(value.representation_hash) &&
              value.resolved_fields.size() <= 4096,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance representation is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.template_id);
  writer.number(static_cast<std::uint32_t>(value.substitution.size()));
  for (const auto& type : value.substitution) write_type(writer, type, 0);
  writer.number(static_cast<std::uint32_t>(value.resolved_fields.size()));
  for (const auto& type : value.resolved_fields) write_type(writer, type, 0);
  writer.string(value.provider_binary_id);
  writer.string(value.provider_rust_type_symbol);
  writer.string(value.representation_hash);
  return writer.finish();
}

inline Record instance_record(const ConcreteInstance& value) {
  return {instance_key(value), instance_payload(value)};
}

inline ConcreteInstance read_instance(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance version is unsupported");
  ConcreteInstance value;
  value.template_id = reader.string();
  auto count = reader.number();
  require(count <= 4096, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance has too many arguments");
  for (std::uint32_t i = 0; i < count; ++i)
    value.substitution.push_back(read_type(reader, 0));
  count = reader.number();
  require(count <= 4096, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance has too many fields");
  for (std::uint32_t i = 0; i < count; ++i)
    value.resolved_fields.push_back(read_type(reader, 0));
  value.provider_binary_id = reader.string();
  value.provider_rust_type_symbol = reader.string();
  value.representation_hash = reader.string();
  require(reader.done() && instance_payload(value) == payload,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance is noncanonical");
  return value;
}

inline WireType substitute_bound(const WireType& source,
                                 const std::vector<WireType>& bindings) {
  if (source.kind == WireTypeKind::BoundVar) {
    require(source.ordinal < bindings.size(),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 instance substitution misses a bound variable");
    return bindings[source.ordinal];
  }
  WireType result = source;
  result.arguments.clear();
  for (const auto& argument : source.arguments)
    result.arguments.push_back(substitute_bound(argument, bindings));
  return result;
}

inline void validate_instance(const Record& record,
                              const NominalTemplate& nominal,
                              std::string_view selected_provider_binary_id) {
  const auto value = read_instance(record.payload);
  require(value.template_id == nominal.template_id &&
              value.substitution.size() == nominal.variable_count &&
              value.resolved_fields.size() == nominal.fields.size() &&
              value.representation_hash == nominal.representation_hash &&
              value.provider_binary_id == selected_provider_binary_id &&
              instance_key(value) == record.id,
          "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 concrete instance conflicts with its provider template");
  for (std::size_t i = 0; i < nominal.fields.size(); ++i) {
    const auto expected = substitute_bound(nominal.fields[i].type,
                                           value.substitution);
    require(type_bytes(expected, 0) == type_bytes(value.resolved_fields[i], 0),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 concrete field type differs from its template substitution");
  }
}

struct Specialization {
  std::string callable_id;
  std::vector<WireType> arguments;
  WireType result;
  std::vector<Access> argument_access;
  std::uint32_t observable_effect_bits = 0;
  std::string materialization_owner;
  std::string native_symbol;
  std::string semantic_hash;
};

inline std::string specialization_payload(const Specialization& value) {
  require(!value.callable_id.empty() && value.arguments.size() <= 256 &&
              value.arguments.size() == value.argument_access.size() &&
              (value.materialization_owner == "provider-record" ||
               value.materialization_owner == "consumer-free-function") &&
              hex_hash(value.semantic_hash),
          "SOURCE_FREE_SPECIALIZATION_MISMATCH",
          "ABI-v10 specialization shape is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.callable_id);
  writer.number(static_cast<std::uint32_t>(value.arguments.size()));
  for (std::size_t i = 0; i < value.arguments.size(); ++i) {
    write_type(writer, value.arguments[i], 0);
    require(static_cast<std::uint8_t>(value.argument_access[i]) >= 1 &&
                static_cast<std::uint8_t>(value.argument_access[i]) <= 3,
            "SOURCE_FREE_SPECIALIZATION_MISMATCH", "ABI-v10 access mode is invalid");
    writer.byte(static_cast<std::uint8_t>(value.argument_access[i]));
  }
  write_type(writer, value.result, 0);
  writer.number(value.observable_effect_bits);
  writer.string(value.materialization_owner);
  writer.string(value.native_symbol);
  writer.string(value.semantic_hash);
  return writer.finish();
}

inline Record specialization_record(const Specialization& value) {
  auto payload = specialization_payload(value);
  return {hash("moss-abi-v10-specialization", payload), std::move(payload)};
}

inline void validate_specialization(const Record& record) {
  // A specialization must be closed. Decode its framed fields and re-encode to
  // check its canonical key, access modes, and semantic hash form.
  WireReader reader(record.payload);
  require(reader.byte() == 1, "SOURCE_FREE_SPECIALIZATION_MISMATCH",
          "ABI-v10 specialization version is unsupported");
  Specialization value;
  value.callable_id = reader.string();
  const auto count = reader.number();
  require(count <= 256, "SOURCE_FREE_SPECIALIZATION_MISMATCH",
          "ABI-v10 specialization has too many arguments");
  for (std::uint32_t i = 0; i < count; ++i) {
    value.arguments.push_back(read_type(reader, 0));
    value.argument_access.push_back(read_access(reader));
  }
  value.result = read_type(reader, 0);
  value.observable_effect_bits = reader.number();
  value.materialization_owner = reader.string();
  value.native_symbol = reader.string();
  value.semantic_hash = reader.string();
  require(reader.done() && specialization_payload(value) == record.payload &&
              specialization_record(value).id == record.id,
          "SOURCE_FREE_SPECIALIZATION_MISMATCH",
          "ABI-v10 specialization key or body is noncanonical");
}

struct KeyProof {
  WireType key_type;
  std::string equality_callable_id;
  std::string equality_specialization_key;
  std::string equality_semantic_hash;
  std::string hash_callable_id;
  std::string hash_specialization_key;
  std::string hash_semantic_hash;
  std::string law_contract_version;
  std::string provenance_id;
};

inline std::string key_proof_payload(const KeyProof& value) {
  require(!value.equality_callable_id.empty() &&
              hex_hash(value.equality_specialization_key) &&
              hex_hash(value.equality_semantic_hash) &&
              !value.hash_callable_id.empty() &&
              hex_hash(value.hash_specialization_key) &&
              hex_hash(value.hash_semantic_hash) &&
              !value.law_contract_version.empty() &&
              !value.provenance_id.empty(), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 key proof shape is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string("key-protocol-v1");
  write_type(writer, value.key_type, 0);
  writer.string(value.equality_callable_id);
  writer.string(value.equality_specialization_key);
  writer.string(value.equality_semantic_hash);
  writer.string(value.hash_callable_id);
  writer.string(value.hash_specialization_key);
  writer.string(value.hash_semantic_hash);
  writer.string(value.law_contract_version);
  writer.string(value.provenance_id);
  return writer.finish();
}

inline Record key_proof_record(const KeyProof& value) {
  auto payload = key_proof_payload(value);
  return {hash("key-proof-v1", payload), std::move(payload)};
}

inline void validate_key_proof(const Record& record) {
  WireReader reader(record.payload);
  require(reader.byte() == 1 && reader.string() == "key-protocol-v1",
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 key proof version is unsupported");
  KeyProof value;
  value.key_type = read_type(reader, 0);
  value.equality_callable_id = reader.string();
  value.equality_specialization_key = reader.string();
  value.equality_semantic_hash = reader.string();
  value.hash_callable_id = reader.string();
  value.hash_specialization_key = reader.string();
  value.hash_semantic_hash = reader.string();
  value.law_contract_version = reader.string();
  value.provenance_id = reader.string();
  require(reader.done() && key_proof_payload(value) == record.payload &&
              key_proof_record(value).id == record.id,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 key proof ID or body is noncanonical");
}

}  // namespace moss::abi_v10
