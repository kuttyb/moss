#include "expression_tree.hpp"
#include "../ast.hpp"
#include "../numeric_literal.hpp"
#include "../parser_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace moss::est {


std::uint64_t stable_origin_id(std::string_view identity) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : identity) { hash ^= byte; hash *= 1099511628211ULL; }
  return hash ? hash : 1;
}

ExprId Arena::append(Node node) {
  return append(std::move(node), {});
}

ExprId Arena::append(Node node, SourceSpan source_span) {
  if (nodes_.size() >= UINT32_MAX) throw std::overflow_error("EST arena full");
  const auto old_size = nodes_.size();
  const auto id = static_cast<ExprId>(nodes_.size());
  nodes_.push_back(std::move(node));
  spans_.push_back(source_span);
  assert(nodes_.size() == old_size + 1 && spans_.size() == nodes_.size() &&
         id == old_size);
  return id;
}

const Node& Arena::at(ExprId id) const { return nodes_.at(id); }

void Arena::note(std::uint64_t owner, ExprSlot slot,
                 std::uint32_t argument_index, Outcome outcome) {
  auto& count = coverage_.at(static_cast<std::size_t>(slot));
  ++count.attempts;
  const auto key = std::make_tuple(owner, static_cast<std::uint8_t>(slot),
                                   argument_index);
  if (!seen_.insert(key).second) return;
  ++count.unique;
  switch (outcome) {
    case Outcome::Built: ++count.built; break;
    case Outcome::Deferred27_1b: ++count.deferred_27_1b; break;
    case Outcome::RejectedSource: ++count.rejected_source; break;
    case Outcome::Failed: ++count.failed; break;
  }
}

ExprId Arena::duplicate(ExprId id) {
  const Node old = at(id);
  return std::visit([this, id](const auto& value) -> ExprId {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Unary>)
      return append(Unary{value.op, duplicate(value.operand)}, span(id));
    else if constexpr (std::is_same_v<T, Binary>)
      return append(Binary{value.op, duplicate(value.left), duplicate(value.right)}, span(id));
    else if constexpr (std::is_same_v<T, Call>) {
      const auto callee = duplicate(value.callee);
      std::vector<ExprId> arguments;
      for (const auto argument : value.arguments) arguments.push_back(duplicate(argument));
      return append(Call{callee, std::move(arguments)}, span(id));
    } else if constexpr (std::is_same_v<T, Member>)
      return append(Member{duplicate(value.base), value.name}, span(id));
    else if constexpr (std::is_same_v<T, Index>)
      return append(Index{duplicate(value.base), duplicate(value.index)}, span(id));
    else if constexpr (std::is_same_v<T, Parenthesized>)
      return append(Parenthesized{duplicate(value.inner)}, span(id));
    else if constexpr (std::is_same_v<T, Message>) {
      std::vector<ExprId> arguments;
      for (const auto argument : value.arguments)
        arguments.push_back(duplicate(argument));
      return append(Message{duplicate(value.target), value.handler,
                            std::move(arguments)}, span(id));
    }
    else if constexpr (std::is_same_v<T, NamedArgument>)
      return append(NamedArgument{value.name, duplicate(value.value)}, span(id));
    else if constexpr (std::is_same_v<T, Collection>) {
      std::vector<ExprId> elements;
      for (const auto element : value.elements) elements.push_back(duplicate(element));
      return append(Collection{value.delimiter, std::move(elements)}, span(id));
    } else if constexpr (std::is_same_v<T, Tuple>) {
      std::vector<ExprId> elements;
      for (const auto element : value.elements) elements.push_back(duplicate(element));
      return append(Tuple{std::move(elements)}, span(id));
    } else if constexpr (std::is_same_v<T, Constructor>) {
      std::vector<ExprId> types, arguments;
      for (const auto type : value.type_arguments) types.push_back(duplicate(type));
      for (const auto argument : value.arguments) arguments.push_back(duplicate(argument));
      return append(Constructor{duplicate(value.type), std::move(types),
                                std::move(arguments)}, span(id));
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      std::vector<ExprId> stages;
      for (const auto stage : value.stages) stages.push_back(duplicate(stage));
      return append(Pipeline{duplicate(value.input), std::move(stages)}, span(id));
    }
    else return append(value, span(id));
  }, old);
}

void Arena::rebuild(std::vector<Handle>& handles, std::uint64_t owner, ExprSlot slot,
                    std::uint32_t argument_index, Node replacement,
                    std::uint64_t source_fingerprint) {
  const auto old_size = nodes_.size();
  for (auto it = handles.begin(); it != handles.end(); ++it) {
    if (it->owner != owner || it->slot != slot ||
        it->argument_index != argument_index) continue;
    handles.erase(it);
    break;
  }
  const auto id = append(std::move(replacement));
  assert(nodes_.size() == old_size + 1 && id == old_size);
  handles.push_back({owner, slot, argument_index, id, source_fingerprint});
}

std::uint64_t fingerprint(const std::string& source) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : source) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void assert_fresh(const Handle& handle, const std::string& source) {
  assert(handle.source_fingerprint == fingerprint(source));
}

const Handle* lookup(const std::vector<Handle>& handles, const Arena& arena,
                     std::uint64_t owner, ExprSlot slot,
                     std::uint32_t argument_index, const std::string& source) {
  if (mode() == Mode::Off) return nullptr;
  for (const auto& handle : handles) {
    if (handle.owner != owner || handle.slot != slot ||
        handle.argument_index != argument_index) continue;
    assert_fresh(handle, source);
    (void)arena.at(handle.expr);
    return &handle;
  }
  return nullptr;
}

ExprId fresh_occurrence(Arena& arena, ExprId root) {
  return arena.duplicate(root);
}

void rebuild_slot(Arena& arena, std::vector<Handle>& handles,
                  std::uint64_t owner, ExprSlot slot,
                  std::uint32_t argument_index, const std::string& new_text) {
  std::uint64_t origin_id = 0;
  std::uint32_t arena_index = 0;
  std::uint32_t base_line = 1, base_column = 1;
  for (const auto& handle : handles)
    if (handle.owner == owner && handle.slot == slot &&
        handle.argument_index == argument_index) {
      origin_id = handle.origin_id;
      arena_index = handle.arena_index;
      const auto source_span = arena.span(handle.expr);
      base_line = source_span.line;
      base_column = source_span.column;
      break;
    }
  const auto root = parse_expression(arena, new_text, origin_id,
                                     base_line, base_column);
  const auto old_size = arena.size();
  handles.erase(std::remove_if(handles.begin(), handles.end(),
      [&](const Handle& handle) {
        return handle.owner == owner && handle.slot == slot &&
               handle.argument_index == argument_index;
      }), handles.end());
  assert(arena.size() == old_size);
  handles.push_back({owner, slot, argument_index, root, fingerprint(new_text),
                     origin_id, arena_index});
  assert(arena.size() >= old_size);
}

Mode mode() {
  const char* raw = std::getenv("MOSS_EST");
  if (!raw || std::string_view(raw) == "off") return Mode::Off;
  if (std::string_view(raw) == "shadow") return Mode::Shadow;
  throw std::invalid_argument("MOSS_EST must be off or shadow (on is reserved)");
}

namespace {
struct Token { std::string text; bool word = false; bool literal = false;
               std::uint32_t line = 1, column = 1; };

std::vector<Token> lex(std::string_view source) {
  std::vector<Token> result;
  std::uint32_t line = 1, column = 1;
  auto advance = [&](std::size_t begin, std::size_t end) {
    for (std::size_t p = begin; p < end; ++p) {
      if (source[p] == '\n') { ++line; column = 1; }
      else ++column;
    }
  };
  for (std::size_t i = 0; i < source.size();) {
    const unsigned char c = source[i];
    if (std::isspace(c)) { const auto begin = i++; advance(begin, i); continue; }
    const auto start = i;
    const auto start_line = line, start_column = column;
    if (std::isalpha(c) || c == '_') {
      while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
      result.push_back({std::string(source.substr(start, i-start)), true, false,
                        start_line, start_column});
    } else if (std::isdigit(c) || (c == '.' && i + 1 < source.size() &&
                                    std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
      const auto literal = scan_numeric_literal(source, i);
      if (!literal.ok || literal.end <= i)
        throw std::invalid_argument("EST invalid numeric literal");
      i = literal.end;
      result.push_back({literal.spelling, false, true, start_line, start_column});
    } else if (c == '"' || c == '\'') {
      const char quote = c;
      ++i;
      bool escaped = false;
      while (i < source.size()) {
        const char next = source[i++];
        if (escaped) { escaped = false; continue; }
        if (next == '\\') { escaped = true; continue; }
        if (next == quote) break;
      }
      result.push_back({std::string(source.substr(start, i-start)), false, true,
                        start_line, start_column});
    } else {
      ++i;
      if (i < source.size()) {
        const auto pair = source.substr(start, 2);
        if (pair == "|>" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" ||
            pair == "//" || pair == "<<" || pair == ">>" || pair == "&&" ||
            pair == "||" || pair == "**") ++i;
      }
      result.push_back({std::string(source.substr(start, i-start)), false, false,
                        start_line, start_column});
    }
    advance(start, i);
  }
  return result;
}

int precedence(const std::string& op) {
  if (op == "|>") return 0;
  if (op == "or") return 1;
  if (op == "xor") return 2;
  if (op == "and") return 3;
  if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
  if (op == "|") return 5;
  if (op == "^") return 6;
  if (op == "&") return 7;
  if (op == "<<" || op == ">>") return 8;
  if (op == "+" || op == "-") return 9;
  if (op == "*" || op == "/" || op == "%") return 10;
  return -1;
}

class Reader {
 public:
  Reader(Arena& arena, std::string_view source, std::uint64_t origin_id,
         std::uint32_t base_line, std::uint32_t base_column)
      : arena_(arena), tokens_(lex(source)), origin_id_(origin_id),
        base_line_(base_line), base_column_(base_column) {}
  ExprId parse() {
    const auto id = expression(0);
    if (position_ != tokens_.size())
      throw std::invalid_argument("EST trailing expression token '" +
                                  tokens_[position_].text + "'");
    return id;
  }
 private:
  Arena& arena_;
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
  std::uint64_t origin_id_ = 0;
  std::uint32_t base_line_ = 1, base_column_ = 1;
  ExprId append(Node node) {
    const auto index = position_ == 0 ? 0 : position_ - 1;
    const auto span = tokens_.empty() ? SourceSpan{} :
        SourceSpan{base_line_ + tokens_[std::min(index, tokens_.size() - 1)].line - 1,
                   tokens_[std::min(index, tokens_.size() - 1)].line == 1
                       ? base_column_ + tokens_[std::min(index, tokens_.size() - 1)].column - 1
                       : tokens_[std::min(index, tokens_.size() - 1)].column,
                   origin_id_};
    return arena_.append(std::move(node), span);
  }
  bool take(const char* spelling) {
    if (position_ < tokens_.size() && tokens_[position_].text == spelling) { ++position_; return true; }
    return false;
  }
  void expect(const char* spelling) {
    if (!take(spelling)) throw std::invalid_argument(std::string("EST expected ") + spelling);
  }
  std::vector<ExprId> call_arguments() {
    expect("(");
    std::vector<ExprId> args;
    if (!take(")")) {
      do {
        if (position_ + 1 < tokens_.size() && tokens_[position_].word &&
            (tokens_[position_ + 1].text == ":" ||
             tokens_[position_ + 1].text == "=")) {
          const auto name = tokens_[position_++].text;
          ++position_;
          args.push_back(append(NamedArgument{name, expression(0)}));
        } else {
          args.push_back(expression(0));
        }
      } while (take(","));
      expect(")");
    }
    return args;
  }
  std::vector<ExprId> collection_items(char close) {
    std::vector<ExprId> items;
    if (take(close == ']' ? "]" : "}")) return items;
    do {
      if (position_ + 1 < tokens_.size() && tokens_[position_].word &&
          (tokens_[position_ + 1].text == ":" ||
           tokens_[position_ + 1].text == "=")) {
        const auto name = tokens_[position_++].text;
        ++position_;
        items.push_back(append(NamedArgument{name, expression(0)}));
      } else items.push_back(expression(0));
    } while (take(","));
    expect(close == ']' ? "]" : "}");
    return items;
  }
  ExprId message_primary() {
    if (position_ == tokens_.size() || !tokens_[position_].word)
      throw std::invalid_argument("EST message requires a target");
    ExprId target = append(Identifier{tokens_[position_++].text});
    for (;;) {
      if (!take(".")) throw std::invalid_argument("EST message requires target.Handler");
      if (position_ == tokens_.size() || !tokens_[position_].word)
        throw std::invalid_argument("EST message requires handler name");
      const auto name = tokens_[position_++].text;
      if (position_ < tokens_.size() && tokens_[position_].text == "(")
        return append(Message{target, name, call_arguments()});
      target = append(Member{target, name});
    }
  }
  ExprId primary() {
    if (take("not")) return append(Unary{"not", expression(4)});
    if (take("~")) return append(Unary{"~", expression(11)});
    if (take("-")) return append(Unary{"-", expression(11)});
    if (take("+")) return append(Unary{"+", expression(11)});
    if (position_ + 2 < tokens_.size() && tokens_[position_].text == "message" &&
        tokens_[position_ + 1].word && tokens_[position_ + 2].text == ".") {
      ++position_;
      return message_primary();
    }
    ExprId id = 0;
    if (take("[")) {
      id = append(Collection{'[', collection_items(']')});
    } else if (take("{")) {
      id = append(Collection{'{', collection_items('}')});
    } else if (take("(")) {
      const auto first = expression(0);
      if (take(",")) {
        std::vector<ExprId> elements{first};
        do { elements.push_back(expression(0)); } while (take(","));
        expect(")");
        id = append(Tuple{std::move(elements)});
      } else {
        expect(")");
        id = append(Parenthesized{first});
      }
    } else {
      if (position_ == tokens_.size()) throw std::invalid_argument("EST missing expression");
      const auto token = tokens_[position_++];
      id = token.text == "_" ? append(Placeholder{})
          : token.literal || token.text == "true" || token.text == "false"
          ? append(Literal{token.text})
          : token.word ? append(Identifier{token.text})
          : throw std::invalid_argument("EST unsupported expression token");
    }
    while (position_ < tokens_.size()) {
      if (take(".")) {
        if (position_ == tokens_.size() || !tokens_[position_].word)
          throw std::invalid_argument("EST expected member name");
        id = append(Member{id, tokens_[position_++].text});
      } else if (take("[")) {
        const auto* type_name = std::get_if<Identifier>(&arena_.at(id));
        if (type_name && !type_name->name.empty() &&
            std::isupper(static_cast<unsigned char>(type_name->name.front()))) {
          std::vector<ExprId> type_arguments;
          if (!take("]")) {
            do { type_arguments.push_back(expression(0)); } while (take(","));
            expect("]");
          }
          if (position_ < tokens_.size() && tokens_[position_].text == "(") {
            auto arguments = call_arguments();
            id = append(Constructor{id, std::move(type_arguments),
                                           std::move(arguments)});
            continue;
          }
          id = append(Constructor{id, std::move(type_arguments), {}});
          continue;
        }
        const auto index = expression(0);
        expect("]");
        id = append(Index{id, index});
      } else if (take("(")) {
        --position_;
        auto arguments = call_arguments();
        const auto* named = std::get_if<Identifier>(&arena_.at(id));
        const auto* member = std::get_if<Member>(&arena_.at(id));
        const auto* member_base = member ?
            std::get_if<Identifier>(&arena_.at(member->base)) : nullptr;
        const bool constructor =
            (named && !named->name.empty() &&
             std::isupper(static_cast<unsigned char>(named->name.front()))) ||
            (member_base && !member_base->name.empty() &&
             std::isupper(static_cast<unsigned char>(member_base->name.front())));
        id = constructor ? append(Constructor{id, {}, std::move(arguments)})
                         : append(Call{id, std::move(arguments)});
      } else break;
    }
    return id;
  }
  ExprId expression(int min_precedence) {
    auto left = primary();
    while (position_ < tokens_.size()) {
      const auto op = tokens_[position_].text;
      const auto level = precedence(op);
      if (level < min_precedence) break;
      ++position_;
      const auto right = expression(level + 1);
      if (op == "|>") {
        const auto* existing = std::get_if<Pipeline>(&arena_.at(left));
        if (existing) {
          auto stages = existing->stages;
          stages.push_back(right);
          left = append(Pipeline{existing->input, std::move(stages)});
        } else left = append(Pipeline{left, {right}});
      } else left = append(Binary{op, left, right});
    }
    return left;
  }
};
}  // namespace

ExprId parse_expression(Arena& arena, std::string_view source,
                        std::uint64_t origin_id, std::uint32_t base_line,
                        std::uint32_t base_column) {
  return Reader(arena, source, origin_id, base_line, base_column).parse();
}

namespace {
bool deferred_27_1b(const std::string& source) {
  (void)source;
  return false;
#if 0
  const auto tokens = lex(source);
  if (!tokens.empty() &&
      (tokens.front().text == "Vector" || tokens.front().text == "Map" ||
       tokens.front().text == "Queue") && tokens.size() > 1 &&
      (tokens[1].text == "[" || tokens[1].text == "(")) return true;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].text == "|" && i + 1 < tokens.size() &&
        tokens[i + 1].text == ">") return true;
    if (tokens[i].text == "_" && i + 1 < tokens.size() &&
        (tokens[i + 1].text == ")" || tokens[i + 1].text == "," ||
         tokens[i + 1].text == "*" || tokens[i + 1].text == "+" ||
         tokens[i + 1].text == "-")) return true;
    if (tokens[i].text == "{" || tokens[i].text == "}") return true;
    if (tokens[i].text == "[") {
      const bool value_start = i == 0 || tokens[i - 1].text == "(" ||
          tokens[i - 1].text == "," || tokens[i - 1].text == "[";
      if (value_start) return true;
    }
    if (tokens[i].word && i + 1 < tokens.size() &&
        tokens[i + 1].text == "(" && !tokens[i].text.empty()) {
      const bool type_name =
          std::isupper(static_cast<unsigned char>(tokens[i].text.front())) ||
          (tokens[i].text.front() == '_' && tokens[i].text.size() > 1);
      const bool qualified_case = i >= 2 && tokens[i - 1].text == "." &&
          tokens[i - 2].word && !tokens[i - 2].text.empty() &&
          std::isupper(static_cast<unsigned char>(tokens[i - 2].text.front()));
      const bool message_handler = !tokens.empty() &&
          tokens.front().text == "message" && i >= 3 &&
          tokens[1].word && tokens[2].text == ".";
      int depth = 0;
      bool labeled_arguments = false;
      for (std::size_t j = i + 1; j < tokens.size(); ++j) {
        if (tokens[j].text == "(") ++depth;
        else if (tokens[j].text == ")" && --depth == 0) break;
        else if (depth == 1 && (tokens[j].text == ":" || tokens[j].text == "="))
          labeled_arguments = true;
      }
      const bool qualified_constructor = i >= 2 && tokens[i - 1].text == "." &&
          labeled_arguments;
      if ((type_name || qualified_case) &&
          (i < 2 || tokens[i - 1].text != "." || qualified_constructor) &&
          !message_handler)
        return true;
    }
  }
  if (tokens.size() > 1 && tokens.front().word && tokens[1].text == ":")
    return true;
  return false;
#endif
}

bool rejected_source_spelling(const std::string& source) {
  const auto tokens = lex(source);
  if (tokens.size() == 1 && (tokens[0].text == "]" || tokens[0].text == ")" ||
                             tokens[0].text == "["))
    return true;
  if (tokens.size() > 1 && tokens.front().word &&
      (tokens.front().text == "spawn" || tokens.front().text == "await"))
    return true;
  if (tokens.size() > 1 && tokens.front().word && tokens[1].text == ":")
    return true;
  if (tokens.size() > 2 && tokens.front().text == "{" &&
      std::any_of(tokens.begin(), tokens.end(),
                  [](const Token& token) { return token.text == ":"; }))
    return true;
  if (tokens.size() > 1 && (tokens.back().text == "," || tokens.back().text == "("))
    return true;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i].text;
    if (token == "**" || token == "&&" || token == "||" || token == "in")
      return true;
    if ((token == "*" || token == "&" || token == "|") &&
        i + 1 < tokens.size() && tokens[i + 1].text == token) return true;
  }
  if (!tokens.empty() && tokens.front().text == "message") {
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
      if (tokens[i].word && tokens[i + 1].text == ":") return true;
  }
  for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (!tokens[i].word || tokens[i + 1].text != "(") continue;
    if (tokens[i].text == "sort") continue;
    int depth = 0;
    for (std::size_t j = i + 1; j < tokens.size(); ++j) {
      if (tokens[j].text == "(") ++depth;
      else if (tokens[j].text == ")" && --depth == 0) break;
      else if (depth == 1 && tokens[j].word && j + 1 < tokens.size() &&
               tokens[j + 1].text == ":") return true;
    }
  }
  if (!tokens.empty()) {
    static const std::set<std::string> incomplete = {
        "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=",
        "&", "|", "^", "<<", ">>", "and", "xor", "or"};
    if (incomplete.count(tokens.back().text)) return true;
  }
  return false;
}

const char* slot_name(ExprSlot slot) {
  static const char* names[] = {"Condition", "Initializer", "ReturnValue",
      "Iterable", "Receiver", "Argument", "LhsPlace", "RhsValue",
      "Scrutinee", "Pattern", "RaisedValue", "Expression"};
  return names[static_cast<std::size_t>(slot)];
}

[[maybe_unused]] std::string quoted(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (const char c : value) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else out << c;
  }
  out << '"';
  return out.str();
}

std::string dump_node(const Arena& arena, ExprId id) {
  const auto& node = arena.at(id);
  return std::visit([&](const auto& value) -> std::string {
    using T = std::decay_t<decltype(value)>;
    std::ostringstream out;
    if constexpr (std::is_same_v<T, Literal>)
      out << "(literal#" << id << ' ' << quoted(value.spelling) << ')';
    else if constexpr (std::is_same_v<T, Identifier>)
      out << "(identifier#" << id << ' ' << quoted(value.name) << ')';
    else if constexpr (std::is_same_v<T, Unary>)
      out << "(unary#" << id << ' ' << quoted(value.op) << ' '
          << dump_node(arena, value.operand) << ')';
    else if constexpr (std::is_same_v<T, Binary>)
      out << "(binary#" << id << ' ' << quoted(value.op) << ' '
          << dump_node(arena, value.left) << ' '
          << dump_node(arena, value.right) << ')';
    else if constexpr (std::is_same_v<T, Call>) {
      out << "(call#" << id << ' ' << dump_node(arena, value.callee);
      for (const auto argument : value.arguments)
        out << ' ' << dump_node(arena, argument);
      out << ')';
    } else if constexpr (std::is_same_v<T, Member>)
      out << "(member#" << id << ' ' << dump_node(arena, value.base) << ' '
          << quoted(value.name) << ')';
    else if constexpr (std::is_same_v<T, Index>)
      out << "(index#" << id << ' ' << dump_node(arena, value.base) << ' '
          << dump_node(arena, value.index) << ')';
    else if constexpr (std::is_same_v<T, Parenthesized>)
      out << "(paren#" << id << ' ' << dump_node(arena, value.inner) << ')';
    else if constexpr (std::is_same_v<T, Message>) {
      out << "(message#" << id << ' ' << dump_node(arena, value.target) << ' '
          << quoted(value.handler);
      for (const auto argument : value.arguments)
        out << ' ' << dump_node(arena, argument);
      out << ')';
    } else if constexpr (std::is_same_v<T, NamedArgument>)
      out << "(named-argument#" << id << ' ' << quoted(value.name) << ' '
          << dump_node(arena, value.value) << ')';
    else if constexpr (std::is_same_v<T, Placeholder>)
      out << "(placeholder#" << id << ')';
    else if constexpr (std::is_same_v<T, Collection>) {
      out << "(collection#" << id << ' ' << value.delimiter;
      for (const auto element : value.elements) out << ' ' << dump_node(arena, element);
      out << (value.delimiter == '[' ? "]" : "}") << ')';
    } else if constexpr (std::is_same_v<T, Tuple>) {
      out << "(tuple#" << id;
      for (const auto element : value.elements) out << ' ' << dump_node(arena, element);
      out << ')';
    } else if constexpr (std::is_same_v<T, Constructor>) {
      out << "(constructor#" << id << ' ' << dump_node(arena, value.type);
      for (const auto type : value.type_arguments)
        out << " (type " << dump_node(arena, type) << ')';
      for (const auto argument : value.arguments)
        out << ' ' << dump_node(arena, argument);
      out << ')';
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      out << "(pipeline#" << id << ' ' << dump_node(arena, value.input);
      for (const auto stage : value.stages) out << ' ' << dump_node(arena, stage);
      out << ')';
    }
    return out.str();
  }, node);
}
}  // namespace

static void populate_impl(Program& program, std::vector<Stmt>* additional,
                          std::uint64_t origin_id) {
  const auto current_mode = mode();
  if (current_mode == Mode::Off) {
    if (const char* stats = std::getenv("MOSS_EST_STATS"); stats &&
        std::string_view(stats) == "1")
      std::cerr << "MOSS_EST_STATS arena_size=0 handle_count=0\n";
    return;
  }
  if (program.expression_arena && !additional) return;
  if (!program.expression_arena) program.expression_arena = std::make_shared<Arena>();
  if (std::find(program.expression_arenas.begin(), program.expression_arenas.end(),
                program.expression_arena) == program.expression_arenas.end())
    program.expression_arenas.push_back(program.expression_arena);
  const auto primary_it = std::find(program.expression_arenas.begin(),
                                    program.expression_arenas.end(),
                                    program.expression_arena);
  const auto primary_arena_index = static_cast<std::uint32_t>(
      std::distance(program.expression_arenas.begin(), primary_it));
  auto& arena = *program.expression_arena;
  const auto coverage_before = arena.coverage();
  const bool dump_enabled = [] {
    const char* dump = std::getenv("MOSS_EST_DUMP");
    return dump && std::string_view(dump) == "1";
  }();
  std::uint64_t next_owner = origin_id ? origin_id : 1;
  const std::string source_file = program.source_lines.empty()
      ? std::string{} : program.source_lines.front().source_file;
  std::uint32_t current_line = 1, current_indent = 0;
  std::string current_line_text;
  auto set_location = [&](int line, const std::string& fallback) {
    current_line = line > 0 ? static_cast<std::uint32_t>(line) : 1;
    current_indent = 0;
    current_line_text = fallback;
    for (const auto& source_line : program.source_lines) {
      if (source_line.no != line ||
          (!source_file.empty() && source_line.source_file != source_file)) continue;
      current_indent = static_cast<std::uint32_t>(std::max(source_line.indent, 0));
      current_line_text = source_line.text;
      break;
    }
  };

  auto append_handle = [&](std::vector<Handle>& handles, std::uint64_t owner,
                           ExprSlot slot, std::uint32_t index,
                           const std::string& source, ExprId id) {
    handles.push_back({owner, slot, index, id, fingerprint(source), origin_id,
                       primary_arena_index});
    if (dump_enabled) {
      std::ostringstream line;
      line << "MOSS_EST_DUMP owner=" << owner << " slot=" << slot_name(slot)
           << " arg=" << index << " origin=" << origin_id << " span="
           << arena.span(id).line << ':' << arena.span(id).column << ' '
           << dump_node(arena, id) << '\n';
      std::cerr << line.str();
      if (const char* path = std::getenv("MOSS_EST_DUMP_FILE")) {
        std::ofstream out(path, std::ios::app);
        if (!out) throw std::runtime_error("cannot open MOSS_EST_DUMP_FILE");
        out << line.str();
      }
    }
  };
  auto bind = [&](std::vector<Handle>& handles, std::uint64_t owner,
                  ExprSlot slot, std::uint32_t index,
                  const std::string& source) -> std::optional<ExprId> {
    if (source.empty()) return std::nullopt;
    bool deferred = false;
    try { deferred = deferred_27_1b(source); }
    catch (const std::invalid_argument&) { }
    if (deferred) {
      arena.note(owner, slot, index, Outcome::Deferred27_1b);
      return std::nullopt;
    }
    try {
      const auto offset = current_line_text.find(source);
      const auto column = current_indent + (offset == std::string::npos ? 0 :
          static_cast<std::uint32_t>(offset)) + 1;
      const auto id = parse_expression(arena, source, origin_id,
                                       current_line, column);
      arena.note(owner, slot, index, Outcome::Built);
      append_handle(handles, owner, slot, index, source, id);
      return id;
    } catch (const std::invalid_argument& error) {
      bool rejected = false;
      try { rejected = rejected_source_spelling(source); }
      catch (const std::invalid_argument&) { rejected = true; }
      arena.note(owner, slot, index,
                 rejected ? Outcome::RejectedSource : Outcome::Failed);
      if (!rejected) {
        if (const char* failures = std::getenv("MOSS_EST_FAILURE_FILE")) {
          std::ofstream out(failures, std::ios::app);
          if (!out) throw std::runtime_error("cannot open MOSS_EST_FAILURE_FILE");
          auto field = [&](std::string_view value) {
            for (const char c : value) {
              if (c == '\\' || c == '\t' || c == '\n' || c == '\r') out << '\\';
              if (c == '\n') out << 'n';
              else if (c == '\r') out << 'r';
              else if (c == '\t') out << 't';
              else out << c;
            }
          };
          out << static_cast<std::size_t>(slot) << '\t';
          field(source);
          out << '\t';
          field(error.what());
          out << '\t';
          field(source_file);
          out << '\n';
        }
      }
      return std::nullopt;
    }
  };
  auto attach_message_arguments = [&](std::vector<Handle>& handles,
                                      std::uint64_t owner,
                                      const std::vector<std::string>& sources,
                                      const Message& message) {
    for (std::uint32_t index = 0; index < sources.size(); ++index) {
      if (index < message.arguments.size()) {
        arena.note(owner, ExprSlot::Argument, index, Outcome::Built);
        append_handle(handles, owner, ExprSlot::Argument, index, sources[index],
                      message.arguments[index]);
      } else {
        bind(handles, owner, ExprSlot::Argument, index, sources[index]);
      }
    }
  };
  auto join = [](const std::vector<std::string>& values) {
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i) result += ", ";
      result += values[i];
    }
    return result;
  };
  auto statement = [&](Stmt& s) {
    if (additional && (!s.expression_handles.empty() || s.est_raw_form)) return;
    set_location(s.line, s.text);
    const auto owner = next_owner++;
    switch (s.kind) {
      case Stmt::Kind::If: case Stmt::Kind::While:
        bind(s.expression_handles, owner, ExprSlot::Condition, 0, s.a); break;
      case Stmt::Kind::For:
        bind(s.expression_handles, owner, ExprSlot::Iterable, 0, s.b); break;
      case Stmt::Kind::Match:
        bind(s.expression_handles, owner, ExprSlot::Scrutinee, 0, s.a); break;
      case Stmt::Kind::Var:
        bind(s.expression_handles, owner, ExprSlot::Initializer, 0, s.b); break;
      case Stmt::Kind::Assign: {
        assert(!s.est_raw_form.has_value());
        const auto target = trim(s.a);
        const bool compound = !target.empty() &&
            std::string_view("+-*/%&|^").find(target.back()) !=
                std::string_view::npos;
        s.est_raw_form = compound ? RawForm::CompoundAssign : RawForm::Assign;
        bind(s.expression_handles, owner, ExprSlot::LhsPlace, 0,
             compound ? target.substr(0, target.size() - 1) : target);
        bind(s.expression_handles, owner, ExprSlot::RhsValue, 0, s.b);
        if (dump_enabled)
          std::cerr << "MOSS_EST_RAW owner=" << owner << " form="
                    << (compound ? "CompoundAssign" : "Assign") << '\n';
        break;
      }
      case Stmt::Kind::Reply: case Stmt::Kind::Return:
        bind(s.expression_handles, owner, ExprSlot::ReturnValue, 0, s.a); break;
      case Stmt::Kind::Raise:
        bind(s.expression_handles, owner, ExprSlot::RaisedValue, 0, s.a); break;
      case Stmt::Kind::Message: {
        const auto text = "message " + s.a + "." + s.b + "(" + join(s.args) + ")";
        const auto root = bind(s.expression_handles, owner, ExprSlot::Receiver, 0, text);
        if (root) {
          const auto* message = std::get_if<Message>(&arena.at(*root));
          assert(message);
          attach_message_arguments(s.expression_handles, owner, s.args, *message);
        } else {
          for (std::uint32_t index = 0; index < s.args.size(); ++index)
            bind(s.expression_handles, owner, ExprSlot::Argument, index, s.args[index]);
        }
        break;
      }
      case Stmt::Kind::Call: {
        assert(!s.est_raw_form.has_value());
        s.est_raw_form = RawForm::ExprStmt;
        bind(s.expression_handles, owner, ExprSlot::Receiver, 0, s.a);
        for (std::uint32_t index = 0; index < s.args.size(); ++index)
          bind(s.expression_handles, owner, ExprSlot::Argument, index, s.args[index]);
        if (dump_enabled)
          std::cerr << "MOSS_EST_RAW owner=" << owner << " form=ExprStmt\n";
        break;
      }
      case Stmt::Kind::Echo:
        for (std::uint32_t index = 0; index < s.args.size(); ++index)
          bind(s.expression_handles, owner, ExprSlot::Argument, index, s.args[index]);
        break;
      case Stmt::Kind::Raw: {
        assert(!s.est_raw_form.has_value());
        const auto eq = top_level_assignment(s.text);
        const bool compound = eq != std::string::npos && eq > 0 &&
            std::string_view("+-*/%&|^").find(s.text[eq - 1]) !=
                std::string_view::npos;
        if (compound) {
          s.est_raw_form = RawForm::CompoundAssign;
          bind(s.expression_handles, owner, ExprSlot::LhsPlace, 0,
               s.text.substr(0, eq - 1));
          bind(s.expression_handles, owner, ExprSlot::RhsValue, 0,
               s.text.substr(eq + 1));
        } else if (eq != std::string::npos) {
          s.est_raw_form = RawForm::Assign;
          bind(s.expression_handles, owner, ExprSlot::LhsPlace, 0,
               s.text.substr(0, eq));
          bind(s.expression_handles, owner, ExprSlot::RhsValue, 0,
               s.text.substr(eq + 1));
        } else {
          s.est_raw_form = RawForm::ExprStmt;
          bind(s.expression_handles, owner, ExprSlot::Expression, 0, s.text);
        }
        assert(s.est_raw_form.has_value());
        if (dump_enabled) {
          const char* form = *s.est_raw_form == RawForm::Assign ? "Assign" :
              *s.est_raw_form == RawForm::CompoundAssign ? "CompoundAssign" :
              "ExprStmt";
          std::cerr << "MOSS_EST_RAW owner=" << owner << " form=" << form << '\n';
        }
        break;
      }
      default: break;
    }
  };
  auto body = [&](std::vector<Stmt>& statements) {
    for (auto& current : statements) statement(current);
  };
  if (additional) body(*additional);
  else if (program.main) body(program.main->body);
  if (!additional) {
  for (auto& function : program.functions) {
    body(function.body);
    if (function.result_expression) {
      set_location(function.result_line, *function.result_expression);
      bind(function.expression_handles, next_owner++, ExprSlot::ReturnValue, 0,
           *function.result_expression);
    }
  }
  for (auto& domain : program.domains) {
    for (auto& field : domain.state) {
      set_location(field.line, field.init);
      bind(field.expression_handles, next_owner++, ExprSlot::Initializer, 0,
           field.init);
    }
    for (auto& handler : domain.handlers) body(handler.body);
  }
  for (auto& object : program.objects) for (auto& method : object.methods) {
    body(method.body);
    if (method.result_expression) {
      set_location(method.result_line, *method.result_expression);
      bind(method.expression_handles, next_owner++, ExprSlot::ReturnValue, 0,
           *method.result_expression);
    }
  }
  for (auto& object : program.objects) for (auto& field : object.fields) {
    set_location(field.line, field.init);
    bind(field.expression_handles, next_owner++, ExprSlot::Initializer, 0,
         field.init);
  }
  for (auto& test : program.tests) body(test.body);
  for (auto& benchmark : program.benchmarks) body(benchmark.body);
  }

  if (const char* report = std::getenv("MOSS_EST_COVERAGE_FILE")) {
    std::ofstream out(report, std::ios::app);
    if (!out) throw std::runtime_error("cannot open MOSS_EST_COVERAGE_FILE");
    if (out.tellp() == 0)
      out << "slot,attempts,unique,built,deferred_27_1b,rejected_source,failed\n";
    for (std::size_t i = 0; i < Arena::slot_count; ++i) {
      const auto& count = arena.coverage()[i];
      const auto& before = coverage_before[i];
      const auto attempts = count.attempts - before.attempts;
      if (attempts)
        out << i << ',' << attempts << ',' << count.unique - before.unique << ','
            << count.built - before.built << ','
            << count.deferred_27_1b - before.deferred_27_1b << ','
            << count.rejected_source - before.rejected_source << ','
            << count.failed - before.failed << '\n';
    }
  }
  if (const char* stats = std::getenv("MOSS_EST_STATS"); stats &&
      std::string_view(stats) == "1") {
    std::uint64_t built = 0;
    for (const auto& count : arena.coverage()) built += count.built;
    std::cerr << "MOSS_EST_STATS arena_size=" << arena.size()
              << " handle_count=" << built << '\n';
  }
  const char* contract = std::getenv("MOSS_EST_CONTRACT_TEST");
  if (!additional && contract && std::string_view(contract) == "1" && program.main &&
      !program.main->body.empty()) {
    Arena dedup_probe;
    dedup_probe.note(9, ExprSlot::Argument, 2, Outcome::Built);
    dedup_probe.note(9, ExprSlot::Argument, 2, Outcome::Failed);
    const auto& dedup_count = dedup_probe.coverage()[
        static_cast<std::size_t>(ExprSlot::Argument)];
    assert(dedup_count.attempts == 2 && dedup_count.unique == 1 &&
           dedup_count.built == 1 && dedup_count.failed == 0);
    Program arena_destination, arena_source;
    arena_destination.expression_arena = std::make_shared<Arena>();
    arena_destination.expression_arenas.push_back(
        arena_destination.expression_arena);
    arena_source.expression_arena = std::make_shared<Arena>();
    arena_source.expression_arenas.push_back(arena_source.expression_arena);
    const auto imported_id = arena_source.expression_arena->append(Literal{"kept"});
    arena_source.main = MainProc{};
    arena_source.main->body.emplace_back();
    arena_source.main->body.back().expression_handles.push_back(
        {71, ExprSlot::Expression, 0, imported_id, fingerprint("kept"), 77, 0});
    adopt_program_arenas(arena_destination, arena_source);
    const auto& adopted = arena_source.main->body.back().expression_handles.front();
    assert(adopted.expr == imported_id && adopted.arena_index == 1 &&
           arena_destination.expression_arenas[adopted.arena_index]->at(imported_id).index() ==
               arena_destination.expression_arenas[1]->at(imported_id).index());
    Program copied = program;
    assert(copied.expression_arena == program.expression_arena);
    assert(copied.expression_arenas == program.expression_arenas);
    assert(copied.main && copied.main->body.size() == program.main->body.size());
    for (std::size_t i = 0; i < program.main->body.size(); ++i) {
      const auto& before = program.main->body[i].expression_handles;
      const auto& after = copied.main->body[i].expression_handles;
      assert(before.size() == after.size());
      for (std::size_t j = 0; j < before.size(); ++j)
        assert(before[j].expr == after[j].expr &&
               arena.span(before[j].expr).line ==
                   static_cast<std::uint32_t>(program.main->body[i].line));
    }
    auto shifted = copied.main->body;
    const auto retained = shifted.front().expression_handles;
    shifted.insert(shifted.begin(), Stmt{});
    shifted.erase(shifted.begin());
    assert(shifted.front().expression_handles.size() == retained.size());
    for (std::size_t i = 0; i < retained.size(); ++i)
      assert(arena.at(retained[i].expr).index() ==
             arena.at(shifted.front().expression_handles[i].expr).index());
    auto same_a = parse_expression(arena, "same.path[index]");
    auto same_b = parse_expression(arena, "same.path[index]");
    assert(same_a != same_b);
    const auto* old_chain = std::get_if<Index>(&arena.at(same_a));
    assert(old_chain);
    const auto fresh = fresh_occurrence(arena, same_a);
    const auto* new_chain = std::get_if<Index>(&arena.at(fresh));
    assert(fresh != same_a && new_chain && new_chain->base != old_chain->base &&
           new_chain->index != old_chain->index);
    const auto* old_member = std::get_if<Member>(&arena.at(old_chain->base));
    const auto* new_member = std::get_if<Member>(&arena.at(new_chain->base));
    assert(old_member && new_member && new_member->base != old_member->base);
    const auto located = parse_expression(arena, "left +\n  right", 0x271b);
    const auto location = arena.span(located);
    assert(location.line == 2 && location.column == 3 &&
           location.origin_id == 0x271b);
    for (ExprId id = 0; id < arena.size(); ++id) {
      const auto node_span = arena.span(id);
      assert(node_span.line > 0 && node_span.column > 0);
    }
    std::vector<Stmt> loaded(1);
    loaded[0].kind = Stmt::Kind::Raw;
    loaded[0].text = "loaded + value";
    populate_loaded(program, loaded, 0x27b);
    assert(loaded[0].expression_handles.size() == 1 &&
           loaded[0].expression_handles.front().origin_id == 0x27b &&
           arena.span(loaded[0].expression_handles.front().expr).origin_id == 0x27b);
    const auto old_id = retained.front().expr;
    const auto old_dump = dump_node(arena, old_id);
    auto rebuilt = retained;
    const auto& old_handle = retained.front();
    rebuild_slot(arena, rebuilt, old_handle.owner, old_handle.slot,
                 old_handle.argument_index, "replacement + 1");
    assert(rebuilt.front().expr != old_id && dump_node(arena, old_id) == old_dump);
    const char* stale = std::getenv("MOSS_EST_STALE_TEST");
    if (stale && std::string_view(stale) == "1") {
      Stmt stale_stmt;
      stale_stmt.kind = Stmt::Kind::Raw;
      stale_stmt.text = "f(x)";
      const auto stale_owner = UINT64_MAX;
      const auto stale_root = parse_expression(arena, stale_stmt.text);
      std::vector<Handle> stale_handles{{stale_owner, ExprSlot::Expression, 0,
          stale_root, fingerprint(stale_stmt.text)}};
      stale_stmt.text = "f(y)";
      (void)lookup(stale_handles, arena, stale_owner, ExprSlot::Expression, 0,
                   stale_stmt.text);
      assert(false && "staleness guard did not fire");
    }
  }
}

void populate(Program& program) { populate_impl(program, nullptr, 0); }

void populate_loaded(Program& program, std::vector<Stmt>& statements,
                     std::uint64_t origin_id) {
  populate_impl(program, &statements, origin_id ? origin_id : 1);
}

void adopt_program_arenas(Program& destination, Program& source) {
  if (!source.expression_arena && source.expression_arenas.empty()) return;
  if (source.expression_arenas.empty() && source.expression_arena)
    source.expression_arenas.push_back(source.expression_arena);
  std::vector<std::uint32_t> arena_remap;
  arena_remap.reserve(source.expression_arenas.size());
  for (const auto& arena : source.expression_arenas) {
    auto found = std::find(destination.expression_arenas.begin(),
                           destination.expression_arenas.end(), arena);
    if (found == destination.expression_arenas.end()) {
      destination.expression_arenas.push_back(arena);
      arena_remap.push_back(static_cast<std::uint32_t>(
          destination.expression_arenas.size() - 1));
    } else arena_remap.push_back(static_cast<std::uint32_t>(
        std::distance(destination.expression_arenas.begin(), found)));
  }
  if (!destination.expression_arena) destination.expression_arena =
      source.expression_arena ? source.expression_arena :
      source.expression_arenas.front();
  auto remap = [&](std::vector<Handle>& handles) {
    for (auto& handle : handles)
      if (handle.arena_index < arena_remap.size())
        handle.arena_index = arena_remap[handle.arena_index];
  };
  auto statements = [&](std::vector<Stmt>& body) {
    for (auto& statement : body) remap(statement.expression_handles);
  };
  auto failure_arms = [&](std::vector<FailureArm>& arms) {
    for (auto& arm : arms) {
      statements(arm.body);
      if (arm.execution_body) statements(*arm.execution_body);
    }
  };
  if (source.main) statements(source.main->body);
  for (auto& function : source.functions) {
    remap(function.expression_handles);
    statements(function.body);
    if (function.execution_body) statements(*function.execution_body);
  }
  for (auto& domain : source.domains) {
    for (auto& field : domain.state) remap(field.expression_handles);
    for (auto& handler : domain.handlers) {
      remap(handler.expression_handles);
      statements(handler.body);
      if (handler.execution_body) statements(*handler.execution_body);
      failure_arms(handler.failure_arms);
      if (handler.execution_failure_arms)
        failure_arms(*handler.execution_failure_arms);
    }
  }
  for (auto& object : source.objects) {
    for (auto& field : object.fields) remap(field.expression_handles);
    for (auto& method : object.methods) {
      remap(method.expression_handles);
      statements(method.body);
      if (method.execution_body) statements(*method.execution_body);
    }
  }
  for (auto& test : source.tests) statements(test.body);
  for (auto& benchmark : source.benchmarks) statements(benchmark.body);
}

}  // namespace moss::est
