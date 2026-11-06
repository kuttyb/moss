#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <cstdint>
#include <set>
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
  enum class Disposition { NotApplicable, Borrowed, Transferred };
  enum class SourceKind { LocalBinding, VectorElement, FreshOwnedValue };
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
  // Lexical binding identity is stable across whole-local rebinding; each
  // assignment has its own definition_identity and may kill the previous one.
  std::string binding_identity;
  std::string killed_definition_identity;
  // Non-empty for a branch definition reaching a checked live join. All
  // predecessor facts for that joined local share this identity.
  std::string join_definition_identity;
  std::string source_binding_identity;
  std::string source_definition_identity;
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
  // Explicit whole-local value flow. A missing row is never interpreted as a
  // borrow; Borrowed and Transferred are checker decisions for LocalBinding.
  Disposition disposition{Disposition::NotApplicable};
  InferenceStatus inference_status{InferenceStatus::Inferred};
  UnsupportedReason unsupported_reason{UnsupportedReason::None};
  Availability availability{Availability::MissingEstOccurrence};
  TypeRef value_type;
  TypedSourceProvenanceV1 provenance;

  // Consumer gate: an inferred record is authoritative only when every
  // identity needed to locate its checker decision is present. Unsupported
  // inference and incomplete/contradictory records are never authority.
  bool is_authoritative() const noexcept {
    const bool disposition_matches_source =
        source_kind == SourceKind::LocalBinding
            ? ((disposition == Disposition::Borrowed &&
                (access == Effect::Read || access == Effect::Write)) ||
               (disposition == Disposition::Transferred &&
                access == Effect::Consume))
            : disposition == Disposition::NotApplicable;
    if (inference_status != InferenceStatus::Inferred ||
        availability != Availability::Present ||
        definition_identity.empty() || specialization_context.empty() ||
        statement_occurrence_id == UINT32_MAX ||
        est_arena_index == UINT32_MAX || expression_occurrence_id == UINT32_MAX)
      return false;
    switch (source_kind) {
      case SourceKind::LocalBinding:
        return disposition_matches_source && !binding_identity.empty() &&
            !source_binding_identity.empty() && !source_definition_identity.empty();
      case SourceKind::VectorElement:
        return disposition == Disposition::NotApplicable &&
            !source_binding_identity.empty();
      case SourceKind::FreshOwnedValue:
        return disposition == Disposition::NotApplicable &&
            source_binding_identity.empty() && !binding_identity.empty() &&
            !killed_definition_identity.empty() &&
            killed_definition_identity != definition_identity &&
            join_definition_identity.empty() && access == Effect::Read &&
            value_type && value_type->is_closed();
    }
    return false;
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
  using Disposition = TypedLocalOwnershipFactV1::Disposition;
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
  std::string binding_identity;
  std::string killed_definition_identity;
  std::string join_definition_identity;
  std::string source_binding_identity;
  TypeRef value_type;
  std::string source_definition_identity;
  Effect access{Effect::Read};
  SourceKind source_kind{SourceKind::LocalBinding};
  Disposition disposition{Disposition::NotApplicable};
  InferenceStatus inference_status{InferenceStatus::Inferred};
  UnsupportedReason unsupported_reason{UnsupportedReason::None};
  Availability availability{Availability::MissingEstOccurrence};
};

// Exact checker authorization for transferring an owned local into a longer-
// lived local binding. This narrowly represents a whole-local move at the
// source definition's final use; it grants no copy or collection-element
// consume authority.
struct TypedLocalTransferFactV1 {
  enum class Action { TransferOwnedValue };
  enum class Availability {
    Present, MissingRhsOccurrence, MissingStatementOccurrence,
    MissingSourceDefinition, MissingTargetDefinition, SourceBorrowed,
    SourceStillLive, ScopeDoesNotOutliveTarget, TypeMismatch, Contradictory
  };
  Availability availability{Availability::MissingRhsOccurrence};
  Action action{Action::TransferOwnedValue};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId rhs_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan rhs_occurrence_span;
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string source_binding_identity;
  std::string source_definition_identity;
  std::string target_binding_identity;
  std::string target_definition_identity;
  std::string killed_target_definition_identity;
  std::uint32_t source_scope_id{UINT32_MAX};
  std::uint32_t target_scope_id{UINT32_MAX};
  bool source_scope_nested_under_target{false};
  std::uint32_t source_last_use_statement_occurrence_id{UINT32_MAX};
  bool source_is_owned{false};
  bool source_dead_after_assignment{false};
  TypeRef source_type;
  TypeRef target_type;
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
        action == Action::TransferOwnedValue &&
        est_arena_index != UINT32_MAX &&
        rhs_expression_occurrence_id != UINT32_MAX &&
        rhs_occurrence_span.source_end > rhs_occurrence_span.source_begin &&
        statement_occurrence_id != UINT32_MAX &&
        !source_binding_identity.empty() &&
        !source_definition_identity.empty() &&
        !target_binding_identity.empty() &&
        !target_definition_identity.empty() &&
        !killed_target_definition_identity.empty() &&
        source_scope_id != UINT32_MAX && target_scope_id != UINT32_MAX &&
        source_scope_id != target_scope_id &&
        source_scope_nested_under_target &&
        source_last_use_statement_occurrence_id == statement_occurrence_id &&
        source_is_owned && source_dead_after_assignment &&
        source_type && source_type->is_closed() && target_type &&
        target_type->is_closed() &&
        source_type->structurally_equal(*target_type) &&
        !specialization_context.empty() && !est_fact_context.empty() &&
        !provenance.source_file.empty() && provenance.line > 0 &&
        !provenance.enclosing_callable_id.empty();
  }
};

struct TypedLocalTransferRequirementV1 : TypedLocalTransferFactV1 {};

// Checker-owned per-definition observations on the shared control-flow edges
// of a while/for loop. The loop expression and definition EST occurrence make
// each backedge/exit result addressable without reconstructing source text.
struct TypedLoopOwnershipFactV1 {
  enum class EdgeKind { Entry, Backedge, ZeroIterationExit, ConvergedExit };
  enum class ValueState { Available, Consumed, MaybeAvailable, Unavailable };
  enum class Availability {
    Present, MissingLoopOccurrence, MissingEdge, MissingDefinition,
    MissingEstOccurrence, ContradictoryEffect
  };
  std::uint32_t loop_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t target_statement_occurrence_id{UINT32_MAX};
  std::uint32_t definition_statement_occurrence_id{UINT32_MAX};
  std::uint32_t loop_est_arena_index{UINT32_MAX};
  est::ExprId loop_expression_occurrence_id{UINT32_MAX};
  std::uint32_t definition_est_arena_index{UINT32_MAX};
  est::ExprId definition_expression_occurrence_id{UINT32_MAX};
  std::string definition_identity;
  std::string binding_identity;
  std::vector<std::string> killed_definition_identities;
  std::string source_binding_identity;
  std::string specialization_context;
  std::string specialization_identity;
  EdgeKind edge_kind{EdgeKind::Entry};
  Effect converged_access{Effect::Read};
  ValueState value_state{ValueState::Unavailable};
  bool reachable{false};
  Availability availability{Availability::MissingEdge};
  TypedSourceProvenanceV1 provenance;
};

struct TypedLoopOwnershipRequirementV1 {
  using EdgeKind = TypedLoopOwnershipFactV1::EdgeKind;
  using ValueState = TypedLoopOwnershipFactV1::ValueState;
  using Availability = TypedLoopOwnershipFactV1::Availability;
  std::uint32_t loop_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t target_statement_occurrence_id{UINT32_MAX};
  std::uint32_t definition_statement_occurrence_id{UINT32_MAX};
  std::uint32_t loop_est_arena_index{UINT32_MAX};
  est::ExprId loop_expression_occurrence_id{UINT32_MAX};
  std::uint32_t definition_est_arena_index{UINT32_MAX};
  est::ExprId definition_expression_occurrence_id{UINT32_MAX};
  std::string definition_identity;
  std::string binding_identity;
  std::vector<std::string> killed_definition_identities;
  std::string source_binding_identity;
  std::string specialization_context;
  std::string specialization_identity;
  EdgeKind edge_kind{EdgeKind::Entry};
  Effect converged_access{Effect::Read};
  ValueState value_state{ValueState::Unavailable};
  Availability availability{Availability::MissingEdge};
};

// Exact checker snapshot for a typed exceptional successor. State is sampled
// by ownership checking at the operation that may raise; consumers must not
// reconstruct it from pre-try state or assume rollback.
struct TypedExceptionalOwnershipFactV1 {
  enum class Availability { Present, MissingSource, MissingRecovery,
                            MissingOccurrence, MissingSummary,
                            ContradictoryAlternatives };
  struct BindingState {
    std::string name;
    std::string binding_identity;
    std::string reaching_definition_identity;
    std::string type_identity;
    bool consumed{false};
    std::vector<std::string> borrow_source_binding_identities;
    std::vector<std::string> borrow_storage_paths;
  };
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t recovery_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_est_arena_index{UINT32_MAX};
  est::ExprId source_expression_occurrence_id{UINT32_MAX};
  std::uint8_t source_est_slot{UINT8_MAX};
  std::uint32_t source_est_argument_index{UINT32_MAX};
  std::string raised_variant_identity;
  std::vector<std::string> source_raise_alternatives;
  std::vector<std::string> recovery_alternatives;
  std::string summary_provenance;
  std::string specialization_context;
  std::string specialization_identity;
  std::vector<BindingState> state_at_raise;
  std::vector<std::string> consumed_binding_identities;
  std::vector<std::string> mutated_storage_paths;
  Availability availability{Availability::MissingOccurrence};
};

struct TypedExceptionalOwnershipRequirementV1 {
  using Availability = TypedExceptionalOwnershipFactV1::Availability;
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t recovery_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_est_arena_index{UINT32_MAX};
  est::ExprId source_expression_occurrence_id{UINT32_MAX};
  std::uint8_t source_est_slot{UINT8_MAX};
  std::uint32_t source_est_argument_index{UINT32_MAX};
  std::string raised_variant_identity;
  std::vector<std::string> source_raise_alternatives;
  std::vector<std::string> recovery_alternatives;
  std::string specialization_context;
  std::string specialization_identity;
  Availability availability{Availability::MissingOccurrence};
};

// Exact permission to transfer the selected owned error carrier through a
// bare `raise` in one recovery arm. This authorizes moving that carrier once;
// it never authorizes duplicating it.
enum class TypedBareRaiseTransferModeV1 : std::uint8_t {
  TransferSelectedCarrier
};

struct TypedBareRaiseTransferFactV1 {
  enum class Availability {
    Present, MissingRaiseOccurrence, MissingRecoveryOccurrence,
    MissingSourceEdge, MissingCarrierType, Contradictory
  };
  std::uint32_t bare_raise_statement_occurrence_id{UINT32_MAX};
  std::uint32_t recovery_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_est_arena_index{UINT32_MAX};
  est::ExprId source_expression_occurrence_id{UINT32_MAX};
  std::uint8_t source_est_slot{UINT8_MAX};
  std::uint32_t source_est_argument_index{UINT32_MAX};
  std::string raised_variant_identity;
  std::vector<std::string> source_raise_alternatives;
  std::vector<std::string> recovery_alternatives;
  std::string selected_carrier_type_identity;
  std::string specialization_context;
  std::string specialization_identity;
  TypedBareRaiseTransferModeV1 mode{
      TypedBareRaiseTransferModeV1::TransferSelectedCarrier};
  bool transfers_original_carrier{false};
  bool permits_copy{false};
  Availability availability{Availability::MissingRaiseOccurrence};

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
        bare_raise_statement_occurrence_id != UINT32_MAX &&
        recovery_statement_occurrence_id != UINT32_MAX &&
        source_statement_occurrence_id != UINT32_MAX &&
        source_est_arena_index != UINT32_MAX &&
        source_expression_occurrence_id != UINT32_MAX &&
        source_est_slot != UINT8_MAX &&
        source_est_argument_index != UINT32_MAX &&
        !raised_variant_identity.empty() &&
        !selected_carrier_type_identity.empty() &&
        mode == TypedBareRaiseTransferModeV1::TransferSelectedCarrier &&
        transfers_original_carrier && !permits_copy;
  }
};

struct TypedBareRaiseTransferRequirementV1 {
  using Availability = TypedBareRaiseTransferFactV1::Availability;
  std::uint32_t bare_raise_statement_occurrence_id{UINT32_MAX};
  std::uint32_t recovery_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_statement_occurrence_id{UINT32_MAX};
  std::uint32_t source_est_arena_index{UINT32_MAX};
  est::ExprId source_expression_occurrence_id{UINT32_MAX};
  std::uint8_t source_est_slot{UINT8_MAX};
  std::uint32_t source_est_argument_index{UINT32_MAX};
  std::string raised_variant_identity;
  std::vector<std::string> source_raise_alternatives;
  std::vector<std::string> recovery_alternatives;
  std::string selected_carrier_type_identity;
  std::string specialization_context;
  std::string specialization_identity;
  TypedBareRaiseTransferModeV1 mode{
      TypedBareRaiseTransferModeV1::TransferSelectedCarrier};
  bool transfers_original_carrier{false};
  bool permits_copy{false};
  Availability availability{Availability::MissingRaiseOccurrence};

  bool is_complete() const noexcept {
    return bare_raise_statement_occurrence_id != UINT32_MAX &&
        recovery_statement_occurrence_id != UINT32_MAX &&
        source_statement_occurrence_id != UINT32_MAX &&
        source_est_arena_index != UINT32_MAX &&
        source_expression_occurrence_id != UINT32_MAX &&
        source_est_slot != UINT8_MAX &&
        source_est_argument_index != UINT32_MAX &&
        !raised_variant_identity.empty() &&
        !selected_carrier_type_identity.empty() &&
        mode == TypedBareRaiseTransferModeV1::TransferSelectedCarrier &&
        transfers_original_carrier && !permits_copy &&
        availability == Availability::Present;
  }
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

struct TypedCopyFactV1 {
  enum class Availability {
    Present, MissingCopyOccurrence, MissingSourceOccurrence,
    MissingStatementOccurrence, MissingType, IneligibleType,
    Contradictory
  };
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId copy_occurrence_id{UINT32_MAX};
  est::ExprId source_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;
  est::SourceSpan copy_occurrence_span;
  est::SourceSpan source_occurrence_span;
  TypeRef source_type;
  TypeRef result_type;
  Effect required_source_access{Effect::Read};
  bool creates_independent_owner{true};
  Availability availability{Availability::MissingCopyOccurrence};

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
        est_arena_index != UINT32_MAX &&
        copy_occurrence_id != UINT32_MAX &&
        source_occurrence_id != UINT32_MAX &&
        statement_occurrence_id != UINT32_MAX &&
        !specialization_context.empty() &&
        !est_fact_context.empty() &&
        !provenance.source_file.empty() && provenance.line > 0 &&
        !provenance.defining_module_id.empty() &&
        !provenance.enclosing_callable_id.empty() &&
        copy_occurrence_span.source_end > copy_occurrence_span.source_begin &&
        source_occurrence_span.source_end > source_occurrence_span.source_begin &&
        source_type && result_type &&
        source_type->structurally_equal(*result_type) &&
        required_source_access == Effect::Read &&
        creates_independent_owner;
  }
};

struct TypedCopyRequirementV1 {
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId copy_occurrence_id{UINT32_MAX};
  est::ExprId source_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;
  est::SourceSpan copy_occurrence_span;
  est::SourceSpan source_occurrence_span;
  TypeRef source_type;
  TypeRef result_type;
  Effect required_source_access{Effect::Read};
  bool creates_independent_owner{true};
};

// Exact checker proof that one fresh-empty collection operation cannot return
// normally. This is not a generic default and does not authorize copying. The
// Never result and Never collection payload are valid only with this fact.
struct TypedCollectionOutcomeFactV1 {
  enum class Operation { QueuePop, MapRemove };
  enum class Outcome { AlwaysRaises };
  enum class Availability {
    Present, MissingOperationOccurrence, MissingStatementOccurrence,
    MissingReceiverIdentity, MissingArgumentOccurrence, MissingType,
    Contradictory
  };
  Operation operation{Operation::QueuePop};
  Outcome outcome{Outcome::AlwaysRaises};
  Availability availability{Availability::MissingOperationOccurrence};
  std::uint32_t operation_est_arena_index{UINT32_MAX};
  est::ExprId operation_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan operation_occurrence_span;
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;
  std::string receiver_binding_identity;
  std::string receiver_definition_identity;
  std::string fresh_empty_definition_identity;
  TypeRef receiver_type;
  est::ExprId argument_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan argument_occurrence_span;
  TypeRef argument_type;
  TypeRef result_type;
  std::string error_type_identity;
  std::string error_variant_identity;

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        outcome != Outcome::AlwaysRaises ||
        operation_est_arena_index == UINT32_MAX ||
        operation_expression_occurrence_id == UINT32_MAX ||
        statement_occurrence_id == UINT32_MAX ||
        specialization_context.empty() || est_fact_context.empty() ||
        provenance.source_file.empty() || provenance.line <= 0 ||
        provenance.defining_module_id.empty() ||
        provenance.enclosing_callable_id.empty() ||
        operation_occurrence_span.source_end <=
            operation_occurrence_span.source_begin ||
        receiver_binding_identity.empty() ||
        receiver_definition_identity.empty() ||
        fresh_empty_definition_identity.empty() ||
        receiver_definition_identity != fresh_empty_definition_identity ||
        !receiver_type || !receiver_type->is_closed() ||
        !result_type || result_type->kind != TypeKind::Never ||
        error_type_identity != "std__CollectionError")
      return false;
    if (operation == Operation::QueuePop)
      return error_variant_identity == "Empty" &&
          receiver_type->kind == TypeKind::Builtin &&
          receiver_type->name == "queue" &&
          receiver_type->type_args.size() == 1 &&
          receiver_type->type_args.front()->kind == TypeKind::Never &&
          argument_expression_occurrence_id == UINT32_MAX &&
          !argument_type;
    if (operation != Operation::MapRemove) return false;
    return error_variant_identity == "MissingKey" &&
        receiver_type->kind == TypeKind::Builtin &&
        receiver_type->name == "map" && receiver_type->type_args.size() == 2 &&
        receiver_type->type_args[1]->kind == TypeKind::Never &&
        argument_expression_occurrence_id != UINT32_MAX &&
        argument_occurrence_span.source_end >
            argument_occurrence_span.source_begin &&
        argument_type && argument_type->is_closed() &&
        receiver_type->type_args.front()->structurally_equal(*argument_type);
  }
};

struct TypedCollectionOutcomeRequirementV1 : TypedCollectionOutcomeFactV1 {};

// A conditional Map miss is an exceptional edge attached to an exact checked
// operation. It authorizes neither ownership transfer nor materialization.
struct TypedConditionalCollectionErrorFactV1 {
  enum class Operation { MapIndexRead, MapIndexWriteThrough, MapGet, MapRemove };
  enum class Availability { Present, MissingOperation, MissingStatement,
                            MissingReceiver, MissingKey, MissingType,
                            MissingExceptionalEdge, Contradictory };
  Operation operation{Operation::MapIndexRead};
  Availability availability{Availability::MissingOperation};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId operation_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan operation_occurrence_span;
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  est::ExprId receiver_expression_occurrence_id{UINT32_MAX};
  est::ExprId key_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan receiver_occurrence_span;
  est::SourceSpan key_occurrence_span;
  std::string receiver_binding_identity;
  std::string receiver_definition_identity;
  TypeRef receiver_type;
  TypeRef key_type;
  TypeRef result_type;
  TypeRef write_value_type;
  std::string error_type_identity{"std__CollectionError"};
  std::string error_variant_identity{"MissingKey"};
  bool exceptional_edge_authorized{false};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        est_arena_index == UINT32_MAX ||
        operation_expression_occurrence_id == UINT32_MAX ||
        statement_occurrence_id == UINT32_MAX ||
        receiver_expression_occurrence_id == UINT32_MAX ||
        key_expression_occurrence_id == UINT32_MAX ||
        receiver_binding_identity.empty() || receiver_definition_identity.empty() ||
        !receiver_type || !receiver_type->is_closed() ||
        !key_type || !key_type->is_closed() || !result_type ||
        !result_type->is_closed() || error_type_identity != "std__CollectionError" ||
        error_variant_identity != "MissingKey" || !exceptional_edge_authorized ||
        specialization_context.empty() || est_fact_context.empty() ||
        provenance.source_file.empty() || provenance.line <= 0 ||
        provenance.defining_module_id.empty() ||
        provenance.enclosing_callable_id.empty() ||
        receiver_type->kind != TypeKind::Builtin || receiver_type->name != "map" ||
        receiver_type->type_args.size() != 2 ||
        !receiver_type->type_args.front() ||
        !receiver_type->type_args.front()->structurally_equal(*key_type))
      return false;
    if (operation == Operation::MapIndexWriteThrough)
      return write_value_type && write_value_type->is_closed();
    return !write_value_type;
  }
};
struct TypedConditionalCollectionErrorRequirementV1
    : TypedConditionalCollectionErrorFactV1 {};

enum class TypedReturnValueActionV1 {
  TransferOwnedValue, ReadCopyPrimitive
};
struct TypedReturnValueFactV1 {
  enum class Availability { Present, MissingReturnOccurrence,
                            MissingExpression, MissingType, MissingBinding,
                            Contradictory };
  Availability availability{Availability::MissingReturnOccurrence};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId expression_occurrence_id{UINT32_MAX};
  est::SourceSpan expression_occurrence_span;
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;
  TypeRef expression_type;
  std::string source_binding_identity;
  std::string source_definition_identity;
  Effect required_access{Effect::Consume};
  TypedReturnValueActionV1 action{TypedReturnValueActionV1::TransferOwnedValue};
  bool checker_proved_owned_rvalue{false};

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        statement_occurrence_id == UINT32_MAX ||
        est_arena_index == UINT32_MAX || expression_occurrence_id == UINT32_MAX ||
        expression_occurrence_span.source_end <=
            expression_occurrence_span.source_begin ||
        specialization_context.empty() || est_fact_context.empty() ||
        provenance.source_file.empty() || provenance.line <= 0 ||
        provenance.defining_module_id.empty() ||
        provenance.enclosing_callable_id.empty() || !expression_type ||
        !expression_type->is_closed()) return false;
    if (action == TypedReturnValueActionV1::ReadCopyPrimitive)
      return required_access == Effect::Read && source_binding_identity.empty() &&
          source_definition_identity.empty() && !checker_proved_owned_rvalue &&
          expression_type->kind == TypeKind::Primitive &&
          (expression_type->name == "int" || expression_type->name == "float" ||
           expression_type->name == "bool" || expression_type->name == "char");
    if (action != TypedReturnValueActionV1::TransferOwnedValue ||
        required_access != Effect::Consume) return false;
    const bool binding = !source_binding_identity.empty() &&
        !source_definition_identity.empty();
    return binding != checker_proved_owned_rvalue;
  }
};
struct TypedReturnValueRequirementV1 : TypedReturnValueFactV1 {};

// Stable source-free transport record for a checker ReturnValue fact.
// Process-local arena, ExprId, and statement occurrence IDs are deliberately
// absent. A .mossi reader remaps the stable body/statement/slot identity to
// the newly parsed checked EST handle before creating an in-memory fact and
// requirement.
struct TypedReturnValueArtifactFactV1 {
  enum class ValueSlot { ReturnValue };
  static constexpr std::uint32_t kVersion = 1;
  std::uint32_t version{kVersion};
  // This is the stable checker callable identity `fn:<qualified-name>` used
  // in ReturnValue provenance, not a process-local callable pointer.
  std::string callable_identity;
  // Hash of this callable's canonical execution-body records in the
  // containing source-free artifact.
  std::string execution_body_hash;
  // Deterministic preorder ordinal across the callable's serialized body.
  std::uint32_t statement_preorder_ordinal{UINT32_MAX};
  ValueSlot value_slot{ValueSlot::ReturnValue};
  std::uint32_t value_argument_index{0};
  int source_line{0};
  // EST byte offsets are relative to the original checked expression source;
  // origin_id is intentionally omitted because it is process-local.
  std::uint32_t expression_span_line{0};
  std::uint32_t expression_span_column{0};
  std::uint32_t expression_source_begin{0};
  std::uint32_t expression_source_end{0};
  std::uint64_t expression_source_fingerprint{0};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypeRef expression_type;
  std::string source_binding_identity;
  std::string source_definition_identity;
  Effect required_access{Effect::Consume};
  TypedReturnValueActionV1 action{
      TypedReturnValueActionV1::TransferOwnedValue};
  bool checker_proved_owned_rvalue{false};
  TypedSourceProvenanceV1 provenance;

  bool is_well_formed() const noexcept {
    if (version != kVersion || callable_identity.empty() ||
        execution_body_hash.empty() ||
        statement_preorder_ordinal == UINT32_MAX ||
        value_slot != ValueSlot::ReturnValue || value_argument_index != 0 ||
        source_line <= 0 || source_line != provenance.line ||
        expression_span_line == 0 || expression_span_column == 0 ||
        expression_source_end <= expression_source_begin ||
        expression_source_fingerprint == 0 ||
        specialization_context.empty() || est_fact_context.empty() ||
        provenance.source_file.empty() ||
        provenance.defining_module_id.empty() ||
        provenance.enclosing_callable_id != callable_identity || !expression_type ||
        !expression_type->is_closed())
      return false;
    if (action == TypedReturnValueActionV1::ReadCopyPrimitive)
      return required_access == Effect::Read &&
          source_binding_identity.empty() && source_definition_identity.empty() &&
          !checker_proved_owned_rvalue &&
          expression_type->kind == TypeKind::Primitive &&
          (expression_type->name == "int" || expression_type->name == "float" ||
           expression_type->name == "bool" || expression_type->name == "char");
    if (action != TypedReturnValueActionV1::TransferOwnedValue ||
        required_access != Effect::Consume) return false;
    const bool binding = !source_binding_identity.empty() &&
        !source_definition_identity.empty();
    return binding != checker_proved_owned_rvalue;
  }
};

enum class TypedCollectionViewOperationV1 { MapKeys, MapValues };
struct TypedCollectionViewFactV1 {
  enum class Availability { Present, MissingOperation, MissingStatement,
                            MissingSourceBinding, MissingLastUse, Escapes,
                            Contradictory };
  Availability availability{Availability::MissingOperation};
  TypedCollectionViewOperationV1 operation{TypedCollectionViewOperationV1::MapKeys};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId operation_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan operation_occurrence_span;
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::string receiver_binding_identity;
  std::string receiver_definition_identity;
  std::string view_binding_identity;
  std::string view_definition_identity;
  std::uint32_t last_use_statement_occurrence_id{UINT32_MAX};
  std::vector<std::uint32_t> bounded_read_use_statement_occurrences;
  bool direct_temporary{false};
  TypeRef receiver_type;
  TypeRef result_type;
  Effect required_access{Effect::Read};
  bool bounded_non_escaping_lifetime{false};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        est_arena_index == UINT32_MAX ||
        operation_expression_occurrence_id == UINT32_MAX ||
        statement_occurrence_id == UINT32_MAX ||
        receiver_binding_identity.empty() || receiver_definition_identity.empty() ||
        last_use_statement_occurrence_id == UINT32_MAX ||
        !receiver_type || !receiver_type->is_closed() ||
        receiver_type->kind != TypeKind::Builtin || receiver_type->name != "map" ||
        receiver_type->type_args.size() != 2 || !result_type ||
        !result_type->is_closed() || result_type->kind != TypeKind::Builtin ||
        result_type->name != "vector" || result_type->type_args.size() != 1 ||
        required_access != Effect::Read || !bounded_non_escaping_lifetime ||
        specialization_context.empty() || est_fact_context.empty() ||
        provenance.source_file.empty() || provenance.line <= 0 ||
        provenance.defining_module_id.empty() ||
        provenance.enclosing_callable_id.empty()) return false;
    const auto& element = operation == TypedCollectionViewOperationV1::MapKeys
        ? receiver_type->type_args.front() : receiver_type->type_args.back();
    return element && result_type->type_args.front() &&
        element->structurally_equal(*result_type->type_args.front()) &&
        (direct_temporary
             ? view_binding_identity.empty() && view_definition_identity.empty() &&
                   last_use_statement_occurrence_id == statement_occurrence_id &&
                   bounded_read_use_statement_occurrences.size() == 1 &&
                   bounded_read_use_statement_occurrences.front() == statement_occurrence_id
             : !view_binding_identity.empty() && !view_definition_identity.empty() &&
                   !bounded_read_use_statement_occurrences.empty());
  }
};
struct TypedCollectionViewRequirementV1 : TypedCollectionViewFactV1 {};

struct TypedProjectedCollectionReadFactV1 {
  enum class Availability { Present, MissingProjection, MissingQueryUse,
                            MissingBinding, Escapes, Contradictory };
  Availability availability{Availability::MissingProjection};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId map_index_expression_occurrence_id{UINT32_MAX};
  est::ExprId member_projection_expression_occurrence_id{UINT32_MAX};
  est::ExprId query_source_expression_occurrence_id{UINT32_MAX};
  est::SourceSpan projection_occurrence_span;
  std::uint32_t projection_statement_occurrence_id{UINT32_MAX};
  std::uint32_t query_statement_occurrence_id{UINT32_MAX};
  std::string receiver_binding_identity;
  std::string receiver_definition_identity;
  TypeRef receiver_type;
  TypeRef key_type;
  TypeRef indexed_value_type;
  TypeRef projection_base_type;
  TypeRef projected_collection_type;
  Effect required_access{Effect::Read};
  bool temporary_non_escaping_query_borrow{false};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
        est_arena_index != UINT32_MAX &&
        map_index_expression_occurrence_id != UINT32_MAX &&
        member_projection_expression_occurrence_id != UINT32_MAX &&
        query_source_expression_occurrence_id != UINT32_MAX &&
        projection_statement_occurrence_id != UINT32_MAX &&
        query_statement_occurrence_id != UINT32_MAX &&
        !receiver_binding_identity.empty() && !receiver_definition_identity.empty() &&
        receiver_type && receiver_type->is_closed() &&
        receiver_type->kind == TypeKind::Builtin && receiver_type->name == "map" &&
        receiver_type->type_args.size() == 2 &&
        receiver_type->type_args.front() && receiver_type->type_args.back() &&
        key_type && key_type->is_closed() &&
        receiver_type->type_args.front()->structurally_equal(*key_type) &&
        indexed_value_type && indexed_value_type->is_closed() &&
        receiver_type->type_args.back()->structurally_equal(*indexed_value_type) &&
        projection_base_type && projection_base_type->is_closed() &&
        indexed_value_type->structurally_equal(*projection_base_type) &&
        projected_collection_type && projected_collection_type->is_closed() &&
        projected_collection_type->kind == TypeKind::Builtin &&
        (projected_collection_type->name == "vector" ||
         projected_collection_type->name == "map" ||
         projected_collection_type->name == "queue") &&
        projected_collection_type->type_args.size() ==
            (projected_collection_type->name == "map" ? 2u : 1u) &&
        std::all_of(projected_collection_type->type_args.begin(),
                    projected_collection_type->type_args.end(),
                    [](const TypeRef& argument) {
                      return argument && argument->is_closed();
                    }) &&
        required_access == Effect::Read && temporary_non_escaping_query_borrow &&
        !specialization_context.empty() && !est_fact_context.empty() &&
        !provenance.source_file.empty() && provenance.line > 0 &&
        !provenance.enclosing_callable_id.empty();
  }
};
struct TypedProjectedCollectionReadRequirementV1
    : TypedProjectedCollectionReadFactV1 {};

// Checker proof that one direct Vector[index].member projection is passed to
// an exactly resolved READ parameter for the duration of that call. This does
// not authorize a Moss-value copy or an escaping borrow.
struct TypedProjectedCallArgumentReadFactV1 {
  enum class TargetResolution { Resolved, Unresolved, Ambiguous, Contradictory };
  enum class Availability {
    Present, MissingCallOccurrence, MissingProjection, MissingReceiverBinding,
    UnsupportedTarget, UnsupportedParameterAccess, UnsupportedReceiverBinding,
    UnsupportedSourceModule, Escaping, Contradictory
  };
  Availability availability{Availability::MissingProjection};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId call_argument_expression_occurrence_id{UINT32_MAX};
  est::ExprId receiver_expression_occurrence_id{UINT32_MAX};
  est::ExprId collection_index_expression_occurrence_id{UINT32_MAX};
  // Exact key/index operand child of `collection_index_expression_occurrence_id`.
  est::ExprId index_expression_occurrence_id{UINT32_MAX};
  est::ExprId member_projection_expression_occurrence_id{UINT32_MAX};
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t argument_index{UINT32_MAX};
  std::uint32_t target_parameter_index{UINT32_MAX};
  est::SourceSpan argument_occurrence_span;
  est::SourceSpan projection_occurrence_span;
  std::string target_callable_identity;
  std::string target_specialization_identity;
  TargetResolution target_resolution{TargetResolution::Unresolved};
  Effect target_parameter_access{Effect::Read};
  std::string receiver_binding_identity;
  std::string receiver_definition_identity;
  std::string member_name;
  TypeRef receiver_type;
  TypeRef index_type;
  TypeRef indexed_element_type;
  TypeRef projection_base_type;
  TypeRef projected_member_type;
  Effect required_access{Effect::Read};
  bool temporary_nonescaping_borrow{false};
  bool permits_copy{false};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypedSourceProvenanceV1 provenance;

  bool has_exact_occurrence() const noexcept {
    return est_arena_index != UINT32_MAX &&
        call_argument_expression_occurrence_id != UINT32_MAX &&
        receiver_expression_occurrence_id != UINT32_MAX &&
        collection_index_expression_occurrence_id != UINT32_MAX &&
        index_expression_occurrence_id != UINT32_MAX &&
        member_projection_expression_occurrence_id != UINT32_MAX &&
        statement_occurrence_id != UINT32_MAX &&
        argument_index != UINT32_MAX &&
        !specialization_context.empty() && !est_fact_context.empty() &&
        argument_occurrence_span.line > 0 &&
        argument_occurrence_span.source_end >=
            argument_occurrence_span.source_begin &&
        projection_occurrence_span.line > 0 &&
        projection_occurrence_span.source_end >=
            projection_occurrence_span.source_begin &&
        !provenance.source_file.empty() && provenance.line > 0 &&
        !provenance.enclosing_callable_id.empty();
  }

  bool is_authoritative() const noexcept {
    const auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return left && right && left->structurally_equal(*right);
    };
    return availability == Availability::Present && has_exact_occurrence() &&
        !target_callable_identity.empty() &&
        target_resolution == TargetResolution::Resolved &&
        target_parameter_index != UINT32_MAX &&
        target_parameter_access == Effect::Read &&
        receiver_binding_identity.size() > 0 &&
        receiver_definition_identity.size() > 0 && !member_name.empty() &&
        receiver_type && receiver_type->is_closed() &&
        receiver_type->kind == TypeKind::Builtin &&
        receiver_type->name == "vector" && receiver_type->type_args.size() == 1 &&
        index_type && index_type->is_closed() &&
        index_type->kind == TypeKind::Primitive && index_type->name == "int" &&
        indexed_element_type && indexed_element_type->is_closed() &&
        same_type(receiver_type->type_args.front(), indexed_element_type) &&
        same_type(indexed_element_type, projection_base_type) &&
        projected_member_type && projected_member_type->is_closed() &&
        required_access == Effect::Read && temporary_nonescaping_borrow &&
        !permits_copy &&
        argument_occurrence_span.line > 0 &&
        argument_occurrence_span.source_end >=
            argument_occurrence_span.source_begin &&
        projection_occurrence_span.line > 0 &&
        projection_occurrence_span.source_end >=
            projection_occurrence_span.source_begin &&
        !provenance.source_file.empty() && provenance.line > 0 &&
        !provenance.defining_module_id.empty();
  }

  bool is_intentionally_unsupported() const noexcept {
    return has_exact_occurrence() && availability != Availability::Present &&
        (availability == Availability::UnsupportedTarget ||
         availability == Availability::UnsupportedParameterAccess ||
         availability == Availability::UnsupportedReceiverBinding ||
         availability == Availability::UnsupportedSourceModule ||
         availability == Availability::Escaping ||
         availability == Availability::Contradictory);
  }
};
struct TypedProjectedCallArgumentReadRequirementV1
    : TypedProjectedCallArgumentReadFactV1 {};

enum class TypedEchoArgumentModeV1 {
  ExpressionValue, BorrowedVectorElement
};

struct TypedEchoArgumentFactV1 {
  enum class Availability {
    Present, MissingStatementOccurrence, MissingArgumentOccurrence,
    MissingEstFactContext, MissingReceiverType, MissingElementType,
    Contradictory
  };
  std::uint32_t echo_statement_occurrence_id{UINT32_MAX};
  std::uint32_t argument_index{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId argument_expression_occurrence_id{UINT32_MAX};
  est::ExprId indexed_expression_occurrence_id{UINT32_MAX};
  est::ExprId receiver_occurrence_id{UINT32_MAX};
  est::ExprId selector_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypeRef argument_type;
  TypeRef receiver_type;
  TypeRef element_type;
  TypedEchoArgumentModeV1 mode{TypedEchoArgumentModeV1::ExpressionValue};
  Effect required_access{Effect::Read};
  bool display_result_non_escaping{false};
  bool permits_copy{false};
  Availability availability{Availability::MissingArgumentOccurrence};

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        echo_statement_occurrence_id == UINT32_MAX ||
        argument_index == UINT32_MAX || est_arena_index == UINT32_MAX ||
        argument_expression_occurrence_id == UINT32_MAX ||
        required_access != Effect::Read ||
        !display_result_non_escaping || permits_copy)
      return false;
    if (mode == TypedEchoArgumentModeV1::ExpressionValue)
      return indexed_expression_occurrence_id == UINT32_MAX &&
          receiver_occurrence_id == UINT32_MAX &&
          selector_occurrence_id == UINT32_MAX && !receiver_type &&
          !element_type;
    if (mode != TypedEchoArgumentModeV1::BorrowedVectorElement ||
        indexed_expression_occurrence_id == UINT32_MAX ||
        receiver_occurrence_id == UINT32_MAX ||
        selector_occurrence_id == UINT32_MAX || !argument_type ||
        !receiver_type || !element_type ||
        !argument_type->structurally_equal(*element_type) ||
        receiver_type->kind != TypeKind::Builtin ||
        receiver_type->name != "vector" ||
        receiver_type->type_args.size() != 1 ||
        !receiver_type->type_args.front())
      return false;
    return receiver_type->type_args.front()->structurally_equal(*element_type);
  }
};

struct TypedEchoArgumentRequirementV1 {
  std::uint32_t echo_statement_occurrence_id{UINT32_MAX};
  std::uint32_t argument_index{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId argument_expression_occurrence_id{UINT32_MAX};
  est::ExprId indexed_expression_occurrence_id{UINT32_MAX};
  est::ExprId receiver_occurrence_id{UINT32_MAX};
  est::ExprId selector_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string est_fact_context;
  TypeRef argument_type;
  TypeRef receiver_type;
  TypeRef element_type;
  TypedEchoArgumentModeV1 mode{TypedEchoArgumentModeV1::ExpressionValue};
  Effect required_access{Effect::Read};
  bool display_result_non_escaping{false};
  bool permits_copy{false};

  bool is_complete() const noexcept {
    if (echo_statement_occurrence_id == UINT32_MAX ||
        argument_index == UINT32_MAX || est_arena_index == UINT32_MAX ||
        argument_expression_occurrence_id == UINT32_MAX ||
        required_access != Effect::Read ||
        !display_result_non_escaping || permits_copy)
      return false;
    if (mode == TypedEchoArgumentModeV1::ExpressionValue)
      return indexed_expression_occurrence_id == UINT32_MAX &&
          receiver_occurrence_id == UINT32_MAX &&
          selector_occurrence_id == UINT32_MAX && !receiver_type &&
          !element_type;
    return mode == TypedEchoArgumentModeV1::BorrowedVectorElement &&
        indexed_expression_occurrence_id != UINT32_MAX &&
        receiver_occurrence_id != UINT32_MAX &&
        selector_occurrence_id != UINT32_MAX && argument_type &&
        receiver_type && element_type &&
        argument_type->structurally_equal(*element_type) &&
        receiver_type->kind == TypeKind::Builtin &&
        receiver_type->name == "vector" &&
        receiver_type->type_args.size() == 1 &&
        receiver_type->type_args.front() &&
        receiver_type->type_args.front()->structurally_equal(*element_type);
  }
};

// Narrow compiler-internal authority for cloning a statically constructed
// domain routing capability into a synchronous Root-ingress thunk. This does
// not authorize Moss-level copies, payload copies, or domain-handle values.
struct TypedRootIngressDomainCaptureFactV1 {
  enum class Availability { Present, MissingOccurrence, Contradictory };
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId message_expression_occurrence_id{UINT32_MAX};
  est::ExprId receiver_expression_occurrence_id{UINT32_MAX};
  std::uint64_t source_fingerprint{0};
  std::uint64_t origin_id{0};
  std::string specialization_context;
  std::string est_fact_context;
  std::string receiver_binding;
  std::string receiver_definition_identity;
  std::string domain_instance_identity;
  std::string domain_type_identity;
  std::string concrete_graph_identity;
  std::string route_capability_identity;
  std::string selected_call_edge_identity;
  std::string selected_call_edge_source;
  std::string selected_call_edge_target;
  std::string selected_call_edge_invocation_kind;
  std::string selected_call_edge_receiver;
  std::string selected_call_edge_source_file;
  int selected_call_edge_line{0};
  std::uint32_t selected_call_edge_occurrence{UINT32_MAX};
  std::uint32_t selected_call_edge_est_arena_index{UINT32_MAX};
  est::ExprId selected_call_edge_message_expression_occurrence_id{UINT32_MAX};
  est::ExprId selected_call_edge_receiver_expression_occurrence_id{UINT32_MAX};
  std::uint64_t selected_call_edge_source_fingerprint{0};
  std::string selected_handler_identity;
  std::string source_file;
  int source_line{0};
  bool synchronous_root_ingress{false};
  bool compiler_generated_capture_only{false};
  bool permits_moss_value_copy{false};
  Availability availability{Availability::MissingOccurrence};

  bool is_authoritative() const noexcept {
    return availability == Availability::Present &&
        statement_occurrence_id != UINT32_MAX &&
        est_arena_index != UINT32_MAX &&
        message_expression_occurrence_id != UINT32_MAX &&
        receiver_expression_occurrence_id != UINT32_MAX &&
        source_fingerprint != 0 &&
        !specialization_context.empty() && !est_fact_context.empty() &&
        !receiver_binding.empty() && !receiver_definition_identity.empty() &&
        !domain_instance_identity.empty() && !domain_type_identity.empty() &&
        !concrete_graph_identity.empty() &&
        !route_capability_identity.empty() &&
        !selected_call_edge_identity.empty() &&
        selected_call_edge_source == "main" &&
        selected_call_edge_target.rfind("handler:", 0) == 0 &&
        selected_call_edge_invocation_kind == "synchronous_message" &&
        selected_call_edge_receiver == receiver_binding &&
        selected_call_edge_source_file == source_file &&
        selected_call_edge_line == source_line &&
        selected_call_edge_occurrence != UINT32_MAX &&
        selected_call_edge_est_arena_index == est_arena_index &&
        selected_call_edge_message_expression_occurrence_id ==
            message_expression_occurrence_id &&
        selected_call_edge_receiver_expression_occurrence_id ==
            receiver_expression_occurrence_id &&
        selected_call_edge_source_fingerprint == source_fingerprint &&
        !selected_handler_identity.empty() && !source_file.empty() &&
        source_line > 0 && synchronous_root_ingress &&
        compiler_generated_capture_only && !permits_moss_value_copy;
  }
};

using TypedRootIngressDomainCaptureRequirementV1 =
    TypedRootIngressDomainCaptureFactV1;

enum class TypedMatchScrutineeKindV1 { Enum, Option };

struct TypedMatchArmFactV1 {
  std::string case_name;
  std::vector<std::string> payload_binding_names;
  std::vector<std::string> payload_binding_identities;
  std::vector<TypeRef> payload_types;
  Effect payload_access{Effect::Read};
  bool transfers_ownership{false};
};

struct TypedMatchOwnershipFactV1 {
  enum class Availability {
    Present, MissingMatchOccurrence, MissingScrutineeOccurrence,
    MissingStatementOccurrence, Contradictory
  };
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId scrutinee_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string scrutinee_binding_identity;
  std::string scrutinee_name;
  TypeRef scrutinee_type;
  TypedMatchScrutineeKindV1 scrutinee_kind{TypedMatchScrutineeKindV1::Enum};
  bool is_consuming{false};
  std::vector<TypedMatchArmFactV1> arms;
  Availability availability{Availability::MissingMatchOccurrence};

  bool is_authoritative() const noexcept {
    if (availability != Availability::Present ||
        statement_occurrence_id == UINT32_MAX ||
        est_arena_index == UINT32_MAX ||
        scrutinee_occurrence_id == UINT32_MAX ||
        specialization_context.empty() ||
        !scrutinee_type || !scrutinee_type->is_closed() || arms.empty())
      return false;
    std::set<std::string> cases;
    for (const auto& arm : arms) {
      if (arm.case_name.empty() || !cases.insert(arm.case_name).second ||
          arm.payload_binding_names.size() != arm.payload_binding_identities.size() ||
          arm.payload_binding_names.size() != arm.payload_types.size())
        return false;
      std::set<std::string> bindings;
      for (std::size_t i = 0; i < arm.payload_binding_names.size(); ++i) {
        if (arm.payload_binding_names[i].empty() ||
            !bindings.insert(arm.payload_binding_names[i]).second ||
            arm.payload_binding_identities[i].empty() ||
            !arm.payload_types[i] || !arm.payload_types[i]->is_closed())
          return false;
      }
      if (is_consuming) {
        if (arm.payload_access != Effect::Consume || !arm.transfers_ownership)
          return false;
      } else if ((arm.payload_access != Effect::Read &&
                  arm.payload_access != Effect::Write) ||
                 arm.transfers_ownership) {
        return false;
      }
    }
    return true;
  }
};

struct TypedMatchOwnershipRequirementV1 {
  std::uint32_t statement_occurrence_id{UINT32_MAX};
  std::uint32_t est_arena_index{UINT32_MAX};
  est::ExprId scrutinee_occurrence_id{UINT32_MAX};
  std::string specialization_identity;
  std::string specialization_context;
  std::string scrutinee_binding_identity;
  std::string scrutinee_name;
  TypeRef scrutinee_type;
  TypedMatchScrutineeKindV1 scrutinee_kind{TypedMatchScrutineeKindV1::Enum};
  bool is_consuming{false};
  std::vector<TypedMatchArmFactV1> arms;
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
  std::vector<TypedLocalTransferFactV1> local_transfer_facts;
  std::vector<TypedLocalTransferRequirementV1>
      required_local_transfer_occurrences;
  std::vector<TypedLoopOwnershipFactV1> loop_ownership_facts;
  std::vector<TypedLoopOwnershipRequirementV1>
      required_loop_ownership_occurrences;
  std::vector<TypedExceptionalOwnershipFactV1> exceptional_ownership_facts;
  std::vector<TypedExceptionalOwnershipRequirementV1>
      required_exceptional_ownership_occurrences;
  std::vector<TypedBareRaiseTransferFactV1> bare_raise_transfer_facts;
  std::vector<TypedBareRaiseTransferRequirementV1>
      required_bare_raise_transfer_occurrences;

  const TypedExceptionalOwnershipFactV1& find_exceptional_ownership_fact(
      const TypedExceptionalOwnershipRequirementV1& requirement) const {
    const TypedExceptionalOwnershipRequirementV1* required = nullptr;
    for (const auto& candidate : required_exceptional_ownership_occurrences)
      if (candidate.source_statement_occurrence_id ==
              requirement.source_statement_occurrence_id &&
          candidate.recovery_statement_occurrence_id ==
              requirement.recovery_statement_occurrence_id &&
          candidate.source_est_arena_index == requirement.source_est_arena_index &&
          candidate.source_expression_occurrence_id ==
              requirement.source_expression_occurrence_id &&
          candidate.source_est_slot == requirement.source_est_slot &&
          candidate.source_est_argument_index ==
              requirement.source_est_argument_index &&
          candidate.raised_variant_identity ==
              requirement.raised_variant_identity &&
          candidate.source_raise_alternatives ==
              requirement.source_raise_alternatives &&
          candidate.recovery_alternatives == requirement.recovery_alternatives &&
          candidate.specialization_context == requirement.specialization_context &&
          candidate.specialization_identity == requirement.specialization_identity) {
        if (required) throw std::invalid_argument(
            "duplicate or contradictory required exceptional ownership identity");
        required = &candidate;
      }
    if (!required) throw std::invalid_argument(
        "unknown required exceptional ownership identity");
    const TypedExceptionalOwnershipFactV1* result = nullptr;
    for (const auto& fact : exceptional_ownership_facts)
      if (fact.source_statement_occurrence_id ==
              requirement.source_statement_occurrence_id &&
          fact.recovery_statement_occurrence_id ==
              requirement.recovery_statement_occurrence_id &&
          fact.source_est_arena_index == requirement.source_est_arena_index &&
          fact.source_expression_occurrence_id ==
              requirement.source_expression_occurrence_id &&
          fact.source_est_slot == requirement.source_est_slot &&
          fact.source_est_argument_index ==
              requirement.source_est_argument_index &&
          fact.raised_variant_identity == requirement.raised_variant_identity &&
          fact.source_raise_alternatives == requirement.source_raise_alternatives &&
          fact.recovery_alternatives == requirement.recovery_alternatives &&
          fact.specialization_context == requirement.specialization_context &&
          fact.specialization_identity == requirement.specialization_identity) {
        if (result) throw std::invalid_argument(
            "duplicate or contradictory exceptional ownership facts");
        result = &fact;
      }
    if (!result || result->availability !=
            TypedExceptionalOwnershipFactV1::Availability::Present ||
        required->availability !=
            TypedExceptionalOwnershipRequirementV1::Availability::Present ||
        result->source_est_arena_index != requirement.source_est_arena_index ||
        result->source_expression_occurrence_id !=
            requirement.source_expression_occurrence_id ||
        result->source_est_slot != requirement.source_est_slot ||
        result->source_est_argument_index !=
            requirement.source_est_argument_index ||
        result->source_raise_alternatives != requirement.source_raise_alternatives ||
        result->recovery_alternatives != requirement.recovery_alternatives)
      throw std::invalid_argument(
          "required checked exceptional ownership fact missing or contradictory");
    return *result;
  }

  const TypedBareRaiseTransferRequirementV1&
  find_bare_raise_transfer_requirement(
      std::uint32_t bare_raise_statement_occurrence_id,
      std::uint32_t recovery_statement_occurrence_id,
      const TypedExceptionalOwnershipFactV1& source_edge,
      const std::string& selected_carrier_type_identity) const {
    const TypedBareRaiseTransferRequirementV1* result = nullptr;
    std::size_t matches = 0;
    for (const auto& requirement :
         required_bare_raise_transfer_occurrences) {
      if (requirement.bare_raise_statement_occurrence_id !=
              bare_raise_statement_occurrence_id ||
          requirement.recovery_statement_occurrence_id !=
              recovery_statement_occurrence_id ||
          requirement.source_statement_occurrence_id !=
              source_edge.source_statement_occurrence_id ||
          requirement.source_est_arena_index !=
              source_edge.source_est_arena_index ||
          requirement.source_expression_occurrence_id !=
              source_edge.source_expression_occurrence_id ||
          requirement.source_est_slot != source_edge.source_est_slot ||
          requirement.source_est_argument_index !=
              source_edge.source_est_argument_index ||
          requirement.raised_variant_identity !=
              source_edge.raised_variant_identity ||
          requirement.source_raise_alternatives !=
              source_edge.source_raise_alternatives ||
          requirement.recovery_alternatives !=
              source_edge.recovery_alternatives ||
          requirement.selected_carrier_type_identity !=
              selected_carrier_type_identity ||
          requirement.specialization_context !=
              source_edge.specialization_context ||
          requirement.specialization_identity !=
              source_edge.specialization_identity)
        continue;
      ++matches;
      result = &requirement;
    }
    if (matches != 1 || !result)
      throw std::invalid_argument(matches
          ? "duplicate or contradictory bare raise transfer requirement"
          : "bare raise transfer requirement missing");
    if (!result->is_complete())
      throw std::invalid_argument("bare raise transfer requirement incomplete");
    return *result;
  }

  const TypedBareRaiseTransferFactV1& find_bare_raise_transfer_fact(
      const TypedBareRaiseTransferRequirementV1& requirement) const {
    const auto& retained = find_bare_raise_transfer_requirement(
        requirement.bare_raise_statement_occurrence_id,
        requirement.recovery_statement_occurrence_id,
        [&]() -> const TypedExceptionalOwnershipFactV1& {
          TypedExceptionalOwnershipRequirementV1 exceptional_requirement;
          exceptional_requirement.source_statement_occurrence_id =
              requirement.source_statement_occurrence_id;
          exceptional_requirement.recovery_statement_occurrence_id =
              requirement.recovery_statement_occurrence_id;
          exceptional_requirement.source_est_arena_index =
              requirement.source_est_arena_index;
          exceptional_requirement.source_expression_occurrence_id =
              requirement.source_expression_occurrence_id;
          exceptional_requirement.source_est_slot =
              requirement.source_est_slot;
          exceptional_requirement.source_est_argument_index =
              requirement.source_est_argument_index;
          exceptional_requirement.raised_variant_identity =
              requirement.raised_variant_identity;
          exceptional_requirement.source_raise_alternatives =
              requirement.source_raise_alternatives;
          exceptional_requirement.recovery_alternatives =
              requirement.recovery_alternatives;
          exceptional_requirement.specialization_context =
              requirement.specialization_context;
          exceptional_requirement.specialization_identity =
              requirement.specialization_identity;
          exceptional_requirement.availability =
              TypedExceptionalOwnershipFactV1::Availability::Present;
          return find_exceptional_ownership_fact(exceptional_requirement);
        }(), requirement.selected_carrier_type_identity);
    (void)retained;

    auto same_identity = [&](const auto& item) {
      return item.bare_raise_statement_occurrence_id ==
              requirement.bare_raise_statement_occurrence_id &&
          item.recovery_statement_occurrence_id ==
              requirement.recovery_statement_occurrence_id &&
          item.source_statement_occurrence_id ==
              requirement.source_statement_occurrence_id &&
          item.source_est_arena_index == requirement.source_est_arena_index &&
          item.source_expression_occurrence_id ==
              requirement.source_expression_occurrence_id &&
          item.source_est_slot == requirement.source_est_slot &&
          item.source_est_argument_index ==
              requirement.source_est_argument_index &&
          item.raised_variant_identity == requirement.raised_variant_identity &&
          item.source_raise_alternatives ==
              requirement.source_raise_alternatives &&
          item.recovery_alternatives == requirement.recovery_alternatives &&
          item.selected_carrier_type_identity ==
              requirement.selected_carrier_type_identity &&
          item.specialization_context == requirement.specialization_context &&
          item.specialization_identity == requirement.specialization_identity;
    };
    std::size_t required_matches = 0;
    for (const auto& candidate : required_bare_raise_transfer_occurrences)
      if (same_identity(candidate)) ++required_matches;
    if (required_matches != 1 || !retained.is_complete())
      throw std::invalid_argument(
          "unknown or contradictory required bare raise transfer");
    const TypedBareRaiseTransferFactV1* result = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& fact : bare_raise_transfer_facts) {
      if (!same_identity(fact)) continue;
      ++fact_matches;
      result = &fact;
    }
    if (fact_matches != 1 || !result)
      throw std::invalid_argument(fact_matches
          ? "duplicate or contradictory bare raise transfer facts"
          : "required bare raise transfer fact missing");
    if (!result->is_authoritative() ||
        result->mode != requirement.mode ||
        result->transfers_original_carrier !=
            requirement.transfers_original_carrier ||
        result->permits_copy != requirement.permits_copy ||
        result->availability != requirement.availability)
      throw std::invalid_argument(
          "required bare raise transfer fact missing or contradictory");
    return *result;
  }
  // Checker-issued bridge from a consumer's existing function-context alias
  // to the stable key in `specializations`. Consumers must not decode either.
  std::map<std::string, std::string> local_ownership_specialization_contexts;
  std::vector<TypedTakeFactV1> take_facts;
  std::vector<TypedTakeRequirementV1> required_take_occurrences;
  std::vector<TypedCopyFactV1> copy_facts;
  std::vector<TypedCopyRequirementV1> required_copy_occurrences;
  std::vector<TypedCollectionOutcomeFactV1> collection_outcome_facts;
  std::vector<TypedCollectionOutcomeRequirementV1>
      required_collection_outcome_occurrences;
  std::vector<TypedConditionalCollectionErrorFactV1>
      conditional_collection_error_facts;
  std::vector<TypedConditionalCollectionErrorRequirementV1>
      required_conditional_collection_error_occurrences;
  std::vector<TypedReturnValueFactV1> return_value_facts;
  std::vector<TypedReturnValueRequirementV1>
      required_return_value_occurrences;
  std::vector<TypedCollectionViewFactV1> collection_view_facts;
  std::vector<TypedCollectionViewRequirementV1>
      required_collection_view_occurrences;
  std::vector<TypedProjectedCollectionReadFactV1>
      projected_collection_read_facts;
  std::vector<TypedProjectedCollectionReadRequirementV1>
      required_projected_collection_read_occurrences;
  std::vector<TypedProjectedCallArgumentReadFactV1>
      projected_call_argument_read_facts;
  std::vector<TypedProjectedCallArgumentReadRequirementV1>
      required_projected_call_argument_read_occurrences;
  std::vector<TypedMatchOwnershipFactV1> match_ownership_facts;
  std::vector<TypedMatchOwnershipRequirementV1> required_match_occurrences;
  std::vector<TypedEchoArgumentFactV1> echo_argument_facts;
  std::vector<TypedEchoArgumentRequirementV1>
      required_echo_argument_occurrences;
  std::vector<TypedRootIngressDomainCaptureFactV1>
      root_ingress_domain_capture_facts;
  std::vector<TypedRootIngressDomainCaptureRequirementV1>
      required_root_ingress_domain_capture_occurrences;
  // Legacy imported generic skeletons may appear in Function::specializations
  // with open type terms. They cannot be projected as closed specializations.
  // Keep their exact IDs visible to consumers until A2 supplies summaries.
  std::map<std::string, std::string> unavailable_specializations;
  // False until A2 emits all reachable open summaries directly from solver
  // terms. ABI consumers must call require_complete_open_summaries().
  bool open_summaries_complete{true};

  const TypedEchoArgumentRequirementV1& find_echo_argument_requirement(
      std::uint32_t arena_index, est::ExprId argument_occurrence,
      std::uint32_t argument_index,
      const std::string& specialization_context) const {
    const TypedEchoArgumentRequirementV1* result = nullptr;
    std::size_t matches = 0;
    for (const auto& requirement : required_echo_argument_occurrences) {
      if (requirement.est_arena_index != arena_index ||
          requirement.argument_expression_occurrence_id !=
              argument_occurrence ||
          requirement.argument_index != argument_index ||
          requirement.specialization_context != specialization_context)
        continue;
      ++matches;
      result = &requirement;
    }
    if (matches != 1 || !result)
      throw std::invalid_argument(matches
          ? "duplicate or contradictory required Echo argument identity"
          : "required Echo argument occurrence missing");
    if (!result->is_complete())
      throw std::invalid_argument("required Echo argument is incomplete");
    return *result;
  }

  const TypedEchoArgumentFactV1& find_echo_argument_fact(
      const TypedEchoArgumentRequirementV1& requirement) const {
    const auto& retained = find_echo_argument_requirement(
        requirement.est_arena_index,
        requirement.argument_expression_occurrence_id,
        requirement.argument_index, requirement.specialization_context);
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return (left == right) ||
          (left && right && left->structurally_equal(*right));
    };
    if (retained.echo_statement_occurrence_id !=
            requirement.echo_statement_occurrence_id ||
        retained.indexed_expression_occurrence_id !=
            requirement.indexed_expression_occurrence_id ||
        retained.receiver_occurrence_id != requirement.receiver_occurrence_id ||
        retained.selector_occurrence_id != requirement.selector_occurrence_id ||
        retained.specialization_identity != requirement.specialization_identity ||
        retained.est_fact_context != requirement.est_fact_context ||
        !same_type(retained.argument_type, requirement.argument_type) ||
        !same_type(retained.receiver_type, requirement.receiver_type) ||
        !same_type(retained.element_type, requirement.element_type) ||
        retained.mode != requirement.mode ||
        retained.required_access != requirement.required_access ||
        retained.display_result_non_escaping !=
            requirement.display_result_non_escaping ||
        retained.permits_copy != requirement.permits_copy)
      throw std::invalid_argument(
          "unknown or contradictory required Echo argument");

    const TypedEchoArgumentFactV1* result = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& fact : echo_argument_facts) {
      if (fact.echo_statement_occurrence_id !=
              requirement.echo_statement_occurrence_id ||
          fact.argument_index != requirement.argument_index ||
          fact.est_arena_index != requirement.est_arena_index ||
          fact.argument_expression_occurrence_id !=
              requirement.argument_expression_occurrence_id ||
          fact.specialization_context != requirement.specialization_context)
        continue;
      ++fact_matches;
      result = &fact;
    }
    if (fact_matches != 1 || !result)
      throw std::invalid_argument(fact_matches
          ? "duplicate or contradictory checked Echo argument facts"
          : "required checked Echo argument fact missing");
    if (!result->is_authoritative() ||
        result->indexed_expression_occurrence_id !=
            requirement.indexed_expression_occurrence_id ||
        result->receiver_occurrence_id != requirement.receiver_occurrence_id ||
        result->selector_occurrence_id != requirement.selector_occurrence_id ||
        result->specialization_identity != requirement.specialization_identity ||
        result->est_fact_context != requirement.est_fact_context ||
        !same_type(result->argument_type, requirement.argument_type) ||
        !same_type(result->receiver_type, requirement.receiver_type) ||
        !same_type(result->element_type, requirement.element_type) ||
        result->mode != requirement.mode ||
        result->required_access != requirement.required_access ||
        result->display_result_non_escaping !=
            requirement.display_result_non_escaping ||
        result->permits_copy != requirement.permits_copy)
      throw std::invalid_argument(
          "required checked Echo argument fact missing or contradictory");
    return *result;
  }

  const TypedRootIngressDomainCaptureFactV1&
  find_root_ingress_domain_capture_fact(
      const TypedRootIngressDomainCaptureRequirementV1& requirement) const {
    const auto same = [](const TypedRootIngressDomainCaptureFactV1& left,
                         const TypedRootIngressDomainCaptureFactV1& right) {
      return left.statement_occurrence_id == right.statement_occurrence_id &&
          left.est_arena_index == right.est_arena_index &&
          left.message_expression_occurrence_id ==
              right.message_expression_occurrence_id &&
          left.receiver_expression_occurrence_id ==
              right.receiver_expression_occurrence_id &&
          left.source_fingerprint == right.source_fingerprint &&
          left.origin_id == right.origin_id &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          left.receiver_binding == right.receiver_binding &&
          left.receiver_definition_identity == right.receiver_definition_identity &&
          left.domain_instance_identity == right.domain_instance_identity &&
          left.domain_type_identity == right.domain_type_identity &&
          left.concrete_graph_identity == right.concrete_graph_identity &&
          left.route_capability_identity == right.route_capability_identity &&
          left.selected_call_edge_identity ==
              right.selected_call_edge_identity &&
          left.selected_call_edge_source == right.selected_call_edge_source &&
          left.selected_call_edge_target == right.selected_call_edge_target &&
          left.selected_call_edge_invocation_kind ==
              right.selected_call_edge_invocation_kind &&
          left.selected_call_edge_receiver == right.selected_call_edge_receiver &&
          left.selected_call_edge_source_file ==
              right.selected_call_edge_source_file &&
          left.selected_call_edge_line == right.selected_call_edge_line &&
          left.selected_call_edge_occurrence ==
              right.selected_call_edge_occurrence &&
          left.selected_call_edge_est_arena_index ==
              right.selected_call_edge_est_arena_index &&
          left.selected_call_edge_message_expression_occurrence_id ==
              right.selected_call_edge_message_expression_occurrence_id &&
          left.selected_call_edge_receiver_expression_occurrence_id ==
              right.selected_call_edge_receiver_expression_occurrence_id &&
          left.selected_call_edge_source_fingerprint ==
              right.selected_call_edge_source_fingerprint &&
          left.selected_handler_identity == right.selected_handler_identity &&
          left.source_file == right.source_file &&
          left.source_line == right.source_line &&
          left.synchronous_root_ingress == right.synchronous_root_ingress &&
          left.compiler_generated_capture_only ==
              right.compiler_generated_capture_only &&
          left.permits_moss_value_copy == right.permits_moss_value_copy &&
          left.availability == right.availability;
    };
    const TypedRootIngressDomainCaptureRequirementV1* retained = nullptr;
    std::size_t requirement_matches = 0;
    for (const auto& candidate : required_root_ingress_domain_capture_occurrences)
      if (candidate.statement_occurrence_id == requirement.statement_occurrence_id &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.message_expression_occurrence_id ==
              requirement.message_expression_occurrence_id &&
          candidate.specialization_context == requirement.specialization_context) {
        retained = &candidate;
        ++requirement_matches;
      }
    if (requirement_matches != 1 || !retained || !same(*retained, requirement) ||
        !retained->is_authoritative())
      throw std::invalid_argument(
          "unknown or contradictory required Root-ingress domain capture");
    const TypedRootIngressDomainCaptureFactV1* result = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& candidate : root_ingress_domain_capture_facts)
      if (candidate.statement_occurrence_id == requirement.statement_occurrence_id &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.message_expression_occurrence_id ==
              requirement.message_expression_occurrence_id &&
          candidate.specialization_context == requirement.specialization_context) {
        result = &candidate;
        ++fact_matches;
      }
    if (fact_matches != 1 || !result || !same(*result, requirement) ||
        !result->is_authoritative())
      throw std::invalid_argument(fact_matches
          ? "duplicate or contradictory Root-ingress domain capture facts"
          : "required Root-ingress domain capture fact missing");
    return *result;
  }

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

  static bool same_local_ownership_type(const TypeRef& left,
                                        const TypeRef& right) {
    return left && right ? left->structurally_equal(*right)
                         : !left && !right;
  }

  void validate_loop_ownership_requirement_specialization(
      const TypedLoopOwnershipRequirementV1& requirement) const {
    const auto mapping = local_ownership_specialization_contexts.find(
        requirement.specialization_context);
    if (mapping == local_ownership_specialization_contexts.end()) {
      if (!requirement.specialization_identity.empty())
        throw std::invalid_argument(
            "loop ownership specialization identity has no checker mapping");
      return;
    }
    if (requirement.specialization_identity.empty() ||
        local_ownership_specialization_identity(
            requirement.specialization_context) !=
            requirement.specialization_identity)
      throw std::invalid_argument(
          "loop ownership specialization identity mismatch");
  }

  static bool same_loop_ownership_requirement(
      const TypedLoopOwnershipRequirementV1& left,
      const TypedLoopOwnershipRequirementV1& right) {
    return left.loop_statement_occurrence_id ==
               right.loop_statement_occurrence_id &&
        left.source_statement_occurrence_id ==
               right.source_statement_occurrence_id &&
        left.target_statement_occurrence_id ==
               right.target_statement_occurrence_id &&
        left.definition_statement_occurrence_id ==
               right.definition_statement_occurrence_id &&
        left.loop_est_arena_index == right.loop_est_arena_index &&
        left.loop_expression_occurrence_id ==
               right.loop_expression_occurrence_id &&
        left.definition_est_arena_index == right.definition_est_arena_index &&
        left.definition_expression_occurrence_id ==
               right.definition_expression_occurrence_id &&
        left.definition_identity == right.definition_identity &&
        left.binding_identity == right.binding_identity &&
        left.killed_definition_identities ==
               right.killed_definition_identities &&
        left.source_binding_identity == right.source_binding_identity &&
        left.specialization_context == right.specialization_context &&
        left.specialization_identity == right.specialization_identity &&
        left.edge_kind == right.edge_kind &&
        left.converged_access == right.converged_access &&
        left.value_state == right.value_state &&
        left.availability == right.availability;
  }

  const TypedLoopOwnershipFactV1* find_loop_ownership_fact(
      const TypedLoopOwnershipRequirementV1& requirement) const {
    const std::size_t matching_requirements = std::count_if(
        required_loop_ownership_occurrences.begin(),
        required_loop_ownership_occurrences.end(), [&](const auto& candidate) {
          return same_loop_ownership_requirement(candidate, requirement);
        });
    if (matching_requirements != 1)
      throw std::invalid_argument(
          "required checker loop ownership requirement missing or duplicated");
    validate_loop_ownership_requirement_specialization(requirement);

    const TypedLoopOwnershipFactV1* result = nullptr;
    for (const auto& fact : loop_ownership_facts) {
      if (fact.loop_statement_occurrence_id !=
              requirement.loop_statement_occurrence_id ||
          fact.source_statement_occurrence_id !=
              requirement.source_statement_occurrence_id ||
          fact.target_statement_occurrence_id !=
              requirement.target_statement_occurrence_id ||
          fact.definition_statement_occurrence_id !=
              requirement.definition_statement_occurrence_id ||
          fact.loop_est_arena_index != requirement.loop_est_arena_index ||
          fact.loop_expression_occurrence_id !=
              requirement.loop_expression_occurrence_id ||
          fact.definition_est_arena_index !=
              requirement.definition_est_arena_index ||
          fact.definition_expression_occurrence_id !=
              requirement.definition_expression_occurrence_id ||
          fact.definition_identity != requirement.definition_identity ||
          fact.binding_identity != requirement.binding_identity ||
          fact.killed_definition_identities !=
              requirement.killed_definition_identities ||
          fact.source_binding_identity != requirement.source_binding_identity ||
          fact.specialization_context != requirement.specialization_context ||
          fact.specialization_identity != requirement.specialization_identity ||
          fact.edge_kind != requirement.edge_kind)
        continue;
      if (result)
        throw std::invalid_argument(
            "duplicate or contradictory checker loop ownership facts");
      result = &fact;
    }
    if (!result || result->availability != requirement.availability ||
        result->converged_access != requirement.converged_access ||
        result->value_state != requirement.value_state ||
        result->availability != TypedLoopOwnershipFactV1::Availability::Present ||
        !result->reachable || result->definition_identity.empty() ||
        result->binding_identity.empty() ||
        result->loop_statement_occurrence_id == UINT32_MAX ||
        result->loop_est_arena_index == UINT32_MAX ||
        result->loop_expression_occurrence_id == UINT32_MAX ||
        result->definition_statement_occurrence_id == UINT32_MAX ||
        result->definition_est_arena_index == UINT32_MAX ||
        result->definition_expression_occurrence_id == UINT32_MAX ||
        result->value_state == TypedLoopOwnershipFactV1::ValueState::Unavailable)
      throw std::invalid_argument(
          "required checker loop ownership fact missing or contradictory");
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
          candidate.binding_identity == req.binding_identity &&
          candidate.killed_definition_identity == req.killed_definition_identity &&
          candidate.join_definition_identity == req.join_definition_identity &&
          candidate.source_binding_identity == req.source_binding_identity &&
          same_local_ownership_type(candidate.value_type, req.value_type) &&
          candidate.source_definition_identity == req.source_definition_identity &&
          candidate.access == req.access &&
          candidate.source_kind == req.source_kind &&
          candidate.disposition == req.disposition &&
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
          fact.binding_identity != req.binding_identity ||
          fact.killed_definition_identity != req.killed_definition_identity ||
          fact.join_definition_identity != req.join_definition_identity ||
          fact.source_binding_identity != req.source_binding_identity ||
          fact.source_definition_identity != req.source_definition_identity)
        continue;
      if (result)
        throw std::invalid_argument("duplicate local ownership fact");
      result = &fact;
    }
    if (!result)
      throw std::invalid_argument("required local ownership fact missing");
    if (result->access != req.access ||
        !same_local_ownership_type(result->value_type, req.value_type) ||
        result->source_kind != req.source_kind ||
        result->disposition != req.disposition ||
        result->inference_status != req.inference_status ||
        result->unsupported_reason != req.unsupported_reason ||
        result->availability != req.availability)
      throw std::invalid_argument("required local ownership fact contradicts requirement");
    if (result->is_intentionally_unsupported()) return *result;
    if (!result->is_authoritative())
      throw std::invalid_argument("required local ownership fact unavailable");
    return *result;
  }

  const TypedLocalTransferFactV1& find_local_transfer_fact(
      const TypedLocalTransferRequirementV1& req) const {
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return left && right && left->structurally_equal(*right);
    };
    auto same_span = [](const est::SourceSpan& left,
                        const est::SourceSpan& right) {
      return left.line == right.line && left.column == right.column &&
          left.origin_id == right.origin_id &&
          left.source_begin == right.source_begin &&
          left.source_end == right.source_end;
    };
    auto same_provenance = [](const TypedSourceProvenanceV1& left,
                              const TypedSourceProvenanceV1& right) {
      return left.source_file == right.source_file &&
          left.line == right.line &&
          left.defining_module_id == right.defining_module_id &&
          left.enclosing_callable_id == right.enclosing_callable_id;
    };
    auto same_contract = [&](const auto& left, const auto& right) {
      return left.availability == right.availability &&
          left.action == right.action &&
          left.est_arena_index == right.est_arena_index &&
          left.rhs_expression_occurrence_id ==
              right.rhs_expression_occurrence_id &&
          same_span(left.rhs_occurrence_span, right.rhs_occurrence_span) &&
          left.statement_occurrence_id == right.statement_occurrence_id &&
          left.source_binding_identity == right.source_binding_identity &&
          left.source_definition_identity == right.source_definition_identity &&
          left.target_binding_identity == right.target_binding_identity &&
          left.target_definition_identity == right.target_definition_identity &&
          left.killed_target_definition_identity ==
              right.killed_target_definition_identity &&
          left.source_scope_id == right.source_scope_id &&
          left.target_scope_id == right.target_scope_id &&
          left.source_scope_nested_under_target ==
              right.source_scope_nested_under_target &&
          left.source_last_use_statement_occurrence_id ==
              right.source_last_use_statement_occurrence_id &&
          left.source_is_owned == right.source_is_owned &&
          left.source_dead_after_assignment ==
              right.source_dead_after_assignment &&
          same_type(left.source_type, right.source_type) &&
          same_type(left.target_type, right.target_type) &&
          left.specialization_identity == right.specialization_identity &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          same_provenance(left.provenance, right.provenance);
    };
    const TypedLocalTransferRequirementV1* required = nullptr;
    std::size_t requirement_matches = 0;
    for (const auto& candidate : required_local_transfer_occurrences) {
      if (!same_contract(candidate, req)) continue;
      required = &candidate;
      ++requirement_matches;
    }
    if (requirement_matches != 1 || !required)
      throw std::invalid_argument(requirement_matches
          ? "duplicate or contradictory local transfer requirement"
          : "unknown local transfer requirement");
    const TypedLocalTransferFactV1* result = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& candidate : local_transfer_facts) {
      if (candidate.est_arena_index != req.est_arena_index ||
          candidate.rhs_expression_occurrence_id !=
              req.rhs_expression_occurrence_id ||
          candidate.statement_occurrence_id != req.statement_occurrence_id ||
          candidate.specialization_identity != req.specialization_identity ||
          candidate.specialization_context != req.specialization_context) continue;
      result = &candidate;
      ++fact_matches;
    }
    if (fact_matches != 1 || !result)
      throw std::invalid_argument(fact_matches
          ? "duplicate local transfer fact"
          : "required local transfer fact missing");
    if (!same_contract(*result, req) || !result->is_authoritative())
      throw std::invalid_argument(
          "required local transfer fact missing or contradictory");
    return *result;
  }

  const TypedLocalTransferFactV1* find_local_transfer_fact(
      std::uint32_t arena_index, est::ExprId rhs_expression_occurrence_id,
      std::uint32_t statement_occurrence_id,
      const std::string& specialization_context,
      const std::string& specialization_identity) const {
    const TypedLocalTransferRequirementV1* result = nullptr;
    for (const auto& requirement : required_local_transfer_occurrences) {
      if (requirement.est_arena_index != arena_index ||
          requirement.rhs_expression_occurrence_id !=
              rhs_expression_occurrence_id ||
          requirement.statement_occurrence_id != statement_occurrence_id ||
          requirement.specialization_context != specialization_context ||
          requirement.specialization_identity != specialization_identity)
        continue;
      if (result)
        throw std::invalid_argument("duplicate local transfer lookup");
      result = &requirement;
    }
    if (!result)
      throw std::invalid_argument("required local transfer fact missing");
    return &find_local_transfer_fact(*result);
  }

  std::vector<const TypedLocalOwnershipFactV1*>
  require_all_local_ownership_facts() const {
    if (local_ownership_facts.size() !=
        required_local_ownership_occurrences.size())
      throw std::invalid_argument(
          "local ownership fact/requirement set is incomplete");
    std::vector<const TypedLocalOwnershipFactV1*> result;
    result.reserve(required_local_ownership_occurrences.size());
    for (const auto& requirement : required_local_ownership_occurrences)
      result.push_back(&find_local_ownership_fact(requirement));
    return result;
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

  const TypedCopyFactV1* find_copy_fact(
      const TypedCopyRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return (left == right) || (left && right && left->structurally_equal(*right));
    };
    auto same_span = [](const est::SourceSpan& left,
                        const est::SourceSpan& right) {
      return left.line == right.line && left.column == right.column &&
          left.origin_id == right.origin_id &&
          left.source_begin == right.source_begin &&
          left.source_end == right.source_end;
    };
    auto same_provenance = [](const TypedSourceProvenanceV1& left,
                              const TypedSourceProvenanceV1& right) {
      return left.source_file == right.source_file &&
          left.line == right.line &&
          left.defining_module_id == right.defining_module_id &&
          left.enclosing_callable_id == right.enclosing_callable_id;
    };
    const TypedCopyRequirementV1* required = nullptr;
    std::size_t matching_requirements = 0;
    for (const auto& candidate : required_copy_occurrences) {
      if (candidate.est_arena_index != requirement.est_arena_index ||
          candidate.copy_occurrence_id != requirement.copy_occurrence_id ||
          candidate.specialization_identity != requirement.specialization_identity ||
          candidate.specialization_context != requirement.specialization_context)
        continue;
      ++matching_requirements;
      required = &candidate;
    }
    if (matching_requirements != 1 || !required)
      throw std::invalid_argument(matching_requirements
          ? "duplicate or contradictory required checked copy requirement"
          : "unknown required checked copy occurrence");

    const TypedCopyFactV1* result = nullptr;
    std::size_t matching_facts = 0;
    for (const auto& fact : copy_facts) {
      if (fact.est_arena_index != requirement.est_arena_index ||
          fact.copy_occurrence_id != requirement.copy_occurrence_id ||
          fact.specialization_identity != requirement.specialization_identity ||
          fact.specialization_context != requirement.specialization_context)
        continue;
      ++matching_facts;
      result = &fact;
    }
    if (matching_facts != 1 || !result) {
      if (matching_facts > 1)
        throw std::invalid_argument("duplicate or contradictory checked copy facts");
      throw std::invalid_argument("required checked copy fact missing");
    }
    if (result->source_occurrence_id != requirement.source_occurrence_id ||
        result->statement_occurrence_id != requirement.statement_occurrence_id ||
        result->est_fact_context != requirement.est_fact_context ||
        !same_provenance(result->provenance, requirement.provenance) ||
        !same_span(result->copy_occurrence_span,
                   requirement.copy_occurrence_span) ||
        !same_span(result->source_occurrence_span,
                   requirement.source_occurrence_span) ||
        !same_type(result->source_type, requirement.source_type) ||
        !same_type(result->result_type, requirement.result_type) ||
        result->required_source_access != requirement.required_source_access ||
        result->creates_independent_owner != requirement.creates_independent_owner)
      throw std::invalid_argument("required checked copy fact contradicts requirement");
    if (!result->is_authoritative())
      throw std::invalid_argument("required checked copy fact unavailable");
    return result;
  }

  const TypedCopyRequirementV1& find_copy_requirement(
      std::uint32_t arena_index, est::ExprId copy_occurrence,
      const std::string& specialization_context) const {
    const TypedCopyRequirementV1* result = nullptr;
    std::size_t matches = 0;
    for (const auto& requirement : required_copy_occurrences) {
      if (requirement.est_arena_index != arena_index ||
          requirement.copy_occurrence_id != copy_occurrence ||
          requirement.specialization_context != specialization_context)
        continue;
      ++matches;
      result = &requirement;
    }
    if (matches != 1 || !result)
      throw std::invalid_argument(matches
          ? "ambiguous checker copy requirement"
          : "checker copy requirement missing");
    (void)find_copy_fact(*result);
    return *result;
  }

  const TypedCopyRequirementV1& find_copy_requirement(
      std::uint32_t arena_index, est::ExprId copy_occurrence,
      const std::string& specialization_context,
      const std::string& specialization_identity) const {
    const auto& requirement = find_copy_requirement(
        arena_index, copy_occurrence, specialization_context);
    if (requirement.specialization_identity != specialization_identity)
      throw std::invalid_argument("checker copy requirement specialization mismatch");
    return requirement;
  }

  // Resolve the complete checker-declared copy authority set. This is the
  // read-only query contract for artifact auditors: callers must serialize
  // these returned checked facts, never discover copy() occurrences by
  // scanning source or generated output. Each required occurrence is matched
  // exactly once and any missing, duplicate, or contradictory row throws.
  std::vector<const TypedCopyFactV1*> require_all_copy_facts() const {
    std::vector<const TypedCopyFactV1*> result;
    result.reserve(required_copy_occurrences.size());
    for (const auto& requirement : required_copy_occurrences)
      result.push_back(find_copy_fact(requirement));
    return result;
  }

  const TypedCollectionOutcomeFactV1* find_collection_outcome_fact(
      const TypedCollectionOutcomeRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return (left == right) || (left && right && left->structurally_equal(*right));
    };
    auto same_span = [](const est::SourceSpan& left,
                        const est::SourceSpan& right) {
      return left.line == right.line && left.column == right.column &&
          left.origin_id == right.origin_id &&
          left.source_begin == right.source_begin &&
          left.source_end == right.source_end;
    };
    auto same_provenance = [](const TypedSourceProvenanceV1& left,
                              const TypedSourceProvenanceV1& right) {
      return left.source_file == right.source_file && left.line == right.line &&
          left.defining_module_id == right.defining_module_id &&
          left.enclosing_callable_id == right.enclosing_callable_id;
    };
    auto same_fields = [&](const TypedCollectionOutcomeFactV1& left,
                           const TypedCollectionOutcomeFactV1& right) {
      return left.operation == right.operation && left.outcome == right.outcome &&
          left.availability == right.availability &&
          left.operation_est_arena_index == right.operation_est_arena_index &&
          left.operation_expression_occurrence_id ==
              right.operation_expression_occurrence_id &&
          same_span(left.operation_occurrence_span,
                    right.operation_occurrence_span) &&
          left.statement_occurrence_id == right.statement_occurrence_id &&
          left.specialization_identity == right.specialization_identity &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          same_provenance(left.provenance, right.provenance) &&
          left.receiver_binding_identity == right.receiver_binding_identity &&
          left.receiver_definition_identity == right.receiver_definition_identity &&
          left.fresh_empty_definition_identity ==
              right.fresh_empty_definition_identity &&
          same_type(left.receiver_type, right.receiver_type) &&
          left.argument_expression_occurrence_id ==
              right.argument_expression_occurrence_id &&
          same_span(left.argument_occurrence_span, right.argument_occurrence_span) &&
          same_type(left.argument_type, right.argument_type) &&
          same_type(left.result_type, right.result_type) &&
          left.error_type_identity == right.error_type_identity &&
          left.error_variant_identity == right.error_variant_identity;
    };
    const TypedCollectionOutcomeRequirementV1* required = nullptr;
    std::size_t matching_requirements = 0;
    for (const auto& candidate : required_collection_outcome_occurrences) {
      if (candidate.operation != requirement.operation ||
          candidate.operation_est_arena_index !=
              requirement.operation_est_arena_index ||
          candidate.operation_expression_occurrence_id !=
              requirement.operation_expression_occurrence_id ||
          candidate.specialization_identity != requirement.specialization_identity ||
          candidate.specialization_context != requirement.specialization_context)
        continue;
      ++matching_requirements;
      required = &candidate;
    }
    if (matching_requirements != 1 || !required)
      throw std::invalid_argument(matching_requirements
          ? "duplicate or contradictory required collection outcome"
          : "unknown required collection outcome occurrence");
    const TypedCollectionOutcomeFactV1* result = nullptr;
    std::size_t matching_facts = 0;
    for (const auto& candidate : collection_outcome_facts) {
      if (candidate.operation != requirement.operation ||
          candidate.operation_est_arena_index !=
              requirement.operation_est_arena_index ||
          candidate.operation_expression_occurrence_id !=
              requirement.operation_expression_occurrence_id ||
          candidate.specialization_identity != requirement.specialization_identity ||
          candidate.specialization_context != requirement.specialization_context)
        continue;
      ++matching_facts;
      result = &candidate;
    }
    if (matching_facts != 1 || !result)
      throw std::invalid_argument(matching_facts
          ? "duplicate or contradictory checked collection outcome facts"
          : "required checked collection outcome fact missing");
    if (!same_fields(*required, requirement) ||
        !same_fields(*result, requirement) || !result->is_authoritative())
      throw std::invalid_argument(
          "required checked collection outcome fact unavailable or contradictory");
    return result;
  }

  std::vector<const TypedCollectionOutcomeFactV1*>
  require_all_collection_outcome_facts() const {
    if (collection_outcome_facts.size() !=
        required_collection_outcome_occurrences.size())
      throw std::invalid_argument(
          "checked collection outcome fact/requirement set is incomplete");
    std::vector<const TypedCollectionOutcomeFactV1*> result;
    result.reserve(required_collection_outcome_occurrences.size());
    for (const auto& requirement : required_collection_outcome_occurrences)
      result.push_back(find_collection_outcome_fact(requirement));
    return result;
  }

  const TypedConditionalCollectionErrorFactV1*
  find_conditional_collection_error_fact(
      const TypedConditionalCollectionErrorRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& a, const TypeRef& b) {
      return a == b || (a && b && a->structurally_equal(*b));
    };
    auto same_span = [](const est::SourceSpan& a, const est::SourceSpan& b) {
      return a.line == b.line && a.column == b.column &&
          a.origin_id == b.origin_id && a.source_begin == b.source_begin &&
          a.source_end == b.source_end;
    };
    auto same_fields = [&](const TypedConditionalCollectionErrorFactV1& a,
                           const TypedConditionalCollectionErrorFactV1& b) {
      return a.operation == b.operation && a.availability == b.availability &&
          a.est_arena_index == b.est_arena_index &&
          a.operation_expression_occurrence_id == b.operation_expression_occurrence_id &&
          same_span(a.operation_occurrence_span, b.operation_occurrence_span) &&
          a.statement_occurrence_id == b.statement_occurrence_id &&
          a.receiver_expression_occurrence_id == b.receiver_expression_occurrence_id &&
          a.key_expression_occurrence_id == b.key_expression_occurrence_id &&
          same_span(a.receiver_occurrence_span, b.receiver_occurrence_span) &&
          same_span(a.key_occurrence_span, b.key_occurrence_span) &&
          a.receiver_binding_identity == b.receiver_binding_identity &&
          a.receiver_definition_identity == b.receiver_definition_identity &&
          same_type(a.receiver_type, b.receiver_type) &&
          same_type(a.key_type, b.key_type) && same_type(a.result_type, b.result_type) &&
          same_type(a.write_value_type, b.write_value_type) &&
          a.error_type_identity == b.error_type_identity &&
          a.error_variant_identity == b.error_variant_identity &&
          a.exceptional_edge_authorized == b.exceptional_edge_authorized &&
          a.specialization_identity == b.specialization_identity &&
          a.specialization_context == b.specialization_context &&
          a.est_fact_context == b.est_fact_context &&
          a.provenance.source_file == b.provenance.source_file &&
          a.provenance.line == b.provenance.line &&
          a.provenance.defining_module_id == b.provenance.defining_module_id &&
          a.provenance.enclosing_callable_id == b.provenance.enclosing_callable_id;
    };
    std::size_t required_matches = 0;
    for (const auto& candidate : required_conditional_collection_error_occurrences)
      if (candidate.operation == requirement.operation &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.operation_expression_occurrence_id ==
              requirement.operation_expression_occurrence_id &&
          candidate.specialization_context == requirement.specialization_context)
        ++required_matches;
    if (required_matches != 1)
      throw std::invalid_argument("missing or duplicate conditional CollectionError requirement");
    const TypedConditionalCollectionErrorFactV1* found = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& candidate : conditional_collection_error_facts)
      if (candidate.operation == requirement.operation &&
          candidate.est_arena_index == requirement.est_arena_index &&
          candidate.operation_expression_occurrence_id ==
              requirement.operation_expression_occurrence_id &&
          candidate.specialization_context == requirement.specialization_context) {
        found = &candidate;
        ++fact_matches;
      }
    if (fact_matches != 1 || !found ||
        !same_fields(*found, requirement) || !found->is_authoritative())
      throw std::invalid_argument("conditional CollectionError fact is missing, duplicate, or contradictory");
    return found;
  }

  const TypedReturnValueFactV1* find_return_value_fact(
      const TypedReturnValueRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& a, const TypeRef& b) {
      return a == b || (a && b && a->structurally_equal(*b));
    };
    const auto key = [&](const TypedReturnValueFactV1& a) {
      return a.statement_occurrence_id == requirement.statement_occurrence_id &&
          a.est_arena_index == requirement.est_arena_index &&
          a.expression_occurrence_id == requirement.expression_occurrence_id &&
          a.specialization_context == requirement.specialization_context &&
          a.specialization_identity == requirement.specialization_identity;
    };
    std::size_t req_count = 0;
    for (const auto& item : required_return_value_occurrences) req_count += key(item);
    const TypedReturnValueFactV1* found = nullptr;
    std::size_t fact_count = 0;
    for (const auto& item : return_value_facts) if (key(item)) {
      found = &item;
      ++fact_count;
    }
    const auto same_span = [](const est::SourceSpan& a, const est::SourceSpan& b) {
      return a.line == b.line && a.column == b.column &&
          a.origin_id == b.origin_id && a.source_begin == b.source_begin &&
          a.source_end == b.source_end;
    };
    const bool same = found && same_type(found->expression_type, requirement.expression_type) &&
        found->availability == requirement.availability &&
        same_span(found->expression_occurrence_span, requirement.expression_occurrence_span) &&
        found->specialization_identity == requirement.specialization_identity &&
        found->est_fact_context == requirement.est_fact_context &&
        found->provenance.source_file == requirement.provenance.source_file &&
        found->provenance.line == requirement.provenance.line &&
        found->provenance.defining_module_id == requirement.provenance.defining_module_id &&
        found->provenance.enclosing_callable_id == requirement.provenance.enclosing_callable_id &&
        found->source_binding_identity == requirement.source_binding_identity &&
        found->source_definition_identity == requirement.source_definition_identity &&
        found->required_access == requirement.required_access &&
        found->action == requirement.action &&
        found->checker_proved_owned_rvalue == requirement.checker_proved_owned_rvalue;
    if (req_count != 1 || fact_count != 1 || !same || !found->is_authoritative()) {
      throw std::invalid_argument("return-value checker fact is missing, duplicate, or contradictory");
    }
    return found;
  }

  const TypedCollectionViewFactV1* find_collection_view_fact(
      const TypedCollectionViewRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& a, const TypeRef& b) {
      return a == b || (a && b && a->structurally_equal(*b));
    };
    const auto key = [&](const TypedCollectionViewFactV1& a) {
      return a.operation == requirement.operation &&
          a.est_arena_index == requirement.est_arena_index &&
          a.operation_expression_occurrence_id ==
              requirement.operation_expression_occurrence_id &&
          a.statement_occurrence_id == requirement.statement_occurrence_id &&
          a.specialization_context == requirement.specialization_context;
    };
    std::size_t req_count = 0;
    for (const auto& item : required_collection_view_occurrences) req_count += key(item);
    const TypedCollectionViewFactV1* found = nullptr;
    std::size_t fact_count = 0;
    for (const auto& item : collection_view_facts) if (key(item)) {
      found = &item;
      ++fact_count;
    }
    const bool same = found && found->availability == requirement.availability &&
        found->receiver_binding_identity == requirement.receiver_binding_identity &&
        found->receiver_definition_identity == requirement.receiver_definition_identity &&
        found->view_binding_identity == requirement.view_binding_identity &&
        found->view_definition_identity == requirement.view_definition_identity &&
        found->last_use_statement_occurrence_id == requirement.last_use_statement_occurrence_id &&
        found->direct_temporary == requirement.direct_temporary &&
        found->bounded_read_use_statement_occurrences ==
            requirement.bounded_read_use_statement_occurrences &&
        same_type(found->receiver_type, requirement.receiver_type) &&
        same_type(found->result_type, requirement.result_type) &&
        found->required_access == requirement.required_access &&
        found->bounded_non_escaping_lifetime == requirement.bounded_non_escaping_lifetime &&
        found->specialization_identity == requirement.specialization_identity &&
        found->est_fact_context == requirement.est_fact_context &&
        found->provenance.source_file == requirement.provenance.source_file &&
        found->provenance.line == requirement.provenance.line &&
        found->provenance.defining_module_id == requirement.provenance.defining_module_id &&
        found->provenance.enclosing_callable_id == requirement.provenance.enclosing_callable_id;
    if (req_count != 1 || fact_count != 1 || !same || !found->is_authoritative())
      throw std::invalid_argument("collection view checker fact is missing, duplicate, or contradictory");
    return found;
  }

  const TypedProjectedCallArgumentReadFactV1&
  find_projected_call_argument_read_fact(
      const TypedProjectedCallArgumentReadRequirementV1& requirement) const {
    const auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return (!left && !right) ||
          (left && right && left->structurally_equal(*right));
    };
    const auto same_span = [](const est::SourceSpan& left,
                              const est::SourceSpan& right) {
      return left.line == right.line && left.column == right.column &&
          left.origin_id == right.origin_id &&
          left.source_begin == right.source_begin &&
          left.source_end == right.source_end;
    };
    const auto same_provenance = [](const TypedSourceProvenanceV1& left,
                                    const TypedSourceProvenanceV1& right) {
      return left.source_file == right.source_file &&
          left.line == right.line &&
          left.defining_module_id == right.defining_module_id &&
          left.enclosing_callable_id == right.enclosing_callable_id;
    };
    const auto same_fields = [&](const auto& left, const auto& right) {
      return left.availability == right.availability &&
          left.est_arena_index == right.est_arena_index &&
          left.call_argument_expression_occurrence_id ==
              right.call_argument_expression_occurrence_id &&
          left.receiver_expression_occurrence_id ==
              right.receiver_expression_occurrence_id &&
          left.collection_index_expression_occurrence_id ==
              right.collection_index_expression_occurrence_id &&
          left.index_expression_occurrence_id ==
              right.index_expression_occurrence_id &&
          left.member_projection_expression_occurrence_id ==
              right.member_projection_expression_occurrence_id &&
          left.statement_occurrence_id == right.statement_occurrence_id &&
          left.argument_index == right.argument_index &&
          left.target_parameter_index == right.target_parameter_index &&
          same_span(left.argument_occurrence_span,
                    right.argument_occurrence_span) &&
          same_span(left.projection_occurrence_span,
                    right.projection_occurrence_span) &&
          left.target_callable_identity == right.target_callable_identity &&
          left.target_specialization_identity ==
              right.target_specialization_identity &&
          left.target_resolution == right.target_resolution &&
          left.target_parameter_access == right.target_parameter_access &&
          left.receiver_binding_identity == right.receiver_binding_identity &&
          left.receiver_definition_identity == right.receiver_definition_identity &&
          left.member_name == right.member_name &&
          same_type(left.receiver_type, right.receiver_type) &&
          same_type(left.index_type, right.index_type) &&
          same_type(left.indexed_element_type, right.indexed_element_type) &&
          same_type(left.projection_base_type, right.projection_base_type) &&
          same_type(left.projected_member_type, right.projected_member_type) &&
          left.required_access == right.required_access &&
          left.temporary_nonescaping_borrow ==
              right.temporary_nonescaping_borrow &&
          left.permits_copy == right.permits_copy &&
          left.specialization_identity == right.specialization_identity &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          same_provenance(left.provenance, right.provenance);
    };
    std::size_t requirement_matches = 0;
    for (const auto& candidate : required_projected_call_argument_read_occurrences)
      requirement_matches += same_fields(candidate, requirement);
    if (requirement_matches != 1)
      throw std::invalid_argument(
          requirement_matches
              ? "duplicate projected call-argument read requirement"
              : "unknown projected call-argument read requirement");
    const TypedProjectedCallArgumentReadFactV1* result = nullptr;
    std::size_t fact_matches = 0;
    for (const auto& candidate : projected_call_argument_read_facts) {
      if (!same_fields(candidate, requirement)) continue;
      result = &candidate;
      ++fact_matches;
    }
    if (fact_matches != 1 || !result)
      throw std::invalid_argument(
          fact_matches ? "duplicate projected call-argument read fact"
                       : "required projected call-argument read fact missing");
    if ((!result->is_authoritative() &&
         !result->is_intentionally_unsupported()) ||
        (!requirement.is_authoritative() &&
         !requirement.is_intentionally_unsupported()))
      throw std::invalid_argument(
          "required projected call-argument read fact is incomplete");
    return *result;
  }

  void require_all_phase28_authority_facts() const {
    if (conditional_collection_error_facts.size() !=
            required_conditional_collection_error_occurrences.size() ||
        return_value_facts.size() != required_return_value_occurrences.size() ||
        collection_view_facts.size() != required_collection_view_occurrences.size())
      throw std::invalid_argument("Phase 28 checker authority fact/requirement set is incomplete");
    for (const auto& requirement : required_conditional_collection_error_occurrences)
      (void)find_conditional_collection_error_fact(requirement);
    for (const auto& requirement : required_return_value_occurrences)
      (void)find_return_value_fact(requirement);
    for (const auto& requirement : required_collection_view_occurrences)
      (void)find_collection_view_fact(requirement);
    if (projected_collection_read_facts.size() !=
        required_projected_collection_read_occurrences.size())
      throw std::invalid_argument("projected collection-read fact/requirement set is incomplete");
    const auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return left == right ||
          (left && right && left->structurally_equal(*right));
    };
    const auto same_span = [](const est::SourceSpan& left,
                              const est::SourceSpan& right) {
      return left.line == right.line && left.column == right.column &&
          left.origin_id == right.origin_id &&
          left.source_begin == right.source_begin &&
          left.source_end == right.source_end;
    };
    const auto same_provenance = [](const TypedSourceProvenanceV1& left,
                                    const TypedSourceProvenanceV1& right) {
      return left.source_file == right.source_file &&
          left.line == right.line &&
          left.defining_module_id == right.defining_module_id &&
          left.enclosing_callable_id == right.enclosing_callable_id;
    };
    const auto same_fields = [&](const TypedProjectedCollectionReadFactV1& left,
                                 const TypedProjectedCollectionReadFactV1& right) {
      return left.availability == right.availability &&
          left.est_arena_index == right.est_arena_index &&
          left.map_index_expression_occurrence_id ==
              right.map_index_expression_occurrence_id &&
          left.member_projection_expression_occurrence_id ==
              right.member_projection_expression_occurrence_id &&
          left.query_source_expression_occurrence_id ==
              right.query_source_expression_occurrence_id &&
          same_span(left.projection_occurrence_span,
                    right.projection_occurrence_span) &&
          left.projection_statement_occurrence_id ==
              right.projection_statement_occurrence_id &&
          left.query_statement_occurrence_id == right.query_statement_occurrence_id &&
          left.receiver_binding_identity == right.receiver_binding_identity &&
          left.receiver_definition_identity == right.receiver_definition_identity &&
          same_type(left.receiver_type, right.receiver_type) &&
          same_type(left.key_type, right.key_type) &&
          same_type(left.indexed_value_type, right.indexed_value_type) &&
          same_type(left.projection_base_type, right.projection_base_type) &&
          same_type(left.projected_collection_type,
                    right.projected_collection_type) &&
          left.required_access == right.required_access &&
          left.temporary_non_escaping_query_borrow ==
              right.temporary_non_escaping_query_borrow &&
          left.specialization_identity == right.specialization_identity &&
          left.specialization_context == right.specialization_context &&
          left.est_fact_context == right.est_fact_context &&
          same_provenance(left.provenance, right.provenance);
    };
    for (const auto& requirement : required_projected_collection_read_occurrences) {
      std::size_t matching_requirements = 0;
      for (const auto& candidate : required_projected_collection_read_occurrences)
        matching_requirements += same_fields(candidate, requirement);
      std::size_t matching_facts = 0;
      for (const auto& candidate : projected_collection_read_facts)
        matching_facts += same_fields(candidate, requirement) &&
            candidate.is_authoritative();
      if (matching_requirements != 1 || matching_facts != 1 ||
          !requirement.is_authoritative())
        throw std::invalid_argument("projected collection-read fact is missing, duplicate, or contradictory");
    }
    if (projected_call_argument_read_facts.size() !=
        required_projected_call_argument_read_occurrences.size())
      throw std::invalid_argument(
          "projected call-argument read fact/requirement set is incomplete");
    for (const auto& requirement :
         required_projected_call_argument_read_occurrences)
      (void)find_projected_call_argument_read_fact(requirement);
  }

  const TypedMatchOwnershipFactV1* find_match_ownership_fact(
      const TypedMatchOwnershipRequirementV1& requirement) const {
    auto same_type = [](const TypeRef& left, const TypeRef& right) {
      return (left == right) || (left && right && left->structurally_equal(*right));
    };
    auto same_arm = [&](const TypedMatchArmFactV1& left,
                        const TypedMatchArmFactV1& right) {
      if (left.case_name != right.case_name ||
          left.payload_binding_names != right.payload_binding_names ||
          left.payload_binding_identities != right.payload_binding_identities ||
          left.payload_access != right.payload_access ||
          left.transfers_ownership != right.transfers_ownership ||
          left.payload_types.size() != right.payload_types.size())
        return false;
      for (std::size_t i = 0; i < left.payload_types.size(); ++i)
        if (!same_type(left.payload_types[i], right.payload_types[i]))
          return false;
      return true;
    };
    const TypedMatchOwnershipRequirementV1* required = nullptr;
    std::size_t matching_requirements = 0;
    for (const auto& candidate : required_match_occurrences) {
      if (candidate.est_arena_index != requirement.est_arena_index ||
          candidate.scrutinee_occurrence_id != requirement.scrutinee_occurrence_id ||
          candidate.statement_occurrence_id != requirement.statement_occurrence_id ||
          candidate.specialization_identity != requirement.specialization_identity ||
          candidate.specialization_context != requirement.specialization_context)
        continue;
      ++matching_requirements;
      required = &candidate;
    }
    if (matching_requirements != 1 || !required)
      throw std::invalid_argument(matching_requirements
          ? "duplicate or contradictory required match requirement"
          : "unknown required match requirement");

    const TypedMatchOwnershipFactV1* result = nullptr;
    std::size_t matching_facts = 0;
    for (const auto& fact : match_ownership_facts) {
      if (fact.est_arena_index != requirement.est_arena_index ||
          fact.scrutinee_occurrence_id != requirement.scrutinee_occurrence_id ||
          fact.statement_occurrence_id != requirement.statement_occurrence_id ||
          fact.specialization_identity != requirement.specialization_identity ||
          fact.specialization_context != requirement.specialization_context)
        continue;
      ++matching_facts;
      result = &fact;
    }
    if (matching_facts != 1 || !result) {
      if (matching_facts > 1)
        throw std::invalid_argument("duplicate or contradictory checked match facts");
      throw std::invalid_argument("required checked match fact missing");
    }
    if (result->is_consuming != requirement.is_consuming ||
        result->scrutinee_kind != requirement.scrutinee_kind ||
        result->scrutinee_binding_identity != requirement.scrutinee_binding_identity ||
        result->scrutinee_name != requirement.scrutinee_name ||
        !same_type(result->scrutinee_type, requirement.scrutinee_type) ||
        result->arms.size() != requirement.arms.size())
      throw std::invalid_argument("required checked match fact contradicts requirement");
    for (std::size_t i = 0; i < result->arms.size(); ++i)
      if (!same_arm(result->arms[i], requirement.arms[i]))
        throw std::invalid_argument(
            "required checked match arm fact contradicts requirement");
    if (!result->is_authoritative())
      throw std::invalid_argument("required checked match fact unavailable");
    return result;
  }

  const TypedMatchOwnershipRequirementV1& find_match_ownership_requirement(
      std::uint32_t arena_index, est::ExprId scrutinee_occurrence,
      std::uint32_t statement_occurrence,
      const std::string& specialization_context) const {
    const TypedMatchOwnershipRequirementV1* result = nullptr;
    std::size_t matches = 0;
    for (const auto& requirement : required_match_occurrences) {
      if (requirement.est_arena_index != arena_index ||
          requirement.scrutinee_occurrence_id != scrutinee_occurrence ||
          requirement.statement_occurrence_id != statement_occurrence ||
          requirement.specialization_context != specialization_context)
        continue;
      ++matches;
      result = &requirement;
    }
    if (matches != 1 || !result)
      throw std::invalid_argument(matches
          ? "ambiguous checker match requirement"
          : "checker match requirement missing");
    (void)find_match_ownership_fact(*result);
    return *result;
  }

  const TypedMatchOwnershipRequirementV1& find_match_ownership_requirement(
      std::uint32_t arena_index, est::ExprId scrutinee_occurrence,
      std::uint32_t statement_occurrence,
      const std::string& specialization_context,
      const std::string& specialization_identity) const {
    const auto& requirement = find_match_ownership_requirement(
        arena_index, scrutinee_occurrence, statement_occurrence, specialization_context);
    if (requirement.specialization_identity != specialization_identity)
      throw std::invalid_argument("checker match requirement specialization mismatch");
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
