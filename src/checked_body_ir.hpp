#pragma once

#include <stdexcept>
#include <string>
#include "ast.hpp"
#include "source_free_abi_v9.hpp"

namespace moss {

inline void checked_body_field(std::string& out, const std::string& value) {
  out += std::to_string(value.size()) + ":" + value;
}

inline void encode_checked_body_type_v1(std::string& out, const TypeRef& type) {
  if (!type) throw std::invalid_argument("checked body node has no TypeRef");
  checked_body_field(out, std::to_string(static_cast<int>(type->kind)));
  if (type->kind == TypeKind::InferenceVar) {
    if (type->var_id.session_id || type->var_id.owner_id)
      throw std::invalid_argument("checked body has non-summary type variable");
    checked_body_field(out, std::to_string(type->var_id.ordinal));
  } else if (type->kind == TypeKind::NominalInstance ||
             type->kind == TypeKind::Enum || type->kind == TypeKind::Domain) {
    if (!type->declaration)
      throw std::invalid_argument("checked body has unresolved declaration");
    checked_body_field(out, type->declaration->id);
  } else {
    checked_body_field(out, type->name);
  }
  checked_body_field(out, std::to_string(type->type_args.size()));
  for (const auto& argument : type->type_args)
    encode_checked_body_type_v1(out, argument);
  checked_body_field(out, std::to_string(type->field_substitutions.size()));
  for (const auto& field : type->field_substitutions) {
    checked_body_field(out, field.first);
    encode_checked_body_type_v1(out, field.second);
  }
}

inline void encode_checked_body_expr_v1(std::string& out,
                                         const CheckedBodyExprV1& expr) {
  if (!expr.type)
    throw std::invalid_argument("checked body node has no TypeRef");
  checked_body_field(out, std::to_string(static_cast<int>(expr.kind)));
  encode_checked_body_type_v1(out, expr.type);
  switch (expr.kind) {
    case CheckedBodyExprV1::Kind::Parameter:
      if (!expr.operands.empty() || !expr.integer_value.empty())
        throw std::invalid_argument("invalid checked parameter node");
      checked_body_field(out, std::to_string(expr.parameter_ordinal));
      break;
    case CheckedBodyExprV1::Kind::Integer:
      if (!expr.operands.empty() || expr.integer_value.empty())
        throw std::invalid_argument("invalid checked integer node");
      checked_body_field(out, expr.integer_value);
      break;
    case CheckedBodyExprV1::Kind::Add:
      if (expr.operands.size() != 2 || !expr.integer_value.empty())
        throw std::invalid_argument("invalid checked addition node");
      checked_body_field(out, "2");
      for (const auto& child : expr.operands)
        encode_checked_body_expr_v1(out, child);
      break;
  }
}

inline std::string checked_body_semantic_hash_v1(
    const std::vector<CheckedBodyStmtV1>& statements) {
  if (statements.size() != 1 ||
      statements.front().kind != CheckedBodyStmtV1::Kind::Return)
    throw std::invalid_argument("unsupported checked body statement sequence");
  std::string encoded = "moss-checked-body-v1\n";
  checked_body_field(encoded, std::to_string(statements.size()));
  for (const auto& statement : statements) {
    checked_body_field(encoded, "return");
    encode_checked_body_expr_v1(encoded, statement.value);
  }
  return "body-semantic-v1:sha256:" + source_free_contract_sha256(encoded);
}

}  // namespace moss
