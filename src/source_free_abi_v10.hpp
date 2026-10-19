#pragma once

// Staged ABI-v10 container. Production still emits/loads ABI v9 until the
// coordinated Team S/Team G cutover. This module deliberately has no fallback
// to v9 or display-string type interpretation.

#include "source_free_abi_v9.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace moss::abi_v10 {

struct Error : std::runtime_error {
  std::string code;
  Error(std::string code_, std::string message)
      : std::runtime_error(message), code(std::move(code_)) {}
};

struct Import {
  std::string module_id;
  std::string binary_hash;
  std::string semantic_hash;
};

struct Record {
  std::string id;
  std::string payload;
};

struct Interface {
  std::string module_id;
  std::string compiler;
  std::string backend_fingerprint;
  std::string backend_rustc;
  std::vector<Import> imports;
  std::vector<Record> binary_contract;
  std::vector<Record> semantic_contract;
  std::vector<Record> private_semantic_dependencies;
  std::vector<Record> provenance;
};

struct SealedInterface {
  Interface value;
  std::string digest;
  std::string binary_hash;
  std::string semantic_hash;
  std::string interface_hash;
};

inline bool hex_hash(std::string_view value) {
  if (value.size() != 71 || value.substr(0, 7) != "sha256:") return false;
  return std::all_of(value.begin() + 7, value.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}

inline bool valid_utf8(std::string_view value) {
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i]);
    if (first < 0x80) { ++i; continue; }
    std::size_t length = 0;
    std::uint32_t scalar = 0;
    if (first >= 0xc2 && first <= 0xdf) { length = 2; scalar = first & 0x1f; }
    else if (first >= 0xe0 && first <= 0xef) { length = 3; scalar = first & 0x0f; }
    else if (first >= 0xf0 && first <= 0xf4) { length = 4; scalar = first & 0x07; }
    else return false;
    if (length > value.size() - i) return false;
    for (std::size_t j = 1; j < length; ++j) {
      const auto next = static_cast<unsigned char>(value[i + j]);
      if ((next & 0xc0) != 0x80) return false;
      scalar = (scalar << 6) | (next & 0x3f);
    }
    if ((length == 2 && scalar < 0x80) ||
        (length == 3 && scalar < 0x800) ||
        (length == 4 && scalar < 0x10000) ||
        (scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff)
      return false;
    i += length;
  }
  return true;
}

inline bool module_id_valid(std::string_view id) {
  if (id.substr(0, 10) != "module-v1:") return false;
  id.remove_prefix(10);
  const auto at = id.find('@');
  const auto split = id.find("::");
  if (at == std::string_view::npos || split == std::string_view::npos ||
      at == 0 || split <= at + 1 || split + 2 >= id.size()) return false;
  const auto valid_part = [](std::string_view part) {
    return std::all_of(part.begin(), part.end(), [](char ch) {
      return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
             (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    });
  };
  return valid_part(id.substr(0, at)) &&
         valid_part(id.substr(at + 1, split - at - 1)) &&
         valid_part(id.substr(split + 2));
}

struct ModuleIdentity {
  std::string package_name;
  std::string package_version;
  std::string module_name;
  std::string raw_id;
};

inline ModuleIdentity parse_module_id(std::string_view id) {
  if (!module_id_valid(id)) {
    throw Error("SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 module identity is malformed");
  }
  std::string_view rest = id.substr(10); // remove "module-v1:"
  const auto at = rest.find('@');
  const auto split = rest.find("::");
  ModuleIdentity result;
  result.package_name = std::string(rest.substr(0, at));
  result.package_version = std::string(rest.substr(at + 1, split - at - 1));
  result.module_name = std::string(rest.substr(split + 2));
  result.raw_id = std::string(id);
  return result;
}

inline std::string hash(std::string_view domain, std::string_view bytes) {
  std::string input(domain);
  input.push_back('\0');
  input.append(bytes);
  return "sha256:" + source_free_contract_sha256(input);
}

inline void require(bool condition, const char* code, const char* message) {
  if (!condition) throw Error(code, message);
}

inline void require(bool condition, const char* code, const std::string& message) {
  if (!condition) throw Error(code, message);
}

inline void validate_identity(const Interface& value) {
  require(module_id_valid(value.module_id), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 module identity is malformed");
  for (const auto* part : {&value.compiler, &value.backend_fingerprint,
                           &value.backend_rustc})
    require(!part->empty() && part->find('\n') == std::string::npos &&
                valid_utf8(*part), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 toolchain identity is malformed");
}

inline std::string frame_records(const std::vector<Record>& records,
                                 const char* section) {
  require(records.size() <= 100000, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 section count exceeds the limit");
  std::string result(section);
  result += "\ncount " + std::to_string(records.size()) + "\n";
  std::string previous;
  for (const auto& record : records) {
    require(!record.id.empty() && record.id.find('\n') == std::string::npos &&
                valid_utf8(record.id) &&
                record.payload.size() <= 1 << 20 &&
                (previous.empty() || previous < record.id),
            "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 record is malformed, oversized, or out of order");
    previous = record.id;
    result += "record " + std::to_string(record.id.size()) + " " +
              std::to_string(record.payload.size()) + "\n";
    result += record.id;
    result += record.payload;
    result += '\n';
  }
  return result;
}

inline std::string frame_imports(const std::vector<Import>& imports) {
  require(imports.size() <= 100000, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 import count exceeds the limit");
  std::string result = "imports\ncount " + std::to_string(imports.size()) + "\n";
  std::string previous;
  for (const auto& item : imports) {
    require(module_id_valid(item.module_id) && hex_hash(item.binary_hash) &&
                hex_hash(item.semantic_hash) &&
                (previous.empty() || previous < item.module_id),
            "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 import identity, hash, or order is invalid");
    previous = item.module_id;
    result += item.module_id + " " + item.binary_hash + " " +
              item.semantic_hash + "\n";
  }
  return result;
}

inline std::string canonical_body(const Interface& value,
                                  std::string* binary_hash = nullptr,
                                  std::string* semantic_hash = nullptr) {
  validate_identity(value);
  const auto imports = frame_imports(value.imports);
  const auto binary = frame_records(value.binary_contract, "binary_contract");
  const auto semantic = frame_records(value.semantic_contract, "semantic_contract");
  const auto dependencies = frame_records(value.private_semantic_dependencies,
                                          "private_semantic_dependencies");
  const auto provenance = frame_records(value.provenance, "provenance");
  if (binary_hash) *binary_hash = hash("moss-abi-v10-binary-contract", binary);
  if (semantic_hash)
    *semantic_hash = hash("moss-abi-v10-semantic-contract",
                          semantic + dependencies);
  return imports + binary + semantic + dependencies + provenance;
}

inline std::string encode(const Interface& value) {
  std::string binary_hash, semantic_hash;
  const auto body = canonical_body(value, &binary_hash, &semantic_hash);
  const std::string header =
      "moss-module-interface-v3\nmodule_id " + value.module_id +
      "\nschema_version 10\nnative_abi 10\ncompiler " + value.compiler +
      "\nbackend_fingerprint " + value.backend_fingerprint +
      "\nbackend_rustc " + value.backend_rustc +
      "\nbinary_contract_hash " + binary_hash +
      "\nsemantic_contract_hash " + semantic_hash + "\n";
  // The interface hash excludes its own field and the provider digest to avoid
  // self-reference. The digest covers their exact emitted bytes.
  const auto interface_hash = hash("moss-abi-v10-interface", header + body);
  const auto material = header + "interface_hash " + interface_hash + "\n" + body;
  const auto digest = source_free_contract_sha256(material);
  return "moss-module-interface-v3\nprovider_contract_digest sha256:" +
         digest + "\n" + material.substr(sizeof("moss-module-interface-v3\n") - 1);
}

class Reader {
 public:
  explicit Reader(std::string_view input) : input_(input) {}
  std::string line() {
    const auto end = input_.find('\n', position_);
    require(end != std::string_view::npos, "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 record line is truncated");
    std::string result(input_.substr(position_, end - position_));
    position_ = end + 1;
    require(valid_utf8(result) && result.find('\r') == std::string::npos,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 line is not canonical UTF-8");
    return result;
  }
  std::string bytes(std::size_t count) {
    require(count <= input_.size() - position_, "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 framed record is truncated");
    std::string result(input_.substr(position_, count));
    position_ += count;
    return result;
  }
  bool done() const { return position_ == input_.size(); }
 private:
  std::string_view input_;
  std::size_t position_ = 0;
};

inline std::size_t count_number(std::string_view text, std::size_t bound) {
  require(!text.empty() && text.size() <= 10 &&
              (text.size() == 1 || text.front() != '0'),
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 count is malformed");
  std::size_t value = 0;
  for (char ch : text) {
    require(ch >= '0' && ch <= '9' &&
                static_cast<std::size_t>(ch - '0') <= bound &&
                value <= (bound - static_cast<std::size_t>(ch - '0')) / 10,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 count exceeds the limit");
    value = value * 10 + (ch - '0');
  }
  return value;
}

inline std::string field(Reader& reader, const char* name) {
  const std::string line = reader.line();
  const std::string prefix = std::string(name) + " ";
  require(line.compare(0, prefix.size(), prefix) == 0,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 header fields are missing or out of order");
  return line.substr(prefix.size());
}

inline std::vector<Record> read_records(Reader& reader, const char* section) {
  require(reader.line() == section, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 section is missing or out of order");
  const auto size = count_number(field(reader, "count"), 100000);
  std::vector<Record> records;
  records.reserve(size);
  std::string previous;
  for (std::size_t i = 0; i < size; ++i) {
    const auto frame = field(reader, "record");
    const auto split = frame.find(' ');
    require(split != std::string::npos && frame.find(' ', split + 1) == std::string::npos,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 frame is malformed");
    const auto id_size = count_number(frame.substr(0, split), 1024);
    const auto payload_size = count_number(frame.substr(split + 1), 1 << 20);
    Record record{reader.bytes(id_size), reader.bytes(payload_size)};
    require(reader.line().empty(), "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 frame has trailing data");
    require(previous.empty() || previous < record.id, "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 record is duplicate, malformed, or out of order");
    previous = record.id;
    records.push_back(std::move(record));
  }
  return records;
}

// Framing and header parse only (validation step 1 of ABI_V10_DESIGN.md §5).
// Callers that must distinguish an artifact-seal mismatch from a malformed
// interface check the digest and rlib marker before `verify_canonical`.
inline SealedInterface parse(std::string_view text) {
  if (text.substr(0, sizeof("moss-module-interface-v3\n") - 1) !=
      "moss-module-interface-v3\n")
    throw Error("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                "source-free provider requires an ABI-v10 source rebuild");
  require(text.size() <= 128 << 20, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 interface exceeds the size limit");
  Reader reader(text);
  require(reader.line() == "moss-module-interface-v3", "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 magic is malformed");
  const auto digest_text = field(reader, "provider_contract_digest");
  require(hex_hash(digest_text), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 provider digest is malformed");
  SealedInterface result;
  result.digest = digest_text.substr(7);
  result.value.module_id = field(reader, "module_id");
  if (field(reader, "schema_version") != "10" ||
      field(reader, "native_abi") != "10")
    throw Error("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                "source-free provider requires an ABI-v10 source rebuild");
  result.value.compiler = field(reader, "compiler");
  result.value.backend_fingerprint = field(reader, "backend_fingerprint");
  result.value.backend_rustc = field(reader, "backend_rustc");
  result.binary_hash = field(reader, "binary_contract_hash");
  result.semantic_hash = field(reader, "semantic_contract_hash");
  result.interface_hash = field(reader, "interface_hash");
  require(hex_hash(result.binary_hash) && hex_hash(result.semantic_hash) &&
              hex_hash(result.interface_hash), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 section hash is malformed");
  require(reader.line() == "imports", "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 imports section is missing");
  const auto import_count = count_number(field(reader, "count"), 100000);
  std::string prev_import;
  for (std::size_t i = 0; i < import_count; ++i) {
    const auto line = reader.line();
    const auto first = line.find(' '), second = line.find(' ', first + 1);
    require(first != std::string::npos && second != std::string::npos &&
                line.find(' ', second + 1) == std::string::npos,
            "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 import record is malformed");
    const auto mod_id = line.substr(0, first);
    require(prev_import.empty() || prev_import < mod_id, "SOURCE_FREE_CONTRACT_INVALID",
            "ABI-v10 import order is invalid or duplicate");
    prev_import = mod_id;
    result.value.imports.push_back({mod_id,
                                     line.substr(first + 1, second - first - 1),
                                     line.substr(second + 1)});
  }
  result.value.binary_contract = read_records(reader, "binary_contract");
  result.value.semantic_contract = read_records(reader, "semantic_contract");
  result.value.private_semantic_dependencies =
      read_records(reader, "private_semantic_dependencies");
  result.value.provenance = read_records(reader, "provenance");
  require(reader.done(), "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 interface has trailing data");
  return result;
}

// The provider digest covers the exact interface bytes with its own line
// removed (ABI_V10_DESIGN.md §2).
inline bool digest_matches(std::string_view text, const SealedInterface& parsed) {
  const std::string prefix = "moss-module-interface-v3\nprovider_contract_digest sha256:";
  if (text.substr(0, prefix.size()) != prefix) return false;
  const auto line_end = text.find('\n', prefix.size());
  if (line_end == std::string_view::npos) return false;
  std::string material("moss-module-interface-v3\n");
  material.append(text.substr(line_end + 1));
  return source_free_contract_sha256(material) == parsed.digest;
}

// Re-emission verifies canonical order, framing, every section hash, full
// interface hash, and the exact-byte provider seal in one comparison.
inline void verify_canonical(std::string_view text, const SealedInterface& parsed) {
  require(encode(parsed.value) == text, "SOURCE_FREE_CONTRACT_INVALID",
          "ABI-v10 interface digest or canonical record validation failed");
}

inline SealedInterface decode(std::string_view text) {
  auto result = parse(text);
  verify_canonical(text, result);
  return result;
}

inline std::string provider_marker(std::string_view tooling_name,
                                   std::string_view digest) {
  require(!tooling_name.empty() && tooling_name.find(':') == std::string_view::npos &&
              digest.size() == 64,
          "SOURCE_FREE_CONTRACT_INVALID", "ABI-v10 marker identity is malformed");
  return "MOSS_ABI_V10_CONTRACT::" + std::string(tooling_name) + "::" +
         std::string(digest) + "::END";
}

// The selected rlib must contain one native-object marker. An .rmeta copy is
// deliberately ignored. The caller must pass the exact rlib selected for rustc.
inline void validate_selected_rlib(std::string_view archive,
                                   std::string_view tooling_name,
                                   std::string_view digest) {
  require(archive.substr(0, 8) == "!<arch>\n",
          "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", "ABI-v10 rlib is not an archive");
  const auto prefix = "MOSS_ABI_V10_CONTRACT::" + std::string(tooling_name) + "::";
  const auto expected = provider_marker(tooling_name, digest);
  std::size_t count = 0;
  for (std::size_t cursor = 8; cursor < archive.size();) {
    require(archive.size() - cursor >= 60 && archive.substr(cursor + 58, 2) == "`\n",
            "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", "ABI-v10 rlib member is malformed");
    const auto header = archive.substr(cursor, 60);
    const auto name = header.substr(0, 16);
    auto size = std::string(header.substr(48, 10));
    size.erase(0, size.find_first_not_of(' '));
    size.erase(size.find_last_not_of(' ') + 1);
    const auto length = count_number(size, archive.size());
    const auto begin = cursor + 60;
    require(length <= archive.size() - begin,
            "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH", "ABI-v10 rlib member is truncated");
    const auto is_archive_table = name.substr(0, 2) == "/ " ||
                                  name.substr(0, 2) == "//";
    if (name.find(".rmeta") == std::string_view::npos &&
        !is_archive_table) {
      auto member = archive.substr(begin, length);
      for (auto found = member.find(prefix); found != std::string_view::npos;
           found = member.find(prefix, found + 1)) {
        ++count;
        require(member.substr(found, expected.size()) == expected,
                "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                "ABI-v10 provider artifact seal disagrees with the interface");
      }
    }
    cursor = begin + length + (length & 1);
    require(cursor <= archive.size(), "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
            "ABI-v10 rlib padding is truncated");
  }
  require(count == 1, "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
          "ABI-v10 provider artifact needs exactly one native-object seal");
}

}  // namespace moss::abi_v10
