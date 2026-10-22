#include "checked_body_ir.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

int main() {
  const std::string module = "module-v1:h3@1.0.0::Service";
  moss::TypeContext context;
  moss::NominalDeclaration declaration;
  declaration.kind = moss::DeclarationKind::Record;
  declaration.program_name = "Service__Box";
  declaration.module_id = module;
  declaration.declaration_name = "Box";
  declaration.fields = {{"value", "", true}};
  auto box = context.declare(std::move(declaration));
  auto var = moss::Type::var(moss::TypeVarId{0, 0, 0});
  auto result = moss::Type::nominal_bound(
      box, box->program_name, {{"value", var}});
  moss::checked_ir::Expr parameter;
  parameter.kind = moss::checked_ir::ExprKind::Parameter;
  parameter.type = var;
  parameter.ordinal = 0;
  moss::checked_ir::Expr construct;
  construct.kind = moss::checked_ir::ExprKind::NominalConstruct;
  construct.type = result;
  construct.semantic_target_id = box->id;
  construct.operands = {parameter};
  moss::checked_ir::Stmt let;
  let.kind = moss::checked_ir::StmtKind::Let;
  let.type = result;
  let.local_ordinal = 0;
  let.expr = construct;
  moss::checked_ir::Expr local;
  local.kind = moss::checked_ir::ExprKind::LocalRead;
  local.type = result;
  local.ordinal = 0;
  moss::checked_ir::Stmt returned;
  returned.kind = moss::checked_ir::StmtKind::Return;
  returned.type = result;
  returned.expr = local;
  const std::vector<moss::checked_ir::Stmt> statements{let, returned};
  const auto bytes = moss::checked_ir::encode_checked_body_v1(statements);
  const auto hash = moss::checked_ir::compute_checked_body_hash_v1(statements);
  assert(hash ==
         "body-semantic-v1:sha256:18537b3ef108674bb3212ff2786a88ba699129bc04d9b0c36486c7669df64a8a");
  auto decoded = moss::checked_ir::decode_h3_checked_body_v1(
      bytes, context, {var}, result, hash);
  assert(moss::checked_ir::encode_checked_body_v1(decoded) == bytes);
  auto dependencies = moss::extract_semantic_dependencies(decoded);
  assert(dependencies.size() == 1 &&
         dependencies.front().kind == moss::SemanticDependency::Kind::NominalType &&
         dependencies.front().target_id == box->id);

  auto call = construct;
  call.kind = moss::checked_ir::ExprKind::Call;
  call.semantic_target_id = "callable-v1:" + module + ":fn:private_box";
  let.expr = call;
  const std::vector<moss::checked_ir::Stmt> nested{let, returned};
  const auto nested_bytes = moss::checked_ir::encode_checked_body_v1(nested);
  const auto nested_hash = moss::checked_ir::compute_checked_body_hash_v1(nested);
  assert(nested_hash ==
         "body-semantic-v1:sha256:5c3804e4d36d4adc483624922429fccb32357523893c97e75c680aeb787ef19e");
  auto nested_decoded = moss::checked_ir::decode_h3_checked_body_v1(
      nested_bytes, context, {var}, result, nested_hash);
  dependencies = moss::extract_semantic_dependencies(nested_decoded);
  assert(dependencies.size() == 1 &&
         dependencies.front().kind == moss::SemanticDependency::Kind::Callable &&
         dependencies.front().target_id == call.semantic_target_id);

  auto rejects = [&](std::string_view forged, const moss::TypeRef& expected,
                     std::string_view body_hash) {
    try {
      (void)moss::checked_ir::decode_h3_checked_body_v1(
          forged, context, {var}, expected, body_hash);
    } catch (const std::invalid_argument&) { return true; }
    return false;
  };
  assert(rejects(bytes.substr(0, bytes.size() - 1), result, hash));
  assert(rejects(bytes, var, hash));
  assert(rejects(bytes, result,
                 "body-semantic-v1:sha256:" + std::string(64, '0')));
  std::string forged = bytes;
  auto at = forged.find(box->id);
  assert(at != std::string::npos);
  forged.replace(at, box->id.size(),
                 box->id.substr(0, box->id.size() - 1) + "X");
  assert(rejects(forged, result,
                 "body-semantic-v1:sha256:" +
                     moss::source_free_contract_sha256(forged)));

  moss::checked_ir::Expr member_receiver;
  member_receiver.kind = moss::checked_ir::ExprKind::Parameter;
  member_receiver.type = var;
  member_receiver.ordinal = 0;
  moss::checked_ir::Expr member_call;
  member_call.kind = moss::checked_ir::ExprKind::MemberCall;
  member_call.type = moss::Type::primitive("int");
  member_call.semantic_target_id =
      std::string(moss::checked_ir::kMemberRequirementPrefix) +
      "callable-v1:" + module + ":fn:size:member:0";
  member_call.member_ordinal = 0;
  member_call.member_name = "length";
  member_call.operands = {member_receiver};
  returned.expr = member_call;
  returned.type = member_call.type;
  const std::vector<moss::checked_ir::Stmt> member_statements{returned};
  const auto member_bytes =
      moss::checked_ir::encode_checked_body_v1(member_statements);
  const auto member_hash =
      moss::checked_ir::compute_checked_body_hash_v1(member_statements);
  const auto member_decoded = moss::checked_ir::decode_checked_body_v1(
      member_bytes, context, {var}, member_call.type, member_hash);
  assert(member_decoded.size() == 1 && member_decoded.front().expr &&
         member_decoded.front().expr->kind ==
             moss::checked_ir::ExprKind::MemberCall &&
         member_decoded.front().expr->semantic_target_id ==
             member_call.semantic_target_id &&
         member_decoded.front().expr->member_name == "length");
  bool narrow_reader_rejected_member = false;
  try {
    (void)moss::checked_ir::decode_h3_checked_body_v1(
        member_bytes, context, {var}, member_call.type, member_hash);
  } catch (const std::invalid_argument&) {
    narrow_reader_rejected_member = true;
  }
  assert(narrow_reader_rejected_member);
  auto invalid_member = member_call;
  invalid_member.semantic_target_id.clear();
  auto invalid_member_statement = member_statements.front();
  invalid_member_statement.expr = invalid_member;
  bool encoder_rejected_unlinked_member = false;
  try {
    (void)moss::checked_ir::encode_checked_body_v1(
        {invalid_member_statement});
  } catch (const std::invalid_argument&) {
    encoder_rejected_unlinked_member = true;
  }
  assert(encoder_rejected_unlinked_member);
  std::cout << "H3 checked body decoder: PASS\n";
}
