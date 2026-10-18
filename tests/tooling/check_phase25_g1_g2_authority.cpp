#include "../../src/ast.hpp"
#include "../../src/type_ir.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace moss;

static NominalDeclaration make_record(const std::string& program_name,
                                      const std::string& module_id,
                                      const std::string& name,
                                      std::vector<NominalFieldSlot> fields) {
  NominalDeclaration d;
  d.kind = DeclarationKind::Record;
  d.program_name = program_name;
  d.module_id = module_id;
  d.declaration_name = name;
  d.fields = std::move(fields);
  return d;
}

int main() {
  std::cout << "Testing G2 Declaration Authority & Nominals..." << std::endl;

  // 1. Unknown nominal cannot be silently interned as primitive when TypeContext::current() is absent
  assert(TypeContext::current() == nullptr);
  auto uninterned = Type::parse_canonical("Person");
  assert(!uninterned.has_value());
  uninterned = Type::parse_canonical("Order");
  assert(!uninterned.has_value());

  bool threw_invalid = false;
  try {
    Type::from_canonical_string("Person");
  } catch (const std::invalid_argument&) {
    threw_invalid = true;
  }
  assert(threw_invalid);

  // Syntax validation without symbol lookup
  assert(Type::is_well_formed_syntax("Person"));
  assert(Type::is_well_formed_syntax("vector[int]"));
  assert(Type::is_well_formed_syntax("map[string, int]"));
  assert(!Type::is_well_formed_syntax("vector["));
  assert(!Type::is_well_formed_syntax("vector[int, int]")); // arity mismatch
  assert(!Type::is_well_formed_syntax("map[int]"));          // arity mismatch

  // 2. Post-registration nominal resolution
  const std::string app_mod = module_identity("app", "1.0.0", "main");
  auto context = std::make_shared<TypeContext>();
  context->declare(make_record("Person", app_mod, "Person", {{"name", "string", false}, {"age", "int", false}}));
  context->register_trait("Costed");
  context->bind_field_annotations();

  {
    TypeContext::Scope scope(context.get());
    assert(TypeContext::current() == context.get());

    auto resolved_person = Type::parse_canonical("Person");
    assert(resolved_person.has_value());
    assert((*resolved_person)->kind == TypeKind::NominalInstance);
    assert((*resolved_person)->declaration == context->find("Person"));

    auto resolved_trait = Type::parse_canonical("Costed");
    assert(resolved_trait.has_value());
    assert((*resolved_trait)->kind == TypeKind::TraitConstraint);

    // Unknown nominal still returns nullopt
    assert(!Type::parse_canonical("UnknownType").has_value());
  }

  // 3. AST Declaration TypeRef Authority and Fail-Closed Contradiction Rejection
  {
    // Param
    Param p("x", "int");
    assert(p.name == "x");
    p.assert_consistent();

    auto int_ref = Type::primitive("int");
    auto str_ref = Type::primitive("string");

    p.bind_type(int_ref);
    assert(p.type == "int");
    assert(p.type_ref == int_ref);
    p.assert_consistent();

    // Contradictory projection throws std::logic_error
    p.type = "string"; // contradictory with bound int_ref
    bool caught_param = false;
    try {
      p.assert_consistent();
    } catch (const std::logic_error& e) {
      caught_param = true;
    }
    assert(caught_param);
  }

  {
    // Method
    Method m;
    m.owner = "Account";
    m.name = "balance";
    m.assert_consistent();

    auto int_ref = Type::primitive("int");
    auto bool_ref = Type::primitive("bool");

    m.bind_return_type(int_ref);
    assert(m.return_type == "int");
    assert(m.return_type_ref == int_ref);
    m.assert_consistent();

    m.return_type = "bool"; // contradictory
    bool caught_method = false;
    try {
      m.assert_consistent();
    } catch (const std::logic_error& e) {
      caught_method = true;
    }
    assert(caught_method);
  }

  {
    // TraitMethod
    TraitMethod tm;
    tm.name = "cost";
    tm.assert_consistent();

    auto float_ref = Type::primitive("float");
    tm.bind_return_type(float_ref);
    assert(tm.return_type == "float");
    assert(tm.return_type_ref == float_ref);
    tm.assert_consistent();

    tm.return_type = "int"; // contradictory
    bool caught_tm = false;
    try {
      tm.assert_consistent();
    } catch (const std::logic_error& e) {
      caught_tm = true;
    }
    assert(caught_tm);
  }

  {
    // Handler
    Handler h;
    h.name = "Deposit";
    h.assert_consistent();

    auto unit_ref = Type::unit();
    h.bind_reply_type(unit_ref);
    assert(h.reply_type == "unit");
    assert(h.reply_type_ref == unit_ref);
    h.assert_consistent();

    h.reply_type = "int"; // contradictory
    bool caught_handler = false;
    try {
      h.assert_consistent();
    } catch (const std::logic_error& e) {
      caught_handler = true;
    }
    assert(caught_handler);
  }

  {
    // Function
    Function f;
    f.name = "calculate";
    f.assert_consistent();

    auto int_ref = Type::primitive("int");
    f.bind_return_type(int_ref);
    assert(f.return_type == "int");
    assert(f.return_type_ref == int_ref);
    f.assert_consistent();

    f.return_type = "string"; // contradictory
    bool caught_fn = false;
    try {
      f.assert_consistent();
    } catch (const std::logic_error& e) {
      caught_fn = true;
    }
    assert(caught_fn);
  }

  {
    // Program consistency
    Program prog;
    Function f;
    f.name = "foo";
    f.bind_return_type(Type::primitive("int"));
    prog.functions.push_back(f);
    prog.assert_declaration_type_consistency();

    // Introduce contradiction
    prog.functions[0].return_type = "float";
    bool caught_prog = false;
    try {
      prog.assert_declaration_type_consistency();
    } catch (const std::logic_error& e) {
      caught_prog = true;
    }
    assert(caught_prog);
  }

  std::cout << "Testing G1 Requirements & CallableId..." << std::endl;

  // 4. CallableId Dedicated Key/Record separate from TypeRef algebra
  auto valid_cid = CallableId::parse("callable-v1:module-v1:pkg@1.0.0::math:fn:add");
  assert(valid_cid.has_value());
  assert(valid_cid->module_id == "module-v1:pkg@1.0.0::math");
  assert(valid_cid->kind == "fn");
  assert(valid_cid->declaration_name == "add");
  assert(valid_cid->canonical_id == "callable-v1:module-v1:pkg@1.0.0::math:fn:add");

  CallableId constructed("module-v1:pkg@1.0.0::math", "fn", "add");
  assert(constructed == *valid_cid);
  assert(constructed.str() == "callable-v1:module-v1:pkg@1.0.0::math:fn:add");

  // Invalid formats reject fail-closed
  assert(!CallableId::parse("callable:math:add").has_value());
  assert(!CallableId::parse("not-a-callable").has_value());
  assert(!CallableId::parse("callable-v1:invalid").has_value());

  // 5. Open Requirements & MemberDispatchRequirement
  MemberDispatchRequirement req;
  req.requirement_id = "req-1";
  req.receiver = Type::builtin("vector", {Type::primitive("int")});
  req.member_name = "push";
  req.argument_types = {Type::primitive("int")};
  req.result = Type::unit();
  req.receiver_access = AccessMode::Write;
  req.argument_access = {AccessMode::Read};
  req.origin = {app_mod, "main.moss", 10, 5, "fn:main", "call:10:5"};
  req.parent_requirement_id = "parent-req-0";
  req.selected_callable_id = "builtin:vector.push";

  assert(req.receiver_access == AccessMode::Write);
  assert(req.argument_access[0] == AccessMode::Read);
  assert(req.origin.line == 10);
  assert(req.parent_requirement_id == "parent-req-0");
  assert(req.selected_callable_id == "builtin:vector.push");

  std::cout << "All G1 and G2 C++ authority assertions passed successfully!" << std::endl;
  return 0;
}
