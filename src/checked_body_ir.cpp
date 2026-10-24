#include "checked_body_ir.hpp"

namespace moss {

void checked_body_field(std::string& out, const std::string& value) {
  out += std::to_string(value.size()) + ":" + value;
}

// Canonical encoding of a TypeRef within checked body IR.
// Rejects unclosed types or non-summary inference variables.
void encode_checked_body_type_v1(std::string& out, const TypeRef& type) {
  if (!type) throw std::invalid_argument("checked body node has no TypeRef");
  checked_body_field(out, std::to_string(static_cast<int>(type->kind)));
  if (type->kind == TypeKind::InferenceVar) {
    if (type->var_id.session_id || type->var_id.owner_id)
      throw std::invalid_argument("checked body has non-summary type variable");
    checked_body_field(out, std::to_string(type->var_id.ordinal));
  } else if (type->kind == TypeKind::NominalInstance ||
             type->kind == TypeKind::Enum || type->kind == TypeKind::Domain) {
    if (!type->declaration || type->declaration->id.empty())
      throw std::invalid_argument("checked body has unresolved declaration");
    checked_body_field(out, type->declaration->id);
  } else {
    checked_body_field(out, type->name);
  }
  checked_body_field(out, std::to_string(type->type_args.size()));
  for (const auto& argument : type->type_args)
    encode_checked_body_type_v1(out, argument);
  checked_body_field(out, std::to_string(type->field_substitutions.size()));
  std::vector<std::pair<std::string, TypeRef>> sorted_substs = type->field_substitutions;
  std::sort(sorted_substs.begin(), sorted_substs.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& field : sorted_substs) {
    checked_body_field(out, field.first);
    encode_checked_body_type_v1(out, field.second);
  }
}


namespace checked_ir {

void encode_checked_expr(std::string& out, const Expr& expr) {
  if (!expr.type)
    throw std::invalid_argument("checked body node has no TypeRef");
  checked_body_field(out, std::to_string(static_cast<int>(expr.kind)));
  encode_checked_body_type_v1(out, expr.type);

  switch (expr.kind) {
    case ExprKind::Parameter:
      if (!expr.operands.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid parameter node: operands or literal present");
      checked_body_field(out, std::to_string(expr.ordinal));
      break;

    case ExprKind::IntegerLiteral: {
      if (!expr.operands.empty() || expr.literal_value.empty())
        throw std::invalid_argument("invalid integer node: operands present or literal empty");
      for (size_t i = 0; i < expr.literal_value.size(); ++i) {
        if (i == 0 && expr.literal_value[0] == '-' && expr.literal_value.size() > 1) continue;
        if (!std::isdigit(static_cast<unsigned char>(expr.literal_value[i])))
          throw std::invalid_argument("non-canonical integer literal in checked body: " + expr.literal_value);
      }
      checked_body_field(out, expr.literal_value);
      break;
    }

    case ExprKind::FloatLiteral:
      if (!expr.operands.empty() || expr.literal_value.empty())
        throw std::invalid_argument("invalid float node: operands present or literal empty");
      checked_body_field(out, expr.literal_value);
      break;

    case ExprKind::BoolLiteral:
      if (!expr.operands.empty() || expr.literal_value.empty())
        throw std::invalid_argument("invalid bool node: operands present or literal empty");
      if (expr.literal_value != "true" && expr.literal_value != "false")
        throw std::invalid_argument("invalid bool literal value: " + expr.literal_value);
      checked_body_field(out, expr.literal_value == "true" ? "1" : "0");
      break;

    case ExprKind::StringLiteral:
      if (!expr.operands.empty())
        throw std::invalid_argument("invalid string node: operands present");
      checked_body_field(out, expr.literal_value);
      break;

    case ExprKind::LocalRead:
      if (!expr.operands.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid local read node: operands or literal present");
      checked_body_field(out, std::to_string(expr.ordinal));
      break;

    case ExprKind::Add:
      if (expr.operands.size() != 2 || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked addition node: expected 2 operands");
      checked_body_field(out, "2");
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::Unary:
      if (expr.operands.size() != 1 || !expr.literal_value.empty())
        throw std::invalid_argument("invalid unary op: expected 1 operand");
      checked_body_field(out, std::to_string(static_cast<int>(expr.unary_op)));
      encode_checked_expr(out, expr.operands[0]);
      break;

    case ExprKind::Binary:
      if (expr.operands.size() != 2 || !expr.literal_value.empty())
        throw std::invalid_argument("invalid binary op: expected 2 operands");
      checked_body_field(out, std::to_string(static_cast<int>(expr.binary_op)));
      encode_checked_expr(out, expr.operands[0]);
      encode_checked_expr(out, expr.operands[1]);
      break;

    case ExprKind::Field:
      if (expr.operands.size() != 1 || expr.semantic_target_id.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked field node: missing receiver or target id");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.member_ordinal));
      encode_checked_expr(out, expr.operands[0]);
      break;

    case ExprKind::VectorLiteral:
      if (!expr.semantic_target_id.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked vector node");
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::MapLiteral:
      if (expr.operands.size() % 2 != 0 || expr.ordinal != 0 ||
          expr.operands.size() / 2 > 32 ||
          !expr.literal_value.empty() ||
          expr.unary_op != UnaryOp::Neg || expr.binary_op != BinaryOp::Add ||
          !expr.semantic_target_id.empty() || expr.member_ordinal != 0 ||
          !expr.member_name.empty() || !expr.target_domain.empty())
        throw std::invalid_argument("invalid checked map node: expected even key-value count");
      checked_body_field(out, std::to_string(expr.operands.size() / 2));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::Call:
      if (expr.semantic_target_id.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked call node: missing semantic target id");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::NominalConstruct:
      if (!expr.type || expr.type->kind != TypeKind::NominalInstance ||
          !expr.type->declaration ||
          expr.semantic_target_id != expr.type->declaration->id ||
          !expr.literal_value.empty() ||
          expr.operands.size() != expr.type->declaration->fields.size())
        throw std::invalid_argument(
            "invalid checked nominal constructor: target or field shape mismatch");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (std::size_t index = 0; index < expr.operands.size(); ++index) {
        const auto& slot = expr.type->declaration->fields[index];
        auto bound = std::find_if(expr.type->field_substitutions.begin(),
                                  expr.type->field_substitutions.end(),
            [&](const auto& field) { return field.first == slot.name; });
        if (!slot.inferred || bound == expr.type->field_substitutions.end() ||
            !bound->second || !expr.operands[index].type ||
            !bound->second->structurally_equal(*expr.operands[index].type))
          throw std::invalid_argument(
              "checked nominal constructor has an unbound or mismatched field");
        checked_body_field(out, expr.semantic_target_id + ":field:" +
                                std::to_string(index) + ":" + slot.name);
        encode_checked_expr(out, expr.operands[index]);
      }
      break;

    case ExprKind::Message:
      if (expr.target_domain.empty() || expr.semantic_target_id.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked message node: missing domain or handler id");
      checked_body_field(out, expr.target_domain);
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::CollectionCount:
      if (expr.operands.size() != 1 || !expr.literal_value.empty() ||
          expr.semantic_target_id != kCollectionCountOperationId)
        throw std::invalid_argument(
            "invalid checked collection-count node: operation or arity mismatch");
      checked_body_field(out, expr.semantic_target_id);
      encode_checked_expr(out, expr.operands.front());
      break;

    case ExprKind::Index:
      if (expr.operands.size() != 2 || !expr.literal_value.empty() ||
          expr.semantic_target_id != kCollectionIndexOperationId)
        throw std::invalid_argument(
            "invalid checked index node: operation or arity mismatch");
      checked_body_field(out, expr.semantic_target_id);
      encode_checked_expr(out, expr.operands[0]);
      encode_checked_expr(out, expr.operands[1]);
      break;

    case ExprKind::MemberCall:
      if (expr.semantic_target_id.rfind(kMemberRequirementPrefix, 0) != 0 ||
          expr.member_name.empty() || expr.operands.empty() ||
          expr.member_ordinal == UINT32_MAX || !expr.literal_value.empty())
        throw std::invalid_argument(
            "invalid checked member call: requirement identity or shape is missing");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.member_ordinal));
      checked_body_field(out, expr.member_name);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::CollectionPush:
      if (expr.semantic_target_id != kCollectionPushOperationId ||
          expr.operands.size() != 2 || !expr.type ||
          expr.type->kind != TypeKind::Unit || !expr.literal_value.empty())
        throw std::invalid_argument(
            "invalid checked Vector.push operation identity or shape");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::PipelineElement:
      if (expr.semantic_target_id.rfind("pipeline-placeholder:", 0) != 0 ||
          !expr.operands.empty() || !expr.literal_value.empty() ||
          expr.ordinal != 0 || expr.pipeline_input_type ||
          !expr.pipeline_placeholder_identity.empty())
        throw std::invalid_argument("invalid checked pipeline placeholder");
      checked_body_field(out, expr.semantic_target_id);
      break;

    case ExprKind::PipelineMap:
      if (expr.semantic_target_id != "functional:Map" || expr.ordinal != 0 ||
          expr.operands.size() != 2 || !expr.pipeline_input_type ||
          !expr.type || expr.type->kind != TypeKind::Builtin ||
          expr.type->name != "vector" || expr.type->type_args.size() != 1 ||
          !expr.operands[1].type ||
          !expr.type->type_args.front()->structurally_equal(
              *expr.operands[1].type) || !expr.literal_value.empty() ||
          expr.pipeline_placeholder_identity.empty())
        throw std::invalid_argument("invalid checked functional Map node");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.ordinal));
      encode_checked_body_type_v1(out, expr.pipeline_input_type);
      checked_body_field(out, expr.pipeline_placeholder_identity);
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;

    case ExprKind::EnumConstruct: {
      if (!expr.type || expr.type->kind != TypeKind::Enum ||
          !expr.type->declaration ||
          expr.type->declaration->kind != DeclarationKind::Enum ||
          expr.semantic_target_id.rfind(
              "nominal-enum-variant-v1:" + expr.type->declaration->id +
                  ":case:", 0) != 0 ||
          expr.payload_ordinals.size() != expr.operands.size() ||
          !expr.literal_value.empty())
        throw std::invalid_argument(
            "invalid checked enum constructor identity or shape");
      for (std::size_t index = 0; index < expr.payload_ordinals.size(); ++index)
        if (expr.payload_ordinals[index] != index ||
            !expr.operands[index].type)
          throw std::invalid_argument(
              "checked enum constructor payload ordinals are noncanonical");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (std::size_t index = 0; index < expr.operands.size(); ++index) {
        checked_body_field(out, std::to_string(expr.payload_ordinals[index]));
        encode_checked_expr(out, expr.operands[index]);
      }
      break;
    }

    default:
      throw std::invalid_argument("unknown checked body expression kind");
  }
}

void encode_checked_stmt(std::string& out, const Stmt& stmt) {
  checked_body_field(out, std::to_string(static_cast<int>(stmt.kind)));
  switch (stmt.kind) {
    case StmtKind::Let:
    case StmtKind::Var:
      if (!stmt.expr || !stmt.type)
        throw std::invalid_argument("invalid let/var statement: missing expr or type");
      checked_body_field(out, std::to_string(stmt.local_ordinal));
      encode_checked_body_type_v1(out, stmt.type);
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::Assign:
      if (!stmt.expr)
        throw std::invalid_argument("invalid assign statement: missing value expr");
      checked_body_field(out, std::to_string(stmt.local_ordinal));
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::AssignField:
      if (stmt.operands.size() != 1 || !stmt.expr || stmt.semantic_target_id.empty())
        throw std::invalid_argument("invalid assign field statement: missing receiver, value, or target id");
      checked_body_field(out, stmt.semantic_target_id);
      checked_body_field(out, std::to_string(stmt.member_ordinal));
      encode_checked_expr(out, stmt.operands[0]);
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::AssignIndex:
      if (stmt.operands.size() != 2 || !stmt.expr || stmt.type ||
          stmt.local_ordinal != 0 || !stmt.semantic_target_id.empty() ||
          stmt.member_ordinal != 0 || !stmt.member_name.empty() ||
          !stmt.then_branch.empty() || !stmt.else_branch.empty() ||
          !stmt.body.empty() || !stmt.match_cases.empty())
        throw std::invalid_argument("invalid assign index statement: missing receiver, index, or value");
      encode_checked_expr(out, stmt.operands[0]);
      encode_checked_expr(out, stmt.operands[1]);
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::ExprStmt:
      if (!stmt.expr)
        throw std::invalid_argument("invalid expr statement: missing expr");
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::Return:
      checked_body_field(out, stmt.expr ? "1" : "0");
      if (stmt.expr) encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::Reply:
      if (!stmt.expr)
        throw std::invalid_argument("invalid reply statement: missing reply expr");
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::If:
      if (!stmt.expr)
        throw std::invalid_argument("invalid if statement: missing condition expr");
      encode_checked_expr(out, *stmt.expr);
      checked_body_field(out, std::to_string(stmt.then_branch.size()));
      for (const auto& s : stmt.then_branch) encode_checked_stmt(out, s);
      checked_body_field(out, std::to_string(stmt.else_branch.size()));
      for (const auto& s : stmt.else_branch) encode_checked_stmt(out, s);
      break;

    case StmtKind::IfJoin:
      if (!stmt.expr || !stmt.type || stmt.operands.size() != 2 ||
          !stmt.expr->type ||
          !stmt.expr->type->structurally_equal(*Type::primitive("bool")) ||
          !stmt.operands[0].type || !stmt.operands[1].type ||
          !stmt.type->structurally_equal(*stmt.operands[0].type) ||
          !stmt.type->structurally_equal(*stmt.operands[1].type) ||
          !stmt.then_branch.empty() || !stmt.else_branch.empty() ||
          !stmt.body.empty() || !stmt.match_cases.empty() ||
          !stmt.semantic_target_id.empty() || stmt.member_ordinal != 0 ||
          !stmt.member_name.empty())
        throw std::invalid_argument("invalid checked if-join statement");
      checked_body_field(out, std::to_string(stmt.local_ordinal));
      encode_checked_body_type_v1(out, stmt.type);
      encode_checked_expr(out, *stmt.expr);
      encode_checked_expr(out, stmt.operands[0]);
      encode_checked_expr(out, stmt.operands[1]);
      break;

    case StmtKind::While:
      if (!stmt.expr)
        throw std::invalid_argument("invalid while statement: missing condition expr");
      encode_checked_expr(out, *stmt.expr);
      checked_body_field(out, std::to_string(stmt.body.size()));
      for (const auto& s : stmt.body) encode_checked_stmt(out, s);
      break;

    case StmtKind::For:
      if (!stmt.expr || !stmt.type || stmt.body.empty())
        throw std::invalid_argument(
            "invalid for statement: missing checked binder, iterable, or body");
      checked_body_field(out, std::to_string(stmt.local_ordinal));
      encode_checked_body_type_v1(out, stmt.type);
      encode_checked_expr(out, *stmt.expr);
      checked_body_field(out, std::to_string(stmt.body.size()));
      for (const auto& s : stmt.body) encode_checked_stmt(out, s);
      break;

    case StmtKind::Match:
      if (!stmt.expr)
        throw std::invalid_argument("invalid match statement: missing subject expr");
      encode_checked_expr(out, *stmt.expr);
      checked_body_field(out, std::to_string(stmt.match_cases.size()));
      for (const auto& c : stmt.match_cases) {
        checked_body_field(out, c.variant_tag);
        checked_body_field(out, c.variant_id);
        checked_body_field(out, std::to_string(c.payload_ordinals.size()));
        for (auto ord : c.payload_ordinals) checked_body_field(out, std::to_string(ord));
        checked_body_field(out, std::to_string(c.payload_types.size()));
        for (const auto& t : c.payload_types) encode_checked_body_type_v1(out, t);
        checked_body_field(out, std::to_string(c.body.size()));
        for (const auto& s : c.body) encode_checked_stmt(out, s);
      }
      break;

    case StmtKind::Try: {
      if (stmt.exceptional_identity.empty() || stmt.branch_identity.empty() ||
          stmt.exceptional_alternatives.empty() || stmt.body.empty() ||
          stmt.exceptional_cases.empty() || stmt.expr || stmt.type)
        throw std::invalid_argument(
            "invalid checked try statement: identity, alternatives, or blocks are missing");
      checked_body_field(out, stmt.exceptional_identity);
      checked_body_field(out, stmt.branch_identity);
      checked_body_field(out,
                         std::to_string(stmt.exceptional_alternatives.size()));
      std::string previous_alternative;
      for (const auto& alternative : stmt.exceptional_alternatives) {
        if (alternative.empty() ||
            (!previous_alternative.empty() && previous_alternative >= alternative))
          throw std::invalid_argument(
              "checked try alternatives are not canonical");
        previous_alternative = alternative;
        checked_body_field(out, alternative);
      }
      checked_body_field(out, std::to_string(stmt.body.size()));
      for (const auto& child : stmt.body) encode_checked_stmt(out, child);
      checked_body_field(out,
                         std::to_string(stmt.exceptional_cases.size()));
      std::string previous_branch;
      for (const auto& branch : stmt.exceptional_cases) {
        if (branch.branch_identity.empty() ||
            (!previous_branch.empty() &&
             previous_branch >= branch.branch_identity) ||
            branch.variant_ids.empty() ||
            branch.payload_ordinals.size() != branch.payload_types.size() ||
            branch.body.empty())
          throw std::invalid_argument(
              "checked recover branch identity or shape is invalid");
        previous_branch = branch.branch_identity;
        checked_body_field(out, branch.branch_identity);
        checked_body_field(out, std::to_string(branch.variant_ids.size()));
        std::string previous_variant;
        for (const auto& variant : branch.variant_ids) {
          if (variant.empty() ||
              (!previous_variant.empty() && previous_variant >= variant))
            throw std::invalid_argument(
                "checked recover variant identities are not canonical");
          previous_variant = variant;
          checked_body_field(out, variant);
        }
        checked_body_field(out,
                           std::to_string(branch.payload_ordinals.size()));
        for (std::size_t index = 0;
             index < branch.payload_ordinals.size(); ++index) {
          if ((index != 0 && branch.payload_ordinals[index - 1] >=
                                  branch.payload_ordinals[index]) ||
              !branch.payload_types[index])
            throw std::invalid_argument(
                "checked recover payload ordinals or TypeRefs are invalid");
          checked_body_field(out,
                             std::to_string(branch.payload_ordinals[index]));
          encode_checked_body_type_v1(out, branch.payload_types[index]);
        }
        checked_body_field(out, std::to_string(branch.body.size()));
        for (const auto& child : branch.body) encode_checked_stmt(out, child);
      }
      break;
    }

    case StmtKind::Raise:
      if (stmt.exceptional_identity.empty() || stmt.branch_identity.empty() ||
          stmt.exceptional_alternatives.empty() || !stmt.expr ||
          stmt.expr->kind != ExprKind::EnumConstruct ||
          !stmt.body.empty() || !stmt.exceptional_cases.empty())
        throw std::invalid_argument(
            "invalid checked raise statement: identity or value is missing");
      checked_body_field(out, stmt.exceptional_identity);
      checked_body_field(out, stmt.branch_identity);
      checked_body_field(out,
                         std::to_string(stmt.exceptional_alternatives.size()));
      {
        std::string previous_alternative;
        for (const auto& alternative : stmt.exceptional_alternatives) {
          if (alternative.empty() ||
              (!previous_alternative.empty() &&
               previous_alternative >= alternative))
            throw std::invalid_argument(
                "checked raise alternatives are not canonical");
          previous_alternative = alternative;
          checked_body_field(out, alternative);
        }
      }
      encode_checked_expr(out, *stmt.expr);
      break;

    case StmtKind::Pass:
      if (stmt.expr || stmt.local_ordinal != 0 || !stmt.operands.empty() ||
          !stmt.semantic_target_id.empty() || stmt.member_ordinal != 0 ||
          !stmt.member_name.empty() || stmt.type ||
          !stmt.then_branch.empty() || !stmt.else_branch.empty() ||
          !stmt.body.empty() || !stmt.match_cases.empty())
        throw std::invalid_argument("invalid pass statement: payload present");
      break;

  }
}

std::string encode_checked_body_v1(const std::vector<Stmt>& statements) {
  if (statements.empty())
    throw std::invalid_argument("empty checked body statement sequence");
  std::string encoded = "moss-checked-body-v1\n";
  checked_body_field(encoded, std::to_string(statements.size()));
  for (const auto& statement : statements) {
    if (statements.size() == 1 && statement.kind == StmtKind::Return) {
      checked_body_field(encoded, "return");
      if (statement.expr) encode_checked_expr(encoded, *statement.expr);
    } else {
      encode_checked_stmt(encoded, statement);
    }
  }
  return encoded;
}

std::string compute_checked_body_hash_v1(const std::vector<Stmt>& statements) {
  return "body-semantic-v1:sha256:" +
      source_free_contract_sha256(encode_checked_body_v1(statements));
}



}  // namespace checked_ir

}  // namespace moss
