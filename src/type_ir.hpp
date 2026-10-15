#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

  bool is_closed() const {
    if (kind == TypeKind::InferenceVar) return false;
    for (const auto& arg : type_args) {
      if (!arg->is_closed()) return false;
    }
    for (const auto& sub : field_substitutions) {
      if (!sub.second->is_closed()) return false;
    }
    return true;
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

  static std::vector<std::string> split_top_level_types(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      char c = s[i];
      if (c == '[') ++depth;
      else if (c == ']') --depth;
      else if (c == ',' && depth == 0) {
        out.push_back(s.substr(start, i - start));
        start = i + 1;
      }
    }
    out.push_back(s.substr(start));
    return out;
  }

  static TypeRef from_canonical_string(const std::string& raw) {
    std::string s;
    for (char c : raw) {
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') s.push_back(c);
    }
    if (s.empty()) return unit();
    if (s == "unit") return unit();
    if (s == "int" || s == "Int") return primitive("int");
    if (s == "float" || s == "Float") return primitive("float");
    if (s == "bool" || s == "Bool") return primitive("bool");
    if (s == "string" || s == "String") return primitive("string");

    auto bracket = s.find('[');
    if (bracket != std::string::npos && s.back() == ']') {
      std::string head = s.substr(0, bracket);
      std::string inside = s.substr(bracket + 1, s.size() - bracket - 2);
      auto parts = split_top_level_types(inside);
      // Check if parts are of the form key=val (NominalInstance)
      bool is_nominal = false;
      for (const auto& part : parts) {
        if (part.find('=') != std::string::npos) {
          is_nominal = true;
          break;
        }
      }
      if (is_nominal) {
        std::vector<std::pair<std::string, TypeRef>> subs;
        for (const auto& part : parts) {
          auto eq = part.find('=');
          if (eq != std::string::npos) {
            std::string k = part.substr(0, eq);
            std::string v = part.substr(eq + 1);
            subs.push_back({k, from_canonical_string(v)});
          }
        }
        return nominal(head, std::move(subs));
      } else {
        std::vector<TypeRef> args;
        for (const auto& part : parts) {
          args.push_back(from_canonical_string(part));
        }
        return builtin(head, std::move(args));
      }
    }

    if (s.rfind("_typevar:", 0) == 0) {
      // parse _typevar:session:owner:ordinal
      TypeVarId id;
      size_t c1 = s.find(':', 9);
      if (c1 != std::string::npos) {
        size_t c2 = s.find(':', c1 + 1);
        if (c2 != std::string::npos) {
          id.session_id = std::stoul(s.substr(9, c1 - 9));
          id.owner_id = std::stoul(s.substr(c1 + 1, c2 - c1 - 1));
          id.ordinal = std::stoul(s.substr(c2 + 1));
          return var(id);
        }
      }
    }

    // Default to primitive/nominal by name
    return primitive(s);
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

} // namespace moss
