#include "../../src/checked_body_ir.hpp"
#include "../../src/type_ir.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cir = moss::checked_ir;

static moss::NominalDeclaration record(const std::string& program_name,
                                       const std::string& module_id,
                                       const std::string& name,
                                       std::vector<moss::NominalFieldSlot> fields) {
  moss::NominalDeclaration declaration;
  declaration.kind = moss::DeclarationKind::Record;
  declaration.program_name = program_name;
  declaration.module_id = module_id;
  declaration.declaration_name = name;
  declaration.fields = std::move(fields);
  return declaration;
}

static bool throws_invalid(const std::function<void()>& operation) {
  try {
    operation();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

int main() {
  std::cout << "Starting Phase 25 Checked Body IR unit tests..." << std::endl;

  // 1. Setup session and nominal declarations
  const std::string app_module = moss::module_identity("app", "1.0.0", "main");
  auto session = std::make_shared<moss::TypeContext>();
  moss::TypeContext& context = *session;
  moss::TypeContext::Scope scope(&context);

  auto box_decl = context.declare(record("Box", app_module, "Box", {{"value", "int", false}}));
  auto cell_decl = context.declare(record("Cell", app_module, "Cell", {{"item", "int", false}}));
  auto point_decl = context.declare(record("Point", app_module, "Point", {{"x", "int", false}, {"y", "int", false}}));
  auto open_box_decl = context.declare(record("OpenBox", app_module,
                                           "OpenBox", {{"value", "", true}}));
  context.bind_field_annotations();

  auto int_type = moss::Type::primitive("int");
  auto float_type = moss::Type::primitive("float");
  auto bool_type = moss::Type::primitive("bool");
  auto string_type = moss::Type::primitive("string");
  auto unit_type = moss::Type::unit();
  auto box_type = moss::Type::nominal_bound(box_decl, "Box", {{"value", int_type}});
  auto cell_type = moss::Type::nominal_bound(cell_decl, "Cell", {{"item", int_type}});
  auto vector_int_type = moss::Type::builtin("vector", {int_type});
  auto open_value = moss::Type::var({0, 0, 0});
  auto open_box_type = moss::Type::nominal_bound(
      open_box_decl, "OpenBox", {{"value", open_value}});

  // 2. Test Expression Nodes
  // Parameter
  cir::Expr p0{cir::ExprKind::Parameter, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr p1{cir::ExprKind::Parameter, int_type, 1, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};

  // Literals
  cir::Expr int_lit1{cir::ExprKind::IntegerLiteral, int_type, 0, "42", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr int_lit2{cir::ExprKind::IntegerLiteral, int_type, 0, "-7", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr float_lit{cir::ExprKind::FloatLiteral, float_type, 0, "3.14", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr bool_lit_t{cir::ExprKind::BoolLiteral, bool_type, 0, "true", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr bool_lit_f{cir::ExprKind::BoolLiteral, bool_type, 0, "false", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr str_lit{cir::ExprKind::StringLiteral, string_type, 0, "moss language", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};

  // Local Read
  cir::Expr local0{cir::ExprKind::LocalRead, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};

  // Unary Op
  cir::Expr un_neg{cir::ExprKind::Unary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0}, {}, 0, {}, {}};
  cir::Expr un_not{cir::ExprKind::Unary, bool_type, 0, {}, cir::UnaryOp::Not, cir::BinaryOp::Add, {bool_lit_t}, {}, 0, {}, {}};

  // Binary Ops
  cir::Expr bin_add{cir::ExprKind::Binary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0, p1}, {}, 0, {}, {}};
  cir::Expr bin_sub{cir::ExprKind::Binary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Sub, {p0, p1}, {}, 0, {}, {}};
  cir::Expr bin_mul{cir::ExprKind::Binary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Mul, {p0, p1}, {}, 0, {}, {}};
  cir::Expr bin_eq{cir::ExprKind::Binary, bool_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Equal, {p0, p1}, {}, 0, {}, {}};

  // Field Access
  cir::Expr box_param{cir::ExprKind::Parameter, box_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr field_box{cir::ExprKind::Field, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {box_param}, box_decl->id, 0, "value", {}};

  cir::Expr cell_param{cir::ExprKind::Parameter, cell_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr field_cell{cir::ExprKind::Field, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {cell_param}, cell_decl->id, 0, "item", {}};

  // Vector Literal
  cir::Expr vec_lit12{cir::ExprKind::VectorLiteral, vector_int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {int_lit1, int_lit2}, {}, 0, {}, {}};
  cir::Expr vec_lit21{cir::ExprKind::VectorLiteral, vector_int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {int_lit2, int_lit1}, {}, 0, {}, {}};

  // Call
  const std::string helper_id = "callable-v1:module-v1:app@1.0.0::main:fn:helper";
  const std::string other_helper_id = "callable-v1:module-v1:app@1.0.0::main:fn:other_helper";
  cir::Expr call_helper{cir::ExprKind::Call, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0}, helper_id, 0, {}, {}};
  cir::Expr call_other{cir::ExprKind::Call, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0}, other_helper_id, 0, {}, {}};

  // A nominal constructor is distinct from a callable target and carries
  // exact template/field-slot identities plus the inferred field TypeRef.
  cir::Expr open_param{cir::ExprKind::Parameter, open_value, 0, {},
                       cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
  cir::Expr construct_open{cir::ExprKind::NominalConstruct, open_box_type, 0, {},
                           cir::UnaryOp::Neg, cir::BinaryOp::Add,
                           {open_param}, open_box_decl->id, 0, {}, {}};
  cir::Stmt return_construct{cir::StmtKind::Return, open_box_type, 0,
                             construct_open, {}, {}, 0, {}, {}, {}, {}, {}};
  assert(moss::is_canonical_semantic_hash(
      cir::compute_checked_body_hash_v1({return_construct})));
  auto malformed_construct = construct_open;
  malformed_construct.semantic_target_id = box_decl->id;
  assert(throws_invalid([&] {
    std::string encoded;
    cir::encode_checked_expr(encoded, malformed_construct);
  }));
  auto constructor_dependencies = moss::extract_semantic_dependencies(
      {return_construct});
  assert(constructor_dependencies.size() == 1 &&
         constructor_dependencies[0].target_id == open_box_decl->id &&
         constructor_dependencies[0].kind ==
             moss::SemanticDependency::Kind::NominalType);

  // Message
  const std::string audit_handler_id = "callable-v1:module-v1:app@1.0.0::main:domain:Audit:handler:Record";
  cir::Expr msg_record{cir::ExprKind::Message, unit_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {int_lit1}, audit_handler_id, 0, "Record", "audit"};

  // 3. Test Statement Nodes and Control Flow
  // Return
  cir::Stmt ret_bin_add{cir::StmtKind::Return, int_type, 0, bin_add, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_bin_sub{cir::StmtKind::Return, int_type, 0, bin_sub, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_void{cir::StmtKind::Return, unit_type, 0, std::nullopt, {}, {}, 0, {}, {}, {}, {}, {}};

  // Let & Var
  cir::Stmt let_x{cir::StmtKind::Let, int_type, 0, int_lit1, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt var_y{cir::StmtKind::Var, int_type, 1, int_lit2, {}, {}, 0, {}, {}, {}, {}, {}};

  // Assign
  cir::Stmt assign_y{cir::StmtKind::Assign, int_type, 1, p0, {}, {}, 0, {}, {}, {}, {}, {}};

  // AssignField
  cir::Stmt assign_field{cir::StmtKind::AssignField, int_type, 0, int_lit1, {box_param}, box_decl->id, 0, "value", {}, {}, {}, {}};

  // If-Else
  cir::Stmt if_stmt{cir::StmtKind::If, unit_type, 0, bin_eq, {}, {}, 0, {}, {ret_bin_add}, {ret_bin_sub}, {}, {}};

  // While
  cir::Stmt while_stmt{cir::StmtKind::While, unit_type, 0, bin_eq, {}, {}, 0, {}, {}, {}, {assign_y}, {}};

  // For
  cir::Stmt for_stmt{cir::StmtKind::For, unit_type, 0, vec_lit12, {}, {}, 0, {}, {}, {}, {assign_y}, {}};

  // Match
  cir::CaseBranch branch_ok{"Ok", "enum-v1:app:Result:Ok", {0}, {int_type}, {ret_bin_add}};
  cir::CaseBranch branch_err{"Error", "enum-v1:app:Result:Error", {0}, {string_type}, {ret_bin_sub}};
  cir::Stmt match_stmt{cir::StmtKind::Match, int_type, 0, p0, {}, {}, 0, {}, {}, {}, {}, {branch_ok, branch_err}};

  // 4. Test Deterministic Encoding & Hashing
  std::string hash_add = cir::compute_checked_body_hash_v1({ret_bin_add});
  std::string hash_sub = cir::compute_checked_body_hash_v1({ret_bin_sub});
  assert(hash_add != hash_sub);
  assert(hash_add.rfind("body-semantic-v1:sha256:", 0) == 0);
  assert(moss::is_canonical_semantic_hash(hash_add));
  assert(moss::is_canonical_semantic_hash(hash_sub));

  // Invariance under local/parameter renaming
  cir::Stmt let_a{cir::StmtKind::Let, int_type, 0, int_lit1, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_a{cir::StmtKind::Return, int_type, 0, local0, {}, {}, 0, {}, {}, {}, {}, {}};
  std::string hash_body1 = cir::compute_checked_body_hash_v1({let_a, ret_a});

  cir::Stmt let_b{cir::StmtKind::Let, int_type, 0, int_lit1, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_b{cir::StmtKind::Return, int_type, 0, local0, {}, {}, 0, {}, {}, {}, {}, {}};
  std::string hash_body2 = cir::compute_checked_body_hash_v1({let_b, ret_b});
  assert(hash_body1 == hash_body2);

  // Vector literal ordering sensitivity: [1, 2] vs [2, 1]
  cir::Stmt ret_vec12{cir::StmtKind::Return, vector_int_type, 0, vec_lit12, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_vec21{cir::StmtKind::Return, vector_int_type, 0, vec_lit21, {}, {}, 0, {}, {}, {}, {}, {}};
  std::string hash_vec12 = cir::compute_checked_body_hash_v1({ret_vec12});
  std::string hash_vec21 = cir::compute_checked_body_hash_v1({ret_vec21});
  assert(hash_vec12 != hash_vec21);

  // Field access on Box.value vs Cell.item
  cir::Stmt ret_field_box{cir::StmtKind::Return, int_type, 0, field_box, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_field_cell{cir::StmtKind::Return, int_type, 0, field_cell, {}, {}, 0, {}, {}, {}, {}, {}};
  std::string hash_field_box = cir::compute_checked_body_hash_v1({ret_field_box});
  std::string hash_field_cell = cir::compute_checked_body_hash_v1({ret_field_cell});
  assert(hash_field_box != hash_field_cell);

  // Calls to different functions
  cir::Stmt ret_call_helper{cir::StmtKind::Return, int_type, 0, call_helper, {}, {}, 0, {}, {}, {}, {}, {}};
  cir::Stmt ret_call_other{cir::StmtKind::Return, int_type, 0, call_other, {}, {}, 0, {}, {}, {}, {}, {}};
  std::string hash_call_helper = cir::compute_checked_body_hash_v1({ret_call_helper});
  std::string hash_call_other = cir::compute_checked_body_hash_v1({ret_call_other});
  assert(hash_call_helper != hash_call_other);

  // Control flow hashing: If vs While
  std::string hash_if = cir::compute_checked_body_hash_v1({if_stmt});
  std::string hash_while = cir::compute_checked_body_hash_v1({while_stmt});
  std::string hash_for = cir::compute_checked_body_hash_v1({for_stmt});
  std::string hash_match = cir::compute_checked_body_hash_v1({match_stmt});
  assert(hash_if != hash_while);
  assert(hash_if != hash_for);
  assert(hash_if != hash_match);

  // 5. Test Malformed IR Rejection
  // Null TypeRef
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Parameter, nullptr, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Non-summary type variable
  assert(throws_invalid([&] {
    auto bad_var = moss::Type::var(moss::TypeVarId{1, 0, 0}); // session_id != 0
    cir::Expr bad{cir::ExprKind::Parameter, bad_var, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Arity mismatch for Binary op (1 operand instead of 2)
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Binary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0}, {}, 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Arity mismatch for Unary op (2 operands instead of 1)
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Unary, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0, p1}, {}, 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Field without semantic target ID
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Field, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {box_param}, "", 0, "value", {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Call without semantic target ID
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Call, int_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {p0}, "", 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Message without target domain
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::Message, unit_type, 0, {}, cir::UnaryOp::Neg, cir::BinaryOp::Add, {int_lit1}, audit_handler_id, 0, "Record", ""};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Non-canonical integer literal
  assert(throws_invalid([&] {
    cir::Expr bad{cir::ExprKind::IntegerLiteral, int_type, 0, "123a", cir::UnaryOp::Neg, cir::BinaryOp::Add, {}, {}, 0, {}, {}};
    std::string out;
    cir::encode_checked_expr(out, bad);
  }));

  // Empty statement sequence
  assert(throws_invalid([&] {
    cir::compute_checked_body_hash_v1({});
  }));

  // 6. Test Semantic Dependency Extraction
  auto deps_helper = moss::extract_semantic_dependencies({ret_call_helper});
  assert(deps_helper.size() == 1);
  assert(deps_helper[0].kind == moss::SemanticDependency::Kind::Callable);
  assert(deps_helper[0].target_id == helper_id);

  cir::Stmt stmt_msg{cir::StmtKind::ExprStmt, unit_type, 0, msg_record, {}, {}, 0, {}, {}, {}, {}, {}};
  auto deps_msg = moss::extract_semantic_dependencies({stmt_msg});
  assert(deps_msg.size() == 1);
  assert(deps_msg[0].kind == moss::SemanticDependency::Kind::Handler);
  assert(deps_msg[0].target_id == audit_handler_id);

  auto deps_field = moss::extract_semantic_dependencies({ret_field_box});
  assert(deps_field.size() == 1);
  assert(deps_field[0].kind == moss::SemanticDependency::Kind::NominalType);
  assert(deps_field[0].target_id == box_decl->id);

  // 7. Test Dependency Closure Validation Scenarios
  // Setup environment of entities
  std::map<std::string, moss::EntityFacts> env;
  env[helper_id] = moss::EntityFacts{helper_id, hash_add, {}, true, ""};
  env[other_helper_id] = moss::EntityFacts{other_helper_id, hash_sub, {}, true, ""};
  env[audit_handler_id] = moss::EntityFacts{audit_handler_id, "body-semantic-v1:sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", {}, true, ""};
  const std::string box_nominal_hash = "nominal-instance-v1:sha256:1111111111111111111111111111111111111111111111111111111111111111";
  env[box_decl->id] = moss::EntityFacts{box_decl->id, box_nominal_hash, {}, true, ""};

  // Scenario 1: Valid closure (callable and nominal dependencies)
  {
    std::vector<moss::SemanticDependency> root_deps{
      {moss::SemanticDependency::Kind::Callable, helper_id, hash_add},
      {moss::SemanticDependency::Kind::NominalType, box_decl->id, box_nominal_hash}
    };
    auto res = moss::validate_dependency_closure("fn:main", root_deps, env);
    assert(res.ok());
    assert(res.status == moss::ClosureValidationStatus::Valid);
  }

  // Scenario 2: Changed dependency (hash mismatch)
  {
    std::string old_stale_hash = "body-semantic-v1:sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    std::vector<moss::SemanticDependency> root_deps{{moss::SemanticDependency::Kind::Callable, helper_id, old_stale_hash}};
    auto res = moss::validate_dependency_closure("fn:main", root_deps, env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::HashMismatch);
    assert(res.failing_id == helper_id);
    assert(res.expected_hash == old_stale_hash);
    assert(res.actual_hash == hash_add);
  }

  // Scenario 3: Missing dependency
  {
    std::string missing_id = "callable-v1:module-v1:app@1.0.0::main:fn:missing_func";
    std::vector<moss::SemanticDependency> root_deps{{moss::SemanticDependency::Kind::Callable, missing_id, hash_add}};
    auto res = moss::validate_dependency_closure("fn:main", root_deps, env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::MissingDependency);
    assert(res.failing_id == missing_id);
  }

  // Scenario 4: Forged dependency hash
  {
    std::string forged_hash = "not-a-valid-sha256-hash";
    std::vector<moss::SemanticDependency> root_deps{{moss::SemanticDependency::Kind::Callable, helper_id, forged_hash}};
    auto res = moss::validate_dependency_closure("fn:main", root_deps, env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::ForgedHash);
    assert(res.failing_id == helper_id);
  }

  // Scenario 5: Cyclic dependency
  {
    const std::string cycle_a = "callable-v1:module-v1:app@1.0.0::main:fn:cycle_a";
    const std::string cycle_b = "callable-v1:module-v1:app@1.0.0::main:fn:cycle_b";
    std::string hash_a = "body-semantic-v1:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    std::string hash_b = "body-semantic-v1:sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    auto cyclic_env = env;
    cyclic_env[cycle_a] = moss::EntityFacts{cycle_a, hash_a, {{moss::SemanticDependency::Kind::Callable, cycle_b, hash_b}}, true, ""};
    cyclic_env[cycle_b] = moss::EntityFacts{cycle_b, hash_b, {{moss::SemanticDependency::Kind::Callable, cycle_a, hash_a}}, true, ""};

    std::vector<moss::SemanticDependency> root_deps{{moss::SemanticDependency::Kind::Callable, cycle_a, hash_a}};
    auto res = moss::validate_dependency_closure("fn:main", root_deps, cyclic_env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::CyclicDependency);
    assert(!res.cycle_path.empty());
  }

  // Scenario 6: Unsupported dependency fact (fail-closed, never fabricate hash)
  {
    const std::string unsupported_id = "callable-v1:module-v1:app@1.0.0::main:fn:unsupported_func";
    auto unsupp_env = env;
    unsupp_env[unsupported_id] = moss::EntityFacts{unsupported_id, "", {}, false, "unsupported_checked_body_form"};

    std::string valid_expected = "body-semantic-v1:sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    std::vector<moss::SemanticDependency> root_deps{{moss::SemanticDependency::Kind::Callable, unsupported_id, valid_expected}};
    auto res = moss::validate_dependency_closure("fn:main", root_deps, unsupp_env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::UnsupportedDependency);
    assert(res.failing_id == unsupported_id);
  }

  // Scenario 7: Missing expected hash (rejects incomplete contract)
  {
    std::vector<moss::SemanticDependency> incomplete_deps{{moss::SemanticDependency::Kind::Callable, helper_id, ""}};
    assert(!incomplete_deps[0].is_complete());
    auto res = moss::validate_dependency_closure("fn:main", incomplete_deps, env);
    assert(!res.ok());
    assert(res.status == moss::ClosureValidationStatus::MissingExpectedHash);
    assert(res.failing_id == helper_id);
  }

  // Scenario 8: Invalid hash kind (Callable vs NominalType hash prefix mismatch)
  {
    // Callable with nominal-instance-v1 prefix
    std::vector<moss::SemanticDependency> bad_callable_deps{{moss::SemanticDependency::Kind::Callable, helper_id, box_nominal_hash}};
    auto res1 = moss::validate_dependency_closure("fn:main", bad_callable_deps, env);
    assert(!res1.ok());
    assert(res1.status == moss::ClosureValidationStatus::InvalidHashKind);
    assert(res1.failing_id == helper_id);

    // NominalType with body-semantic-v1 prefix
    std::vector<moss::SemanticDependency> bad_nominal_deps{{moss::SemanticDependency::Kind::NominalType, box_decl->id, hash_add}};
    auto res2 = moss::validate_dependency_closure("fn:main", bad_nominal_deps, env);
    assert(!res2.ok());
    assert(res2.status == moss::ClosureValidationStatus::InvalidHashKind);
    assert(res2.failing_id == box_decl->id);

    // Environment actual hash has wrong kind
    auto bad_env = env;
    bad_env[helper_id] = moss::EntityFacts{helper_id, box_nominal_hash, {}, true, ""};
    std::vector<moss::SemanticDependency> valid_callable_deps{{moss::SemanticDependency::Kind::Callable, helper_id, hash_add}};
    auto res3 = moss::validate_dependency_closure("fn:main", valid_callable_deps, bad_env);
    assert(!res3.ok());
    assert(res3.status == moss::ClosureValidationStatus::InvalidHashKind);
  }

  // 8. Test make_checked_function
  // 8a. Function with no dependencies succeeds
  {
    auto fn = moss::make_checked_function("callable-v1:main:fn:add", {int_type, int_type}, int_type, {ret_bin_add});
    assert(fn.has_supported_body);
    assert(!fn.body_semantic_hash.empty());
    assert(fn.body_semantic_hash == hash_add);
    assert(fn.dependencies.empty());
  }

  // 8b. Function with dependency but missing expected hash fails closed (incomplete contract)
  {
    auto fn_incomplete = moss::make_checked_function("callable-v1:main:fn:caller", {int_type}, int_type, {ret_call_helper});
    assert(!fn_incomplete.has_supported_body);
    assert(fn_incomplete.body_semantic_hash.empty());
    assert(fn_incomplete.unsupported_reason.find("incomplete_dependency_contract") != std::string::npos);
    assert(fn_incomplete.dependencies.size() == 1);
    assert(!fn_incomplete.dependencies[0].is_complete());
  }

  // 8c. Function with dependency and provided expected hash succeeds
  {
    std::map<std::string, std::string> expected_map{{helper_id, hash_add}};
    auto fn_complete = moss::make_checked_function("callable-v1:main:fn:caller", {int_type}, int_type, {ret_call_helper}, expected_map);
    assert(fn_complete.has_supported_body);
    assert(!fn_complete.body_semantic_hash.empty());
    assert(fn_complete.dependencies.size() == 1);
    assert(fn_complete.dependencies[0].is_complete());
    assert(fn_complete.dependencies[0].expected_hash == hash_add);
  }

  std::cout << "All Phase 25 Checked Body IR and Dependency Closure tests passed!" << std::endl;
  return 0;
}
