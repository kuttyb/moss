#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace moss {

// The .mossmap format is the shared, compiler-owned provenance contract used by
// editor, debugger, and native-code inspection tools.  Numeric compiler IR IDs
// are intentionally absent: every identity below is derived from Moss source
// and semantic context.
struct DebugGeneratedRange {
  std::string file;
  int start_line = 0;
  int start_column = 1;
  int end_line = 0;
  int end_column = 1;
};

struct DebugSourceSpan {
  std::string file;
  int start_line = 0;
  int start_column = 1;
  int end_line = 0;
  int end_column = 1;
};

struct DebugLineMapping {
  int moss_line = 0;
  int generated_rust_line = 0;
};

struct DebugMapEntry {
  std::string semantic_identity;
  std::string construct_kind;
  DebugSourceSpan source;
  DebugGeneratedRange generated;
  std::string generated_symbol;
  std::string native_symbol;
  std::vector<std::string> provenance;
  std::vector<DebugLineMapping> line_mappings;
};

struct DebugMap {
  int format_version = 1;
  std::string source_file;
  std::string generated_rust_file;
  std::string native_executable;
  bool debug_build = false;
  bool optimized = false;
  std::vector<DebugMapEntry> entries;
};

inline std::string debug_json_escape(const std::string& value) {
  static const char* digits = "0123456789abcdef";
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 0x20) {
          escaped += "\\u00";
          escaped.push_back(digits[(c >> 4) & 0x0f]);
          escaped.push_back(digits[c & 0x0f]);
        } else {
          escaped.push_back(static_cast<char>(c));
        }
    }
  }
  return escaped;
}

inline void write_debug_json_string(std::ostream& out,
                                    const std::string& value) {
  out << '"' << debug_json_escape(value) << '"';
}

inline void write_debug_map(std::ostream& out, const DebugMap& map) {
  out << "{\n";
  out << "  \"format\": \"moss-debug-map\",\n";
  out << "  \"version\": " << map.format_version << ",\n";
  out << "  \"source_file\": ";
  write_debug_json_string(out, map.source_file);
  out << ",\n  \"generated_rust_file\": ";
  write_debug_json_string(out, map.generated_rust_file);
  out << ",\n  \"native_executable\": ";
  write_debug_json_string(out, map.native_executable);
  out << ",\n  \"debug_build\": " << (map.debug_build ? "true" : "false");
  out << ",\n  \"optimized\": " << (map.optimized ? "true" : "false");
  out << ",\n  \"entries\": [\n";
  for (std::size_t index = 0; index < map.entries.size(); ++index) {
    const auto& entry = map.entries[index];
    out << "    {\n";
    out << "      \"semantic_identity\": ";
    write_debug_json_string(out, entry.semantic_identity);
    out << ",\n      \"construct_kind\": ";
    write_debug_json_string(out, entry.construct_kind);
    out << ",\n      \"source\": {\"file\": ";
    write_debug_json_string(out, entry.source.file);
    out << ", \"start_line\": " << entry.source.start_line
        << ", \"start_column\": " << entry.source.start_column
        << ", \"end_line\": " << entry.source.end_line
        << ", \"end_column\": " << entry.source.end_column << "},\n";
    out << "      \"generated\": {\"file\": ";
    write_debug_json_string(out, entry.generated.file);
    out << ", \"start_line\": " << entry.generated.start_line
        << ", \"start_column\": " << entry.generated.start_column
        << ", \"end_line\": " << entry.generated.end_line
        << ", \"end_column\": " << entry.generated.end_column << "},\n";
    out << "      \"generated_symbol\": ";
    write_debug_json_string(out, entry.generated_symbol);
    out << ",\n      \"native_symbol\": ";
    write_debug_json_string(out, entry.native_symbol);
    out << ",\n      \"provenance\": [";
    for (std::size_t origin = 0; origin < entry.provenance.size(); ++origin) {
      if (origin) out << ", ";
      write_debug_json_string(out, entry.provenance[origin]);
    }
    out << "],\n      \"line_mappings\": [";
    for (std::size_t line = 0; line < entry.line_mappings.size(); ++line) {
      if (line) out << ", ";
      out << "{\"moss_line\": " << entry.line_mappings[line].moss_line
          << ", \"generated_rust_line\": "
          << entry.line_mappings[line].generated_rust_line << "}";
    }
    out << "]\n    }";
    if (index + 1 != map.entries.size()) out << ',';
    out << '\n';
  }
  out << "  ]\n}\n";
}

}  // namespace moss
