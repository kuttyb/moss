#include "expression_tree.hpp"
#include "../ast.hpp"
#include "../parser_support.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace moss::est {

ExprId Arena::append(Node node) {
  if (nodes_.size() >= UINT32_MAX) throw std::overflow_error("EST arena full");
  const auto id = static_cast<ExprId>(nodes_.size());
  nodes_.push_back(std::move(node));
  return id;
}

const Node& Arena::at(ExprId id) const { return nodes_.at(id); }

void Arena::note(ExprSlot slot, bool success) {
  auto& count = coverage_.at(static_cast<std::size_t>(slot));
  ++count.attempted;
  if (success) ++count.built;
  else ++count.failed;
}

ExprId Arena::duplicate(ExprId id) {
  const Node old = at(id);
  return std::visit([this](const auto& value) -> ExprId {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Unary>)
      return append(Unary{value.op, duplicate(value.operand)});
    else if constexpr (std::is_same_v<T, Binary>)
      return append(Binary{value.op, duplicate(value.left), duplicate(value.right)});
    else if constexpr (std::is_same_v<T, Call>) {
      const auto callee = duplicate(value.callee);
      std::vector<ExprId> arguments;
      for (const auto argument : value.arguments) arguments.push_back(duplicate(argument));
      return append(Call{callee, std::move(arguments)});
    } else if constexpr (std::is_same_v<T, Member>)
      return append(Member{duplicate(value.base), value.name});
    else if constexpr (std::is_same_v<T, Index>)
      return append(Index{duplicate(value.base), duplicate(value.index)});
    else if constexpr (std::is_same_v<T, Parenthesized>)
      return append(Parenthesized{duplicate(value.inner)});
    else return append(value);
  }, old);
}

void Arena::rebuild(std::vector<Handle>& handles, ExprSlot slot,
                    std::uint32_t argument_index, Node replacement,
                    std::uint64_t source_fingerprint) {
  const auto old_size = nodes_.size();
  for (auto it = handles.begin(); it != handles.end(); ++it) {
    if (it->slot != slot || it->argument_index != argument_index) continue;
    handles.erase(it);
    break;
  }
  const auto id = append(std::move(replacement));
  assert(nodes_.size() == old_size + 1 && id == old_size);
  handles.push_back({slot, argument_index, id, source_fingerprint});
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

Mode mode() {
  const char* raw = std::getenv("MOSS_EST");
  if (!raw || std::string_view(raw) == "off") return Mode::Off;
  if (std::string_view(raw) == "shadow") return Mode::Shadow;
  throw std::invalid_argument("MOSS_EST must be off or shadow (on is reserved)");
}

namespace {
struct Token { std::string text; bool word = false; bool literal = false; };

std::vector<Token> lex(std::string_view source) {
  std::vector<Token> result;
  for (std::size_t i = 0; i < source.size();) {
    const unsigned char c = source[i];
    if (std::isspace(c)) { ++i; continue; }
    const auto start = i;
    if (std::isalpha(c) || c == '_') {
      while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
      result.push_back({std::string(source.substr(start, i-start)), true, false});
    } else if (std::isdigit(c) || (c == '.' && i + 1 < source.size() &&
                                    std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
      while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_' || source[i] == '.')) ++i;
      result.push_back({std::string(source.substr(start, i-start)), false, true});
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
      result.push_back({std::string(source.substr(start, i-start)), false, true});
    } else {
      ++i;
      if (i < source.size()) {
        const auto pair = source.substr(start, 2);
        if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" ||
            pair == "//" || pair == "<<" || pair == ">>") ++i;
      }
      result.push_back({std::string(source.substr(start, i-start)), false, false});
    }
  }
  return result;
}

int precedence(const std::string& op) {
  if (op == "or") return 1;
  if (op == "xor") return 2;
  if (op == "and") return 3;
  if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
  if (op == "+" || op == "-") return 5;
  if (op == "*" || op == "/" || op == "%") return 6;
  return -1;
}

class Reader {
 public:
  Reader(Arena& arena, std::string_view source) : arena_(arena), tokens_(lex(source)) {}
  ExprId parse() {
    const auto id = expression(0);
    if (position_ != tokens_.size()) throw std::invalid_argument("EST trailing expression tokens");
    return id;
  }
 private:
  Arena& arena_;
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
  bool take(const char* spelling) {
    if (position_ < tokens_.size() && tokens_[position_].text == spelling) { ++position_; return true; }
    return false;
  }
  void expect(const char* spelling) {
    if (!take(spelling)) throw std::invalid_argument(std::string("EST expected ") + spelling);
  }
  ExprId primary() {
    if (take("not")) return arena_.append(Unary{"not", expression(4)});
    if (take("-")) return arena_.append(Unary{"-", expression(7)});
    if (take("+")) return arena_.append(Unary{"+", expression(7)});
    if (take("(")) {
      const auto inner = expression(0);
      expect(")");
      return arena_.append(Parenthesized{inner});
    }
    if (position_ == tokens_.size()) throw std::invalid_argument("EST missing expression");
    const auto token = tokens_[position_++];
    ExprId id = token.literal || token.text == "true" || token.text == "false"
        ? arena_.append(Literal{token.text})
        : token.word ? arena_.append(Identifier{token.text})
        : throw std::invalid_argument("EST unsupported expression token");
    while (position_ < tokens_.size()) {
      if (take(".")) {
        if (position_ == tokens_.size() || !tokens_[position_].word)
          throw std::invalid_argument("EST expected member name");
        id = arena_.append(Member{id, tokens_[position_++].text});
      } else if (take("[")) {
        const auto index = expression(0);
        expect("]");
        id = arena_.append(Index{id, index});
      } else if (take("(")) {
        std::vector<ExprId> args;
        if (!take(")")) {
          do { args.push_back(expression(0)); } while (take(","));
          expect(")");
        }
        id = arena_.append(Call{id, std::move(args)});
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
      left = arena_.append(Binary{op, left, right});
    }
    return left;
  }
};
}  // namespace

ExprId parse_expression(Arena& arena, std::string_view source) {
  return Reader(arena, source).parse();
}

void populate(Program& program) {
  if (mode() == Mode::Off) return;
  if (!program.expression_arena) program.expression_arena = std::make_shared<Arena>();
  // The shadow builder is intentionally syntax-only. Checked consumers still
  // read their original string fields until the Phase 27.2 migration.
  auto& arena = *program.expression_arena;
  auto bind = [&](std::vector<Handle>& handles, ExprSlot slot,
                  std::uint32_t index, const std::string& source) {
    if (source.empty()) return;
    try {
      const auto id = parse_expression(arena, source);
      handles.push_back({slot, index, id, fingerprint(source)});
      arena.note(slot, true);
    } catch (const std::invalid_argument& error) {
      arena.note(slot, false);
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
        out << '\n';
      }
    }
  };
  auto statement = [&](Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::If: case Stmt::Kind::While:
        bind(s.expression_handles, ExprSlot::Condition, 0, s.a); break;
      case Stmt::Kind::For:
        bind(s.expression_handles, ExprSlot::Iterable, 0, s.b); break;
      case Stmt::Kind::Match:
        bind(s.expression_handles, ExprSlot::Scrutinee, 0, s.a); break;
      case Stmt::Kind::Var:
        bind(s.expression_handles, ExprSlot::Initializer, 0, s.b); break;
      case Stmt::Kind::Assign:
        bind(s.expression_handles, ExprSlot::LhsPlace, 0, s.a);
        bind(s.expression_handles, ExprSlot::RhsValue, 0, s.b); break;
      case Stmt::Kind::Reply: case Stmt::Kind::Return:
        bind(s.expression_handles, ExprSlot::ReturnValue, 0, s.a); break;
      case Stmt::Kind::Raise:
        bind(s.expression_handles, ExprSlot::RaisedValue, 0, s.a); break;
      case Stmt::Kind::Call: case Stmt::Kind::Message:
        bind(s.expression_handles, ExprSlot::Receiver, 0, s.a);
        for (std::uint32_t i = 0; i < s.args.size(); ++i)
          bind(s.expression_handles, ExprSlot::Argument, i, s.args[i]);
        break;
      case Stmt::Kind::Echo:
        for (std::uint32_t i = 0; i < s.args.size(); ++i)
          bind(s.expression_handles, ExprSlot::Argument, i, s.args[i]);
        break;
      case Stmt::Kind::Raw: {
        const auto eq = top_level_assignment(s.text);
        if (eq != std::string::npos) {
          s.est_raw_form = RawForm::Assign;
          bind(s.expression_handles, ExprSlot::LhsPlace, 0, s.text.substr(0, eq));
          bind(s.expression_handles, ExprSlot::RhsValue, 0, s.text.substr(eq + 1));
        } else if (const auto compound = s.text.find("+="); compound != std::string::npos) {
          s.est_raw_form = RawForm::CompoundAssign;
          bind(s.expression_handles, ExprSlot::LhsPlace, 0, s.text.substr(0, compound));
          bind(s.expression_handles, ExprSlot::RhsValue, 0, s.text.substr(compound + 2));
        } else {
          s.est_raw_form = RawForm::ExprStmt;
          bind(s.expression_handles, ExprSlot::Expression, 0, s.text);
        }
        break;
      }
      default: break;
    }
  };
  auto body = [&](std::vector<Stmt>& statements) {
    for (auto& s : statements) statement(s);
  };
  if (program.main) body(program.main->body);
  for (auto& f : program.functions) {
    body(f.body);
    if (f.result_expression) bind(f.expression_handles, ExprSlot::ReturnValue, 0, *f.result_expression);
  }
  for (auto& d : program.domains) {
    for (auto& field : d.state)
      bind(field.expression_handles, ExprSlot::Initializer, 0, field.init);
    for (auto& h : d.handlers) body(h.body);
  }
  for (auto& o : program.objects) for (auto& m : o.methods) {
    body(m.body);
    if (m.result_expression) bind(m.expression_handles, ExprSlot::ReturnValue, 0, *m.result_expression);
  }
  for (auto& o : program.objects) for (auto& field : o.fields)
    bind(field.expression_handles, ExprSlot::Initializer, 0, field.init);
  for (auto& t : program.tests) body(t.body);
  for (auto& b : program.benchmarks) body(b.body);
  if (const char* report = std::getenv("MOSS_EST_COVERAGE_FILE")) {
    std::ofstream out(report, std::ios::app);
    if (!out) throw std::runtime_error("cannot open MOSS_EST_COVERAGE_FILE");
    for (std::size_t i = 0; i < Arena::slot_count; ++i) {
      const auto& count = arena.coverage()[i];
      if (count.attempted)
        out << i << ',' << count.attempted << ',' << count.built << ','
            << count.failed << '\n';
    }
  }
}

}  // namespace moss::est
