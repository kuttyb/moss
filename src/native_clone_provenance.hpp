#pragma once

#include "source_free_abi_v9.hpp"

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace moss {

// Byte coordinates are retained alongside one-based, end-exclusive source
// coordinates so the serializer can bind the checked fact to exact bytes.
struct NativeCloneSpanV2 {
  std::uint32_t start_line{0}, start_column{0};
  std::uint32_t end_line{0}, end_column{0};
  std::uint64_t source_begin{0}, source_end{0};
};

struct NativeCheckedCopyFactV1 {
  std::string source_file;
  std::string source_bytes;
  std::uint32_t est_arena_index{0};
  std::uint32_t copy_expression_id{0}, source_expression_id{0};
  NativeCloneSpanV2 copy_occurrence_span, source_occurrence_span;
  std::uint32_t statement_occurrence_id{0};
  std::string specialization_identity, specialization_context,
      est_fact_context;
  std::string source_type_key, result_type_key;
  std::string required_source_access{"READ"};
  bool creates_independent_owner{false};
  std::string availability{"Present"};
  std::string provenance_source_file;
  std::uint32_t provenance_line{0};
  std::string defining_module_id, enclosing_callable_id;
};

struct NativeCloneSourceCopyV2 {
  NativeCheckedCopyFactV1 checked_fact;
  std::vector<NativeCloneSpanV2> rust_clone_spans;
};

struct NativeCloneOccurrenceV2 {
  NativeCloneSpanV2 rust_span;
  std::string origin_kind{"unclassified"};
  std::string source_copy_id;
  NativeCloneSpanV2 rust_context_span;
  // For an explicit_copy this is generated from the exact checked fact row.
  // Other origins remain unclassified until independent evidence authorizes it.
  std::string semantic_type{"unclassified"};
  std::string ownership_fact{"unclassified"};
  std::string semantic_identity{"unclassified"};
};

struct NativeCloneProvenanceV2 {
  std::string mode{"debug"};
  std::vector<NativeCloneSourceCopyV2> source_copy_occurrences;
  std::vector<NativeCloneOccurrenceV2> occurrences;
};

inline std::string native_clone_json_string(std::string_view value) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 0xf];
        } else out += static_cast<char>(c);
    }
  }
  out += '"';
  return out;
}

inline std::string native_clone_fact_span_json(const NativeCloneSpanV2& span) {
  return "{\"end_column\":" + std::to_string(span.end_column) +
      ",\"end_line\":" + std::to_string(span.end_line) +
      ",\"source_begin\":" + std::to_string(span.source_begin) +
      ",\"source_end\":" + std::to_string(span.source_end) +
      ",\"start_column\":" + std::to_string(span.start_column) +
      ",\"start_line\":" + std::to_string(span.start_line) + "}";
}

inline std::string native_clone_span_json(const NativeCloneSpanV2& span) {
  return "{\"start\":{\"line\":" + std::to_string(span.start_line) +
      ",\"column\":" + std::to_string(span.start_column) +
      "},\"end\":{\"line\":" + std::to_string(span.end_line) +
      ",\"column\":" + std::to_string(span.end_column) + "}}";
}

inline std::string native_clone_source_span_json(const NativeCloneSpanV2& span) {
  auto json = native_clone_span_json(span);
  json.pop_back();
  return json + ",\"copy_id\":\"copy@" + std::to_string(span.start_line) +
      ":" + std::to_string(span.start_column) + "\"}";
}

inline std::string native_clone_source_copy_id(const NativeCloneSpanV2& span) {
  return "copy@" + std::to_string(span.start_line) + ":" +
      std::to_string(span.start_column);
}

inline std::string native_checked_copy_fact_json(
    const NativeCheckedCopyFactV1& fact) {
  if (fact.availability != "Present" || fact.required_source_access != "READ" ||
      !fact.creates_independent_owner || fact.source_type_key.empty() ||
      fact.result_type_key.empty() || fact.source_bytes.empty() ||
      fact.source_file.empty() || fact.specialization_context.empty() ||
      fact.est_fact_context.empty())
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT invalid checked copy provenance fact");
  const auto source_hash = "sha256:" + source_free_contract_sha256(fact.source_bytes);
  return "{\"availability\":\"Present\",\"copy_expression_id\":" +
      std::to_string(fact.copy_expression_id) +
      ",\"copy_occurrence_span\":" + native_clone_fact_span_json(fact.copy_occurrence_span) +
      ",\"creates_independent_owner\":true,\"est_arena_index\":" +
      std::to_string(fact.est_arena_index) +
      ",\"est_fact_context\":" + native_clone_json_string(fact.est_fact_context) +
      ",\"provenance\":{\"enclosing_callable_id\":" +
      native_clone_json_string(fact.enclosing_callable_id) +
      ",\"defining_module_id\":" + native_clone_json_string(fact.defining_module_id) +
      ",\"line\":" + std::to_string(fact.provenance_line) +
      ",\"source_file\":" + native_clone_json_string(fact.provenance_source_file) +
      "},\"required_source_access\":\"READ\",\"result_type_key\":" +
      native_clone_json_string(fact.result_type_key) +
      ",\"source_expression_id\":" + std::to_string(fact.source_expression_id) +
      ",\"source_file\":" + native_clone_json_string(fact.source_file) +
      ",\"source_occurrence_span\":" + native_clone_fact_span_json(fact.source_occurrence_span) +
      ",\"source_sha256\":" + native_clone_json_string(source_hash) +
      ",\"source_type_key\":" + native_clone_json_string(fact.source_type_key) +
      ",\"specialization_context\":" + native_clone_json_string(fact.specialization_context) +
      ",\"specialization_identity\":" + native_clone_json_string(fact.specialization_identity) +
      ",\"statement_occurrence_id\":" + std::to_string(fact.statement_occurrence_id) + "}";
}

inline std::string native_checked_copy_identity(
    const NativeCheckedCopyFactV1& fact) {
  const auto row = native_checked_copy_fact_json(fact);
  return "typed-copy-fact-v1:" + source_free_contract_sha256(row);
}

// Serializes the frozen v2 manifest. Rust spans carry exact byte offsets and
// coordinates supplied by the occurrence scanner; context hashes bind to the
// exact context bytes in the emitted artifact.
inline void write_native_clone_provenance_v2(
    std::ostream& out, std::string_view rust_bytes,
    std::string_view source_bytes, const NativeCloneProvenanceV2& manifest) {
  if (manifest.mode != "debug" && manifest.mode != "optimized")
    throw std::runtime_error("invalid native clone provenance mode");
  const auto emit_span = [&](const NativeCloneSpanV2& span) {
    if (span.source_begin > span.source_end || span.source_end > rust_bytes.size())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT clone span outside Rust artifact");
    out << native_clone_span_json(span);
  };
  out << "{\"format\":\"moss-clone-provenance\",\"version\":2,\"mode\":"
      << native_clone_json_string(manifest.mode)
      << ",\"rust_sha256\":"
      << native_clone_json_string(source_free_contract_sha256(rust_bytes))
      << ",\"source_sha256\":"
      << native_clone_json_string(source_free_contract_sha256(source_bytes))
      << ",\"source_copy_occurrences\":[";
  for (std::size_t i = 0; i < manifest.source_copy_occurrences.size(); ++i) {
    if (i) out << ',';
    const auto& row = manifest.source_copy_occurrences[i];
    if (row.checked_fact.source_bytes != source_bytes)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT copy fact source digest disagrees with artifact source");
    const auto& copy_span = row.checked_fact.copy_occurrence_span;
    const auto& source_span = row.checked_fact.source_occurrence_span;
    if (copy_span.source_end > source_bytes.size() ||
        copy_span.source_begin >= copy_span.source_end ||
        source_span.source_begin < copy_span.source_begin ||
        source_span.source_end > copy_span.source_end ||
        source_span.source_begin >= source_span.source_end ||
        source_bytes.substr(copy_span.source_begin, 4) != "copy")
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT invalid checked copy source spans");
    const auto fact_json = native_checked_copy_fact_json(row.checked_fact);
    out << "{\"copy_id\":" << native_clone_json_string(
            native_clone_source_copy_id(row.checked_fact.copy_occurrence_span))
        << ",\"checked_copy_fact\":" << fact_json
        << ",\"source_span\":";
    // Source spans use the same coordinate shape; validate against Moss bytes.
    out << native_clone_source_span_json(row.checked_fact.copy_occurrence_span)
        << ",\"semantic_status\":\"implemented\",\"rust_clone_spans\":[";
    for (std::size_t j = 0; j < row.rust_clone_spans.size(); ++j) {
      if (j) out << ',';
      emit_span(row.rust_clone_spans[j]);
    }
    out << "]}";
  }
  out << "],\"occurrences\":[";
  for (std::size_t i = 0; i < manifest.occurrences.size(); ++i) {
    if (i) out << ',';
    const auto& row = manifest.occurrences[i];
    if (row.rust_context_span.source_begin > row.rust_span.source_begin ||
        row.rust_context_span.source_end < row.rust_span.source_end ||
        row.rust_context_span.source_end > rust_bytes.size())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT clone context does not enclose occurrence");
    if (row.rust_span.source_begin >= row.rust_span.source_end ||
        rust_bytes.substr(row.rust_span.source_begin, 7) != ".clone(")
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT occurrence is not a Rust clone call");
    const auto context = rust_bytes.substr(
        row.rust_context_span.source_begin,
        row.rust_context_span.source_end - row.rust_context_span.source_begin);
    const NativeCheckedCopyFactV1* checked = nullptr;
    if (row.origin_kind == "explicit_copy") {
      for (const auto& source : manifest.source_copy_occurrences)
        if (native_clone_source_copy_id(source.checked_fact.copy_occurrence_span) ==
            row.source_copy_id) {
          if (checked) throw std::runtime_error("duplicate explicit copy identity");
          checked = &source.checked_fact;
        }
      if (!checked) throw std::runtime_error("explicit clone lacks checked copy source");
    }
    out << "{\"rust_span\":";
    emit_span(row.rust_span);
    out << ",\"origin_kind\":" << native_clone_json_string(row.origin_kind)
        << ",\"source_copy_id\":";
    if (row.origin_kind == "explicit_copy") out << native_clone_json_string(row.source_copy_id);
    else out << "null";
    out << ",\"evidence\":{\"rust_context_span\":";
    emit_span(row.rust_context_span);
    out << ",\"rust_context_sha256\":"
        << native_clone_json_string(source_free_contract_sha256(context))
        << ",\"semantic_type\":";
    if (checked) out << native_clone_json_string(checked->result_type_key);
    else out << native_clone_json_string(row.semantic_type);
    out << ",\"ownership_fact\":";
    if (checked) out << native_clone_json_string(checked->required_source_access);
    else out << native_clone_json_string(row.ownership_fact);
    out << ",\"semantic_identity\":";
    if (checked) out << native_clone_json_string(native_checked_copy_identity(*checked));
    else out << native_clone_json_string(row.semantic_identity);
    out << "}}";
  }
  out << "]}";
}

}  // namespace moss
