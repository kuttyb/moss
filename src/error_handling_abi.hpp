#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace moss {

// Phase 21.0 freezes the semantic and native provider boundary at ABI v8.
// These are compiler records. They do not introduce a source-level union of
// unrelated Moss error enums.
inline constexpr int kErrorHandlingAbiVersion = 8;

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

// This is the checker-resolved representation of a Moss type. ABI legality is
// decided from this tree, never from a substring match on the source spelling.
enum class ResolvedTypeKind : std::uint8_t {
  Scalar = 0,
  ErrorEnum = 1,
  Aggregate = 2,
  OwnedUnbounded = 3,
  Capability = 4,
  Borrowed = 5,
  Unknown = 6,
};

struct ResolvedTypeAbi {
  ResolvedTypeKind kind = ResolvedTypeKind::Unknown;
  std::string canonical_type;
  std::vector<ResolvedTypeAbi> children;

  friend bool operator==(const ResolvedTypeAbi& left,
                         const ResolvedTypeAbi& right) {
    return left.kind == right.kind &&
           left.canonical_type == right.canonical_type &&
           left.children == right.children;
  }
};

inline void append_resolved_type_fingerprint(std::string& output,
                                             const ResolvedTypeAbi& type) {
  output += std::to_string(static_cast<unsigned>(type.kind)) + ":";
  output += std::to_string(type.canonical_type.size()) + ":";
  output += type.canonical_type + ":";
  output += std::to_string(type.children.size()) + ":";
  for (const auto& child : type.children)
    append_resolved_type_fingerprint(output, child);
}

inline std::string resolved_type_fingerprint(const ResolvedTypeAbi& type) {
  std::string result;
  append_resolved_type_fingerprint(result, type);
  return result;
}

inline bool parse_resolved_type_fingerprint_part(
    const std::string& text, size_t& cursor, ResolvedTypeAbi& result) {
  const auto number = [&](size_t& value) {
    const auto end = text.find(':', cursor);
    if (end == std::string::npos || end == cursor) return false;
    try {
      value = static_cast<size_t>(std::stoull(text.substr(cursor, end - cursor)));
    } catch (...) {
      return false;
    }
    cursor = end + 1;
    return true;
  };
  size_t kind = 0, name_size = 0, child_count = 0;
  if (!number(kind) || kind > static_cast<size_t>(ResolvedTypeKind::Unknown) ||
      !number(name_size) || cursor + name_size >= text.size())
    return false;
  result.kind = static_cast<ResolvedTypeKind>(kind);
  result.canonical_type = text.substr(cursor, name_size);
  cursor += name_size;
  if (cursor >= text.size() || text[cursor++] != ':' || !number(child_count) ||
      child_count > 1000000)
    return false;
  for (size_t index = 0; index < child_count; ++index) {
    ResolvedTypeAbi child;
    if (!parse_resolved_type_fingerprint_part(text, cursor, child)) return false;
    result.children.push_back(std::move(child));
  }
  return true;
}

inline std::optional<ResolvedTypeAbi> parse_resolved_type_fingerprint(
    const std::string& text) {
  size_t cursor = 0;
  ResolvedTypeAbi result;
  if (!parse_resolved_type_fingerprint_part(text, cursor, result) ||
      cursor != text.size())
    return std::nullopt;
  return result;
}

inline bool resolved_type_owned_noncapability(const ResolvedTypeAbi& type) {
  if (type.canonical_type.empty() ||
      type.kind == ResolvedTypeKind::Capability ||
      type.kind == ResolvedTypeKind::Borrowed ||
      type.kind == ResolvedTypeKind::Unknown)
    return false;
  return std::all_of(type.children.begin(), type.children.end(),
                     resolved_type_owned_noncapability);
}

inline bool resolved_type_bounded_payload(const ResolvedTypeAbi& type) {
  if (!resolved_type_owned_noncapability(type) ||
      type.kind == ResolvedTypeKind::Aggregate ||
      type.kind == ResolvedTypeKind::OwnedUnbounded)
    return false;
  return std::all_of(type.children.begin(), type.children.end(),
                     resolved_type_bounded_payload);
}

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
  ResolvedTypeAbi resolved_type;
};

// A recovery projection consumes an opaque frame. A match yields the owned
// payload and an opaque residual containing private captures/origin state;
// rebuild consumes both and restores the exact frame for bare re-raise.
struct RaisedProjectionAbi {
  std::string payload_rust_type;
  std::string residual_rust_type;
  std::string projection_rust_type;
  std::string project_symbol;
  std::string rebuild_symbol;

  bool valid() const {
    return !payload_rust_type.empty() && !residual_rust_type.empty() &&
           !projection_rust_type.empty() && !project_symbol.empty() &&
           !rebuild_symbol.empty();
  }
};

struct RaisedValueAbi {
  RaisedIdentity identity;
  std::uint32_t stable_tag = 0;
  std::vector<OwnedPayloadFieldAbi> payload;
  RaisedProjectionAbi projection;
};

struct TaggedOutcomeAbi {
  std::string rust_type;
  std::string normal_type;
  // One specialization-local closed table may contain variants belonging to
  // several Moss enums. Only its stable tag is visible to the application;
  // the provider owns the actual Rust payload enum.
  std::vector<RaisedValueAbi> raised;
};

struct RaisedTagTranslationAbi {
  std::uint32_t callee_tag = 0;
  std::uint32_t caller_tag = 0;
  RaisedIdentity identity;
};

// The caller owns this adapter. It consumes the callee's public opaque frame
// and stores it in one variant of the caller-private carrier enum. It never
// casts, serializes, or returns a callee frame as a caller frame.
struct ImportedRaiseBridgeAbi {
  std::uint32_t source_order = 0;
  std::string callee_specialization;
  std::string callee_outcome_rust_type;
  std::string callee_frame_rust_type;
  std::string caller_carrier_variant;
  std::string bridge_symbol;
  std::vector<RaisedTagTranslationAbi> translations;
};

inline bool tagged_outcome_abi_valid(const TaggedOutcomeAbi& outcome);

inline bool imported_raise_bridge_valid(const ImportedRaiseBridgeAbi& bridge,
                                        const TaggedOutcomeAbi& caller) {
  if (bridge.callee_specialization.empty() ||
      bridge.callee_outcome_rust_type.empty() ||
      bridge.callee_frame_rust_type.empty() ||
      bridge.caller_carrier_variant.empty() || bridge.bridge_symbol.empty() ||
      bridge.translations.empty())
    return false;
  std::set<std::uint32_t> callee_tags;
  std::set<std::uint32_t> caller_tags;
  for (const auto& translation : bridge.translations) {
    if (!translation.identity.valid() ||
        translation.caller_tag >= caller.raised.size() ||
        !callee_tags.insert(translation.callee_tag).second ||
        !caller_tags.insert(translation.caller_tag).second ||
        !(caller.raised[translation.caller_tag].identity ==
          translation.identity))
      return false;
  }
  return true;
}

// Local handler validation proves that a bridge is internally consistent with
// its caller.  Application linkage additionally supplies the loaded callee ABI
// and proves that every translated tag denotes the same semantic identity.
inline bool imported_raise_bridge_matches_callee(
    const ImportedRaiseBridgeAbi& bridge,
    const TaggedOutcomeAbi& caller,
    const std::string& callee_specialization,
    const std::string& callee_frame_rust_type,
    const TaggedOutcomeAbi& callee) {
  if (!imported_raise_bridge_valid(bridge, caller) ||
      bridge.callee_specialization != callee_specialization ||
      bridge.callee_outcome_rust_type != callee.rust_type ||
      bridge.callee_frame_rust_type != callee_frame_rust_type ||
      !tagged_outcome_abi_valid(callee))
    return false;
  for (const auto& translation : bridge.translations)
    if (translation.callee_tag >= callee.raised.size() ||
        callee.raised[translation.callee_tag].stable_tag !=
            translation.callee_tag ||
        !(callee.raised[translation.callee_tag].identity ==
          translation.identity) ||
        callee.raised[translation.callee_tag].payload.size() !=
            caller.raised[translation.caller_tag].payload.size())
      return false;
    else
      for (size_t field_index = 0;
           field_index < callee.raised[translation.callee_tag].payload.size();
           ++field_index) {
        const auto& callee_field =
            callee.raised[translation.callee_tag].payload[field_index];
        const auto& caller_field =
            caller.raised[translation.caller_tag].payload[field_index];
        if (callee_field.name != caller_field.name ||
            callee_field.moss_type != caller_field.moss_type ||
            callee_field.kind != caller_field.kind ||
            callee_field.owned != caller_field.owned ||
            callee_field.bounded != caller_field.bounded ||
            !(callee_field.resolved_type == caller_field.resolved_type))
          return false;
      }
  return true;
}

inline bool owned_payload_field_allowed(const OwnedPayloadFieldAbi& field) {
  if (field.name.empty() || field.moss_type.empty() || !field.owned ||
      !field.bounded || field.resolved_type.canonical_type != field.moss_type ||
      !resolved_type_bounded_payload(field.resolved_type))
    return false;
  return (field.kind == OwnedPayloadFieldAbi::Kind::Scalar &&
          field.resolved_type.kind == ResolvedTypeKind::Scalar) ||
         (field.kind == OwnedPayloadFieldAbi::Kind::ErrorEnum &&
          field.resolved_type.kind == ResolvedTypeKind::ErrorEnum);
}

inline bool tagged_outcome_abi_valid(const TaggedOutcomeAbi& outcome) {
  if (outcome.rust_type.empty() || outcome.normal_type.empty()) return false;
  std::set<std::string> identities;
  std::set<std::string> payload_types;
  std::set<std::string> residual_types;
  std::set<std::string> projection_types;
  std::set<std::string> project_symbols;
  std::set<std::string> rebuild_symbols;
  std::optional<RaisedIdentity> previous;
  for (size_t index = 0; index < outcome.raised.size(); ++index) {
    const auto& raised = outcome.raised[index];
    if (!raised.identity.valid() || raised.stable_tag != index ||
        !raised.projection.valid() ||
        !payload_types.insert(raised.projection.payload_rust_type).second ||
        !residual_types.insert(raised.projection.residual_rust_type).second ||
        !projection_types.insert(raised.projection.projection_rust_type).second ||
        !project_symbols.insert(raised.projection.project_symbol).second ||
        !rebuild_symbols.insert(raised.projection.rebuild_symbol).second ||
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
  ResolvedTypeAbi resolved_type;
};

inline bool failure_capture_allowed(const FailureCaptureAbi& capture) {
  return !capture.name.empty() && !capture.provider_rust_field.empty() &&
         capture.owned && capture.definitely_initialized &&
         capture.resolved_type.canonical_type == capture.moss_type &&
         resolved_type_owned_noncapability(capture.resolved_type);
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

struct RaisingNodeAbi {
  std::uint32_t node = 0;
  RaiseSet possible_alternatives;
  // Lexical try scopes that may receive a matching alternative, ordered from
  // innermost to outermost. Scope zero is the handler boundary.
  std::vector<std::uint32_t> recovery_scope_chain;
  // True only for a syntactic bare raise in the innermost active recovery
  // arm. Such a node may route only through ReRaiseToOuter.
  bool selected_error_slot = false;
  std::uint32_t selected_error_scope = 0;
  std::uint32_t outer_scope = 0;
};

enum class ExceptionalDestinationKind : std::uint8_t {
  RecoveryArm = 0,
  HandlerExit = 1,
  OuterRecoveryOrExit = 2,
};

struct ExceptionalDestinationAbi {
  std::uint32_t node = 0;
  ExceptionalDestinationKind kind = ExceptionalDestinationKind::HandlerExit;
  std::uint32_t lexical_scope = 0;
  std::uint32_t match_order = 0;
  RaiseSet accepted_alternatives;
};

struct ExceptionalCfgVerification {
  bool ok = true;
  std::vector<std::string> errors;
};

inline ExceptionalCfgVerification verify_exceptional_cfg(
    const std::vector<RaisingNodeAbi>& raising_nodes,
    const std::vector<ExceptionalDestinationAbi>& destinations,
    const std::vector<ExceptionalCfgEdgeAbi>& edges) {
  ExceptionalCfgVerification result;
  std::set<std::uint32_t> unique_nodes;
  std::map<std::uint32_t, ExceptionalDestinationKind> destination_kinds;
  std::set<std::pair<std::uint32_t, std::uint32_t>> recovery_orders;
  for (const auto& destination : destinations) {
    if (!destination_kinds.emplace(destination.node, destination.kind).second)
      result.errors.push_back("exceptional destination list contains duplicates");
    if (destination.kind == ExceptionalDestinationKind::RecoveryArm &&
        !recovery_orders.emplace(destination.lexical_scope,
                                 destination.match_order).second)
      result.errors.push_back(
          "recovery destinations duplicate lexical match order");
  }
  for (const auto& raising : raising_nodes) {
    if (!unique_nodes.insert(raising.node).second)
      result.errors.push_back("raising-node list contains duplicates");
    if (raising.possible_alternatives.empty())
      result.errors.push_back("raising node " + std::to_string(raising.node) +
                              " has no possible alternatives");
    std::set<std::uint32_t> unique_scopes(
        raising.recovery_scope_chain.begin(),
        raising.recovery_scope_chain.end());
    if (unique_scopes.size() != raising.recovery_scope_chain.size())
      result.errors.push_back(
          "raising node recovery scope chain contains duplicates");
  }
  std::map<std::uint32_t, RaiseSet> routed;
  std::set<std::tuple<std::uint32_t, std::uint32_t,
                      ExceptionalEdgeKind, std::string>> exact_edges;
  for (const auto& edge : edges) {
    if (!unique_nodes.count(edge.source_node))
      result.errors.push_back("exceptional edge source is not a raising node");
    auto destination = destination_kinds.find(edge.destination_node);
    if (destination == destination_kinds.end()) {
      result.errors.push_back("exceptional edge has an invalid destination");
    } else {
      const bool compatible =
          (edge.kind == ExceptionalEdgeKind::RaiseToRecover &&
           destination->second == ExceptionalDestinationKind::RecoveryArm) ||
          (edge.kind == ExceptionalEdgeKind::RaiseToHandlerExit &&
           destination->second == ExceptionalDestinationKind::HandlerExit) ||
          (edge.kind == ExceptionalEdgeKind::ReRaiseToOuter &&
           destination->second ==
               ExceptionalDestinationKind::OuterRecoveryOrExit);
      if (!compatible)
        result.errors.push_back(
            "exceptional edge kind contradicts its destination");
    }
    if (edge.alternatives.empty())
      result.errors.push_back("exceptional edge has an empty raise set");
    if (edge.consumers != kAllExceptionalCfgConsumers)
      result.errors.push_back("exceptional edge omits a required consumer");
    auto raising = std::find_if(
        raising_nodes.begin(), raising_nodes.end(),
        [&](const RaisingNodeAbi& candidate) {
          return candidate.node == edge.source_node;
        });
    if (raising != raising_nodes.end()) {
      if (raising->selected_error_slot !=
          (edge.kind == ExceptionalEdgeKind::ReRaiseToOuter))
        result.errors.push_back(
            "exceptional edge violates lexical re-raise routing");
      if (destination != destination_kinds.end()) {
        const auto destination_record = std::find_if(
            destinations.begin(), destinations.end(),
            [&](const ExceptionalDestinationAbi& candidate) {
              return candidate.node == edge.destination_node;
            });
        if (edge.kind == ExceptionalEdgeKind::RaiseToRecover &&
            std::find(raising->recovery_scope_chain.begin(),
                      raising->recovery_scope_chain.end(),
                      destination_record->lexical_scope) ==
                raising->recovery_scope_chain.end())
          result.errors.push_back(
              "recover edge escapes the node's lexical try chain");
        if (edge.kind == ExceptionalEdgeKind::RaiseToHandlerExit &&
            destination_record->lexical_scope != 0)
          result.errors.push_back(
              "handler-exit edge targets a lexical recovery scope");
        if (edge.kind == ExceptionalEdgeKind::ReRaiseToOuter &&
            (raising->selected_error_scope == 0 ||
             destination_record->lexical_scope != raising->outer_scope ||
             destination_record->lexical_scope ==
                 raising->selected_error_scope))
          result.errors.push_back(
              "bare re-raise does not target the designated outer scope");
        for (const auto& alternative : edge.alternatives)
          if (!destination_record->accepted_alternatives.count(alternative))
            result.errors.push_back(
                "exceptional destination does not accept an alternative");
        if (!raising->selected_error_slot &&
            (edge.kind == ExceptionalEdgeKind::RaiseToRecover ||
             edge.kind == ExceptionalEdgeKind::RaiseToHandlerExit)) {
          for (const auto& alternative : edge.alternatives) {
            const ExceptionalDestinationAbi* nearest = nullptr;
            for (const auto scope : raising->recovery_scope_chain) {
              for (const auto& candidate : destinations) {
                if (candidate.kind !=
                        ExceptionalDestinationKind::RecoveryArm ||
                    candidate.lexical_scope != scope ||
                    !candidate.accepted_alternatives.count(alternative))
                  continue;
                if (!nearest || candidate.match_order < nearest->match_order)
                  nearest = &candidate;
              }
              if (nearest) break;
            }
            if (nearest &&
                (edge.kind != ExceptionalEdgeKind::RaiseToRecover ||
                 edge.destination_node != nearest->node))
              result.errors.push_back(
                  "exceptional edge skips the nearest lexical recovery arm");
            if (!nearest &&
                edge.kind != ExceptionalEdgeKind::RaiseToHandlerExit)
              result.errors.push_back(
                  "exceptional edge recovers an alternative with no matching lexical arm");
          }
        }
      }
      for (const auto& alternative : edge.alternatives) {
        if (!raising->possible_alternatives.count(alternative))
          result.errors.push_back(
              "exceptional edge routes an impossible alternative");
        if (!routed[edge.source_node].insert(alternative).second)
          result.errors.push_back(
              "exceptional alternative has duplicate or contradictory routing");
        const auto key = std::make_tuple(
            edge.source_node, edge.destination_node, edge.kind,
            alternative.canonical());
        if (!exact_edges.insert(key).second)
          result.errors.push_back("duplicate exceptional edge");
      }
    }
  }
  for (const auto& raising : raising_nodes) {
    if (routed[raising.node] != raising.possible_alternatives)
      result.errors.push_back("raising node " + std::to_string(raising.node) +
                              " does not route every possible alternative");
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
  std::string raised_carrier_rust_type;
  TaggedOutcomeAbi outcome;
  std::vector<ImportedRaiseBridgeAbi> imported_raise_bridges;
  // Canonically ordered, counted declarations for every structural type used
  // by a payload or capture, including provider-private aggregate dependencies.
  std::vector<ResolvedTypeAbi> owned_type_shapes;
  std::vector<FailureArmAbi> failure_arms;
  FailureReplyAbi reply;
  std::vector<RaisingNodeAbi> raising_nodes;
  std::vector<ExceptionalDestinationAbi> exceptional_destinations;
  std::vector<ExceptionalCfgEdgeAbi> exceptional_edges;
  bool application_owns_nested_wrapper = true;
  bool application_owns_root_wrapper = true;
  bool nested_wrapper_may_propagate = true;
  bool root_wrapper_requires_total_dispatch = true;
};

inline bool provider_handler_abi_valid(const ProviderHandlerAbi& abi) {
  if (abi.handler.empty() || abi.body_symbol.empty() ||
      abi.opaque_failure_frame_type.empty() ||
      abi.raised_carrier_rust_type.empty() ||
      !tagged_outcome_abi_valid(abi.outcome) ||
      !abi.application_owns_nested_wrapper ||
      !abi.application_owns_root_wrapper ||
      !abi.nested_wrapper_may_propagate ||
      !abi.root_wrapper_requires_total_dispatch)
    return false;
  if (abi.reply.moss_type.empty() || !abi.reply.every_normal_arm_replies)
    return false;
  if (!verify_exceptional_cfg(abi.raising_nodes, abi.exceptional_destinations,
                              abi.exceptional_edges).ok)
    return false;
  std::set<std::string> carrier_variants;
  std::set<std::string> bridge_symbols;
  for (size_t bridge_index = 0;
       bridge_index < abi.imported_raise_bridges.size(); ++bridge_index) {
    const auto& bridge = abi.imported_raise_bridges[bridge_index];
    if (bridge.source_order != bridge_index ||
        !imported_raise_bridge_valid(bridge, abi.outcome) ||
        !carrier_variants.insert(bridge.caller_carrier_variant).second ||
        !bridge_symbols.insert(bridge.bridge_symbol).second)
      return false;
  }
  std::map<std::string, ResolvedTypeAbi> owned_type_shapes;
  std::optional<std::string> previous_shape;
  for (const auto& shape : abi.owned_type_shapes) {
    if (shape.canonical_type.empty() ||
        shape.kind == ResolvedTypeKind::Unknown ||
        (previous_shape && *previous_shape >= shape.canonical_type) ||
        !owned_type_shapes.emplace(shape.canonical_type, shape).second)
      return false;
    previous_shape = shape.canonical_type;
  }
  for (const auto& entry : owned_type_shapes)
    for (const auto& child : entry.second.children) {
      auto dependency = owned_type_shapes.find(child.canonical_type);
      if (dependency == owned_type_shapes.end() ||
          !(dependency->second == child))
        return false;
    }
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
        if (!identity.valid() || !covered.insert(identity).second) return false;
    }
    if (arm.callable_symbol.empty() || !arm.escaping_raise_set_empty) return false;
    std::set<std::string> capture_names;
    std::set<std::string> capture_fields;
    for (const auto& capture : arm.captures) {
      auto shape = owned_type_shapes.find(capture.moss_type);
      if (!failure_capture_allowed(capture) ||
          shape == owned_type_shapes.end() ||
          !(shape->second == capture.resolved_type))
        return false;
      if (!capture_names.insert(capture.name).second ||
          !capture_fields.insert(capture.provider_rust_field).second)
        return false;
    }
  }
  for (const auto& raised : abi.outcome.raised)
    for (const auto& payload : raised.payload) {
      auto shape = owned_type_shapes.find(payload.moss_type);
      if (shape == owned_type_shapes.end() ||
          !(shape->second == payload.resolved_type))
        return false;
    }
  std::set<RaisedIdentity> outcome_identities;
  for (const auto& raised : abi.outcome.raised)
    outcome_identities.insert(raised.identity);
  RaiseSet handler_exit_identities;
  for (const auto& edge : abi.exceptional_edges)
    if (edge.kind == ExceptionalEdgeKind::RaiseToHandlerExit)
      handler_exit_identities.insert(edge.alternatives.begin(),
                                     edge.alternatives.end());
  return handler_exit_identities == outcome_identities;
}

struct RootTotalityVerification {
  bool ok = true;
  RaiseSet missing;
  std::vector<std::string> errors;
};

// Provider validity and application Root validity are intentionally separate.
// A provider with partial/no on_fail arms can be called from another handler,
// where its typed frame is propagated. A concrete Root wrapper cannot be
// emitted unless every escaping identity selects exactly one arm.
inline RootTotalityVerification verify_root_totality(
    const ProviderHandlerAbi& abi, const RaiseSet& escaping) {
  RootTotalityVerification result;
  RaiseSet outcome_identities;
  for (const auto& raised : abi.outcome.raised)
    outcome_identities.insert(raised.identity);
  if (escaping != outcome_identities)
    result.errors.push_back(
        "Root escaping set does not match the concrete body outcome");
  RaiseSet remaining = escaping;
  std::set<RaisedIdentity> selected;
  bool catch_all = false;
  for (const auto& arm : abi.failure_arms) {
    if (!arm.escaping_raise_set_empty) {
      result.errors.push_back("Root failure arm may itself escape");
      continue;
    }
    if (arm.pattern.kind == FailurePatternKind::CatchAll) {
      catch_all = true;
      remaining.clear();
      continue;
    }
    for (const auto& identity : arm.pattern.variants) {
      if (!escaping.count(identity)) continue;  // legal unreachable arm
      if (!selected.insert(identity).second)
        result.errors.push_back("Root alternative selects multiple arms");
      remaining.erase(identity);
    }
  }
  if (!remaining.empty() && !catch_all) {
    result.missing = remaining;
    result.errors.push_back("Root dispatch omits an escaping alternative");
  }
  if (!abi.reply.every_normal_arm_replies)
    result.errors.push_back("Root failure reply is not total");
  result.ok = result.errors.empty();
  return result;
}

}  // namespace moss
