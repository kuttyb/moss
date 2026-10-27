#pragma once

#include "ast.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace moss {

// Build a transient validation view for an imported function. Its serialized
// non-Method constraints remain intact, while the sealed typed summary replaces
// body-derived Method constraints. The imported Function itself is untouched.
inline std::optional<Function> imported_method_validation_view(
    const Function& function) {
  if (!function.open_summary ||
      function.open_summary->member_requirements.empty())
    return std::nullopt;

  Function view = function;
  view.constraints.erase(std::remove_if(
      view.constraints.begin(), view.constraints.end(),
      [](const Constraint& constraint) {
        return constraint.kind == ConstraintKind::Method;
      }), view.constraints.end());
  const auto& summary = *function.open_summary;
  for (std::size_t ordinal = 0;
       ordinal < summary.member_requirements.size(); ++ordinal) {
    const auto& member = summary.member_requirements[ordinal];
    std::optional<std::size_t> receiver_index;
    for (std::size_t index = 0; index < summary.parameter_types.size(); ++index) {
      if (!summary.parameter_types[index] || !member.receiver ||
          !summary.parameter_types[index]->structurally_equal(*member.receiver))
        continue;
      if (receiver_index)
        throw std::logic_error(
            "imported member receiver matches multiple parameters");
      receiver_index = index;
    }
    if (!receiver_index || *receiver_index >= function.params.size())
      throw std::logic_error(
          "imported member receiver is detached from parameters");
    Constraint requirement(ConstraintKind::Method,
        function.params[*receiver_index].name, member.member_name,
        "_method_result:" + std::to_string(ordinal));
    requirement.arity = member.argument_types.size();
    for (const auto& argument_type : member.argument_types) {
      std::optional<std::size_t> argument_index;
      for (std::size_t index = 0; index < summary.parameter_types.size(); ++index) {
        if (!summary.parameter_types[index] || !argument_type ||
            !summary.parameter_types[index]->structurally_equal(*argument_type))
          continue;
        if (argument_index)
          throw std::logic_error(
              "imported member argument matches multiple parameters");
        argument_index = index;
      }
      if (!argument_index || *argument_index >= function.params.size())
        throw std::logic_error(
            "imported member argument is detached from parameters");
      requirement.arguments.push_back(function.params[*argument_index].name);
    }
    view.constraints.push_back(std::move(requirement));
  }
  return view;
}

}  // namespace moss
