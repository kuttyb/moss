#include "../../src/type_ir.hpp"
#include <cassert>

int main() {
  moss::TypeSolver solver(1);

  auto T = solver.fresh(1);
  auto unrelated = solver.fresh(1);
  assert(solver.unify(unrelated, moss::Type::primitive("bool")));

  auto lhs = moss::Type::builtin("map", {T, T});
  auto rhs = moss::Type::builtin("map", {
      moss::Type::primitive("int"),
      moss::Type::primitive("string")
  });

  // Contradictory constraints must fail.
  assert(!solver.unify(lhs, rhs));

  // Did failed unification leave T bound to Int?
  assert(solver.resolve(T)->kind == moss::TypeKind::InferenceVar);

  // The failed operation must not prevent a later valid constraint.
  assert(solver.unify(T, moss::Type::primitive("string")));
  assert(solver.resolve(T)->to_canonical_string() == "string");

  auto U = solver.fresh(1);
  auto V = solver.fresh(1);
  moss::Requirement require_u;
  require_u.id = "require-u";
  moss::Requirement require_v;
  require_v.id = "require-v";
  solver.require(U->var_id, require_u);
  solver.require(V->var_id, require_v);
  auto W = solver.fresh(1);
  auto nested_lhs = moss::Type::builtin("tuple", {U, W, W});
  auto nested_rhs = moss::Type::builtin("tuple", {
      V, moss::Type::primitive("int"), moss::Type::primitive("string")
  });
  assert(!solver.unify(nested_lhs, nested_rhs));
  assert(solver.resolve(U)->kind == moss::TypeKind::InferenceVar);
  assert(solver.resolve(V)->kind == moss::TypeKind::InferenceVar);
  assert(solver.resolve(W)->kind == moss::TypeKind::InferenceVar);
  assert(solver.requirements(U->var_id).size() == 1);
  assert(solver.requirements(U->var_id).front().id == "require-u");
  assert(solver.requirements(V->var_id).size() == 1);
  assert(solver.requirements(V->var_id).front().id == "require-v");

  moss::TypeSolver successful(2);
  auto S = successful.fresh(1);
  auto same = moss::Type::builtin("map", {S, S});
  auto matching = moss::Type::builtin("map", {
      moss::Type::primitive("int"),
      moss::Type::primitive("int")
  });
  assert(successful.unify(same, matching));
  assert(successful.resolve(S)->to_canonical_string() == "int");

  // A prior successful binding remains after a later contradiction.
  assert(solver.resolve(unrelated)->to_canonical_string() == "bool");
}
