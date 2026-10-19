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
  moss::TypeContext context;
  const auto int_type = moss::Type::primitive("int");
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

  // Unsupported checked expressions are rejected rather than falling back.
  cir::Expr string_literal;
  string_literal.kind = cir::ExprKind::StringLiteral;
  string_literal.type = string_type;
  string_literal.literal_value = "unsupported";
  const auto unsupported_statements =
      std::vector<cir::Stmt>{returned(string_literal)};
  const auto unsupported_body =
      cir::encode_checked_body_v1(unsupported_statements);
  const auto unsupported_hash =
      cir::compute_checked_body_hash_v1(unsupported_statements);
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        unsupported_body, context, {int_type}, string_type, unsupported_hash);
  }));

  std::cout << "Focused checked-body importer cases: PASS\n";
  return 0;
}
