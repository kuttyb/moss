#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
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
  MapLiteral = 13,
  NominalConstruct = 14,
  // Additive ABI-v10 checked-body-v1 tags. Keep existing values fixed: the
  // encoder's canonical bytes and hashes are part of the sealed interface.
  CollectionCount = 15,
  Index = 16,
  // Generic member calls link to a checker-owned summary requirement by ID.
  MemberCall = 17,
  // Statically checked mutable Vector.push, retained as an explicit checked
  // operation so the reader can validate its WRITE target and element type.
  CollectionPush = 18
};

inline constexpr std::string_view kCollectionCountOperationId =
    "builtin:collection-count-v1";
inline constexpr std::string_view kCollectionIndexOperationId =
    "builtin:index";
inline constexpr std::string_view kMemberRequirementPrefix =
    "member-requirement-v1:";
inline constexpr std::string_view kCollectionPushOperationId =
    "builtin:vector-push-v1";

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
  Match = 12,
  Pass = 13,
  IfJoin = 14
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

    default:
      throw std::invalid_argument("unknown checked body expression kind");
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

inline std::string encode_checked_body_v1(const std::vector<Stmt>& statements) {
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

inline std::string compute_checked_body_hash_v1(const std::vector<Stmt>& statements) {
  return "body-semantic-v1:sha256:" +
      source_free_contract_sha256(encode_checked_body_v1(statements));
}

// The source-free rich-body importer accepts the checked H3 straight-line
// form and single-return integer arithmetic. It resolves declaration IDs
// against the importing TypeContext and proves parameter/result types and the
// canonical byte shape; unsupported node kinds fail closed instead of falling
// back to source text.
class H3BodyReader {
 public:
  H3BodyReader(std::string_view bytes, const TypeContext& context,
               const std::vector<TypeRef>& parameters, const TypeRef& result,
               bool allow_empty_vector_return = false,
               bool allow_collection_loop_body = false,
               bool allow_vector_literal_relation = false,
               bool allow_member_call = false,
               bool allow_generic_map_index_mutation = false,
               bool allow_typed_enum_match = false)
      : bytes_(bytes), context_(context), parameters_(parameters), result_(result),
        allow_empty_vector_return_(allow_empty_vector_return),
        allow_collection_loop_body_(allow_collection_loop_body),
        allow_vector_literal_relation_(allow_vector_literal_relation),
        allow_member_call_(allow_member_call),
        allow_generic_map_index_mutation_(allow_generic_map_index_mutation),
        allow_typed_enum_match_(allow_typed_enum_match) {}

  std::vector<Stmt> read() {
    constexpr std::string_view header = "moss-checked-body-v1\n";
    if (bytes_.substr(0, header.size()) != header)
      fail("checked body version is unsupported");
    position_ = header.size();
    std::vector<Stmt> statements;
    const auto statement_count = number();
    if (allow_collection_loop_body_) {
      if (bytes_.size() > (1u << 20) || statement_count == 0 ||
          statement_count > kMaxBodyNodes)
        fail("collection checked body exceeds bounds");
      if (statement_count == 1) {
        const auto marker_position = position_;
        const auto first_field = field();
        position_ = marker_position;
        if (first_field == "return") {
          statements.push_back(read_single_return());
        } else {
          scopes_.emplace_back();
          statements.push_back(read_loop_stmt(0));
          scopes_.pop_back();
        }
      } else {
        scopes_.emplace_back();
        read_loop_statements(statement_count, 0, statements);
        scopes_.pop_back();
      }
    } else if (statement_count == 1) {
      statements.push_back(read_single_return());
    } else if (statement_count == 2) {
      statements.push_back(read_let());
      statements.push_back(read_return());
    } else {
      fail("checked body statement shape is unsupported");
    }
    if (position_ != bytes_.size() ||
        encode_checked_body_v1(statements) != bytes_)
      fail("H3 checked body is noncanonical");
    return statements;
  }

 private:
  std::string_view bytes_;
  const TypeContext& context_;
  const std::vector<TypeRef>& parameters_;
  TypeRef result_;
  std::size_t position_ = 0;
  TypeRef local_type_;
  bool allow_empty_vector_return_ = false;
  bool allow_collection_loop_body_ = false;
  bool allow_vector_literal_relation_ = false;
  bool allow_member_call_ = false;
  bool allow_generic_map_index_mutation_ = false;
  bool allow_typed_enum_match_ = false;
  static constexpr std::uint32_t kMaxBodyNodes = 4096;
  static constexpr std::uint32_t kMaxLocalCount = 256;
  static constexpr unsigned kMaxBlockDepth = 32;
  std::uint32_t node_count_ = 0;
  std::uint32_t next_local_ordinal_ = 0;
  struct LocalBinding {
    TypeRef type;
    bool mutable_binding = false;
  };
  std::map<std::uint32_t, LocalBinding> locals_;
  std::vector<std::vector<std::uint32_t>> scopes_;

  [[noreturn]] static void fail(const char* message) {
    throw std::invalid_argument(message);
  }

  std::string field() {
    const std::size_t begin = position_;
    while (position_ < bytes_.size() &&
           std::isdigit(static_cast<unsigned char>(bytes_[position_])))
      ++position_;
    if (begin == position_ || position_ >= bytes_.size() ||
        bytes_[position_] != ':' ||
        (position_ - begin > 1 && bytes_[begin] == '0'))
      fail("malformed checked body field length");
    std::size_t length = 0;
    auto parsed = std::from_chars(bytes_.data() + begin,
                                  bytes_.data() + position_, length);
    if (parsed.ec != std::errc{} || ++position_ > bytes_.size() ||
        length > bytes_.size() - position_ || length > (1u << 20))
      fail("checked body field exceeds bounds");
    std::string value(bytes_.substr(position_, length));
    position_ += length;
    return value;
  }

  std::uint32_t number() {
    const std::string value = field();
    if (value.empty() || (value.size() > 1 && value[0] == '0'))
      fail("checked body integer is noncanonical");
    std::uint32_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                  result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
      fail("checked body integer is invalid");
    return result;
  }

  TypeRef read_type(unsigned depth = 0) {
    if (depth >= 64) fail("checked body type is too deep");
    const auto tag = number();
    if (tag > static_cast<std::uint32_t>(TypeKind::Unit))
      fail("checked body type kind is unknown");
    const auto kind = static_cast<TypeKind>(tag);
    const std::string identity = field();
    const auto type_count = number();
    if (type_count > 64) fail("checked body type has too many arguments");
    std::vector<TypeRef> arguments;
    for (std::uint32_t index = 0; index < type_count; ++index)
      arguments.push_back(read_type(depth + 1));
    const auto field_count = number();
    if (field_count > 64) fail("checked body type has too many fields");
    std::vector<std::pair<std::string, TypeRef>> fields;
    for (std::uint32_t index = 0; index < field_count; ++index) {
      std::string name = field();
      TypeRef value = read_type(depth + 1);
      fields.emplace_back(std::move(name), std::move(value));
    }
    if (kind == TypeKind::InferenceVar) {
      if (type_count || field_count) fail("checked body variable has payload");
      std::uint32_t ordinal = 0;
      auto parsed = std::from_chars(identity.data(),
                                    identity.data() + identity.size(), ordinal);
      if (identity.empty() || (identity.size() > 1 && identity[0] == '0') ||
          parsed.ec != std::errc{} ||
          parsed.ptr != identity.data() + identity.size())
        fail("checked body variable ordinal is invalid");
      return Type::var(TypeVarId{0, 0, ordinal});
    }
    if (kind == TypeKind::Primitive && !identity.empty() &&
        !type_count && !field_count)
      return Type::primitive(identity);
    if (kind == TypeKind::Builtin && !identity.empty() && !field_count)
      return Type::builtin(identity, std::move(arguments));
    if (kind == TypeKind::Unit &&
        (identity.empty() || identity == "unit") && !type_count &&
        !field_count)
      return Type::unit();
    if (kind == TypeKind::NominalInstance && !type_count) {
      auto declaration = context_.find_id(identity);
      if (!declaration || declaration->kind != DeclarationKind::Record ||
          declaration->inferred_slot_count() != fields.size())
        fail("checked body nominal template ID is unknown or mismatched");
      return Type::nominal_bound(declaration, declaration->program_name,
                                 std::move(fields));
    }
    if (kind == TypeKind::Enum && !identity.empty() && !type_count &&
        !field_count) {
      auto declaration = context_.find_id(identity);
      if (!declaration || declaration->kind != DeclarationKind::Enum)
        fail("checked body enum declaration identity is unknown or mismatched");
      auto type = Type::enumeration_bound(declaration,
                                          declaration->program_name);
      if (!type) fail("checked body enum declaration is invalid");
      return type;
    }
    fail("checked body type shape is unsupported");
  }

  static bool is_integer_type(const TypeRef& type) {
    return type && type->kind == TypeKind::Primitive && type->name == "int";
  }

  static bool is_bool_type(const TypeRef& type) {
    return type && type->kind == TypeKind::Primitive && type->name == "bool";
  }

  static bool is_collection_type(const TypeRef& type) {
    return type && type->kind == TypeKind::Builtin &&
        type->name == "vector" && type->type_args.size() == 1 &&
        type->type_args.front();
  }

  bool is_generic_parameter_type(const TypeRef& type) const {
    return type && type->kind == TypeKind::InferenceVar &&
        std::any_of(parameters_.begin(), parameters_.end(), [&](const TypeRef& p) {
          return p && p->structurally_equal(*type);
        });
  }

  bool arithmetic_types_match(const TypeRef& result, const TypeRef& left,
                              const TypeRef& right) const {
    if (is_integer_type(result))
      return is_integer_type(left) && is_integer_type(right);
    if (!is_generic_parameter_type(result)) return false;
    const bool left_generic = left && left->structurally_equal(*result);
    const bool right_generic = right && right->structurally_equal(*result);
    const bool left_integer = is_integer_type(left);
    const bool right_integer = is_integer_type(right);
    return (left_generic || left_integer) &&
           (right_generic || right_integer) &&
           (left_generic || right_generic);
  }

  static bool is_canonical_integer(std::string_view value) {
    if (value.empty()) return false;
    std::size_t first_digit = value.front() == '-' ? 1 : 0;
    if (first_digit == value.size()) return false;
    if (value[first_digit] == '0' && value.size() - first_digit > 1)
      return false;
    for (std::size_t index = first_digit; index < value.size(); ++index)
      if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    return true;
  }

  static bool is_decimal_float(std::string_view value) {
    std::size_t index = 0;
    if (index < value.size() && (value[index] == '+' || value[index] == '-'))
      ++index;
    auto digits = [&] {
      const auto start = index;
      while (index < value.size() &&
             std::isdigit(static_cast<unsigned char>(value[index])))
        ++index;
      return index != start;
    };
    const bool before_point = digits();
    const bool point = index < value.size() && value[index] == '.';
    bool after_point = false;
    if (point) {
      ++index;
      after_point = digits();
    }
    if (!before_point && !after_point) return false;
    if (index < value.size() && (value[index] == 'e' || value[index] == 'E')) {
      ++index;
      if (index < value.size() && (value[index] == '+' || value[index] == '-'))
        ++index;
      if (!digits()) return false;
    }
    return index == value.size();
  }

  static TypeRef resolved_field_type(const TypeRef& receiver,
                                    std::uint32_t ordinal,
                                    std::string_view target_id) {
    if (!receiver || receiver->kind != TypeKind::NominalInstance ||
        !receiver->declaration ||
        receiver->declaration->kind != DeclarationKind::Record ||
        receiver->declaration->id != target_id ||
        ordinal >= receiver->declaration->fields.size())
      return nullptr;

    const auto& declaration = receiver->declaration;
    if (receiver->field_substitutions.size() !=
        declaration->inferred_slot_count())
      return nullptr;
    std::size_t substitution_index = 0;
    for (const auto& field : declaration->fields) {
      if (!field.inferred) continue;
      const auto& substitution =
          receiver->field_substitutions[substitution_index++];
      if (substitution.first != field.name || !substitution.second)
        return nullptr;
    }
    const auto& slot = declaration->fields[ordinal];
    if (slot.inferred) {
      std::size_t inferred_ordinal = 0;
      for (std::size_t index = 0; index < ordinal; ++index)
        if (declaration->fields[index].inferred) ++inferred_ordinal;
      if (inferred_ordinal >= receiver->field_substitutions.size()) return nullptr;
      const auto& substitution = receiver->field_substitutions[inferred_ordinal];
      if (substitution.first != slot.name || !substitution.second) return nullptr;
      return substitution.second;
    }

    auto owner = declaration->owner.lock();
    if (!owner) return nullptr;
    const auto& annotations = owner->field_annotations(declaration->id);
    if (ordinal >= annotations.size()) return nullptr;
    return annotations[ordinal];
  }

  static bool is_integer_arithmetic(BinaryOp operation) {
    switch (operation) {
      case BinaryOp::Add:
      case BinaryOp::Sub:
      case BinaryOp::Mul:
      case BinaryOp::Div:
      case BinaryOp::Mod:
        return true;
      default:
        return false;
    }
  }

  Expr read_expr(unsigned depth = 0) {
    if (depth >= 64) fail("checked body expression is too deep");
    if (++node_count_ > kMaxBodyNodes)
      fail("checked body node count exceeds bounds");
    const auto tag = number();
    if (tag > static_cast<std::uint32_t>(ExprKind::CollectionPush))
      fail("checked body expression kind is unknown");
    Expr expression;
    expression.kind = static_cast<ExprKind>(tag);
    if ((expression.kind == ExprKind::CollectionCount ||
         expression.kind == ExprKind::Index ||
         expression.kind == ExprKind::CollectionPush) &&
        !allow_collection_loop_body_)
      fail("checked body collection operation is outside the H3 subset");
    expression.type = read_type();
    if (expression.kind == ExprKind::IntegerLiteral) {
      expression.literal_value = field();
      if (!is_integer_type(expression.type) ||
          !is_canonical_integer(expression.literal_value))
        fail("checked body integer literal is invalid or mistyped");
    } else if (expression.kind == ExprKind::FloatLiteral) {
      expression.literal_value = field();
      if (!expression.type || expression.type->kind != TypeKind::Primitive ||
          expression.type->name != "float" ||
          !is_decimal_float(expression.literal_value))
        fail("checked body float literal is invalid or mistyped");
    } else if (expression.kind == ExprKind::BoolLiteral) {
      const auto value = field();
      if (!expression.type || expression.type->kind != TypeKind::Primitive ||
          expression.type->name != "bool" || (value != "0" && value != "1"))
        fail("checked body bool literal is invalid or mistyped");
      expression.literal_value = value == "1" ? "true" : "false";
    } else if (expression.kind == ExprKind::StringLiteral) {
      expression.literal_value = field();
      if (!expression.type || expression.type->kind != TypeKind::Primitive ||
          expression.type->name != "string")
        fail("checked body string literal is mistyped");
    } else if (expression.kind == ExprKind::Parameter) {
      expression.ordinal = number();
      if (expression.ordinal >= parameters_.size() ||
          !expression.type->structurally_equal(*parameters_[expression.ordinal]))
        fail("checked body parameter type is mismatched");
    } else if (expression.kind == ExprKind::Add) {
      if (number() != 2)
        fail("checked body addition has an unsupported type or arity");
      expression.operands.push_back(read_expr(depth + 1));
      expression.operands.push_back(read_expr(depth + 1));
      if (!arithmetic_types_match(expression.type,
                                  expression.operands[0].type,
                                  expression.operands[1].type))
        fail("checked body addition operand type is mismatched");
    } else if (expression.kind == ExprKind::Binary) {
      const auto operation = number();
      if (operation < static_cast<std::uint32_t>(BinaryOp::Add) ||
          operation > static_cast<std::uint32_t>(BinaryOp::Xor))
        fail("checked body binary operator is unknown");
      expression.binary_op = static_cast<BinaryOp>(operation);
      expression.operands.push_back(read_expr(depth + 1));
      expression.operands.push_back(read_expr(depth + 1));
      const auto& left = expression.operands[0].type;
      const auto& right = expression.operands[1].type;
      if (is_integer_arithmetic(expression.binary_op)) {
        if (!arithmetic_types_match(expression.type, left, right))
          fail("checked body binary operand type is mismatched");
      } else if (allow_collection_loop_body_ &&
                 (expression.binary_op == BinaryOp::Equal ||
                  expression.binary_op == BinaryOp::NotEqual)) {
        if (!is_bool_type(expression.type) || !left || !right ||
            !left->structurally_equal(*right) ||
            !(is_generic_parameter_type(left) ||
              (left->kind == TypeKind::Primitive &&
               (left->name == "int" || left->name == "float" ||
                left->name == "bool" || left->name == "string" ||
                left->name == "char"))))
          fail("checked body equality operand type is mismatched");
      } else if (allow_collection_loop_body_ &&
                 (expression.binary_op == BinaryOp::Less ||
                  expression.binary_op == BinaryOp::LessEqual ||
                  expression.binary_op == BinaryOp::Greater ||
                  expression.binary_op == BinaryOp::GreaterEqual)) {
        const bool same_ordering_type = left && right &&
            left->structurally_equal(*right) &&
            (is_integer_type(left) ||
             (left->kind == TypeKind::Primitive &&
              (left->name == "float" || left->name == "char")));
        const bool mixed_open_integer = left && right &&
            ((is_generic_parameter_type(left) && is_integer_type(right)) ||
             (is_integer_type(left) && is_generic_parameter_type(right)));
        if (!is_bool_type(expression.type) ||
            (!same_ordering_type && !mixed_open_integer))
          fail("checked body ordering operand type is mismatched");
      } else if (allow_collection_loop_body_ &&
                 (expression.binary_op == BinaryOp::And ||
                  expression.binary_op == BinaryOp::Or ||
                  expression.binary_op == BinaryOp::Xor)) {
        if (!is_bool_type(expression.type) || !is_bool_type(left) ||
            !is_bool_type(right))
          fail("checked body boolean operand type is mismatched");
      } else {
        fail("checked body binary operator is outside the collection subset");
      }
    } else if (expression.kind == ExprKind::LocalRead) {
      expression.ordinal = number();
      if (allow_collection_loop_body_) {
        const auto found = locals_.find(expression.ordinal);
        if (found == locals_.end() || !found->second.type ||
            !expression.type->structurally_equal(*found->second.type))
          fail("checked body local is undeclared or mistyped");
      } else if (expression.ordinal != 0 || !local_type_ ||
                 !expression.type->structurally_equal(*local_type_)) {
        fail("checked body local type is mismatched");
      }
    } else if (expression.kind == ExprKind::MapLiteral) {
      if (!allow_collection_loop_body_ ||
          expression.type->kind != TypeKind::Builtin ||
          expression.type->name != "map" ||
          expression.type->type_args.size() != 2)
        fail("checked body map literal has an unsupported type or context");
      const auto count = number();
      if (count > 32) fail("checked body map literal exceeds bounds");
      for (std::uint32_t index = 0; index < count * 2; ++index)
        expression.operands.push_back(read_expr(depth + 1));
      for (std::size_t index = 0; index < expression.operands.size(); index += 2)
        if (!expression.type->type_args[0]->structurally_equal(
                *expression.operands[index].type) ||
            !expression.type->type_args[1]->structurally_equal(
                *expression.operands[index + 1].type))
          fail("checked body map literal key or value TypeRef is mismatched");
    } else if (expression.kind == ExprKind::Field) {
      expression.semantic_target_id = field();
      expression.member_ordinal = number();
      expression.operands.push_back(read_expr(depth + 1));
      const auto field_type = resolved_field_type(
          expression.operands.front().type, expression.member_ordinal,
          expression.semantic_target_id);
      if (!field_type || !field_type->structurally_equal(*expression.type))
        fail("checked body field identity or type is mismatched");
      expression.member_name = expression.operands.front().type->declaration
          ->fields[expression.member_ordinal].name;
    } else if (expression.kind == ExprKind::VectorLiteral &&
               allow_vector_literal_relation_) {
      const auto count = number();
      if (count == 0 || count > 64 ||
          expression.type->kind != TypeKind::Builtin ||
          expression.type->name != "vector" ||
          expression.type->type_args.size() != 1 ||
          (result_ && !expression.type->structurally_equal(*result_)))
        fail("checked vector literal relation has an unsupported type or arity");
      for (std::uint32_t index = 0; index < count; ++index)
        expression.operands.push_back(read_expr(depth + 1));
    } else if (expression.kind == ExprKind::VectorLiteral &&
               allow_empty_vector_return_) {
      if (number() != 0 ||
          expression.type->kind != TypeKind::Builtin ||
          expression.type->name != "vector" ||
          expression.type->type_args.size() != 1 ||
          (result_ && !expression.type->structurally_equal(*result_)))
        fail("checked empty-vector return type or arity is mismatched");
    } else if (expression.kind == ExprKind::CollectionCount) {
      expression.semantic_target_id = field();
      if (expression.semantic_target_id != kCollectionCountOperationId)
        fail("checked collection-count operation identity is unknown");
      expression.operands.push_back(read_expr(depth + 1));
      if (!is_integer_type(expression.type) ||
          !is_collection_type(expression.operands.front().type))
        fail("checked collection-count receiver or result type is mismatched");
    } else if (expression.kind == ExprKind::Index) {
      expression.semantic_target_id = field();
      if (expression.semantic_target_id != kCollectionIndexOperationId)
        fail("checked index operation identity is unknown");
      expression.operands.push_back(read_expr(depth + 1));
      expression.operands.push_back(read_expr(depth + 1));
      const auto& container = expression.operands[0].type;
      const auto& index = expression.operands[1].type;
      const bool sequence_index = container &&
          container->kind == TypeKind::Builtin &&
          (container->name == "vector" || container->name == "queue") &&
          container->type_args.size() == 1 && is_integer_type(index) &&
          container->type_args.front()->structurally_equal(*expression.type);
      const bool map_index = container &&
          container->kind == TypeKind::Builtin && container->name == "map" &&
          container->type_args.size() == 2 &&
          container->type_args[0]->structurally_equal(*index) &&
          container->type_args[1]->structurally_equal(*expression.type);
      const bool generic_map_index = allow_generic_map_index_mutation_ &&
          container && container->kind == TypeKind::InferenceVar &&
          expression.operands[0].kind == ExprKind::Parameter &&
          expression.operands[0].ordinal < parameters_.size() &&
          parameters_[expression.operands[0].ordinal] &&
          parameters_[expression.operands[0].ordinal]->structurally_equal(
              *container);
      if (!sequence_index && !map_index && !generic_map_index)
        fail("checked index receiver, index, or result TypeRef is mismatched");
    } else if (expression.kind == ExprKind::MemberCall) {
      if (!allow_member_call_)
        fail("checked member call is outside the unified ABI-v10 reader");
      expression.semantic_target_id = field();
      expression.member_ordinal = number();
      expression.member_name = field();
      const auto count = number();
      if (expression.semantic_target_id.rfind(kMemberRequirementPrefix, 0) != 0 ||
          expression.member_name.empty() || expression.member_ordinal >= 4096 ||
          count == 0 || count > 65)
        fail("checked member call requirement identity or arity is invalid");
      for (std::uint32_t index = 0; index < count; ++index)
        expression.operands.push_back(read_expr(depth + 1));
    } else if (expression.kind == ExprKind::CollectionPush) {
      if (!allow_collection_loop_body_)
        fail("checked Vector.push is outside the collection-loop reader");
      expression.semantic_target_id = field();
      const auto count = number();
      if (expression.semantic_target_id != kCollectionPushOperationId ||
          count != 2 || expression.type->kind != TypeKind::Unit)
        fail("checked Vector.push identity, result, or arity is invalid");
      for (std::uint32_t index = 0; index < count; ++index)
        expression.operands.push_back(read_expr(depth + 1));
      const auto& receiver = expression.operands[0].type;
      const auto& value = expression.operands[1].type;
      if (!receiver || receiver->kind != TypeKind::Builtin ||
          receiver->name != "vector" || receiver->type_args.size() != 1 ||
          !receiver->type_args.front()->structurally_equal(*value))
        fail("checked Vector.push receiver and element TypeRefs disagree");
    } else if (expression.kind == ExprKind::Call ||
               expression.kind == ExprKind::NominalConstruct) {
      expression.semantic_target_id = field();
      const auto count = number();
      if (count > 64) fail("checked body expression has too many operands");
      if (expression.kind == ExprKind::Call) {
        const bool builtin_assert =
            expression.semantic_target_id == "builtin:assert";
        if (!builtin_assert &&
            expression.semantic_target_id.rfind("callable-v1:", 0) != 0)
          fail("checked body call target is not a callable ID");
        for (std::uint32_t index = 0; index < count; ++index)
          expression.operands.push_back(read_expr(depth + 1));
        if (builtin_assert &&
            (count != 1 || expression.type->kind != TypeKind::Unit ||
             !is_bool_type(expression.operands.front().type)))
          fail("checked assert must consume one Bool and return Unit");
      } else {
        if (expression.type->kind != TypeKind::NominalInstance ||
            !expression.type->declaration ||
            expression.semantic_target_id != expression.type->declaration->id ||
            count != expression.type->declaration->fields.size())
          fail("checked body constructor target is mismatched");
        for (std::uint32_t index = 0; index < count; ++index) {
          const auto& slot = expression.type->declaration->fields[index];
          if (field() != expression.semantic_target_id + ":field:" +
                             std::to_string(index) + ":" + slot.name)
            fail("checked body constructor field ID is mismatched");
          expression.operands.push_back(read_expr(depth + 1));
        }
      }
    } else {
      fail("checked body expression is outside the H3 subset");
    }
    return expression;
  }

  void declare_loop_local(std::uint32_t ordinal, const TypeRef& type,
                          bool mutable_binding) {
    if (ordinal != next_local_ordinal_ || ordinal >= kMaxLocalCount ||
        !type || scopes_.empty() || locals_.count(ordinal))
      fail("checked body local declaration ordinal or scope is invalid");
    ++next_local_ordinal_;
    locals_.emplace(ordinal, LocalBinding{type, mutable_binding});
    scopes_.back().push_back(ordinal);
  }

  void read_loop_block(std::uint32_t count, unsigned depth,
                       std::vector<Stmt>& output) {
    if (depth > kMaxBlockDepth || count > kMaxBodyNodes - node_count_)
      fail("checked body block exceeds bounds");
    scopes_.emplace_back();
    output.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
      output.push_back(read_loop_stmt(depth));
    for (const auto ordinal : scopes_.back()) locals_.erase(ordinal);
    scopes_.pop_back();
  }

  void read_loop_statements(std::uint32_t count, unsigned depth,
                            std::vector<Stmt>& output) {
    if (count > kMaxBodyNodes - node_count_)
      fail("checked body statement count exceeds bounds");
    output.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
      output.push_back(read_loop_stmt(depth));
  }

  Stmt read_loop_stmt(unsigned depth) {
    if (++node_count_ > kMaxBodyNodes)
      fail("checked body node count exceeds bounds");
    const auto tag = number();
    if (tag < static_cast<std::uint32_t>(StmtKind::Let) ||
        tag > static_cast<std::uint32_t>(StmtKind::IfJoin))
      fail("checked body statement kind is unknown");
    Stmt statement;
    statement.kind = static_cast<StmtKind>(tag);
    switch (statement.kind) {
      case StmtKind::Let:
      case StmtKind::Var: {
        statement.local_ordinal = number();
        statement.type = read_type();
        statement.expr = read_expr();
        if (!statement.type->structurally_equal(*statement.expr->type))
          fail("checked body local initializer TypeRef is mismatched");
        declare_loop_local(statement.local_ordinal, statement.type,
                           statement.kind == StmtKind::Var);
        break;
      }
      case StmtKind::Assign: {
        statement.local_ordinal = number();
        statement.expr = read_expr();
        const auto found = locals_.find(statement.local_ordinal);
        if (found == locals_.end() || !found->second.mutable_binding)
          fail("checked body assignment target is undeclared or immutable");
        if (!found->second.type->structurally_equal(*statement.expr->type))
          fail("checked body assignment TypeRef is mismatched");
        statement.type = found->second.type;
        break;
      }
      case StmtKind::AssignIndex: {
        statement.operands.push_back(read_expr());
        statement.operands.push_back(read_expr());
        statement.expr = read_expr();
        const auto& receiver = statement.operands[0];
        const auto& index = statement.operands[1];
        const bool local_map = receiver.kind == ExprKind::LocalRead &&
            receiver.ordinal < locals_.size() &&
            locals_.at(receiver.ordinal).mutable_binding && receiver.type &&
            receiver.type->kind == TypeKind::Builtin &&
            receiver.type->name == "map" && receiver.type->type_args.size() == 2 &&
            receiver.type->type_args[0]->structurally_equal(*index.type) &&
            receiver.type->type_args[1]->structurally_equal(*statement.expr->type);
        const bool generic_map_parameter = allow_generic_map_index_mutation_ &&
            receiver.kind == ExprKind::Parameter &&
            receiver.ordinal < parameters_.size() && receiver.type &&
            receiver.type->kind == TypeKind::InferenceVar &&
            parameters_[receiver.ordinal] &&
            parameters_[receiver.ordinal]->structurally_equal(*receiver.type);
        if (!local_map && !generic_map_parameter)
          fail("checked body indexed assignment target is not a mutable typed local Map");
        break;
      }
      case StmtKind::If: {
        statement.expr = read_expr();
        if (!is_bool_type(statement.expr->type))
          fail("checked body if condition is not Bool");
        const auto then_count = number();
        read_loop_block(then_count, depth + 1, statement.then_branch);
        const auto else_count = number();
        read_loop_block(else_count, depth + 1, statement.else_branch);
        break;
      }
      case StmtKind::IfJoin: {
        statement.local_ordinal = number();
        statement.type = read_type();
        statement.expr = read_expr();
        statement.operands.push_back(read_expr());
        statement.operands.push_back(read_expr());
        if (!is_bool_type(statement.expr->type) ||
            !statement.type->structurally_equal(*statement.operands[0].type) ||
            !statement.type->structurally_equal(*statement.operands[1].type))
          fail("checked if-join condition or branch TypeRef is mismatched");
        declare_loop_local(statement.local_ordinal, statement.type, false);
        break;
      }
      case StmtKind::While: {
        statement.expr = read_expr();
        if (!is_bool_type(statement.expr->type))
          fail("checked body while condition is not Bool");
        const auto body_count = number();
        read_loop_block(body_count, depth + 1, statement.body);
        break;
      }
      case StmtKind::Return: {
        const auto has_value = number();
        if (has_value != 1 || !result_)
          fail("checked body return value is missing or unsupported");
        statement.expr = read_expr();
        if (!statement.expr->type->structurally_equal(*result_))
          fail("checked body return TypeRef is mismatched");
        statement.type = result_;
        break;
      }
      case StmtKind::Pass:
        break;
      case StmtKind::ExprStmt: {
        if (!allow_collection_loop_body_)
          fail("checked expression statement is outside the collection-loop subset");
        statement.expr = read_expr();
        const auto& expression = *statement.expr;
        const bool checked_assert =
            expression.kind == ExprKind::Call &&
            expression.semantic_target_id == "builtin:assert";
        const bool checked_push =
            expression.kind == ExprKind::CollectionPush;
        if (!checked_assert && !checked_push)
          fail("checked expression statement is not a validated assertion or mutation");
        if (checked_push) {
          if (expression.operands.size() != 2 ||
              expression.operands.front().kind != ExprKind::LocalRead)
            fail("checked Vector.push receiver is not a local binding");
          const auto local = locals_.find(expression.operands.front().ordinal);
          if (local == locals_.end() || !local->second.mutable_binding)
            fail("checked Vector.push target is undeclared or not writable");
        }
        break;
      }
      case StmtKind::AssignField:
      case StmtKind::Reply:
        fail("checked body statement is outside the collection-loop subset");
      case StmtKind::For: {
        statement.local_ordinal = number();
        statement.type = read_type();
        statement.expr = read_expr();
        const auto body_count = number();
        if (!body_count)
          fail("checked body for loop has an empty body");
        if (statement.expr->type->kind == TypeKind::Builtin &&
            (statement.expr->type->name == "vector" ||
             statement.expr->type->name == "seq") &&
            statement.expr->type->type_args.size() == 1 &&
            !statement.expr->type->type_args.front()->structurally_equal(
                *statement.type))
          fail("checked body for binder TypeRef disagrees with iterable element");
        if (statement.expr->type->kind == TypeKind::Primitive &&
            statement.expr->type->name == "Range" &&
            !statement.type->structurally_equal(*Type::primitive("int")))
          fail("checked body Range binder TypeRef is not Int");
        scopes_.emplace_back();
        declare_loop_local(statement.local_ordinal, statement.type, false);
        read_loop_block(body_count, depth + 1, statement.body);
        for (const auto ordinal : scopes_.back()) locals_.erase(ordinal);
        scopes_.pop_back();
        break;
      }
      case StmtKind::Match: {
        if (!allow_typed_enum_match_)
          fail("checked body match is outside the typed-enum subset");
        statement.expr = read_expr();
        const auto& subject = statement.expr->type;
        if (!subject || subject->kind != TypeKind::Enum ||
            !subject->declaration ||
            subject->declaration->kind != DeclarationKind::Enum)
          fail("checked body match subject is not a closed enum identity");
        const auto case_count = number();
        if (!case_count || case_count > 128)
          fail("checked body match case count is invalid");
        std::set<std::string> variant_ids;
        std::set<std::string> variant_tags;
        statement.match_cases.reserve(case_count);
        for (std::uint32_t index = 0; index < case_count; ++index) {
          CaseBranch branch;
          branch.variant_tag = field();
          branch.variant_id = field();
          const std::string expected_id =
              "nominal-enum-variant-v1:" + subject->declaration->id +
              ":case:" + branch.variant_tag;
          if (branch.variant_tag.empty() || branch.variant_id != expected_id ||
              !variant_ids.insert(branch.variant_id).second ||
              !variant_tags.insert(branch.variant_tag).second)
            fail("checked body match variant identity is duplicate or mismatched");
          const auto payload_count = number();
          if (payload_count > 64)
            fail("checked body match payload count exceeds bounds");
          for (std::uint32_t payload = 0; payload < payload_count; ++payload)
            branch.payload_ordinals.push_back(number());
          const auto payload_type_count = number();
          if (payload_type_count != payload_count)
            fail("checked body match payload ordinal and type counts differ");
          for (std::uint32_t payload = 0; payload < payload_type_count; ++payload)
            branch.payload_types.push_back(read_type());
          const auto body_count = number();
          if (!body_count || body_count > kMaxBodyNodes - node_count_)
            fail("checked body match arm is empty or exceeds bounds");
          std::set<std::uint32_t> payload_ordinals;
          scopes_.emplace_back();
          for (std::size_t payload = 0; payload < branch.payload_ordinals.size();
               ++payload) {
            if (!payload_ordinals.insert(branch.payload_ordinals[payload]).second)
              fail("checked body match payload ordinal is duplicated");
            declare_loop_local(branch.payload_ordinals[payload],
                               branch.payload_types[payload], false);
          }
          read_loop_block(body_count, depth + 1, branch.body);
          for (const auto ordinal : scopes_.back()) locals_.erase(ordinal);
          scopes_.pop_back();
          statement.match_cases.push_back(std::move(branch));
        }
        break;
      }
      default:
        fail("checked body statement kind is unsupported");
    }
    return statement;
  }

  Stmt read_single_return() {
    if (field() != "return")
      fail("single-statement checked body is not a return");
    Stmt statement;
    statement.kind = StmtKind::Return;
    statement.expr = read_expr();
    if ((!result_ && !allow_empty_vector_return_) ||
        (result_ && !statement.expr->type->structurally_equal(*result_)))
      fail("single-return checked body result type is mismatched");
    statement.type = result_ ? result_ : statement.expr->type;
    return statement;
  }

  Stmt read_let() {
    const auto kind = number();
    if (kind != static_cast<std::uint32_t>(StmtKind::Let) &&
        kind != static_cast<std::uint32_t>(StmtKind::Var))
      fail("H3 checked body has no leading local declaration");
    Stmt statement;
    statement.kind = static_cast<StmtKind>(kind);
    statement.local_ordinal = number();
    if (statement.local_ordinal != 0)
      fail("H3 checked body local ordinal is not zero");
    statement.type = read_type();
    statement.expr = read_expr();
    if (!statement.type->structurally_equal(*statement.expr->type))
      fail("H3 checked body let type is mismatched");
    local_type_ = statement.type;
    return statement;
  }

  Stmt read_return() {
    if (number() != static_cast<std::uint32_t>(StmtKind::Return) ||
        number() != 1)
      fail("H3 checked body has no terminal return");
    Stmt statement;
    statement.kind = StmtKind::Return;
    statement.expr = read_expr();
    if (statement.expr->kind != ExprKind::LocalRead || !result_ ||
        !statement.expr->type->structurally_equal(*result_))
      fail("H3 checked body return type is mismatched");
    statement.type = result_;
    return statement;
  }
};

inline std::vector<Stmt> decode_h3_checked_body_v1(
    std::string_view bytes, const TypeContext& context,
    const std::vector<TypeRef>& parameters, const TypeRef& result,
    std::string_view expected_hash) {
  H3BodyReader reader(bytes, context, parameters, result);
  auto statements = reader.read();
  if (compute_checked_body_hash_v1(statements) != expected_hash)
    throw std::invalid_argument("H3 checked body hash is mismatched");
  return statements;
}

// Bounded codec entry point for the statement/expression subset needed by
// collection-loop checked bodies. The ordinary H3 decoder intentionally keeps
// its narrow historical grammar; these nodes are not enabled there.
inline std::vector<Stmt> decode_collection_loop_checked_body_v1(
    std::string_view bytes, const TypeContext& context,
    const std::vector<TypeRef>& parameters, const TypeRef& result,
    std::string_view expected_hash) {
  H3BodyReader reader(bytes, context, parameters, result, false, true);
  auto statements = reader.read();
  if (compute_checked_body_hash_v1(statements) != expected_hash)
    throw std::invalid_argument("collection checked body hash is mismatched");
  return statements;
}

// Narrow ABI-v10 consumer entry point for the R2b canonical empty-vector body.
// This deliberately accepts exactly one `return VectorLiteral([])` whose
// authoritative type is the typed summary result. It does not enable vector
// literals in the broader H3 body decoder.
inline std::vector<Stmt> decode_empty_vector_return_checked_body_v1(
    std::string_view bytes, const TypeContext& context, const TypeRef& result,
    std::string_view expected_hash) {
  if (!result || result->kind != TypeKind::Builtin ||
      result->name != "vector" || result->type_args.size() != 1)
    throw std::invalid_argument("checked empty-vector summary result is unsupported");
  H3BodyReader reader(bytes, context, {}, result, true);
  auto statements = reader.read();
  if (statements.size() != 1 ||
      statements.front().kind != StmtKind::Return ||
      !statements.front().expr ||
      statements.front().expr->kind != ExprKind::VectorLiteral ||
      !statements.front().expr->operands.empty() ||
      compute_checked_body_hash_v1(statements) != expected_hash)
    throw std::invalid_argument("checked body is not the canonical empty-vector return");
  return statements;
}

// Unified ABI-v10 checked-body-v1 entry point. All admitted expression and
// statement forms share one bounded reader and canonical hash check; callers
// do not select a decoder from summary or body shape. The historical narrow
// entry points above remain available to compatibility tests and readers that
// intentionally enforce their old subset.
inline std::vector<Stmt> decode_checked_body_v1(
    std::string_view bytes, const TypeContext& context,
    const std::vector<TypeRef>& parameters, const TypeRef& result,
    std::string_view expected_hash,
    bool allow_vector_literal_relation = false,
    bool allow_generic_map_index_mutation = false,
    bool allow_typed_enum_match = false) {
  H3BodyReader reader(bytes, context, parameters, result, true, true,
                      allow_vector_literal_relation, true,
                      allow_generic_map_index_mutation,
                      allow_typed_enum_match);
  auto statements = reader.read();
  if (compute_checked_body_hash_v1(statements) != expected_hash)
    throw std::invalid_argument("checked body semantic hash is mismatched");
  return statements;
}

inline bool is_empty_vector_return_checked_body_candidate_v1(
    std::string_view bytes, const TypeContext& context) {
  try {
    H3BodyReader reader(bytes, context, {}, nullptr, true);
    const auto statements = reader.read();
    return statements.size() == 1 &&
        statements.front().kind == StmtKind::Return &&
        statements.front().expr &&
        statements.front().expr->kind == ExprKind::VectorLiteral &&
        statements.front().expr->operands.empty();
  } catch (const std::invalid_argument&) {
    return false;
  }
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
    case CheckedBodyExprV1::Kind::Field:
      if (expr.operands.size() != 1 || expr.semantic_target_id.empty() ||
          !expr.integer_value.empty())
        throw std::invalid_argument("invalid checked field node");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.member_ordinal));
      encode_checked_body_expr_v1(out, expr.operands.front());
      break;
    case CheckedBodyExprV1::Kind::VectorLiteral:
      if (!expr.semantic_target_id.empty() || !expr.integer_value.empty())
        throw std::invalid_argument("invalid checked vector node");
      checked_body_field(out, std::to_string(expr.operands.size()));
      for (const auto& child : expr.operands)
        encode_checked_body_expr_v1(out, child);
      break;
    case CheckedBodyExprV1::Kind::Call:
      if (expr.semantic_target_id.empty() || !expr.integer_value.empty())
        throw std::invalid_argument("invalid checked call node");
      checked_body_field(out, expr.semantic_target_id);
      checked_body_field(out, std::to_string(expr.operands.size()));
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
    case CheckedBodyExprV1::Kind::Field:
      expr.kind = checked_ir::ExprKind::Field;
      expr.semantic_target_id = legacy.semantic_target_id;
      expr.member_ordinal = legacy.member_ordinal;
      if (!legacy.operands.empty())
        expr.operands.push_back(from_legacy_expr(legacy.operands.front()));
      break;
    case CheckedBodyExprV1::Kind::VectorLiteral:
      expr.kind = checked_ir::ExprKind::VectorLiteral;
      for (const auto& op : legacy.operands)
        expr.operands.push_back(from_legacy_expr(op));
      break;
    case CheckedBodyExprV1::Kind::Call:
      expr.kind = checked_ir::ExprKind::Call;
      expr.semantic_target_id = legacy.semantic_target_id;
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
    // Builtin operations carry their own stable operation identity and are
    // validated by the checked-body reader. They are not callable closure
    // edges; only checker-selected callable IDs enter dependency records.
    if (expr.semantic_target_id.rfind("builtin:", 0) != 0 &&
        expr.semantic_target_id.rfind("builtin-scalar-protocol:", 0) != 0)
      out.insert({SemanticDependency::Kind::Callable,
                  expr.semantic_target_id, ""});
  } else if (expr.kind == checked_ir::ExprKind::Message) {
    if (expr.semantic_target_id.empty())
      throw std::invalid_argument("message expression has empty semantic target id");
    out.insert({SemanticDependency::Kind::Handler, expr.semantic_target_id, ""});
  } else if (expr.kind == checked_ir::ExprKind::Field) {
    if (!expr.semantic_target_id.empty()) {
      out.insert({SemanticDependency::Kind::NominalType, expr.semantic_target_id, ""});
    }
  } else if (expr.kind == checked_ir::ExprKind::NominalConstruct) {
    if (expr.semantic_target_id.empty())
      throw std::invalid_argument("nominal constructor has empty template id");
    out.insert({SemanticDependency::Kind::NominalType, expr.semantic_target_id, ""});
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
