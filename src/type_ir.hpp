#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "source_free_abi_v9.hpp"

namespace moss {

enum class TypeKind {
  Primitive,
  Builtin,
  NominalInstance,
  Enum,
  Domain,
  TraitConstraint,
  InferenceVar,
  Unit
};

struct TypeVarId {
  uint32_t session_id{0};
  uint32_t owner_id{0};
  uint32_t ordinal{0};

  bool operator==(const TypeVarId& o) const {
    return session_id == o.session_id && owner_id == o.owner_id && ordinal == o.ordinal;
  }
  bool operator!=(const TypeVarId& o) const { return !(*this == o); }
  bool operator<(const TypeVarId& o) const {
    if (session_id != o.session_id) return session_id < o.session_id;
    if (owner_id != o.owner_id) return owner_id < o.owner_id;
    return ordinal < o.ordinal;
  }
};

class Type;
using TypeRef = std::shared_ptr<Type>;

class Type {
public:
  TypeKind kind{TypeKind::Unit};
  std::string name; // primitive, enum, domain, trait, builtin, or nominal template
  std::vector<TypeRef> type_args; // for builtin containers (vector, map, queue, etc.)
  std::vector<std::pair<std::string, TypeRef>> field_substitutions; // for NominalInstance
  TypeVarId var_id; // for InferenceVar

  static TypeRef primitive(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Primitive;
    t->name = std::move(name);
    return t;
  }

  static TypeRef builtin(std::string name, std::vector<TypeRef> args) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Builtin;
    t->name = std::move(name);
    t->type_args = std::move(args);
    return t;
  }

  static TypeRef nominal(std::string template_name, std::vector<std::pair<std::string, TypeRef>> subs) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::NominalInstance;
    t->name = std::move(template_name);
    t->field_substitutions = std::move(subs);
    return t;
  }

  static TypeRef enumeration(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Enum;
    t->name = std::move(name);
    return t;
  }

  static TypeRef domain(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Domain;
    t->name = std::move(name);
    return t;
  }

  static TypeRef trait_constraint(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::TraitConstraint;
    t->name = std::move(name);
    return t;
  }

  static TypeRef var(TypeVarId id) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::InferenceVar;
    t->var_id = id;
    return t;
  }

  static TypeRef unit() {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Unit;
    t->name = "unit";
    return t;
  }

  // Type-argument count of a built-in collection, or nullopt for other names.
  static std::optional<size_t> collection_arity(std::string_view builtin_name) {
    if (builtin_name == "vector" || builtin_name == "queue") return 1;
    if (builtin_name == "map") return 2;
    return std::nullopt;
  }

  bool is_closed() const {
    if (kind == TypeKind::InferenceVar) return false;
    // `vector`, `queue`, and `map` are closed only with every type argument;
    // the bare spelling is an open collection whose element/key/value types
    // are still unbound (Option B). Partial arity is never closed.
    if (kind == TypeKind::Builtin)
      if (auto arity = collection_arity(name); arity && type_args.size() != *arity)
        return false;
    for (const auto& arg : type_args) {
      if (!arg->is_closed()) return false;
    }
    for (const auto& sub : field_substitutions) {
      if (!sub.second->is_closed()) return false;
    }
    return true;
  }

  bool structurally_equal(const Type& other) const {
    if (kind != other.kind) return false;
    if (kind == TypeKind::InferenceVar) return var_id == other.var_id;
    if (kind != TypeKind::Unit && name != other.name) return false;
    if (type_args.size() != other.type_args.size() ||
        field_substitutions.size() != other.field_substitutions.size()) return false;
    for (size_t i = 0; i < type_args.size(); ++i)
      if (!type_args[i]->structurally_equal(*other.type_args[i])) return false;
    for (size_t i = 0; i < field_substitutions.size(); ++i)
      if (field_substitutions[i].first != other.field_substitutions[i].first ||
          !field_substitutions[i].second->structurally_equal(
              *other.field_substitutions[i].second)) return false;
    return true;
  }

  static void append_part(std::string& out, std::string_view part) {
    out += std::to_string(part.size());
    out += ':';
    out.append(part.data(), part.size());
  }

  std::string structural_key() const {
    std::string key;
    append_part(key, std::to_string(static_cast<unsigned>(kind)));
    if (kind == TypeKind::InferenceVar) {
      append_part(key, std::to_string(var_id.session_id));
      append_part(key, std::to_string(var_id.owner_id));
      append_part(key, std::to_string(var_id.ordinal));
    } else {
      append_part(key, name);
      append_part(key, std::to_string(type_args.size()));
      for (const auto& arg : type_args) append_part(key, arg->structural_key());
      append_part(key, std::to_string(field_substitutions.size()));
      for (const auto& field : field_substitutions) {
        append_part(key, field.first);
        append_part(key, field.second->structural_key());
      }
    }
    return key;
  }

  std::string stable_type_key() const {
    if (!is_closed()) throw std::invalid_argument("open type has no stable type key");
    return "type-key-v1:sha256:" + source_free_contract_sha256(structural_key());
  }

  std::string to_canonical_string() const {
    switch (kind) {
      case TypeKind::Primitive:
      case TypeKind::Enum:
      case TypeKind::Domain:
      case TypeKind::TraitConstraint:
      case TypeKind::Unit:
        return name;
      case TypeKind::InferenceVar:
        return "_typevar:" + std::to_string(var_id.session_id) + ":" +
               std::to_string(var_id.owner_id) + ":" + std::to_string(var_id.ordinal);
      case TypeKind::Builtin: {
        std::string s = name;
        if (!type_args.empty()) {
          s += "[";
          for (size_t i = 0; i < type_args.size(); ++i) {
            if (i > 0) s += ",";
            s += type_args[i]->to_canonical_string();
          }
          s += "]";
        }
        return s;
      }
      case TypeKind::NominalInstance: {
        std::string s = name;
        if (!field_substitutions.empty()) {
          s += "[";
          for (size_t i = 0; i < field_substitutions.size(); ++i) {
            if (i > 0) s += ",";
            s += field_substitutions[i].first + "=" + field_substitutions[i].second->to_canonical_string();
          }
          s += "]";
        }
        return s;
      }
    }
    return name;
  }

  // This parser is for legacy display/ABI ingress. Semantic operations use the
  // resulting tree; they never search display text for brackets or '='.
  static std::optional<TypeRef> parse_canonical(std::string_view raw) {
    struct Parser {
      std::string_view text;
      size_t pos{0};
      void space() {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
               text[pos] == '\n' || text[pos] == '\r')) ++pos;
      }
      std::string token() {
        space();
        size_t begin = pos;
        while (pos < text.size() && text[pos] != '[' && text[pos] != ']' &&
               text[pos] != ',' && text[pos] != '=' && text[pos] != ' ' &&
               text[pos] != '\t' && text[pos] != '\n' && text[pos] != '\r') ++pos;
        return std::string(text.substr(begin, pos - begin));
      }
      static std::optional<uint32_t> number(std::string_view value) {
        uint32_t result = 0;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (value.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size()) return std::nullopt;
        return result;
      }
      std::optional<TypeRef> parse(unsigned depth = 0) {
        if (depth >= 128) return std::nullopt;
        std::string head = token();
        if (head.empty()) return std::nullopt;
        space();
        if (pos < text.size() && text[pos] == '[') {
          ++pos;
          std::vector<TypeRef> args;
          std::vector<std::pair<std::string, TypeRef>> fields;
          bool nominal = false, positional = false;
          do {
            space();
            if (pos == text.size() || text[pos] == ']') return std::nullopt;
            size_t start = pos;
            std::string candidate = token();
            space();
            if (pos < text.size() && text[pos] == '=') {
              if (positional || candidate.empty()) return std::nullopt;
              nominal = true;
              ++pos;
              auto value = parse(depth + 1);
              if (!value) return std::nullopt;
              for (const auto& field : fields)
                if (field.first == candidate) return std::nullopt;
              fields.emplace_back(candidate, *value);
            } else {
              if (nominal) return std::nullopt;
              positional = true;
              pos = start;
              auto value = parse(depth + 1);
              if (!value) return std::nullopt;
              args.push_back(*value);
            }
            space();
            if (pos < text.size() && text[pos] == ']') { ++pos; break; }
            if (pos == text.size() || text[pos] != ',') return std::nullopt;
            ++pos;
          } while (true);
          if (nominal) return Type::nominal(head, std::move(fields));
          if (head == "Vector") head = "vector";
          else if (head == "Map") head = "map";
          else if (head == "Queue") head = "queue";
          // A parameterized collection names every type argument; only the
          // bare spelling denotes an open, not-yet-inferred collection.
          if (auto arity = collection_arity(head); arity && args.size() != *arity)
            return std::nullopt;
          return Type::builtin(head, std::move(args));
        }
        if (head == "unit") return Type::unit();
        if (head == "Int" || head == "int") return Type::primitive("int");
        if (head == "Float" || head == "float") return Type::primitive("float");
        if (head == "Bool" || head == "bool") return Type::primitive("bool");
        if (head == "String" || head == "string") return Type::primitive("string");
        if (head == "Vector") head = "vector";
        else if (head == "Map") head = "map";
        else if (head == "Queue") head = "queue";
        if (head == "vector" || head == "map" || head == "queue")
          return Type::builtin(head, {});
        constexpr std::string_view prefix = "_typevar:";
        if (head.rfind(prefix, 0) == 0) {
          std::string_view ids(head.data() + prefix.size(), head.size() - prefix.size());
          auto first = ids.find(':');
          auto second = first == std::string_view::npos ? first : ids.find(':', first + 1);
          if (first == std::string_view::npos || second == std::string_view::npos)
            return std::nullopt;
          auto session = number(ids.substr(0, first));
          auto owner = number(ids.substr(first + 1, second - first - 1));
          auto ordinal = number(ids.substr(second + 1));
          if (!session || !owner || !ordinal) return std::nullopt;
          return Type::var({*session, *owner, *ordinal});
        }
        return Type::primitive(head);
      }
    } parser{raw};
    auto parsed = parser.parse();
    parser.space();
    return parsed && parser.pos == raw.size() ? parsed : std::nullopt;
  }

  static TypeRef from_canonical_string(const std::string& raw) {
    auto parsed = parse_canonical(raw);
    if (!parsed) throw std::invalid_argument("malformed type expression: " + raw);
    return *parsed;
  }
};

struct FieldSlot {
  std::string name;
  std::optional<std::string> declared_type;
  std::optional<uint32_t> variable_ordinal;
  bool exported{false};
};

struct NominalTemplate {
  std::string template_id;
  std::string defining_module;
  std::string declaration_name;
  std::vector<FieldSlot> field_slots;
  bool exported{false};

  bool is_generic() const {
    for (const auto& slot : field_slots) {
      if (slot.variable_ordinal.has_value()) return true;
    }
    return false;
  }
};

struct ResolvedNominalInstance {
  std::string template_id;
  std::vector<std::pair<std::string, std::string>> substitution; // field_name -> type
  std::string instance_key;

  std::string to_canonical_type_string() const {
    std::string s = template_id;
    if (!substitution.empty()) {
      s += "[";
      for (size_t i = 0; i < substitution.size(); ++i) {
        if (i > 0) s += ",";
        s += substitution[i].first + "=" + substitution[i].second;
      }
      s += "]";
    }
    return s;
  }
};

enum class RequirementKind {
  SameType,
  Field,
  Method,
  Operator,
  Index,
  Iterate,
  Callable,
  MapKey
};

enum class AccessMode {
  Read,
  Write,
  Consume
};

struct RequirementOrigin {
  std::string module_id;
  std::string source_file;
  int line{0};
  int column{0};
  std::string enclosing_callable;
};

struct Requirement {
  RequirementKind kind{RequirementKind::SameType};
  TypeRef subject;
  std::vector<TypeRef> arguments;
  std::optional<TypeRef> result;
  AccessMode access{AccessMode::Read};
  RequirementOrigin origin;
};

// One solver belongs to one inference session. Variables are allocated, never
// inferred from their printed name; requirements follow the representative.
class TypeSolver {
  struct Variable {
    TypeVarId parent;
    std::optional<TypeRef> binding;
    std::vector<Requirement> requirements;
  };
  uint32_t session_;
  uint32_t next_ordinal_{0};
  std::map<TypeVarId, Variable> variables_;

  TypeVarId root(TypeVarId id) {
    auto found = variables_.find(id);
    if (found == variables_.end()) throw std::invalid_argument("unknown type variable");
    if (found->second.parent == id) return id;
    found->second.parent = root(found->second.parent);
    return found->second.parent;
  }
  bool occurs(TypeVarId id, const TypeRef& value) {
    TypeRef resolved = resolve(value);
    if (resolved->kind == TypeKind::InferenceVar) return root(resolved->var_id) == id;
    for (const auto& arg : resolved->type_args)
      if (occurs(id, arg)) return true;
    for (const auto& field : resolved->field_substitutions)
      if (occurs(id, field.second)) return true;
    return false;
  }
  bool bind(TypeVarId id, const TypeRef& value) {
    id = root(id);
    TypeRef resolved = resolve(value);
    if (resolved->kind == TypeKind::InferenceVar) {
      TypeVarId other = root(resolved->var_id);
      if (id == other) return true;
      // Deterministic representative independent of constraint order.
      if (other < id) std::swap(id, other);
      auto& primary = variables_.at(id);
      auto& secondary = variables_.at(other);
      secondary.parent = id;
      primary.requirements.insert(primary.requirements.end(),
          secondary.requirements.begin(), secondary.requirements.end());
      if (primary.binding && secondary.binding)
        return unify(*primary.binding, *secondary.binding);
      if (!primary.binding) primary.binding = secondary.binding;
      return true;
    }
    if (occurs(id, resolved)) return false;
    auto& variable = variables_.at(id);
    if (variable.binding) return unify(*variable.binding, resolved);
    variable.binding = resolved;
    return true;
  }

public:
  explicit TypeSolver(uint32_t session) : session_(session) {}
  TypeRef fresh(uint32_t owner_id) {
    TypeVarId id{session_, owner_id, next_ordinal_++};
    variables_.emplace(id, Variable{id, std::nullopt, {}});
    return Type::var(id);
  }
  TypeRef resolve(const TypeRef& value) {
    if (value->kind == TypeKind::InferenceVar) {
      TypeVarId id = root(value->var_id);
      auto& variable = variables_.at(id);
      return variable.binding ? resolve(*variable.binding) : Type::var(id);
    }
    if (value->type_args.empty() && value->field_substitutions.empty()) return value;
    if (value->kind == TypeKind::Builtin) {
      std::vector<TypeRef> args;
      for (const auto& arg : value->type_args) args.push_back(resolve(arg));
      return Type::builtin(value->name, std::move(args));
    }
    if (value->kind == TypeKind::NominalInstance) {
      std::vector<std::pair<std::string, TypeRef>> fields;
      for (const auto& field : value->field_substitutions)
        fields.emplace_back(field.first, resolve(field.second));
      return Type::nominal(value->name, std::move(fields));
    }
    return value;
  }
  bool unify(const TypeRef& left, const TypeRef& right) {
    TypeRef a = resolve(left), b = resolve(right);
    if (a->kind == TypeKind::InferenceVar) return bind(a->var_id, b);
    if (b->kind == TypeKind::InferenceVar) return bind(b->var_id, a);
    if (a->kind != b->kind || a->name != b->name ||
        a->type_args.size() != b->type_args.size() ||
        a->field_substitutions.size() != b->field_substitutions.size()) return false;
    for (size_t i = 0; i < a->type_args.size(); ++i)
      if (!unify(a->type_args[i], b->type_args[i])) return false;
    for (size_t i = 0; i < a->field_substitutions.size(); ++i)
      if (a->field_substitutions[i].first != b->field_substitutions[i].first ||
          !unify(a->field_substitutions[i].second, b->field_substitutions[i].second))
        return false;
    return true;
  }
  void require(TypeVarId id, Requirement requirement) {
    variables_.at(root(id)).requirements.push_back(std::move(requirement));
  }
  const std::vector<Requirement>& requirements(TypeVarId id) {
    return variables_.at(root(id)).requirements;
  }
};

} // namespace moss
