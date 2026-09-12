#pragma once

#include <string>
#include <vector>

namespace moss {

// Semantic requirements inferred from Moss expressions. These are compiler
// data, never source-level generic syntax.
enum class ConstraintKind { Operator, Method, Field, Indexable, Iterable, Callable, SameType, ResultType };
struct Constraint {
  ConstraintKind kind;
  std::string subject;
  std::string detail;
  std::string result;
};

} // namespace moss
