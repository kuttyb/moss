#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "ast.hpp"

namespace moss {

// Compiler-internal handoff. These records own checked TypeRefs and exact IDs;
// the ABI-v10 serializer is a separate Team B consumer.
struct TypedSourceProvenanceV1 {
  std::string source_file;
  int line{0};
  std::string defining_module_id;
  std::string enclosing_callable_id;
};

struct TypedNominalTemplateV1 {
  struct Field {
    std::string name;
    TypeRef declared_type;  // null for an inferred slot
    std::optional<uint32_t> variable_ordinal;
  };
  NominalDeclarationRef declaration;
  std::vector<Field> fields;
  bool exported{false};
  TypedSourceProvenanceV1 provenance;
};

struct TypedClosedTypeV1 {
  TypeRef type;
  std::string type_key;
  std::optional<std::string> nominal_template_id;
  std::optional<std::string> nominal_instance_key;
  std::vector<TypeRef> ordered_resolved_fields;
};

struct TypedNominalInstanceV1 {
  std::string template_id;
  TypeRef type;
  std::string instance_key;
  std::vector<TypeRef> ordered_resolved_fields;
};

struct TypedSemanticDependencyV1 {
  std::string callable_id;
  std::string specialization_key;
  TypedSourceProvenanceV1 provenance;
};

struct TypedSpecializationV1 {
  std::string key;
  SpecializationKey identity;
  std::string callable_id;
  bool exported{false};
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Effect> parameter_access;
  std::optional<StateLeafEffects> parameter_leaf_effects;
  ObservableEffects observable_effects;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<TypedSemanticDependencyV1> dependencies;
  TypedSourceProvenanceV1 provenance;
};

// Checker-owned ownership access for one local binding definition or Vector
// loop element. expression_occurrence_id names the RHS for LocalBinding and
// the iterable expression for VectorElement. definition_identity distinguishes
// rebinding versions of one source name or the exact loop element binding.
// Consumers must treat unavailable records as missing semantic authority.
struct TypedLocalOwnershipFactV1 {
  enum class SourceKind { LocalBinding, VectorElement };
  enum class InferenceStatus { Inferred, Unsupported };
  enum class UnsupportedReason {
    None, ControlFlowJoin, LoopCarriedUse, AmbiguousUse, UnsupportedSource,
    ConsumingLoopElement
  };
  enum class Availability {
    NotRequired, Present, MissingEstOccurrence, MissingStatementOccurrence,
    MissingBindingIdentity, ContradictoryEffect
  };

  std::string definition_identity;
  // Non-empty for a branch definition reaching a checked live join. All
  // predecessor facts for that joined local share this identity.
  std::string join_definition_identity;
  std::string source_binding_identity;
  // Stable checker-issued key for the checked specialization, empty for a
  // non-specialized body. This is distinct from specialization_context, which
  // is the checker-published consumer lookup alias used by existing engines.
  std::string specialization_identity;
  std::string specialization_context;
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId expression_occurrence_id{UINT32_MAX};
  Effect access{Effect::Read};
  SourceKind source_kind{SourceKind::LocalBinding};
  InferenceStatus inference_status{InferenceStatus::Inferred};
  UnsupportedReason unsupported_reason{UnsupportedReason::None};
  Availability availability{Availability::MissingEstOccurrence};
  TypeRef value_type;
  TypedSourceProvenanceV1 provenance;

  // Consumer gate: an inferred record is authoritative only when every
  // identity needed to locate its checker decision is present. Unsupported
  // inference and incomplete/contradictory records are never authority.
  bool is_authoritative() const noexcept {
    return inference_status == InferenceStatus::Inferred &&
        availability == Availability::Present &&
        !definition_identity.empty() && !source_binding_identity.empty() &&
        !specialization_context.empty() &&
        statement_occurrence_id != UINT32_MAX &&
        est_arena_index != UINT32_MAX &&
        expression_occurrence_id != UINT32_MAX;
  }

  bool is_intentionally_unsupported() const noexcept {
    return inference_status == InferenceStatus::Unsupported &&
        unsupported_reason != UnsupportedReason::None &&
        availability == Availability::NotRequired;
  }
};

// Checker-owned requirement that a local ownership fact must exist for
// an expression occurrence. If an occurrence appears here, consumers must
// fail closed with MOSS_INTERNAL_INVARIANT if no authoritative or
// intentionally unsupported fact is present in local_ownership_facts.
struct TypedLocalOwnershipRequirementV1 {
  using SourceKind = TypedLocalOwnershipFactV1::SourceKind;
  using InferenceStatus = TypedLocalOwnershipFactV1::InferenceStatus;
  using UnsupportedReason = TypedLocalOwnershipFactV1::UnsupportedReason;
  using Availability = TypedLocalOwnershipFactV1::Availability;
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId expression_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  // Stable checker-issued specialization key; empty for ordinary bodies.
  std::string specialization_identity;
  // Exact context alias published by the checker for existing consumers.
  std::string specialization_context;
  std::string definition_identity;
  std::string join_definition_identity;
  std::string source_binding_identity;
  Effect access{Effect::Read};
  SourceKind source_kind{SourceKind::LocalBinding};
  InferenceStatus inference_status{InferenceStatus::Inferred};
  UnsupportedReason unsupported_reason{UnsupportedReason::None};
  Availability availability{Availability::MissingEstOccurrence};
};

enum class TypedTakeReplacementV1 { OptionNone, VectorEmpty, MapEmpty, QueueEmpty };
enum class TypedTakeProjectionKindV1 {
  RecordField, VectorIndex, MapIndex, QueueIndex
};

struct TypedTakeProjectionV1 {
  TypedTakeProjectionKindV1 kind{TypedTakeProjectionKindV1::RecordField};
  std::uint32_t expression_occurrence_id{UINT32_MAX};
  std::uint32_t selector_occurrence_id{UINT32_MAX};
  std::string semantic_identity;
  std::uint32_t ordinal{UINT32_MAX};
  TypeRef input_type;
  TypeRef output_type;
};

struct TypedTakeFactV1 {
  enum class Availability {
    Present, MissingTakeOccurrence, MissingPlaceOccurrence,
    MissingStatementOccurrence, MissingBindingIdentity, MissingType,
    Contradictory
  };
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId take_occurrence_id{UINT32_MAX};
  est::ExprId place_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  std::string root_binding_identity;
  std::string root_name;
  TypeRef terminal_type;
  TypeRef result_type;
  std::vector<TypedTakeProjectionV1> projection_path;
  TypedTakeReplacementV1 replacement{TypedTakeReplacementV1::OptionNone};
  Effect required_access{Effect::Write};
  bool transfers_original_value{false};
  bool exclusive_projection_access{false};
  Availability availability{Availability::MissingTakeOccurrence};

  bool is_authoritative() const noexcept {
    const bool replacement_matches_type = terminal_type &&
        ((terminal_type->kind == TypeKind::Builtin &&
          terminal_type->name == "option" &&
          replacement == TypedTakeReplacementV1::OptionNone) ||
         (terminal_type->kind == TypeKind::Builtin &&
          terminal_type->name == "vector" &&
          replacement == TypedTakeReplacementV1::VectorEmpty) ||
         (terminal_type->kind == TypeKind::Builtin &&
          terminal_type->name == "map" &&
          replacement == TypedTakeReplacementV1::MapEmpty) ||
         (terminal_type->kind == TypeKind::Builtin &&
          terminal_type->name == "queue" &&
          replacement == TypedTakeReplacementV1::QueueEmpty));
    return availability == Availability::Present &&
        est_arena_index != UINT32_MAX && take_occurrence_id != UINT32_MAX &&
        place_occurrence_id != UINT32_MAX &&
        statement_occurrence_id != UINT32_MAX &&
        !specialization_context.empty() && !est_fact_context.empty() &&
        !root_binding_identity.empty() && !root_name.empty() &&
        terminal_type && result_type && !projection_path.empty() &&
        terminal_type->structurally_equal(*result_type) &&
        replacement_matches_type &&
        std::all_of(projection_path.begin(), projection_path.end(),
            [](const auto& projection) {
              return projection.expression_occurrence_id != UINT32_MAX &&
                  projection.input_type && projection.output_type &&
                  !projection.semantic_identity.empty() &&
                  (projection.kind != TypedTakeProjectionKindV1::VectorIndex &&
                   projection.kind != TypedTakeProjectionKindV1::MapIndex &&
                   projection.kind != TypedTakeProjectionKindV1::QueueIndex
                       ? projection.selector_occurrence_id == UINT32_MAX
                       : projection.selector_occurrence_id != UINT32_MAX);
            }) &&
        projection_path.back().output_type->structurally_equal(*terminal_type) &&
        [&] {
          for (std::size_t index = 0; index < projection_path.size(); ++index) {
            const auto& projection = projection_path[index];
            if (projection.ordinal != index) return false;
            if (index && !projection_path[index - 1].output_type->structurally_equal(
                             *projection.input_type)) return false;
          }
          return true;
        }() &&
        required_access == Effect::Write && transfers_original_value &&
        exclusive_projection_access;
  }
};

struct TypedTakeRequirementV1 {
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId take_occurrence_id{UINT32_MAX};
  est::ExprId place_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  std::string root_binding_identity;
  std::string root_name;
  TypeRef terminal_type;
  TypeRef result_type;
  std::vector<TypedTakeProjectionV1> projection_path;
  TypedTakeReplacementV1 replacement{TypedTakeReplacementV1::OptionNone};
  Effect required_access{Effect::Write};
  bool transfers_original_value{false};
  bool exclusive_projection_access{false};
};

// A missing or incomplete checker summary is represented as unavailable,
// never guessed from the function's display result or source text.
struct TypedOpenCallableSummaryV1 {
  std::string callable_id;
  bool exported{false};
  // A hash is present only when the checker retained supported semantic nodes.
  bool checked_body_ir_available{false};
  std::optional<CheckedBodyV1> checked_body_v1;
  std::optional<std::string> checked_body_ir_v1_hash;
  uint32_t bound_variable_count{0};
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<Requirement> requirements;
  std::vector<MemberDispatchRequirement> member_requirements;
  std::vector<std::string> semantic_dependencies;
  std::vector<AccessMode> parameter_access;
  // Present only when the checker retained exact parameter leaf paths.
  std::optional<StateLeafEffects> parameter_leaf_effects;
  ObservableEffects observable_effects;
  TypedSourceProvenanceV1 provenance;
};

enum class TypedOpenSummaryGapV1 {
  MissingTypedFacts,
  UnresolvedEffects,
  MissingParameterAccess
};

inline const char* typed_open_summary_gap_name(TypedOpenSummaryGapV1 gap) {
  switch (gap) {
    case TypedOpenSummaryGapV1::MissingTypedFacts: return "missing_typed_facts";
    case TypedOpenSummaryGapV1::UnresolvedEffects: return "unresolved_effects";
    case TypedOpenSummaryGapV1::MissingParameterAccess:
      return "missing_parameter_access";
  }
  throw std::invalid_argument("unknown typed open summary gap");
}

inline const char* typed_open_summary_gap_message(TypedOpenSummaryGapV1 gap) {
  switch (gap) {
    case TypedOpenSummaryGapV1::MissingTypedFacts:
      return "checker has no complete typed open summary";
    case TypedOpenSummaryGapV1::UnresolvedEffects:
      return "checker result or effect facts remain unresolved";
    case TypedOpenSummaryGapV1::MissingParameterAccess:
      return "checker parameter access facts remain unresolved";
  }
  throw std::invalid_argument("unknown typed open summary gap");
}

struct TypedSemanticModuleV1 {
  static constexpr const char* version = "typed-semantic-v1";
  std::map<std::string, std::shared_ptr<const std::string>> modules;
  std::map<std::string, TypedNominalTemplateV1> nominal_templates;
  std::map<std::string, TypedClosedTypeV1> closed_types;
  std::map<std::string, TypedNominalInstanceV1> nominal_instances;
  std::map<std::string, TypedOpenCallableSummaryV1> open_summaries;
  // Checked source requirements remain inspectable when unresolved callback
  // effects prevent publication of their enclosing open summary. These facts
  // are not an ABI-publishable callable summary.
  std::map<std::string, Requirement> source_requirements;
  // Source requirements remain addressable when a generic summary cannot
  // publish its unresolved observable effects yet. This table does not make
  // such a summary callable through the typed API.
  std::map<std::string, MemberDispatchRequirement> source_member_requirements;
  // Structured absence keeps ABI consumers from parsing diagnostic prose.
  std::map<std::string, TypedOpenSummaryGapV1> unavailable_open_summaries;
  std::map<std::string, TypedSpecializationV1> specializations;
  // Facts for checked local definitions and Vector loop elements. Unsupported
  // means the checker deliberately declined this bounded inference. Missing
  // or contradictory availability on an Inferred record is missing authority
  // and must fail closed.
  std::vector<TypedLocalOwnershipFactV1> local_ownership_facts;
  // Checker-owned occurrences where a local ownership fact is required.
  // Whole-record omission of a required fact causes consumer fail-closed.
  std::vector<TypedLocalOwnershipRequirementV1> required_local_ownership_occurrences;
  // Checker-issued bridge from a consumer's existing function-context alias
  // to the stable key in `specializations`. Consumers must not decode either.
  std::map<std::string, std::string> local_ownership_specialization_contexts;
  std::vector<TypedTakeFactV1> take_facts;
  std::vector<TypedTakeRequirementV1> required_take_occurrences;
  // Legacy imported generic skeletons may appear in Function::specializations
  // with open type terms. They cannot be projected as closed specializations.
  // Keep their exact IDs visible to consumers until A2 supplies summaries.
  std::map<std::string, std::string> unavailable_specializations;
  // False until A2 emits all reachable open summaries directly from solver
  // terms. ABI consumers must call require_complete_open_summaries().
  bool open_summaries_complete{true};

  const std::string& local_ownership_specialization_identity(
      const std::string& consumer_context) const {
    const auto found = local_ownership_specialization_contexts.find(
        consumer_context);
    if (found == local_ownership_specialization_contexts.end() ||
        specializations.find(found->second) == specializations.end())
      throw std::invalid_argument(
          "unknown checker-issued local ownership specialization context");
    return found->second;
  }

  void validate_local_ownership_requirement_specialization(
      const TypedLocalOwnershipRequirementV1& requirement) const {
    const auto mapping = local_ownership_specialization_contexts.find(
        requirement.specialization_context);
    if (mapping == local_ownership_specialization_contexts.end()) {
      if (!requirement.specialization_identity.empty())
        throw std::invalid_argument(
            "local ownership specialization identity has no checker mapping");
      return;
    }
    if (requirement.specialization_identity.empty() ||
        local_ownership_specialization_identity(
            requirement.specialization_context) !=
            requirement.specialization_identity)
      throw std::invalid_argument(
          "local ownership specialization identity mismatch");
  }

  const TypedLocalOwnershipRequirementV1* find_local_ownership_requirement(
      std::uint32_t arena_index, est::ExprId expr_id,
      const std::string& context) const {
    const TypedLocalOwnershipRequirementV1* result = nullptr;
    for (const auto& req : required_local_ownership_occurrences) {
      if (req.est_arena_index == arena_index &&
          req.expression_occurrence_id == expr_id &&
          req.specialization_context == context) {
        if (result)
          throw std::invalid_argument(
              "ambiguous required local ownership occurrence");
        result = &req;
      }
    }
    if (result) {
      validate_local_ownership_requirement_specialization(*result);
      (void)find_local_ownership_fact(*result);
    }
    return result;
  }

  const TypedLocalOwnershipRequirementV1* find_local_ownership_requirement(
      std::uint32_t arena_index, est::ExprId expr_id,
      std::uint32_t statement_occurrence_id,
      const std::string& specialization_context,
      const std::string& specialization_identity,
      const std::string& definition_identity) const {
    const TypedLocalOwnershipRequirementV1* result = nullptr;
    for (const auto& req : required_local_ownership_occurrences) {
      if (req.est_arena_index != arena_index ||
          req.expression_occurrence_id != expr_id ||
          req.statement_occurrence_id != statement_occurrence_id ||
          req.specialization_context != specialization_context ||
          req.specialization_identity != specialization_identity ||
          req.definition_identity != definition_identity)
        continue;
      if (result)
        throw std::invalid_argument(
            "duplicate required local ownership identity");
      result = &req;
    }
    if (result) {
      validate_local_ownership_requirement_specialization(*result);
      (void)find_local_ownership_fact(*result);
    }
    return result;
  }

  // Resolve one exact checker-issued requirement to its fact. No first-match
  // behavior: omitted, duplicate, altered, unavailable or contradictory
  // records are all missing authority and therefore errors.
  const TypedLocalOwnershipFactV1& find_local_ownership_fact(
      const TypedLocalOwnershipRequirementV1& req) const {
    std::size_t matching_requirements = 0;
    for (const auto& candidate : required_local_ownership_occurrences) {
      if (candidate.est_arena_index == req.est_arena_index &&
          candidate.expression_occurrence_id == req.expression_occurrence_id &&
          candidate.statement_occurrence_id == req.statement_occurrence_id &&
          candidate.specialization_context == req.specialization_context &&
          candidate.specialization_identity == req.specialization_identity &&
          candidate.definition_identity == req.definition_identity &&
          candidate.join_definition_identity == req.join_definition_identity &&
          candidate.source_binding_identity == req.source_binding_identity &&
          candidate.access == req.access &&
          candidate.source_kind == req.source_kind &&
          candidate.inference_status == req.inference_status &&
          candidate.unsupported_reason == req.unsupported_reason &&
          candidate.availability == req.availability)
        ++matching_requirements;
    }
    if (matching_requirements != 1)
      throw std::invalid_argument(matching_requirements
          ? "duplicate required local ownership identity"
          : "local ownership requirement was not checker-issued");
    validate_local_ownership_requirement_specialization(req);

    const TypedLocalOwnershipFactV1* result = nullptr;
    for (const auto& fact : local_ownership_facts) {
      if (fact.est_arena_index != req.est_arena_index ||
          fact.expression_occurrence_id != req.expression_occurrence_id ||
          fact.statement_occurrence_id != req.statement_occurrence_id ||
          fact.specialization_context != req.specialization_context ||
          fact.specialization_identity != req.specialization_identity ||
          fact.definition_identity != req.definition_identity ||
          fact.join_definition_identity != req.join_definition_identity ||
          fact.source_binding_identity != req.source_binding_identity)
        continue;
      if (result)
        throw std::invalid_argument("duplicate local ownership fact");
      result = &fact;
    }
    if (!result)
      throw std::invalid_argument("required local ownership fact missing");
    if (result->access != req.access ||
        result->source_kind != req.source_kind ||
        result->inference_status != req.inference_status ||
        result->unsupported_reason != req.unsupported_reason ||
        result->availability != req.availability)
      throw std::invalid_argument("required local ownership fact contradicts requirement");
    if (result->is_intentionally_unsupported()) return *result;
    if (!result->is_authoritative())
      throw std::invalid_argument("required local ownership fact unavailable");
    return *result;
  }

  const TypedTakeFactV1* find_take_fact(
      const TypedTakeRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return left && right && left->structurally_equal(*right);
    };
    auto same_projection = [](const TypedTakeProjectionV1& left,
                              const TypedTakeProjectionV1& right) {
      return left.kind == right.kind &&
          left.expression_occurrence_id == right.expression_occurrence_id &&
          left.selector_occurrence_id == right.selector_occurrence_id &&
          left.semantic_identity == right.semantic_identity &&
          left.ordinal == right.ordinal && left.input_type && right.input_type &&
          left.input_type->structurally_equal(*right.input_type) &&
          left.output_type && right.output_type &&
          left.output_type->structurally_equal(*right.output_type);
    };
    auto same_projection_path = [&](const auto& left, const auto& right) {
      return left.size() == right.size() &&
          std::equal(left.begin(), left.end(), right.begin(), same_projection);
    };
    auto same_requirement = [&](const TypedTakeRequirementV1& left,
                               const TypedTakeRequirementV1& right) {
      return left.est_arena_index == right.est_arena_index &&
          left.take_occurrence_id == right.take_occurrence_id &&
          left.place_occurrence_id == right.place_occurrence_id &&
          left.statement_occurrence_id == right.statement_occurrence_id &&
          left.specialization_identity == right.specialization_identity &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          left.root_binding_identity == right.root_binding_identity &&
          left.root_name == right.root_name &&
          same_type(left.terminal_type, right.terminal_type) &&
          same_type(left.result_type, right.result_type) &&
          same_projection_path(left.projection_path, right.projection_path) &&
          left.replacement == right.replacement &&
          left.required_access == right.required_access &&
          left.transfers_original_value == right.transfers_original_value &&
          left.exclusive_projection_access == right.exclusive_projection_access;
    };
    std::size_t matching_requirement_identities = 0;
    std::size_t matching_requirements = 0;
    for (const auto& candidate : required_take_occurrences) {
      if (candidate.est_arena_index != requirement.est_arena_index ||
          candidate.take_occurrence_id != requirement.take_occurrence_id ||
          candidate.specialization_context !=
              requirement.specialization_context)
        continue;
      ++matching_requirement_identities;
      if (same_requirement(candidate, requirement))
        ++matching_requirements;
    }
    if (matching_requirement_identities != 1 || matching_requirements != 1)
      throw std::invalid_argument(matching_requirement_identities
          ? "duplicate or contradictory required take identity"
          : "take requirement was not checker-issued");
    const auto mapping = local_ownership_specialization_contexts.find(
        requirement.specialization_context);
    if (mapping != local_ownership_specialization_contexts.end()) {
      if (requirement.specialization_identity.empty() ||
          local_ownership_specialization_identity(
              requirement.specialization_context) !=
              requirement.specialization_identity)
        throw std::invalid_argument("take specialization identity mismatch");
    } else if (!requirement.specialization_identity.empty()) {
      throw std::invalid_argument(
          "take specialization identity has no checker mapping");
    }
    const TypedTakeFactV1* result = nullptr;
    std::size_t matching_fact_identities = 0;
    for (const auto& fact : take_facts) {
      if (fact.est_arena_index != requirement.est_arena_index ||
          fact.take_occurrence_id != requirement.take_occurrence_id ||
          fact.specialization_identity !=
              requirement.specialization_identity ||
          fact.specialization_context !=
              requirement.specialization_context)
        continue;
      ++matching_fact_identities;
      result = &fact;
    }
    if (matching_fact_identities != 1 || !result)
      if (matching_fact_identities > 1)
        throw std::invalid_argument("duplicate or contradictory checked take facts");
    if (!result)
      throw std::invalid_argument("required checked take fact missing");
    if (result->place_occurrence_id != requirement.place_occurrence_id ||
        result->statement_occurrence_id !=
            requirement.statement_occurrence_id ||
        result->root_binding_identity != requirement.root_binding_identity ||
        result->est_fact_context != requirement.est_fact_context ||
        result->root_name != requirement.root_name ||
        !same_type(result->terminal_type, requirement.terminal_type) ||
        !same_type(result->result_type, requirement.result_type) ||
        !same_projection_path(result->projection_path,
                              requirement.projection_path) ||
        result->replacement != requirement.replacement ||
        result->required_access != requirement.required_access ||
        result->transfers_original_value !=
            requirement.transfers_original_value ||
        result->exclusive_projection_access !=
            requirement.exclusive_projection_access)
      throw std::invalid_argument("required checked take fact contradicts requirement");
    if (!result->is_authoritative())
      throw std::invalid_argument("required checked take fact unavailable");
    return result;
  }

  const TypedTakeRequirementV1& find_take_requirement(
      std::uint32_t arena_index, est::ExprId take_occurrence,
      const std::string& specialization_context) const {
    const TypedTakeRequirementV1* result = nullptr;
    std::size_t matches = 0;
    for (const auto& requirement : required_take_occurrences) {
      if (requirement.est_arena_index != arena_index ||
          requirement.take_occurrence_id != take_occurrence ||
          requirement.specialization_context != specialization_context)
        continue;
      ++matches;
      result = &requirement;
    }
    if (matches != 1 || !result)
      throw std::invalid_argument(matches
          ? "ambiguous checker take requirement"
          : "checker take requirement missing");
    (void)find_take_fact(*result);
    return *result;
  }

  const TypedTakeRequirementV1& find_take_requirement(
      std::uint32_t arena_index, est::ExprId take_occurrence,
      const std::string& specialization_context,
      const std::string& specialization_identity) const {
    const auto& requirement = find_take_requirement(
        arena_index, take_occurrence, specialization_context);
    if (requirement.specialization_identity != specialization_identity)
      throw std::invalid_argument("checker take requirement specialization mismatch");
    return requirement;
  }

  void require_complete_open_summaries() const {
    if (!open_summaries_complete)
      throw std::invalid_argument("typed semantic open callable summaries unavailable");
  }

  void require_complete_body_hashes() const {
    for (const auto& entry : open_summaries)
      if (!entry.second.checked_body_v1)
        throw std::invalid_argument(
            "checked semantic body hash unavailable for " + entry.first);
  }

  void require_complete_specializations() const {
    if (!unavailable_specializations.empty())
      throw std::invalid_argument("typed semantic closed specialization facts unavailable");
  }

  const TypedClosedTypeV1& closed_type(TypeRef type) {
    if (!type || !type->is_closed())
      throw std::invalid_argument("typed semantic projection requires a closed TypeRef: " +
          (type ? type->to_canonical_string() : std::string("<null>")));
    const std::string key = type->stable_type_key();
    auto found = closed_types.find(key);
    if (found != closed_types.end()) return found->second;
    TypedClosedTypeV1 record;
    record.type = type;
    record.type_key = key;
    if (type->kind == TypeKind::NominalInstance) {
      record.nominal_template_id = type->declaration->id;
      record.nominal_instance_key = type->nominal_instance_key();
      record.ordered_resolved_fields = type->resolved_fields();
      nominal_instances.emplace(*record.nominal_instance_key,
          TypedNominalInstanceV1{*record.nominal_template_id, type,
              *record.nominal_instance_key, record.ordered_resolved_fields});
    }
    for (const auto& argument : type->type_args) closed_type(argument);
    for (const auto& field : type->field_substitutions) closed_type(field.second);
    return closed_types.emplace(key, std::move(record)).first->second;
  }

  const TypedSpecializationV1& specialization(const std::string& key) const {
    auto found = specializations.find(key);
    if (found == specializations.end())
      throw std::invalid_argument("unknown typed semantic specialization key");
    return found->second;
  }
};

TypedSemanticModuleV1 project_typed_semantics_v1(const Program& program);

}  // namespace moss
