#include "../../src/type_ir.hpp"
#include <cassert>
#include <stdexcept>
#include <string>

using moss::Type;
using moss::TypeKind;
using moss::TypeSolver;

int main() {
  auto nested = Type::parse_canonical("Vector[Box[value=Map[String,Vector[Cell[item=Int]]]]]");
  assert(nested && (*nested)->kind == TypeKind::Builtin);
  assert((*nested)->type_args[0]->kind == TypeKind::NominalInstance);
  assert((*nested)->type_args[0]->field_substitutions[0].second->kind == TypeKind::Builtin);
  assert((*nested)->structurally_equal(*Type::from_canonical_string(
      "vector[Box[value=map[string,vector[Cell[item=int]]]]]")));
  assert((*nested)->stable_type_key() == Type::from_canonical_string(
      "vector[Box[value=map[string,vector[Cell[item=int]]]]]")->stable_type_key());
  assert((*nested)->stable_type_key() != Type::from_canonical_string(
      "vector[Other[value=map[string,vector[Cell[item=int]]]]]")->stable_type_key());
  for (const std::string bad : {"Vector[Box[value=Int]", "Box[value=Int,,other=Int]",
        "Box[value=Int,other]", "Box[value=Int,value=String]", "Vector[Box[value=]]",
        "Vector[Int]junk", "_typevar:1:2:x", "Map[Int,Box[value=String]]]"})
    assert(!Type::parse_canonical(bad));

  TypeSolver solver(7);
  auto first = solver.fresh(1);
  auto second = solver.fresh(1);
  assert(!first->structurally_equal(*second));
  assert(solver.unify(first, Type::builtin("vector", {Type::primitive("int")})));
  assert(solver.resolve(first)->structurally_equal(*Type::builtin("vector", {Type::primitive("int")})));
  assert(!solver.unify(second, Type::builtin("vector", {second})));
  assert(!solver.unify(first, Type::builtin("vector", {Type::primitive("string")})));
  assert(solver.unify(second, Type::nominal("pkg@1::m.Box", {{"value", Type::primitive("int")}})));
  assert(!solver.unify(second, Type::nominal("pkg@1::m.Cell", {{"value", Type::primitive("int")}})));
  bool open_key_rejected = false;
  try { (void)solver.fresh(2)->stable_type_key(); }
  catch (const std::invalid_argument&) { open_key_rejected = true; }
  assert(open_key_rejected);
}
