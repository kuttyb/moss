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
    if (inference_status != InferenceStatus::Inferred ||
        availability != Availability::Present ||
        definition_identity.empty() || specialization_context.empty() ||
        statement_occurrence_id == UINT32_MAX ||
        est_arena_index == UINT32_MAX ||
        expression_occurrence_id == UINT32_MAX)
      return false;
    switch (source_kind) {
      case SourceKind::LocalBinding:
        return !binding_identity.empty() && !source_binding_identity.empty();
      case SourceKind::VectorElement:
        return !source_binding_identity.empty();
      case SourceKind::FreshOwnedValue:
        return !binding_identity.empty() && source_binding_identity.empty() &&
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
  Effect access{Effect::Read};
  SourceKind source_kind{SourceKind::LocalBinding};
  InferenceStatus inference_status{InferenceStatus::Inferred};
  UnsupportedReason unsupported_reason{UnsupportedReason::None};
  Availability availability{Availability::MissingEstOccurrence};
};

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
  std::vector<TypedLoopOwnershipFactV1> loop_ownership_facts;
  std::vector<TypedLoopOwnershipRequirementV1>
      required_loop_ownership_occurrences;
  std::vector<TypedExceptionalOwnershipFactV1> exceptional_ownership_facts;
  std::vector<TypedExceptionalOwnershipRequirementV1>
      required_exceptional_ownership_occurrences;

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
  std::vector<TypedMatchOwnershipFactV1> match_ownership_facts;
  std::vector<TypedMatchOwnershipRequirementV1> required_match_occurrences;
  std::vector<TypedEchoArgumentFactV1> echo_argument_facts;
  std::vector<TypedEchoArgumentRequirementV1>
      required_echo_argument_occurrences;
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
          fact.binding_identity != req.binding_identity ||
          fact.killed_definition_identity != req.killed_definition_identity ||
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
        !same_local_ownership_type(result->value_type, req.value_type) ||
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
