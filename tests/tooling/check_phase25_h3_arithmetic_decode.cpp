#include "../../src/checked_body_ir.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cir = moss::checked_ir;

namespace {

cir::Expr parameter(const moss::TypeRef& type, std::uint32_t ordinal) {
  cir::Expr expression;
  expression.kind = cir::ExprKind::Parameter;
  expression.type = type;
  expression.ordinal = ordinal;
  return expression;
}

cir::Expr integer(const moss::TypeRef& type, std::string value) {
  cir::Expr expression;
  expression.kind = cir::ExprKind::IntegerLiteral;
  expression.type = type;
  expression.literal_value = std::move(value);
  return expression;
}

cir::Expr scalar(cir::ExprKind kind, const moss::TypeRef& type,
                 std::string value) {
  cir::Expr expression;
  expression.kind = kind;
  expression.type = type;
  expression.literal_value = std::move(value);
  return expression;
}

cir::Expr field(const moss::TypeRef& type, cir::Expr receiver,
                std::string target_id, std::uint32_t ordinal) {
  cir::Expr expression;
  expression.kind = cir::ExprKind::Field;
  expression.type = type;
  expression.operands.push_back(std::move(receiver));
  expression.semantic_target_id = std::move(target_id);
  expression.member_ordinal = ordinal;
  return expression;
}

cir::Expr binary(const moss::TypeRef& type, cir::BinaryOp operation,
                 cir::Expr left, cir::Expr right) {
  cir::Expr expression;
  expression.kind = cir::ExprKind::Binary;
  expression.type = type;
  expression.binary_op = operation;
  expression.operands = {std::move(left), std::move(right)};
  return expression;
}

cir::Stmt returned(cir::Expr expression) {
  cir::Stmt statement;
  statement.kind = cir::StmtKind::Return;
  statement.type = expression.type;
  statement.expr = std::move(expression);
  return statement;
}

template <typename Fn>
bool throws_invalid(Fn&& fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  auto context_owner = std::make_shared<moss::TypeContext>();
  moss::TypeContext& context = *context_owner;
  const auto int_type = moss::Type::primitive("int");
  const auto float_type = moss::Type::primitive("float");
  const auto bool_type = moss::Type::primitive("bool");
  const auto string_type = moss::Type::primitive("string");
  const auto callable_id =
      "callable-v1:module-v1:provider@0.1.0::Service:fn:helper";

  // Checked equivalent of `return x * 2 + 20`, built directly without an
  // interface, source file, or legacy generic_stmt payload.
  auto arithmetic = binary(
      int_type, cir::BinaryOp::Add,
      binary(int_type, cir::BinaryOp::Mul, parameter(int_type, 0),
             integer(int_type, "2")),
      integer(int_type, "20"));
  const auto arithmetic_body = cir::encode_checked_body_v1({returned(arithmetic)});
  assert(arithmetic_body.find("generic_stmt") == std::string::npos);
  const auto arithmetic_hash =
      cir::compute_checked_body_hash_v1({returned(arithmetic)});
  assert(arithmetic_hash ==
         "body-semantic-v1:sha256:d1efb33af28881f27deb3485d4587a8ed60b3a59b57acf2015f8b98f96ba536a");
  const auto decoded = cir::decode_h3_checked_body_v1(
      arithmetic_body, context, {int_type}, int_type, arithmetic_hash);
  assert(decoded.size() == 1 && decoded[0].kind == cir::StmtKind::Return);
  assert(decoded[0].expr && decoded[0].expr->kind == cir::ExprKind::Binary &&
         decoded[0].expr->binary_op == cir::BinaryOp::Add);
  const auto& multiply = decoded[0].expr->operands[0];
  assert(multiply.kind == cir::ExprKind::Binary &&
         multiply.binary_op == cir::BinaryOp::Mul);
  assert(multiply.operands[0].kind == cir::ExprKind::Parameter &&
         multiply.operands[0].ordinal == 0 &&
         multiply.operands[0].type->structurally_equal(*int_type));
  assert(multiply.operands[1].kind == cir::ExprKind::IntegerLiteral &&
         multiply.operands[1].literal_value == "2");
  assert(decoded[0].expr->operands[1].kind == cir::ExprKind::IntegerLiteral &&
         decoded[0].expr->operands[1].literal_value == "20");
  assert(decoded[0].expr->type->structurally_equal(*int_type));
  assert(cir::encode_checked_body_v1(decoded) == arithmetic_body);

  const auto direct_parameter_statements = std::vector<cir::Stmt>{
      returned(parameter(int_type, 0))};
  const auto direct_parameter_body =
      cir::encode_checked_body_v1(direct_parameter_statements);
  const auto direct_parameter_hash =
      cir::compute_checked_body_hash_v1(direct_parameter_statements);
  assert(direct_parameter_hash ==
         "body-semantic-v1:sha256:467d9bfcd6df1efdb681fff68c7afd64972e5586b6bba753ad6f1d8b588c9feb");
  const auto direct_parameter = cir::decode_h3_checked_body_v1(
      direct_parameter_body, context, {int_type}, int_type,
      direct_parameter_hash);
  assert(direct_parameter[0].expr->kind == cir::ExprKind::Parameter &&
         direct_parameter[0].expr->ordinal == 0 &&
         direct_parameter[0].expr->type->structurally_equal(*int_type));
  assert(cir::encode_checked_body_v1(direct_parameter) ==
         direct_parameter_body);

  // Open generic identity from the ABI summary. Parameter, arithmetic result,
  // and return retain the same authoritative TypeRef variable; literals
  // remain checked Int values.
  const auto generic_type = moss::Type::var(moss::TypeVarId{0, 0, 0});
  auto generic_arithmetic = binary(
      generic_type, cir::BinaryOp::Add,
      binary(generic_type, cir::BinaryOp::Mul, parameter(generic_type, 0),
             integer(int_type, "2")),
      integer(int_type, "20"));
  const auto generic_statements =
      std::vector<cir::Stmt>{returned(generic_arithmetic)};
  const auto generic_body = cir::encode_checked_body_v1(generic_statements);
  const auto generic_hash =
      cir::compute_checked_body_hash_v1(generic_statements);
  assert(generic_hash ==
         "body-semantic-v1:sha256:13de51438fae61d2dcf409d3b32d2e95f97a48496ac3f3e32b5da470be07f465");
  const auto generic_decoded = cir::decode_h3_checked_body_v1(
      generic_body, context, {generic_type}, generic_type, generic_hash);
  const auto& generic_add = *generic_decoded[0].expr;
  const auto& generic_multiply = generic_add.operands[0];
  assert(generic_decoded[0].expr->type->structurally_equal(*generic_type));
  assert(generic_multiply.type->structurally_equal(*generic_type));
  assert(generic_multiply.operands[0].kind == cir::ExprKind::Parameter &&
         generic_multiply.operands[0].type->structurally_equal(*generic_type));
  assert(generic_multiply.operands[1].kind == cir::ExprKind::IntegerLiteral &&
         generic_multiply.operands[1].type->structurally_equal(*int_type));
  assert(generic_add.operands[1].kind == cir::ExprKind::IntegerLiteral &&
         generic_add.operands[1].type->structurally_equal(*int_type));

  const auto generic_parameter_statements = std::vector<cir::Stmt>{
      returned(parameter(generic_type, 0))};
  const auto generic_parameter_body =
      cir::encode_checked_body_v1(generic_parameter_statements);
  const auto generic_parameter_hash =
      cir::compute_checked_body_hash_v1(generic_parameter_statements);
  assert(generic_parameter_hash ==
         "body-semantic-v1:sha256:80848c7ed73e0e8bec95d74204bd8e3322b8bcda1de35e4ca058e735f0bdcb5a");
  const auto generic_parameter = cir::decode_h3_checked_body_v1(
      generic_parameter_body, context, {generic_type}, generic_type,
      generic_parameter_hash);
  assert(generic_parameter[0].expr->type->structurally_equal(*generic_type));
  assert(cir::encode_checked_body_v1(generic_parameter) ==
         generic_parameter_body);

  const std::vector<std::pair<cir::Expr, moss::TypeRef>> scalar_cases = {
      {integer(int_type, "42"), int_type},
      {scalar(cir::ExprKind::FloatLiteral, float_type, "3.14"), float_type},
      {scalar(cir::ExprKind::BoolLiteral, bool_type, "true"), bool_type},
      {scalar(cir::ExprKind::BoolLiteral, bool_type, "false"), bool_type},
      {scalar(cir::ExprKind::StringLiteral, string_type, "moss language"),
       string_type}};
  const std::vector<std::string> scalar_hashes = {
      "body-semantic-v1:sha256:f6c7cbc17ec5495c177351559485513d96c04bb4fa3925e738c06eea95d155f2",
      "body-semantic-v1:sha256:e26635add09562296aa3a2824b1f76089a12e3e70819c5bb0bc1000e96dfe763",
      "body-semantic-v1:sha256:a74f9431d2a45e9b17a0bc4792599bd275a4a46cc5b0c6e3a005a0a0e999a353",
      "body-semantic-v1:sha256:bb4bc92880271ea01c5f27d1c60e0638fb1eb3979622050bd5bf2288bb9579cc",
      "body-semantic-v1:sha256:1c9cca618be4e98de0479324ccbedb7c70880c330d3784e1adb0490892296050"};
  std::size_t scalar_index = 0;
  for (const auto& test_case : scalar_cases) {
    const auto statements = std::vector<cir::Stmt>{returned(test_case.first)};
    const auto body = cir::encode_checked_body_v1(statements);
    const auto hash = cir::compute_checked_body_hash_v1(statements);
    assert(hash == scalar_hashes.at(scalar_index++));
    const auto round_trip = cir::decode_h3_checked_body_v1(
        body, context, {}, test_case.second, hash);
    assert(round_trip.size() == 1 && round_trip[0].expr);
    assert(round_trip[0].expr->type->structurally_equal(*test_case.second));
    assert(cir::encode_checked_body_v1(round_trip) == body);
  }

  moss::NominalDeclaration box_declaration;
  box_declaration.kind = moss::DeclarationKind::Record;
  box_declaration.program_name = "Box";
  box_declaration.module_id = moss::module_identity("provider", "0.1.0", "types");
  box_declaration.declaration_name = "Box";
  box_declaration.fields = {{"value", "", true}};
  const auto box = context.declare(std::move(box_declaration));
  context.bind_field_annotations();
  const auto generic_box_type = moss::Type::nominal_bound(
      box, "Box", {{"value", generic_type}});
  const auto generic_box_parameter = parameter(generic_box_type, 0);
  const auto generic_field = field(generic_type, generic_box_parameter, box->id, 0);
  const auto field_statements = std::vector<cir::Stmt>{returned(generic_field)};
  const auto field_body = cir::encode_checked_body_v1(field_statements);
  const auto field_hash = cir::compute_checked_body_hash_v1(field_statements);
  assert(field_hash ==
         "body-semantic-v1:sha256:be26ddd07d9d7a2d6d2ea9ea5dfc282f517308bb839261687d93c4fb77b62e19");
  const auto field_decoded = cir::decode_h3_checked_body_v1(
      field_body, context, {generic_box_type}, generic_type, field_hash);
  assert(field_decoded[0].expr->kind == cir::ExprKind::Field &&
         field_decoded[0].expr->semantic_target_id == box->id &&
         field_decoded[0].expr->member_ordinal == 0 &&
         field_decoded[0].expr->member_name == "value" &&
         field_decoded[0].expr->type->structurally_equal(*generic_type));
  assert(cir::encode_checked_body_v1(field_decoded) == field_body);

  const auto forged_field = field(
      generic_type, generic_box_parameter, "nominal-template-v1:forged", 0);
  const auto forged_field_statements =
      std::vector<cir::Stmt>{returned(forged_field)};
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        cir::encode_checked_body_v1(forged_field_statements), context,
        {generic_box_type}, generic_type,
        cir::compute_checked_body_hash_v1(forged_field_statements));
  }));
  const auto out_of_range_field = field(
      generic_type, generic_box_parameter, box->id, 1);
  const auto out_of_range_statements =
      std::vector<cir::Stmt>{returned(out_of_range_field)};
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        cir::encode_checked_body_v1(out_of_range_statements), context,
        {generic_box_type}, generic_type,
        cir::compute_checked_body_hash_v1(out_of_range_statements));
  }));
  const auto mismatched_field = field(
      int_type, generic_box_parameter, box->id, 0);
  const auto mismatched_field_statements =
      std::vector<cir::Stmt>{returned(mismatched_field)};
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        cir::encode_checked_body_v1(mismatched_field_statements), context,
        {generic_box_type}, int_type,
        cir::compute_checked_body_hash_v1(mismatched_field_statements));
  }));

  // An unrelated open variable cannot be substituted as an operand or result.
  const auto other_generic = moss::Type::var(moss::TypeVarId{0, 0, 1});
  auto mismatched_arithmetic = binary(
      generic_type, cir::BinaryOp::Add,
      binary(generic_type, cir::BinaryOp::Mul, parameter(generic_type, 0),
             parameter(other_generic, 1)),
      integer(int_type, "20"));
  const auto mismatched_statements =
      std::vector<cir::Stmt>{returned(mismatched_arithmetic)};
  const auto mismatched_body =
      cir::encode_checked_body_v1(mismatched_statements);
  const auto mismatched_hash =
      cir::compute_checked_body_hash_v1(mismatched_statements);
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        mismatched_body, context, {generic_type, other_generic}, generic_type,
        mismatched_hash);
  }));

  // A generic result cannot claim the parameter's identity when its checked
  // operands only establish concrete integer arithmetic.
  auto mismatched_result = binary(
      generic_type, cir::BinaryOp::Add,
      integer(int_type, "2"), integer(int_type, "20"));
  const auto mismatched_result_statements =
      std::vector<cir::Stmt>{returned(mismatched_result)};
  const auto mismatched_result_body =
      cir::encode_checked_body_v1(mismatched_result_statements);
  const auto mismatched_result_hash =
      cir::compute_checked_body_hash_v1(mismatched_result_statements);
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        mismatched_result_body, context, {generic_type}, generic_type,
        mismatched_result_hash);
  }));

  // Existing H3 two-statement local-result form, including its exact call ID.
  cir::Expr call;
  call.kind = cir::ExprKind::Call;
  call.type = int_type;
  call.semantic_target_id = callable_id;
  call.operands = {parameter(int_type, 0)};
  cir::Stmt let;
  let.kind = cir::StmtKind::Let;
  let.local_ordinal = 0;
  let.type = int_type;
  let.expr = std::move(call);
  cir::Expr local;
  local.kind = cir::ExprKind::LocalRead;
  local.type = int_type;
  local.ordinal = 0;
  auto h3_body_statements = std::vector<cir::Stmt>{let, returned(local)};
  const auto h3_body = cir::encode_checked_body_v1(h3_body_statements);
  const auto h3_hash = cir::compute_checked_body_hash_v1(h3_body_statements);
  assert(h3_hash ==
         "body-semantic-v1:sha256:efce6d9874a0288d11afe9f8289a00fc99f86300ef1ec618c06334e56eea85b2");
  const auto h3_decoded = cir::decode_h3_checked_body_v1(
      h3_body, context, {int_type}, int_type, h3_hash);
  assert(h3_decoded.size() == 2 &&
         h3_decoded[0].kind == cir::StmtKind::Let &&
         h3_decoded[1].kind == cir::StmtKind::Return);
  assert(h3_decoded[0].expr &&
         h3_decoded[0].expr->semantic_target_id == callable_id);

  // Invalid framing and a mismatched expected hash are rejected.
  auto invalid_body = arithmetic_body;
  invalid_body[0] = 'X';
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        invalid_body, context, {int_type}, int_type, arithmetic_hash);
  }));
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        arithmetic_body, context, {int_type}, int_type,
        "body-semantic-v1:sha256:" + std::string(64, '0'));
  }));

  const auto malformed_float_statements = std::vector<cir::Stmt>{returned(
      scalar(cir::ExprKind::FloatLiteral, float_type, "3.x"))};
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        cir::encode_checked_body_v1(malformed_float_statements), context, {},
        float_type,
        cir::compute_checked_body_hash_v1(malformed_float_statements));
  }));

  const auto malformed_bool_statements = std::vector<cir::Stmt>{returned(
      scalar(cir::ExprKind::BoolLiteral, bool_type, "true"))};
  auto malformed_bool_body =
      cir::encode_checked_body_v1(malformed_bool_statements);
  const auto bool_wire = malformed_bool_body.find("1:1");
  assert(bool_wire != std::string::npos);
  malformed_bool_body[bool_wire + 2] = '2';
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        malformed_bool_body, context, {}, bool_type,
        cir::compute_checked_body_hash_v1(malformed_bool_statements));
  }));

  // Unsupported checked expressions are rejected rather than falling back.
  cir::Expr vector_literal;
  vector_literal.kind = cir::ExprKind::VectorLiteral;
  vector_literal.type = moss::Type::builtin("vector", {int_type});
  vector_literal.operands = {integer(int_type, "1")};
  const auto unsupported_statements =
      std::vector<cir::Stmt>{returned(vector_literal)};
  const auto unsupported_body =
      cir::encode_checked_body_v1(unsupported_statements);
  const auto unsupported_hash =
      cir::compute_checked_body_hash_v1(unsupported_statements);
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        unsupported_body, context, {int_type}, vector_literal.type,
        unsupported_hash);
  }));

  std::cout << "Focused checked-body importer cases: PASS\n";
  return 0;
}
