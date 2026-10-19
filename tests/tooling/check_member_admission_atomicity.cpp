#include "../../src/type_ir.hpp"
#include <cassert>

int main() {
  auto admitted = [] {
    moss::MemberAdmission admission;
    admission.status = moss::MemberAdmission::Status::Admitted;
    admission.admitted_template_or_builtin_family = "test-member";
    admission.selected_callable_id = "callable-v1:test";
    return admission;
  };

  // Contradictory contributions reject atomically, while a prior unrelated
  // binding remains committed.
  moss::TypeSolver contradictory_solver(1);
  auto T = contradictory_solver.fresh(1);
  auto unrelated = contradictory_solver.fresh(1);
  assert(contradictory_solver.unify(
      unrelated, moss::Type::primitive("bool")));
  moss::MemberDispatchRequirement contradictory;
  contradictory.requirement_id = "contradictory-member";
  auto contradictory_policy = [&](const moss::MemberDispatchRequirement&) {
    auto admission = admitted();
    admission.contributes = {
        {T, moss::Type::primitive("int")},
        {T, moss::Type::primitive("string")},
    };
    return admission;
  };
  auto failed = moss::close_member_requirements(
      contradictory_solver, {contradictory}, contradictory_policy);
  assert(failed.status ==
         moss::MemberClosureResult::Status::UnificationFailed);
  assert(contradictory_solver.resolve(T)->kind ==
         moss::TypeKind::InferenceVar);
  assert(contradictory_solver.resolve(unrelated)->to_canonical_string() ==
         "bool");

  // Every equality in a successful admission is committed.
  moss::TypeSolver successful_solver(2);
  auto A = successful_solver.fresh(1);
  auto B = successful_solver.fresh(1);
  moss::MemberDispatchRequirement successful;
  successful.requirement_id = "successful-member";
  successful.receiver = moss::Type::builtin("vector", {A});
  successful.argument_types = {B};
  successful.argument_access = {moss::AccessMode::Read};
  successful.result = moss::Type::unit();
  auto successful_policy = [&](const moss::MemberDispatchRequirement&) {
    auto admission = admitted();
    admission.argument_access = {moss::AccessMode::Read};
    admission.result = moss::Type::unit();
    admission.contributes = {
        {A, moss::Type::primitive("int")},
        {B, moss::Type::primitive("string")},
    };
    return admission;
  };
  auto closed = moss::close_member_requirements(
      successful_solver, {successful}, successful_policy);
  assert(closed.status == moss::MemberClosureResult::Status::Closed);
  assert(successful_solver.resolve(A)->to_canonical_string() == "int");
  assert(successful_solver.resolve(B)->to_canonical_string() == "string");

  // A result conflict rolls back contributions made earlier in the same
  // admission and preserves the pre-existing result binding.
  moss::TypeSolver result_solver(3);
  auto C = result_solver.fresh(1);
  auto expected = result_solver.fresh(1);
  assert(result_solver.unify(expected, moss::Type::primitive("bool")));
  moss::MemberDispatchRequirement result_conflict;
  result_conflict.requirement_id = "result-conflict-member";
  result_conflict.result = expected;
  auto result_policy = [&](const moss::MemberDispatchRequirement&) {
    auto admission = admitted();
    admission.result = moss::Type::primitive("string");
    admission.contributes = {{C, moss::Type::primitive("int")}};
    return admission;
  };
  auto result_failed = moss::close_member_requirements(
      result_solver, {result_conflict}, result_policy);
  assert(result_failed.status ==
         moss::MemberClosureResult::Status::UnificationFailed);
  assert(result_solver.resolve(C)->kind == moss::TypeKind::InferenceVar);
  assert(result_solver.resolve(expected)->to_canonical_string() == "bool");
}
