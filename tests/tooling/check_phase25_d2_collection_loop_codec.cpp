#include "../../src/checked_body_ir.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cir = moss::checked_ir;

namespace {

moss::TypeRef primitive(const char* name) {
  return moss::Type::primitive(name);
}

cir::Expr parameter(moss::TypeRef type, std::uint32_t ordinal) {
  cir::Expr value;
  value.kind = cir::ExprKind::Parameter;
  value.type = std::move(type);
  value.ordinal = ordinal;
  return value;
}

cir::Expr local(moss::TypeRef type, std::uint32_t ordinal) {
  cir::Expr value;
  value.kind = cir::ExprKind::LocalRead;
  value.type = std::move(type);
  value.ordinal = ordinal;
  return value;
}

cir::Expr integer(moss::TypeRef type, std::string literal) {
  cir::Expr value;
  value.kind = cir::ExprKind::IntegerLiteral;
  value.type = std::move(type);
  value.literal_value = std::move(literal);
  return value;
}

cir::Expr binary(moss::TypeRef type, cir::BinaryOp op, cir::Expr left,
                 cir::Expr right) {
  cir::Expr value;
  value.kind = cir::ExprKind::Binary;
  value.type = std::move(type);
  value.binary_op = op;
  value.operands = {std::move(left), std::move(right)};
  return value;
}

cir::Expr count(moss::TypeRef vector_type) {
  cir::Expr value;
  value.kind = cir::ExprKind::CollectionCount;
  value.type = primitive("int");
  value.semantic_target_id = std::string(cir::kCollectionCountOperationId);
  value.operands.push_back(parameter(std::move(vector_type), 0));
  return value;
}

cir::Expr index(moss::TypeRef element_type, moss::TypeRef vector_type,
                moss::TypeRef int_type) {
  cir::Expr value;
  value.kind = cir::ExprKind::Index;
  value.type = std::move(element_type);
  value.semantic_target_id = std::string(cir::kCollectionIndexOperationId);
  value.operands.push_back(parameter(std::move(vector_type), 0));
  value.operands.push_back(local(std::move(int_type), 0));
  return value;
}

cir::Stmt return_value(cir::Expr value) {
  cir::Stmt statement;
  statement.kind = cir::StmtKind::Return;
  statement.type = value.type;
  statement.expr = std::move(value);
  return statement;
}

std::vector<cir::Stmt> index_of_body(const moss::TypeRef& element_type) {
  const auto int_type = primitive("int");
  const auto bool_type = primitive("bool");
  const auto vector_type = moss::Type::builtin("vector", {element_type});

  cir::Stmt cursor;
  cursor.kind = cir::StmtKind::Var;
  cursor.local_ordinal = 0;
  cursor.type = int_type;
  cursor.expr = integer(int_type, "0");

  cir::Stmt length;
  length.kind = cir::StmtKind::Var;
  length.local_ordinal = 1;
  length.type = int_type;
  length.expr = count(vector_type);

  cir::Stmt early_return = return_value(local(int_type, 0));
  cir::Stmt found;
  found.kind = cir::StmtKind::If;
  found.expr = binary(
      bool_type, cir::BinaryOp::Equal,
      index(element_type, vector_type, int_type), parameter(element_type, 1));
  found.then_branch.push_back(std::move(early_return));

  cir::Stmt increment;
  increment.kind = cir::StmtKind::Assign;
  increment.local_ordinal = 0;
  increment.expr = binary(int_type, cir::BinaryOp::Add, local(int_type, 0),
                          integer(int_type, "1"));

  cir::Stmt loop;
  loop.kind = cir::StmtKind::While;
  loop.expr = binary(bool_type, cir::BinaryOp::Less, local(int_type, 0),
                     local(int_type, 1));
  loop.body = {std::move(found), std::move(increment)};

  return {std::move(cursor), std::move(length), std::move(loop),
          return_value(binary(int_type, cir::BinaryOp::Sub,
                              integer(int_type, "0"),
                              integer(int_type, "1")))};
}

std::string hash_for(std::string_view bytes) {
  return "body-semantic-v1:sha256:" + moss::source_free_contract_sha256(bytes);
}

bool rejects(std::string_view bytes, const moss::TypeContext& context,
             const std::vector<moss::TypeRef>& parameters,
             const moss::TypeRef& result) {
  try {
    (void)cir::decode_collection_loop_checked_body_v1(
        bytes, context, parameters, result, hash_for(bytes));
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
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

int main(int argc, char** argv) {
  moss::TypeContext context;
  const auto int_type = primitive("int");
  const auto string_type = primitive("string");
  const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
  const auto generic_vector = moss::Type::builtin("vector", {variable});
  const auto generic_parameters = std::vector<moss::TypeRef>{
      generic_vector, variable};
  if (argc == 3 && std::string(argv[1]) == "--compat-bodies") {
    auto count_body = cir::encode_checked_body_v1(
        {return_value(count(generic_vector))});
    std::ofstream count_file(argv[2], std::ios::binary);
    count_file.write(count_body.data(),
                     static_cast<std::streamsize>(count_body.size()));
    cir::Expr indexed;
    indexed.kind = cir::ExprKind::Index;
    indexed.type = variable;
    indexed.semantic_target_id = std::string(cir::kCollectionIndexOperationId);
    indexed.operands = {parameter(generic_vector, 0),
                        integer(int_type, "0")};
    auto index_body = cir::encode_checked_body_v1({return_value(indexed)});
    std::ofstream index_file(std::string(argv[2]) + ".index",
                             std::ios::binary);
    index_file.write(index_body.data(),
                     static_cast<std::streamsize>(index_body.size()));
    cir::Stmt pass;
    pass.kind = cir::StmtKind::Pass;
    auto pass_body = cir::encode_checked_body_v1(
        {std::move(pass), return_value(integer(int_type, "42"))});
    std::ofstream pass_file(std::string(argv[2]) + ".pass",
                            std::ios::binary);
    pass_file.write(pass_body.data(),
                    static_cast<std::streamsize>(pass_body.size()));
    return count_file.good() && index_file.good() && pass_file.good() ? 0 : 1;
  }
  auto generic_statements = index_of_body(variable);
  const auto generic_body = cir::encode_checked_body_v1(generic_statements);
  const auto generic_hash = cir::compute_checked_body_hash_v1(generic_statements);
  const auto generic_decoded = cir::decode_collection_loop_checked_body_v1(
      generic_body, context, generic_parameters, int_type, generic_hash);
  const auto unified_decoded = cir::decode_checked_body_v1(
      generic_body, context, generic_parameters, int_type, generic_hash);
  assert(cir::encode_checked_body_v1(generic_decoded) == generic_body);
  assert(cir::encode_checked_body_v1(unified_decoded) == generic_body);
  assert(cir::compute_checked_body_hash_v1(generic_decoded) == generic_hash);
  assert(cir::compute_checked_body_hash_v1(unified_decoded) == generic_hash);
  assert(unified_decoded.size() == generic_decoded.size());
  assert(generic_decoded.size() == 4 &&
         generic_decoded[0].kind == cir::StmtKind::Var &&
         generic_decoded[0].local_ordinal == 0 &&
         generic_decoded[1].kind == cir::StmtKind::Var &&
         generic_decoded[1].local_ordinal == 1 &&
         generic_decoded[2].kind == cir::StmtKind::While &&
         generic_decoded[3].kind == cir::StmtKind::Return);
  assert(generic_decoded[0].expr->kind == cir::ExprKind::IntegerLiteral &&
         generic_decoded[0].expr->literal_value == "0" &&
         generic_decoded[2].expr->kind == cir::ExprKind::Binary &&
         generic_decoded[2].expr->binary_op == cir::BinaryOp::Less &&
         generic_decoded[2].body.size() == 2 &&
         generic_decoded[2].body[0].kind == cir::StmtKind::If &&
         generic_decoded[2].body[0].then_branch.size() == 1 &&
         generic_decoded[2].body[0].then_branch[0].kind ==
             cir::StmtKind::Return &&
         generic_decoded[2].body[1].kind == cir::StmtKind::Assign &&
         generic_decoded[2].body[1].local_ordinal == 0 &&
         generic_decoded[3].expr->kind == cir::ExprKind::Binary &&
         generic_decoded[3].expr->binary_op == cir::BinaryOp::Sub &&
         generic_decoded[3].expr->operands.size() == 2 &&
         generic_decoded[3].expr->operands[0].literal_value == "0" &&
         generic_decoded[3].expr->operands[1].literal_value == "1");
  const auto& decoded_if = generic_decoded[2].body[0];
  const auto& equality = *decoded_if.expr;
  assert(equality.kind == cir::ExprKind::Binary &&
         equality.binary_op == cir::BinaryOp::Equal &&
         equality.operands[0].kind == cir::ExprKind::Index &&
         equality.operands[0].semantic_target_id ==
             cir::kCollectionIndexOperationId &&
         equality.operands[0].type->structurally_equal(*variable) &&
         equality.operands[0].operands[0].type->structurally_equal(
             *generic_vector) &&
         equality.operands[1].type->structurally_equal(*variable));
  assert(generic_decoded[1].kind == cir::StmtKind::Var &&
         generic_decoded[1].expr->kind == cir::ExprKind::CollectionCount &&
         generic_decoded[1].expr->semantic_target_id ==
             cir::kCollectionCountOperationId &&
         generic_decoded[1].expr->operands[0].kind == cir::ExprKind::Parameter &&
         generic_decoded[1].expr->operands[0].ordinal == 0 &&
         generic_decoded[1].expr->operands[0].type->structurally_equal(
             *generic_vector) &&
         generic_decoded[1].expr->type->structurally_equal(*int_type) &&
         generic_decoded[2].expr->operands[1].kind == cir::ExprKind::LocalRead &&
         generic_decoded[2].expr->operands[1].ordinal == 1 &&
         generic_decoded[2].expr->operands[1].type->structurally_equal(
             *int_type));

  cir::Stmt one_local;
  one_local.kind = cir::StmtKind::Var;
  one_local.local_ordinal = 0;
  one_local.type = int_type;
  one_local.expr = integer(int_type, "0");
  const auto one_local_body = cir::encode_checked_body_v1({one_local});
  const auto one_local_hash = cir::compute_checked_body_hash_v1({one_local});
  const auto one_local_decoded = cir::decode_collection_loop_checked_body_v1(
      one_local_body, context, generic_parameters, int_type, one_local_hash);
  assert(one_local_decoded.size() == 1 &&
         one_local_decoded.front().kind == cir::StmtKind::Var &&
         cir::encode_checked_body_v1(one_local_decoded) == one_local_body);

  for (const auto& concrete_type : {int_type, string_type}) {
    const auto concrete_vector = moss::Type::builtin("vector", {concrete_type});
    const auto parameters = std::vector<moss::TypeRef>{concrete_vector,
                                                        concrete_type};
    const auto statements = index_of_body(concrete_type);
    const auto bytes = cir::encode_checked_body_v1(statements);
    const auto hash = cir::compute_checked_body_hash_v1(statements);
    const auto decoded = cir::decode_collection_loop_checked_body_v1(
        bytes, context, parameters, int_type, hash);
    assert(cir::encode_checked_body_v1(decoded) == bytes);
    assert(cir::compute_checked_body_hash_v1(decoded) == hash);
    const auto& decoded_index = decoded[2].body[0].expr->operands[0];
    assert(decoded_index.type->structurally_equal(*concrete_type));
    assert(decoded[1].expr->type->structurally_equal(*int_type) &&
           decoded[2].expr->operands[1].ordinal == 1);
  }

  auto invalid_ordinal = generic_statements;
  invalid_ordinal[2].body[1].local_ordinal = 17;
  assert(rejects(cir::encode_checked_body_v1(invalid_ordinal), context,
                 generic_parameters, int_type));

  auto read_before_declaration = generic_statements;
  read_before_declaration[1].expr = local(int_type, 1);
  assert(rejects(cir::encode_checked_body_v1(read_before_declaration), context,
                 generic_parameters, int_type));

  auto n_read_before_declaration = generic_statements;
  n_read_before_declaration[0].expr = local(int_type, 1);
  assert(rejects(cir::encode_checked_body_v1(n_read_before_declaration), context,
                 generic_parameters, int_type));

  auto local_escapes_scope = generic_statements;
  cir::Stmt scoped_let;
  scoped_let.kind = cir::StmtKind::Let;
  scoped_let.local_ordinal = 2;
  scoped_let.type = int_type;
  scoped_let.expr = integer(int_type, "7");
  local_escapes_scope[2].body[0].else_branch.push_back(scoped_let);
  local_escapes_scope[3].expr = local(int_type, 2);
  assert(rejects(cir::encode_checked_body_v1(local_escapes_scope), context,
                 generic_parameters, int_type));

  auto mutable_assignment = generic_statements;
  cir::Stmt assign_n;
  assign_n.kind = cir::StmtKind::Assign;
  assign_n.local_ordinal = 1;
  assign_n.expr = integer(int_type, "7");
  mutable_assignment.insert(mutable_assignment.begin() + 2, assign_n);
  const auto mutable_assignment_bytes =
      cir::encode_checked_body_v1(mutable_assignment);
  const auto mutable_assignment_hash =
      cir::compute_checked_body_hash_v1(mutable_assignment);
  const auto mutable_assignment_decoded =
      cir::decode_collection_loop_checked_body_v1(
          mutable_assignment_bytes, context, generic_parameters, int_type,
          mutable_assignment_hash);
  assert(mutable_assignment_decoded[1].kind == cir::StmtKind::Var &&
         mutable_assignment_decoded[2].kind == cir::StmtKind::Assign &&
         mutable_assignment_decoded[2].local_ordinal == 1 &&
         cir::encode_checked_body_v1(mutable_assignment_decoded) ==
             mutable_assignment_bytes &&
         cir::compute_checked_body_hash_v1(mutable_assignment_decoded) ==
             mutable_assignment_hash);

  auto explicit_let_assignment = generic_statements;
  explicit_let_assignment[1].kind = cir::StmtKind::Let;
  explicit_let_assignment.insert(explicit_let_assignment.begin() + 2,
                                 assign_n);
  assert(rejects(cir::encode_checked_body_v1(explicit_let_assignment), context,
                 generic_parameters, int_type));

  auto mismatched_n_type = generic_statements;
  mismatched_n_type[1].type = primitive("bool");
  assert(rejects(cir::encode_checked_body_v1(mismatched_n_type), context,
                 generic_parameters, int_type));

  auto mismatched_assignment = generic_statements;
  cir::Expr wrong_value;
  wrong_value.kind = cir::ExprKind::BoolLiteral;
  wrong_value.type = primitive("bool");
  wrong_value.literal_value = "true";
  mismatched_assignment[2].body[1].expr = wrong_value;
  assert(rejects(cir::encode_checked_body_v1(mismatched_assignment), context,
                 generic_parameters, int_type));

  auto invalid_index_type = generic_statements;
  invalid_index_type[2].body[0].expr->operands[0].type = int_type;
  auto invalid_index_bytes = cir::encode_checked_body_v1(invalid_index_type);
  assert(rejects(invalid_index_bytes, context, generic_parameters, int_type));

  auto invalid_operator = generic_statements;
  invalid_operator[2].body[0].expr->binary_op =
      static_cast<cir::BinaryOp>(99);
  assert(rejects(cir::encode_checked_body_v1(invalid_operator), context,
                 generic_parameters, int_type));

  auto invalid_branch = generic_statements;
  invalid_branch[2].body[0].expr = integer(int_type, "1");
  assert(rejects(cir::encode_checked_body_v1(invalid_branch), context,
                 generic_parameters, int_type));

  auto invalid_parameter = std::vector<cir::Stmt>{return_value(
      parameter(int_type, 9))};
  assert(rejects(cir::encode_checked_body_v1(invalid_parameter), context,
                 generic_parameters, int_type));

  auto invalid_count_identity = generic_body;
  const auto count_id = std::string(cir::kCollectionCountOperationId);
  const auto count_offset = invalid_count_identity.find(count_id);
  assert(count_offset != std::string::npos);
  invalid_count_identity.replace(count_offset, count_id.size(),
                                 "builtin:collection-count-v0");
  assert(rejects(invalid_count_identity, context, generic_parameters,
                 int_type));
  auto unknown_expression_tag = cir::encode_checked_body_v1(
      {return_value(index(variable, generic_vector, int_type))});
  const auto index_tag = unknown_expression_tag.find("2:16");
  assert(index_tag != std::string::npos);
  unknown_expression_tag.replace(index_tag, 4, "2:17");
  assert(rejects(unknown_expression_tag, context, generic_parameters,
                 variable));
  assert(rejects(generic_body.substr(0, generic_body.size() - 1), context,
                 generic_parameters, int_type));

  auto deep_blocks = std::vector<cir::Stmt>{return_value(integer(int_type, "0"))};
  for (int depth = 0; depth < 34; ++depth) {
    cir::Stmt nested;
    nested.kind = cir::StmtKind::If;
    cir::Expr condition;
    condition.kind = cir::ExprKind::BoolLiteral;
    condition.type = primitive("bool");
    condition.literal_value = "true";
    nested.expr = std::move(condition);
    nested.then_branch = std::move(deep_blocks);
    deep_blocks = {std::move(nested)};
  }
  assert(rejects(cir::encode_checked_body_v1(deep_blocks), context,
                 generic_parameters, int_type));

  std::vector<cir::Stmt> too_many_locals;
  for (std::uint32_t ordinal = 0; ordinal < 257; ++ordinal) {
    cir::Stmt declaration;
    declaration.kind = cir::StmtKind::Let;
    declaration.local_ordinal = ordinal;
    declaration.type = int_type;
    declaration.expr = integer(int_type, "0");
    too_many_locals.push_back(std::move(declaration));
  }
  too_many_locals.push_back(return_value(integer(int_type, "-1")));
  assert(rejects(cir::encode_checked_body_v1(too_many_locals), context,
                 generic_parameters, int_type));

  cir::Stmt unsupported_for;
  unsupported_for.kind = cir::StmtKind::For;
  unsupported_for.expr = parameter(generic_vector, 0);
  const std::vector<cir::Stmt> unsupported_statements{
      generic_statements.front(), generic_statements[1], unsupported_for,
      generic_statements.back()};
  assert(rejects(cir::encode_checked_body_v1(unsupported_statements), context,
                 generic_parameters, int_type));

  // The historical H3 entry point continues to reject new operation tags.
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        generic_body, context, generic_parameters, int_type, generic_hash);
  }));
  cir::Stmt no_op;
  no_op.kind = cir::StmtKind::Pass;
  const auto pass_body = cir::encode_checked_body_v1(
      {no_op, return_value(integer(int_type, "42"))});
  const auto pass_hash = cir::compute_checked_body_hash_v1(
      {no_op, return_value(integer(int_type, "42"))});
  const auto pass_decoded = cir::decode_checked_body_v1(
      pass_body, context, {}, int_type, pass_hash);
  assert(pass_decoded.size() == 2 &&
         pass_decoded.front().kind == cir::StmtKind::Pass &&
         pass_decoded.back().kind == cir::StmtKind::Return &&
         cir::encode_checked_body_v1(pass_decoded) == pass_body);
  auto invalid_pass = no_op;
  invalid_pass.expr = integer(int_type, "0");
  assert(throws_invalid([&] {
    (void)cir::encode_checked_body_v1(
        {invalid_pass, return_value(integer(int_type, "42"))});
  }));
  assert(throws_invalid([&] {
    (void)cir::decode_h3_checked_body_v1(
        pass_body, context, {}, int_type, pass_hash);
  }));
  assert(throws_invalid([&] {
    (void)cir::decode_checked_body_v1(
        generic_body, context, generic_parameters, int_type,
        "body-semantic-v1:sha256:wrong");
  }));

  std::cout << "D2 checked-body collection loop codec: PASS\n";
  return 0;
}
