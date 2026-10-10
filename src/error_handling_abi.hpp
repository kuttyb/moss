#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace moss {

// Phase 21.0 freezes the semantic and native provider boundary at ABI v7.
// These are compiler records. They do not introduce a source-level union of
// unrelated Moss error enums.
inline constexpr int kErrorHandlingAbiVersion = 7;

enum class RaisedIdentityKind : std::uint8_t {
  EnumVariant = 0,
  ScalarType = 1,
};

struct RaisedIdentity {
  RaisedIdentityKind kind = RaisedIdentityKind::EnumVariant;
  std::string type;
  std::string variant;

  static RaisedIdentity enum_variant(std::string type_name,
                                     std::string variant_name) {
    return {RaisedIdentityKind::EnumVariant, std::move(type_name),
            std::move(variant_name)};
  }

  static RaisedIdentity scalar(std::string type_name) {
    return {RaisedIdentityKind::ScalarType, std::move(type_name), {}};
  }

  bool valid() const {
    return !type.empty() &&
        ((kind == RaisedIdentityKind::EnumVariant && !variant.empty()) ||
         (kind == RaisedIdentityKind::ScalarType && variant.empty()));
  }

  std::string canonical() const {
    return kind == RaisedIdentityKind::EnumVariant
        ? "enum:" + type + ":" + variant
        : "scalar:" + type;
  }

  friend bool operator<(const RaisedIdentity& left,
                        const RaisedIdentity& right) {
    return std::tie(left.kind, left.type, left.variant) <
           std::tie(right.kind, right.type, right.variant);
  }

  friend bool operator==(const RaisedIdentity& left,
                         const RaisedIdentity& right) {
    return std::tie(left.kind, left.type, left.variant) ==
           std::tie(right.kind, right.type, right.variant);
  }
};

// std::set is the concrete normalized representation: insertion performs
// deduplication and iteration is the stable semantic serialization order.
using RaiseSet = std::set<RaisedIdentity>;

inline RaiseSet raise_set_union(RaiseSet left, const RaiseSet& right) {
  left.insert(right.begin(), right.end());
  return left;
}

inline RaiseSet raise_set_without(RaiseSet source,
                                  const RaiseSet& handled) {
  for (const auto& identity : handled) source.erase(identity);
  return source;
}

enum class TaggedOutcomeTag : std::uint8_t {
  Normal = 0,
  Raised = 1,
};

struct OwnedPayloadFieldAbi {
  enum class Kind : std::uint8_t {
    Scalar = 0,
    ErrorEnum = 1,
  };

  std::string name;
  std::string moss_type;
  Kind kind = Kind::Scalar;
  // Payload fields are passed by value. The concrete Rust field may remain
  // private inside the provider's public opaque failure-frame type.
  bool owned = true;
  bool bounded = true;
};

struct RaisedValueAbi {
  RaisedIdentity identity;
  std::uint32_t stable_tag = 0;
  std::vector<OwnedPayloadFieldAbi> payload;
};

struct TaggedOutcomeAbi {
  std::string rust_type;
  std::string normal_type;
  // One specialization-local closed table may contain variants belonging to
  // several Moss enums. Only its stable tag is visible to the application;
  // the provider owns the actual Rust payload enum.
  std::vector<RaisedValueAbi> raised;
};

inline bool owned_payload_field_allowed(const OwnedPayloadFieldAbi& field) {
  std::string type;
  type.reserve(field.moss_type.size());
  for (unsigned char character : field.moss_type)
    if (!std::isspace(character))
      type.push_back(static_cast<char>(std::tolower(character)));
  static const std::set<std::string> forbidden = {
      "string", "fileio", "range", "rangebatch", "mossguard",
      "protectedread", "protectedwrite"};
  if (field.name.empty() || field.moss_type.empty() || !field.owned ||
      !field.bounded || forbidden.count(type))
    return false;
  return type.find('&') == std::string::npos &&
         type.find("guard") == std::string::npos &&
         type.find("protected") == std::string::npos &&
         type.find("domainhandle") == std::string::npos &&
         type.find("vector[") == std::string::npos &&
         type.find("map[") == std::string::npos &&
         type.find("queue[") == std::string::npos;
}

inline bool tagged_outcome_abi_valid(const TaggedOutcomeAbi& outcome) {
  if (outcome.rust_type.empty() || outcome.normal_type.empty()) return false;
  std::set<std::string> identities;
  std::optional<RaisedIdentity> previous;
  for (size_t index = 0; index < outcome.raised.size(); ++index) {
    const auto& raised = outcome.raised[index];
    if (!raised.identity.valid() || raised.stable_tag != index ||
        !identities.insert(raised.identity.canonical()).second ||
        (previous && !(previous.value() < raised.identity)))
      return false;
    previous = raised.identity;
    std::set<std::string> field_names;
    for (const auto& field : raised.payload)
      if (!owned_payload_field_allowed(field) ||
          !field_names.insert(field.name).second)
        return false;
  }
  return true;
}

inline bool tagged_outcome_matches_raise_set(const TaggedOutcomeAbi& outcome,
                                             const RaiseSet& raises) {
  if (outcome.raised.size() != raises.size()) return false;
  auto expected = raises.begin();
  for (const auto& raised : outcome.raised) {
    if (expected == raises.end() || !(raised.identity == *expected)) return false;
    ++expected;
  }
  return expected == raises.end();
}

enum class FailurePatternKind : std::uint8_t {
  Variant = 0,
  CatchAll = 1,
};

struct FailurePatternAbi {
  FailurePatternKind kind = FailurePatternKind::Variant;
  RaiseSet variants;
};

struct FailureCaptureAbi {
  std::string name;
  std::string moss_type;
  std::string provider_rust_field;
  bool owned = true;
  bool definitely_initialized = true;
};

inline bool failure_capture_type_allowed(const std::string& type) {
  std::string normalized;
  normalized.reserve(type.size());
  for (unsigned char character : type)
    if (!std::isspace(character))
      normalized.push_back(static_cast<char>(std::tolower(character)));
  static const std::set<std::string> forbidden = {
      "fileio", "range", "rangebatch", "mossguard", "protectedread",
      "protectedwrite"};
  if (type.empty() || forbidden.count(normalized)) return false;
  return normalized.find('&') == std::string::npos &&
         normalized.find("guard") == std::string::npos &&
         normalized.find("protected") == std::string::npos &&
         normalized.find("domainhandle") == std::string::npos;
}

inline bool failure_capture_allowed(const FailureCaptureAbi& capture) {
  return !capture.name.empty() && !capture.provider_rust_field.empty() &&
         capture.owned && capture.definitely_initialized &&
         failure_capture_type_allowed(capture.moss_type);
}

struct FailureArmAbi {
  std::uint32_t source_order = 0;
  FailurePatternAbi pattern;
  std::string callable_symbol;
  std::vector<FailureCaptureAbi> captures;
  // This is the pseudo-handler's independent state footprint. It is never
  // unioned into the normal handler's per-leaf effects.
  std::vector<std::string> state_reads;
  std::vector<std::string> state_writes;
  std::vector<std::string> state_consumes;
  bool escaping_raise_set_empty = true;
};

struct FailureReplyAbi {
  std::string moss_type = "unit";
  bool one_way = true;
  bool every_normal_arm_replies = true;
};

enum class ExceptionalEdgeKind : std::uint8_t {
  RaiseToRecover = 0,
  RaiseToHandlerExit = 1,
  ReRaiseToOuter = 2,
};

enum class ExceptionalCfgConsumer : std::uint32_t {
  D7 = 1u << 0,
  Ownership = 1u << 1,
  GuardCancellation = 1u << 2,
  EarlyAcquisition2PL = 1u << 3,
  Cleanup = 1u << 4,
  Codegen = 1u << 5,
};

inline constexpr std::uint32_t exceptional_consumer_bit(
    ExceptionalCfgConsumer consumer) {
  return static_cast<std::uint32_t>(consumer);
}

inline constexpr std::uint32_t kAllExceptionalCfgConsumers =
    exceptional_consumer_bit(ExceptionalCfgConsumer::D7) |
    exceptional_consumer_bit(ExceptionalCfgConsumer::Ownership) |
    exceptional_consumer_bit(ExceptionalCfgConsumer::GuardCancellation) |
    exceptional_consumer_bit(ExceptionalCfgConsumer::EarlyAcquisition2PL) |
    exceptional_consumer_bit(ExceptionalCfgConsumer::Cleanup) |
    exceptional_consumer_bit(ExceptionalCfgConsumer::Codegen);

struct ExceptionalCfgEdgeAbi {
  std::uint32_t source_node = 0;
  std::uint32_t destination_node = 0;
  ExceptionalEdgeKind kind = ExceptionalEdgeKind::RaiseToHandlerExit;
  RaiseSet alternatives;
  // A producer must explicitly certify every required consumer. Metadata
  // missing any bit fails closed in the verifier and source-free loader.
  std::uint32_t consumers = 0;
};

struct ExceptionalCfgVerification {
  bool ok = true;
  std::vector<std::string> errors;
};

inline ExceptionalCfgVerification verify_exceptional_cfg(
    const std::vector<std::uint32_t>& raising_nodes,
    const std::vector<ExceptionalCfgEdgeAbi>& edges) {
  ExceptionalCfgVerification result;
  std::set<std::uint32_t> unique_nodes(raising_nodes.begin(),
                                      raising_nodes.end());
  if (unique_nodes.size() != raising_nodes.size())
    result.errors.push_back("raising-node list contains duplicates");
  for (std::uint32_t node : raising_nodes) {
    const bool represented = std::any_of(
        edges.begin(), edges.end(), [node](const ExceptionalCfgEdgeAbi& edge) {
          return edge.source_node == node && !edge.alternatives.empty();
        });
    if (!represented)
      result.errors.push_back("raising node " + std::to_string(node) +
                              " has no exceptional edge");
  }
  for (const auto& edge : edges) {
    if (!unique_nodes.count(edge.source_node))
      result.errors.push_back("exceptional edge source is not a raising node");
    if (edge.alternatives.empty())
      result.errors.push_back("exceptional edge has an empty raise set");
    if (edge.consumers != kAllExceptionalCfgConsumers)
      result.errors.push_back("exceptional edge omits a required consumer");
  }
  result.ok = result.errors.empty();
  return result;
}

enum class WrapperRole : std::uint8_t {
  Nested = 0,
  Root = 1,
};

enum class RootFailureStep : std::uint8_t {
  JoinBranches = 0,
  CloseRootFileIO = 1,
  ReleaseAttemptGuards = 2,
  AcquireFreshFailureLocks = 3,
  ExecuteSelectedArm = 4,
  FulfillReply = 5,
};

inline const std::vector<RootFailureStep>& root_failure_sequence() {
  static const std::vector<RootFailureStep> sequence = {
      RootFailureStep::JoinBranches,
      RootFailureStep::CloseRootFileIO,
      RootFailureStep::ReleaseAttemptGuards,
      RootFailureStep::AcquireFreshFailureLocks,
      RootFailureStep::ExecuteSelectedArm,
      RootFailureStep::FulfillReply,
  };
  return sequence;
}

struct ProviderHandlerAbi {
  std::string handler;
  std::string body_symbol;
  std::string opaque_failure_frame_type;
  TaggedOutcomeAbi outcome;
  std::vector<FailureArmAbi> failure_arms;
  FailureReplyAbi reply;
  std::vector<std::uint32_t> raising_nodes;
  std::vector<ExceptionalCfgEdgeAbi> exceptional_edges;
  bool application_owns_nested_wrapper = true;
  bool application_owns_root_wrapper = true;
};

inline bool provider_handler_abi_valid(const ProviderHandlerAbi& abi) {
  if (abi.handler.empty() || abi.body_symbol.empty() ||
      abi.opaque_failure_frame_type.empty() ||
      !tagged_outcome_abi_valid(abi.outcome) ||
      !abi.application_owns_nested_wrapper ||
      !abi.application_owns_root_wrapper)
    return false;
  if (abi.reply.moss_type.empty() || !abi.reply.every_normal_arm_replies)
    return false;
  if (!verify_exceptional_cfg(abi.raising_nodes, abi.exceptional_edges).ok)
    return false;
  std::set<RaisedIdentity> covered;
  std::set<std::string> callable_symbols;
  for (size_t index = 0; index < abi.failure_arms.size(); ++index) {
    const auto& arm = abi.failure_arms[index];
    if (arm.source_order != index ||
        !callable_symbols.insert(arm.callable_symbol).second)
      return false;
    if (arm.pattern.kind == FailurePatternKind::CatchAll) {
      if (!arm.pattern.variants.empty() || index + 1 != abi.failure_arms.size())
        return false;
    } else {
      if (arm.pattern.variants.empty()) return false;
      for (const auto& identity : arm.pattern.variants)
        if (!covered.insert(identity).second) return false;
    }
    if (arm.callable_symbol.empty() || !arm.escaping_raise_set_empty) return false;
    std::set<std::string> capture_names;
    std::set<std::string> capture_fields;
    for (const auto& capture : arm.captures) {
      if (!failure_capture_allowed(capture)) return false;
      if (!capture_names.insert(capture.name).second ||
          !capture_fields.insert(capture.provider_rust_field).second)
        return false;
    }
  }
  std::set<RaisedIdentity> outcome_identities;
  for (const auto& raised : abi.outcome.raised)
    outcome_identities.insert(raised.identity);
  for (const auto& edge : abi.exceptional_edges)
    for (const auto& identity : edge.alternatives)
      if (!outcome_identities.count(identity)) return false;
  return true;
}

}  // namespace moss
