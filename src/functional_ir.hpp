#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "constraints.hpp"

namespace moss {

// Functional/dataflow structure remains a Moss compiler concept until it is
// either materialized as a value or lowered to an explicit loop.  In
// particular, these nodes are not Rust iterator objects and cannot escape at
// runtime.
enum class FunctionalNodeKind {
  Source,
  Map,
  Filter,
  Reduce,
  Sum,
  Count,
  Any,
  All
};

// Fusion answers whether stages may share a traversal. Materialization is a
// separate plan for the logical collection produced by a transformation.
enum class FunctionalMaterializationKind {
  NotApplicable,
  Virtual,
  Materialize,
  ReusableStorageCandidate
};

struct FunctionalSourceSpan {
  int line = 0;
  std::size_t stage = 0;
};

// Ownership effects answer how a value is accessed.  These facts answer
// whether changing eager stage ordering could be observed.  They deliberately
// stay independent so future optimizers can consume both analyses.
struct ObservableEffects {
  bool local_capture_read = false;
  bool local_mutation = false;
  bool domain_read = false;
  bool domain_write = false;
  bool message = false;
  bool await = false;
  bool external_io = false;
  bool may_fail = false;
  // Termination is independent of failure and externally visible effects.
  // Phase 4.5 currently treats any reachable Moss `while` as potentially
  // divergent; ordinary fusion may preserve it, but work-skipping transforms
  // must not omit it.
  bool may_diverge = false;
  bool unresolved = true;

  bool fusion_safe() const {
    return !local_mutation && !domain_read && !domain_write && !message &&
           !await && !external_io && !may_fail && !unresolved;
  }

  bool deterministic() const {
    return fusion_safe();
  }

  bool safe_to_skip() const {
    return fusion_safe() && !may_diverge;
  }

  void merge(const ObservableEffects& other) {
    local_capture_read = local_capture_read || other.local_capture_read;
    local_mutation = local_mutation || other.local_mutation;
    domain_read = domain_read || other.domain_read;
    domain_write = domain_write || other.domain_write;
    message = message || other.message;
    await = await || other.await;
    external_io = external_io || other.external_io;
    may_fail = may_fail || other.may_fail;
    may_diverge = may_diverge || other.may_diverge;
    unresolved = unresolved || other.unresolved;
  }
};

struct FunctionalNode {
  // Numeric IDs are dense, compilation-local handles.  They are useful for
  // exact semantic-analysis/codegen handoff, but deliberately are not source
  // identities and may change when unrelated pipelines are added.
  std::size_t transient_id = 0;
  // This identity is derived from the owning semantic context and source
  // occurrence, so provenance does not depend on traversal numbering.
  std::string semantic_identity;
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  FunctionalSourceSpan span;
  std::string source_text;
  std::string input_type;
  std::string output_type;
  std::string callable_identity;
  std::string callable_expression;
  std::vector<std::string> captures;
  Effect ownership = Effect::Read;
  ObservableEffects effects;
  bool logical_materialization = false;
  bool materialization_eliminated = false;
  FunctionalMaterializationKind materialization =
      FunctionalMaterializationKind::NotApplicable;
  bool escapes = false;
  bool multiple_consumers = false;
  bool barrier_required = false;
  bool dead_stage_eliminated = false;
  std::string materialization_reason;
  std::vector<std::string> provenance;
};

struct FunctionalPipeline {
  // Exact compilation-local plan handle carried by the checked AST.
  std::size_t transient_id = 0;
  std::string semantic_identity;
  int line = 0;
  std::string context;
  std::string expression;
  std::string source_expression;
  std::string source_type;
  std::string output_type;
  std::vector<FunctionalNode> nodes;
  bool fusion_eligible = false;
  bool element_independent = false;
  bool deterministic = false;
  bool reduction_compatible = false;
  bool fused = false;
  bool count_uses_exact_length = false;
  bool short_circuit_terminal = false;
  // Scope-level dataflow links. A producer virtualized into one consumer is
  // omitted physically, while the consumer lowers the combined pipeline.
  std::size_t virtual_upstream_pipeline_id = 0;
  std::size_t virtualized_into_pipeline_id = 0;
  std::size_t traversal_group_id = 0;
  std::string binding_name;
  bool binding_immutable = false;
  std::size_t binding_use_count = 0;
  std::string binding_materialization_reason;
  std::vector<std::string> lowered_provenance;
  std::vector<std::string> optimization_notes;
  std::string decision;
};

struct FunctionalTraversalConsumer {
  std::size_t pipeline_id = 0;
  std::string result_binding;
  int line = 0;
  bool mutable_binding = false;
};

// A scope-level dataflow DAG with one stable source and two or more terminal
// consumers. Each consumer keeps its original pipeline identity and
// provenance; this node only records their shared physical traversal.
struct FunctionalTraversalGroup {
  std::size_t transient_id = 0;
  std::string semantic_identity;
  std::string context;
  int line = 0;
  std::string source_expression;
  std::string source_type;
  std::vector<FunctionalTraversalConsumer> consumers;
  std::vector<std::string> provenance;
  std::string decision;
};

inline const char* functional_node_name(FunctionalNodeKind kind) {
  switch (kind) {
    case FunctionalNodeKind::Source: return "Source";
    case FunctionalNodeKind::Map: return "Map";
    case FunctionalNodeKind::Filter: return "Filter";
    case FunctionalNodeKind::Reduce: return "Reduce";
    case FunctionalNodeKind::Sum: return "Sum";
    case FunctionalNodeKind::Count: return "Count";
    case FunctionalNodeKind::Any: return "Any";
    case FunctionalNodeKind::All: return "All";
  }
  return "Unknown";
}

inline const char* functional_materialization_name(
    FunctionalMaterializationKind kind) {
  switch (kind) {
    case FunctionalMaterializationKind::NotApplicable: return "not-applicable";
    case FunctionalMaterializationKind::Virtual: return "virtual";
    case FunctionalMaterializationKind::Materialize: return "materialized";
    case FunctionalMaterializationKind::ReusableStorageCandidate:
      return "reusable-storage-candidate";
  }
  return "unknown";
}

}  // namespace moss
