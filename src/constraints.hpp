#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace moss {

// Ownership/effect requirements inferred by the compiler.  These are an
// internal representation only; Moss source deliberately has no ownership
// annotations or reference syntax.
enum class Effect { Read, Write, Consume };

// Semantic requirements inferred from Moss expressions. These are compiler
// data, never source-level generic syntax.
enum class ConstraintKind { Operator, Method, Field, Indexable, Iterable, Callable, SameType, ResultType };
struct Constraint {
  ConstraintKind kind;
  std::string subject;
  std::string detail;
  std::string result;
  // Method constraints retain the full call shape. `arguments` are source
  // relationships re-typed for each concrete specialization; `result` links
  // a method result to its enclosing function result when needed.
  std::size_t arity = 0;
  std::vector<std::string> arguments;
  std::vector<std::string> result_expectations;

  Constraint(ConstraintKind constraint_kind, std::string constraint_subject,
             std::string constraint_detail, std::string constraint_result)
      : kind(constraint_kind), subject(std::move(constraint_subject)),
        detail(std::move(constraint_detail)), result(std::move(constraint_result)) {}
};

} // namespace moss
