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
  bool unresolved = true;

  bool fusion_safe() const {
    return !local_mutation && !domain_read && !domain_write && !message &&
           !await && !external_io && !may_fail && !unresolved;
  }

  bool deterministic() const {
    return fusion_safe();
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
    unresolved = unresolved || other.unresolved;
  }
};

struct FunctionalNode {
  std::size_t id = 0;
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
  std::vector<std::size_t> provenance;
};

struct FunctionalPipeline {
  std::size_t id = 0;
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
  std::vector<std::size_t> lowered_provenance;
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

}  // namespace moss
