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
  auto session = std::make_shared<TypeContext>();
  TypeContext& context = *session;
  context.declare(record("Box", app, "Box", {{"value", "", true}}));
  context.declare(record("Cell", app, "Cell", {{"item", "", true}}));
  context.declare(record("Other", app, "Other", {{"value", "", true}}));
  context.declare(record("Pt", app, "Pt", {{"x", "int", false}}));
  context.declare(record("Pair", app, "Pair", {{"a", "", true}, {"b", "", true}}));
  context.declare(record("Node", app, "Node", {{"children", "vector[Node]", false}}));
  context.declare(record("Alpha__Token", alpha, "Token", {{"id", "int", false}}));
  context.declare(record("Beta__Token", beta, "Token", {{"id", "int", false}}));
  context.bind_field_annotations();
  const auto& recursive_field = context.field_annotations(context.find("Node")->id);
  assert(recursive_field.size() == 1 && recursive_field[0]);
  assert(recursive_field[0]->type_args[0]->declaration == context.find("Node"));
  assert(context.field_annotations(context.find("Box")->id)[0] == nullptr);
  assert(context.find("Box")->interned_module_id == context.find("Cell")->interned_module_id);
  assert(context.find("Box")->interned_module_id == context.find_module(app));
  assert(context.find("Alpha__Token")->interned_module_id !=
         context.find("Beta__Token")->interned_module_id);
  assert(throws_invalid([&] { (void)context.intern_module("Alpha"); }));
  assert(throws_invalid([&] { (void)context.intern_module("module-v1:@1.0::Alpha"); }));
  assert(throws_invalid([&] { (void)context.intern_module("module-v1:app@::Alpha"); }));
  assert(throws_invalid([&] { (void)context.intern_module("module-v1:app@1.0::"); }));
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
  // Distinct lookup aliases for one exact declaration ID share the interned
  // object. A conflicting field contract cannot hide behind that same ID.
  auto box_alias = context.declare(record("Alias__Box", app, "Box",
                                          {{"value", "", true}}));
  assert(box_alias == context.find("Box"));
  assert(context.find("Alias__Box") == context.find_id(box_alias->id));
  assert(throws_invalid([&] {
    context.declare(record("OtherAlias__Box", app, "Box",
                           {{"value", "string", false}}));
  }));
  assert(throws_invalid([&] {
    context.declare(record("Box", app, "Box",
                           {{"other", "", true}}));
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
    auto empty = std::make_shared<TypeContext>();
    TypeContext::Scope none(empty.get());
    auto unbound = Type::from_canonical_string("Box[value=int]");
    assert(unbound->kind == TypeKind::NominalInstance && !unbound->declaration);
    assert(!unbound->is_closed());
    assert(throws_invalid([&] { (void)unbound->stable_type_key(); }));
    assert(!unbound->structurally_equal(*box));
    assert(!unbound->structurally_equal(*Type::from_canonical_string("Box[value=int]")));
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

  // A specialization hashes the resolved callable and every ordered binding.
  moss::SpecializationKey instance;
  instance.callable_id = "callable-v1:" + app + ":fn:unwrap";
  instance.arguments = {box};
  auto base_key = instance.stable_key();
  assert(base_key && base_key->find("specialization-v2:sha256:") == 0);
  instance.callable_id = "callable-v1:" + beta + ":fn:unwrap";
  assert(instance.stable_key() != base_key);
  instance.callable_id = "callable-v1:" + app + ":fn:unwrap";
  instance.receiver = box;
  assert(instance.stable_key() != base_key);
  instance.receiver.reset();
  instance.callable_arguments = {"callable-v1:" + app + ":fn:predicate"};
  assert(instance.stable_key() != base_key);
  instance.callable_arguments.clear();
  instance.open_result_bindings = {{0, Type::primitive("int")}};
  assert(instance.stable_key() != base_key);
  instance.open_result_bindings = {{0, solver.fresh(3)}};
  assert(!instance.stable_key());
  instance.open_result_bindings = {{1, Type::primitive("int")},
                                   {0, Type::primitive("string")}};
  assert(!instance.stable_key());
  instance.open_result_bindings = {{0, Type::primitive("int")},
                                   {0, Type::primitive("string")}};
  assert(!instance.stable_key());

  moss::MemberDispatchRequirement member;
  member.requirement_id = "requirement-v1:push";
  member.receiver = Type::builtin("vector", {Type::primitive("int")});
  member.member_name = "push";
  member.argument_types = {Type::primitive("int")};
  member.result = Type::unit();
  member.receiver_access = moss::AccessMode::Write;
  member.argument_access = {moss::AccessMode::Read};
  member.admitted_template_or_builtin_family = "builtin:vector";
  member.selected_callable_id = "builtin:vector.push";
  assert(moss::member_requirement_is_closed(member));
  assert(member.receiver_access == moss::AccessMode::Write &&
         member.argument_access[0] == moss::AccessMode::Read);
  auto open_item = solver.fresh(4);
  member.receiver = Type::builtin("vector", {open_item});
  member.argument_types = {open_item};
  assert(!moss::member_requirement_is_closed(member));
  assert(solver.unify(open_item, Type::primitive("int")));
  auto resolved_member = moss::resolve_member_requirement(solver, member);
  assert(moss::member_requirement_is_closed(resolved_member));

  // Admission is policy owned, while the solver applies contributions and
  // refuses to close unresolved or contradictory facts.
  TypeSolver member_solver(8);
  auto element = member_solver.fresh(1);
  moss::MemberDispatchRequirement open_push;
  open_push.requirement_id = "requirement-v1:open-push";
  open_push.receiver = Type::builtin("vector", {element});
  open_push.member_name = "push";
  open_push.argument_types = {Type::primitive("int")};
  open_push.result = Type::unit();
  open_push.origin = {app, "open.moss", 4, 3, "fn:main", "call:4:3"};
  auto policy = [&](const moss::MemberDispatchRequirement& fact) {
    moss::MemberAdmission answer;
    assert(fact.origin.source_file == "open.moss");
    answer.status = moss::MemberAdmission::Status::Admitted;
    answer.admitted_template_or_builtin_family = "builtin:vector";
    answer.selected_callable_id = "builtin:vector.push";
    answer.receiver_access = moss::AccessMode::Write;
    answer.argument_access = {moss::AccessMode::Read};
    answer.result = Type::unit();
    answer.contributes = {{fact.receiver->type_args[0], fact.argument_types[0]}};
    return answer;
  };
  auto closure = moss::close_member_requirements(member_solver, {open_push}, policy);
  assert(closure.status == moss::MemberClosureResult::Status::Closed);
  assert(closure.requirements[0].origin.call_expression_id == "call:4:3");
  assert(closure.requirements[0].receiver_access == moss::AccessMode::Write);
  assert(closure.requirements[0].argument_access[0] == moss::AccessMode::Read);
  assert(closure.requirements[0].receiver->type_args[0]->name == "int");
  auto conflicting = open_push;
  conflicting.argument_types = {Type::primitive("string")};
  auto failed = moss::close_member_requirements(member_solver, {conflicting}, policy);
  assert(failed.status == moss::MemberClosureResult::Status::UnificationFailed);
  auto deferred = moss::close_member_requirements(member_solver, {open_push},
      [](const moss::MemberDispatchRequirement&) {
        return moss::MemberAdmission{};
      });
  assert(deferred.status == moss::MemberClosureResult::Status::Deferred);
}
