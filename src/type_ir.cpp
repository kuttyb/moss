#include "type_ir.hpp"

namespace moss {

std::optional<TypeRef> Type::parse_canonical(std::string_view raw) {
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
          else if (head == "Set") head = "set";
          // A parameterized collection names every type argument; only the
          // bare spelling denotes an open, not-yet-inferred collection.
          if (auto arity = Type::collection_arity(head); arity && args.size() != *arity)
            return std::nullopt;
          return Type::builtin(head, std::move(args));
        }
        if (head == "unit") return Type::unit();
        if (head == "Int" || head == "int") return Type::primitive("int");
        if (head == "Float" || head == "float") return Type::primitive("float");
        if (head == "Bool" || head == "bool") return Type::primitive("bool");
        if (head == "String" || head == "string") return Type::primitive("string");
        if (head == "Char" || head == "char") return Type::primitive("char");
        if (head == "Range") return Type::primitive("Range");
        if (head == "RangeBatch") return Type::primitive("RangeBatch");
        if (head == "FileIO") return Type::primitive("FileIO");
        if (head == "executor") return Type::primitive("executor");
        if (head == "Vector") head = "vector";
        else if (head == "Map") head = "map";
        else if (head == "Queue") head = "queue";
        else if (head == "Set") head = "set";
        if (head == "vector" || head == "map" || head == "queue" ||
            head == "set")
          return Type::builtin(head, {});
        if (const TypeContext* context = TypeContext::current()) {
          if (auto declared = context->find(head)) {
            if (declared->kind == DeclarationKind::Record)
              return Type::nominal_bound(declared, head, {});
            if (declared->kind == DeclarationKind::Enum) return Type::enumeration(head);
            return Type::domain(head);
          }
          if (context->has_trait(head)) {
            return Type::trait_constraint(head);
          }
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
        if (!head.empty() && head[0] == '_') {
          return Type::primitive(head);
        }
        return std::nullopt;
      }
    } parser{raw};
    auto parsed = parser.parse();
    parser.space();
    return parsed && parser.pos == raw.size() ? parsed : std::nullopt;
  }

bool Type::is_well_formed_syntax(std::string_view raw) {
    struct SyntaxChecker {
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
      bool check(unsigned depth = 0) {
        if (depth >= 128) return false;
        std::string head = token();
        if (head.empty()) return false;
        space();
        if (pos < text.size() && text[pos] == '[') {
          ++pos;
          size_t count = 0;
          do {
            space();
            if (pos == text.size() || text[pos] == ']') return false;
            size_t start = pos;
            std::string candidate = token();
            space();
            if (pos < text.size() && text[pos] == '=') {
              ++pos;
              if (!check(depth + 1)) return false;
            } else {
              pos = start;
              if (!check(depth + 1)) return false;
            }
            ++count;
            space();
            if (pos < text.size() && text[pos] == ']') { ++pos; break; }
            if (pos == text.size() || text[pos] != ',') return false;
            ++pos;
          } while (true);
          std::string norm = head;
          if (norm == "Vector") norm = "vector";
          else if (norm == "Map") norm = "map";
          else if (norm == "Queue") norm = "queue";
          else if (norm == "Set") norm = "set";
          if (auto arity = Type::collection_arity(norm); arity && count != *arity)
            return false;
          return true;
        }
        return true;
      }
    } checker{raw};
    bool ok = checker.check();
    checker.space();
    return ok && checker.pos == raw.size();
  }

}  // namespace moss
