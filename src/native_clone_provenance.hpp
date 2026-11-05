#pragma once

#include "source_free_abi_v9.hpp"
#include "typed_semantic_api.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <ostream>
#include <set>
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

struct NativeCloneSourceCopyV3 {
  std::string copy_id;
  NativeCloneSpanV2 source_span;
  std::string semantic_status{"implemented"};
  std::vector<NativeCloneSpanV2> rust_clone_spans;
};

struct NativeCloneCheckedCopyFactV3 {
  std::string copy_id;
  std::vector<NativeCheckedCopyFactV1> facts;
};

struct NativeCloneProvenanceV3 {
  std::string mode{"debug"};
  std::vector<NativeCloneSourceCopyV3> source_copy_occurrences;
  std::vector<NativeCloneCheckedCopyFactV3> checked_copy_facts;
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
      ",\"provenance\":{\"defining_module_id\":" +
      native_clone_json_string(fact.defining_module_id) +
      ",\"enclosing_callable_id\":" + native_clone_json_string(fact.enclosing_callable_id) +
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

inline std::string native_clone_mask_rust_non_code(std::string_view rust) {
  std::string masked(rust);
  auto blank = [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i)
      if (masked[i] != '\n' && masked[i] != '\r') masked[i] = ' ';
  };
  std::size_t i = 0;
  while (i < rust.size()) {
    if (i + 1 < rust.size() && rust[i] == '/' && rust[i + 1] == '/') {
      auto end = rust.find('\n', i + 2);
      if (end == std::string_view::npos) end = rust.size();
      blank(i, end);
      i = end;
      continue;
    }
    if (i + 1 < rust.size() && rust[i] == '/' && rust[i + 1] == '*') {
      std::size_t depth = 1, end = i + 2;
      while (end < rust.size() && depth) {
        if (end + 1 < rust.size() && rust[end] == '/' && rust[end + 1] == '*') {
          ++depth; end += 2;
        } else if (end + 1 < rust.size() && rust[end] == '*' && rust[end + 1] == '/') {
          --depth; end += 2;
        } else ++end;
      }
      if (depth) throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated Rust comment in clone audit");
      blank(i, end); i = end; continue;
    }
    // Raw strings are common in embedded generated runtime text.
    if (rust[i] == 'r') {
      std::size_t quote = i + 1;
      while (quote < rust.size() && rust[quote] == '#') ++quote;
      if (quote < rust.size() && rust[quote] == '"') {
        const auto hashes = quote - i - 1;
        std::string terminator = "\"" + std::string(hashes, '#');
        auto end = rust.find(terminator, quote + 1);
        if (end == std::string_view::npos)
          throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated Rust raw string in clone audit");
        end += terminator.size(); blank(i, end); i = end; continue;
      }
    }
    if (rust[i] == '"') {
      std::size_t end = i + 1;
      bool closed = false;
      while (end < rust.size()) {
        if (rust[end] == '\\') { end += std::min<std::size_t>(2, rust.size() - end); continue; }
        if (rust[end++] == '"') { closed = true; break; }
      }
      if (!closed) throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated Rust string in clone audit");
      blank(i, end); i = end; continue;
    }
    if (rust[i] == '\'') {
      std::size_t end = i + 1;
      if (end < rust.size() && rust[end] == '\\') end += 2;
      else if (end < rust.size()) ++end;
      if (end < rust.size() && rust[end] == '\'') {
        ++end; blank(i, end); i = end; continue;
      }
    }
    ++i;
  }
  return masked;
}

inline bool native_clone_identifier_char(char value) {
  const auto c = static_cast<unsigned char>(value);
  return std::isalnum(c) || value == '_';
}

inline std::vector<std::size_t> native_clone_identifier_occurrences(
    std::string_view code, std::string_view identifier) {
  std::vector<std::size_t> result;
  for (std::size_t at = 0; (at = code.find(identifier, at)) != std::string_view::npos;
       at += identifier.size()) {
    const bool left = at == 0 || !native_clone_identifier_char(code[at - 1]);
    const auto end = at + identifier.size();
    const bool right = end == code.size() || !native_clone_identifier_char(code[end]);
    if (left && right) result.push_back(at);
  }
  return result;
}

inline std::string native_clone_normalize_callable_identity(std::string value) {
  const auto at = value.rfind('@');
  if (at != std::string::npos && at + 1 < value.size()) {
    std::size_t end = at + 1;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) ++end;
    if (end > at + 1 && (end == value.size() || value[end] == '#'))
      value.erase(at);
  }
  return value;
}

inline std::set<std::string> native_clone_tooling_function_inventory(
    std::string_view rust) {
  constexpr std::string_view prefix = "// Moss tooling begin|function|";
  std::set<std::string> identities;
  std::size_t at = 0;
  while ((at = rust.find(prefix, at)) != std::string_view::npos) {
    const auto begin = at + prefix.size();
    const auto end = rust.find('|', begin);
    if (end == std::string_view::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT malformed Moss function inventory marker");
    identities.insert(native_clone_normalize_callable_identity(
        std::string(rust.substr(begin, end - begin))));
    at = end + 1;
  }
  return identities;
}

inline std::size_t native_clone_matching_delimiter(
    std::string_view code, std::size_t open, char left, char right) {
  if (open >= code.size() || code[open] != left)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT invalid Rust helper delimiter");
  std::size_t depth = 0;
  for (std::size_t i = open; i < code.size(); ++i) {
    if (code[i] == left) ++depth;
    else if (code[i] == right && --depth == 0) return i;
  }
  throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated Rust helper delimiter");
}

inline NativeCloneSpanV2 native_checked_copy_source_span(
    std::string_view source_bytes, std::uint64_t begin, std::uint64_t end);

// Builds provenance from the raw artifact. Caller-supplied clone rows are not
// accepted: explicit-copy links come only from exact fact IDs in the emitted
// marker/helper/call graph; every other clone remains unclassified.
inline NativeCloneProvenanceV3 build_native_clone_provenance_v3(
    std::string mode, std::string_view rust_bytes,
    std::string_view source_bytes,
    std::vector<NativeCloneSourceCopyV3> lexical_copies,
    std::vector<NativeCloneCheckedCopyFactV3> exact_fact_groups) {
  NativeCloneProvenanceV3 manifest;
  manifest.mode = std::move(mode);
  manifest.source_copy_occurrences = std::move(lexical_copies);
  manifest.checked_copy_facts = std::move(exact_fact_groups);
  const auto code = native_clone_mask_rust_non_code(rust_bytes);
  struct FactBinding {
    std::string copy_id, helper, identity;
    const NativeCheckedCopyFactV1* fact{};
    std::size_t body_begin{}, body_end{};
  };
  std::map<std::string, std::string> source_copy_ids;
  for (const auto& row : manifest.source_copy_occurrences) {
    if (row.copy_id != native_clone_source_copy_id(row.source_span) ||
        !source_copy_ids.emplace(row.copy_id, row.copy_id).second)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT duplicate or invalid lexical copy row");
  }
  std::map<std::string, FactBinding> bindings;
  for (const auto& group : manifest.checked_copy_facts) {
    if (!source_copy_ids.count(group.copy_id) || group.facts.empty())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT checked-copy group lacks lexical source row");
    for (const auto& fact : group.facts) {
      if (fact.source_bytes != source_bytes || group.copy_id !=
          native_clone_source_copy_id(fact.copy_occurrence_span))
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT checked-copy source bytes/locator mismatch");
      const auto id = native_checked_copy_identity(fact);
      const auto helper = "__moss_copy_value_" + id.substr(19, 16);
      if (!bindings.emplace(id, FactBinding{group.copy_id, helper, id, &fact, 0, 0}).second)
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT duplicate checked-copy identity");
    }
  }
  if (manifest.checked_copy_facts.size() != manifest.source_copy_occurrences.size())
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT lexical copy lacks a checked-copy group");

  const std::string marker_prefix = "/* Moss copy fact call: ";
  std::set<std::string> marked_facts;
  std::size_t marker_at = 0;
  while ((marker_at = rust_bytes.find(marker_prefix, marker_at)) != std::string_view::npos) {
    const auto id_begin = marker_at + marker_prefix.size();
    const auto marker_end = rust_bytes.find(" */", id_begin);
    if (marker_end == std::string_view::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated explicit-copy marker");
    const std::string id(rust_bytes.substr(id_begin, marker_end - id_begin));
    auto binding = bindings.find(id);
    if (binding == bindings.end() || !marked_facts.insert(id).second) {
      std::string known;
      for (const auto& item : bindings) known += " [" + item.first + "]";
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unknown or duplicate explicit-copy marker: " + id + known);
    }
    std::size_t call = marker_end + 3;
    while (call < code.size() && std::isspace(static_cast<unsigned char>(code[call]))) ++call;
    const auto& helper = binding->second.helper;
    if (call + helper.size() > code.size() || code.substr(call, helper.size()) != helper ||
        (call + helper.size() < code.size() &&
         native_clone_identifier_char(code[call + helper.size()])))
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT explicit-copy marker is not adjacent to its helper call");
    auto open_call = call + helper.size();
    while (open_call < code.size() && std::isspace(static_cast<unsigned char>(code[open_call]))) ++open_call;
    if (open_call >= code.size() || code[open_call] != '(')
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT explicit-copy marker does not precede a direct call");

    const std::string function_prefix = "fn " + helper;
    auto def = code.find(function_prefix);
    if (def == std::string::npos || code.find(function_prefix, def + 1) != std::string::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper definition missing or duplicated");
    const auto helper_references = native_clone_identifier_occurrences(code, helper);
    if (helper_references.size() != 2 || helper_references[0] != def + 3 ||
        helper_references[1] != call)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper has extra references or callers");
    auto params_open = def + function_prefix.size();
    while (params_open < code.size() && std::isspace(static_cast<unsigned char>(code[params_open]))) ++params_open;
    const auto params_close = native_clone_matching_delimiter(code, params_open, '(', ')');
    auto body_open = code.find('{', params_close + 1);
    if (body_open == std::string::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper has no body");
    const auto body_close = native_clone_matching_delimiter(code, body_open, '{', '}');
    binding->second.body_begin = def;
    binding->second.body_end = body_close + 1;
    marker_at = marker_end + 3;
  }

  const auto function_inventory = native_clone_tooling_function_inventory(rust_bytes);
  for (auto& entry : bindings) {
    auto& binding = entry.second;
    if (binding.body_end) continue;
    // A base template fact can be uninstantiated only when its callable is
    // absent from the raw emitted-function inventory and an exact specialized
    // descendant for the same lexical copy was emitted.
    const auto& fact = *binding.fact;
    if (!fact.specialization_identity.empty())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT required checked-copy helper is absent from raw Rust");
    const auto base_context = native_clone_normalize_callable_identity(
        fact.specialization_context.empty() ? fact.enclosing_callable_id
                                            : fact.specialization_context);
    bool base_callable_emitted = function_inventory.count(base_context) != 0;
    std::string callable_name = base_context;
    if (callable_name.rfind("fn:", 0) == 0) callable_name.erase(0, 3);
    const auto generic = callable_name.find('<');
    if (generic != std::string::npos) callable_name.erase(generic);
    const auto context_suffix = callable_name.find('#');
    if (context_suffix != std::string::npos) callable_name.erase(context_suffix);
    const auto rust_fn_token = "fn " + callable_name;
    for (auto at = code.find(rust_fn_token); at != std::string::npos;
         at = code.find(rust_fn_token, at + 1)) {
      const auto end = at + rust_fn_token.size();
      if (end == code.size() || !native_clone_identifier_char(code[end])) {
        base_callable_emitted = true;
        break;
      }
    }
    if (base_callable_emitted)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT emitted base callable lacks its checked-copy helper");
    std::set<std::string> descendants;
    for (const auto& callable : function_inventory)
      if (callable.rfind(base_context + "<", 0) == 0)
        descendants.insert(callable);
    if (descendants.empty())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT uninstantiated copy template lacks specialization inventory");
    for (const auto& descendant : descendants) {
      bool exact_emitted_fact = false;
      for (const auto& sibling : bindings)
        if (sibling.second.copy_id == binding.copy_id && sibling.second.body_end &&
            native_clone_normalize_callable_identity(
                sibling.second.fact->specialization_context) == descendant)
          exact_emitted_fact = true;
      if (!exact_emitted_fact)
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT specialization inventory lacks exact checked-copy helper");
    }
  }

  std::map<std::string, NativeCloneSourceCopyV3*> source_rows;
  for (auto& row : manifest.source_copy_occurrences) {
    row.rust_clone_spans.clear();
    source_rows.emplace(row.copy_id, &row);
  }
  for (std::size_t at = 0; (at = code.find(".clone(", at)) != std::string::npos; at += 7) {
    const auto span = native_checked_copy_source_span(rust_bytes, at, at + 7);
    const FactBinding* owner = nullptr;
    for (const auto& entry : bindings) {
      const auto& candidate = entry.second;
      if (candidate.body_end && at >= candidate.body_begin && at + 7 <= candidate.body_end) {
        if (owner) throw std::runtime_error("MOSS_INTERNAL_INVARIANT clone lies in overlapping copy helpers");
        owner = &candidate;
      }
    }
    NativeCloneOccurrenceV2 occurrence;
    occurrence.rust_span = span;
    if (owner) {
      occurrence.origin_kind = "explicit_copy";
      occurrence.source_copy_id = owner->copy_id;
      occurrence.semantic_identity = owner->identity;
    } else {
      occurrence.origin_kind = "unclassified";
      occurrence.semantic_type = "unclassified";
      occurrence.ownership_fact = "unclassified";
      occurrence.semantic_identity = "unclassified";
    }
    const auto context_begin = owner ? owner->body_begin :
        (rust_bytes.rfind('\n', at) == std::string_view::npos ? 0 : rust_bytes.rfind('\n', at) + 1);
    const auto line_end = rust_bytes.find('\n', at);
    const auto context_end = owner ? owner->body_end :
        (line_end == std::string_view::npos ? rust_bytes.size() : line_end);
    occurrence.rust_context_span = native_checked_copy_source_span(
        rust_bytes, context_begin, std::max(context_begin + 1, context_end));
    if (owner)
      source_rows.at(owner->copy_id)->rust_clone_spans.push_back(span);
    manifest.occurrences.push_back(std::move(occurrence));
  }
  return manifest;
}

inline NativeCloneSpanV2 native_checked_copy_source_span(
    std::string_view source_bytes, std::uint64_t begin, std::uint64_t end) {
  if (begin >= end || end > source_bytes.size())
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT checked-copy span is outside source bytes");
  std::uint32_t start_line = 1, start_column = 1;
  std::uint32_t end_line = 1, end_column = 1;
  for (std::uint64_t index = 0; index <= end; ++index) {
    if (index == begin) {
      start_line = end_line;
      start_column = end_column;
    }
    if (index == end) break;
    if (source_bytes[index] == '\n') {
      ++end_line;
      end_column = 1;
    } else {
      ++end_column;
    }
  }
  return {start_line, start_column, end_line, end_column, begin, end};
}

inline NativeCheckedCopyFactV1 native_checked_copy_fact_from_typed(
    const TypedCopyFactV1& fact, std::string_view source_bytes) {
  if (!fact.is_authoritative())
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT cannot serialize unauthoritative TypedCopyFactV1");
  std::error_code error;
  const auto path = std::filesystem::absolute(
      std::filesystem::path(fact.provenance.source_file), error).lexically_normal();
  if (error)
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT cannot normalize explicit-copy source path");
  NativeCheckedCopyFactV1 row;
  row.source_file = path.string();
  row.source_bytes.assign(source_bytes);
  const auto& copy_local = fact.copy_occurrence_span;
  const auto& source_local = fact.source_occurrence_span;
  if (copy_local.source_end <= copy_local.source_begin ||
      source_local.source_begin < copy_local.source_begin ||
      source_local.source_end > copy_local.source_end ||
      source_local.source_end <= source_local.source_begin)
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT explicit-copy spans do not share one EST buffer");
  std::uint64_t line_start = 0;
  std::uint32_t current_line = 1;
  while (current_line < copy_local.line) {
    const auto newline = source_bytes.find('\n', line_start);
    if (newline == std::string_view::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT explicit-copy source line is out of bounds");
    line_start = newline + 1;
    ++current_line;
  }
  const std::uint64_t close_offset = line_start + copy_local.column - 1;
  if (copy_local.column == 0 || close_offset >= source_bytes.size() ||
      source_bytes[close_offset] != ')')
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT explicit-copy span does not identify its closing parenthesis");
  const std::uint64_t copy_end = close_offset + 1;
  const std::uint64_t copy_width =
      copy_local.source_end - copy_local.source_begin;
  if (copy_end < copy_width)
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT explicit-copy source range underflows file offsets");
  const std::uint64_t copy_begin = copy_end - copy_width;
  const std::uint64_t source_begin = copy_begin +
      (source_local.source_begin - copy_local.source_begin);
  const std::uint64_t source_end = copy_begin +
      (source_local.source_end - copy_local.source_begin);
  row.est_arena_index = fact.est_arena_index;
  row.copy_expression_id = fact.copy_occurrence_id;
  row.source_expression_id = fact.source_occurrence_id;
  row.copy_occurrence_span = native_checked_copy_source_span(
      source_bytes, copy_begin, copy_end);
  row.source_occurrence_span = native_checked_copy_source_span(
      source_bytes, source_begin, source_end);
  row.statement_occurrence_id = fact.statement_occurrence_id;
  row.specialization_identity = fact.specialization_identity;
  row.specialization_context = fact.specialization_context;
  row.est_fact_context = fact.est_fact_context;
  row.source_type_key = fact.source_type->stable_type_key();
  row.result_type_key = fact.result_type->stable_type_key();
  row.required_source_access = fact.required_source_access == Effect::Read
      ? "READ" : "UNSUPPORTED";
  row.creates_independent_owner = fact.creates_independent_owner;
  row.availability = fact.availability == TypedCopyFactV1::Availability::Present
      ? "Present" : "Missing";
  row.provenance_source_file = row.source_file;
  row.provenance_line = static_cast<std::uint32_t>(fact.provenance.line);
  row.defining_module_id = fact.provenance.defining_module_id;
  row.enclosing_callable_id = fact.provenance.enclosing_callable_id;
  return row;
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

// Serializes the independently auditable v3 manifest. Exact checker rows are
// top-level and may contain several specialization facts for one lexical
// copy@line:column locator.
inline void write_native_clone_provenance_v3(
    std::ostream& out, std::string_view rust_bytes,
    std::string_view source_bytes, const NativeCloneProvenanceV3& manifest) {
  if (manifest.mode != "debug" && manifest.mode != "optimized")
    throw std::runtime_error("invalid native clone provenance mode");
  const auto emit_rust_span = [&](const NativeCloneSpanV2& span) {
    if (span.source_begin >= span.source_end || span.source_end > rust_bytes.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT clone span outside Rust artifact");
    out << native_clone_span_json(span);
  };
  const auto emit_source_span = [&](const NativeCloneSpanV2& span) {
    if (span.source_begin >= span.source_end || span.source_end > source_bytes.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked-copy span outside Moss source");
    out << native_clone_source_span_json(span);
  };
  std::map<std::string, const NativeCloneSourceCopyV3*> source_by_id;
  for (const auto& source : manifest.source_copy_occurrences) {
    if (source.copy_id != native_clone_source_copy_id(source.source_span) ||
        source.semantic_status != "implemented" ||
        source.source_span.source_end > source_bytes.size() ||
        source.source_span.source_begin >= source.source_span.source_end ||
        source_bytes.substr(source.source_span.source_begin, 4) != "copy" ||
        !source_by_id.emplace(source.copy_id, &source).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT invalid or duplicate lexical copy occurrence");
    for (const auto& clone : source.rust_clone_spans)
      if (clone.source_begin >= clone.source_end ||
          clone.source_end > rust_bytes.size() ||
          rust_bytes.substr(clone.source_begin, 7) != ".clone(")
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT invalid clone span on lexical copy row");
  }
  std::map<std::string, std::pair<std::string,
      const NativeCheckedCopyFactV1*>> fact_by_identity;
  std::map<std::string, std::vector<std::pair<std::string,
      const NativeCheckedCopyFactV1*>>> sorted_facts_by_copy;
  std::set<std::string> fact_group_ids;
  for (const auto& group : manifest.checked_copy_facts) {
    if (!source_by_id.count(group.copy_id) ||
        !fact_group_ids.insert(group.copy_id).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT checked fact group has no unique lexical source row");
    for (const auto& fact : group.facts) {
      if (fact.source_bytes != source_bytes ||
          group.copy_id != native_clone_source_copy_id(
              fact.copy_occurrence_span))
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT checked copy fact has no matching lexical source row");
      const auto identity = native_checked_copy_identity(fact);
      if (!fact_by_identity.emplace(identity,
              std::make_pair(group.copy_id, &fact)).second)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT duplicate exact checked copy fact identity");
      sorted_facts_by_copy[group.copy_id].emplace_back(identity, &fact);
    }
    if (group.facts.empty())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT lexical checked copy group has no specialization facts");
  }
  if (fact_group_ids.size() != source_by_id.size())
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT lexical copy occurrence lacks checked fact group");
  for (auto& group : sorted_facts_by_copy)
    std::sort(group.second.begin(), group.second.end(),
        [](const auto& left, const auto& right) {
          return left.first < right.first;
        });
  out << "{\"format\":\"moss-clone-provenance\",\"version\":3,\"mode\":"
      << native_clone_json_string(manifest.mode)
      << ",\"rust_sha256\":"
      << native_clone_json_string(source_free_contract_sha256(rust_bytes))
      << ",\"source_sha256\":"
      << native_clone_json_string(source_free_contract_sha256(source_bytes))
      << ",\"source_copy_occurrences\":[";
  for (std::size_t i = 0; i < manifest.source_copy_occurrences.size(); ++i) {
    if (i) out << ',';
    const auto& row = manifest.source_copy_occurrences[i];
    out << "{\"copy_id\":" << native_clone_json_string(row.copy_id)
        << ",\"source_span\":";
    emit_source_span(row.source_span);
    out << ",\"semantic_status\":"
        << native_clone_json_string(row.semantic_status)
        << ",\"rust_clone_spans\":[";
    for (std::size_t j = 0; j < row.rust_clone_spans.size(); ++j) {
      if (j) out << ',';
      emit_rust_span(row.rust_clone_spans[j]);
    }
    out << "]}";
  }
  out << "],\"checked_copy_facts\":[";
  for (std::size_t i = 0; i < manifest.checked_copy_facts.size(); ++i) {
    if (i) out << ',';
    const auto& group = manifest.checked_copy_facts[i];
    out << "{\"copy_id\":" << native_clone_json_string(group.copy_id)
        << ",\"facts\":[";
    const auto& facts = sorted_facts_by_copy.at(group.copy_id);
    for (std::size_t j = 0; j < facts.size(); ++j) {
      if (j) out << ',';
      out << native_checked_copy_fact_json(*facts[j].second);
    }
    out << "]}";
  }
  out << "],\"occurrences\":[";
  std::set<std::string> used_fact_identities;
  for (std::size_t i = 0; i < manifest.occurrences.size(); ++i) {
    if (i) out << ',';
    const auto& row = manifest.occurrences[i];
    if (row.rust_context_span.source_begin > row.rust_span.source_begin ||
        row.rust_context_span.source_end < row.rust_span.source_end ||
        row.rust_context_span.source_end > rust_bytes.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT clone context does not enclose occurrence");
    if (row.rust_span.source_begin >= row.rust_span.source_end ||
        rust_bytes.substr(row.rust_span.source_begin, 7) != ".clone(")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT occurrence is not a Rust clone call");
    const auto context = rust_bytes.substr(
        row.rust_context_span.source_begin,
        row.rust_context_span.source_end - row.rust_context_span.source_begin);
    const NativeCheckedCopyFactV1* checked = nullptr;
    if (row.origin_kind == "explicit_copy") {
      auto fact = fact_by_identity.find(row.semantic_identity);
      if (fact == fact_by_identity.end() ||
          fact->second.first != row.source_copy_id)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT explicit clone lacks exact fact-bound source");
      checked = fact->second.second;
      const auto identity = native_checked_copy_identity(*checked);
      const auto helper = "__moss_copy_value_" + identity.substr(19, 16);
      if (context.find(helper) == std::string_view::npos)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT explicit clone context is not its fact helper");
      used_fact_identities.insert(identity);
    }
    out << "{\"rust_span\":";
    emit_rust_span(row.rust_span);
    out << ",\"origin_kind\":" << native_clone_json_string(row.origin_kind)
        << ",\"source_copy_id\":";
    if (row.origin_kind == "explicit_copy")
      out << native_clone_json_string(row.source_copy_id);
    else
      out << "null";
    out << ",\"evidence\":{\"rust_context_span\":";
    emit_rust_span(row.rust_context_span);
    out << ",\"rust_context_sha256\":"
        << native_clone_json_string(source_free_contract_sha256(context))
        << ",\"semantic_type\":";
    if (checked) out << native_clone_json_string(checked->result_type_key);
    else out << native_clone_json_string(row.semantic_type);
    out << ",\"ownership_fact\":";
    if (checked)
      out << native_clone_json_string(checked->required_source_access);
    else
      out << native_clone_json_string(row.ownership_fact);
    out << ",\"semantic_identity\":";
    if (checked)
      out << native_clone_json_string(
          native_checked_copy_identity(*checked));
    else
      out << native_clone_json_string(row.semantic_identity);
    out << "}}";
  }
  out << "]}";
}

}  // namespace moss
