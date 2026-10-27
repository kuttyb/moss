#include "imported_method_validation.hpp"

#include <cassert>
#include <iostream>

int main() {
  moss::Function imported;
  imported.source_file = "provider.mossi";
  moss::Param receiver;
  receiver.name = "value";
  receiver.type = "string";
  imported.params.push_back(receiver);
  moss::Param operand;
  operand.name = "offset";
  operand.type = "int";
  imported.params.push_back(operand);
  imported.constraints.emplace_back(
      moss::ConstraintKind::Operator, "offset", "+", "offset");
  imported.constraints.emplace_back(
      moss::ConstraintKind::Method, "value", "forged_member", "");

  moss::OpenCallableSummaryFacts summary;
  summary.parameter_types = {
      moss::Type::primitive("string"), moss::Type::primitive("int")};
  moss::MemberDispatchRequirement member;
  member.member_name = "length";
  member.receiver = summary.parameter_types.front();
  summary.member_requirements.push_back(std::move(member));
  imported.open_summary = std::move(summary);

  auto view = moss::imported_method_validation_view(imported);
  assert(view);
  assert(imported.constraints.size() == 2);
  assert(view->constraints.size() == 2);
  assert(view->constraints[0].kind == moss::ConstraintKind::Operator);
  assert(view->constraints[0].subject == "offset");
  assert(view->constraints[0].detail == "+");
  assert(view->constraints[1].kind == moss::ConstraintKind::Method);
  assert(view->constraints[1].subject == "value");
  assert(view->constraints[1].detail == "length");
  std::cout << "PASS mixed imported validation constraints preserved\n";
}
