#include "../../src/type_ir.hpp"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>

using moss::DeclarationKind;
using moss::NominalDeclaration;
using moss::Type;
using moss::TypeContext;
using moss::TypeKind;
using moss::TypeSolver;

static NominalDeclaration record(const std::string& program_name,
                                 const std::string& module_id,
                                 const std::string& name,
                                 std::vector<moss::NominalFieldSlot> fields) {
  NominalDeclaration declaration;
  declaration.kind = DeclarationKind::Record;
  declaration.program_name = program_name;
  declaration.module_id = module_id;
  declaration.declaration_name = name;
  declaration.fields = std::move(fields);
  return declaration;
}

static bool throws_invalid(const std::function<void()>& operation) {
  try { operation(); } catch (const std::invalid_argument&) { return true; }
  return false;
}

int main() {
  const std::string app = moss::module_identity("app", "1.0.0", "main");
  const std::string alpha = moss::module_identity("alpha", "1.0.0", "Alpha");
  const std::string beta = moss::module_identity("beta", "2.0.0", "Beta");
  TypeContext context;
  context.declare(record("Box", app, "Box", {{"value", "", true}}));
  context.declare(record("Cell", app, "Cell", {{"item", "", true}}));
  context.declare(record("Other", app, "Other", {{"value", "", true}}));
  context.declare(record("Pt", app, "Pt", {{"x", "int", false}}));
  context.declare(record("Pair", app, "Pair", {{"a", "", true}, {"b", "", true}}));
  context.declare(record("Node", app, "Node", {{"children", "vector[Node]", false}}));
  context.declare(record("Alpha__Token", alpha, "Token", {{"id", "int", false}}));
  context.declare(record("Beta__Token", beta, "Token", {{"id", "int", false}}));
  TypeContext::Scope scope(&context);

  // Canonical identities follow TYPE_IR_CONTRACT §3.1.
  assert(context.find("Alpha__Token")->id ==
         "nominal-template-v1:module-v1:alpha@1.0.0::Alpha:type:Token");
  assert(context.find_id("nominal-template-v1:module-v1:alpha@1.0.0::Alpha:type:Token") ==
         context.find("Alpha__Token"));
  // Re-declaring a program name with another identity is rejected.
  assert(throws_invalid([&] {
    context.declare(record("Box", beta, "Box", {{"value", "", true}}));
  }));

  auto nested = Type::parse_canonical("Vector[Box[value=Map[String,Vector[Cell[item=Int]]]]]");
  assert(nested && (*nested)->kind == TypeKind::Builtin);
  assert((*nested)->type_args[0]->kind == TypeKind::NominalInstance);
  assert((*nested)->type_args[0]->declaration == context.find("Box"));
  assert((*nested)->type_args[0]->field_substitutions[0].second->kind == TypeKind::Builtin);
  assert((*nested)->structurally_equal(*Type::from_canonical_string(
      "vector[Box[value=map[string,vector[Cell[item=int]]]]]")));
  assert((*nested)->stable_type_key() == Type::from_canonical_string(
      "vector[Box[value=map[string,vector[Cell[item=int]]]]]")->stable_type_key());
  // Same layout, different declaration: distinct types and keys.
  assert((*nested)->stable_type_key() != Type::from_canonical_string(
      "vector[Other[value=map[string,vector[Cell[item=int]]]]]")->stable_type_key());
  auto box = Type::from_canonical_string("Box[value=int]");
  auto other = Type::from_canonical_string("Other[value=int]");
  assert(!box->structurally_equal(*other));
  assert(box->nominal_instance_key() != other->nominal_instance_key());
  // Same declaration, different resolved field types: distinct instance keys.
  assert(box->nominal_instance_key() !=
         Type::from_canonical_string("Box[value=string]")->nominal_instance_key());
  // Same declaration name in two packages: distinct identities.
  auto alpha_token = Type::from_canonical_string("Alpha__Token");
  auto beta_token = Type::from_canonical_string("Beta__Token");
  assert(alpha_token->is_closed() && beta_token->is_closed());
  assert(!alpha_token->structurally_equal(*beta_token));
  assert(alpha_token->stable_type_key() != beta_token->stable_type_key());
  // Substitution spelling order does not change instance identity.
  auto ab = Type::from_canonical_string("Pair[a=int,b=string]");
  auto ba = Type::from_canonical_string("Pair[b=string,a=int]");
  assert(ab->structurally_equal(*ba));
  assert(ab->nominal_instance_key() == ba->nominal_instance_key());
  // A fully declared record is closed in its bare spelling; a template with an
  // inferred slot is a known-open template until every slot is substituted.
  assert(Type::from_canonical_string("Pt")->is_closed());
  assert(!Type::from_canonical_string("Box")->is_closed());
  assert(Type::from_canonical_string("Box")->is_bare_template());
  assert(!Type::from_canonical_string("Pair[a=int]")->is_closed());
  assert(!Type::from_canonical_string("Box[value=int,extra=int]")->is_closed());
  // A recursive layout through a collection has a terminating key.
  auto node = Type::from_canonical_string("Node");
  assert(node->is_closed());
  assert(node->stable_type_key() == Type::from_canonical_string("Node")->stable_type_key());
  // Outside any declaring session a nominal name is unbound: never closed.
  {
    TypeContext empty;
    TypeContext::Scope none(&empty);
    auto unbound = Type::from_canonical_string("Box[value=int]");
    assert(unbound->kind == TypeKind::NominalInstance && !unbound->declaration);
    assert(!unbound->is_closed());
    assert(throws_invalid([&] { (void)unbound->stable_type_key(); }));
    assert(!unbound->structurally_equal(*box));
  }

  for (const std::string bad : {"Vector[Box[value=Int]", "Box[value=Int,,other=Int]",
        "Box[value=Int,other]", "Box[value=Int,value=String]", "Vector[Box[value=]]",
        "Vector[Int]junk", "_typevar:1:2:x", "Map[Int,Box[value=String]]]"})
    assert(!Type::parse_canonical(bad));

  // Built-in collection arity: a parameterized collection names every type
  // argument, at any nesting depth; the bare spelling is the open form.
  for (const std::string partial : {"map[int]", "Map[String]", "vector[int,string]",
        "Queue[Int,Int]", "map[int,string,bool]", "vector[map[int]]",
        "Map[String,Vector[Int,Int]]", "Box[value=map[int]]"})
    assert(!Type::parse_canonical(partial));
  for (const std::string open : {"vector", "queue", "map", "vector[map]",
        "map[string,vector]", "Box[value=vector]"}) {
    auto parsed = Type::parse_canonical(open);
    assert(parsed && !(*parsed)->is_closed());
  }
  for (const std::string closed : {"vector[int]", "queue[string]", "map[int,string]",
        "Box[value=map[string,vector[int]]]"}) {
    auto parsed = Type::parse_canonical(closed);
    assert(parsed && (*parsed)->is_closed());
  }
  assert(!Type::builtin("map", {Type::primitive("int")})->is_closed());
  assert(!Type::builtin("vector", {Type::primitive("int"), Type::primitive("int")})->is_closed());

  TypeSolver solver(7);
  auto first = solver.fresh(1);
  auto second = solver.fresh(1);
  assert(!first->structurally_equal(*second));
  assert(solver.unify(first, Type::builtin("vector", {Type::primitive("int")})));
  assert(solver.resolve(first)->structurally_equal(*Type::builtin("vector", {Type::primitive("int")})));
  assert(!solver.unify(second, Type::builtin("vector", {second})));
  assert(!solver.unify(first, Type::builtin("vector", {Type::primitive("string")})));
  // The solver unifies nominal instances by declaration identity, not layout.
  assert(solver.unify(second, Type::nominal("Box", {{"value", Type::primitive("int")}})));
  assert(!solver.unify(second, Type::nominal("Other", {{"value", Type::primitive("int")}})));
  assert(solver.resolve(second)->declaration == context.find("Box"));
  assert(throws_invalid([&] { (void)solver.fresh(2)->stable_type_key(); }));
}
