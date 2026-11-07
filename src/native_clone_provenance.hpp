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
  // Emitter projection metadata derived from the checked Program. It is not
  // part of the canonical checker-fact identity above.
  std::vector<std::string> generated_callable_symbols;
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
  std::string generated_function_variant_id;
  std::string generated_function_symbol;
  NativeCloneSpanV2 generated_function_span;
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

struct NativeRootIngressCaptureInputV1 {
  TypedRootIngressDomainCaptureFactV1 checked_fact;
  std::string source_bytes;
};

struct NativeRootIngressCaptureV1 {
  std::string capture_id;
  TypedRootIngressDomainCaptureFactV1 checked_fact;
  std::string source_sha256;
  NativeCloneSpanV2 rust_span;
};

struct NativeCloneProvenanceV3 {
  std::string mode{"debug"};
  std::vector<NativeCloneSourceCopyV3> source_copy_occurrences;
  std::vector<NativeCloneCheckedCopyFactV3> checked_copy_facts;
  std::vector<NativeRootIngressCaptureV1> root_ingress_domain_captures;
  struct RuntimeCloneSiteObservation {
    std::string site_id, operator_kind, source_file, expected_expression,
        semantic_type, provenance_status{"source_site_only_unapproved"};
    std::uint32_t generated_occurrence{0};
    NativeCloneSpanV2 rust_span, rust_context_span;
  };
  std::vector<RuntimeCloneSiteObservation> runtime_clone_site_observations;
  std::vector<NativeCloneOccurrenceV2> occurrences;
};

struct NativeRuntimeCloneSiteSpecV1 {
  const char* id;
  const char* operator_kind;
  const char* source_file;
  const char* expected_expression;
  const char* semantic_type;
  const char* source_sha256{nullptr};
  std::uint32_t source_line{0}, source_column{0}, source_end_column{0};
  std::uint64_t source_begin{0}, source_end{0};
};

inline const std::vector<NativeRuntimeCloneSiteSpecV1>&
native_runtime_clone_site_specs_v1() {
  static const std::vector<NativeRuntimeCloneSiteSpecV1> sites{
      {"r7-010-runtime-01", "direct_method", "src/executor_runtime.cpp",
       "self.lock_inner().active_executor.clone()", "Option<Arc<MossGlobalExec>>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",497,9,56,21185,21232},
      {"r7-010-runtime-02", "direct_method", "src/executor_runtime.cpp",
       "self.affinity.clone()", "Option<Vec<usize>>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",857,38,85,36876,36923},
      {"r7-010-runtime-03", "direct_method", "src/executor_runtime.cpp",
       "s.clone().into_raw_owned()", "MossBranchScope",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",1407,60,107,59291,59338},
      {"r7-010-runtime-04", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(&node)", "Arc<MossFairNode>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",58,44,91,2174,2221},
      {"r7-010-runtime-05", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(&waiter)", "Arc<MossFairWaiter>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",152,55,102,6497,6544},
      {"r7-010-runtime-06", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(scope)", "Arc<MossBranchScopeState>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",438,45,92,18991,19038},
      {"r7-010-runtime-08", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(gx)", "Arc<MossGlobalExec>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",733,15,62,31425,31472},
      {"r7-010-runtime-09", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(&gx)", "Arc<MossGlobalExec>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",919,38,85,39420,39467},
      {"r7-010-runtime-10", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(&reply_slot)",
       "Arc<(Mutex<Option<R>>, Condvar)>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",1089,17,64,45973,46020},
      {"r7-010-runtime-11", "qualified_arc", "src/executor_runtime.cpp",
       "std::sync::Arc::clone(inner.active_executor.as_ref().expect(",
       "Arc<MossGlobalExec>",
       "3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690",1116,26,73,47064,47111},
      {"file-chunk-01", "qualified_arc", "src/file_chunk_codegen.inc",
       "std::sync::Arc::clone(&", "Arc<Mutex<Option<ChunkOutcome>>>",
       "2ba9d8adf1aa1ec926294d2fa8a8f0140466b516ead78da36745d273ad3796c7",238,52,95,12621,12664},
      {"fileio-read-borrow-01", "qualified_arc", "src/fileio_runtime.cpp",
       "Arc::clone(&file.inner)", "Arc<MossFairMutex<Option<FileIOInner>>>",
       "12b88c7b446c964de8a61ad68ff90bb1ee5740d00d936f8b724ec2fd9c1a6e22",1293,21,72,46965,47016}};
  return sites;
}

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

inline std::string native_root_ingress_capture_fact_json(
    const TypedRootIngressDomainCaptureFactV1& fact) {
  const auto availability = [&] {
    switch (fact.availability) {
      case TypedRootIngressDomainCaptureFactV1::Availability::Present:
        return std::string("Present");
      case TypedRootIngressDomainCaptureFactV1::Availability::MissingOccurrence:
        return std::string("MissingOccurrence");
      case TypedRootIngressDomainCaptureFactV1::Availability::Contradictory:
        return std::string("Contradictory");
    }
    return std::string("Unknown");
  }();
  // std::map gives the stable lexical key order used by the canonical ID.
  const std::map<std::string, std::string> fields{
      {"availability", native_clone_json_string(availability)},
      {"compiler_generated_capture_only", fact.compiler_generated_capture_only ? "true" : "false"},
      {"concrete_graph_identity", native_clone_json_string(fact.concrete_graph_identity)},
      {"domain_instance_identity", native_clone_json_string(fact.domain_instance_identity)},
      {"domain_type_identity", native_clone_json_string(fact.domain_type_identity)},
      {"est_arena_index", std::to_string(fact.est_arena_index)},
      {"est_fact_context", native_clone_json_string(fact.est_fact_context)},
      {"message_expression_occurrence_id", std::to_string(fact.message_expression_occurrence_id)},
      {"origin_id", std::to_string(fact.origin_id)},
      {"permits_moss_value_copy", fact.permits_moss_value_copy ? "true" : "false"},
      {"receiver_binding", native_clone_json_string(fact.receiver_binding)},
      {"receiver_definition_identity", native_clone_json_string(fact.receiver_definition_identity)},
      {"receiver_expression_occurrence_id", std::to_string(fact.receiver_expression_occurrence_id)},
      {"route_capability_identity", native_clone_json_string(fact.route_capability_identity)},
      {"selected_call_edge_est_arena_index", std::to_string(fact.selected_call_edge_est_arena_index)},
      {"selected_call_edge_identity", native_clone_json_string(fact.selected_call_edge_identity)},
      {"selected_call_edge_invocation_kind", native_clone_json_string(fact.selected_call_edge_invocation_kind)},
      {"selected_call_edge_line", std::to_string(fact.selected_call_edge_line)},
      {"selected_call_edge_message_expression_occurrence_id", std::to_string(fact.selected_call_edge_message_expression_occurrence_id)},
      {"selected_call_edge_occurrence", std::to_string(fact.selected_call_edge_occurrence)},
      {"selected_call_edge_receiver", native_clone_json_string(fact.selected_call_edge_receiver)},
      {"selected_call_edge_receiver_expression_occurrence_id", std::to_string(fact.selected_call_edge_receiver_expression_occurrence_id)},
      {"selected_call_edge_source", native_clone_json_string(fact.selected_call_edge_source)},
      {"selected_call_edge_source_file", native_clone_json_string(fact.selected_call_edge_source_file)},
      {"selected_call_edge_source_fingerprint", std::to_string(fact.selected_call_edge_source_fingerprint)},
      {"selected_call_edge_target", native_clone_json_string(fact.selected_call_edge_target)},
      {"selected_handler_identity", native_clone_json_string(fact.selected_handler_identity)},
      {"source_file", native_clone_json_string(fact.source_file)},
      {"source_fingerprint", std::to_string(fact.source_fingerprint)},
      {"source_line", std::to_string(fact.source_line)},
      {"specialization_context", native_clone_json_string(fact.specialization_context)},
      {"statement_occurrence_id", std::to_string(fact.statement_occurrence_id)},
      {"synchronous_root_ingress", fact.synchronous_root_ingress ? "true" : "false"}};
  std::string json = "{";
  bool first = true;
  for (const auto& field : fields) {
    if (!first) json += ',';
    first = false;
    json += native_clone_json_string(field.first) + ":" + field.second;
  }
  return json + "}";
}

inline std::string native_root_ingress_capture_identity(
    const TypedRootIngressDomainCaptureFactV1& fact) {
  return "root-ingress-domain-capture-fact-v1:" +
      source_free_contract_sha256(native_root_ingress_capture_fact_json(fact));
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
    const NativeCheckedCopyFactV1& fact, bool stable_source_identity = false) {
  if (fact.availability != "Present" || fact.required_source_access != "READ" ||
      !fact.creates_independent_owner || fact.source_type_key.empty() ||
      fact.result_type_key.empty() || fact.source_bytes.empty() ||
      fact.source_file.empty() || fact.specialization_context.empty() ||
      fact.est_fact_context.empty())
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT invalid checked copy provenance fact");
  const auto source_hash = "sha256:" + source_free_contract_sha256(fact.source_bytes);
  // Physical paths remain in the serialized checker evidence, but are not
  // semantic occurrence identity: the same source copied from two checkout
  // roots must name the same checked copy decision. The content digest,
  // exact byte spans, callable/module identity, and EST IDs remain bound.
  const auto source_identity = stable_source_identity
      ? source_hash : fact.source_file;
  const auto provenance_source_identity = stable_source_identity
      ? source_hash : fact.provenance_source_file;
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
      ",\"source_file\":" + native_clone_json_string(provenance_source_identity) +
      "},\"required_source_access\":\"READ\",\"result_type_key\":" +
      native_clone_json_string(fact.result_type_key) +
      ",\"source_expression_id\":" + std::to_string(fact.source_expression_id) +
      ",\"source_file\":" + native_clone_json_string(source_identity) +
      ",\"source_occurrence_span\":" + native_clone_fact_span_json(fact.source_occurrence_span) +
      ",\"source_sha256\":" + native_clone_json_string(source_hash) +
      ",\"source_type_key\":" + native_clone_json_string(fact.source_type_key) +
      ",\"specialization_context\":" + native_clone_json_string(fact.specialization_context) +
      ",\"specialization_identity\":" + native_clone_json_string(fact.specialization_identity) +
      ",\"statement_occurrence_id\":" + std::to_string(fact.statement_occurrence_id) + "}";
}

inline std::string native_checked_copy_identity(
    const NativeCheckedCopyFactV1& fact) {
  const auto row = native_checked_copy_fact_json(fact, true);
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

struct NativeCloneFunctionVariantSpan {
  std::string kind;
  std::string semantic_identity;
  std::string generated_symbol;
  std::size_t begin{}, end{};
};

inline NativeCloneFunctionVariantSpan native_clone_function_variant_at(
    std::string_view rust, std::size_t position) {
  constexpr std::string_view function_prefix = "// Moss tooling begin|function|";
  constexpr std::string_view main_prefix = "// Moss tooling begin|main|";
  constexpr std::string_view method_prefix = "// Moss tooling begin|method|";
  constexpr std::string_view end_prefix = "// Moss tooling end|";
  const auto function_begin = rust.rfind(function_prefix, position);
  const auto main_begin = rust.rfind(main_prefix, position);
  const auto method_begin = rust.rfind(method_prefix, position);
  std::size_t begin = std::string_view::npos;
  std::string_view prefix;
  std::string kind;
  const auto choose = [&](std::size_t candidate, std::string_view marker,
                          std::string candidate_kind) {
    if (candidate != std::string_view::npos &&
        (begin == std::string_view::npos || candidate > begin)) {
      begin = candidate;
      prefix = marker;
      kind = std::move(candidate_kind);
    }
  };
  choose(function_begin, function_prefix, "function");
  choose(main_begin, main_prefix, "main");
  choose(method_begin, method_prefix, "method");
  if (begin == std::string_view::npos)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT copy helper is outside a tooling function variant");
  const auto line_end = rust.find('\n', begin);
  if (line_end == std::string_view::npos || position < line_end)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT malformed tooling function variant marker");
  const auto identity_begin = begin + prefix.size();
  const auto identity_end = rust.find('|', identity_begin);
  if (identity_end == std::string_view::npos || identity_end >= line_end)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT tooling function variant lacks identity");
  const auto symbol_begin = identity_end + 1;
  const auto symbol_end = rust.find('|', symbol_begin);
  if (symbol_end == std::string_view::npos || symbol_end >= line_end || symbol_end == symbol_begin)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT tooling function variant lacks generated symbol");
  const std::string identity(rust.substr(identity_begin, identity_end - identity_begin));
  const std::string symbol(rust.substr(symbol_begin, symbol_end - symbol_begin));
  const std::string end_marker = std::string(end_prefix) + identity;
  const auto end = rust.find(end_marker, line_end);
  const auto intervening_function = std::min(
      std::min(rust.find(function_prefix, line_end), rust.find(main_prefix, line_end)),
      rust.find(method_prefix, line_end));
  if (end == std::string_view::npos ||
      (intervening_function != std::string_view::npos && intervening_function < end) ||
      position >= end)
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT tooling function variant is unterminated or overlapping");
  return {kind, identity, symbol, begin, end};
}

inline std::string native_clone_function_variant_identity(
    std::string_view rust, const NativeCloneFunctionVariantSpan& variant) {
  const auto body = rust.substr(variant.begin, variant.end - variant.begin);
  const auto material = variant.semantic_identity + "\n" + variant.generated_symbol +
      "\n" + std::string(body);
  return "rust-function-variant-v1:" + source_free_contract_sha256(material);
}

inline bool native_clone_variant_matches_checked_copy(
    const NativeCheckedCopyFactV1& fact,
    const NativeCloneFunctionVariantSpan& variant) {
  const auto expected_context = native_clone_normalize_callable_identity(
      fact.specialization_context.empty() ? fact.enclosing_callable_id
                                          : fact.specialization_context);
  if (native_clone_normalize_callable_identity(variant.semantic_identity) !=
      expected_context)
    return false;
  return std::find(fact.generated_callable_symbols.begin(),
                   fact.generated_callable_symbols.end(),
                   variant.generated_symbol) !=
      fact.generated_callable_symbols.end();
}

inline bool native_clone_variant_declares_function(
    std::string_view code, const NativeCloneFunctionVariantSpan& variant) {
  std::string declaration = variant.generated_symbol;
  if (variant.kind == "method") {
    const auto separator = declaration.rfind("::");
    if (separator == std::string::npos) return false;
    declaration.erase(0, separator + 2);
  }
  const std::string prefix = "fn " + declaration;
  std::size_t count = 0;
  std::size_t at = variant.begin;
  while ((at = code.find(prefix, at)) != std::string_view::npos && at < variant.end) {
    const auto end = at + prefix.size();
    if (end <= variant.end &&
        (end == code.size() || !native_clone_identifier_char(code[end])))
      ++count;
    at = end;
  }
  return count == 1;
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
    std::vector<NativeCloneCheckedCopyFactV3> exact_fact_groups,
    std::vector<NativeRootIngressCaptureInputV1> root_capture_inputs = {}) {
  NativeCloneProvenanceV3 manifest;
  manifest.mode = std::move(mode);
  manifest.source_copy_occurrences = std::move(lexical_copies);
  manifest.checked_copy_facts = std::move(exact_fact_groups);
  std::sort(manifest.source_copy_occurrences.begin(),
      manifest.source_copy_occurrences.end(), [](const auto& left, const auto& right) {
        return left.source_span.source_begin < right.source_span.source_begin;
      });
  std::map<std::string, std::size_t> lexical_order;
  for (std::size_t i = 0; i < manifest.source_copy_occurrences.size(); ++i)
    lexical_order.emplace(manifest.source_copy_occurrences[i].copy_id, i);
  std::sort(manifest.checked_copy_facts.begin(),
      manifest.checked_copy_facts.end(), [&](const auto& left, const auto& right) {
        const auto left_order = lexical_order.find(left.copy_id);
        const auto right_order = lexical_order.find(right.copy_id);
        if ((left_order != lexical_order.end()) !=
            (right_order != lexical_order.end()))
          return left_order != lexical_order.end();
        if (left_order == lexical_order.end()) return left.copy_id < right.copy_id;
        return left_order->second < right_order->second;
      });
  const auto code = native_clone_mask_rust_non_code(rust_bytes);
  std::map<std::string, const NativeRuntimeCloneSiteSpecV1*> runtime_site_by_id;
  for (const auto& site : native_runtime_clone_site_specs_v1())
    if (!runtime_site_by_id.emplace(site.id, &site).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT duplicate runtime clone site specification");
  std::map<std::string, std::uint32_t> runtime_marker_counts;
  const std::string runtime_marker_prefix = "/* MOSS_RUNTIME_CLONE_SITE:";
  std::size_t runtime_marker_at = 0;
  while ((runtime_marker_at = rust_bytes.find(runtime_marker_prefix,
                                               runtime_marker_at)) !=
         std::string_view::npos) {
    const auto id_begin = runtime_marker_at + runtime_marker_prefix.size();
    const auto marker_end = rust_bytes.find(" */", id_begin);
    if (marker_end == std::string_view::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unterminated runtime clone site marker");
    const std::string site_id(rust_bytes.substr(id_begin, marker_end - id_begin));
    const auto spec = runtime_site_by_id.find(site_id);
    if (spec == runtime_site_by_id.end())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unknown runtime clone site marker");
    const auto generated_occurrence = runtime_marker_counts[site_id]++;
    if (generated_occurrence != 0 && site_id != "file-chunk-01")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT duplicate non-expanding runtime clone site marker");

    const auto line_begin = rust_bytes.rfind('\n', runtime_marker_at) ==
            std::string_view::npos
        ? 0 : rust_bytes.rfind('\n', runtime_marker_at) + 1;
    const auto line_end = rust_bytes.find('\n', marker_end);
    const auto bounded_line_end = line_end == std::string_view::npos
        ? rust_bytes.size() : line_end;
    std::string normalized_line;
    bool pending_space = false;
    for (std::size_t index = line_begin; index < bounded_line_end; ++index) {
      const unsigned char ch = static_cast<unsigned char>(code[index]);
      if (std::isspace(ch)) {
        pending_space = !normalized_line.empty();
        continue;
      }
      if (pending_space) normalized_line.push_back(' ');
      pending_space = false;
      normalized_line.push_back(static_cast<char>(ch));
    }
    if (normalized_line.find(spec->second->expected_expression) ==
        std::string::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT runtime clone marker expression mismatch: " +
          site_id);

    std::size_t operator_begin = std::string_view::npos;
    std::size_t operator_end = std::string_view::npos;
    if (std::string_view(spec->second->operator_kind) == "direct_method") {
      std::size_t found = 0;
      for (auto at = code.find(".clone()", line_begin);
           at != std::string::npos && at < bounded_line_end;
           at = code.find(".clone()", at + 8)) {
        ++found;
        operator_begin = at;
        operator_end = at + 8;
      }
      if (found != 1)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT runtime direct clone marker does not select one operator: " +
            site_id);
    } else if (std::string_view(spec->second->operator_kind) ==
               "qualified_arc") {
      const auto call = code.find("Arc::clone", marker_end + 3);
      if (call == std::string::npos || call >= bounded_line_end)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT runtime Arc marker lacks its qualified call: " +
            site_id);
      const auto open = code.find('(', call + 9);
      if (open == std::string::npos || open >= bounded_line_end)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT runtime Arc marker lacks call arguments: " +
            site_id);
      operator_begin = call;
      operator_end = native_clone_matching_delimiter(code, open, '(', ')') + 1;
      if (operator_end > bounded_line_end)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT runtime Arc call crosses its marked line: " +
            site_id);
    } else {
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unsupported runtime clone operator kind");
    }

    NativeCloneProvenanceV3::RuntimeCloneSiteObservation observation;
    observation.site_id = site_id;
    observation.generated_occurrence = generated_occurrence;
    observation.operator_kind = spec->second->operator_kind;
    observation.source_file = spec->second->source_file;
    observation.expected_expression = spec->second->expected_expression;
    observation.semantic_type = spec->second->semantic_type;
    observation.rust_span = native_checked_copy_source_span(
        rust_bytes, operator_begin, operator_end);
    auto context_begin = line_begin;
    auto context_end = std::max(line_begin + 1, bounded_line_end);
    if (site_id == "file-chunk-01") {
      const auto publish = code.find("branch_publish(&", context_end);
      if (publish == std::string_view::npos || publish - line_begin > 4096)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT FileChunk clone lacks nearby branch publish context");
      const auto publish_end = code.find('\n', publish);
      context_end = publish_end == std::string_view::npos
          ? code.size() : publish_end;
      const auto slot_assign = code.rfind("let ", line_begin - 1);
      const auto scope_call = code.rfind("branch_scope_new_current()", line_begin);
      const auto scope_assign = scope_call == std::string_view::npos
          ? std::string_view::npos : code.rfind("let ", scope_call);
      context_begin = scope_assign == std::string_view::npos
          ? line_begin : code.rfind('\n', scope_assign) + 1;
      const auto context = code.substr(context_begin, context_end - context_begin);
      const auto marker_line = normalized_line;
      const auto clone_token = marker_line.find(" = std::sync::Arc::clone(&");
      const auto clone_close = clone_token == std::string_view::npos
          ? std::string_view::npos : marker_line.find(");", clone_token);
      const auto publish_open = context.find("branch_publish(&");
      const auto publish_comma = publish_open == std::string_view::npos
          ? std::string_view::npos : context.find(", move ||", publish_open);
      if (slot_assign == std::string_view::npos || clone_token == std::string_view::npos ||
          clone_close == std::string_view::npos || publish_open == std::string_view::npos ||
          publish_comma == std::string_view::npos ||
          marker_line.find("let ") != 0 || marker_line.back() != ';')
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT FileChunk clone context shape mismatch");
      const auto arg_begin = clone_token + std::string_view(" = std::sync::Arc::clone(&").size();
      const auto clone_arg = marker_line.substr(arg_begin, clone_close - arg_begin);
      const auto publish_scope_begin = publish_open +
          std::string_view("branch_publish(&").size();
      const auto publish_scope = publish_comma == std::string_view::npos
          ? std::string{} : context.substr(
              publish_scope_begin, publish_comma - publish_scope_begin);
      const auto preceding_context = context.substr(0, line_begin-context_begin);
      const auto last_prior_char = preceding_context.find_last_not_of("\n\r \t");
      const auto allocation_line_begin = last_prior_char == std::string::npos
          ? std::string::npos : preceding_context.find_last_of('\n', last_prior_char);
      const auto allocation_line = preceding_context.substr(
          allocation_line_begin == std::string::npos ? 0 : allocation_line_begin + 1);
      if (clone_arg.empty() ||
          allocation_line.find("let " + std::string(clone_arg)) == std::string_view::npos ||
          allocation_line.find("std::sync::Arc::new(") == std::string_view::npos ||
          publish_scope.empty() ||
          context.substr(0, line_begin - context_begin).find(
              "let " + std::string(publish_scope) + " = branch_scope_new_current()") ==
              std::string_view::npos)
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT FileChunk clone argument lacks its result-slot allocation: " +
            std::string(clone_arg));
    }
    observation.rust_context_span = native_checked_copy_source_span(
        rust_bytes, context_begin, context_end);
    manifest.runtime_clone_site_observations.push_back(std::move(observation));
    runtime_marker_at = marker_end + 3;
  }
  const bool has_executor_runtime =
      rust_bytes.find("struct MossProcessRuntime") != std::string_view::npos;
  if (has_executor_runtime) {
    for (const auto& site : native_runtime_clone_site_specs_v1()) {
      if (std::string_view(site.source_file) != "src/executor_runtime.cpp")
        continue;
      if (!runtime_marker_counts.count(site.id))
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT emitted executor runtime lacks required site marker: " +
            std::string(site.id));
    }
  }
  const bool has_fileio_borrow_runtime =
      rust_bytes.find("MossFileIOReadBorrow") != std::string_view::npos;
  if (has_fileio_borrow_runtime &&
      !runtime_marker_counts.count("fileio-read-borrow-01"))
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT emitted FileIO runtime lacks read-borrow site marker");
  const bool has_file_chunk_pipeline =
      rust_bytes.find("Moss backend: FileIO CHUNK PIPELINE -- eligible") !=
      std::string_view::npos;
  if (has_file_chunk_pipeline && !runtime_marker_counts.count("file-chunk-01"))
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT emitted FileChunk pipeline lacks result-slot site marker");
  std::size_t expected_file_chunk_expansions = 0;
  for (std::size_t at = 0; (at = rust_bytes.find(
           "Moss backend: FileIO CHUNK PIPELINE -- eligible", at)) !=
       std::string_view::npos; ++at)
    ++expected_file_chunk_expansions;
  if (runtime_marker_counts["file-chunk-01"] != expected_file_chunk_expansions)
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT FileChunk marker count does not cover every eligible expansion");
  std::sort(manifest.runtime_clone_site_observations.begin(),
      manifest.runtime_clone_site_observations.end(), [](const auto& left,
                                                         const auto& right) {
        return left.rust_span.source_begin < right.rust_span.source_begin;
      });
  std::map<std::string, const NativeRootIngressCaptureInputV1*> root_facts;
  for (const auto& input : root_capture_inputs) {
    if (!input.checked_fact.is_authoritative() ||
        input.checked_fact.permits_moss_value_copy ||
        input.source_bytes != source_bytes)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT invalid or source-mismatched Root-ingress capture fact");
    const auto id = native_root_ingress_capture_identity(input.checked_fact);
    if (!root_facts.emplace(id, &input).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT duplicate Root-ingress capture fact identity");
  }
  std::map<std::uint64_t, std::string> root_capture_by_share;
  std::set<std::string> marked_root_captures;
  const std::string root_capture_marker =
      "/* Moss root ingress capture fact: ";
  std::size_t root_marker_at = 0;
  while ((root_marker_at = rust_bytes.find(root_capture_marker,
                                            root_marker_at)) !=
         std::string_view::npos) {
    const auto id_begin = root_marker_at + root_capture_marker.size();
    const auto marker_end = rust_bytes.find(" */", id_begin);
    if (marker_end == std::string_view::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unterminated Root-ingress capture marker");
    const std::string id(rust_bytes.substr(id_begin, marker_end - id_begin));
    const auto fact = root_facts.find(id);
    if (fact == root_facts.end() || !marked_root_captures.insert(id).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT unknown or duplicate Root-ingress capture marker");
    std::size_t share_end = marker_end + 3;
    while (share_end < code.size() &&
           std::isspace(static_cast<unsigned char>(code[share_end])))
      ++share_end;
    if (share_end + 2 > code.size() ||
        code.substr(share_end, 2) != "))")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress capture marker does not belong to share_domain");
    share_end += 2;
    const auto share_begin = rust_bytes.rfind("moss_rt::share_domain(&(",
                                               root_marker_at);
    if (share_begin == std::string_view::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress capture lacks typed share_domain call");
    std::size_t receiver_end = root_marker_at;
    while (receiver_end > 0 &&
           std::isspace(static_cast<unsigned char>(code[receiver_end - 1])))
      --receiver_end;
    std::size_t receiver_begin = receiver_end;
    while (receiver_begin > 0 &&
           native_clone_identifier_char(code[receiver_begin - 1]))
      --receiver_begin;
    if (receiver_begin == receiver_end ||
        code.substr(receiver_begin, receiver_end - receiver_begin) !=
            fact->second->checked_fact.receiver_binding)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress marker is not bound to its checked receiver");
    auto [position, inserted] = root_capture_by_share.emplace(share_begin, id);
    (void)position;
    if (!inserted)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT duplicate Root-ingress share marker span");
    NativeRootIngressCaptureV1 capture;
    capture.capture_id = id;
    capture.checked_fact = fact->second->checked_fact;
    capture.source_sha256 = source_free_contract_sha256(source_bytes);
    capture.rust_span = native_checked_copy_source_span(
        rust_bytes, share_begin, share_end);
    manifest.root_ingress_domain_captures.push_back(std::move(capture));
    root_marker_at = marker_end + 3;
  }
  if (marked_root_captures.size() != root_facts.size())
    throw std::runtime_error(
        "MOSS_INTERNAL_INVARIANT checked Root-ingress capture fact lacks exactly one Rust marker");
  std::sort(manifest.root_ingress_domain_captures.begin(),
      manifest.root_ingress_domain_captures.end(), [](const auto& left,
                                                      const auto& right) {
        return left.rust_span.source_begin < right.rust_span.source_begin;
      });
  struct FactBinding {
    struct HelperBody {
      std::size_t begin{}, end{};
      NativeCloneFunctionVariantSpan variant;
      std::string variant_id;
    };
    std::string copy_id, helper, identity;
    const NativeCheckedCopyFactV1* fact{};
    std::vector<HelperBody> helper_bodies;
    std::set<std::pair<std::string, std::string>> variants;
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
      if (!bindings.emplace(id, FactBinding{group.copy_id, helper, id, &fact, {}, {}}).second)
        throw std::runtime_error("MOSS_INTERNAL_INVARIANT duplicate checked-copy identity");
    }
  }
  if (manifest.checked_copy_facts.size() != manifest.source_copy_occurrences.size())
    throw std::runtime_error("MOSS_INTERNAL_INVARIANT lexical copy lacks a checked-copy group");

  const std::string marker_prefix = "/* Moss copy fact call: ";
  std::map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> marked_facts;
  std::map<std::string, std::size_t> marked_calls;
  std::size_t marker_at = 0;
  while ((marker_at = rust_bytes.find(marker_prefix, marker_at)) != std::string_view::npos) {
    const auto id_begin = marker_at + marker_prefix.size();
    const auto marker_end = rust_bytes.find(" */", id_begin);
    if (marker_end == std::string_view::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unterminated explicit-copy marker");
    const std::string id(rust_bytes.substr(id_begin, marker_end - id_begin));
    auto binding = bindings.find(id);
    if (binding == bindings.end()) {
      std::string known;
      for (const auto& item : bindings) known += " [" + item.first + "]";
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unknown explicit-copy marker: " + id + known);
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

    const auto variant = native_clone_function_variant_at(rust_bytes, marker_at);
    if (call < variant.begin || call >= variant.end)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT checked-copy call escapes its generated function variant");
    if (!native_clone_variant_matches_checked_copy(*binding->second.fact, variant) ||
        !native_clone_variant_declares_function(code, variant))
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT generated function variant does not match checked-copy callable authority");
    if (!binding->second.variants.emplace(variant.semantic_identity,
                                         variant.generated_symbol).second)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT checked-copy fact repeats within one generated function variant");

    const std::string function_prefix = "fn " + helper;
    auto def = code.rfind(function_prefix, marker_at);
    if (def == std::string::npos || def < variant.begin ||
        (def + function_prefix.size() < code.size() &&
         native_clone_identifier_char(code[def + function_prefix.size()])))
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper definition is missing from its function variant");
    auto params_open = def + function_prefix.size();
    while (params_open < code.size() && std::isspace(static_cast<unsigned char>(code[params_open]))) ++params_open;
    const auto params_close = native_clone_matching_delimiter(code, params_open, '(', ')');
    auto body_open = code.find('{', params_close + 1);
    if (body_open == std::string::npos)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper has no body");
    const auto body_close = native_clone_matching_delimiter(code, body_open, '{', '}');
    if (body_close + 1 > variant.end)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper body escapes its generated function variant");
    binding->second.helper_bodies.push_back({def, body_close + 1, variant,
        native_clone_function_variant_identity(rust_bytes, variant)});
    marked_facts[id].emplace_back(def, call);
    ++marked_calls[helper];
    marker_at = marker_end + 3;
  }

  for (const auto& entry : bindings) {
    const auto marked = marked_facts.find(entry.first);
    if (marked == marked_facts.end() || marked->second.empty()) continue;
    const auto references = native_clone_identifier_occurrences(code, entry.second.helper);
    const auto expected = marked_calls.at(entry.second.helper) * 2;
    if (references.size() != expected)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper has missing or extra references");
    std::multiset<std::size_t> expected_positions;
    for (const auto& pair : marked->second) {
      expected_positions.insert(pair.first + 3);
      expected_positions.insert(pair.second);
    }
    if (std::multiset<std::size_t>(references.begin(), references.end()) != expected_positions)
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT per-fact helper reference is not an authorized definition or call");
  }
  for (const auto& entry : bindings)
    if (marked_facts.count(entry.first) == 0 &&
        !native_clone_identifier_occurrences(code, entry.second.helper).empty())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unmarked per-fact helper exists in raw Rust");

  const auto function_inventory = native_clone_tooling_function_inventory(rust_bytes);
  for (auto& entry : bindings) {
    auto& binding = entry.second;
    if (!binding.helper_bodies.empty()) continue;
    if (!native_clone_identifier_occurrences(code, binding.helper).empty())
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT unmarked or uncalled per-fact helper exists in raw Rust");
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
        if (sibling.second.copy_id == binding.copy_id && !sibling.second.helper_bodies.empty() &&
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
  for (std::size_t at = 0; (at = code.find(".clone(", at)) != std::string::npos; at += 8) {
    if (at + 8 > code.size() || code.substr(at, 8) != ".clone()")
      throw std::runtime_error("MOSS_INTERNAL_INVARIANT clone method has unexpected arguments");
    const auto span = native_checked_copy_source_span(rust_bytes, at, at + 8);
    const FactBinding* owner = nullptr;
    for (const auto& entry : bindings) {
      const auto& candidate = entry.second;
      for (const auto& body : candidate.helper_bodies)
        if (at >= body.begin && at + 8 <= body.end) {
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
    const auto owner_body = [&]() -> const FactBinding::HelperBody* {
      if (owner) for (const auto& body : owner->helper_bodies)
        if (at >= body.begin && at + 8 <= body.end) return &body;
      return static_cast<const FactBinding::HelperBody*>(nullptr);
    }();
    const auto context_begin = owner ? owner_body->begin :
        (rust_bytes.rfind('\n', at) == std::string_view::npos ? 0 : rust_bytes.rfind('\n', at) + 1);
    const auto line_end = rust_bytes.find('\n', at);
    const auto context_end = owner ? owner_body->end :
        (line_end == std::string_view::npos ? rust_bytes.size() : line_end);
    occurrence.rust_context_span = native_checked_copy_source_span(
        rust_bytes, context_begin, std::max(context_begin + 1, context_end));
    if (owner)
      source_rows.at(owner->copy_id)->rust_clone_spans.push_back(span);
    if (owner) {
      occurrence.generated_function_variant_id = owner_body->variant_id;
      occurrence.generated_function_symbol = owner_body->variant.generated_symbol;
      occurrence.generated_function_span = native_checked_copy_source_span(
          rust_bytes, owner_body->variant.begin, owner_body->variant.end);
    }
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
        rust_bytes.substr(row.rust_span.source_begin, 8) != ".clone()")
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
          rust_bytes.substr(clone.source_begin, 8) != ".clone()")
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
  std::map<std::string, const NativeRootIngressCaptureV1*> root_capture_by_id;
  std::map<std::uint64_t, std::string> root_capture_by_span;
  for (const auto& capture : manifest.root_ingress_domain_captures) {
    const auto capture_text = capture.rust_span.source_begin < rust_bytes.size() &&
            capture.rust_span.source_end <= rust_bytes.size() &&
            capture.rust_span.source_begin < capture.rust_span.source_end
        ? rust_bytes.substr(capture.rust_span.source_begin,
              capture.rust_span.source_end - capture.rust_span.source_begin)
        : std::string_view{};
    const std::string marker = "/* Moss root ingress capture fact: " +
        capture.capture_id + " */";
    if (!capture.checked_fact.is_authoritative() ||
        capture.checked_fact.permits_moss_value_copy ||
        capture.capture_id != native_root_ingress_capture_identity(
                                  capture.checked_fact) ||
        capture.source_sha256 != source_free_contract_sha256(source_bytes) ||
        capture_text.rfind("moss_rt::share_domain(&(", 0) != 0 ||
        capture_text.size() < 2 ||
        capture_text.substr(capture_text.size() - 2) != "))" ||
        capture_text.find(marker) == std::string_view::npos ||
        !root_capture_by_id.emplace(capture.capture_id, &capture).second ||
        !root_capture_by_span.emplace(capture.rust_span.source_begin,
                                      capture.capture_id).second)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT invalid or duplicate Root-ingress capture row");
  }
  for (const auto& occurrence : manifest.occurrences) {
    if (occurrence.origin_kind == "root_ingress_domain_capture")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT domain sharing is not a Moss value-copy occurrence");
  }
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
  out << "],\"root_ingress_domain_captures\":[";
  for (std::size_t i = 0;
       i < manifest.root_ingress_domain_captures.size(); ++i) {
    if (i) out << ',';
    const auto& capture = manifest.root_ingress_domain_captures[i];
    if (capture.rust_span.source_end > rust_bytes.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT Root-ingress Rust span is out of bounds");
    out << "{\"capture_id\":"
        << native_clone_json_string(capture.capture_id)
        << ",\"checked_fact\":"
        << native_root_ingress_capture_fact_json(capture.checked_fact)
        << ",\"source_sha256\":"
        << native_clone_json_string("sha256:" + capture.source_sha256)
        << ",\"rust_span\":";
    emit_rust_span(capture.rust_span);
    out << '}';
  }
  out << "],\"runtime_clone_site_observations\":[";
  std::set<std::pair<std::string, std::uint32_t>> emitted_runtime_site_ids;
  std::map<std::string, std::uint32_t> next_runtime_occurrence;
  for (std::size_t i = 0;
       i < manifest.runtime_clone_site_observations.size(); ++i) {
    if (i) out << ',';
    const auto& row = manifest.runtime_clone_site_observations[i];
    const auto spec = std::find_if(native_runtime_clone_site_specs_v1().begin(),
        native_runtime_clone_site_specs_v1().end(), [&](const auto& candidate) {
          return row.site_id == candidate.id;
        });
    if (spec == native_runtime_clone_site_specs_v1().end() ||
        !emitted_runtime_site_ids.emplace(row.site_id, row.generated_occurrence).second ||
        row.operator_kind != spec->operator_kind ||
        row.source_file != spec->source_file ||
        row.expected_expression != spec->expected_expression ||
        row.semantic_type != spec->semantic_type ||
        row.provenance_status != "source_site_only_unapproved" ||
        row.generated_occurrence != next_runtime_occurrence[row.site_id]++ ||
        !spec->source_sha256 || spec->source_line == 0 ||
        spec->source_column == 0 || spec->source_end_column <= spec->source_column ||
        spec->source_begin >= spec->source_end ||
        row.rust_context_span.source_begin > row.rust_span.source_begin ||
        row.rust_context_span.source_end < row.rust_span.source_end ||
        row.rust_context_span.source_end > rust_bytes.size())
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT invalid, duplicate, or unauthorized runtime site observation");
    const auto operator_text = rust_bytes.substr(row.rust_span.source_begin,
        row.rust_span.source_end - row.rust_span.source_begin);
    const bool exact_operator = row.operator_kind == "direct_method"
        ? operator_text == ".clone()"
        : row.operator_kind == "qualified_arc" &&
            operator_text.rfind("Arc::clone(", 0) == 0 &&
            operator_text.back() == ')';
    const auto context = rust_bytes.substr(row.rust_context_span.source_begin,
        row.rust_context_span.source_end - row.rust_context_span.source_begin);
    if (!exact_operator ||
        native_clone_mask_rust_non_code(context).find(
            row.expected_expression) == std::string::npos)
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT runtime site observation operator/context mismatch");
    out << "{\"site_id\":" << native_clone_json_string(row.site_id)
        << ",\"generated_occurrence\":" << row.generated_occurrence
        << ",\"operator_kind\":"
        << native_clone_json_string(row.operator_kind)
        << ",\"source_file\":"
        << native_clone_json_string(row.source_file)
        << ",\"expected_expression\":"
        << native_clone_json_string(row.expected_expression)
        << ",\"semantic_type\":"
        << native_clone_json_string(row.semantic_type)
        << ",\"provenance_status\":"
        << native_clone_json_string(row.provenance_status)
        << ",\"source_sha256\":"
        << native_clone_json_string(spec->source_sha256)
        << ",\"source_marker\":"
        << native_clone_json_string(std::string("MOSS_RUNTIME_CLONE_SITE:") +
                                    row.site_id)
        << ",\"source_span\":{\"start\":{\"line\":"
        << spec->source_line << ",\"column\":" << spec->source_column
        << "},\"end\":{\"line\":" << spec->source_line
        << ",\"column\":" << spec->source_end_column
        << "},\"source_begin\":" << spec->source_begin
        << ",\"source_end\":" << spec->source_end << '}'
        << ",\"rust_span\":";
    emit_rust_span(row.rust_span);
    out << ",\"rust_context_span\":";
    emit_rust_span(row.rust_context_span);
    out << ",\"rust_context_sha256\":"
        << native_clone_json_string(source_free_contract_sha256(context))
        << '}';
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
        rust_bytes.substr(row.rust_span.source_begin, 8) != ".clone()")
      throw std::runtime_error(
          "MOSS_INTERNAL_INVARIANT occurrence is not a Rust clone call");
    const auto context = rust_bytes.substr(
        row.rust_context_span.source_begin,
        row.rust_context_span.source_end - row.rust_context_span.source_begin);
    const NativeCheckedCopyFactV1* checked = nullptr;
    NativeCloneFunctionVariantSpan parsed_variant;
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
      parsed_variant = native_clone_function_variant_at(
          rust_bytes, static_cast<std::size_t>(row.rust_span.source_begin));
      if (parsed_variant.generated_symbol != row.generated_function_symbol ||
          native_clone_function_variant_identity(rust_bytes, parsed_variant) !=
              row.generated_function_variant_id ||
          parsed_variant.begin != row.generated_function_span.source_begin ||
          parsed_variant.end != row.generated_function_span.source_end ||
          !native_clone_variant_matches_checked_copy(*checked, parsed_variant) ||
          !native_clone_variant_declares_function(
              native_clone_mask_rust_non_code(rust_bytes), parsed_variant))
        throw std::runtime_error(
            "MOSS_INTERNAL_INVARIANT explicit clone generated-function variant evidence is stale or forged");
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
    if (checked) {
      out << ",\"generated_function_variant_id\":"
          << native_clone_json_string(row.generated_function_variant_id)
          << ",\"generated_function_symbol\":"
          << native_clone_json_string(row.generated_function_symbol)
          << ",\"checked_callable_symbols\":[";
      for (std::size_t symbol_index = 0;
           symbol_index < checked->generated_callable_symbols.size();
           ++symbol_index) {
        if (symbol_index) out << ',';
        out << native_clone_json_string(
            checked->generated_callable_symbols[symbol_index]);
      }
      out << "]"
          << ",\"generated_function_span\":";
      emit_rust_span(row.generated_function_span);
    }
    out << "}}";
  }
  out << "]}";
}

}  // namespace moss
