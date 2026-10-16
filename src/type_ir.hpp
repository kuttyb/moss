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
class TypeContext;

// ---------------------------------------------------------------------------
// Stable declaration identities (TYPE_IR_CONTRACT §3.1).
//
// A nominal type is identified by its defining module and declaration, never
// by its program spelling (`Provider__Box`), source path, layout, or a Rust
// symbol. One NominalDeclaration is interned per declaration per compilation
// session; nominal TypeRefs point at it.

inline std::string module_identity(std::string_view package,
                                   std::string_view version,
                                   std::string_view module) {
  return "module-v1:" + std::string(package) + "@" + std::string(version) +
         "::" + std::string(module);
}

enum class DeclarationKind { Record, Enum, Domain };

struct NominalFieldSlot {
  std::string name;
  std::string declared_type;  // canonical annotation; empty for an inferred slot
  bool inferred{false};
};

struct NominalDeclaration {
  DeclarationKind kind{DeclarationKind::Record};
  std::string program_name;      // checker spelling used only for name lookup
  std::string module_id;         // module-v1:<package>@<version>::<module>
  std::shared_ptr<const std::string> interned_module_id;
  std::string declaration_name;  // source declaration name
  std::string id;                // nominal-template-v1:<module_id>:type:<name>
  std::vector<NominalFieldSlot> fields;  // records only, in source order
  // The session that owns this declaration; resolves declared field types.
  std::weak_ptr<const TypeContext> owner;

  size_t inferred_slot_count() const {
    return static_cast<size_t>(std::count_if(fields.begin(), fields.end(),
        [](const NominalFieldSlot& slot) { return slot.inferred; }));
  }
};
using NominalDeclarationRef = std::shared_ptr<const NominalDeclaration>;

inline std::string declaration_identity(DeclarationKind kind,
                                        const std::string& module_id,
                                        const std::string& name) {
  switch (kind) {
    case DeclarationKind::Record:
      return "nominal-template-v1:" + module_id + ":type:" + name;
    case DeclarationKind::Enum:
      return "nominal-enum-v1:" + module_id + ":enum:" + name;
    case DeclarationKind::Domain:
      return "nominal-domain-v1:" + module_id + ":domain:" + name;
  }
  return {};
}

// The declaration table of one compilation session. Name resolution binds a
// program spelling to its interned declaration exactly once; after that every
// comparison and key uses the declaration identity. The active context is
// scoped (RAII) to the checker/generator/build step that owns the program.
class TypeContext : public std::enable_shared_from_this<TypeContext> {
 public:
  // A defining module has one value in this compilation session, including
  // when its declarations arrive from separate source-free consumers.
  std::shared_ptr<const std::string> intern_module(const std::string& id) {
    constexpr size_t prefix_size = sizeof("module-v1:") - 1;
    const size_t version = id.find('@', prefix_size);
    const size_t module = id.find("::", version == std::string::npos
        ? prefix_size : version + 1);
    if (id.rfind("module-v1:", 0) != 0 ||
        version == std::string::npos || version == prefix_size ||
        module == std::string::npos || module == version + 1 ||
        module + 2 == id.size())
      throw std::invalid_argument("invalid module identity");
    auto found = modules_.find(id);
    if (found != modules_.end()) return found->second;
    auto value = std::make_shared<const std::string>(id);
    modules_.emplace(id, value);
    return value;
  }
  std::shared_ptr<const std::string> find_module(std::string_view id) const {
    auto found = modules_.find(std::string(id));
    return found == modules_.end() ? nullptr : found->second;
  }
  // Interns a declaration. Re-declaring one program name with a different
  // identity is a compiler error, never a silent replacement.
  NominalDeclarationRef declare(NominalDeclaration declaration) {
    declaration.interned_module_id = intern_module(declaration.module_id);
    declaration.id = declaration_identity(declaration.kind, declaration.module_id,
                                          declaration.declaration_name);
    declaration.owner = weak_from_this();
    auto existing = by_name_.find(declaration.program_name);
    if (existing != by_name_.end()) {
      if (existing->second->id != declaration.id)
        throw std::invalid_argument("conflicting nominal identity for '" +
                                    declaration.program_name + "'");
      return existing->second;
    }
    auto interned = std::make_shared<const NominalDeclaration>(std::move(declaration));
    by_name_.emplace(interned->program_name, interned);
    by_id_.emplace(interned->id, interned);
    return interned;
  }
  NominalDeclarationRef find(std::string_view program_name) const {
    auto found = by_name_.find(std::string(program_name));
    return found == by_name_.end() ? nullptr : found->second;
  }
  NominalDeclarationRef find_id(std::string_view id) const {
    auto found = by_id_.find(std::string(id));
    return found == by_id_.end() ? nullptr : found->second;
  }
  static const TypeContext* current() {
    return stack().empty() ? nullptr : stack().back();
  }
  class Scope {
   public:
    explicit Scope(const TypeContext* context)
        : active_(context != nullptr), context_(context) {
      if (active_) stack().push_back(context);
    }
    ~Scope() { if (active_) release(context_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
   private:
    bool active_;
    const TypeContext* context_;
  };
  // A copyable member form of Scope for objects (such as the generator) that
  // are themselves copied; each live copy keeps the same context bound.
  class Binding {
   public:
    explicit Binding(const TypeContext* context) : context_(context) { push(); }
    Binding(const Binding& other) : context_(other.context_) { push(); }
    Binding& operator=(const Binding&) = delete;
    ~Binding() { if (context_) release(context_); }
   private:
    void push() { if (context_) stack().push_back(context_); }
    const TypeContext* context_;
  };

 private:
  // Removes the most recent binding of `context`, so a scope that ends out of
  // order cannot unbind another session's context.
  static void release(const TypeContext* context) {
    auto& contexts = stack();
    for (auto entry = contexts.rbegin(); entry != contexts.rend(); ++entry)
      if (*entry == context) {
        contexts.erase(std::next(entry).base());
        return;
      }
  }
  static std::vector<const TypeContext*>& stack() {
    thread_local std::vector<const TypeContext*> contexts;
    return contexts;
  }
  std::unordered_map<std::string, NominalDeclarationRef> by_name_;
  std::unordered_map<std::string, NominalDeclarationRef> by_id_;
  std::unordered_map<std::string, std::shared_ptr<const std::string>> modules_;
};

inline NominalDeclarationRef bind_declaration(std::string_view program_name,
                                              DeclarationKind kind) {
  const TypeContext* context = TypeContext::current();
  if (!context) return nullptr;
  auto found = context->find(program_name);
  return found && found->kind == kind ? found : nullptr;
}

class Type {
public:
  TypeKind kind{TypeKind::Unit};
  std::string name; // primitive, enum, domain, trait, builtin, or nominal template
  std::vector<TypeRef> type_args; // for builtin containers (vector, map, queue, etc.)
  std::vector<std::pair<std::string, TypeRef>> field_substitutions; // inferred slots, template order
  TypeVarId var_id; // for InferenceVar
  // NominalInstance/Enum/Domain: the interned declaration. An unbound nominal
  // (no resolved declaration) is never closed and has no stable key.
  NominalDeclarationRef declaration;

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

  // Binds the template through the active session context. Substitutions are
  // stored in template slot order, so constructor argument order or spelling
  // cannot change instance identity.
  static TypeRef nominal(std::string template_name, std::vector<std::pair<std::string, TypeRef>> subs) {
    auto declaration = bind_declaration(template_name, DeclarationKind::Record);
    return nominal_bound(std::move(declaration), std::move(template_name), std::move(subs));
  }

  static TypeRef nominal_bound(NominalDeclarationRef declaration, std::string template_name,
                               std::vector<std::pair<std::string, TypeRef>> subs) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::NominalInstance;
    t->name = std::move(template_name);
    t->declaration = std::move(declaration);
    if (t->declaration) {
      std::vector<std::pair<std::string, TypeRef>> ordered;
      for (const auto& slot : t->declaration->fields) {
        if (!slot.inferred) continue;
        for (const auto& sub : subs)
          if (sub.first == slot.name) { ordered.push_back(sub); break; }
      }
      // Keep unknown or duplicate names visible; is_closed rejects them.
      if (ordered.size() == subs.size()) subs = std::move(ordered);
    }
    t->field_substitutions = std::move(subs);
    return t;
  }

  static TypeRef enumeration(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Enum;
    t->declaration = bind_declaration(name, DeclarationKind::Enum);
    t->name = std::move(name);
    return t;
  }

  static TypeRef domain(std::string name) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Domain;
    t->declaration = bind_declaration(name, DeclarationKind::Domain);
    t->name = std::move(name);
    return t;
  }

  bool declaration_kind() const {
    return kind == TypeKind::NominalInstance || kind == TypeKind::Enum ||
           kind == TypeKind::Domain;
  }

  // Same type constructor: declaration identity for nominal kinds, the
  // built-in/primitive name otherwise.
  bool same_head(const Type& other) const {
    if (kind != other.kind) return false;
    if (declaration_kind()) {
      if (declaration && other.declaration)
        return declaration->id == other.declaration->id;
      return !declaration && !other.declaration && name == other.name;
    }
    return kind == TypeKind::Unit || name == other.name;
  }

  // A bare template spelling (`Box`) names the declaration without choosing
  // its inferred slots: the known-open receiver of TYPE_IR_CONTRACT §4.4.
  bool is_bare_template() const {
    return kind == TypeKind::NominalInstance && declaration &&
           declaration->inferred_slot_count() != 0 && field_substitutions.empty();
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
    if (declaration_kind() && !declaration) return false;
    if (kind == TypeKind::NominalInstance) {
      // Every inferred slot is substituted exactly once, in template order.
      size_t index = 0;
      for (const auto& slot : declaration->fields) {
        if (!slot.inferred) continue;
        if (index >= field_substitutions.size() ||
            field_substitutions[index].first != slot.name) return false;
        ++index;
      }
      if (index != field_substitutions.size()) return false;
    }
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
    if (!same_head(other)) return false;
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
      append_part(key, declaration ? declaration->id : name);
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

  // type-key-v1 (TYPE_IR_CONTRACT §3.2): a length-prefixed binary encoding
  // hashed with SHA-256. A nominal instance hashes its template identity and
  // ordered *resolved* field types; re-entering an instance already being
  // encoded (a recursive layout through a collection) emits a back-reference.
  void append_type_key(std::string& out, std::vector<std::string>& active) const;
  std::vector<TypeRef> resolved_fields() const;

  std::string stable_type_key() const {
    if (!is_closed()) throw std::invalid_argument("open type has no stable type key");
    std::vector<std::string> active;
    std::string material;
    append_type_key(material, active);
    return "type-key-v1:sha256:" + source_free_contract_sha256(material);
  }

  // NominalInstanceKey of a closed record instance.
  std::string nominal_instance_key() const {
    if (kind != TypeKind::NominalInstance || !is_closed())
      throw std::invalid_argument("only a closed nominal instance has an instance key");
    std::vector<std::string> active;
    std::string material;
    append_type_key(material, active);
    return "nominal-instance-v1:sha256:" + source_free_contract_sha256(material);
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
        if (const TypeContext* context = TypeContext::current())
          if (auto declared = context->find(head)) {
            if (declared->kind == DeclarationKind::Record)
              return Type::nominal_bound(declared, head, {});
            if (declared->kind == DeclarationKind::Enum) return Type::enumeration(head);
            return Type::domain(head);
          }
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

// Ordered field types of a record instance: declared fields from the
// template (resolved in the declaration's own session), inferred slots from
// this instance's substitution.
inline std::vector<TypeRef> Type::resolved_fields() const {
  if (kind != TypeKind::NominalInstance || !is_closed())
    throw std::invalid_argument("only a closed nominal instance has resolved fields");
  auto owner = declaration->owner.lock();
  if (!owner)
    throw std::invalid_argument("declaration " + declaration->id +
                                " outlived its compilation session");
  TypeContext::Scope scope(owner.get());
  std::vector<TypeRef> fields;
  size_t inferred = 0;
  for (const auto& slot : declaration->fields) {
    if (slot.inferred) {
      fields.push_back(field_substitutions[inferred++].second);
      continue;
    }
    auto declared = parse_canonical(slot.declared_type);
    if (!declared)
      throw std::invalid_argument("declared field type of " + declaration->id +
                                  " is malformed: " + slot.declared_type);
    fields.push_back(*declared);
  }
  return fields;
}

inline void Type::append_type_key(std::string& out, std::vector<std::string>& active) const {
  switch (kind) {
    case TypeKind::Primitive:
      append_part(out, "primitive");
      append_part(out, name);
      return;
    case TypeKind::Unit:
      append_part(out, "unit");
      return;
    case TypeKind::Enum:
    case TypeKind::Domain:
      append_part(out, kind == TypeKind::Enum ? "enum" : "domain");
      append_part(out, declaration->id);
      return;
    case TypeKind::TraitConstraint:
      append_part(out, "trait");
      append_part(out, name);
      return;
    case TypeKind::InferenceVar:
      throw std::invalid_argument("open type has no stable type key");
    case TypeKind::Builtin:
      append_part(out, "builtin-instance-v1");
      append_part(out, name);
      append_part(out, std::to_string(type_args.size()));
      for (const auto& argument : type_args) {
        std::string nested;
        argument->append_type_key(nested, active);
        append_part(out, nested);
      }
      return;
    case TypeKind::NominalInstance: {
      std::string instance = declaration->id;
      for (const auto& sub : field_substitutions)
        append_part(instance, sub.second->structural_key());
      for (size_t depth = 0; depth < active.size(); ++depth)
        if (active[active.size() - 1 - depth] == instance) {
          append_part(out, "backref");
          append_part(out, std::to_string(depth));
          return;
        }
      active.push_back(instance);
      append_part(out, "nominal-instance-v1");
      append_part(out, declaration->id);
      const auto fields = resolved_fields();
      append_part(out, std::to_string(fields.size()));
      for (const auto& field : fields) {
        std::string nested;
        field->append_type_key(nested, active);
        append_part(out, nested);
      }
      active.pop_back();
      return;
    }
  }
}

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
  std::string call_expression_id;
};

struct Requirement {
  std::string id;
  RequirementKind kind{RequirementKind::SameType};
  TypeRef subject;
  std::vector<TypeRef> arguments;
  std::optional<TypeRef> result;
  AccessMode access{AccessMode::Read};
  std::vector<AccessMode> argument_access;
  std::optional<std::string> parent_requirement_id;
  RequirementOrigin origin;
};

// The solver-owned record passed to Team S's member admission policy. It
// retains the type terms and access contract through substitution; display
// text and backend symbols are never used to select a member target.
struct MemberDispatchRequirement {
  std::string requirement_id;
  TypeRef receiver;
  std::string member_name;
  std::vector<TypeRef> argument_types;
  std::optional<TypeRef> result;
  AccessMode receiver_access{AccessMode::Read};
  std::vector<AccessMode> argument_access;
  std::optional<std::string> admitted_template_or_builtin_family;
  std::optional<std::string> selected_callable_id;
  RequirementOrigin origin;
  std::optional<std::string> parent_requirement_id;
};

// All consumers use the same length-prefixed specialization-v2 encoding.
// The caller supplies resolved callable/receiver/callable-argument identities;
// this record cannot turn an open TypeRef into a concrete key.
struct SpecializationKey {
  std::string callable_id;
  std::optional<TypeRef> receiver;
  std::vector<TypeRef> arguments;
  std::vector<std::string> callable_arguments;
  std::vector<std::pair<uint32_t, TypeRef>> open_result_bindings;

  std::optional<std::string> stable_key() const {
    if (callable_id.empty()) return std::nullopt;
    std::string material;
    Type::append_part(material, "specialization-v2");
    Type::append_part(material, callable_id);
    Type::append_part(material, receiver ? "receiver" : "no-receiver");
    if (receiver) {
      if (!*receiver || !(*receiver)->is_closed()) return std::nullopt;
      Type::append_part(material, (*receiver)->stable_type_key());
    }
    Type::append_part(material, std::to_string(arguments.size()));
    for (const auto& argument : arguments) {
      if (!argument || !argument->is_closed()) return std::nullopt;
      Type::append_part(material, argument->stable_type_key());
    }
    Type::append_part(material, std::to_string(callable_arguments.size()));
    for (const auto& callable : callable_arguments)
      Type::append_part(material, callable);
    Type::append_part(material, std::to_string(open_result_bindings.size()));
    for (const auto& binding : open_result_bindings) {
      if (!binding.second || !binding.second->is_closed()) return std::nullopt;
      Type::append_part(material, std::to_string(binding.first));
      Type::append_part(material, binding.second->stable_type_key());
    }
    return "specialization-v2:sha256:" + source_free_contract_sha256(material);
  }
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
      return Type::nominal_bound(value->declaration, value->name, std::move(fields));
    }
    return value;
  }
  bool unify(const TypeRef& left, const TypeRef& right) {
    TypeRef a = resolve(left), b = resolve(right);
    if (a->kind == TypeKind::InferenceVar) return bind(a->var_id, b);
    if (b->kind == TypeKind::InferenceVar) return bind(b->var_id, a);
    if (!a->same_head(*b) ||
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

inline MemberDispatchRequirement resolve_member_requirement(
    TypeSolver& solver, const MemberDispatchRequirement& requirement) {
  MemberDispatchRequirement result = requirement;
  if (result.receiver) result.receiver = solver.resolve(result.receiver);
  for (auto& argument : result.argument_types)
    if (argument) argument = solver.resolve(argument);
  if (result.result && *result.result)
    result.result = solver.resolve(*result.result);
  return result;
}

inline bool member_requirement_is_closed(
    const MemberDispatchRequirement& requirement) {
  if (!requirement.receiver || !requirement.receiver->is_closed() ||
      requirement.argument_access.size() != requirement.argument_types.size() ||
      !requirement.admitted_template_or_builtin_family ||
      !requirement.selected_callable_id) return false;
  for (const auto& argument : requirement.argument_types)
    if (!argument || !argument->is_closed()) return false;
  return !requirement.result ||
      (*requirement.result && (*requirement.result)->is_closed());
}

} // namespace moss
