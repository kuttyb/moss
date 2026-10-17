#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ast.hpp"
#include "source_free_abi_v9.hpp"
#include "type_ir.hpp"

namespace moss {

// Field encoder helper: <len>:<value>
inline void checked_body_field(std::string& out, const std::string& value) {
  out += std::to_string(value.size()) + ":" + value;
}

// Canonical encoding of a TypeRef within checked body IR.
// Rejects unclosed types or non-summary inference variables.
inline void encode_checked_body_type_v1(std::string& out, const TypeRef& type) {
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

// ============================================================================
// Comprehensive Self-Contained Checked Body IR (Phase 25 A4 / Team H)
// ============================================================================

namespace checked_ir {

enum class ExprKind : uint32_t {
  Parameter = 0,
  IntegerLiteral = 1,
  Add = 2,
  Field = 3,
  VectorLiteral = 4,
  Call = 5,
  FloatLiteral = 6,
  BoolLiteral = 7,
  StringLiteral = 8,
  LocalRead = 9,
  Unary = 10,
  Binary = 11,
  Message = 12,
  MapLiteral = 13
};

enum class UnaryOp : uint32_t {
  Neg = 1,
  Not = 2
};

enum class BinaryOp : uint32_t {
  Add = 1,
  Sub = 2,
  Mul = 3,
  Div = 4,
  Mod = 5,
  Equal = 6,
  NotEqual = 7,
  Less = 8,
  LessEqual = 9,
  Greater = 10,
  GreaterEqual = 11,
  And = 12,
  Or = 13,
  Xor = 14
};

struct Expr {
  ExprKind kind = ExprKind::Parameter;
  TypeRef type;

  // Ordinals:
  // For Parameter: parameter index
  // For LocalRead: local binding index
  uint32_t ordinal = 0;

  // Literal values:
  std::string literal_value;

  // Operators:
  UnaryOp unary_op = UnaryOp::Neg;
  BinaryOp binary_op = BinaryOp::Add;

  // Children:
  std::vector<Expr> operands;

  // Exact target identifiers:
  // For Field: nominal declaration ID ("nominal-template-v1:...")
  // For Call: callable ID ("callable-v1:...")
  // For Message: handler callable ID ("callable-v1:...")
  std::string semantic_target_id;
  uint32_t member_ordinal = 0;
  std::string member_name;
  std::string target_domain;
};

enum class StmtKind : uint32_t {
  Let = 1,
  Var = 2,
  Assign = 3,
  AssignField = 4,
  AssignIndex = 5,
  ExprStmt = 6,
  Return = 7,
  Reply = 8,
  If = 9,
  While = 10,
  For = 11,
  Match = 12
};

struct Stmt;

struct CaseBranch {
  std::string variant_tag;
  std::string variant_id;
  std::vector<uint32_t> payload_ordinals;
  std::vector<TypeRef> payload_types;
  std::vector<Stmt> body;
};

struct Stmt {
  StmtKind kind = StmtKind::Return;
  TypeRef type;

  uint32_t local_ordinal = 0;
  std::optional<Expr> expr;
  std::vector<Expr> operands;
  std::string semantic_target_id;
  uint32_t member_ordinal = 0;
  std::string member_name;

  // Control flow blocks:
  std::vector<Stmt> then_branch;
  std::vector<Stmt> else_branch;
  std::vector<Stmt> body;
  std::vector<CaseBranch> match_cases;
};

inline void encode_checked_expr(std::string& out, const Expr& expr) {
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
      if (expr.operands.size() % 2 != 0 || !expr.semantic_target_id.empty() || !expr.literal_value.empty())
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

    case ExprKind::Message:
      if (expr.target_domain.empty() || expr.semantic_target_id.empty() || !expr.literal_value.empty())
        throw std::invalid_argument("invalid checked message node: missing domain or handler id");
      checked_body_field(out, expr.target_domain);
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_expr(out, child);
      break;
  }
}

inline void encode_checked_stmt(std::string& out, const Stmt& stmt) {
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
      if (stmt.operands.size() != 2 || !stmt.expr)
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

    case StmtKind::While:
      if (!stmt.expr)
        throw std::invalid_argument("invalid while statement: missing condition expr");
      encode_checked_expr(out, *stmt.expr);
      checked_body_field(out, std::to_string(stmt.body.size()));
      for (const auto& s : stmt.body) encode_checked_stmt(out, s);
      break;

    case StmtKind::For:
      if (!stmt.expr)
        throw std::invalid_argument("invalid for statement: missing iterable expr");
      checked_body_field(out, std::to_string(stmt.local_ordinal));
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
  }
}

inline std::string compute_checked_body_hash_v1(const std::vector<Stmt>& statements) {
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
  return "body-semantic-v1:sha256:" + source_free_contract_sha256(encoded);
}

} // namespace checked_ir

// ============================================================================
// Legacy compatibility wrappers for AST CheckedBodyExprV1 / CheckedBodyStmtV1
// ============================================================================

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

// Conversion from legacy AST nodes to rich checked_ir
inline checked_ir::Expr from_legacy_expr(const CheckedBodyExprV1& legacy) {
  checked_ir::Expr expr;
  expr.type = legacy.type;
  switch (legacy.kind) {
    case CheckedBodyExprV1::Kind::Parameter:
      expr.kind = checked_ir::ExprKind::Parameter;
      expr.ordinal = legacy.parameter_ordinal;
      break;
    case CheckedBodyExprV1::Kind::Integer:
      expr.kind = checked_ir::ExprKind::IntegerLiteral;
      expr.literal_value = legacy.integer_value;
      break;
    case CheckedBodyExprV1::Kind::Add:
      expr.kind = checked_ir::ExprKind::Add;
      for (const auto& op : legacy.operands)
        expr.operands.push_back(from_legacy_expr(op));
      break;
  }
  return expr;
}

inline checked_ir::Stmt from_legacy_stmt(const CheckedBodyStmtV1& legacy) {
  checked_ir::Stmt stmt;
  stmt.kind = checked_ir::StmtKind::Return;
  stmt.expr = from_legacy_expr(legacy.value);
  stmt.type = legacy.value.type;
  return stmt;
}

// ============================================================================
// Exact Semantic Dependency Model (Phase 25 A4 H1 / Team H)
// ============================================================================

struct SemanticDependency {
  enum class Kind {
    Callable,
    Handler,
    NominalType
  };

  Kind kind = Kind::Callable;
  std::string target_id;       // Exact semantic identifier
  std::string expected_hash;   // Expected canonical hash

  bool is_complete() const { return !expected_hash.empty(); }

  bool operator==(const SemanticDependency& o) const {
    return kind == o.kind && target_id == o.target_id && expected_hash == o.expected_hash;
  }
  bool operator!=(const SemanticDependency& o) const { return !(*this == o); }
  bool operator<(const SemanticDependency& o) const {
    if (target_id != o.target_id) return target_id < o.target_id;
    if (expected_hash != o.expected_hash) return expected_hash < o.expected_hash;
    return static_cast<int>(kind) < static_cast<int>(o.kind);
  }
};

inline void collect_expr_dependencies(const checked_ir::Expr& expr,
                                      std::set<SemanticDependency>& out) {
  if (expr.kind == checked_ir::ExprKind::Call) {
    if (expr.semantic_target_id.empty())
      throw std::invalid_argument("call expression has empty semantic target id");
    out.insert({SemanticDependency::Kind::Callable, expr.semantic_target_id, ""});
  } else if (expr.kind == checked_ir::ExprKind::Message) {
    if (expr.semantic_target_id.empty())
      throw std::invalid_argument("message expression has empty semantic target id");
    out.insert({SemanticDependency::Kind::Handler, expr.semantic_target_id, ""});
  } else if (expr.kind == checked_ir::ExprKind::Field) {
    if (!expr.semantic_target_id.empty()) {
      out.insert({SemanticDependency::Kind::NominalType, expr.semantic_target_id, ""});
    }
  }
  for (const auto& operand : expr.operands) {
    collect_expr_dependencies(operand, out);
  }
}

inline void collect_stmt_dependencies(const checked_ir::Stmt& stmt,
                                      std::set<SemanticDependency>& out) {
  if (stmt.expr) {
    collect_expr_dependencies(*stmt.expr, out);
  }
  for (const auto& op : stmt.operands) {
    collect_expr_dependencies(op, out);
  }
  if (stmt.kind == checked_ir::StmtKind::AssignField && !stmt.semantic_target_id.empty()) {
    out.insert({SemanticDependency::Kind::NominalType, stmt.semantic_target_id, ""});
  }
  for (const auto& s : stmt.then_branch) collect_stmt_dependencies(s, out);
  for (const auto& s : stmt.else_branch) collect_stmt_dependencies(s, out);
  for (const auto& s : stmt.body) collect_stmt_dependencies(s, out);
  for (const auto& c : stmt.match_cases) {
    for (const auto& s : c.body) collect_stmt_dependencies(s, out);
  }
}

inline std::vector<SemanticDependency> extract_semantic_dependencies(
    const std::vector<checked_ir::Stmt>& statements) {
  std::set<SemanticDependency> deps;
  for (const auto& stmt : statements) {
    collect_stmt_dependencies(stmt, deps);
  }
  return std::vector<SemanticDependency>(deps.begin(), deps.end());
}

inline bool is_canonical_dependency_hash(SemanticDependency::Kind kind, std::string_view hash) {
  std::string_view hex_part = hash;
  if (kind == SemanticDependency::Kind::Callable || kind == SemanticDependency::Kind::Handler) {
    constexpr std::string_view body_prefix = "body-semantic-v1:sha256:";
    constexpr std::string_view sha_prefix = "sha256:";
    if (hex_part.rfind(body_prefix, 0) == 0) {
      hex_part.remove_prefix(body_prefix.size());
    } else if (hex_part.rfind(sha_prefix, 0) == 0) {
      hex_part.remove_prefix(sha_prefix.size());
    } else {
      return false;
    }
  } else if (kind == SemanticDependency::Kind::NominalType) {
    constexpr std::string_view nominal_prefix = "nominal-instance-v1:sha256:";
    constexpr std::string_view type_key_prefix = "type-key-v1:sha256:";
    constexpr std::string_view sha_prefix = "sha256:";
    if (hex_part.rfind(nominal_prefix, 0) == 0) {
      hex_part.remove_prefix(nominal_prefix.size());
    } else if (hex_part.rfind(type_key_prefix, 0) == 0) {
      hex_part.remove_prefix(type_key_prefix.size());
    } else if (hex_part.rfind(sha_prefix, 0) == 0) {
      hex_part.remove_prefix(sha_prefix.size());
    } else {
      return false;
    }
  } else {
    return false;
  }

  if (hex_part.size() != 64) return false;
  return std::all_of(hex_part.begin(), hex_part.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

inline bool is_well_formed_hash_any_kind(std::string_view hash) {
  constexpr std::string_view p1 = "body-semantic-v1:sha256:";
  constexpr std::string_view p2 = "nominal-instance-v1:sha256:";
  constexpr std::string_view p3 = "type-key-v1:sha256:";
  constexpr std::string_view p4 = "sha256:";
  std::string_view hex_part = hash;
  if (hex_part.rfind(p1, 0) == 0) hex_part.remove_prefix(p1.size());
  else if (hex_part.rfind(p2, 0) == 0) hex_part.remove_prefix(p2.size());
  else if (hex_part.rfind(p3, 0) == 0) hex_part.remove_prefix(p3.size());
  else if (hex_part.rfind(p4, 0) == 0) hex_part.remove_prefix(p4.size());
  else return false;

  if (hex_part.size() != 64) return false;
  return std::all_of(hex_part.begin(), hex_part.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

inline bool is_canonical_semantic_hash(std::string_view hash) {
  return is_well_formed_hash_any_kind(hash);
}

struct EntityFacts {
  std::string id;
  std::string semantic_hash;
  std::vector<SemanticDependency> dependencies;
  bool is_supported = true;
  std::string unsupported_reason;
};

enum class ClosureValidationStatus {
  Valid,
  MissingDependency,
  MissingExpectedHash,
  InvalidHashKind,
  HashMismatch,
  ForgedHash,
  CyclicDependency,
  UnsupportedDependency
};

struct ClosureValidationResult {
  ClosureValidationStatus status = ClosureValidationStatus::Valid;
  std::string failing_id;
  std::string expected_hash;
  std::string actual_hash;
  std::vector<std::string> cycle_path;
  std::string error_message;

  bool ok() const { return status == ClosureValidationStatus::Valid; }
};

inline ClosureValidationResult validate_dependency_closure(
    const std::string& root_id,
    const std::vector<SemanticDependency>& root_deps,
    const std::map<std::string, EntityFacts>& environment) {
  ClosureValidationResult result;

  // 0 = unvisited, 1 = visiting (in recursion stack), 2 = visited
  std::map<std::string, int> visit_state;
  std::vector<std::string> current_path;

  std::function<bool(const std::string&, const std::vector<SemanticDependency>&)> dfs =
      [&](const std::string& id, const std::vector<SemanticDependency>& deps) -> bool {
    visit_state[id] = 1;
    current_path.push_back(id);

    for (const auto& dep : deps) {
      // 1. Expected hash MUST be present; an empty expected hash leaves the contract incomplete.
      if (dep.expected_hash.empty()) {
        result.status = ClosureValidationStatus::MissingExpectedHash;
        result.failing_id = dep.target_id;
        result.error_message = "missing expected semantic hash for dependency: " + dep.target_id;
        return false;
      }

      // 2. Validate expected hash well-formedness and kind
      if (!is_well_formed_hash_any_kind(dep.expected_hash)) {
        result.status = ClosureValidationStatus::ForgedHash;
        result.failing_id = dep.target_id;
        result.expected_hash = dep.expected_hash;
        result.error_message = "forged or non-canonical expected dependency hash for " + dep.target_id;
        return false;
      }
      if (!is_canonical_dependency_hash(dep.kind, dep.expected_hash)) {
        result.status = ClosureValidationStatus::InvalidHashKind;
        result.failing_id = dep.target_id;
        result.expected_hash = dep.expected_hash;
        result.error_message = "expected dependency hash kind mismatch for " + dep.target_id;
        return false;
      }

      // 3. Check missing in environment
      auto it = environment.find(dep.target_id);
      if (it == environment.end()) {
        result.status = ClosureValidationStatus::MissingDependency;
        result.failing_id = dep.target_id;
        result.expected_hash = dep.expected_hash;
        result.error_message = "missing semantic dependency: " + dep.target_id;
        return false;
      }

      const auto& fact = it->second;

      // 4. Check unsupported
      if (!fact.is_supported) {
        result.status = ClosureValidationStatus::UnsupportedDependency;
        result.failing_id = dep.target_id;
        result.error_message = "dependency has unsupported checked body: " + dep.target_id +
                               " (" + fact.unsupported_reason + ")";
        return false;
      }

      // 5. Validate actual hash well-formedness and kind
      if (!is_well_formed_hash_any_kind(fact.semantic_hash)) {
        result.status = ClosureValidationStatus::ForgedHash;
        result.failing_id = dep.target_id;
        result.actual_hash = fact.semantic_hash;
        result.error_message = "dependency has forged or non-canonical actual hash: " + dep.target_id;
        return false;
      }
      if (!is_canonical_dependency_hash(dep.kind, fact.semantic_hash)) {
        result.status = ClosureValidationStatus::InvalidHashKind;
        result.failing_id = dep.target_id;
        result.actual_hash = fact.semantic_hash;
        result.error_message = "actual dependency hash kind mismatch for " + dep.target_id;
        return false;
      }

      // 6. Check hash mismatch
      if (dep.expected_hash != fact.semantic_hash) {
        result.status = ClosureValidationStatus::HashMismatch;
        result.failing_id = dep.target_id;
        result.expected_hash = dep.expected_hash;
        result.actual_hash = fact.semantic_hash;
        result.error_message = "semantic hash mismatch for dependency " + dep.target_id +
                               ": expected " + dep.expected_hash + ", found " + fact.semantic_hash;
        return false;
      }

      // 7. Check cycle
      if (visit_state[dep.target_id] == 1) {
        result.status = ClosureValidationStatus::CyclicDependency;
        result.failing_id = dep.target_id;
        result.cycle_path = current_path;
        result.cycle_path.push_back(dep.target_id);
        result.error_message = "cyclic semantic dependency detected involving " + dep.target_id;
        return false;
      }

      if (visit_state[dep.target_id] == 0) {
        if (!dfs(dep.target_id, fact.dependencies))
          return false;
      }
    }

    current_path.pop_back();
    visit_state[id] = 2;
    return true;
  };

  dfs(root_id, root_deps);
  return result;
}

struct CheckedBodyFunction {
  std::string callable_id;
  std::vector<TypeRef> parameter_types;
  TypeRef result_type;
  std::vector<checked_ir::Stmt> statements;
  std::vector<SemanticDependency> dependencies;
  std::string body_semantic_hash;
  bool has_supported_body = true;
  std::string unsupported_reason;
};

inline CheckedBodyFunction make_checked_function(
    std::string callable_id,
    std::vector<TypeRef> params,
    TypeRef result_type,
    std::vector<checked_ir::Stmt> stmts,
    const std::map<std::string, std::string>& expected_hashes = {}) {
  CheckedBodyFunction fn;
  fn.callable_id = std::move(callable_id);
  fn.parameter_types = std::move(params);
  fn.result_type = std::move(result_type);
  fn.statements = std::move(stmts);
  try {
    fn.body_semantic_hash = checked_ir::compute_checked_body_hash_v1(fn.statements);
    fn.dependencies = extract_semantic_dependencies(fn.statements);
    bool has_incomplete_dependency = false;
    std::string missing_dep_id;
    for (auto& dep : fn.dependencies) {
      auto found = expected_hashes.find(dep.target_id);
      if (found != expected_hashes.end()) {
        dep.expected_hash = found->second;
      }
      if (dep.expected_hash.empty()) {
        has_incomplete_dependency = true;
        if (missing_dep_id.empty()) missing_dep_id = dep.target_id;
      }
    }
    if (has_incomplete_dependency) {
      fn.has_supported_body = false;
      fn.unsupported_reason = "incomplete_dependency_contract: missing expected hash for " + missing_dep_id;
      fn.body_semantic_hash.clear();
    } else {
      fn.has_supported_body = true;
    }
  } catch (const std::exception& e) {
    fn.has_supported_body = false;
    fn.unsupported_reason = e.what();
    fn.body_semantic_hash.clear();
  }
  return fn;
}

}  // namespace moss
