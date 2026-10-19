#pragma once

// Canonical ABI-v10 semantic record payloads. WireType is a serialized value,
// not an inference variable or a second type solver; compiler adapters must
// encode/decode the authoritative TypeRef at the integration boundary.

#include "source_free_abi_v10.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
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

inline bool nominal_template_id_valid(std::string_view identity) {
  constexpr std::string_view prefix = "nominal-template-v1:";
  if (identity.substr(0, prefix.size()) != prefix) return false;
  const auto separator = identity.rfind(":type:");
  if (separator == std::string_view::npos ||
      separator <= prefix.size() || separator + 6 >= identity.size() ||
      !module_id_valid(identity.substr(prefix.size(),
                                       separator - prefix.size())))
    return false;
  const auto name = identity.substr(separator + 6);
  if (!((name.front() >= 'A' && name.front() <= 'Z') ||
        (name.front() >= 'a' && name.front() <= 'z') ||
        name.front() == '_')) return false;
  return std::all_of(name.begin() + 1, name.end(), [](char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '_';
  });
}

inline bool checked_body_hash_valid(std::string_view identity) {
  constexpr std::string_view prefix = "body-semantic-v1:";
  return identity.substr(0, prefix.size()) == prefix &&
      hex_hash(identity.substr(prefix.size()));
}

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
  if (type.kind == WireTypeKind::Nominal)
    require(nominal_template_id_valid(type.identity),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 nominal TypeExpr has a noncanonical template ID");
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
  if (type.kind == WireTypeKind::Nominal)
    require(nominal_template_id_valid(type.identity),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 nominal TypeExpr has a noncanonical template ID");
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

inline void write_effect_tags(WireWriter& writer,
                              const std::vector<std::string>& tags) {
  require(tags.size() <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 effect set exceeds the limit");
  writer.number(static_cast<std::uint32_t>(tags.size()));
  std::string previous;
  for (const auto& tag : tags) {
    require(!tag.empty() && previous < tag, "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 effect tags are not canonical");
    previous = tag;
    writer.string(tag);
  }
}

inline std::vector<std::string> read_effect_tags(WireReader& reader) {
  const auto count = reader.number();
  require(count <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 effect set exceeds the limit");
  std::vector<std::string> result;
  for (std::uint32_t i = 0; i < count; ++i) result.push_back(reader.string());
  return result;
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
  std::vector<std::string> observable_effect_tags;
  std::string body_semantic_hash;
  std::string provenance_id;
};

inline bool summary_ordinals_dense(const Summary& summary) {
  std::vector<bool> used(summary.variable_count, false);
  auto visit = [&](const auto& self, const WireType& type) -> void {
    if (type.kind == WireTypeKind::BoundVar &&
        type.ordinal < summary.variable_count) used[type.ordinal] = true;
    for (const auto& argument : type.arguments) self(self, argument);
  };
  for (const auto& type : summary.parameters) visit(visit, type);
  visit(visit, summary.result);
  for (const auto& requirement : summary.requirements)
    visit(visit, requirement.receiver);
  return std::find(used.begin(), used.end(), false) == used.end();
}

inline std::string summary_payload(const Summary& summary) {
  require(!summary.callable_id.empty() && summary.variable_count <= 4096 &&
              summary.parameters.size() <= 256 &&
              summary.parameters.size() == summary.parameter_access.size() &&
              summary.requirements.size() <= 4096 &&
              summary.dependencies.size() <= 4096 &&
              checked_body_hash_valid(summary.body_semantic_hash) &&
              !summary.provenance_id.empty() &&
              summary_ordinals_dense(summary),
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
                checked_body_hash_valid(dependency.second),
            "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 semantic dependencies are not canonical");
    previous = dependency.first;
    writer.string(dependency.first);
    writer.string(dependency.second);
  }
  writer.number(summary.observable_effect_bits);
  write_effect_tags(writer, summary.observable_effect_tags);
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
  summary.observable_effect_tags = read_effect_tags(reader);
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

// The body bytes are the checked_ir::encode_checked_body_v1 projection, not
// source statements. The record binds that versioned IR to the open summary
// and to exact callable/nominal dependencies before source-free import.
struct CheckedBodyRecord {
  struct Dependency {
    enum class Kind : std::uint8_t { Callable = 1, NominalType = 2 };
    Kind kind = Kind::Callable;
    std::string target_id;
    std::string expected_hash;
  };
  std::string callable_id;
  std::string semantic_hash;
  std::string checked_ir_bytes;
  std::vector<Dependency> dependencies;
};

inline std::string checked_body_record_payload(const CheckedBodyRecord& body) {
  require(!body.callable_id.empty() &&
              body.checked_ir_bytes.rfind("moss-checked-body-v1\n", 0) == 0 &&
              checked_body_hash_valid(body.semantic_hash) &&
              body.semantic_hash == "body-semantic-v1:sha256:" +
                  source_free_contract_sha256(body.checked_ir_bytes) &&
              body.dependencies.size() <= 4096,
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 checked body has invalid version or semantic hash");
  WireWriter writer;
  writer.byte(1);
  writer.string("checked-body-v1");
  writer.string(body.callable_id);
  writer.string(body.semantic_hash);
  writer.string(body.checked_ir_bytes);
  writer.number(static_cast<std::uint32_t>(body.dependencies.size()));
  std::string previous;
  for (const auto& dep : body.dependencies) {
    require(!dep.target_id.empty() && previous < dep.target_id &&
                ((dep.kind == CheckedBodyRecord::Dependency::Kind::Callable &&
                  checked_body_hash_valid(dep.expected_hash)) ||
                 (dep.kind == CheckedBodyRecord::Dependency::Kind::NominalType &&
                  nominal_template_id_valid(dep.target_id) &&
                  hex_hash(dep.expected_hash))),
            "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 checked body dependency kind or hash is invalid");
    previous = dep.target_id;
    writer.byte(static_cast<std::uint8_t>(dep.kind));
    writer.string(dep.target_id);
    writer.string(dep.expected_hash);
  }
  return writer.finish();
}

inline Record checked_body_record(const CheckedBodyRecord& body) {
  return {"checked-body-v1:" + body.callable_id,
          checked_body_record_payload(body)};
}

inline CheckedBodyRecord read_checked_body_record(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1 && reader.string() == "checked-body-v1",
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 checked body record version is unsupported");
  CheckedBodyRecord body;
  body.callable_id = reader.string();
  body.semantic_hash = reader.string();
  body.checked_ir_bytes = reader.string();
  const auto count = reader.number();
  require(count <= 4096, "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 checked body has too many dependencies");
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto kind = reader.byte();
    require(kind >= 1 && kind <= 2, "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 checked body dependency kind is unknown");
    body.dependencies.push_back({
        static_cast<CheckedBodyRecord::Dependency::Kind>(kind),
        reader.string(), reader.string()});
  }
  require(reader.done() && checked_body_record_payload(body) == payload,
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 checked body record is noncanonical");
  return body;
}

inline void validate_checked_body_record(const Record& record) {
  const auto body = read_checked_body_record(record.payload);
  require(checked_body_record(body).id == record.id,
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 checked body record ID is forged");
}

inline std::string requirement_bytes(const Requirement& value,
                                      std::uint32_t variable_count) {
  WireWriter writer;
  write_type(writer, value.receiver, variable_count);
  writer.string(value.operation);
  writer.byte(static_cast<std::uint8_t>(value.access));
  writer.number(value.parent);
  writer.string(value.provenance_id);
  return writer.finish();
}

// Imported facts are a lower bound on caller-visible ownership/effects. A
// consumer can discover extra requirements/effects, but cannot erase provider
// constraints or weaken WRITE/CONSUME access to READ.
inline void validate_summary_nonweakening(const Summary& provider,
                                           const Summary& consumer) {
  require(provider.callable_id == consumer.callable_id &&
              provider.variable_count == consumer.variable_count &&
              provider.parameters.size() == consumer.parameters.size() &&
              provider.parameter_access == consumer.parameter_access &&
              provider.requirements.size() <= consumer.requirements.size() &&
              (consumer.observable_effect_bits & provider.observable_effect_bits) ==
                  provider.observable_effect_bits &&
              provider.body_semantic_hash == consumer.body_semantic_hash,
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 consumer weakens an imported summary");
  for (std::size_t i = 0; i < provider.parameters.size(); ++i)
    require(type_bytes(provider.parameters[i], provider.variable_count) ==
                type_bytes(consumer.parameters[i], consumer.variable_count),
            "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 consumer changes an imported parameter type");
  require(type_bytes(provider.result, provider.variable_count) ==
              type_bytes(consumer.result, consumer.variable_count),
          "SOURCE_FREE_SUMMARY_INVALID",
          "ABI-v10 consumer changes an imported result type");
  for (std::size_t i = 0; i < provider.requirements.size(); ++i)
    require(requirement_bytes(provider.requirements[i], provider.variable_count) ==
                requirement_bytes(consumer.requirements[i], consumer.variable_count),
            "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 consumer erases an imported requirement");
  for (const auto& dependency : provider.dependencies) {
    const auto match = std::find(consumer.dependencies.begin(),
                                 consumer.dependencies.end(), dependency);
    require(match != consumer.dependencies.end(), "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 consumer erases an imported semantic dependency");
  }
  for (const auto& effect : provider.observable_effect_tags) {
    require(std::find(consumer.observable_effect_tags.begin(),
                      consumer.observable_effect_tags.end(), effect) !=
                consumer.observable_effect_tags.end(),
            "SOURCE_FREE_SUMMARY_INVALID",
            "ABI-v10 consumer erases an imported observable effect");
  }
}

struct SourceProvenance {
  std::string provenance_id;
  std::string module_id;
  std::string logical_source_id;
  std::string source_file_display;
  std::uint32_t start_line = 0;
  std::uint32_t start_column = 0;
  std::uint32_t end_line = 0;
  std::uint32_t end_column = 0;
  std::string declaration_id;
};

inline std::string provenance_payload(const SourceProvenance& value) {
  require(!value.provenance_id.empty() && valid_utf8(value.provenance_id) &&
              module_id_valid(value.module_id) &&
              !value.logical_source_id.empty() && valid_utf8(value.logical_source_id) &&
              valid_utf8(value.source_file_display) &&
              (value.start_line < value.end_line ||
               (value.start_line == value.end_line && value.start_column <= value.end_column)) &&
              valid_utf8(value.declaration_id),
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 source provenance is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.provenance_id);
  writer.string(value.module_id);
  writer.string(value.logical_source_id);
  writer.string(value.source_file_display);
  writer.number(value.start_line);
  writer.number(value.start_column);
  writer.number(value.end_line);
  writer.number(value.end_column);
  writer.string(value.declaration_id);
  return writer.finish();
}

inline SourceProvenance read_provenance(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 source provenance record version is unsupported");
  SourceProvenance value;
  value.provenance_id = reader.string();
  value.module_id = reader.string();
  value.logical_source_id = reader.string();
  value.source_file_display = reader.string();
  value.start_line = reader.number();
  value.start_column = reader.number();
  value.end_line = reader.number();
  value.end_column = reader.number();
  value.declaration_id = reader.string();
  require(reader.done() && provenance_payload(value) == payload,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 source provenance is noncanonical");
  return value;
}

inline Record provenance_record(const SourceProvenance& value) {
  return {value.provenance_id, provenance_payload(value)};
}

inline void validate_provenance(const Record& record) {
  const auto value = read_provenance(record.payload);
  require(value.provenance_id == record.id, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 provenance ID disagrees with its record");
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
  std::vector<std::string> method_template_ids;
  std::string provenance_id;
};

inline std::string template_payload(const NominalTemplate& value) {
  require(nominal_template_id_valid(value.template_id) &&
              value.template_id == "nominal-template-v1:" + value.module_id +
                  ":type:" + value.declaration_name &&
              module_id_valid(value.module_id) &&
              !value.declaration_name.empty() && value.variable_count <= 4096 &&
              value.fields.size() <= 4096 && hex_hash(value.representation_hash) &&
              value.method_template_ids.size() <= 4096 &&
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
  writer.number(static_cast<std::uint32_t>(value.method_template_ids.size()));
  std::string previous_method;
  for (const auto& mid : value.method_template_ids) {
    require(!mid.empty() && (previous_method.empty() || previous_method < mid),
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ABI-v10 method template IDs are not canonical");
    previous_method = mid;
    writer.string(mid);
  }
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
  const auto method_count = reader.number();
  require(method_count <= 4096, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
          "ABI-v10 method count exceeds the limit");
  for (std::uint32_t i = 0; i < method_count; ++i)
    value.method_template_ids.push_back(reader.string());
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

struct MethodTemplate {
  std::string method_id;
  std::string template_id;
  std::string method_name;
  bool exported = false;
  std::uint32_t variable_count = 0;
  std::vector<WireType> parameters;
  WireType result;
  Access receiver_access = Access::Read;
  std::vector<Access> parameter_access;
  std::vector<Requirement> requirements;
  std::vector<std::pair<std::string, std::string>> dependencies;
  std::uint32_t observable_effect_bits = 0;
  std::vector<std::string> observable_effect_tags;
  std::string provenance_id;
};

inline std::string method_template_payload(const MethodTemplate& value) {
  require(!value.method_id.empty() &&
              nominal_template_id_valid(value.template_id) &&
              !value.method_name.empty() && value.variable_count <= 4096 &&
              value.parameters.size() <= 256 &&
              value.parameters.size() == value.parameter_access.size() &&
              value.requirements.size() <= 4096 &&
              value.dependencies.size() <= 4096 &&
              !value.provenance_id.empty(),
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 method template shape is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.method_id);
  writer.string(value.template_id);
  writer.string(value.method_name);
  writer.byte(value.exported ? 1 : 0);
  writer.number(value.variable_count);
  writer.number(static_cast<std::uint32_t>(value.parameters.size()));
  for (std::size_t i = 0; i < value.parameters.size(); ++i) {
    write_type(writer, value.parameters[i], value.variable_count);
    require(static_cast<std::uint8_t>(value.parameter_access[i]) >= 1 &&
                static_cast<std::uint8_t>(value.parameter_access[i]) <= 3,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 access mode is invalid");
    writer.byte(static_cast<std::uint8_t>(value.parameter_access[i]));
  }
  write_type(writer, value.result, value.variable_count);
  require(static_cast<std::uint8_t>(value.receiver_access) >= 1 &&
              static_cast<std::uint8_t>(value.receiver_access) <= 3,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 access mode is invalid");
  writer.byte(static_cast<std::uint8_t>(value.receiver_access));
  writer.number(static_cast<std::uint32_t>(value.requirements.size()));
  for (std::size_t i = 0; i < value.requirements.size(); ++i) {
    const auto& requirement = value.requirements[i];
    require(requirement.parent <= i && !requirement.operation.empty() &&
                !requirement.provenance_id.empty(),
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 requirement DAG is invalid");
    write_type(writer, requirement.receiver, value.variable_count);
    writer.string(requirement.operation);
    require(static_cast<std::uint8_t>(requirement.access) >= 1 &&
                static_cast<std::uint8_t>(requirement.access) <= 3,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 access mode is invalid");
    writer.byte(static_cast<std::uint8_t>(requirement.access));
    writer.number(requirement.parent);
    writer.string(requirement.provenance_id);
  }
  writer.number(static_cast<std::uint32_t>(value.dependencies.size()));
  std::string previous;
  for (const auto& dep : value.dependencies) {
    require(!dep.first.empty() && (previous.empty() || previous < dep.first) &&
                checked_body_hash_valid(dep.second),
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 dependencies are not canonical");
    previous = dep.first;
    writer.string(dep.first);
    writer.string(dep.second);
  }
  writer.number(value.observable_effect_bits);
  write_effect_tags(writer, value.observable_effect_tags);
  writer.string(value.provenance_id);
  return writer.finish();
}

inline MethodTemplate read_method_template(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 method template version is unsupported");
  MethodTemplate value;
  value.method_id = reader.string();
  value.template_id = reader.string();
  value.method_name = reader.string();
  const auto exported = reader.byte();
  require(exported <= 1, "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 visibility invalid");
  value.exported = exported == 1;
  value.variable_count = reader.number();
  require(value.variable_count <= 4096, "SOURCE_FREE_CONTRACT_INVALID", "Too many variables");
  const auto params = reader.number();
  require(params <= 256, "SOURCE_FREE_CONTRACT_INVALID", "Too many parameters");
  for (std::uint32_t i = 0; i < params; ++i) {
    value.parameters.push_back(read_type(reader, value.variable_count));
    value.parameter_access.push_back(read_access(reader));
  }
  value.result = read_type(reader, value.variable_count);
  value.receiver_access = read_access(reader);
  const auto reqs = reader.number();
  require(reqs <= 4096, "SOURCE_FREE_CONTRACT_INVALID", "Too many requirements");
  for (std::uint32_t i = 0; i < reqs; ++i) {
    Requirement req;
    req.receiver = read_type(reader, value.variable_count);
    req.operation = reader.string();
    req.access = read_access(reader);
    req.parent = reader.number();
    req.provenance_id = reader.string();
    value.requirements.push_back(std::move(req));
  }
  const auto deps = reader.number();
  require(deps <= 4096, "SOURCE_FREE_CONTRACT_INVALID", "Too many dependencies");
  for (std::uint32_t i = 0; i < deps; ++i)
    value.dependencies.push_back({reader.string(), reader.string()});
  value.observable_effect_bits = reader.number();
  value.observable_effect_tags = read_effect_tags(reader);
  value.provenance_id = reader.string();
  require(reader.done() && method_template_payload(value) == payload,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 method template is noncanonical");
  return value;
}

inline Record method_template_record(const MethodTemplate& value) {
  return {value.method_id, method_template_payload(value)};
}

inline void validate_method_template(const Record& record) {
  const auto value = read_method_template(record.payload);
  require(value.method_id == record.id, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 method template ID disagrees with its record");
}

struct ModuleRecord {
  std::string module_id;
  std::string package_name;
  std::string package_version;
  std::string module_name;
  std::string provider_binary_id;
};

inline std::string module_record_payload(const ModuleRecord& value) {
  require(module_id_valid(value.module_id) && !value.package_name.empty() &&
              !value.package_version.empty() && !value.module_name.empty() &&
              hex_hash(value.provider_binary_id),
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 module record is invalid");
  WireWriter writer;
  writer.byte(1);
  writer.string(value.module_id);
  writer.string(value.package_name);
  writer.string(value.package_version);
  writer.string(value.module_name);
  writer.string(value.provider_binary_id);
  return writer.finish();
}

inline ModuleRecord read_module_record(std::string_view payload) {
  WireReader reader(payload);
  require(reader.byte() == 1, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 module record version is unsupported");
  ModuleRecord value;
  value.module_id = reader.string();
  value.package_name = reader.string();
  value.package_version = reader.string();
  value.module_name = reader.string();
  value.provider_binary_id = reader.string();
  require(reader.done() && module_record_payload(value) == payload,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 module record is noncanonical");
  return value;
}

inline Record module_record(const ModuleRecord& value) {
  return {value.module_id, module_record_payload(value)};
}

inline void validate_module_record(const Record& record) {
  const auto value = read_module_record(record.payload);
  require(value.module_id == record.id, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 module record ID disagrees with its record");
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
  require(nominal_template_id_valid(value.template_id) &&
              value.substitution.size() <= 4096,
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
  std::uint32_t equality_effect_bits = 0;
  std::vector<std::string> equality_effect_tags;
  std::string hash_callable_id;
  std::string hash_specialization_key;
  std::string hash_semantic_hash;
  std::uint32_t hash_effect_bits = 0;
  std::vector<std::string> hash_effect_tags;
  std::string law_contract_version;
  std::string provenance_id;
};

inline bool is_key_proof_payload(std::string_view payload) {
  return payload.size() >= 20 && payload[1] == 0 && payload[2] == 0 &&
      payload[3] == 0 && static_cast<unsigned char>(payload[4]) == 15 &&
      payload.substr(5, 15) == "key-protocol-v1";
}

inline std::string key_proof_payload(const KeyProof& value) {
  require(!value.equality_callable_id.empty() &&
              hex_hash(value.equality_specialization_key) &&
              hex_hash(value.equality_semantic_hash) &&
              !value.hash_callable_id.empty() &&
              hex_hash(value.hash_specialization_key) &&
              hex_hash(value.hash_semantic_hash) &&
              !value.law_contract_version.empty() &&
              !value.provenance_id.empty() &&
              value.equality_effect_bits == 0 && value.hash_effect_bits == 0 &&
              value.equality_effect_tags.empty() && value.hash_effect_tags.empty(),
          "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 key proof shape is invalid");
  // The protocol signatures are encoded by this record version: equality is
  // (READ K, READ K) -> Bool and hash is (READ K) -> Int. The two exact targets
  // and their semantic hashes are bound below; no Rust Eq/Hash policy is stored.
  WireWriter writer;
  writer.byte(1);
  writer.string("key-protocol-v1");
  write_type(writer, value.key_type, 0);
  writer.string(value.equality_callable_id);
  writer.string(value.equality_specialization_key);
  writer.string(value.equality_semantic_hash);
  writer.number(value.equality_effect_bits);
  write_effect_tags(writer, value.equality_effect_tags);
  writer.string(value.hash_callable_id);
  writer.string(value.hash_specialization_key);
  writer.string(value.hash_semantic_hash);
  writer.number(value.hash_effect_bits);
  write_effect_tags(writer, value.hash_effect_tags);
  writer.string(value.law_contract_version);
  writer.string(value.provenance_id);
  return writer.finish();
}

inline Record key_proof_record(const KeyProof& value) {
  auto payload = key_proof_payload(value);
  return {hash("key-proof-v1", payload), std::move(payload)};
}

inline KeyProof read_key_proof(const Record& record) {
  WireReader reader(record.payload);
  require(reader.byte() == 1 && reader.string() == "key-protocol-v1",
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 key proof version is unsupported");
  KeyProof value;
  value.key_type = read_type(reader, 0);
  value.equality_callable_id = reader.string();
  value.equality_specialization_key = reader.string();
  value.equality_semantic_hash = reader.string();
  value.equality_effect_bits = reader.number();
  value.equality_effect_tags = read_effect_tags(reader);
  value.hash_callable_id = reader.string();
  value.hash_specialization_key = reader.string();
  value.hash_semantic_hash = reader.string();
  value.hash_effect_bits = reader.number();
  value.hash_effect_tags = read_effect_tags(reader);
  value.law_contract_version = reader.string();
  value.provenance_id = reader.string();
  require(reader.done() && key_proof_payload(value) == record.payload &&
              key_proof_record(value).id == record.id,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 key proof ID or body is noncanonical");
  return value;
}

inline void validate_key_proof(const Record& record) {
  (void)read_key_proof(record);
}

inline void validate_key_proof_coherence(const std::vector<Record>& proofs) {
  std::map<std::string, std::string> by_type;
  for (const auto& record : proofs) {
    const auto proof = read_key_proof(record);
    const auto key = type_bytes(proof.key_type, 0);
    const auto prior = by_type.find(key);
    require(prior == by_type.end() || prior->second == record.id,
            "KEY_PROOF_COHERENCE_MISMATCH",
            "ABI-v10 consumers bind conflicting proofs for one key type");
    by_type[key] = record.id;
  }
}

inline KeyProof make_builtin_key_proof(std::string_view type_name) {
  std::string name(type_name);
  if (name == "int" || name == "Int") name = "Int";
  else if (name == "float" || name == "Float") name = "Float";
  else if (name == "char" || name == "Char") name = "Char";
  else if (name == "string" || name == "String") name = "String";
  else if (name == "bool" || name == "Bool") name = "Bool";
  else throw Error("SOURCE_FREE_CONTRACT_INVALID", "Unsupported builtin key proof type: " + name);

  KeyProof proof;
  std::string prim_name;
  if (name == "Int") prim_name = "int";
  else if (name == "Float") prim_name = "float";
  else if (name == "Char") prim_name = "char";
  else if (name == "String") prim_name = "string";
  else prim_name = "bool";

  proof.key_type = {WireTypeKind::Primitive, prim_name, {}, 0};
  proof.equality_callable_id = "builtin:key-protocol-v1:isequal:" + name;
  proof.hash_callable_id = "builtin:key-protocol-v1:hash:" + name;
  proof.equality_specialization_key = hash("moss-abi-v10-specialization", proof.equality_callable_id);
  proof.hash_specialization_key = hash("moss-abi-v10-specialization", proof.hash_callable_id);

  std::string eq_text, hash_text;
  if (name == "Int") { eq_text = "a == b"; hash_text = "a"; }
  else if (name == "Float") { eq_text = "moss_float_isequal(a, b)"; hash_text = "moss_float_hash(a)"; }
  else if (name == "Char") { eq_text = "a == b"; hash_text = "a as u32 as i64"; }
  else if (name == "String") { eq_text = "byte equality"; hash_text = "moss_string_hash"; }
  else if (name == "Bool") { eq_text = "a == b"; hash_text = "0 / 1"; }

  proof.equality_semantic_hash = hash("moss-abi-v10-key-lowering", eq_text);
  proof.hash_semantic_hash = hash("moss-abi-v10-key-lowering", hash_text);
  proof.equality_effect_bits = 0;
  proof.equality_effect_tags.clear();
  proof.hash_effect_bits = 0;
  proof.hash_effect_tags.clear();
  proof.law_contract_version = "key-laws-v1";
  proof.provenance_id = "builtin:key-protocol-v1:" + name;
  return proof;
}

inline void validate_complete_interface(
    const SealedInterface& sealed,
    std::string_view selected_provider_binary_id = "",
    std::string_view selected_rlib_archive = "") {
  validate_identity(sealed.value);

  const auto encoded = encode(sealed.value);
  require(digest_matches(encoded, sealed), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 provider contract digest mismatch");

  if (!selected_rlib_archive.empty()) {
    const auto mod_info = parse_module_id(sealed.value.module_id);
    validate_selected_rlib(selected_rlib_archive, mod_info.module_name, sealed.digest);
  }

  std::string binary_hash, semantic_hash;
  (void)canonical_body(sealed.value, &binary_hash, &semantic_hash);
  require(binary_hash == sealed.binary_hash, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 binary contract hash mismatch");
  require(semantic_hash == sealed.semantic_hash, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 semantic contract hash mismatch");

  std::set<std::string> import_modules;
  for (const auto& imp : sealed.value.imports) {
    require(module_id_valid(imp.module_id), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 import module ID is malformed");
    require(hex_hash(imp.binary_hash) && hex_hash(imp.semantic_hash),
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 import hash is malformed");
    require(import_modules.insert(imp.module_id).second,
            "SOURCE_FREE_CONTRACT_INVALID", "Duplicate import module ID");
  }

  std::set<std::string> known_provenances;
  for (const auto& rec : sealed.value.provenance) {
    validate_provenance(rec);
    known_provenances.insert(rec.id);
  }

  std::map<std::string, NominalTemplate> known_templates;
  std::vector<ConcreteInstance> known_instances;
  for (const auto& rec : sealed.value.binary_contract) {
    // Versioned records use distinct identities: declaration IDs are nominal
    // strings, while concrete instance keys are SHA-256. Never swallow a
    // malformed versioned record and fall through to the legacy text section.
    if (rec.payload.empty()) continue;
    require(rec.payload[0] == 1 ||
                static_cast<unsigned char>(rec.payload[0]) >= 32,
            "SOURCE_FREE_CONTRACT_INVALID",
            "Unknown ABI-v10 binary record version");
    if (rec.payload[0] != 1) continue;
    if (nominal_template_id_valid(rec.id)) {
      const auto tmpl = read_template(rec.payload);
      validate_template(rec);
      known_templates[tmpl.template_id] = tmpl;
    } else if (hex_hash(rec.id)) {
      const auto inst = read_instance(rec.payload);
      require(instance_key(inst) == rec.id,
              "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
              "ConcreteInstance key mismatch");
      known_instances.push_back(inst);
    } else {
      throw Error("SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                  "ABI-v10 binary record has an unknown canonical identity");
    }
  }

  std::vector<Summary> public_summaries;
  std::vector<MethodTemplate> public_methods;
  std::vector<Record> key_proof_records;
  for (const auto& rec : sealed.value.semantic_contract) {
    if (rec.payload.empty()) continue;
    require(rec.payload[0] == 1 ||
                static_cast<unsigned char>(rec.payload[0]) >= 32,
            "SOURCE_FREE_CONTRACT_INVALID",
            "Unknown ABI-v10 semantic record version");
    if (rec.payload[0] != 1) continue;
    // The record ID selects the versioned schema. A failed decode is fatal;
    // trying other schemas could hide a forged or truncated public contract.
    if (nominal_template_id_valid(rec.id)) {
      const auto tmpl = read_template(rec.payload);
      validate_template(rec);
      require(known_templates.emplace(tmpl.template_id, tmpl).second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Duplicate nominal template record ID");
    } else if (is_key_proof_payload(rec.payload)) {
      validate_key_proof(rec);
      key_proof_records.push_back(rec);
    } else if (hex_hash(rec.id)) {
      const auto sum = read_summary(rec.payload);
      validate_summary(rec);
      public_summaries.push_back(sum);
    } else {
      const auto method = read_method_template(rec.payload);
      validate_method_template(rec);
      public_methods.push_back(method);
    }
  }

  std::map<std::string, Summary> private_summaries;
  std::map<std::string, MethodTemplate> private_methods;
  std::map<std::string, CheckedBodyRecord> private_bodies;
  for (const auto& rec : sealed.value.private_semantic_dependencies) {
    if (rec.payload.empty()) continue;
    require(rec.payload[0] == 1 ||
                static_cast<unsigned char>(rec.payload[0]) >= 32,
            "SOURCE_FREE_CONTRACT_INVALID",
            "Unknown ABI-v10 private dependency record version");
    if (rec.payload[0] != 1) continue;
    if (rec.id.rfind("checked-body-v1:", 0) == 0) {
      const auto body = read_checked_body_record(rec.payload);
      validate_checked_body_record(rec);
      require(private_bodies.emplace(body.callable_id, body).second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Duplicate checked body record ID");
    } else if (nominal_template_id_valid(rec.id)) {
      const auto tmpl = read_template(rec.payload);
      validate_template(rec);
      require(known_templates.emplace(tmpl.template_id, tmpl).second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Duplicate nominal template record ID");
    } else if (hex_hash(rec.id)) {
      const auto sum = read_summary(rec.payload);
      validate_summary(rec);
      require(private_summaries.emplace(sum.callable_id, sum).second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Duplicate private callable summary ID");
    } else {
      const auto method = read_method_template(rec.payload);
      validate_method_template(rec);
      require(private_methods.emplace(method.method_id, method).second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Duplicate private method template ID");
    }
  }

  const auto validate_nominal_references = [&](const WireType& root) {
    std::function<void(const WireType&)> visit = [&](const WireType& type) {
      if (type.kind == WireTypeKind::Nominal) {
        require(nominal_template_id_valid(type.identity),
                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                "Nominal TypeExpr has a forged template ID");
        const auto found = known_templates.find(type.identity);
        if (found != known_templates.end()) {
          require(type.arguments.size() == found->second.variable_count,
                  "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                  "Nominal TypeExpr has the wrong template argument count");
        } else {
          constexpr std::string_view prefix = "nominal-template-v1:";
          const auto separator = type.identity.rfind(":type:");
          const auto owner = type.identity.substr(
              prefix.size(), separator - prefix.size());
          require(owner != sealed.value.module_id &&
                      import_modules.count(owner) > 0,
                  "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                  "Nominal TypeExpr references an unknown provider template");
        }
      }
      for (const auto& argument : type.arguments) visit(argument);
    };
    visit(root);
  };
  for (const auto& sum : public_summaries) {
    for (const auto& parameter : sum.parameters)
      validate_nominal_references(parameter);
    validate_nominal_references(sum.result);
    for (const auto& requirement : sum.requirements)
      validate_nominal_references(requirement.receiver);
  }
  for (const auto& pair : private_summaries) {
    const auto& sum = pair.second;
    for (const auto& parameter : sum.parameters)
      validate_nominal_references(parameter);
    validate_nominal_references(sum.result);
    for (const auto& requirement : sum.requirements)
      validate_nominal_references(requirement.receiver);
  }
  for (const auto& method : public_methods) {
    for (const auto& parameter : method.parameters)
      validate_nominal_references(parameter);
    validate_nominal_references(method.result);
  }
  for (const auto& pair : private_methods) {
    const auto& method = pair.second;
    for (const auto& parameter : method.parameters)
      validate_nominal_references(parameter);
    validate_nominal_references(method.result);
  }

  for (const auto& inst : known_instances) {
    const auto it = known_templates.find(inst.template_id);
    require(it != known_templates.end(), "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "ConcreteInstance references unknown template ID: " + inst.template_id);
    if (!selected_provider_binary_id.empty()) {
      require(inst.provider_binary_id == selected_provider_binary_id,
              "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
              "ConcreteInstance provider_binary_id does not match selected provider");
    }
    validate_instance(instance_record(inst), it->second, inst.provider_binary_id);
  }

  std::map<std::string, std::string> callable_body_hashes;
  for (const auto& sum : public_summaries)
    require(callable_body_hashes.emplace(sum.callable_id,
                                         sum.body_semantic_hash).second,
            "SOURCE_FREE_CONTRACT_INVALID",
            "Duplicate public callable summary ID");
  for (const auto& pair : private_summaries)
    require(callable_body_hashes.emplace(pair.first,
                                         pair.second.body_semantic_hash).second,
            "SOURCE_FREE_CONTRACT_INVALID",
            "Callable summary appears in both public and private sections");
  for (const auto& pair : private_bodies) {
    const auto& body = pair.second;
    const auto owner = callable_body_hashes.find(body.callable_id);
    require(owner != callable_body_hashes.end() &&
                owner->second == body.semantic_hash,
            "SOURCE_FREE_SUMMARY_INVALID",
            "Checked body has no matching callable summary hash");
    for (const auto& dep : body.dependencies) {
      if (dep.kind == CheckedBodyRecord::Dependency::Kind::Callable) {
        const auto target = callable_body_hashes.find(dep.target_id);
        require(target != callable_body_hashes.end(),
                "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING",
                "Checked body references an unknown callable dependency");
        require(target->second == dep.expected_hash,
                "SOURCE_FREE_CONTRACT_INVALID",
                "Checked body callable dependency hash mismatch");
      } else {
        const auto target = known_templates.find(dep.target_id);
        require(target != known_templates.end(),
                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                "Checked body references an unknown nominal template");
        require(target->second.representation_hash == dep.expected_hash,
                "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
                "Checked body nominal representation hash mismatch");
      }
    }
  }

  std::set<std::string> required_deps;
  for (const auto& sum : public_summaries) {
    for (const auto& dep : sum.dependencies) {
      required_deps.insert(dep.first);
    }
  }
  for (const auto& m : public_methods) {
    for (const auto& dep : m.dependencies) {
      required_deps.insert(dep.first);
    }
  }
  std::vector<std::string> dep_worklist(required_deps.begin(), required_deps.end());
  std::set<std::string> visited_deps = required_deps;
  while (!dep_worklist.empty()) {
    const auto current = dep_worklist.back();
    dep_worklist.pop_back();
    const auto sum_it = private_summaries.find(current);
    if (sum_it != private_summaries.end()) {
      for (const auto& dep : sum_it->second.dependencies) {
        if (visited_deps.insert(dep.first).second) {
          dep_worklist.push_back(dep.first);
        }
      }
    }
  }
  for (const auto& dep : visited_deps)
    require(callable_body_hashes.count(dep) > 0,
            "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING",
            "Missing callable semantic dependency: " + dep);
  const auto validate_dependency_hashes = [&](const auto& dependencies) {
    for (const auto& dep : dependencies) {
      const auto found = callable_body_hashes.find(dep.first);
      require(found != callable_body_hashes.end(),
              "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING",
              "Missing callable semantic dependency: " + dep.first);
      require(dep.second == found->second,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Callable semantic dependency hash mismatch: " + dep.first);
    }
  };
  for (const auto& sum : public_summaries)
    validate_dependency_hashes(sum.dependencies);
  for (const auto& pair : private_summaries)
    validate_dependency_hashes(pair.second.dependencies);
  for (const auto& method : public_methods)
    validate_dependency_hashes(method.dependencies);
  for (const auto& pair : private_methods)
    validate_dependency_hashes(pair.second.dependencies);

  if (!key_proof_records.empty()) {
    validate_key_proof_coherence(key_proof_records);
  }

  // Referential integrity checks across templates, methods, summaries, and provenance
  for (const auto& pair : known_templates) {
    if (!pair.second.provenance_id.empty()) {
      require(known_provenances.count(pair.second.provenance_id) > 0,
              "SOURCE_FREE_CONTRACT_INVALID",
              "NominalTemplate references unknown provenance ID: " + pair.second.provenance_id);
    }
    for (const auto& f : pair.second.fields) {
      if (!f.provenance_id.empty()) {
        require(known_provenances.count(f.provenance_id) > 0,
                "SOURCE_FREE_CONTRACT_INVALID",
                "Field references unknown provenance ID: " + f.provenance_id);
      }
    }
    for (const auto& mid : pair.second.method_template_ids) {
      bool method_found = false;
      for (const auto& m : public_methods) {
        if (m.method_id == mid) { method_found = true; break; }
      }
      if (!method_found && private_methods.count(mid) > 0) method_found = true;
      require(method_found, "SOURCE_FREE_CONTRACT_INVALID",
              "NominalTemplate references unknown method template: " + mid);
    }
  }

  for (const auto& sum : public_summaries) {
    if (!sum.provenance_id.empty()) {
      require(known_provenances.count(sum.provenance_id) > 0,
              "SOURCE_FREE_CONTRACT_INVALID",
              "Summary references unknown provenance ID: " + sum.provenance_id);
    }
    for (const auto& req : sum.requirements) {
      if (!req.provenance_id.empty()) {
        require(known_provenances.count(req.provenance_id) > 0,
                "SOURCE_FREE_CONTRACT_INVALID",
                "Requirement references unknown provenance ID: " + req.provenance_id);
      }
    }
  }

  for (const auto& m : public_methods) {
    if (!m.provenance_id.empty()) {
      require(known_provenances.count(m.provenance_id) > 0,
              "SOURCE_FREE_CONTRACT_INVALID",
              "MethodTemplate references unknown provenance ID: " + m.provenance_id);
    }
    for (const auto& req : m.requirements) {
      if (!req.provenance_id.empty()) {
        require(known_provenances.count(req.provenance_id) > 0,
                "SOURCE_FREE_CONTRACT_INVALID",
                "Method requirement references unknown provenance ID: " + req.provenance_id);
      }
    }
    require(known_templates.count(m.template_id) > 0,
            "SOURCE_FREE_TYPE_IDENTITY_MISMATCH",
            "MethodTemplate references unknown template ID: " + m.template_id);
  }
}

}  // namespace moss::abi_v10
