#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ast.hpp"
#include "diagnostics.hpp"

using std::string;
using std::vector;

namespace moss {

static string ltrim(string s) {
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}
static string rtrim(string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}
static string trim(string s) { return rtrim(ltrim(std::move(s))); }
static bool starts_with(const string& s, const string& p) { return s.rfind(p, 0) == 0; }
static bool ends_with(const string& s, const string& p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

static vector<string> split_top_level(const string& s, char delim) {
  vector<string> out;
  int par = 0, br = 0, sq = 0;
  bool in_str = false, esc = false;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(') ++par; else if (c == ')') --par;
    else if (c == '{') ++br; else if (c == '}') --br;
    else if (c == '[') ++sq; else if (c == ']') --sq;
    else if (c == delim && par == 0 && br == 0 && sq == 0) {
      out.push_back(trim(s.substr(start, i - start)));
      start = i + 1;
    }
  }
  out.push_back(trim(s.substr(start)));
  return out;
}

static string canonical_type_name(string type) {
  type = trim(std::move(type));
  if (type == "Int") return "int";
  if (type == "Float") return "float";
  if (type == "Bool") return "bool";
  if (type == "String") return "string";
  if (type == "Vector") return "vector";
  if (type == "Map") return "map";
  if (type == "Queue") return "queue";
  return type;
}

static bool parse_index(const string& text, string& base, string& index) {
  string value = trim(text);
  if (value.empty() || value.back() != ']') return false;
  int depth = 0; bool in_str = false;
  for (size_t i = value.size(); i-- > 0;) {
    char c = value[i];
    if (in_str) { if (c == '"' && (i == 0 || value[i-1] != '\\')) in_str = false; continue; }
    if (c == '"') { in_str = true; continue; }
    if (c == ']') ++depth;
    else if (c == '[' && --depth == 0) {
      base = trim(value.substr(0, i)); index = trim(value.substr(i + 1, value.size() - i - 2));
      return !base.empty() && !index.empty();
    }
  }
  return false;
}

static bool parse_simple_call(const string& text, string& callee, vector<string>& args) {
  string value = trim(text);
  auto lp = value.find('(');
  auto rp = value.rfind(')');
  if (lp == string::npos || rp != value.size() - 1 || lp == 0) return false;
  callee = trim(value.substr(0, lp));
  if (callee.empty()) return false;
  string inside = value.substr(lp + 1, rp - lp - 1);
  args.clear();
  if (!trim(inside).empty()) args = split_top_level(inside, ',');
  return true;
}

static bool plain_identifier(const string& value) {
  string s = trim(value);
  if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
  return std::all_of(s.begin() + 1, s.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  });
}

static bool parse_member_call(const string& text, string& receiver, string& handler,
                              vector<string>& args) {
  string value = trim(text);
  auto dot = value.find('.');
  auto lp = value.find('(', dot == string::npos ? 0 : dot);
  auto rp = value.rfind(')');
  if (dot == string::npos || lp == string::npos || rp != value.size() - 1 || dot >= lp)
    return false;
  receiver = trim(value.substr(0, dot));
  handler = trim(value.substr(dot + 1, lp - dot - 1));
  if (receiver.empty() || handler.empty()) return false;
  string inside = value.substr(lp + 1, rp - lp - 1);
  args.clear();
  if (!trim(inside).empty()) args = split_top_level(inside, ',');
  return true;
}

// A bare `receiver.method` is a callable only inside a functional stage.  It
// remains statically bound to one concrete receiver type and never becomes a
// general runtime method value.
static bool parse_bound_method_callable(const string& text, string& receiver,
                                        string& method) {
  string value = trim(text);
  if (value.find('(') != string::npos || value.find(')') != string::npos)
    return false;
  auto dot = value.rfind('.');
  if (dot == string::npos) return false;
  receiver = trim(value.substr(0, dot));
  method = trim(value.substr(dot + 1));
  return plain_identifier(receiver) && plain_identifier(method) && receiver != "_";
}

static size_t matching_paren(const string& text, size_t open) {
  if (open >= text.size() || text[open] != '(') return string::npos;
  int depth = 0;
  bool in_str = false, esc = false;
  for (size_t i = open; i < text.size(); ++i) {
    char c = text[i];
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(') ++depth;
    else if (c == ')' && --depth == 0) return i;
  }
  return string::npos;
}

static size_t top_level_assignment(const string& text) {
  int par = 0, br = 0, sq = 0;
  bool in_str = false, esc = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(') ++par;
    else if (c == ')') --par;
    else if (c == '{') ++br;
    else if (c == '}') --br;
    else if (c == '[') ++sq;
    else if (c == ']') --sq;
    else if (c == '=' && par == 0 && br == 0 && sq == 0) {
      char before = i ? text[i - 1] : '\0';
      char after = i + 1 < text.size() ? text[i + 1] : '\0';
      if (before != '=' && before != '!' && before != '<' && before != '>' &&
          after != '=') return i;
    }
  }
  return string::npos;
}

static size_t top_level_colon(const string& text) {
  int par = 0, br = 0, sq = 0;
  bool in_str = false, esc = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(') ++par;
    else if (c == ')') --par;
    else if (c == '{') ++br;
    else if (c == '}') --br;
    else if (c == '[') ++sq;
    else if (c == ']') --sq;
    else if (c == ':' && par == 0 && br == 0 && sq == 0) return i;
  }
  return string::npos;
}

// Named object constructor arguments use '=' in the preferred surface syntax.
// The ':' spelling remains a compatibility form while the frontend migrates.
static bool parse_named_argument(const string& text, string& name, string& value) {
  size_t equal = top_level_assignment(text);
  size_t colon = top_level_colon(text);
  size_t delimiter = string::npos;
  if (equal != string::npos && (colon == string::npos || equal < colon)) delimiter = equal;
  else if (colon != string::npos) delimiter = colon;
  if (delimiter == string::npos) return false;
  name = trim(text.substr(0, delimiter));
  value = trim(text.substr(delimiter + 1));
  return plain_identifier(name) && !value.empty();
}

static vector<string> split_pipeline_stages(string expression) {
  expression = trim(std::move(expression));
  vector<string> stages;
  int par = 0, br = 0, sq = 0;
  bool in_str = false, esc = false;
  size_t start = 0;
  for (size_t i = 0; i + 1 < expression.size(); ++i) {
    char c = expression[i];
    if (in_str) {
      if (esc) esc = false;
      else if (c == '\\') esc = true;
      else if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '(') ++par;
    else if (c == ')') --par;
    else if (c == '{') ++br;
    else if (c == '}') --br;
    else if (c == '[') ++sq;
    else if (c == ']') --sq;
    if (c == '|' && expression[i + 1] == '>' && par == 0 && br == 0 && sq == 0) {
      stages.push_back(trim(expression.substr(start, i - start)));
      start = i + 2;
      ++i;
    }
  }
  if (stages.empty()) return {};
  stages.push_back(trim(expression.substr(start)));
  return stages;
}

struct ParsedFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  string raw;
  vector<string> arguments;
};

struct ParsedFunctionalPipeline {
  string source;
  vector<ParsedFunctionalStage> stages;
};

static std::optional<FunctionalNodeKind> functional_stage_kind(const string& name) {
  if (name == "map") return FunctionalNodeKind::Map;
  if (name == "filter") return FunctionalNodeKind::Filter;
  if (name == "reduce") return FunctionalNodeKind::Reduce;
  if (name == "sum") return FunctionalNodeKind::Sum;
  if (name == "count") return FunctionalNodeKind::Count;
  if (name == "any") return FunctionalNodeKind::Any;
  if (name == "all") return FunctionalNodeKind::All;
  return std::nullopt;
}

// This recognizes the functional source surface but deliberately does not
// decide whether the source is a collection.  The typed checker makes that
// decision so legacy/general scalar pipelines keep their existing meaning.
static std::optional<ParsedFunctionalPipeline> parse_functional_pipeline(
    const string& expression) {
  auto parts = split_pipeline_stages(expression);
  if (parts.size() < 2) return std::nullopt;
  ParsedFunctionalPipeline pipeline;
  pipeline.source = parts.front();
  for (size_t index = 1; index < parts.size(); ++index) {
    string callee;
    vector<string> arguments;
    bool call = parse_simple_call(parts[index], callee, arguments);
    if (!call) callee = trim(parts[index]);
    auto kind = functional_stage_kind(callee);
    if (!kind) return std::nullopt;
    ParsedFunctionalStage stage;
    stage.kind = *kind;
    stage.raw = parts[index];
    if (call) stage.arguments = std::move(arguments);
    pipeline.stages.push_back(std::move(stage));
  }
  return pipeline;
}

static bool functional_terminal_kind(FunctionalNodeKind kind) {
  return kind == FunctionalNodeKind::Reduce || kind == FunctionalNodeKind::Sum ||
      kind == FunctionalNodeKind::Count || kind == FunctionalNodeKind::Any ||
      kind == FunctionalNodeKind::All;
}

static bool functional_pipeline_requires_materialization(
    const ParsedFunctionalPipeline& pipeline) {
  return pipeline.stages.empty() ||
      !functional_terminal_kind(pipeline.stages.back().kind);
}

static string normalize_pipeline(string expression) {
  expression = trim(std::move(expression));
  auto stages = split_pipeline_stages(expression);
  if (stages.empty()) return expression;
  string value = stages.front();
  for (size_t i = 1; i < stages.size(); ++i) {
    string stage = stages[i];
    string callee;
    vector<string> args;
    if (parse_simple_call(stage, callee, args)) {
      std::ostringstream call;
      call << callee << "(" << value;
      for (const auto& arg : args) call << ", " << arg;
      call << ")";
      value = call.str();
    } else {
      value = stage + "(" + value + ")";
    }
  }
  return value;
}


static string snake_case(const string& s) {
  string out;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (std::isupper(static_cast<unsigned char>(c))) {
      if (i && !out.empty() && out.back() != '_') out.push_back('_');
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else out.push_back(c);
  }
  return out;
}

class Parser {
 public:
  explicit Parser(vector<Line> lines) : lines_(std::move(lines)) {
    int unit = 0;
    for (const auto& line : lines_) {
      if (line.indent > 0) unit = unit == 0 ? line.indent : std::gcd(unit, line.indent);
    }
    indent_unit_ = unit > 0 ? unit : 2;
    if (indent_unit_ < 2) indent_unit_ = 2;
  }

  Program parse() {
    Program p;
    while (i_ < lines_.size()) {
      const auto& L = lines_[i_];
      if (L.indent != 0) fail(L, "top-level declaration must start at indentation 0");
      if (starts_with(L.text, "domain ")) p.domains.push_back(parse_domain());
      else if (starts_with(L.text, "type ")) p.objects.push_back(parse_object());
      else if (starts_with(L.text, "trait ")) p.traits.push_back(parse_trait());
      else if (L.text == "proc main()" || L.text == "proc main():") {
        if (p.main) fail(L, "duplicate proc main()");
        p.main = parse_main();
      } else if (starts_with(L.text, "fn ")) {
        auto function = parse_function();
        if (function.name == "main") {
          if (p.main) fail(L, "duplicate main function");
          if (function.expression_body)
            fail(L, "main must use an indented statement body");
          if (function.result_expression) {
            Stmt statement;
            statement.line = function.result_line;
            statement.indent = 0;
            statement.text = *function.result_expression;
            statement.continuation_lines = function.result_continuation_lines;
            if (parse_message_call(statement.text, statement.a, statement.b, statement.args))
              statement.kind = Stmt::Kind::Call;
            else {
              string callee;
              if (parse_simple_call(statement.text, callee, statement.args)) {
                statement.kind = Stmt::Kind::Call;
                statement.a = callee;
              } else {
                statement.kind = Stmt::Kind::Raw;
              }
            }
            function.body.push_back(std::move(statement));
            function.result_expression.reset();
          }
          if (!function.body.size())
            fail(L, "main must use an indented statement body");
          p.main = MainProc{std::move(function.body), function.line, function.header};
        } else {
          p.functions.push_back(std::move(function));
        }
      } else {
        fail(L, "expected 'domain', 'type Name:', 'fn', or 'proc main()'");
      }
    }
    return p;
  }

 private:
  vector<Line> lines_;
  size_t i_ = 0;
  int indent_unit_ = 2;

  [[noreturn]] void fail(const Line& L, const string& msg) { throw CompileError(L.no, msg); }

  static bool identifier(const string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
  }

  static bool parse_message_call(const string& text, string& receiver, string& handler,
                                 vector<string>& args) {
    if (!parse_member_call(text, receiver, handler, args)) return false;
    return identifier(receiver) && identifier(handler);
  }

  static bool contains_word_outside_string(const string& text, const string& word) {
    bool in_str = false, esc = false;
    for (size_t i = 0; i + word.size() <= text.size(); ++i) {
      char ch = text[i];
      if (in_str) {
        if (esc) esc = false;
        else if (ch == '\\') esc = true;
        else if (ch == '"') in_str = false;
        continue;
      }
      if (ch == '"') { in_str = true; continue; }
      if (text.compare(i, word.size(), word) != 0) continue;
      bool left = i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_');
      size_t after = i + word.size();
      bool right = after == text.size() || !(std::isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
      if (left && right) return true;
    }
    return false;
  }

  static vector<Param> parse_params(const Line& L, const string& inside) {
    vector<Param> ps;
    if (trim(inside).empty()) return ps;
    for (auto part : split_top_level(inside, ',')) {
      auto c = part.find(':');
      Param p;
      if (c == string::npos) {
        p.name = trim(part);
      } else {
        p.name = trim(part.substr(0, c));
        p.type = canonical_type_name(trim(part.substr(c + 1)));
      }
      if (p.name.empty() || !identifier(p.name)) throw CompileError(L.no, "invalid parameter: " + part);
      ps.push_back(std::move(p));
    }
    return ps;
  }

  ObjectType parse_object() {
    Line head = lines_[i_++];
    // Legacy: type User = object. Preferred: type User:
    string rest = trim(head.text.substr(5));
    auto eq = rest.find('=');
    bool legacy = eq != string::npos;
    if (legacy) {
      if (trim(rest.substr(eq + 1)) != "object")
        fail(head, "object declaration must be 'type Name:' or 'type Name = object'");
    } else if (!ends_with(rest, ":")) {
      fail(head, "object declaration must be 'type Name:' or 'type Name = object'");
    }
    ObjectType o;
    o.header = head.text;
    o.name = legacy ? trim(rest.substr(0, eq)) : trim(rest.substr(0, rest.size() - 1));
    if (!identifier(o.name)) fail(head, "invalid type name '" + o.name + "'");
    o.line = head.no;
    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      auto L = lines_[i_++];
      if (L.indent != head.indent + indent_unit_)
        fail(L, "object fields must use one indentation level");
      if (starts_with(L.text, "fn ")) {
        string sig = trim(L.text.substr(3)); auto lp = sig.find('('), rp = matching_paren(sig, lp);
        if (lp == string::npos || rp == string::npos) fail(L, "object method requires a signature");
        Method m; m.name = trim(sig.substr(0, lp)); m.params = parse_params(L, sig.substr(lp + 1, rp - lp - 1)); m.line = L.no;
        m.owner = o.name;
        string suffix = trim(sig.substr(rp + 1)); if (ends_with(suffix, ":")) suffix.pop_back(); suffix = trim(suffix);
        if (!suffix.empty() && starts_with(suffix, "->")) m.return_type = canonical_type_name(trim(suffix.substr(2)));
        m.body = parse_stmt_block(L.indent + indent_unit_);
        if (!m.body.empty() && (m.body.back().kind == Stmt::Kind::Raw || m.body.back().kind == Stmt::Kind::Call)) {
          m.result_expression = m.body.back().text;
          m.result_line = m.body.back().line;
          m.result_continuation_lines = m.body.back().continuation_lines;
          m.body.pop_back();
        }
        o.methods.push_back(std::move(m));
        continue;
      }
      auto c = L.text.find(':');
      Field f;
      f.header = L.text;
      if (c == string::npos) {
        f.name = trim(L.text);
      } else {
        f.name = trim(L.text.substr(0, c));
        f.type = canonical_type_name(trim(L.text.substr(c + 1)));
      }
      if (!identifier(f.name)) fail(L, "object field must be 'name' or 'name: type'");
      f.line = L.no;
      o.fields.push_back(std::move(f));
    }
    return o;
  }

  Trait parse_trait() {
    Line head = lines_[i_++];
    string rest = trim(head.text.substr(6));
    if (ends_with(rest, ":")) rest.pop_back();
    Trait t; t.name = trim(rest); t.header = head.text; t.line = head.no;
    if (!identifier(t.name)) fail(head, "invalid trait name '" + t.name + "'");
    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      Line L = lines_[i_++];
      if (L.indent != head.indent + indent_unit_ || !starts_with(L.text, "fn "))
        fail(L, "trait members must be method declarations at one indentation level");
      string sig = trim(L.text.substr(3));
      auto lp = sig.find('('), rp = matching_paren(sig, lp);
      if (lp == string::npos || rp == string::npos) fail(L, "trait method requires a signature");
      TraitMethod m; m.name = trim(sig.substr(0, lp)); m.line = L.no;
      if (!identifier(m.name)) fail(L, "invalid trait method name '" + m.name + "'");
      m.params = parse_params(L, sig.substr(lp + 1, rp - lp - 1));
      string suffix = trim(sig.substr(rp + 1));
      if (ends_with(suffix, ":")) suffix.pop_back();
      suffix = trim(suffix);
      if (!suffix.empty()) {
        if (!starts_with(suffix, "->")) fail(L, "trait method return type must follow '->'");
        m.return_type = canonical_type_name(trim(suffix.substr(2)));
      }
      t.methods.push_back(std::move(m));
    }
    if (t.methods.empty()) fail(head, "trait must declare at least one method");
    return t;
  }

  Domain parse_domain() {
    Line head = lines_[i_++];
    Domain d;
    d.header = head.text;
    d.name = trim(head.text.substr(7));
    if (ends_with(d.name, ":")) d.name = trim(d.name.substr(0, d.name.size() - 1));
    d.line = head.no;
    if (d.name.empty()) fail(head, "domain name is required");

    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      const auto L = lines_[i_];
      if (L.indent != head.indent + indent_unit_)
        fail(L, "domain members must use one indentation level");
      if (starts_with(L.text, "var ")) {
        d.state.push_back(parse_state_field());
      } else if (starts_with(L.text, "on ")) {
        d.handlers.push_back(parse_handler(head.indent + indent_unit_, "on"));
      } else if (starts_with(L.text, "fn ")) {
        d.handlers.push_back(parse_handler(head.indent + indent_unit_, "fn"));
      } else {
        // Julia-like state declarations omit both the var keyword and, when
        // inferable, the type annotation: `value = 0`.
        d.state.push_back(parse_state_field());
      }
    }
    return d;
  }

  Field parse_state_field() {
    Line L = lines_[i_++];
    string rest = trim(L.text);
    if (starts_with(rest, "var ")) rest = trim(rest.substr(4));
    if (rest.empty()) fail(L, "state declaration requires a field name");
    Field f;
    f.header = L.text;
    f.line = L.no;
    auto eq = top_level_assignment(rest);
    auto colon = top_level_colon(rest);
    if (colon != string::npos && (eq == string::npos || colon < eq)) {
      f.name = trim(rest.substr(0, colon));
      string rhs = trim(rest.substr(colon + 1));
      if (eq != string::npos) {
        // `eq` is relative to rest and follows the annotation colon.
        f.type = canonical_type_name(trim(rest.substr(colon + 1, eq - colon - 1)));
        f.init = trim(rest.substr(eq + 1));
      } else {
        f.type = canonical_type_name(rhs);
      }
    } else if (eq != string::npos) {
      f.name = trim(rest.substr(0, eq));
      f.init = trim(rest.substr(eq + 1));
    } else {
      // Accept a bare field as an inference request; the checker will reject
      // it if no initializer or other static constraint determines its type.
      f.name = trim(rest);
    }
    if (!plain_identifier(f.name))
      fail(L, "state declaration must be 'name = value' or 'name: type = value'");
    if ((colon != string::npos && f.type.empty()) || (eq != string::npos && f.init.empty()))
      fail(L, "state declaration requires a non-empty type or initializer");
    return f;
  }

  Handler parse_handler(int member_indent, const string& keyword) {
    Line head = lines_[i_++];
    string sig = trim(head.text.substr(keyword.size()));
    auto lp = sig.find('('), rp = matching_paren(sig, lp);
    if (lp == string::npos || rp == string::npos || rp < lp)
      fail(head, "handler must be 'on Name(args...)' or 'fn Name(args...) -> Type'");
    Handler h;
    h.header = head.text;
    h.name = trim(sig.substr(0, lp));
    if (!identifier(h.name)) fail(head, "invalid handler name '" + h.name + "'");
    h.params = parse_params(head, sig.substr(lp + 1, rp - lp - 1));
    string suffix = trim(sig.substr(rp + 1));
    if (ends_with(suffix, ":")) suffix = trim(suffix.substr(0, suffix.size() - 1));
    if (!suffix.empty()) {
      if (!starts_with(suffix, "->"))
        fail(head, "handler must be 'on Name(args...)' or 'fn Name(args...) -> Type'");
      string type = trim(suffix.substr(2));
      if (type.empty()) fail(head, "reply type is required after '->'");
      h.reply_type = canonical_type_name(std::move(type));
    }
    h.line = head.no;
    h.body = parse_stmt_block(member_indent + indent_unit_);
    if (h.body.empty()) fail(head, "handler body may not be empty");
    return h;
  }

  Function parse_function() {
    Line head = lines_[i_++];
    string sig = trim(head.text.substr(3));
    auto lp = sig.find('('), rp = matching_paren(sig, lp);
    if (lp == string::npos || rp == string::npos || rp < lp)
      fail(head, "function must be 'fn Name(args...)', optionally followed by '-> Type', ':' or '= expression'");
    Function f;
    f.name = trim(sig.substr(0, lp));
    f.header = head.text;
    if (!identifier(f.name)) fail(head, "invalid function name '" + f.name + "'");
    f.params = parse_params(head, sig.substr(lp + 1, rp - lp - 1));
    string suffix = trim(sig.substr(rp + 1));
    if (starts_with(suffix, "->")) {
      suffix = trim(suffix.substr(2));
      auto colon = suffix.find(':');
      auto equal = suffix.find('=');
      size_t end = std::min(colon == string::npos ? suffix.size() : colon,
                            equal == string::npos ? suffix.size() : equal);
      string type = trim(suffix.substr(0, end));
      if (type.empty()) fail(head, "function return type is required after '->'");
      f.return_type = canonical_type_name(type);
      suffix = trim(suffix.substr(end));
    }
    f.line = head.no;
    if (starts_with(suffix, "=")) {
      string expression = trim(suffix.substr(1));
      if (expression.empty()) fail(head, "expression-bodied function requires an expression after '='");
      f.expression_body = true;
      f.result_expression = expression;
      f.result_line = head.no;
      return f;
    }
    if (!suffix.empty() && suffix != ":")
      fail(head, "function header must end with ':', '= expression', or an optional '-> Type'");
    f.body = parse_stmt_block(head.indent + indent_unit_);
    if (f.body.empty()) fail(head, "function body may not be empty");
    if (!f.body.empty() && f.body.back().indent == 0 &&
        (f.body.back().kind == Stmt::Kind::Raw || f.body.back().kind == Stmt::Kind::Call)) {
      f.result_expression = f.body.back().text;
      f.result_line = f.body.back().line;
      f.result_continuation_lines = f.body.back().continuation_lines;
      f.body.pop_back();
    }
    if (f.body.empty() && !f.result_expression)
      fail(head, "function body may not be empty");
    return f;
  }

  MainProc parse_main() {
    Line head = lines_[i_++];
    MainProc m;
    m.line = head.no;
    m.header = head.text;
    m.body = parse_stmt_block(indent_unit_);
    if (m.body.empty()) fail(head, "main body may not be empty");
    return m;
  }

  vector<Stmt> parse_stmt_block(int base_indent) {
    vector<Stmt> out;
    while (i_ < lines_.size()) {
      Line L = lines_[i_];
      if (L.indent < base_indent) break;
      if (L.indent % indent_unit_ != 0) fail(L, "indentation must use consistent spaces");
      if (L.indent < base_indent) break;
      // The statement parser preserves nested indentation. A block ends only when we return
      // to indentation less than the base indentation supplied by the parent construct.
      ++i_;
      out.push_back(parse_stmt(L, base_indent));
    }
    return out;
  }

  Stmt parse_stmt(const Line& L, int base_indent) {
    Stmt s;
    s.line = L.no;
    s.indent = (L.indent - base_indent) / indent_unit_;
    s.text = L.text;
    s.continuation_lines = L.continuation_lines;

    static const vector<string> forbidden = {"async ", "yield ", "lock ", "shared ", "thread "};
    for (const auto& k : forbidden) if (starts_with(L.text, k)) fail(L, "'" + trim(k) + "' is not part of Moss's concurrency model");

    if (starts_with(L.text, "echo ")) {
      s.kind = Stmt::Kind::Echo;
      s.args = split_top_level(trim(L.text.substr(5)), ',');
      return s;
    }
    if (starts_with(L.text, "if ")) {
      s.kind = Stmt::Kind::If;
      s.a = trim(L.text.substr(3));
      if (ends_with(s.a, ":")) s.a = trim(s.a.substr(0, s.a.size() - 1));
      return s;
    }
    if (L.text == "else" || L.text == "else:") { s.kind = Stmt::Kind::Else; return s; }
    if (starts_with(L.text, "while ")) {
      s.kind = Stmt::Kind::While;
      s.a = trim(L.text.substr(6));
      if (ends_with(s.a, ":")) s.a = trim(s.a.substr(0, s.a.size() - 1));
      return s;
    }
    if (starts_with(L.text, "message ")) {
      string call = trim(L.text.substr(8));
      if (!parse_message_call(call, s.a, s.b, s.args))
        fail(L, "message requires a domain call 'receiver.Handler(args)'");
      s.kind = Stmt::Kind::Message;
      return s;
    }
    if (starts_with(L.text, "let ") || starts_with(L.text, "var ")) {
      bool is_var = starts_with(L.text, "var ");
      s.kind = is_var ? Stmt::Kind::Var : Stmt::Kind::Let;
      string rest = trim(L.text.substr(4));
      auto eq = top_level_assignment(rest);
      if (eq == string::npos) fail(L, is_var ? "local var requires an initializer in v0.2" : "let requires an initializer");
      s.a = trim(rest.substr(0, eq));
      s.b = trim(rest.substr(eq + 1));
      if (starts_with(s.b, "await ")) {
        s.kind = Stmt::Kind::AwaitMessage;
        s.is_mutable = is_var;
        string call = trim(s.b.substr(6));
        if (!parse_message_call(call, s.b, s.c, s.args))
          fail(L, "await requires a domain call 'receiver.Handler(args)'");
      } else if (contains_word_outside_string(s.b, "await")) {
        fail(L, "await is only supported as the complete initializer of let or local var");
      }
      return s;
    }
    auto assignment = top_level_assignment(L.text);
    if (assignment != string::npos) {
      s.a = trim(L.text.substr(0, assignment));
      s.b = trim(L.text.substr(assignment + 1));
      if (s.a.empty() || s.b.empty()) fail(L, "assignment requires a target and an expression");
      s.declaration = false;
      if (starts_with(s.b, "await ")) {
        s.kind = Stmt::Kind::AwaitMessage;
        string call = trim(s.b.substr(6));
        if (!parse_message_call(call, s.b, s.c, s.args))
          fail(L, "await requires a domain call 'receiver.Handler(args)'");
      } else if (contains_word_outside_string(s.b, "await")) {
        fail(L, "await is only supported as the complete right-hand side of an assignment");
      } else {
        s.kind = Stmt::Kind::Assign;
      }
      return s;
    }
    if (L.text == "reply") fail(L, "reply requires a value");
    if (starts_with(L.text, "reply ")) {
      s.kind = Stmt::Kind::Reply;
      s.a = trim(L.text.substr(6));
      if (s.a.empty()) fail(L, "reply requires a value");
      return s;
    }
    if (L.text == "return") { s.kind = Stmt::Kind::Return; return s; }
    if (starts_with(L.text, "return ")) {
      s.kind = Stmt::Kind::Return;
      s.a = trim(L.text.substr(7));
      if (s.a.empty()) fail(L, "return requires a value");
      return s;
    }

    if (starts_with(L.text, "await "))
      fail(L, "await requires an assignment target, for example 'value = await receiver.Handler(...)'");

    if (contains_word_outside_string(L.text, "await"))
      fail(L, "await is only supported as a complete Moss request/reply assignment");

    // A naked dotted call is kept distinct so the checker can reject it for a domain
    // receiver while allowing future local member-call syntax.
    if (parse_message_call(L.text, s.a, s.b, s.args)) {
      s.kind = Stmt::Kind::Call;
      return s;
    }
    string callee;
    if (parse_simple_call(L.text, callee, s.args)) {
      s.kind = Stmt::Kind::Call;
      s.a = callee;
      return s;
    }

    s.kind = Stmt::Kind::Raw;
    return s;
  }
};

static vector<Line> lex_lines(std::istream& in) {
  vector<Line> out;
  string raw;
  int no = 0;
  while (std::getline(in, raw)) {
    ++no;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    size_t first = raw.find_first_not_of(' ');
    if (first == string::npos) continue;
    if (raw[first] == '#') continue;
    if (raw.find('\t') != string::npos) throw CompileError(no, "tabs are not allowed; use space indentation");

    // Strip comments outside strings.
    bool in_str = false, esc = false;
    size_t cut = raw.size();
    for (size_t i = first; i < raw.size(); ++i) {
      char c = raw[i];
      if (in_str) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"') in_str = false;
      } else {
        if (c == '"') in_str = true;
        else if (c == '#') { cut = i; break; }
      }
    }
    raw = rtrim(raw.substr(0, cut));
    if (trim(raw).empty()) continue;
    int indent = 0;
    while (indent < (int)raw.size() && raw[indent] == ' ') ++indent;
    string text = trim(raw.substr(indent));
    if (starts_with(text, "|>") && !out.empty()) {
      out.back().text += " " + text;
      out.back().continuation_lines.push_back(no);
      continue;
    }
    out.push_back(Line{no, indent, std::move(text), {}});
  }
  return out;
}

enum class MethodResolutionFailure {
  None,
  UnknownReceiver,
  MissingMethod,
  WrongArity,
  IncompatibleArguments,
  Ambiguous
};

template <typename ObjectMap>
static const Method* resolve_object_method(
    const ObjectMap& objects, const string& type, const string& name,
    const vector<string>& argument_types, bool unknown_argument_is_wildcard,
    MethodResolutionFailure* failure) {
  if (failure) *failure = MethodResolutionFailure::None;
  auto object = objects.find(canonical_type_name(type));
  if (object == objects.end()) {
    if (failure) *failure = MethodResolutionFailure::UnknownReceiver;
    return nullptr;
  }
  const Method* found = nullptr;
  bool saw_name = false;
  bool saw_arity = false;
  for (const auto& method : object->second->methods) {
    if (method.name != name) continue;
    saw_name = true;
    if (method.params.size() != argument_types.size()) continue;
    saw_arity = true;
    bool compatible = true;
    for (size_t index = 0; index < argument_types.size(); ++index) {
      if (argument_types[index].empty() && unknown_argument_is_wildcard) continue;
      if (argument_types[index].empty() || method.params[index].type.empty() ||
          canonical_type_name(method.params[index].type) !=
              canonical_type_name(argument_types[index])) {
        compatible = false;
        break;
      }
    }
    if (!compatible) continue;
    if (found) {
      if (failure) *failure = MethodResolutionFailure::Ambiguous;
      return nullptr;
    }
    found = &method;
  }
  if (!found && failure)
    *failure = !saw_name ? MethodResolutionFailure::MissingMethod
                         : !saw_arity ? MethodResolutionFailure::WrongArity
                                      : MethodResolutionFailure::IncompatibleArguments;
  return found;
}

static string functional_function_context(
    const Function& function,
    const FunctionSpecialization* specialization = nullptr) {
  if (!specialization) return "fn:" + function.name;
  std::ostringstream context;
  context << "fn:" << function.name << "<";
  for (size_t index = 0; index < specialization->parameter_types.size(); ++index) {
    if (index) context << ",";
    context << specialization->parameter_types[index];
  }
  context << ">";
  return context.str();
}

static string functional_method_context(const ObjectType& object,
                                        const Method& method) {
  return "method:" + object.name + "." + method.name;
}

static string functional_handler_context(const Domain& domain,
                                         const Handler& handler) {
  return "handler:" + domain.name + "." + handler.name;
}

class Checker {
 public:
  explicit Checker(Program& p) : p_(p) {
    for (auto& f : p_.functions) {
      if (!functions_.emplace(f.name, &f).second)
        err(f.line, "duplicate function: " + f.name);
    }
    for (auto& d : p_.domains) {
      if (!domains_.emplace(d.name, &d).second) err(d.line, "duplicate domain: " + d.name);
    }
    for (auto& o : p_.objects) {
      if (!objects_.emplace(o.name, &o).second) err(o.line, "duplicate object type: " + o.name);
    }
    for (auto& t : p_.traits) {
      if (!traits_.emplace(t.name, &t).second) err(t.line, "duplicate trait: " + t.name);
    }
  }

  void run() {
    // Seed internal structural/generic parameter relations before cross-reference inference.
    infer_function_signatures(false);
    infer_object_fields();
    check_traits();
    check_objects();
    // Function results and handler replies can constrain each other through an
    // await or a local call. Run a few non-final inference rounds before the
    // final unresolved-type diagnostics.
    for (size_t round = 0; round < 3; ++round) {
      infer_domain_state_fields(false);
      infer_function_signatures(false);
      infer_handler_reply_types(false);
    }
    infer_domain_state_fields(true);
    infer_handler_reply_types(true);
    infer_function_signatures(true);
    check_objects();
    check_local_call_cycles();
    infer_effects();
    infer_observable_effects();
    check_method_ownership();
    for (const auto& f : p_.functions) check_function(f);
    for (const auto& d : p_.domains) check_domain(d);
    if (p_.main) check_main(*p_.main);
    // Await boundedness is an ordinary well-formedness rule checked for every
    // executable body above. Graph construction below only records domain edges.
    check_global_await_cycles();
    build_functional_ir();
  }

  const vector<Warning>& warnings() const { return warnings_; }

 private:
  Program& p_;
  std::unordered_map<string, Function*> functions_;
  std::unordered_map<string, Domain*> domains_;
  std::unordered_map<string, ObjectType*> objects_;
  std::unordered_map<string, Trait*> traits_;
  const ObjectType* current_object_ = nullptr;
  vector<Warning> warnings_;

  using TypeEnv = std::unordered_map<string,string>;
  using TypeEnvVisitor = std::function<void(const Stmt&, const TypeEnv&)>;

  [[noreturn]] void err(int line, const string& msg) const { throw CompileError(line, msg); }

  bool valid_type(const string& t) const {
    string type = canonical_type_name(t);
    if (type == "int" || type == "float" || type == "bool" || type == "string" ||
        type == "unit") return true;
    if (domains_.count(type) || objects_.count(type)) return true;
    if (traits_.count(type) || type == "vector" || type == "map" || type == "queue") return true;
    if (starts_with(type, "vector[") && ends_with(type, "]")) return valid_type(trim(type.substr(7, type.size()-8)));
    if (starts_with(type, "queue[") && ends_with(type, "]")) return valid_type(trim(type.substr(6, type.size()-7)));
    if (starts_with(type, "map[") && ends_with(type, "]")) {
      auto ps = split_top_level(type.substr(4, type.size()-5), ',');
      return ps.size() == 2 && valid_type(ps[0]) && valid_type(ps[1]);
    }
    if ((starts_with(type, "seq[") || starts_with(type, "option[")) && ends_with(type, "]"))
      return valid_type(trim(type.substr(type.find('[')+1, type.size()-type.find('[')-2)));
    if (starts_with(type, "table[") && ends_with(type, "]")) {
      auto inside = type.substr(6, type.size()-7);
      auto ps = split_top_level(inside, ',');
      return ps.size() == 2 && valid_type(ps[0]) && valid_type(ps[1]);
    }
    return false;
  }

  bool has_constraint(const Function& f, const string& subject) const {
    return std::any_of(f.constraints.begin(), f.constraints.end(), [&](const Constraint& c) {
      if (c.subject == subject) return true;
      return c.kind == ConstraintKind::Method &&
          std::any_of(c.arguments.begin(), c.arguments.end(),
                      [&](const string& argument) {
                        return expression_uses(argument, subject);
                      });
    });
  }
  std::set<string> constraint_details(const Function& f, const string& subject) const {
    std::set<string> out;
    for (const auto& c : f.constraints) if (c.subject == subject && c.kind == ConstraintKind::Operator) out.insert(c.detail);
    for (const auto& c : f.constraints) if (c.subject == subject && c.kind == ConstraintKind::Indexable) out.insert("[]");
    return out;
  }

  bool has_structural_requirement(const Function& f, const string& subject, ConstraintKind kind, const string& detail) const {
    return std::any_of(f.constraints.begin(), f.constraints.end(), [&](const Constraint& c) {
      return c.subject == subject && c.kind == kind && c.detail == detail;
    });
  }

  bool type_has_field(const string& type, const string& field) const {
    auto it = objects_.find(type);
    if (it == objects_.end()) return false;
    return std::any_of(it->second->fields.begin(), it->second->fields.end(), [&](const Field& f) { return f.name == field; });
  }

  const Method* resolve_method(const string& type, const string& name,
                               const vector<string>& argument_types = {},
                               bool unknown_argument_is_wildcard = false,
                               MethodResolutionFailure* failure = nullptr) const {
    return resolve_object_method(objects_, type, name, argument_types,
                                 unknown_argument_is_wildcard, failure);
  }

  static string method_result_marker(size_t constraint_index) {
    return "_method_result:" + std::to_string(constraint_index);
  }

  std::optional<string> constraint_expression_type(const Function& function,
                                                   const string& expression) const {
    std::unordered_map<string,string> env;
    for (const auto& parameter : function.params)
      if (!parameter.type.empty() && !traits_.count(parameter.type))
        env[parameter.name] = parameter.type;
    auto type = obvious_expr_type(expression, env);
    if (!type || starts_with(*type, "_")) return std::nullopt;
    return canonical_type_name(*type);
  }

  size_t add_method_requirement(Function& function, const string& subject,
                                const string& method, const vector<string>& arguments,
                                const string& expected_result) {
    size_t index = 0;
    for (; index < function.constraints.size(); ++index) {
      const auto& candidate = function.constraints[index];
      if (candidate.kind == ConstraintKind::Method && candidate.subject == subject &&
          candidate.detail == method && candidate.arity == arguments.size() &&
          candidate.arguments == arguments)
        break;
    }
    if (index == function.constraints.size()) {
      Constraint requirement{ConstraintKind::Method, subject, method, ""};
      requirement.arity = arguments.size();
      requirement.arguments = arguments;
      function.constraints.push_back(std::move(requirement));
    }

    auto& requirement = function.constraints[index];
    if (expected_result == "$function_result") {
      requirement.result = method_result_marker(index);
      if (!function.return_type || starts_with(*function.return_type, "_method_result:"))
        function.return_type = requirement.result;
    } else if (!expected_result.empty() && !starts_with(expected_result, "_")) {
      string expected = canonical_type_name(expected_result);
      if (!requirement.result_expectations.empty() &&
          !same_type(requirement.result_expectations.front(), expected))
        err(function.line, "conflicting result expectations for required method '" +
            method + "': '" + requirement.result_expectations.front() +
            "' and '" + expected + "'");
      if (requirement.result_expectations.empty())
        requirement.result_expectations.push_back(expected);
    }
    return index;
  }

  void derive_expression_constraints(Function& function, const string& expression,
                                     const string& expected_result = "") {
    string original = trim(expression);
    if (auto pipeline = parse_functional_pipeline(original)) {
      for (const auto& parameter : function.params) {
        if (trim(pipeline->source) == parameter.name &&
            (parameter.type.empty() || parameter.type == "vector")) {
          if (!has_structural_requirement(function, parameter.name,
                                          ConstraintKind::Iterable,
                                          "functional source"))
            function.constraints.push_back({ConstraintKind::Iterable,
                                            parameter.name,
                                            "functional source", ""});
          function.generic = true;
          function.static_dispatch = true;
        }
      }
      derive_expression_constraints(function, pipeline->source);
      for (const auto& stage : pipeline->stages) {
        if (stage.kind == FunctionalNodeKind::Map ||
            stage.kind == FunctionalNodeKind::Filter ||
            stage.kind == FunctionalNodeKind::Any ||
            stage.kind == FunctionalNodeKind::All) {
          if (!stage.arguments.empty()) {
            string callable = trim(stage.arguments.front());
            for (const auto& parameter : function.params)
              if (callable == parameter.name && parameter.type.empty()) {
                if (!has_structural_requirement(function, parameter.name,
                                                ConstraintKind::Callable,
                                                "functional callable"))
                  function.constraints.push_back({ConstraintKind::Callable,
                                                  parameter.name,
                                                  "functional callable", ""});
                function.generic = true;
                function.static_dispatch = true;
              }
            derive_expression_constraints(function, callable);
          }
        } else if (stage.kind == FunctionalNodeKind::Reduce) {
          if (!stage.arguments.empty())
            derive_expression_constraints(function, stage.arguments.front());
          if (stage.arguments.size() == 2) {
            string callable = trim(stage.arguments[1]);
            for (const auto& parameter : function.params)
              if (callable == parameter.name && parameter.type.empty()) {
                if (!has_structural_requirement(function, parameter.name,
                                                ConstraintKind::Callable,
                                                "functional callable"))
                  function.constraints.push_back({ConstraintKind::Callable,
                                                  parameter.name,
                                                  "functional callable", ""});
                function.generic = true;
                function.static_dispatch = true;
              }
            derive_expression_constraints(function, callable);
          }
        }
      }
      if (expected_result == "$function_result" &&
          (!function.return_type || starts_with(*function.return_type, "_")))
        function.return_type = "_functional_result:" + function.name;
      return;
    }
    string e = normalize_pipeline(std::move(original));
    if (e.empty()) return;
    string receiver, method;
    vector<string> member_args;
    bool member_call = parse_member_call(e, receiver, method, member_args);
    for (const auto& parameter : function.params) {
      bool trait_receiver = !parameter.type.empty() && traits_.count(parameter.type);
      if (member_call && trim(receiver) == parameter.name &&
          (parameter.type.empty() || trait_receiver)) {
        add_method_requirement(function, parameter.name, method, member_args,
                               expected_result);
        function.static_dispatch = true;
        if (parameter.type.empty()) function.generic = true;
        for (const auto& related_parameter : function.params)
          if (related_parameter.type.empty() &&
              std::any_of(member_args.begin(), member_args.end(),
                          [&](const string& argument) {
                            return expression_uses(argument, related_parameter.name);
                          }))
            function.generic = true;

        const TraitMethod* trait_method = nullptr;
        if (trait_receiver) {
          const auto* trait = traits_.at(parameter.type);
          auto found = std::find_if(trait->methods.begin(), trait->methods.end(),
                                    [&](const TraitMethod& candidate) {
                                      return candidate.name == method &&
                                             candidate.params.size() == member_args.size();
                                    });
          if (found != trait->methods.end()) trait_method = &*found;
        }
        for (size_t index = 0; index < member_args.size(); ++index) {
          string expected;
          if (trait_method && !trait_method->params[index].type.empty())
            expected = trait_method->params[index].type;
          derive_expression_constraints(function, member_args[index], expected);
        }
      }
      if (!parameter.type.empty()) continue;
      auto dot = e.find('.');
      if (dot != string::npos && trim(e.substr(0, dot)) == parameter.name && e.find('(', dot) == string::npos) {
        string field = trim(e.substr(dot + 1));
        if (!field.empty() && !has_structural_requirement(function, parameter.name, ConstraintKind::Field, field))
          function.constraints.push_back({ConstraintKind::Field, parameter.name, field, ""});
      }
    }
    string ib, ii;
    if (parse_index(e, ib, ii)) {
      for (const auto& p : function.params) if (trim(ib) == p.name && p.type.empty()) {
        if (!has_structural_requirement(function, p.name, ConstraintKind::Indexable, "index"))
          function.constraints.push_back({ConstraintKind::Indexable, p.name, "index", ""});
        function.generic = true;
      }
      derive_expression_constraints(function, ii);
    }
    for (const auto& ops : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"}, {"+", "-", "*", "/"}})
      if (auto binary = split_binary(e, ops)) {
        auto left_type = constraint_expression_type(function, binary->first);
        auto right_type = constraint_expression_type(function, binary->second);
        string left_expected = right_type.value_or("");
        string right_expected = left_type.value_or("");
        bool comparison = ops.front() == "==";
        if (!comparison && !expected_result.empty() && expected_result != "$function_result") {
          if (left_expected.empty()) left_expected = expected_result;
          if (right_expected.empty()) right_expected = expected_result;
        }
        derive_expression_constraints(function, binary->first, left_expected);
        derive_expression_constraints(function, binary->second, right_expected);
      }
    string callee; vector<string> call_args;
    if (parse_simple_call(e, callee, call_args) && !member_call) {
      const Function* called = functions_.count(callee) ? functions_.at(callee) : nullptr;
      for (size_t index = 0; index < call_args.size(); ++index) {
        string expected;
        if (called && index < called->params.size() &&
            !called->params[index].type.empty() &&
            !traits_.count(called->params[index].type))
          expected = called->params[index].type;
        derive_expression_constraints(function, call_args[index], expected);
      }
    }
  }

  static bool numeric_type(const string& t) { return t == "int" || t == "float"; }
  bool trait_conforms(const string& type, const string& trait,
                      string* reason = nullptr) const {
    auto it = traits_.find(trait);
    if (it == traits_.end()) {
      if (reason) *reason = "unknown trait";
      return false;
    }
    auto object = objects_.find(type);
    if (object == objects_.end()) {
      if (reason) *reason = "only concrete object types can satisfy method traits";
      return false;
    }
    for (const auto& method : it->second->methods) {
      vector<string> args;
      for (const auto& parameter : method.params)
        args.push_back(canonical_type_name(parameter.type));
      MethodResolutionFailure failure = MethodResolutionFailure::None;
      auto found = resolve_method(type, method.name, args, true, &failure);
      if (!found) {
        if (reason) {
          if (failure == MethodResolutionFailure::MissingMethod)
            *reason = "missing required trait method '" + method.name + "'";
          else if (failure == MethodResolutionFailure::WrongArity)
            *reason = "trait method '" + method.name + "' has incompatible arity";
          else if (failure == MethodResolutionFailure::IncompatibleArguments)
            *reason = "trait method '" + method.name + "' has an incompatible parameter signature";
          else
            *reason = "trait method '" + method.name + "' is ambiguous";
        }
        return false;
      }
      if (method.return_type &&
          (!found->return_type || !same_type(*method.return_type, *found->return_type))) {
        if (reason)
          *reason = "trait method '" + method.name + "' has incompatible result type";
        return false;
      }
    }
    return true;
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

  Handler* find_handler(Domain& d, const string& name) {
    for (auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

  void check_traits() {
    for (const auto& trait : p_.traits) {
      std::set<string> signatures;
      for (const auto& method : trait.methods) {
        string signature = method.name + "/" + std::to_string(method.params.size());
        for (const auto& parameter : method.params) {
          if (!parameter.type.empty() && !valid_type(parameter.type))
            err(method.line, "unknown parameter type '" + parameter.type +
                "' in trait method '" + trait.name + "." + method.name + "'");
          signature += ":" + canonical_type_name(parameter.type);
        }
        if (!signatures.insert(signature).second)
          err(method.line, "duplicate trait method signature '" + trait.name + "." +
              method.name + "'");
        if (method.return_type && !valid_type(*method.return_type))
          err(method.line, "unknown result type '" + *method.return_type +
              "' in trait method '" + trait.name + "." + method.name + "'");
      }
    }
  }

  void check_objects() {
    for (const auto& object : p_.objects) {
      std::set<string> field_names;
      std::set<string> method_signatures;
      for (const auto& field : object.fields) {
        if (!valid_type(field.type)) err(field.line, "unknown field type '" + field.type + "'");
        if (!field_names.insert(field.name).second)
          err(field.line, "duplicate object field '" + field.name + "' in " + object.name);
      }
      current_object_ = &object;
      for (auto& method : const_cast<ObjectType&>(object).methods) {
        string signature = method.name + "/" + std::to_string(method.params.size());
        std::unordered_map<string,string> env;
        for (const auto& field : object.fields) env[field.name] = field.type;
        for (const auto& param : method.params) {
          if (param.type.empty())
            err(method.line, "cannot infer type for parameter '" + param.name +
                "' in method '" + object.name + "." + method.name + "'");
          if (!valid_type(param.type))
            err(method.line, "unknown parameter type '" + param.type +
                "' in method '" + object.name + "." + method.name + "'");
          signature += ":" + canonical_type_name(param.type);
          env[param.name] = param.type;
        }
        if (!method_signatures.insert(signature).second)
          err(method.line, "duplicate method signature '" + object.name + "." +
              method.name + "'");
        if (method.return_type && !valid_type(*method.return_type))
          err(method.line, "unknown result type '" + *method.return_type +
              "' in method '" + object.name + "." + method.name + "'");
        TypeEnv entry_env = env;
        TypeEnv inferred_env = env;
        infer_statement_expressions(method.body, inferred_env);
        for (const auto& s : method.body) {
          if (s.kind == Stmt::Kind::Return && !s.a.empty()) {
            auto result = inferred_expr_type(s.a, inferred_env);
            if (result) {
              if (!method.return_type) method.return_type = *result;
              else if (!same_type(*method.return_type, *result))
                err(s.line, "method '" + object.name + "." + method.name + "' returns '" + *result +
                    "' but another return path has type '" + *method.return_type + "'");
            }
          }
        }
        if (method.result_expression) {
          auto result = inferred_expr_type(*method.result_expression, inferred_env);
          if (result) {
            if (!method.return_type) method.return_type = *result;
            else if (!same_type(*method.return_type, *result))
              err(method.result_line ? method.result_line : method.line,
                  "method '" + object.name + "." + method.name + "' returns '" + *result +
                  "' but is annotated/inferred as '" + *method.return_type + "'");
          }
        }
        bool has_value_return = method.result_expression.has_value() ||
            std::any_of(method.body.begin(), method.body.end(), [](const Stmt& statement) {
              return statement.kind == Stmt::Kind::Return && !statement.a.empty();
            });
        if (!method.return_type && !has_value_return) method.return_type = "unit";
        Function method_function; method_function.name = object.name + "." + method.name; method_function.params = method.params; method_function.return_type = method.return_type;
        TypeEnv final_env = check_stmts(method.body, std::move(entry_env), nullptr,
                                        nullptr, &method_function);
        if (method.result_expression)
          check_expression(method.result_line ? method.result_line : method.line,
                           *method.result_expression, final_env);
        if (!method.return_type && has_value_return)
          err(method.line, "cannot infer return type for method '" + object.name + "." + method.name + "'");
      }
      current_object_ = nullptr;
    }
  }

  void check_domain(const Domain& d) {
    std::set<string> state_names, handler_names;
    for (const auto& f : d.state) {
      if (!valid_type(f.type)) err(f.line, "unknown state type '" + f.type + "'");
      if (!state_names.insert(f.name).second) err(f.line, "duplicate state field '" + f.name + "'");
      // Actor handles may exist as state; they still expose only message sends.
    }
    for (const auto& h : d.handlers) {
      if (!handler_names.insert(h.name).second) err(h.line, "duplicate handler '" + h.name + "' in domain " + d.name);
      if (h.reply_type && !valid_type(*h.reply_type)) err(h.line, "unknown reply type '" + *h.reply_type + "'");
      bool has_reply = std::any_of(h.body.begin(), h.body.end(), [](const Stmt& s) {
        return s.kind == Stmt::Kind::Reply;
      });
      if (has_reply && !h.reply_type)
        err(h.line, "cannot infer reply type for handler '" + d.name + "." + h.name +
            "'; add an annotation or use a statically typed reply expression");
      if (h.reply_type && !has_reply)
        err(h.line, "reply handler '" + d.name + "." + h.name + "' must contain at least one reply statement");
      std::unordered_map<string,string> env;
      env["self"] = d.name;
      for (const auto& p : h.params) {
        if (p.type.empty())
          err(h.line, "cannot infer type for parameter '" + p.name +
              "' in handler '" + d.name + "." + h.name + "'");
        if (!valid_type(p.type)) err(h.line, "unknown parameter type '" + p.type + "'");
        if (env.count(p.name)) err(h.line, "duplicate parameter '" + p.name + "'");
        env[p.name] = p.type;
      }
      for (const auto& f : d.state) env[f.name] = f.type;
      check_stmts(h.body, env, &d, &h);
      OwnershipEnv ownership;
      ownership.types = env;
      for (const auto& f : d.state) ownership.state_fields.insert(f.name);
      check_ownership(h.body, std::move(ownership), &d, &h);
    }
  }

  void check_main(const MainProc& m) {
    std::unordered_map<string,string> env;
    check_stmts(m.body, env, nullptr, nullptr);
    OwnershipEnv ownership;
    ownership.types = std::move(env);
    check_ownership(m.body, std::move(ownership), nullptr, nullptr);
  }

  void check_function(const Function& f) {
    std::unordered_map<string,string> env;
    std::set<string> names;
    if (f.return_type && *f.return_type != "unit" && !valid_type(*f.return_type) && !starts_with(*f.return_type, "_"))
      err(f.line, "unknown return type '" + *f.return_type + "' in function '" + f.name + "'");
    for (const auto& param : f.params) {
      if (param.type.empty() && !f.generic && !has_constraint(f, param.name))
        err(f.line, "cannot infer type for parameter '" + param.name +
            "' in function '" + f.name + "'");
      if (!param.type.empty() && !valid_type(param.type) && !starts_with(param.type, "_")) err(f.line, "unknown parameter type '" + param.type + "'");
      if (!names.insert(param.name).second) err(f.line, "duplicate parameter: " + param.name);
      env[param.name] = param.type.empty() ? "_generic:" + param.name : param.type;
    }
    TypeEnv entry_env = env;
    TypeEnv inferred_env = env;
    infer_statement_expressions(f.body, inferred_env);
    env = check_stmts(f.body, entry_env, nullptr, nullptr, &f);
    OwnershipEnv ownership;
    ownership.types = std::move(entry_env);
    OwnershipEnv final_ownership = check_ownership(
        f.body, std::move(ownership), nullptr, nullptr);
    if (f.result_expression) {
      check_ownership_expression(f.result_line ? f.result_line : f.line,
                                 *f.result_expression, final_ownership,
                                 Effect::Consume);
      check_expression(f.result_line ? f.result_line : f.line, *f.result_expression, env);
      auto actual = inferred_expr_type(*f.result_expression, env);
      if (!actual)
        err(f.result_line ? f.result_line : f.line,
            "cannot infer the result type of function '" + f.name + "'");
      if (!f.return_type) {
        // The signature pass normally fills this in. Keep this assignment as a
        // defensive fallback for a function whose result was inferred late.
        const_cast<Function&>(f).return_type = *actual;
      } else if (!starts_with(*f.return_type, "_") &&
                 !starts_with(*actual, "_method_") &&
                 canonical_type_name(*f.return_type) != canonical_type_name(*actual)) {
        err(f.result_line ? f.result_line : f.line,
            "function '" + f.name + "' returns '" + *actual +
            "' but is annotated '" + *f.return_type + "'");
      }
    } else if (!f.return_type) {
      const_cast<Function&>(f).return_type = "unit";
    }
  }

  static std::optional<string> spawn_domain(const string& expr) {
    string e = trim(expr);
    if (!starts_with(e, "spawn ") || !ends_with(e, "()")) return std::nullopt;
    string name = trim(e.substr(6, e.size() - 8));
    if (name.empty()) return std::nullopt;
    return name;
  }

  const Handler* check_call(int line, const string& receiver, const string& message,
                            const vector<string>& args,
                            const std::unordered_map<string,string>& env) const {
    auto it = env.find(receiver);
    if (it == env.end()) err(line, "unknown message receiver '" + receiver + "'");
    auto dit = domains_.find(it->second);
    if (dit == domains_.end())
      err(line, "'" + receiver + "' is not a domain reference; use a local function call or a domain reference");
    const Handler* h = find_handler(*dit->second, message);
    if (!h) err(line, "domain " + dit->second->name + " has no message handler '" + message + "'");
    if (h->params.size() != args.size())
      err(line, "message " + dit->second->name + "." + message + " expects " +
          std::to_string(h->params.size()) + " arguments, got " + std::to_string(args.size()));
    for (size_t index = 0; index < args.size(); ++index) {
      if (auto pipeline = parse_functional_pipeline(args[index]);
          pipeline && functional_pipeline_requires_materialization(*pipeline))
        err(line, "functional pipeline must be materialized in a local binding "
                  "before crossing a domain boundary");
      auto actual = inferred_expr_type(args[index], env);
      if (actual && !same_type(h->params[index].type, *actual))
        err(line, "argument " + std::to_string(index + 1) + " to message " +
            dit->second->name + "." + message + " has type '" + *actual +
            "', expected '" + h->params[index].type + "'");
    }
    return h;
  }

  static std::optional<string> obvious_expr_type(const string& expression,
                                                  const std::unordered_map<string,string>& env) {
    string e = trim(expression);
    if (e == "true" || e == "false") return "bool";
    if (e.size() >= 2 && e.front() == '"' && e.back() == '"') return "string";
    auto local = env.find(e);
    if (local != env.end() && !local->second.empty() && local->second != "_value") return local->second;
    size_t start = (!e.empty() && (e.front() == '+' || e.front() == '-')) ? 1 : 0;
    if (start < e.size() && std::all_of(e.begin() + static_cast<std::ptrdiff_t>(start), e.end(), [](char c) {
          return std::isdigit(static_cast<unsigned char>(c));
        })) return "int";
    bool dot = false, digit = start < e.size();
    for (size_t i = start; digit && i < e.size(); ++i) {
      if (e[i] == '.' && !dot) dot = true;
      else if (!std::isdigit(static_cast<unsigned char>(e[i]))) digit = false;
    }
    if (digit && dot) return "float";
    return std::nullopt;
  }

  static bool unresolved_semantic_type(const string& type) {
    return type.empty() || starts_with(type, "_") || type == "vector" ||
        type == "queue" || type == "map";
  }

  static std::optional<string> functional_element_type(const string& type,
                                                        const string& source) {
    string value = canonical_type_name(type);
    if (starts_with(value, "vector[") && ends_with(value, "]"))
      return trim(value.substr(7, value.size() - 8));
    if (starts_with(value, "seq[") && ends_with(value, "]"))
      return trim(value.substr(4, value.size() - 5));
    if (value == "vector" || starts_with(value, "_generic:"))
      return "_functional_element:" + trim(source);
    return std::nullopt;
  }

  std::optional<string> functional_callable_result(
      const string& callable, const vector<string>& input_types,
      const TypeEnv& env) const {
    string value = trim(callable);
    if (value.empty()) return std::nullopt;

    // `_` is an expression-local placeholder.  It never becomes a runtime
    // callable object; ordinary expression inference runs with a concrete
    // synthetic binding for the current element.
    if (expression_uses(value, "_")) {
      if (input_types.size() != 1) return std::nullopt;
      TypeEnv placeholder_env = env;
      placeholder_env["_"] = input_types.front();
      return inferred_expr_type(value, placeholder_env);
    }

    string bound_receiver, bound_method;
    if (parse_bound_method_callable(value, bound_receiver, bound_method)) {
      auto receiver_type = inferred_expr_type(bound_receiver, env);
      if (!receiver_type) return std::nullopt;
      const Method* method = resolve_method(
          canonical_type_name(*receiver_type), bound_method, input_types,
          false, nullptr);
      if (!method || !method->return_type) return std::nullopt;
      return canonical_type_name(*method->return_type);
    }

    auto local = env.find(value);
    if (local != env.end() && starts_with(local->second, "callable:"))
      value = local->second.substr(9);
    else if (local != env.end() && starts_with(local->second, "_generic:"))
      return "_functional_callable_result:" + value;
    auto function = functions_.find(value);
    if (function == functions_.end() ||
        function->second->params.size() != input_types.size())
      return std::nullopt;
    for (size_t index = 0; index < input_types.size(); ++index) {
      const string& expected = function->second->params[index].type;
      if (!expected.empty() && !traits_.count(expected) &&
          !unresolved_semantic_type(input_types[index]) &&
          !same_type(expected, input_types[index]))
        return std::nullopt;
    }
    if (!function->second->return_type) return std::nullopt;
    string result = canonical_type_name(*function->second->return_type);
    if (starts_with(result, "_") || traits_.count(result))
      return specialized_function_return_type(*function->second, input_types);
    return result;
  }

  std::optional<string> inferred_functional_pipeline_type(
      const string& expression, const TypeEnv& env) const {
    auto parsed = parse_functional_pipeline(expression);
    if (!parsed) return std::nullopt;
    auto source_type = inferred_expr_type(parsed->source, env);
    if (!source_type) return std::nullopt;
    auto element = functional_element_type(*source_type, parsed->source);
    if (!element) return std::nullopt;

    string current_element = *element;
    string output = starts_with(*source_type, "vector[")
        ? *source_type : "_functional_collection:" + current_element;
    bool terminal = false;
    for (const auto& stage : parsed->stages) {
      if (terminal) return std::nullopt;
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          if (stage.arguments.size() != 1) return std::nullopt;
          auto result = functional_callable_result(stage.arguments.front(),
                                                   {current_element}, env);
          if (!result) return std::nullopt;
          current_element = canonical_type_name(*result);
          output = unresolved_semantic_type(current_element)
              ? "_functional_collection:" + current_element
              : "vector[" + current_element + "]";
          break;
        }
        case FunctionalNodeKind::Filter: {
          if (stage.arguments.size() != 1) return std::nullopt;
          auto result = functional_callable_result(stage.arguments.front(),
                                                   {current_element}, env);
          if (!result || (!unresolved_semantic_type(*result) &&
                          canonical_type_name(*result) != "bool"))
            return std::nullopt;
          output = unresolved_semantic_type(current_element)
              ? "_functional_collection:" + current_element
              : "vector[" + current_element + "]";
          break;
        }
        case FunctionalNodeKind::Reduce: {
          if (stage.arguments.size() != 2) return std::nullopt;
          auto accumulator = inferred_expr_type(stage.arguments.front(), env);
          if (!accumulator) return std::nullopt;
          auto result = functional_callable_result(stage.arguments[1],
                                                   {*accumulator, current_element}, env);
          if (!result || (!unresolved_semantic_type(*result) &&
                          !unresolved_semantic_type(*accumulator) &&
                          !same_type(*result, *accumulator)))
            return std::nullopt;
          output = canonical_type_name(*accumulator);
          terminal = true;
          break;
        }
        case FunctionalNodeKind::Sum:
          if (!stage.arguments.empty() ||
              (!unresolved_semantic_type(current_element) &&
               !numeric_type(current_element)))
            return std::nullopt;
          output = current_element;
          terminal = true;
          break;
        case FunctionalNodeKind::Count:
          if (!stage.arguments.empty()) return std::nullopt;
          output = "int";
          terminal = true;
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All: {
          if (stage.arguments.size() > 1) return std::nullopt;
          string predicate_type = current_element;
          if (!stage.arguments.empty()) {
            auto result = functional_callable_result(stage.arguments.front(),
                                                     {current_element}, env);
            if (!result) return std::nullopt;
            predicate_type = *result;
          }
          if (!unresolved_semantic_type(predicate_type) &&
              canonical_type_name(predicate_type) != "bool")
            return std::nullopt;
          output = "bool";
          terminal = true;
          break;
        }
        case FunctionalNodeKind::Source:
          return std::nullopt;
      }
    }
    return output;
  }

  struct MoveInfo {
    int line = 0;
    string destination;
  };

  struct OwnershipEnv {
    std::unordered_map<string,string> types;
    std::set<string> state_fields;
    std::map<string,MoveInfo> moved;
  };

  static bool simple_identifier(const string& value) {
    string s = trim(value);
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
  }

  static bool expression_uses(const string& expression, const string& name) {
    bool in_string = false, escaped = false;
    for (size_t i = 0; i < expression.size();) {
      char c = expression[i];
      if (in_string) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') in_string = false;
        ++i;
        continue;
      }
      if (c == '"') { in_string = true; ++i; continue; }
      if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) { ++i; continue; }
      size_t end = i + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_')) ++end;
      if (expression.compare(i, end - i, name) == 0) {
        size_t before = i;
        while (before > 0 && std::isspace(static_cast<unsigned char>(expression[before - 1]))) --before;
        bool member_name = before > 0 && expression[before - 1] == '.';
        size_t after = end;
        while (after < expression.size() && std::isspace(static_cast<unsigned char>(expression[after]))) ++after;
        bool named_field = after < expression.size() && expression[after] == ':';
        if (!named_field && after < expression.size() && expression[after] == '=') {
          named_field = after + 1 == expression.size() || expression[after + 1] != '=';
        }
        if (!member_name && !named_field) return true;
      }
      i = end;
    }
    return false;
  }

  bool copy_type(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int" || t == "float" || t == "bool") return true;
    if (starts_with(t, "option[") && ends_with(t, "]"))
      return copy_type(trim(t.substr(7, t.size() - 8)));
    return false;
  }

  bool transfer_type(const string& type) const {
    return !copy_type(type) && !domains_.count(canonical_type_name(type));
  }

  static std::optional<std::pair<string,string>> split_binary(const string& expression,
                                                               const vector<string>& operators) {
    int par = 0, br = 0, sq = 0;
    bool in_str = false;
    for (size_t i = expression.size(); i-- > 0;) {
      char c = expression[i];
      if (in_str) {
        if (c == '"' && (i == 0 || expression[i - 1] != '\\')) in_str = false;
        continue;
      }
      if (c == '"') { in_str = true; continue; }
      if (c == ')') ++par; else if (c == '(') --par;
      else if (c == '}') ++br; else if (c == '{') --br;
      else if (c == ']') ++sq; else if (c == '[') --sq;
      if (par != 0 || br != 0 || sq != 0) continue;
      for (const auto& op : operators) {
        if (i + op.size() <= expression.size() &&
            expression.compare(i, op.size(), op) == 0)
          return std::make_pair(trim(expression.substr(0, i)),
                                trim(expression.substr(i + op.size())));
      }
    }
    return std::nullopt;
  }

  std::optional<string> inferred_expr_type(const string& expression,
                                           const std::unordered_map<string,string>& env) const {
    string original = trim(expression);
    if (auto functional = inferred_functional_pipeline_type(original, env))
      return functional;
    string e = normalize_pipeline(std::move(original));
    while (e.size() >= 2 && e.front() == '(' && e.back() == ')') {
      int depth = 0;
      bool wraps = true;
      for (size_t i = 0; i < e.size(); ++i) {
        if (e[i] == '(') ++depth;
        else if (e[i] == ')' && --depth == 0 && i + 1 != e.size()) { wraps = false; break; }
      }
      if (!wraps) break;
      e = trim(e.substr(1, e.size() - 2));
    }
    if (auto type = obvious_expr_type(e, env)) return canonical_type_name(*type);
    if (plain_identifier(e) && functions_.count(e))
      return "callable:" + e;
    if (e == "Map()") return string("map");
    if (e == "Queue()") return string("queue");
    if (e.size() >= 2 && e.front() == '[' && e.back() == ']') {
      auto parts = split_top_level(e.substr(1, e.size() - 2), ',');
      if (parts.size() == 1 && trim(parts[0]).empty()) return std::nullopt;
      string element;
      for (const auto& part : parts) {
        auto t = inferred_expr_type(part, env);
        if (!t) return std::nullopt;
        if (element.empty()) element = *t;
        else if (canonical_type_name(element) != canonical_type_name(*t)) return std::nullopt;
      }
      return "vector[" + element + "]";
    }
    string index_base, index_expr;
    if (parse_index(e, index_base, index_expr)) {
      auto bt = inferred_expr_type(index_base, env);
      if (!bt) return std::nullopt;
      if (starts_with(*bt, "_generic:")) return string("_element:element:" + bt->substr(9));
      if (*bt == "vector" || *bt == "queue") return string("_element:element:" + index_base);
      if (starts_with(*bt, "vector[") && ends_with(*bt, "]")) return trim(bt->substr(7, bt->size()-8));
      if (starts_with(*bt, "queue[") && ends_with(*bt, "]")) return trim(bt->substr(6, bt->size()-7));
      if (starts_with(*bt, "map[") && ends_with(*bt, "]")) {
        auto ps = split_top_level(bt->substr(4, bt->size()-5), ',');
        if (ps.size() == 2) return trim(ps[1]);
      }
      return std::nullopt;
    }
    auto object_call = [&]() -> std::optional<string> {
      string callee;
      vector<string> args;
      if (!parse_simple_call(e, callee, args)) return std::nullopt;
      if (objects_.count(callee)) return callee;
      if (callee == "sqrt") return string("float");
      if (callee == "sum" && args.size() == 1) {
        auto argument_type = inferred_expr_type(args.front(), env);
        if (argument_type && starts_with(*argument_type, "seq[") && ends_with(*argument_type, "]"))
          return trim(argument_type->substr(4, argument_type->size() - 5));
        if (argument_type && starts_with(*argument_type, "vector[") && ends_with(*argument_type, "]"))
          return trim(argument_type->substr(7, argument_type->size() - 8));
        return argument_type;
      }
      auto function = functions_.find(callee);
      if (function == functions_.end() && current_object_) {
        auto method = resolve_method(current_object_->name, callee, {});
        if (method && method->return_type) return *method->return_type;
      }
      if (function != functions_.end() && function->second->return_type) {
        string r = canonical_type_name(*function->second->return_type);
        if (function->second->static_dispatch) {
          vector<string> actual_types;
          for (const auto& arg : args)
            actual_types.push_back(inferred_expr_type(arg, env).value_or(""));
          bool concrete = actual_types.size() == function->second->params.size() &&
              std::all_of(actual_types.begin(), actual_types.end(),
                          [](const string& type) {
                            return !type.empty() && !starts_with(type, "_");
                          });
          if (concrete) {
            auto specialized = specialized_function_return_type(
                *function->second, actual_types);
            if (specialized) return specialized;
          }
        }
        if (starts_with(r, "_method_result:")) {
          vector<string> actual_types;
          for (const auto& arg : args)
            actual_types.push_back(inferred_expr_type(arg, env).value_or(""));
          return specialized_function_return_type(*function->second, actual_types);
        }
        if (starts_with(r, "_generic:")) {
          auto relation = function->second->generic_results.find(r.substr(9));
          if (relation == function->second->generic_results.end()) return std::nullopt;
          auto it = env.find(relation->second);
          if (it != env.end()) return it->second;
        }
        if (starts_with(r, "_element:")) {
          auto relation = function->second->generic_results.find(r.substr(9));
          if (relation == function->second->generic_results.end()) return std::nullopt;
          auto it = env.find(relation->second);
          if (it != env.end() && starts_with(it->second, "vector[") && ends_with(it->second, "]"))
            return trim(it->second.substr(7, it->second.size()-8));
        }
        if (starts_with(r, "_field:")) {
          auto key = r.substr(7); auto pos = key.find(':');
          if (pos != string::npos) pos = key.find(':', pos + 1);
          string source = pos == string::npos ? key : key.substr(0, pos);
          for (size_t ai = 0; ai < args.size() && ai < function->second->params.size(); ++ai)
            if (function->second->params[ai].name == source) {
              auto at = inferred_expr_type(args[ai], env);
              if (at) {
                auto object = objects_.find(*at);
                if (object != objects_.end() && pos != string::npos) for (const auto& f : object->second->fields)
                  if (f.name == key.substr(pos + 1)) return f.type;
              }
            }
          auto relation = function->second->generic_results.find(key);
          if (relation != function->second->generic_results.end()) {
            auto base = env.find(relation->second);
            if (base != env.end()) {
              auto object = objects_.find(base->second);
              if (object != objects_.end() && pos != string::npos) for (const auto& f : object->second->fields)
                if (f.name == key.substr(pos + 1)) return f.type;
            }
          }
        }
        return r;
      }
      return std::nullopt;
    }();
    if (object_call) return object_call;
    string method_receiver, method_name; vector<string> method_args;
    if (parse_member_call(e, method_receiver, method_name, method_args)) {
      auto base = inferred_expr_type(method_receiver, env);
      if (base) {
        vector<string> argument_types;
        for (const auto& arg : method_args) argument_types.push_back(inferred_expr_type(arg, env).value_or(""));
        if (auto method = resolve_method(canonical_type_name(*base), method_name, argument_types))
          if (method->return_type) return *method->return_type;
        if (starts_with(*base, "_generic:")) return string("_method_value");
        auto trait = traits_.find(canonical_type_name(*base));
        if (trait != traits_.end()) {
          auto declared = std::find_if(trait->second->methods.begin(), trait->second->methods.end(),
                                       [&](const TraitMethod& candidate) {
                                         return candidate.name == method_name &&
                                                candidate.params.size() == method_args.size();
                                       });
          if (declared != trait->second->methods.end())
            return declared->return_type.value_or("_method_value");
        }
      }
    }

    auto dot = e.rfind('.');
    if (dot != string::npos) {
      auto base_type = inferred_expr_type(e.substr(0, dot), env);
      string field = trim(e.substr(dot + 1));
      if (base_type) {
        auto object = objects_.find(canonical_type_name(*base_type));
        if (object != objects_.end()) {
          for (const auto& candidate : object->second->fields)
            if (candidate.name == field && !candidate.type.empty())
              return canonical_type_name(candidate.type);
        }
      }
    }

    string mr, mh; vector<string> ma;
    if (parse_member_call(e, mr, mh, ma)) {
      auto bt = env.find(mr);
      if (bt != env.end()) {
        string t = bt->second;
        if (mh == "pop" && ma.empty() && starts_with(t, "queue[") && ends_with(t, "]"))
          return trim(t.substr(6, t.size()-7));
        if (mh == "pop" && ma.empty() && starts_with(t, "vector[") && ends_with(t, "]"))
          return trim(t.substr(7, t.size()-8));
      }
    }

    if (auto comparison = split_binary(e, {"==", "!=", "<=", ">=", "<", ">"}))
      return string("bool");
    if (auto arithmetic = split_binary(e, {"+", "-", "*", "/"})) {
      auto left = inferred_expr_type(arithmetic->first, env);
      auto right = inferred_expr_type(arithmetic->second, env);
      if (left && right) {
        if (left == right && starts_with(*left, "_generic:")) return *left;
        if (*left == "string" && *right == "string" && e.find('+') != string::npos)
          return string("string");
        if ((*left == "int" || *left == "float") &&
            (*right == "int" || *right == "float"))
          return (*left == "float" || *right == "float") ? "float" : "int";
      }
    }
    return std::nullopt;
  }

  static bool same_type(const string& left, const string& right) {
    return canonical_type_name(left) == canonical_type_name(right);
  }

  std::unordered_map<string,string> instantiated_function_env(
      const Function& function, const vector<string>& parameter_types) const {
    std::unordered_map<string,string> env;
    for (size_t index = 0;
         index < function.params.size() && index < parameter_types.size(); ++index)
      env[function.params[index].name] = canonical_type_name(parameter_types[index]);
    for (const auto& statement : function.body) {
      if (statement.kind != Stmt::Kind::Assign && statement.kind != Stmt::Kind::Let &&
          statement.kind != Stmt::Kind::Var)
        continue;
      if (auto type = inferred_expr_type(statement.b, env))
        env[statement.a] = canonical_type_name(*type);
    }
    return env;
  }

  const Method* resolve_method_requirement(const Function& function,
                                           const Constraint& requirement,
                                           const vector<string>& parameter_types,
                                           MethodResolutionFailure* failure = nullptr) const {
    auto env = instantiated_function_env(function, parameter_types);
    auto receiver = env.find(requirement.subject);
    if (receiver == env.end()) {
      if (failure) *failure = MethodResolutionFailure::UnknownReceiver;
      return nullptr;
    }
    vector<string> argument_types;
    for (const auto& argument : requirement.arguments)
      argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
    return resolve_method(canonical_type_name(receiver->second), requirement.detail,
                          argument_types, false, failure);
  }

  std::optional<string> specialized_function_return_type(
      const Function& function, const vector<string>& parameter_types) const {
    if (!function.return_type) return std::nullopt;
    string result = canonical_type_name(*function.return_type);
    bool requires_concrete_result = starts_with(result, "_") || traits_.count(result);
    if (requires_concrete_result) {
      auto env = instantiated_function_env(function, parameter_types);
      std::optional<string> inferred_result;
      auto include_result = [&](const string& expression) {
        auto actual = inferred_expr_type(expression, env);
        if (!actual || starts_with(*actual, "_")) return true;
        string concrete = canonical_type_name(*actual);
        if (inferred_result && !same_type(*inferred_result, concrete)) return false;
        inferred_result = concrete;
        return true;
      };
      if (function.result_expression && !include_result(*function.result_expression))
        return std::nullopt;
      for (const auto& statement : function.body)
        if (statement.kind == Stmt::Kind::Return && !statement.a.empty() &&
            !include_result(statement.a))
          return std::nullopt;
      if (inferred_result) return inferred_result;
    }
    if (starts_with(result, "_generic:")) {
      string parameter = result.substr(9);
      for (size_t index = 0;
           index < function.params.size() && index < parameter_types.size(); ++index)
        if (function.params[index].name == parameter)
          return canonical_type_name(parameter_types[index]);
      return std::nullopt;
    }
    if (traits_.count(result)) {
      for (size_t index = 0;
           index < function.params.size() && index < parameter_types.size(); ++index)
        if (canonical_type_name(function.params[index].type) == result)
          return canonical_type_name(parameter_types[index]);
      return std::nullopt;
    }
    if (!starts_with(result, "_method_result:")) return result;
    string suffix = result.substr(15);
    if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), [](char ch) {
          return std::isdigit(static_cast<unsigned char>(ch));
        }))
      return std::nullopt;
    size_t index = static_cast<size_t>(std::stoull(suffix));
    if (index >= function.constraints.size()) return std::nullopt;
    auto method = resolve_method_requirement(function, function.constraints[index],
                                             parameter_types);
    if (!method || !method->return_type) return std::nullopt;
    return canonical_type_name(*method->return_type);
  }

  void constrain_constructor_fields(int line, const string& expression,
                                    const std::unordered_map<string,string>& env) {
    string normalized = normalize_pipeline(trim(expression));
    string constructor;
    vector<string> args;
    if (!parse_simple_call(normalized, constructor, args)) {
      for (const auto& operators : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"},
                                                           {"+", "-", "*", "/"}}) {
        if (auto binary = split_binary(normalized, operators)) {
          constrain_constructor_fields(line, binary->first, env);
          constrain_constructor_fields(line, binary->second, env);
          return;
        }
      }
      return;
    }
    auto object = objects_.find(constructor);
    if (object == objects_.end()) {
      for (size_t index = 0; index < args.size(); ++index) {
        constrain_constructor_fields(line, args[index], env);
        auto function = functions_.find(constructor);
        if (function != functions_.end() && index < function->second->params.size()) {
          if (auto actual = inferred_expr_type(args[index], env)) {
            auto& parameter = function->second->params[index];
            if (parameter.type.empty() && !function->second->generic &&
                !function->second->static_dispatch)
              parameter.type = *actual;
            else if (!function->second->generic && !function->second->static_dispatch &&
                     !traits_.count(parameter.type) && !same_type(parameter.type, *actual))
              err(line, "argument " + std::to_string(index + 1) + " to function '" +
                  constructor + "' has type '" + *actual + "', expected '" +
                  parameter.type + "'");
          }
        }
      }
      return;
    }
    std::unordered_map<string,string> supplied;
    for (const auto& arg : args) {
      string field_name, field_value;
      if (!parse_named_argument(arg, field_name, field_value))
        err(line, "object constructor fields must be named for '" + constructor + "'");
      if (!supplied.emplace(field_name, field_value).second)
        err(line, "duplicate field '" + field_name + "' in " + constructor + " constructor");
    }
    for (const auto& field_arg : supplied) {
      auto field = std::find_if(object->second->fields.begin(), object->second->fields.end(),
                                [&](const Field& candidate) { return candidate.name == field_arg.first; });
      if (field == object->second->fields.end())
        err(line, "unknown field '" + field_arg.first + "' in " + constructor + " constructor");
      constrain_constructor_fields(line, field_arg.second, env);
      auto actual = inferred_expr_type(field_arg.second, env);
      if (!actual) continue;
      string inferred = canonical_type_name(*actual);
      if (field->type.empty()) field->type = inferred;
      else if (!same_type(field->type, inferred))
        err(line, "field '" + constructor + "." + field->name + "' has conflicting inferred types '" +
            field->type + "' and '" + inferred + "'");
    }
  }

  static bool concrete_environment_type(const string& type) {
    return !type.empty() && !starts_with(type, "_");
  }

  // This is the common control-flow join for inference, ordinary checking,
  // local-call discovery, ownership type state, and await-edge discovery. A
  // later pass must not recover a branch type that this join discarded.
  TypeEnv merge_type_environments(const vector<TypeEnv>& paths, int line,
                                  bool reject_conflicts) const {
    if (paths.empty()) return {};
    TypeEnv merged;
    for (const auto& entry : paths.front()) {
      const string& binding = entry.first;
      string agreed = entry.second;
      bool present_everywhere = true;
      bool disagrees = false;
      for (size_t index = 1; index < paths.size(); ++index) {
        auto found = paths[index].find(binding);
        if (found == paths[index].end()) {
          present_everywhere = false;
          break;
        }
        if (!same_type(agreed, found->second)) {
          if (reject_conflicts && concrete_environment_type(agreed) &&
              concrete_environment_type(found->second)) {
            string left = canonical_type_name(agreed);
            string right = canonical_type_name(found->second);
            if (domains_.count(left) && domains_.count(right))
              err(line, "binding '" + binding +
                  "' has conflicting domain types across control-flow paths: " +
                  left + " and " + right);
            err(line, "binding '" + binding +
                "' has conflicting concrete types across control-flow paths: " +
                left + " and " + right);
          }
          disagrees = true;
        }
      }
      if (present_everywhere)
        merged[binding] = disagrees ? "_value" : canonical_type_name(agreed);
    }
    return merged;
  }

  void advance_type_environment(const Stmt& statement, TypeEnv& env,
                                const vector<Stmt>& statements,
                                bool record_semantic_types) const {
    if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
        statement.kind == Stmt::Kind::Assign) {
      if (statement.kind != Stmt::Kind::Assign || simple_identifier(statement.a)) {
        auto existing = env.find(statement.a);
        if (auto spawned = spawn_domain(statement.b)) {
          env[statement.a] = *spawned;
        } else if (auto inferred = inferred_expr_type(statement.b, env)) {
          env[statement.a] = canonical_type_name(*inferred);
        } else if (statement.kind != Stmt::Kind::Assign || existing == env.end()) {
          env[statement.a] = "_value";
        }
        if (record_semantic_types)
          const_cast<Stmt&>(statement).semantic_type = env[statement.a];
      } else {
        string base, index_expression;
        if (parse_index(statement.a, base, index_expression)) {
          auto container = env.find(base);
          auto value_type = inferred_expr_type(statement.b, env);
          if (container != env.end() && value_type && container->second == "map") {
            auto key_type = inferred_expr_type(index_expression, env);
            if (key_type) {
              container->second = "map[" + *key_type + "," + *value_type + "]";
              if (record_semantic_types) {
                for (auto& prior : const_cast<vector<Stmt>&>(statements))
                  if (prior.a == base && prior.b == "Map()")
                    prior.semantic_type = container->second;
              }
            }
          } else if (container != env.end() && value_type &&
                     (container->second == "queue" || container->second == "vector")) {
            container->second += "[" + *value_type + "]";
          }
        }
      }
      return;
    }

    if (statement.kind == Stmt::Kind::AwaitMessage) {
      auto receiver = env.find(statement.b);
      if (receiver != env.end()) {
        auto domain = domains_.find(canonical_type_name(receiver->second));
        if (domain != domains_.end())
          if (const Handler* handler = find_handler(*domain->second, statement.c))
            if (handler->reply_type) env[statement.a] = *handler->reply_type;
      }
      return;
    }

    if (statement.kind == Stmt::Kind::Call && !statement.b.empty() &&
        statement.b == "push" && statement.args.size() == 1) {
      auto container = env.find(statement.a);
      auto argument = inferred_expr_type(statement.args.front(), env);
      if (container != env.end() && argument &&
          (container->second == "vector" || container->second == "queue"))
        container->second += "[" + *argument + "]";
    }
  }

  void walk_type_environment_block(const vector<Stmt>& statements, size_t& index,
                                   int level, TypeEnv& env,
                                   const TypeEnvVisitor& visitor,
                                   bool reject_conflicts,
                                   bool record_join_types,
                                   bool record_semantic_types) const {
    while (index < statements.size()) {
      const Stmt& statement = statements[index];
      if (statement.indent < level) return;
      if (statement.indent > level)
        err(statement.line, "indentation jumps more than one block level");
      if (statement.kind == Stmt::Kind::Else) return;

      if (statement.kind == Stmt::Kind::If) {
        visitor(statement, env);
        TypeEnv incoming = env;
        ++index;
        TypeEnv then_env = incoming;
        walk_type_environment_block(statements, index, level + 1, then_env,
                                    visitor, reject_conflicts, record_join_types,
                                    record_semantic_types);

        vector<TypeEnv> paths;
        paths.push_back(std::move(then_env));
        if (index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          TypeEnv else_env = incoming;
          walk_type_environment_block(statements, index, level + 1, else_env,
                                      visitor, reject_conflicts, record_join_types,
                                      record_semantic_types);
          paths.push_back(std::move(else_env));
        } else {
          paths.push_back(std::move(incoming));
        }
        env = merge_type_environments(paths, statement.line, reject_conflicts);
        if (record_join_types)
          const_cast<Stmt&>(statement).joined_types = env;
        continue;
      }

      if (statement.kind == Stmt::Kind::While) {
        visitor(statement, env);
        TypeEnv incoming = env;
        ++index;
        TypeEnv body_env = incoming;
        walk_type_environment_block(statements, index, level + 1, body_env,
                                    visitor, reject_conflicts, record_join_types,
                                    record_semantic_types);
        env = merge_type_environments({incoming, body_env}, statement.line,
                                      reject_conflicts);
        if (record_join_types)
          const_cast<Stmt&>(statement).joined_types = env;
        continue;
      }

      visitor(statement, env);
      advance_type_environment(statement, env, statements, record_semantic_types);
      ++index;
    }
  }

  TypeEnv walk_type_environment(const vector<Stmt>& statements, TypeEnv env,
                                const TypeEnvVisitor& visitor,
                                bool reject_conflicts,
                                bool record_join_types = false,
                                bool record_semantic_types = false) const {
    size_t index = 0;
    walk_type_environment_block(statements, index, 0, env, visitor,
                                reject_conflicts, record_join_types,
                                record_semantic_types);
    if (index != statements.size())
      err(statements[index].line, "unexpected 'else' without matching 'if'");
    return env;
  }

  void infer_statement_expressions(const vector<Stmt>& statements,
                                   std::unordered_map<string,string>& env) {
    TypeEnvVisitor infer_statement = [&](const Stmt& statement, const TypeEnv& current_env) {
      switch (statement.kind) {
        case Stmt::Kind::If:
        case Stmt::Kind::While:
          constrain_constructor_fields(statement.line, statement.a, current_env);
          break;
        case Stmt::Kind::Let:
        case Stmt::Kind::Var:
        case Stmt::Kind::Assign:
          constrain_constructor_fields(statement.line, statement.b, current_env);
          break;
        case Stmt::Kind::Message:
        case Stmt::Kind::AwaitMessage:
        case Stmt::Kind::Call:
          for (const auto& arg : statement.args)
            constrain_constructor_fields(statement.line, arg, current_env);
          if (statement.kind == Stmt::Kind::Message || statement.kind == Stmt::Kind::AwaitMessage) {
            const string& receiver_name = statement.kind == Stmt::Kind::Message
                ? statement.a : statement.b;
            const string& handler_name = statement.kind == Stmt::Kind::Message
                ? statement.b : statement.c;
            auto receiver = current_env.find(receiver_name);
            if (receiver != current_env.end() && domains_.count(receiver->second)) {
              auto* handler = find_handler(*domains_.at(receiver->second), handler_name);
              if (handler) {
                for (size_t index = 0;
                     index < statement.args.size() && index < handler->params.size(); ++index) {
                  if (auto pipeline =
                          parse_functional_pipeline(statement.args[index]);
                      pipeline &&
                          functional_pipeline_requires_materialization(*pipeline))
                    err(statement.line,
                        "functional pipeline must be materialized in a local binding "
                        "before crossing a domain boundary");
                  auto actual = inferred_expr_type(statement.args[index], current_env);
                  if (!actual) continue;
                  auto& parameter = handler->params[index];
                  bool inferred_container =
                      (parameter.type == "vector" && starts_with(*actual, "vector[")) ||
                      (parameter.type == "queue" && starts_with(*actual, "queue[")) ||
                      (parameter.type == "map" && starts_with(*actual, "map["));
                  if (parameter.type.empty() || inferred_container)
                    parameter.type = *actual;
                  else if (!same_type(parameter.type, *actual))
                    err(statement.line, "argument " + std::to_string(index + 1) +
                        " to message " + receiver->second + "." + handler_name +
                        " has type '" + *actual + "', expected '" + parameter.type + "'");
                }
              }
            }
          }
          if (statement.kind == Stmt::Kind::Call && statement.b.empty()) {
            auto function = functions_.find(statement.a);
            if (function != functions_.end()) {
              for (size_t i = 0; i < statement.args.size() && i < function->second->params.size(); ++i) {
                auto actual = inferred_expr_type(statement.args[i], current_env);
                if (!actual) continue;
                auto& parameter = function->second->params[i];
                if (parameter.type.empty() && !function->second->generic &&
                    !function->second->static_dispatch)
                  parameter.type = *actual;
                else if (!function->second->generic &&
                         !function->second->static_dispatch &&
                         !traits_.count(parameter.type) &&
                         !same_type(parameter.type, *actual))
                  err(statement.line, "argument " + std::to_string(i + 1) + " to function '" +
                      function->second->name + "' has type '" + *actual +
                      "', expected '" + parameter.type + "'");
              }
            }
          }
          break;
        case Stmt::Kind::Echo:
          for (const auto& arg : statement.args)
            constrain_constructor_fields(statement.line, arg, current_env);
          break;
        case Stmt::Kind::Reply:
        case Stmt::Kind::Return:
          if (!statement.a.empty())
            constrain_constructor_fields(statement.line, statement.a, current_env);
          break;
        case Stmt::Kind::Raw:
          constrain_constructor_fields(statement.line, statement.text, current_env);
          break;
        case Stmt::Kind::Else:
          break;
      }
    };
    env = walk_type_environment(statements, env, infer_statement, false);
  }

  void infer_object_fields() {
    for (size_t round = 0;
         round <= p_.objects.size() + p_.functions.size() + p_.domains.size() + 2; ++round) {
      for (const auto& function : p_.functions) {
        std::unordered_map<string,string> env;
        for (const auto& parameter : function.params) env[parameter.name] = parameter.type;
        infer_statement_expressions(function.body, env);
        if (function.result_expression)
          constrain_constructor_fields(function.result_line, *function.result_expression, env);
      }
      for (auto& domain : p_.domains) {
        for (const auto& field : domain.state) {
          if (!field.init.empty())
            constrain_constructor_fields(field.line, field.init, {});
        }
        for (auto& handler : domain.handlers) {
          std::unordered_map<string,string> env;
          env["self"] = domain.name;
          for (const auto& parameter : handler.params) env[parameter.name] = parameter.type;
          for (const auto& field : domain.state) env[field.name] = field.type;
          infer_statement_expressions(handler.body, env);
        }
      }
      if (p_.main) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(p_.main->body, env);
      }
    }
    for (const auto& object : p_.objects) {
      for (const auto& field : object.fields) {
        if (field.type.empty())
          err(field.line, "cannot infer type for field '" + object.name + "." + field.name +
              "'; add an annotation or a constructor constraint");
      }
    }
  }

  void infer_domain_state_fields(bool finalize) {
    for (size_t round = 0;
         round <= p_.domains.size() * 3 + p_.objects.size() + 3; ++round) {
      for (auto& domain : p_.domains) {
        std::unordered_map<string,string> initializer_env;
        initializer_env["self"] = domain.name;
        for (const auto& field : domain.state) {
          if (!field.type.empty()) initializer_env[field.name] = field.type;
        }
        for (auto& field : domain.state) {
          if (!field.init.empty()) {
            constrain_constructor_fields(field.line, field.init, initializer_env);
            if (auto actual = inferred_expr_type(field.init, initializer_env)) {
              if (field.type.empty()) field.type = *actual;
              else if (!same_type(field.type, *actual))
                err(field.line, "state field '" + domain.name + "." + field.name +
                    "' is annotated '" + field.type + "' but its initializer has type '" +
                    *actual + "'");
            }
          }
          if (!field.type.empty()) initializer_env[field.name] = field.type;
        }

        for (auto& handler : domain.handlers) {
          std::unordered_map<string,string> env;
          env["self"] = domain.name;
          for (const auto& parameter : handler.params) env[parameter.name] = parameter.type;
          for (const auto& field : domain.state) env[field.name] = field.type;
          infer_statement_expressions(handler.body, env);
          for (auto& field : domain.state) {
            auto inferred = env.find(field.name);
            if (inferred == env.end() || inferred->second.empty() || inferred->second == "_value") continue;
            if (field.type.empty()) field.type = inferred->second;
            else if (!same_type(field.type, inferred->second))
              err(field.line, "state field '" + domain.name + "." + field.name +
                  "' has conflicting inferred types '" + field.type + "' and '" +
                  inferred->second + "'");
          }
        }
      }
    }
    if (!finalize) return;
    for (const auto& domain : p_.domains) {
      for (const auto& field : domain.state) {
        if (field.type.empty())
          err(field.line, "cannot infer type for state field '" + domain.name + "." +
              field.name + "'; add an annotation or initializer");
      }
    }
  }

  void infer_handler_reply_types(bool finalize) {
    for (size_t round = 0;
         round <= p_.domains.size() * 3 + p_.functions.size() + 3; ++round) {
      for (auto& domain : p_.domains) {
        for (auto& handler : domain.handlers) {
          std::unordered_map<string,string> env;
          env["self"] = domain.name;
          for (const auto& parameter : handler.params) env[parameter.name] = parameter.type;
          for (const auto& field : domain.state) env[field.name] = field.type;
          infer_statement_expressions(handler.body, env);
          for (const auto& statement : handler.body) {
            if (statement.kind != Stmt::Kind::Reply) continue;
            auto actual = inferred_expr_type(statement.a, env);
            if (!actual) continue;
            if (!handler.reply_type) {
              handler.reply_type = *actual;
            } else if (!same_type(*handler.reply_type, *actual)) {
              err(statement.line, "conflicting reply types in handler '" + domain.name +
                  "." + handler.name + "': expected '" + *handler.reply_type +
                  "' but this reply has type '" + *actual + "'");
            }
          }
        }
      }
    }
    if (!finalize) return;
    for (const auto& domain : p_.domains) {
      for (const auto& handler : domain.handlers) {
        bool has_reply = std::any_of(handler.body.begin(), handler.body.end(),
                                     [](const Stmt& statement) {
                                       return statement.kind == Stmt::Kind::Reply;
                                     });
        if (has_reply && !handler.reply_type)
          err(handler.line, "cannot infer reply type for handler '" + domain.name +
              "." + handler.name + "'; add an annotation or use a statically typed reply expression");
      }
    }
  }

  void infer_function_signatures(bool finalize) {
    for (auto& function : p_.functions) {
      for (const auto& parameter : function.params)
        if (!parameter.type.empty() && traits_.count(parameter.type))
          function.static_dispatch = true;
      for (const auto& parameter : function.params) {
        if (!parameter.type.empty() && parameter.type != "vector" && parameter.type != "queue" && parameter.type != "map") continue;
        auto mark = [&](const string& expression) {
          auto binary = split_binary(trim(expression), {"+", "-", "*", "/"});
          if (binary) {
            for (const auto& p : function.params) {
              if (trim(binary->first) == p.name && trim(binary->second) == p.name) {
                function.generic = true;
                function.static_dispatch = true;
                string e = trim(expression);
                string op = e.find('+') != string::npos ? "+" : e.find('-') != string::npos ? "-" : e.find('*') != string::npos ? "*" : "/";
                function.constraints.push_back({ConstraintKind::Operator, p.name, op, p.name});
                function.generic_results[p.name] = p.name;
              }
            }
          }
          string base, idx;
          if (parse_index(expression, base, idx)) {
            for (const auto& p : function.params) if (trim(base) == p.name) {
              function.generic = true;
              function.constraints.push_back({ConstraintKind::Indexable, p.name, "index", "element:" + p.name});
              function.generic_results["element:" + p.name] = p.name;
            }
          }
        };
        if (function.result_expression) mark(*function.result_expression);
        for (const auto& s : function.body) if (s.kind == Stmt::Kind::Return && !s.a.empty()) mark(s.a);
      }
      auto result_expectation = [&]() {
        if (function.return_type && !starts_with(*function.return_type, "_"))
          return *function.return_type;
        return string("$function_result");
      };
      if (function.result_expression)
        derive_expression_constraints(function, *function.result_expression,
                                      result_expectation());
      for (const auto& s : function.body) {
        derive_expression_constraints(function, s.a);
        derive_expression_constraints(function, s.b);
        for (const auto& arg : s.args) derive_expression_constraints(function, arg);
        if (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var)
          derive_expression_constraints(function, s.b);
        if (s.kind == Stmt::Kind::Return && !s.a.empty())
          derive_expression_constraints(function, s.a, result_expectation());
      }
      std::unordered_map<string,string> local_sources;
      for (const auto& statement : function.body) {
        if ((statement.kind == Stmt::Kind::Assign ||
             statement.kind == Stmt::Kind::Let ||
             statement.kind == Stmt::Kind::Var) &&
            plain_identifier(statement.a))
          local_sources[statement.a] = statement.b;
        if (statement.kind == Stmt::Kind::Return) {
          auto source = local_sources.find(trim(statement.a));
          if (source != local_sources.end())
            derive_expression_constraints(function, source->second,
                                          result_expectation());
        }
      }
      if (function.result_expression) {
        auto source = local_sources.find(trim(*function.result_expression));
        if (source != local_sources.end())
          derive_expression_constraints(function, source->second,
                                        result_expectation());
      }
      if (!function.return_type || starts_with(*function.return_type, "_"))
        for (const auto& kv : function.generic_results)
          function.return_type = kv.first.rfind("element:", 0) == 0 ? "_element:" + kv.first : kv.first.rfind("field:", 0) == 0 ? "_field:" + kv.first : "_generic:" + kv.first;
    }
    for (size_t round = 0; round <= p_.functions.size() * 3 + 3; ++round) {
      for (auto& function : p_.functions) {
        std::unordered_map<string,string> env;
        for (const auto& parameter : function.params) env[parameter.name] = parameter.type;
        infer_statement_expressions(function.body, env);
        if (function.return_type && starts_with(*function.return_type, "_field:") && function.result_expression) {
          if (auto resolved = inferred_expr_type(*function.result_expression, env)) function.return_type = *resolved;
        }
        if (function.result_expression) {
          constrain_constructor_fields(function.result_line, *function.result_expression, env);
          if (auto result = inferred_expr_type(*function.result_expression, env)) {
            if (!function.return_type) function.return_type = *result;
            else if (!starts_with(*function.return_type, "_") &&
                     !starts_with(*result, "_method_") &&
                     !same_type(*function.return_type, *result))
              err(function.result_line, "function '" + function.name + "' returns '" + *result +
                  "' but is annotated '" + *function.return_type + "'");
          }
        } else if (!function.return_type) {
          for (const auto& statement : function.body) {
            if (statement.kind != Stmt::Kind::Return || statement.a.empty()) continue;
            auto result = inferred_expr_type(statement.a, env);
            if (!result) continue;
            if (!function.return_type) function.return_type = *result;
            else if (!starts_with(*function.return_type, "_method_result:") &&
                     !starts_with(*result, "_method_") &&
                     !same_type(*function.return_type, *result))
              err(statement.line, "function '" + function.name + "' returns '" + *result +
                  "' but another return path has type '" + *function.return_type + "'");
          }
        }
      }
      for (auto& domain : p_.domains) {
        for (auto& handler : domain.handlers) {
          std::unordered_map<string,string> env;
          env["self"] = domain.name;
          for (const auto& parameter : handler.params) env[parameter.name] = parameter.type;
          for (const auto& field : domain.state) env[field.name] = field.type;
          infer_statement_expressions(handler.body, env);
        }
      }
      if (p_.main) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(p_.main->body, env);
      }
    }
    if (!finalize) return;
    for (auto& function : p_.functions) {
      if (function.generic && std::all_of(function.params.begin(), function.params.end(), [](const Param& p) { return !p.type.empty(); }))
        function.generic = false;
      if (!function.generic && function.result_expression) {
        auto binary = split_binary(trim(*function.result_expression), {"+", "-", "*", "/"});
        if (binary) for (const auto& p : function.params)
          if (p.type.empty() && trim(binary->first) == p.name && trim(binary->second) == p.name) {
            function.generic = true;
            function.static_dispatch = true;
            string be = trim(*function.result_expression);
            string op = be.find('+') != string::npos ? "+" : be.find('-') != string::npos ? "-" : be.find('*') != string::npos ? "*" : "/";
            function.constraints.push_back({ConstraintKind::Operator, p.name, op, p.name});
            function.generic_results[p.name] = p.name;
            function.return_type = "_generic:" + p.name;
          }
      }
      for (const auto& parameter : function.params) {
        if (parameter.type.empty() && !function.generic)
          err(function.line, "cannot infer type for parameter '" + parameter.name +
              "' in function '" + function.name + "'");
      }
      if (!function.return_type) {
        bool has_value_return = std::any_of(function.body.begin(), function.body.end(),
            [](const Stmt& statement) {
              return statement.kind == Stmt::Kind::Return && !statement.a.empty();
            });
        if (has_value_return)
          err(function.line, "cannot infer return type for function '" + function.name + "'");
        function.return_type = "unit";
      }
    }
  }

  static Effect join_effect(Effect left, Effect right) {
    return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
  }

  static bool effect_is_stronger(Effect left, Effect right) {
    return static_cast<int>(left) > static_cast<int>(right);
  }

  static Effect function_parameter_effect(const Function& function, size_t index) {
    return index < function.parameter_effects.size()
        ? function.parameter_effects[index] : Effect::Read;
  }

  static Effect method_parameter_effect(const Method& method, size_t index) {
    return index < method.parameter_effects.size()
        ? method.parameter_effects[index] : Effect::Read;
  }

  static bool simple_effect_identifier(const string& value) {
    return simple_identifier(trim(value));
  }

  bool effect_requires_borrow(const string& type) const {
    string t = canonical_type_name(type);
    if (t.empty() || starts_with(t, "_") || copy_type(t) || domains_.count(t)) return false;
    return true;
  }

  struct StorageLocation {
    string root;
    vector<string> path;
  };

  static string strip_expression_parens(string value) {
    value = trim(std::move(value));
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')' &&
           matching_paren(value, 0) == value.size() - 1)
      value = trim(value.substr(1, value.size() - 2));
    return value;
  }

  std::optional<StorageLocation> storage_location(
      const string& expression,
      const std::unordered_map<string,string>& env) const {
    string value = strip_expression_parens(normalize_pipeline(trim(expression)));
    if (simple_identifier(value)) {
      if (!env.count(value)) return std::nullopt;
      if (current_object_ && value != "self" &&
          std::any_of(current_object_->fields.begin(), current_object_->fields.end(),
                      [&](const Field& field) { return field.name == value; }))
        return StorageLocation{"self", {value}};
      return StorageLocation{value, {}};
    }
    string base, index;
    if (parse_index(value, base, index)) {
      auto location = storage_location(base, env);
      if (!location) return std::nullopt;
      location->path.push_back("[]");
      return location;
    }
    auto dot = value.rfind('.');
    if (dot != string::npos && value.find('(', dot) == string::npos) {
      auto location = storage_location(value.substr(0, dot), env);
      if (!location) return std::nullopt;
      location->path.push_back(trim(value.substr(dot + 1)));
      return location;
    }
    return std::nullopt;
  }

  static bool storage_locations_overlap(const StorageLocation& left,
                                        const StorageLocation& right) {
    if (left.root != right.root) return false;
    size_t shared = std::min(left.path.size(), right.path.size());
    for (size_t index = 0; index < shared; ++index) {
      if (left.path[index] == "[]" || right.path[index] == "[]") continue;
      if (left.path[index] != right.path[index]) return false;
    }
    return true;
  }

  static string effect_access_name(Effect effect) {
    if (effect == Effect::Write) return "mutation";
    if (effect == Effect::Consume) return "transfer";
    return "read";
  }

  void check_conflicting_call_accesses(
      int line, const string& call_name, const vector<string>& expressions,
      const vector<Effect>& effects, const OwnershipEnv& env) const {
    struct Access { StorageLocation location; Effect effect; };
    vector<Access> accesses;
    for (size_t index = 0; index < expressions.size(); ++index) {
      auto location = storage_location(expressions[index], env.types);
      if (!location) continue;
      auto type = inferred_expr_type(expressions[index], env.types);
      if (!type || !transfer_type(*type)) continue;
      accesses.push_back(Access{*location,
          index < effects.size() ? effects[index] : Effect::Read});
    }
    for (size_t left = 0; left < accesses.size(); ++left) {
      for (size_t right = left + 1; right < accesses.size(); ++right) {
        if (!storage_locations_overlap(accesses[left].location,
                                       accesses[right].location))
          continue;
        if (accesses[left].effect == Effect::Read &&
            accesses[right].effect == Effect::Read)
          continue;
        err(line, "conflicting accesses to value '" + accesses[left].location.root +
            "' in call to '" + call_name + "': " +
            effect_access_name(accesses[left].effect) + " overlaps with " +
            effect_access_name(accesses[right].effect));
      }
    }
  }

  Effect projection_effect(const string& expression, Effect requested,
                           const std::unordered_map<string,string>& env) const {
    if (requested != Effect::Consume) return requested;
    auto type = inferred_expr_type(expression, env);
    return type && transfer_type(*type) ? Effect::Consume : Effect::Read;
  }

  bool possible_method_effects(const string& receiver_type,
                               const string& method_name, size_t arity,
                               Effect& receiver_effect,
                               vector<Effect>& parameter_effects) const {
    receiver_effect = Effect::Read;
    parameter_effects.assign(arity, Effect::Read);
    bool found = false;
    string type = canonical_type_name(receiver_type);
    for (const auto& object : p_.objects) {
      if (!starts_with(type, "_") && traits_.count(type) &&
          !trait_conforms(object.name, type))
        continue;
      if (!starts_with(type, "_") && !traits_.count(type) &&
          object.name != type)
        continue;
      for (const auto& method : object.methods) {
        if (method.name != method_name || method.params.size() != arity) continue;
        found = true;
        receiver_effect = join_effect(receiver_effect, method.receiver_effect);
        for (size_t index = 0; index < arity; ++index)
          parameter_effects[index] = join_effect(
              parameter_effects[index], method_parameter_effect(method, index));
      }
    }
    return found;
  }

  void mark_effect_name(const string& name, Effect effect,
                        const vector<Param>& params,
                        const std::unordered_map<string,string>& env,
                        vector<Effect>& parameter_effects,
                        Effect& receiver_effect,
                        const std::set<string>& receiver_fields) const {
    string value = trim(name);
    auto parameter = std::find_if(params.begin(), params.end(),
                                  [&](const Param& candidate) { return candidate.name == value; });
    if (parameter != params.end()) {
      size_t index = static_cast<size_t>(parameter - params.begin());
      if (index >= parameter_effects.size()) parameter_effects.resize(params.size(), Effect::Read);
      parameter_effects[index] = join_effect(parameter_effects[index], effect);
      return;
    }
    if (value == "self" || receiver_fields.count(value)) {
      receiver_effect = join_effect(receiver_effect, effect);
      return;
    }
    // A field/member expression may have been normalized into an identifier by
    // the parser.  Only bindings in the current environment participate in the
    // effect summary; unknown names are handled by the type checker.
    (void)env;
  }

  void analyze_effect_expression(
      const string& expression, const std::unordered_map<string,string>& env,
      const vector<Param>& params, vector<Effect>& parameter_effects,
      Effect& receiver_effect, const std::set<string>& receiver_fields,
      Effect requested = Effect::Read) const {
    string value = normalize_pipeline(trim(expression));
    if (value.empty()) return;

    if (simple_effect_identifier(value)) {
      auto type = env.find(value);
      Effect effective = requested;
      if (effective == Effect::Consume && (type == env.end() ||
                                           !effect_requires_borrow(type->second)))
        effective = Effect::Read;
      mark_effect_name(value, effective, params, env, parameter_effects,
                       receiver_effect, receiver_fields);
      return;
    }

    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(value, receiver, method, arguments)) {
      auto receiver_type = inferred_expr_type(receiver, env);
      if (receiver_type) {
        string concrete = canonical_type_name(*receiver_type);
        bool collection = concrete == "vector" || concrete == "queue" || concrete == "map" ||
            starts_with(concrete, "vector[") || starts_with(concrete, "queue[") ||
            starts_with(concrete, "map[");
        if (collection) {
          Effect receiver_use = (method == "push" || method == "pop")
              ? Effect::Write : Effect::Read;
          analyze_effect_expression(receiver, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, receiver_use);
          for (size_t index = 0; index < arguments.size(); ++index)
            analyze_effect_expression(arguments[index], env, params, parameter_effects,
                                      receiver_effect, receiver_fields,
                                      method == "push" && index == 0
                                          ? Effect::Consume : Effect::Read);
          return;
        }
        if (auto object_method = resolve_method(concrete, method, {} , true, nullptr)) {
          analyze_effect_expression(receiver, env, params, parameter_effects,
                                    receiver_effect, receiver_fields,
                                    object_method->receiver_effect);
          for (size_t index = 0; index < arguments.size(); ++index) {
            Effect argument_effect = method_parameter_effect(*object_method, index);
            analyze_effect_expression(arguments[index], env, params, parameter_effects,
                                      receiver_effect, receiver_fields, argument_effect);
          }
          return;
        }
      }
      Effect possible_receiver = Effect::Read;
      vector<Effect> possible_parameters;
      if (receiver_type && possible_method_effects(*receiver_type, method,
                                                   arguments.size(),
                                                   possible_receiver,
                                                   possible_parameters)) {
        analyze_effect_expression(receiver, env, params, parameter_effects,
                                  receiver_effect, receiver_fields,
                                  possible_receiver);
        for (size_t index = 0; index < arguments.size(); ++index)
          analyze_effect_expression(arguments[index], env, params,
                                    parameter_effects, receiver_effect,
                                    receiver_fields, possible_parameters[index]);
        return;
      }
      // An unresolved call with no bounded concrete candidate is diagnosed by
      // ordinary static dispatch checking; keep the effect walk conservative.
      analyze_effect_expression(receiver, env, params, parameter_effects,
                                receiver_effect, receiver_fields, Effect::Read);
      for (const auto& argument : arguments)
        analyze_effect_expression(argument, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
      return;
    }

    string callee;
    vector<string> call_arguments;
    if (parse_simple_call(value, callee, call_arguments)) {
      if (objects_.count(callee)) {
        auto object = objects_.at(callee);
        std::unordered_map<string,string> supplied;
        for (const auto& argument : call_arguments) {
          string field, field_value;
          if (parse_named_argument(argument, field, field_value)) supplied[field] = field_value;
        }
        for (const auto& field : object->fields) {
          auto value_it = supplied.find(field.name);
          if (value_it != supplied.end()) {
            analyze_effect_expression(value_it->second, env, params, parameter_effects,
                                      receiver_effect, receiver_fields,
                                      effect_requires_borrow(field.type)
                                          ? Effect::Consume : Effect::Read);
          }
        }
        return;
      }
      auto function = functions_.find(callee);
      if (function != functions_.end()) {
        for (size_t index = 0; index < call_arguments.size(); ++index) {
          Effect argument_effect = function_parameter_effect(*function->second, index);
          analyze_effect_expression(call_arguments[index], env, params,
                                    parameter_effects, receiver_effect,
                                    receiver_fields, argument_effect);
        }
      } else if (auto self = env.find("self"); self != env.end() &&
                 objects_.count(canonical_type_name(self->second))) {
        vector<string> argument_types;
        for (const auto& argument : call_arguments)
          argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
        if (const Method* object_method = resolve_method(
                canonical_type_name(self->second), callee, argument_types,
                true, nullptr)) {
          analyze_effect_expression("self", env, params, parameter_effects,
                                    receiver_effect, receiver_fields,
                                    object_method->receiver_effect);
          for (size_t index = 0; index < call_arguments.size(); ++index)
            analyze_effect_expression(call_arguments[index], env, params,
                                      parameter_effects, receiver_effect,
                                      receiver_fields,
                                      method_parameter_effect(*object_method, index));
        } else {
          for (const auto& argument : call_arguments)
            analyze_effect_expression(argument, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
        }
      } else {
        for (const auto& argument : call_arguments)
          analyze_effect_expression(argument, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
      }
      return;
    }

    string base, index;
    if (parse_index(value, base, index)) {
      analyze_effect_expression(base, env, params, parameter_effects,
                                receiver_effect, receiver_fields,
                                projection_effect(value, requested, env));
      analyze_effect_expression(index, env, params, parameter_effects,
                                receiver_effect, receiver_fields, Effect::Read);
      return;
    }

    auto dot = value.rfind('.');
    if (dot != string::npos && value.find('(', dot) == string::npos) {
      analyze_effect_expression(value.substr(0, dot), env, params,
                                parameter_effects, receiver_effect,
                                receiver_fields,
                                projection_effect(value, requested, env));
      return;
    }

    for (const auto& operators : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"},
                                                         {"+", "-", "*", "/"}}) {
      if (auto binary = split_binary(value, operators)) {
        analyze_effect_expression(binary->first, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        analyze_effect_expression(binary->second, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        return;
      }
    }

    // Keep the analysis conservative for expressions that are not represented
    // by a richer AST node yet: every parameter mentioned is observed.
    for (const auto& parameter : params)
      if (expression_uses(value, parameter.name))
        mark_effect_name(parameter.name, Effect::Read, params, env,
                         parameter_effects, receiver_effect, receiver_fields);
    for (const auto& field : receiver_fields)
      if (expression_uses(value, field)) receiver_effect = join_effect(receiver_effect, Effect::Read);
  }

  void analyze_effect_block(const vector<Stmt>& statements, size_t& index, int level,
                            std::unordered_map<string,string>& env,
                            const vector<Param>& params,
                            vector<Effect>& parameter_effects,
                            Effect& receiver_effect,
                            const std::set<string>& receiver_fields) const {
    while (index < statements.size()) {
      const Stmt& statement = statements[index];
      if (statement.indent < level || statement.indent > level) return;
      if (statement.kind == Stmt::Kind::Else) return;
      if (statement.kind == Stmt::Kind::If || statement.kind == Stmt::Kind::While) {
        analyze_effect_expression(statement.a, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        bool is_if = statement.kind == Stmt::Kind::If;
        ++index;
        auto child_env = env;
        analyze_effect_block(statements, index, level + 1, child_env, params,
                             parameter_effects, receiver_effect, receiver_fields);
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          auto else_env = env;
          analyze_effect_block(statements, index, level + 1, else_env, params,
                               parameter_effects, receiver_effect, receiver_fields);
        }
        continue;
      }
      switch (statement.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          auto source_type = inferred_expr_type(statement.b, env);
          if (storage_location(statement.b, env) && source_type &&
              effect_requires_borrow(*source_type))
            analyze_effect_expression(statement.b, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Consume);
          else
            analyze_effect_expression(statement.b, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          if (auto type = inferred_expr_type(statement.b, env)) env[statement.a] = *type;
          else if (auto spawned = spawn_domain(statement.b)) env[statement.a] = *spawned;
          else env[statement.a] = "_value";
          ++index;
          break;
        }
        case Stmt::Kind::Assign: {
          string base, ignored;
          if (parse_index(statement.a, base, ignored))
            analyze_effect_expression(statement.a, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Write);
          else {
            auto dot = statement.a.rfind('.');
            if (dot != string::npos)
              analyze_effect_expression(statement.a, env, params,
                                        parameter_effects, receiver_effect,
                                        receiver_fields, Effect::Write);
            else if (simple_effect_identifier(statement.a) && env.count(statement.a))
              mark_effect_name(statement.a, Effect::Write, params, env,
                               parameter_effects, receiver_effect, receiver_fields);
          }
          auto source_type = inferred_expr_type(statement.b, env);
          if (storage_location(statement.b, env) && source_type &&
              effect_requires_borrow(*source_type))
            analyze_effect_expression(statement.b, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Consume);
          else
            analyze_effect_expression(statement.b, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          if (auto type = inferred_expr_type(statement.b, env)) env[statement.a] = *type;
          ++index;
          break;
        }
        case Stmt::Kind::Call:
          if (!statement.b.empty())
            analyze_effect_expression(statement.text, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          else
            analyze_effect_expression(statement.a + "(" + join_arguments(statement.args) + ")",
                                      env, params, parameter_effects, receiver_effect,
                                      receiver_fields, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Message: {
          analyze_effect_expression(statement.a, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
          for (const auto& argument : statement.args)
            analyze_effect_expression(argument, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          ++index;
          break;
        }
        case Stmt::Kind::AwaitMessage: {
          analyze_effect_expression(statement.b, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
          for (const auto& argument : statement.args)
            analyze_effect_expression(argument, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          if (auto receiver = env.find(statement.b); receiver != env.end() && domains_.count(receiver->second)) {
            if (auto domain = domains_.find(receiver->second); domain != domains_.end()) {
              if (const Handler* handler = find_handler(*domain->second, statement.c))
                if (handler->reply_type) env[statement.a] = *handler->reply_type;
            }
          }
          ++index;
          break;
        }
        case Stmt::Kind::Reply:
          analyze_effect_expression(statement.a, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Echo:
          for (const auto& argument : statement.args)
            analyze_effect_expression(argument, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Return:
          if (!statement.a.empty())
            analyze_effect_expression(statement.a, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Consume);
          ++index;
          break;
        case Stmt::Kind::Raw:
          analyze_effect_expression(statement.text, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Else:
        case Stmt::Kind::If:
        case Stmt::Kind::While:
          return;
      }
    }
  }

  static string join_arguments(const vector<string>& arguments) {
    std::ostringstream out;
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (index) out << ", ";
      out << arguments[index];
    }
    return out.str();
  }

  struct LocalCallSite {
    string target;
    vector<string> arguments;
    vector<string> argument_types;
  };

  string callable_label(const string& id) const {
    if (starts_with(id, "fn:")) return id.substr(3);
    if (starts_with(id, "method:")) return id.substr(7);
    return id;
  }

  const Method* callable_method(const string& id, const ObjectType** owner = nullptr) const {
    if (!starts_with(id, "method:")) return nullptr;
    string qualified = id.substr(7);
    auto dot = qualified.find('.');
    if (dot == string::npos) return nullptr;
    auto object = objects_.find(qualified.substr(0, dot));
    if (object == objects_.end()) return nullptr;
    if (owner) *owner = object->second;
    string method_name = qualified.substr(dot + 1);
    auto found = std::find_if(object->second->methods.begin(), object->second->methods.end(),
                              [&](const Method& method) {
                                return method.name == method_name;
                              });
    return found == object->second->methods.end() ? nullptr : &*found;
  }

  void collect_local_call_sites(
      int line, const string& expression,
      const std::unordered_map<string,string>& env,
      const ObjectType* implicit_owner,
      vector<LocalCallSite>& calls) const {
    (void)line;
    string original = trim(expression);
    if (auto pipeline = parse_functional_pipeline(original)) {
      collect_local_call_sites(line, pipeline->source, env, implicit_owner, calls);
      auto source_type = inferred_expr_type(pipeline->source, env);
      string element = source_type
          ? functional_element_type(*source_type, pipeline->source).value_or("")
          : "";
      for (const auto& stage : pipeline->stages) {
        if (stage.kind == FunctionalNodeKind::Reduce &&
            !stage.arguments.empty())
          collect_local_call_sites(line, stage.arguments.front(), env,
                                   implicit_owner, calls);
        string callable;
        vector<string> argument_types;
        if ((stage.kind == FunctionalNodeKind::Map ||
             stage.kind == FunctionalNodeKind::Filter ||
             stage.kind == FunctionalNodeKind::Any ||
             stage.kind == FunctionalNodeKind::All) &&
            !stage.arguments.empty()) {
          callable = stage.arguments.front();
          argument_types = {element};
        } else if (stage.kind == FunctionalNodeKind::Reduce &&
                   stage.arguments.size() == 2) {
          callable = stage.arguments[1];
          argument_types = {
              inferred_expr_type(stage.arguments.front(), env).value_or(""),
              element};
        }
        if (callable.empty()) continue;
        if (expression_uses(callable, "_")) {
          TypeEnv placeholder_env = env;
          placeholder_env["_"] = element;
          collect_local_call_sites(line, callable, placeholder_env,
                                   implicit_owner, calls);
        } else {
          string identity = trim(callable);
          string bound_receiver, bound_method;
          if (parse_bound_method_callable(identity, bound_receiver,
                                          bound_method)) {
            auto receiver_type = inferred_expr_type(bound_receiver, env);
            if (receiver_type) {
              string concrete = canonical_type_name(*receiver_type);
              if (const Method* method = resolve_method(
                      concrete, bound_method, argument_types, true, nullptr))
                calls.push_back(LocalCallSite{
                    "method:" + concrete + "." + method->name, {},
                    argument_types});
            }
          } else {
            auto local = env.find(identity);
            if (local != env.end() && starts_with(local->second, "callable:"))
              identity = local->second.substr(9);
            if (functions_.count(identity))
              calls.push_back(LocalCallSite{"fn:" + identity, {}, argument_types});
          }
        }
        if (stage.kind == FunctionalNodeKind::Map) {
          auto result = functional_callable_result(callable, {element}, env);
          if (result) element = *result;
        }
      }
      return;
    }
    string value = normalize_pipeline(std::move(original));
    if (value.empty()) return;
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')' &&
           matching_paren(value, 0) == value.size() - 1)
      value = trim(value.substr(1, value.size() - 2));

    string receiver, method_name;
    vector<string> arguments;
    if (parse_member_call(value, receiver, method_name, arguments)) {
      vector<string> argument_types;
      for (const auto& argument : arguments)
        argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
      auto receiver_type = inferred_expr_type(receiver, env);
      if (receiver_type) {
        string type = canonical_type_name(*receiver_type);
        if (objects_.count(type)) {
          if (const Method* method = resolve_method(type, method_name, argument_types,
                                                    true, nullptr)) {
            calls.push_back(LocalCallSite{
                "method:" + type + "." + method->name, arguments,
                argument_types});
          }
        } else if (traits_.count(type) || starts_with(type, "_")) {
          for (const auto& object : p_.objects) {
            if (traits_.count(type) && !trait_conforms(object.name, type)) continue;
            for (const auto& method : object.methods)
              if (method.name == method_name && method.params.size() == arguments.size()) {
                calls.push_back(LocalCallSite{
                    "method:" + object.name + "." + method.name, arguments,
                    argument_types});
              }
          }
        }
      }
      collect_local_call_sites(line, receiver, env, implicit_owner, calls);
      for (const auto& argument : arguments)
        collect_local_call_sites(line, argument, env, implicit_owner, calls);
      return;
    }

    string callee;
    vector<string> call_arguments;
    if (parse_simple_call(value, callee, call_arguments)) {
      vector<string> argument_types;
      for (const auto& argument : call_arguments)
        argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
      auto function = functions_.find(callee);
      if (function != functions_.end()) {
        calls.push_back(LocalCallSite{"fn:" + callee, call_arguments,
                                      argument_types});
        for (size_t index = 0;
             index < call_arguments.size() && index < function->second->params.size();
             ++index) {
          if (!has_structural_requirement(*function->second,
                                          function->second->params[index].name,
                                          ConstraintKind::Callable,
                                          "functional callable"))
            continue;
          string identity = trim(call_arguments[index]);
          if (functions_.count(identity))
            calls.push_back(LocalCallSite{"fn:" + identity, {}, {}});
        }
      } else if (implicit_owner && !objects_.count(callee)) {
        if (const Method* method = resolve_method(implicit_owner->name, callee,
                                                  argument_types, true, nullptr))
          calls.push_back(LocalCallSite{
              "method:" + implicit_owner->name + "." + method->name,
              call_arguments, argument_types});
      }
      for (const auto& argument : call_arguments) {
        string field, field_value;
        collect_local_call_sites(line,
                                 parse_named_argument(argument, field, field_value)
                                     ? field_value : argument,
                                 env, implicit_owner, calls);
      }
      return;
    }

    string base, index;
    if (parse_index(value, base, index)) {
      collect_local_call_sites(line, base, env, implicit_owner, calls);
      collect_local_call_sites(line, index, env, implicit_owner, calls);
      return;
    }
    for (const auto& operators : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"},
                                                         {"+", "-", "*", "/"}})
      if (auto binary = split_binary(value, operators)) {
        collect_local_call_sites(line, binary->first, env, implicit_owner, calls);
        collect_local_call_sites(line, binary->second, env, implicit_owner, calls);
        return;
      }
  }

  vector<string> statement_expressions(const Stmt& statement) const {
    vector<string> expressions;
    switch (statement.kind) {
      case Stmt::Kind::Let:
      case Stmt::Kind::Var:
        expressions.push_back(statement.b);
        break;
      case Stmt::Kind::Assign:
        expressions.push_back(statement.a);
        expressions.push_back(statement.b);
        break;
      case Stmt::Kind::Call:
        expressions.push_back(!statement.text.empty()
            ? statement.text
            : statement.a + "(" + join_arguments(statement.args) + ")");
        break;
      case Stmt::Kind::Message:
        expressions.insert(expressions.end(), statement.args.begin(), statement.args.end());
        break;
      case Stmt::Kind::AwaitMessage:
        expressions.insert(expressions.end(), statement.args.begin(), statement.args.end());
        break;
      case Stmt::Kind::Echo:
        expressions.insert(expressions.end(), statement.args.begin(), statement.args.end());
        break;
      case Stmt::Kind::If:
      case Stmt::Kind::While:
        expressions.push_back(statement.a);
        break;
      case Stmt::Kind::Reply:
      case Stmt::Kind::Return:
        if (!statement.a.empty()) expressions.push_back(statement.a);
        break;
      case Stmt::Kind::Raw:
        expressions.push_back(statement.text);
        break;
      case Stmt::Kind::Else:
        break;
    }
    return expressions;
  }

  // These slots mirror the expressions that Rust emission evaluates directly.
  // Keeping them separate from the broader call/effect walker lets semantic
  // analysis attach one exact pipeline plan to each concrete codegen use.
  vector<string> functional_statement_expressions(const Stmt& statement) const {
    vector<string> expressions;
    switch (statement.kind) {
      case Stmt::Kind::Let:
      case Stmt::Kind::Var:
      case Stmt::Kind::Assign:
        expressions.push_back(statement.b);
        break;
      case Stmt::Kind::Call:
      case Stmt::Kind::Message:
      case Stmt::Kind::AwaitMessage:
      case Stmt::Kind::Echo:
        expressions.insert(expressions.end(), statement.args.begin(),
                           statement.args.end());
        break;
      case Stmt::Kind::If:
      case Stmt::Kind::While:
        expressions.push_back(statement.a);
        break;
      case Stmt::Kind::Reply:
      case Stmt::Kind::Return:
        if (!statement.a.empty()) expressions.push_back(statement.a);
        break;
      case Stmt::Kind::Raw:
        expressions.push_back(statement.text);
        break;
      case Stmt::Kind::Else:
        break;
    }
    return expressions;
  }

  void collect_callable_edges(
      const vector<Stmt>& body, const std::optional<string>& result_expression,
      int result_line, TypeEnv env, const ObjectType* implicit_owner,
      const string& source, std::map<string,std::set<string>>& graph,
      std::map<string,int>& edge_lines) const {
    TypeEnvVisitor collect_statement = [&](const Stmt& statement,
                                            const TypeEnv& current_env) {
      for (const auto& expression : statement_expressions(statement)) {
        vector<LocalCallSite> calls;
        collect_local_call_sites(statement.line, expression, current_env,
                                 implicit_owner, calls);
        for (const auto& call : calls) {
          graph[source].insert(call.target);
          edge_lines.emplace(source + "\n" + call.target, statement.line);
        }
      }
    };
    env = walk_type_environment(body, std::move(env), collect_statement, false);
    if (result_expression) {
      if (result_line <= 0)
        throw std::runtime_error(
            "internal error: callable result expression has no Moss source line");
      vector<LocalCallSite> calls;
      collect_local_call_sites(result_line, *result_expression, env,
                               implicit_owner, calls);
      for (const auto& call : calls) {
        graph[source].insert(call.target);
        edge_lines.emplace(source + "\n" + call.target, result_line);
      }
    }
  }

  void check_local_call_cycles() const {
    std::map<string,std::set<string>> graph;
    std::map<string,int> edge_lines;
    for (const auto& function : p_.functions) {
      graph["fn:" + function.name];
      TypeEnv env;
      for (const auto& parameter : function.params)
        env[parameter.name] = parameter.type.empty()
            ? "_generic:" + parameter.name : parameter.type;
      collect_callable_edges(function.body, function.result_expression,
                             function.result_line, std::move(env), nullptr,
                             "fn:" + function.name, graph, edge_lines);
    }
    for (const auto& object : p_.objects) {
      for (const auto& method : object.methods) {
        graph["method:" + object.name + "." + method.name];
        TypeEnv env;
        env["self"] = object.name;
        for (const auto& field : object.fields) env[field.name] = field.type;
        for (const auto& parameter : method.params) env[parameter.name] = parameter.type;
        collect_callable_edges(method.body, method.result_expression,
                               method.result_line, std::move(env), &object,
                               "method:" + object.name + "." + method.name,
                               graph, edge_lines);
      }
    }

    std::map<string,int> state;
    vector<string> stack;
    std::function<void(const string&)> visit = [&](const string& node) {
      state[node] = 1;
      stack.push_back(node);
      auto edges = graph.find(node);
      if (edges != graph.end()) {
        for (const auto& next : edges->second) {
          if (state[next] == 0) visit(next);
          else if (state[next] == 1) {
            auto begin = std::find(stack.begin(), stack.end(), next);
            std::ostringstream cycle;
            for (auto at = begin; at != stack.end(); ++at) {
              if (at != begin) cycle << " -> ";
              cycle << callable_label(*at);
            }
            cycle << " -> " << callable_label(next);
            int line = edge_lines.count(node + "\n" + next)
                ? edge_lines.at(node + "\n" + next) : 1;
            err(line, "recursive local call cycle: " + cycle.str() +
                "; recursion is not supported in Phase 2");
          }
        }
      }
      stack.pop_back();
      state[node] = 2;
    };
    // Freeze the roots before DFS. Recursive lookup is deliberately read-only,
    // so call-graph traversal no longer depends on std::map insertion semantics.
    vector<string> graph_nodes;
    for (const auto& entry : graph) graph_nodes.push_back(entry.first);
    for (const auto& node : graph_nodes)
      if (state[node] == 0) visit(node);
  }

  void check_global_await_cycles() const {
    struct AwaitDependency {
      string target;
      int line = 0;
    };
    std::map<string,vector<AwaitDependency>> graph;
    std::set<string> visited;
    std::function<void(const vector<Stmt>&, const std::optional<string>&, int,
                       TypeEnv, const ObjectType*, const string&)> visit_body;
    std::function<void(int, const string&, const TypeEnv&,
                       const ObjectType*, const string&)> visit_expression;
    std::function<void(const LocalCallSite&, const string&)> visit_callable;

    visit_expression = [&](int line, const string& expression, const TypeEnv& env,
                           const ObjectType* implicit_owner,
                           const string& source_domain) {
      vector<LocalCallSite> calls;
      collect_local_call_sites(line, expression, env, implicit_owner, calls);
      for (const auto& call : calls) visit_callable(call, source_domain);
    };

    visit_callable = [&](const LocalCallSite& call, const string& source_domain) {
      std::ostringstream key;
      key << source_domain << "\n" << call.target;
      for (const auto& type : call.argument_types) key << "\n" << type;
      if (!visited.insert(key.str()).second) return;

      if (starts_with(call.target, "fn:")) {
        auto function = functions_.find(call.target.substr(3));
        if (function == functions_.end()) return;
        TypeEnv env;
        for (size_t index = 0; index < function->second->params.size(); ++index) {
          const auto& parameter = function->second->params[index];
          bool use_actual = parameter.type.empty() || traits_.count(parameter.type);
          string actual = index < call.argument_types.size()
              ? call.argument_types[index] : "";
          env[parameter.name] = use_actual && !actual.empty()
              ? actual
              : parameter.type.empty() ? "_dynamic:" + parameter.name
                                       : parameter.type;
        }
        visit_body(function->second->body, function->second->result_expression,
                   function->second->result_line, std::move(env), nullptr,
                   source_domain);
        return;
      }

      const ObjectType* owner = nullptr;
      const Method* method = callable_method(call.target, &owner);
      if (!method || !owner) return;
      TypeEnv env;
      env["self"] = owner->name;
      for (const auto& field : owner->fields) env[field.name] = field.type;
      for (size_t index = 0; index < method->params.size(); ++index) {
        string actual = index < call.argument_types.size()
            ? call.argument_types[index] : "";
        env[method->params[index].name] = method->params[index].type.empty() &&
            !actual.empty() ? actual : method->params[index].type;
      }
      visit_body(method->body, method->result_expression, method->result_line,
                 std::move(env), owner, source_domain);
    };

    visit_body = [&](const vector<Stmt>& body,
                     const std::optional<string>& result_expression,
                     int result_line, TypeEnv env,
                     const ObjectType* implicit_owner,
                     const string& source_domain) {
      TypeEnvVisitor visit_statement = [&](const Stmt& statement,
                                            const TypeEnv& current_env) {
        if (statement.kind == Stmt::Kind::AwaitMessage) {
          const Domain* target = bounded_await_target(statement.b, current_env);
          if (!target)
            throw std::runtime_error(
                "internal error: unvalidated await target reached cycle construction");
          if (!source_domain.empty()) {
            auto& dependencies = graph[source_domain];
            bool duplicate = std::any_of(
                dependencies.begin(), dependencies.end(),
                [&](const AwaitDependency& dependency) {
                  return dependency.target == target->name &&
                         dependency.line == statement.line;
                });
            if (!duplicate)
              dependencies.push_back({target->name, statement.line});
          }
        }
        for (const auto& expression : statement_expressions(statement))
          visit_expression(statement.line, expression, current_env, implicit_owner,
                           source_domain);
      };
      env = walk_type_environment(body, std::move(env), visit_statement, true);
      if (result_expression) {
        if (result_line <= 0)
          throw std::runtime_error(
              "internal error: await-bearing result expression has no Moss source line");
        visit_expression(result_line, *result_expression, env, implicit_owner,
                         source_domain);
      }
    };

    // Local-call cycles are rejected before this pass. That keeps transitive
    // await discovery finite and simple; `visited` still memoizes repeated
    // specializations. If Moss gains recursion, await-effect propagation must
    // be revisited explicitly rather than relying on this traversal shape.
    //
    // The resulting global DAG serves two soundness obligations. It prevents
    // logical deadlock for serialized, non-reentrant domains, and it prevents
    // cyclic nested state-lock acquisition in the direct shared-memory backend,
    // whose handler may retain one domain lock while awaiting another domain.
    for (const auto& domain : p_.domains) {
      graph[domain.name];
      for (const auto& handler : domain.handlers) {
        std::unordered_map<string,string> env;
        env["self"] = domain.name;
        for (const auto& field : domain.state) env[field.name] = field.type;
        for (const auto& parameter : handler.params) env[parameter.name] = parameter.type;
        visit_body(handler.body, std::nullopt, 0, std::move(env), nullptr,
                   domain.name);
      }
    }

    std::map<string,int> state;
    vector<string> stack;
    vector<int> incoming_lines;
    std::function<void(const string&, int)> visit =
        [&](const string& domain, int incoming_line) {
      state[domain] = 1;
      stack.push_back(domain);
      incoming_lines.push_back(incoming_line);
      auto dependencies = graph.find(domain);
      if (dependencies != graph.end()) {
        for (const auto& dependency : dependencies->second) {
          if (state[dependency.target] == 0) {
            visit(dependency.target, dependency.line);
          } else if (state[dependency.target] == 1) {
            auto begin = std::find(stack.begin(), stack.end(), dependency.target);
            size_t first = static_cast<size_t>(
                std::distance(stack.begin(), begin));
            std::ostringstream witness;
            witness << "await cycle detected:";
            for (size_t index = first; index + 1 < stack.size(); ++index) {
              witness << "\n  " << stack[index] << " --await line "
                      << incoming_lines[index + 1] << "--> " << stack[index + 1];
            }
            witness << "\n  " << domain << " --await line "
                    << dependency.line << "--> " << dependency.target;
            err(dependency.line, witness.str());
          }
        }
      }
      incoming_lines.pop_back();
      stack.pop_back();
      state[domain] = 2;
    };
    for (const auto& domain : p_.domains)
      if (state[domain.name] == 0) visit(domain.name, 0);
  }

  void infer_effects() {
    for (auto& function : p_.functions)
      function.parameter_effects.assign(function.params.size(), Effect::Read);
    for (auto& object : p_.objects)
      for (auto& method : object.methods)
        method.parameter_effects.assign(method.params.size(), Effect::Read);

    size_t entities = p_.functions.size();
    for (const auto& object : p_.objects) entities += object.methods.size();
    size_t rounds = entities * 4 + 4;
    for (size_t round = 0; round < rounds; ++round) {
      bool changed = false;
      for (auto& function : p_.functions) {
        std::unordered_map<string,string> env;
        for (const auto& parameter : function.params)
          env[parameter.name] = parameter.type.empty() ? "_generic:" + parameter.name : parameter.type;
        auto inferred = function.parameter_effects;
        Effect receiver = Effect::Read;
        std::set<string> no_fields;
        size_t index = 0;
        analyze_effect_block(function.body, index, 0, env, function.params,
                             inferred, receiver, no_fields);
        if (function.result_expression)
          analyze_effect_expression(*function.result_expression, env, function.params,
                                    inferred, receiver, no_fields, Effect::Consume);
        if (inferred != function.parameter_effects) {
          function.parameter_effects = std::move(inferred);
          changed = true;
        }
      }
      for (auto& object : p_.objects) {
        for (auto& method : object.methods) {
          std::unordered_map<string,string> env;
          std::set<string> fields;
          for (const auto& field : object.fields) {
            env[field.name] = field.type;
            fields.insert(field.name);
          }
          env["self"] = object.name;
          for (const auto& parameter : method.params) env[parameter.name] = parameter.type;
          auto inferred = method.parameter_effects;
          Effect receiver = Effect::Read;
          size_t index = 0;
          analyze_effect_block(method.body, index, 0, env, method.params,
                               inferred, receiver, fields);
          if (method.result_expression)
            analyze_effect_expression(*method.result_expression, env, method.params,
                                      inferred, receiver, fields, Effect::Consume);
          if (inferred != method.parameter_effects || receiver != method.receiver_effect) {
            method.parameter_effects = std::move(inferred);
            method.receiver_effect = receiver;
            changed = true;
          }
        }
      }
      if (!changed) break;
    }
  }

  static ObservableEffects no_observable_effects() {
    ObservableEffects effects;
    effects.unresolved = false;
    return effects;
  }

  static bool same_observable_effects(const ObservableEffects& left,
                                      const ObservableEffects& right) {
    return left.local_capture_read == right.local_capture_read &&
        left.local_mutation == right.local_mutation &&
        left.domain_read == right.domain_read &&
        left.domain_write == right.domain_write &&
        left.message == right.message && left.await == right.await &&
        left.external_io == right.external_io &&
        left.may_fail == right.may_fail &&
        left.may_diverge == right.may_diverge &&
        left.unresolved == right.unresolved;
  }

  ObservableEffects observable_expression_effects(
      const string& expression, const TypeEnv& env,
      const std::set<string>& domain_fields = {},
      const ObjectType* implicit_object = nullptr) const {
    ObservableEffects effects = no_observable_effects();
    string original = trim(expression);
    if (original.empty()) return effects;

    if (auto pipeline = parse_functional_pipeline(original)) {
      effects.merge(observable_expression_effects(
          pipeline->source, env, domain_fields, implicit_object));
      auto source_type = inferred_expr_type(pipeline->source, env);
      string element = source_type
          ? functional_element_type(*source_type, pipeline->source).value_or("")
          : "";
      for (const auto& stage : pipeline->stages) {
        if (stage.kind == FunctionalNodeKind::Reduce &&
            !stage.arguments.empty())
          effects.merge(observable_expression_effects(
              stage.arguments.front(), env, domain_fields, implicit_object));
        string callable;
        if ((stage.kind == FunctionalNodeKind::Map ||
             stage.kind == FunctionalNodeKind::Filter ||
             stage.kind == FunctionalNodeKind::Any ||
             stage.kind == FunctionalNodeKind::All) &&
            !stage.arguments.empty())
          callable = stage.arguments.front();
        else if (stage.kind == FunctionalNodeKind::Reduce &&
                 stage.arguments.size() == 2)
          callable = stage.arguments[1];
        if (callable.empty()) continue;
        if (expression_uses(callable, "_")) {
          TypeEnv placeholder_env = env;
          placeholder_env["_"] = element;
          effects.merge(observable_expression_effects(
              callable, placeholder_env, domain_fields, implicit_object));
        } else {
          string identity = trim(callable);
          string bound_receiver, bound_method;
          if (parse_bound_method_callable(identity, bound_receiver,
                                          bound_method)) {
            auto receiver_type = inferred_expr_type(bound_receiver, env);
            vector<string> input_types;
            if (stage.kind == FunctionalNodeKind::Reduce &&
                stage.arguments.size() == 2)
              input_types = {
                  inferred_expr_type(stage.arguments.front(), env).value_or(""),
                  element};
            else
              input_types = {element};
            const Method* method = receiver_type
                ? resolve_method(canonical_type_name(*receiver_type), bound_method,
                                 input_types, true, nullptr)
                : nullptr;
            if (method) {
              effects.merge(method->observable_effects);
              effects.local_capture_read = true;
              if (domain_fields.count(bound_receiver)) effects.domain_read = true;
              if (method->receiver_effect != Effect::Read)
                effects.local_mutation = true;
            } else {
              effects.unresolved = true;
            }
          } else {
            auto local = env.find(identity);
            if (local != env.end() && starts_with(local->second, "callable:"))
              identity = local->second.substr(9);
            auto function = functions_.find(identity);
            if (function != functions_.end())
              effects.merge(function->second->observable_effects);
            else
              effects.unresolved = true;
          }
        }
        if (stage.kind == FunctionalNodeKind::Map) {
          auto result = functional_callable_result(callable, {element}, env);
          if (result) element = *result;
        }
      }
      return effects;
    }

    string value = normalize_pipeline(original);
    string index_base, index_expression;
    if (parse_index(value, index_base, index_expression)) {
      effects.may_fail = true;
      effects.merge(observable_expression_effects(
          index_base, env, domain_fields, implicit_object));
      effects.merge(observable_expression_effects(
          index_expression, env, domain_fields, implicit_object));
      return effects;
    }
    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(value, receiver, method, arguments)) {
      effects.merge(observable_expression_effects(
          receiver, env, domain_fields, implicit_object));
      for (const auto& argument : arguments)
        effects.merge(observable_expression_effects(
            argument, env, domain_fields, implicit_object));
      auto receiver_type = inferred_expr_type(receiver, env);
      if (receiver_type) {
        vector<string> argument_types;
        for (const auto& argument : arguments)
          argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
        if (const Method* resolved = resolve_method(
                canonical_type_name(*receiver_type), method, argument_types,
                true, nullptr)) {
          effects.merge(resolved->observable_effects);
          return effects;
        }
        if (domains_.count(canonical_type_name(*receiver_type))) {
          effects.domain_read = true;
          return effects;
        }
      }
      effects.unresolved = true;
      return effects;
    }
    string callee;
    vector<string> arguments_call;
    if (parse_simple_call(value, callee, arguments_call)) {
      for (const auto& argument : arguments_call) {
        string field, field_value;
        effects.merge(observable_expression_effects(
            parse_named_argument(argument, field, field_value) ? field_value : argument,
            env, domain_fields, implicit_object));
      }
      auto function = functions_.find(callee);
      if (function != functions_.end()) {
        effects.merge(function->second->observable_effects);
        return effects;
      }
      if (implicit_object) {
        vector<string> argument_types;
        for (const auto& argument : arguments_call)
          argument_types.push_back(inferred_expr_type(argument, env).value_or(""));
        if (const Method* resolved = resolve_method(
                implicit_object->name, callee, argument_types, true, nullptr)) {
          effects.merge(resolved->observable_effects);
          return effects;
        }
      }
      if (objects_.count(callee) || callee == "sqrt" || callee == "sum" ||
          callee == "Map" || callee == "Queue")
        return effects;
      effects.unresolved = true;
      return effects;
    }
    for (const auto& operators :
         vector<vector<string>>{{" or ", " and "},
                                {"==", "!=", "<=", ">=", "<", ">"},
                                {"+", "-"}, {"*", "/"}}) {
      if (auto binary = split_binary(value, operators)) {
        effects.merge(observable_expression_effects(
            binary->first, env, domain_fields, implicit_object));
        effects.merge(observable_expression_effects(
            binary->second, env, domain_fields, implicit_object));
        if (operators.front() == "*") {
          auto division = split_binary(value, {"/"});
          if (division) effects.may_fail = true;
        }
        return effects;
      }
    }
    for (const auto& field : domain_fields)
      if (expression_uses(value, field)) effects.domain_read = true;
    return effects;
  }

  ObservableEffects observable_body_effects(
      const vector<Stmt>& body, TypeEnv env,
      const std::set<string>& parameters,
      const std::set<string>& domain_fields = {},
      const ObjectType* implicit_object = nullptr) const {
    ObservableEffects effects = no_observable_effects();
    std::set<string> locals = parameters;
    for (const auto& statement : body) {
      // Moss does not attempt a termination proof. A syntactic while is a
      // conservative divergence seed, including when nested in control flow.
      if (statement.kind == Stmt::Kind::While) effects.may_diverge = true;
      if (statement.kind == Stmt::Kind::Echo) effects.external_io = true;
      if (statement.kind == Stmt::Kind::Message) effects.message = true;
      if (statement.kind == Stmt::Kind::AwaitMessage) effects.await = true;
      if (statement.kind == Stmt::Kind::Assign ||
          statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var) {
        string root = trim(statement.a);
        auto dot = root.find('.');
        auto bracket = root.find('[');
        size_t end = std::min(dot == string::npos ? root.size() : dot,
                              bracket == string::npos ? root.size() : bracket);
        root = trim(root.substr(0, end));
        if (domain_fields.count(root)) effects.domain_write = true;
        else if (parameters.count(root) ||
                 (implicit_object && (root == "self" ||
                  std::any_of(implicit_object->fields.begin(),
                              implicit_object->fields.end(),
                              [&](const Field& field) {
                                return field.name == root;
                              }))))
          effects.local_mutation = true;
        locals.insert(statement.a);
      }
      for (const auto& expression_value : statement_expressions(statement))
        effects.merge(observable_expression_effects(
            expression_value, env, domain_fields, implicit_object));
      if (statement.kind == Stmt::Kind::Assign ||
          statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var) {
        if (auto type = inferred_expr_type(statement.b, env))
          env[statement.a] = *type;
      }
    }
    return effects;
  }

  void infer_observable_effects() {
    for (auto& function : p_.functions)
      function.observable_effects = no_observable_effects();
    for (auto& object : p_.objects)
      for (auto& method : object.methods)
        method.observable_effects = no_observable_effects();
    for (auto& domain : p_.domains)
      for (auto& handler : domain.handlers)
        handler.observable_effects = no_observable_effects();

    size_t entities = p_.functions.size();
    for (const auto& object : p_.objects) entities += object.methods.size();
    for (const auto& domain : p_.domains) entities += domain.handlers.size();
    // Local call cycles were rejected before this pass. Repeated rounds make
    // the declaration order irrelevant while propagating the finite summaries
    // bottom-up through that acyclic graph.
    for (size_t round = 0; round < entities * 3 + 3; ++round) {
      bool changed = false;
      for (auto& function : p_.functions) {
        TypeEnv env;
        std::set<string> parameters;
        for (const auto& parameter : function.params) {
          env[parameter.name] = parameter.type.empty()
              ? "_generic:" + parameter.name : parameter.type;
          parameters.insert(parameter.name);
        }
        auto inferred = observable_body_effects(
            function.body, env, parameters);
        if (function.result_expression)
          inferred.merge(observable_expression_effects(
              *function.result_expression, env));
        if (!same_observable_effects(inferred, function.observable_effects)) {
          function.observable_effects = inferred;
          changed = true;
        }
      }
      for (auto& object : p_.objects) {
        for (auto& method : object.methods) {
          TypeEnv env;
          std::set<string> parameters;
          env["self"] = object.name;
          for (const auto& field : object.fields) env[field.name] = field.type;
          for (const auto& parameter : method.params) {
            env[parameter.name] = parameter.type;
            parameters.insert(parameter.name);
          }
          auto inferred = observable_body_effects(
              method.body, env, parameters, {}, &object);
          if (method.result_expression)
            inferred.merge(observable_expression_effects(
                *method.result_expression, env, {}, &object));
          if (!same_observable_effects(inferred, method.observable_effects)) {
            method.observable_effects = inferred;
            changed = true;
          }
        }
      }
      for (auto& domain : p_.domains) {
        std::set<string> domain_fields;
        for (const auto& field : domain.state)
          domain_fields.insert(field.name);
        for (auto& handler : domain.handlers) {
          TypeEnv env;
          std::set<string> parameters;
          env["self"] = domain.name;
          for (const auto& field : domain.state) env[field.name] = field.type;
          for (const auto& parameter : handler.params) {
            env[parameter.name] = parameter.type;
            parameters.insert(parameter.name);
          }
          auto inferred = observable_body_effects(
              handler.body, env, parameters, domain_fields);
          if (!same_observable_effects(inferred,
                                       handler.observable_effects)) {
            handler.observable_effects = inferred;
            changed = true;
          }
        }
      }
      if (!changed) break;
    }
  }

  static vector<string> functional_captures(const string& expression,
                                            const TypeEnv& env) {
    std::set<string> captures;
    bool in_string = false, escaped = false;
    for (size_t index = 0; index < expression.size();) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        ++index;
        continue;
      }
      if (ch == '"') { in_string = true; ++index; continue; }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      string name = expression.substr(index, end - index);
      size_t before = index;
      while (before > 0 &&
             std::isspace(static_cast<unsigned char>(expression[before - 1])))
        --before;
      bool member_name = before > 0 && expression[before - 1] == '.';
      if (name != "_" && !member_name && env.count(name)) captures.insert(name);
      index = end;
    }
    return vector<string>(captures.begin(), captures.end());
  }

  static string observable_barrier(const ObservableEffects& effects) {
    if (effects.domain_write) return "observable domain WRITE";
    if (effects.domain_read) return "observable domain READ";
    if (effects.message) return "message send";
    if (effects.await) return "await";
    if (effects.external_io) return "external/I/O effect";
    if (effects.local_mutation) return "observable local mutation";
    if (effects.may_fail) return "possible failure ordering";
    if (effects.unresolved) return "unresolved effect";
    return "none";
  }

  ObservableEffects functional_stage_effects(
      const string& callable, const vector<string>& input_types,
      const TypeEnv& env, string& identity,
      vector<string>& captures,
      const std::set<string>& domain_fields,
      const ObjectType* implicit_object) const {
    ObservableEffects effects = no_observable_effects();
    identity = trim(callable);
    if (expression_uses(identity, "_")) {
      if (input_types.size() != 1) {
        effects.unresolved = true;
        return effects;
      }
      TypeEnv placeholder_env = env;
      placeholder_env["_"] = input_types.front();
      effects = observable_expression_effects(
          identity, placeholder_env, domain_fields, implicit_object);
      captures = functional_captures(identity, env);
      effects.local_capture_read = !captures.empty();
      identity = "placeholder:" + identity;
      return effects;
    }
    string bound_receiver, bound_method;
    if (parse_bound_method_callable(identity, bound_receiver, bound_method)) {
      auto receiver_type = inferred_expr_type(bound_receiver, env);
      const Method* method = receiver_type
          ? resolve_method(canonical_type_name(*receiver_type), bound_method,
                           input_types, true, nullptr)
          : nullptr;
      if (!method) {
        effects.unresolved = true;
        return effects;
      }
      effects = method->observable_effects;
      effects.local_capture_read = true;
      if (domain_fields.count(bound_receiver)) effects.domain_read = true;
      if (method->receiver_effect != Effect::Read)
        effects.local_mutation = true;
      captures.push_back(bound_receiver);
      return effects;
    }
    auto local = env.find(identity);
    if (local != env.end() && starts_with(local->second, "callable:"))
      identity = local->second.substr(9);
    auto function = functions_.find(identity);
    if (function == functions_.end()) {
      effects.unresolved = true;
      return effects;
    }
    effects = function->second->observable_effects;
    return effects;
  }

  std::optional<size_t> add_functional_pipeline_ir(
      int line, const string& expression, const string& context,
      const string& semantic_identity,
      const TypeEnv& env, Function* owning_function,
      size_t& next_pipeline_id, size_t& next_node_id,
      const std::set<string>& domain_fields = {},
      const ObjectType* implicit_object = nullptr,
      const vector<int>& continuation_lines = {}) {
    auto parsed = parse_functional_pipeline(expression);
    if (!parsed) return std::nullopt;
    auto source_type = inferred_expr_type(parsed->source, env);
    if (!source_type) return std::nullopt;
    auto element = functional_element_type(*source_type, parsed->source);
    if (!element || starts_with(*source_type, "_")) return std::nullopt;

    FunctionalPipeline pipeline;
    pipeline.transient_id = next_pipeline_id++;
    pipeline.semantic_identity = semantic_identity;
    pipeline.line = line;
    pipeline.context = context;
    pipeline.expression = trim(expression);
    pipeline.source_expression = parsed->source;
    pipeline.source_type = canonical_type_name(*source_type);
    pipeline.fusion_eligible = true;
    pipeline.element_independent = true;
    pipeline.deterministic = true;

    FunctionalNode source;
    source.transient_id = next_node_id++;
    source.semantic_identity = semantic_identity + ":source";
    source.kind = FunctionalNodeKind::Source;
    source.span = {line, 0};
    source.source_text = parsed->source;
    source.input_type = pipeline.source_type;
    source.output_type = pipeline.source_type;
    source.effects = observable_expression_effects(
        parsed->source, env, domain_fields, implicit_object);
    source.effects.unresolved = false;
    source.provenance.push_back(source.semantic_identity);
    pipeline.nodes.push_back(std::move(source));

    string current_element = canonical_type_name(*element);
    string current_value_type = pipeline.source_type;
    for (size_t index = 0; index < parsed->stages.size(); ++index) {
      const auto& parsed_stage = parsed->stages[index];
      FunctionalNode node;
      node.transient_id = next_node_id++;
      node.semantic_identity = semantic_identity + ":stage:" +
          std::to_string(index + 1);
      node.kind = parsed_stage.kind;
      int stage_line = index < continuation_lines.size()
          ? continuation_lines[index] : line;
      node.span = {stage_line, index + 1};
      node.source_text = parsed_stage.raw;
      node.input_type = current_value_type;
      node.effects = no_observable_effects();
      node.provenance.push_back(node.semantic_identity);

      string callable;
      if ((node.kind == FunctionalNodeKind::Map ||
           node.kind == FunctionalNodeKind::Filter ||
           node.kind == FunctionalNodeKind::Any ||
           node.kind == FunctionalNodeKind::All) &&
          !parsed_stage.arguments.empty())
        callable = parsed_stage.arguments.front();
      else if (node.kind == FunctionalNodeKind::Reduce &&
               parsed_stage.arguments.size() == 2)
        callable = parsed_stage.arguments[1];
      if (!callable.empty()) {
        node.callable_expression = callable;
        vector<string> callback_input_types{current_element};
        if (node.kind == FunctionalNodeKind::Reduce)
          callback_input_types.insert(
              callback_input_types.begin(),
              inferred_expr_type(parsed_stage.arguments.front(), env).value_or(""));
        node.effects = functional_stage_effects(
            callable, callback_input_types, env, node.callable_identity,
            node.captures, domain_fields, implicit_object);
        if (owning_function &&
            !starts_with(node.callable_identity, "placeholder:") &&
            !node.callable_identity.empty() &&
            std::find(owning_function->callable_dependencies.begin(),
                      owning_function->callable_dependencies.end(),
                      node.callable_identity) ==
                owning_function->callable_dependencies.end())
          owning_function->callable_dependencies.push_back(node.callable_identity);
        auto function = functions_.find(node.callable_identity);
        if (function != functions_.end() && !function->second->parameter_effects.empty())
          node.ownership = function->second->parameter_effects.front();
        string bound_receiver, bound_method;
        if (parse_bound_method_callable(node.callable_identity, bound_receiver,
                                        bound_method)) {
          auto receiver_type = inferred_expr_type(bound_receiver, env);
          const Method* method = receiver_type
              ? resolve_method(canonical_type_name(*receiver_type), bound_method,
                               callback_input_types, true, nullptr)
              : nullptr;
          size_t element_parameter = node.kind == FunctionalNodeKind::Reduce ? 1 : 0;
          if (method && element_parameter < method->parameter_effects.size())
            node.ownership = method->parameter_effects[element_parameter];
        }
      }
      if (node.kind == FunctionalNodeKind::Reduce &&
          !parsed_stage.arguments.empty())
        node.effects.merge(observable_expression_effects(
            parsed_stage.arguments.front(), env, domain_fields,
            implicit_object));

      switch (node.kind) {
        case FunctionalNodeKind::Map: {
          auto result = functional_callable_result(
              callable, {current_element}, env);
          if (!result) return std::nullopt;
          current_element = canonical_type_name(*result);
          current_value_type = "vector[" + current_element + "]";
          node.output_type = current_value_type;
          node.logical_materialization = true;
          break;
        }
        case FunctionalNodeKind::Filter:
          node.output_type = current_value_type;
          node.logical_materialization = true;
          break;
        case FunctionalNodeKind::Reduce:
          node.output_type = inferred_expr_type(
              parsed_stage.arguments.front(), env).value_or("");
          current_value_type = node.output_type;
          pipeline.reduction_compatible = true;
          break;
        case FunctionalNodeKind::Sum:
          node.output_type = current_element;
          current_value_type = current_element;
          pipeline.reduction_compatible = true;
          break;
        case FunctionalNodeKind::Count:
          node.output_type = "int";
          current_value_type = "int";
          pipeline.reduction_compatible = true;
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All:
          node.output_type = "bool";
          current_value_type = "bool";
          pipeline.reduction_compatible = true;
          break;
        case FunctionalNodeKind::Source:
          return std::nullopt;
      }
      if (!node.effects.fusion_safe()) {
        pipeline.fusion_eligible = false;
        pipeline.element_independent = false;
        pipeline.deterministic = false;
        if (pipeline.decision.empty())
          pipeline.decision = "fusion stopped: " + observable_barrier(node.effects);
      }
      pipeline.nodes.push_back(std::move(node));
    }
    pipeline.output_type = current_value_type;
    if (pipeline.decision.empty())
      pipeline.decision = "fusion eligible: ordered element-independent stages";
    size_t pipeline_id = pipeline.transient_id;
    p_.functional_pipelines.push_back(std::move(pipeline));
    return pipeline_id;
  }

  void collect_functional_ir_from_body(
      const vector<Stmt>& body, const std::optional<string>& result,
      int result_line, const vector<int>& result_continuation_lines,
      std::unordered_map<string,size_t>* result_pipeline_ids,
      TypeEnv env, const string& context, Function* owning_function,
      size_t& next_pipeline_id, size_t& next_node_id,
      const std::set<string>& domain_fields = {},
      const ObjectType* implicit_object = nullptr) {
    TypeEnvVisitor visitor = [&](const Stmt& statement,
                                 const TypeEnv& current_env) {
      auto expressions = functional_statement_expressions(statement);
      auto& ids = statement.functional_pipeline_ids[context];
      ids.clear();
      for (size_t index = 0; index < expressions.size(); ++index) {
        string identity = context + "@" + std::to_string(statement.line) +
            ":expression:" + std::to_string(index);
        auto id = add_functional_pipeline_ir(
            statement.line, expressions[index], context, identity, current_env,
            owning_function, next_pipeline_id, next_node_id, domain_fields,
            implicit_object, statement.continuation_lines);
        ids.push_back(id.value_or(0));
      }
    };
    env = walk_type_environment(body, std::move(env), visitor, true);
    if (result) {
      if (result_line <= 0)
        throw std::runtime_error(
            "internal error: functional result expression has no Moss source line");
      int line = result_line;
      auto id = add_functional_pipeline_ir(
          line, *result, context,
          context + "@" + std::to_string(line) + ":result", env,
          owning_function, next_pipeline_id, next_node_id, domain_fields,
          implicit_object, result_continuation_lines);
      if (result_pipeline_ids) {
        result_pipeline_ids->erase(context);
        if (id) (*result_pipeline_ids)[context] = *id;
      }
    }
  }

  void build_functional_ir() {
    p_.functional_pipelines.clear();
    size_t next_pipeline_id = 1;
    size_t next_node_id = 1;
    for (auto& function : p_.functions) {
      function.callable_dependencies.clear();
      function.result_functional_pipeline_ids.clear();
      if (function.static_dispatch && !function.specializations.empty()) {
        for (const auto& specialization : function.specializations) {
          TypeEnv env;
          for (size_t index = 0; index < function.params.size(); ++index) {
            env[function.params[index].name] = specialization.parameter_types[index];
          }
          string context = functional_function_context(function, &specialization);
          collect_functional_ir_from_body(
              function.body, function.result_expression, function.result_line,
              function.result_continuation_lines,
              &function.result_functional_pipeline_ids, std::move(env), context,
              &function, next_pipeline_id, next_node_id);
        }
      } else {
        TypeEnv env;
        for (const auto& parameter : function.params)
          env[parameter.name] = parameter.type.empty()
              ? "_generic:" + parameter.name : parameter.type;
        collect_functional_ir_from_body(
            function.body, function.result_expression, function.result_line,
            function.result_continuation_lines,
            &function.result_functional_pipeline_ids, std::move(env),
            functional_function_context(function), &function,
            next_pipeline_id, next_node_id);
      }
    }
    for (auto& object : p_.objects) {
      for (auto& method : object.methods) {
        method.result_functional_pipeline_ids.clear();
        TypeEnv env;
        env["self"] = object.name;
        for (const auto& field : object.fields) env[field.name] = field.type;
        for (const auto& parameter : method.params)
          env[parameter.name] = parameter.type;
        collect_functional_ir_from_body(
            method.body, method.result_expression, method.result_line,
            method.result_continuation_lines,
            &method.result_functional_pipeline_ids, std::move(env),
            functional_method_context(object, method), nullptr,
            next_pipeline_id, next_node_id, {}, &object);
      }
    }
    for (const auto& domain : p_.domains) {
      for (const auto& handler : domain.handlers) {
        TypeEnv env;
        env["self"] = domain.name;
        for (const auto& field : domain.state) env[field.name] = field.type;
        std::set<string> domain_fields;
        for (const auto& field : domain.state) domain_fields.insert(field.name);
        for (const auto& parameter : handler.params)
          env[parameter.name] = parameter.type;
        collect_functional_ir_from_body(
            handler.body, std::nullopt, 0, {}, nullptr, std::move(env),
            functional_handler_context(domain, handler), nullptr,
            next_pipeline_id, next_node_id, domain_fields);
      }
    }
    if (p_.main)
      collect_functional_ir_from_body(
          p_.main->body, std::nullopt, 0, {}, nullptr, {}, "main", nullptr,
          next_pipeline_id, next_node_id);
  }

  void check_method_ownership() {
    for (const auto& object : p_.objects) {
      current_object_ = &object;
      for (const auto& method : object.methods) {
        OwnershipEnv ownership;
        ownership.types["self"] = object.name;
        for (const auto& field : object.fields)
          ownership.types[field.name] = field.type;
        for (const auto& parameter : method.params)
          ownership.types[parameter.name] = parameter.type;
        OwnershipEnv final_ownership = check_ownership(
            method.body, std::move(ownership), nullptr, nullptr);
        if (method.result_expression)
          check_ownership_expression(
              method.result_line ? method.result_line : method.line,
              *method.result_expression, final_ownership, Effect::Consume);
      }
      current_object_ = nullptr;
    }
  }

  std::optional<size_t> static_payload_size(const string& type,
                                             std::set<string>& visiting) const {
    string t = canonical_type_name(type);
    if (t == "int" || t == "float") return sizeof(std::int64_t);
    if (t == "bool") return sizeof(bool);
    if (t == "unit") return size_t(0);
    if (t == "string" || domains_.count(t)) return std::nullopt;
    if (starts_with(t, "option[") && ends_with(t, "]")) {
      auto inner = static_payload_size(trim(t.substr(7, t.size() - 8)), visiting);
      return inner ? std::optional<size_t>(*inner + 1) : std::nullopt;
    }
    auto object = objects_.find(t);
    if (object == objects_.end()) return std::nullopt;
    if (!visiting.insert(t).second) return std::nullopt;
    size_t total = 0;
    for (const auto& field : object->second->fields) {
      auto size = static_payload_size(field.type, visiting);
      if (!size) { visiting.erase(t); return std::nullopt; }
      total += *size;
    }
    visiting.erase(t);
    return total;
  }

  void warn_payload(int line, const string& type) {
    std::set<string> visiting;
    auto size = static_payload_size(type, visiting);
    if (!size || *size <= 1024) return;
    string message = "message payload copies " + std::to_string(*size) +
        " bytes across a domain boundary";
    if (std::any_of(warnings_.begin(), warnings_.end(), [&](const Warning& warning) {
          return warning.line == line && warning.message == message;
        })) return;
    warnings_.push_back(Warning{line, std::move(message)});
  }

  void require_available(int line, const string& expression, const OwnershipEnv& env) const {
    for (const auto& entry : env.moved) {
      if (expression_uses(expression, entry.first)) {
        err(line, "value '" + entry.first + "' was transferred to '" +
            entry.second.destination + "' at line " + std::to_string(entry.second.line) +
            " (the value was consumed). Create an explicit deep copy if both values must remain independently usable.");
      }
    }
  }

  void consume_binding(int line, const string& name, const OwnershipEnv& env,
                       const string& destination) const {
    auto type = env.types.find(name);
    if (type == env.types.end() || !transfer_type(type->second)) return;
    if (env.state_fields.count(name))
      err(line, "domain state '" + name + "' cannot be consumed into '" + destination + "'");
    const_cast<OwnershipEnv&>(env).moved[name] = MoveInfo{line, destination};
  }

  bool check_functional_pipeline_ownership(int line, const string& expression,
                                           OwnershipEnv& env) {
    auto parsed = parse_functional_pipeline(expression);
    if (!parsed) return false;
    auto source_type = inferred_expr_type(parsed->source, env.types);
    if (!source_type) return false;
    auto element = functional_element_type(*source_type, parsed->source);
    if (!element) return false;

    // Functional collection transformations logically read their source.
    check_ownership_expression(line, parsed->source, env, Effect::Read);
    string current_element = *element;
    for (const auto& stage : parsed->stages) {
      vector<string> callback_inputs;
      string callable;
      if ((stage.kind == FunctionalNodeKind::Map ||
           stage.kind == FunctionalNodeKind::Filter ||
           stage.kind == FunctionalNodeKind::Any ||
           stage.kind == FunctionalNodeKind::All) &&
          !stage.arguments.empty()) {
        callback_inputs = {current_element};
        callable = stage.arguments.front();
      } else if (stage.kind == FunctionalNodeKind::Reduce &&
                 stage.arguments.size() == 2) {
        auto accumulator = inferred_expr_type(stage.arguments.front(), env.types);
        // The terminal result owns the initial accumulator on the empty path;
        // a nontrivial bound initializer therefore transfers into the reduce.
        check_ownership_expression(line, stage.arguments.front(), env,
                                   Effect::Consume);
        callback_inputs = {accumulator.value_or("_value"), current_element};
        callable = stage.arguments[1];
      }
      if (callable.empty()) continue;

      if (expression_uses(callable, "_")) {
        OwnershipEnv placeholder_env = env;
        placeholder_env.types["_"] = current_element;
        vector<string> captures = functional_captures(callable, env.types);
        vector<Param> capture_parameters;
        vector<Effect> capture_effects(captures.size(), Effect::Read);
        for (const auto& capture : captures)
          capture_parameters.push_back(
              Param{capture, env.types.at(capture)});
        Effect unused_receiver_effect = Effect::Read;
        analyze_effect_expression(
            callable, placeholder_env.types, capture_parameters,
            capture_effects, unused_receiver_effect, {});
        for (size_t index = 0; index < captures.size(); ++index) {
          if (capture_effects[index] == Effect::Read) continue;
          err(line, "functional placeholder requires " +
              string(capture_effects[index] == Effect::Write
                         ? "WRITE" : "CONSUME") +
              " access to captured binding '" + captures[index] +
              "'; captures must remain read-only");
        }
        check_ownership_expression(line, callable, placeholder_env, Effect::Read);
        if (placeholder_env.moved.count("_") && transfer_type(current_element))
          err(line, "functional placeholder requires CONSUME access to a "
              "nontrivial source element; this pipeline only reads its source; "
              "no implicit copy is inserted");
        if (stage.kind == FunctionalNodeKind::Map) {
          auto result = inferred_expr_type(callable, placeholder_env.types);
          auto location = storage_location(callable, placeholder_env.types);
          if (result && transfer_type(*result) && location && location->root == "_")
            err(line, "functional map result aliases nontrivial source storage "
                "through '_'; map reads its source and no implicit copy is inserted");
        }
      } else {
        string identity = trim(callable);
        string bound_receiver, bound_method;
        if (parse_bound_method_callable(identity, bound_receiver, bound_method)) {
          check_ownership_expression(line, bound_receiver, env, Effect::Read);
          auto receiver_type = inferred_expr_type(bound_receiver, env.types);
          const Method* method = receiver_type
              ? resolve_method(canonical_type_name(*receiver_type), bound_method,
                               callback_inputs, true, nullptr)
              : nullptr;
          if (method) {
            if (method->receiver_effect != Effect::Read)
              err(line, "functional bound method '" + identity +
                  "' cannot mutate or consume captured binding '" +
                  bound_receiver + "'");
            for (size_t index = 0;
                 index < callback_inputs.size() && index < method->params.size();
                 ++index) {
              Effect effect = method_parameter_effect(*method, index);
              bool element_input = stage.kind != FunctionalNodeKind::Reduce || index == 1;
              if (element_input && !copy_type(callback_inputs[index]) &&
                  effect != Effect::Read)
                err(line, "functional bound method '" + identity +
                    "' requires " +
                    string(effect == Effect::Write ? "WRITE" : "CONSUME") +
                    " access to an element, but this pipeline only reads its source; "
                    "no implicit copy is inserted");
              if ((stage.kind == FunctionalNodeKind::Filter ||
                   stage.kind == FunctionalNodeKind::Any ||
                   stage.kind == FunctionalNodeKind::All) &&
                  effect != Effect::Read)
                err(line, "functional predicate '" + identity +
                    "' must not mutate or consume its element");
            }
          }
        } else {
          auto local = env.types.find(identity);
          if (local != env.types.end() && starts_with(local->second, "callable:"))
            identity = local->second.substr(9);
          auto function = functions_.find(identity);
          if (function != functions_.end()) {
            for (size_t index = 0;
                 index < callback_inputs.size() &&
                     index < function->second->params.size();
                 ++index) {
              Effect effect = function_parameter_effect(*function->second, index);
              bool element_input =
                  stage.kind != FunctionalNodeKind::Reduce || index == 1;
              if (element_input && !copy_type(callback_inputs[index]) &&
                  effect != Effect::Read)
                err(line, "functional callable '" + identity +
                    "' requires " +
                    string(effect == Effect::Write ? "WRITE" : "CONSUME") +
                    " access to an element, but this pipeline only reads its source; "
                    "no implicit copy is inserted");
              if ((stage.kind == FunctionalNodeKind::Filter ||
                   stage.kind == FunctionalNodeKind::Any ||
                   stage.kind == FunctionalNodeKind::All) &&
                  effect != Effect::Read)
                err(line, "functional predicate '" + identity +
                    "' must not mutate or consume its element");
            }
          }
        }
      }

      if (stage.kind == FunctionalNodeKind::Map) {
        auto result = functional_callable_result(callable, {current_element}, env.types);
        if (result) current_element = *result;
      }
    }
    return true;
  }

  void check_ownership_expression(int line, const string& expression,
                                  OwnershipEnv& env, Effect requested = Effect::Read) {
    string original = trim(expression);
    if (check_functional_pipeline_ownership(line, original, env)) return;
    string value = normalize_pipeline(std::move(original));
    if (value.empty()) return;
    if (simple_identifier(value)) {
      if (functions_.count(value)) return;  // Statically closed callable identity.
      auto location = storage_location(value, env.types);
      string binding = location ? location->root : value;
      require_available(line, binding, env);
      if (requested == Effect::Consume)
        consume_binding(line, binding, env,
                        "an expression at line " + std::to_string(line));
      return;
    }

    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(value, receiver, method, arguments)) {
      auto receiver_type = inferred_expr_type(receiver, env.types);
      if (receiver_type) {
        string concrete = canonical_type_name(*receiver_type);
        bool collection = concrete == "vector" || concrete == "queue" || concrete == "map" ||
            starts_with(concrete, "vector[") || starts_with(concrete, "queue[") ||
            starts_with(concrete, "map[");
        if (collection) {
          vector<string> access_expressions{receiver};
          vector<Effect> access_effects{
              (method == "push" || method == "pop") ? Effect::Write : Effect::Read};
          for (size_t index = 0; index < arguments.size(); ++index) {
            access_expressions.push_back(arguments[index]);
            access_effects.push_back(method == "push" && index == 0
                ? Effect::Consume : Effect::Read);
          }
          check_conflicting_call_accesses(line, method, access_expressions,
                                          access_effects, env);
          check_ownership_expression(line, receiver, env,
              (method == "push" || method == "pop") ? Effect::Write : Effect::Read);
          for (size_t index = 0; index < arguments.size(); ++index)
            check_ownership_expression(line, arguments[index], env,
                method == "push" && index == 0 ? Effect::Consume : Effect::Read);
          return;
        }
        vector<string> argument_types;
        for (const auto& argument : arguments)
          argument_types.push_back(inferred_expr_type(argument, env.types).value_or(""));
        if (auto object_method = resolve_method(concrete, method, argument_types, true, nullptr)) {
          vector<string> access_expressions{receiver};
          vector<Effect> access_effects{object_method->receiver_effect};
          for (size_t index = 0; index < arguments.size(); ++index) {
            access_expressions.push_back(arguments[index]);
            access_effects.push_back(method_parameter_effect(*object_method, index));
          }
          check_conflicting_call_accesses(line, method, access_expressions,
                                          access_effects, env);
          check_ownership_expression(line, receiver, env, object_method->receiver_effect);
          for (size_t index = 0; index < arguments.size(); ++index)
            check_ownership_expression(line, arguments[index], env,
                method_parameter_effect(*object_method, index));
          return;
        }
      }
      Effect possible_receiver = Effect::Read;
      vector<Effect> possible_parameters;
      if (receiver_type && possible_method_effects(*receiver_type, method,
                                                   arguments.size(),
                                                   possible_receiver,
                                                   possible_parameters)) {
        vector<string> access_expressions{receiver};
        vector<Effect> access_effects{possible_receiver};
        access_expressions.insert(access_expressions.end(), arguments.begin(),
                                  arguments.end());
        access_effects.insert(access_effects.end(), possible_parameters.begin(),
                              possible_parameters.end());
        check_conflicting_call_accesses(line, method, access_expressions,
                                        access_effects, env);
        check_ownership_expression(line, receiver, env, possible_receiver);
        for (size_t index = 0; index < arguments.size(); ++index)
          check_ownership_expression(line, arguments[index], env,
                                     possible_parameters[index]);
        return;
      }
      check_ownership_expression(line, receiver, env, Effect::Read);
      for (const auto& argument : arguments)
        check_ownership_expression(line, argument, env, Effect::Read);
      return;
    }

    string callee;
    vector<string> call_arguments;
    if (parse_simple_call(value, callee, call_arguments)) {
      if (objects_.count(callee)) {
        std::unordered_map<string,string> supplied;
        for (const auto& argument : call_arguments) {
          string field, field_value;
          if (parse_named_argument(argument, field, field_value)) supplied[field] = field_value;
        }
        vector<string> access_expressions;
        vector<Effect> access_effects;
        for (const auto& field : objects_.at(callee)->fields) {
          auto supplied_value = supplied.find(field.name);
          if (supplied_value != supplied.end()) {
            access_expressions.push_back(supplied_value->second);
            access_effects.push_back(effect_requires_borrow(field.type)
                ? Effect::Consume : Effect::Read);
          }
        }
        check_conflicting_call_accesses(line, callee, access_expressions,
                                        access_effects, env);
        for (const auto& field : objects_.at(callee)->fields) {
          auto supplied_value = supplied.find(field.name);
          if (supplied_value != supplied.end())
            check_ownership_expression(line, supplied_value->second, env,
                effect_requires_borrow(field.type) ? Effect::Consume : Effect::Read);
        }
        return;
      }
      auto function = functions_.find(callee);
      if (function != functions_.end()) {
        vector<Effect> effects;
        for (size_t index = 0; index < call_arguments.size(); ++index)
          effects.push_back(function_parameter_effect(*function->second, index));
        check_conflicting_call_accesses(line, callee, call_arguments, effects, env);
      } else if (current_object_) {
        vector<string> argument_types;
        for (const auto& argument : call_arguments)
          argument_types.push_back(inferred_expr_type(argument, env.types).value_or(""));
        if (const Method* object_method = resolve_method(
                current_object_->name, callee, argument_types, true, nullptr)) {
          vector<string> access_expressions{"self"};
          vector<Effect> access_effects{object_method->receiver_effect};
          for (size_t index = 0; index < call_arguments.size(); ++index) {
            access_expressions.push_back(call_arguments[index]);
            access_effects.push_back(method_parameter_effect(*object_method, index));
          }
          check_conflicting_call_accesses(line, callee, access_expressions,
                                          access_effects, env);
          check_ownership_expression(line, "self", env,
                                     object_method->receiver_effect);
          for (size_t index = 0; index < call_arguments.size(); ++index)
            check_ownership_expression(line, call_arguments[index], env,
                                       method_parameter_effect(*object_method, index));
          return;
        }
      }
      for (size_t index = 0; index < call_arguments.size(); ++index) {
        Effect argument_effect = function == functions_.end()
            ? Effect::Read : function_parameter_effect(*function->second, index);
        check_ownership_expression(line, call_arguments[index], env, argument_effect);
      }
      return;
    }

    string base, index;
    if (parse_index(value, base, index)) {
      check_ownership_expression(line, base, env,
                                 projection_effect(value, requested, env.types));
      check_ownership_expression(line, index, env, Effect::Read);
      return;
    }

    auto dot = value.rfind('.');
    if (dot != string::npos && value.find('(', dot) == string::npos) {
      check_ownership_expression(line, value.substr(0, dot), env,
                                 projection_effect(value, requested, env.types));
      return;
    }
    for (const auto& operators : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"},
                                                         {"+", "-", "*", "/"}}) {
      if (auto binary = split_binary(value, operators)) {
        check_ownership_expression(line, binary->first, env, Effect::Read);
        check_ownership_expression(line, binary->second, env, Effect::Read);
        return;
      }
    }
    require_available(line, value, env);
  }

  static void merge_moved(OwnershipEnv& destination, const OwnershipEnv& branch) {
    for (const auto& entry : branch.moved) {
      if (destination.types.count(entry.first)) destination.moved.emplace(entry.first, entry.second);
    }
  }

  void require_cross_domain_value(int line, const string& expression,
                                  const string& expected_type,
                                  const OwnershipEnv& env,
                                  const string& action) const {
    // A message is Moss's explicit semantic copy boundary.  The source value
    // remains available after a send/await/reply; the backend materializes a
    // detached payload (or an equivalent proven optimization).  This helper is
    // retained as a single validation hook for future reference-capability
    // checks, but ordinary aggregate values are intentionally legal here.
    (void)line;
    (void)expression;
    (void)expected_type;
    (void)env;
    (void)action;
  }

  OwnershipEnv check_ownership(const vector<Stmt>& statements, OwnershipEnv env,
                               const Domain* current_domain,
                               const Handler* current_handler) {
    size_t index = 0;
    check_ownership_block(statements, index, 0, env, current_domain, current_handler);
    return env;
  }

  void check_ownership_block(const vector<Stmt>& statements, size_t& index, int level,
                             OwnershipEnv& env, const Domain* current_domain,
                             const Handler* current_handler) {
    (void)current_domain;
    while (index < statements.size()) {
      const Stmt& s = statements[index];
      if (s.indent < level) return;
      if (s.indent > level) return;
      if (s.kind == Stmt::Kind::Else) return;

      if (s.kind == Stmt::Kind::If || s.kind == Stmt::Kind::While) {
        require_available(s.line, s.a, env);
        bool is_if = s.kind == Stmt::Kind::If;
        OwnershipEnv incoming = env;
        ++index;
        OwnershipEnv body = incoming;
        check_ownership_block(statements, index, level + 1, body, current_domain, current_handler);
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          OwnershipEnv alternative = incoming;
          check_ownership_block(statements, index, level + 1, alternative,
                                current_domain, current_handler);
          env = incoming;
          env.types = merge_type_environments(
              {body.types, alternative.types}, s.line, false);
          merge_moved(env, alternative);
        } else {
          env = incoming;
          env.types = merge_type_environments(
              {incoming.types, body.types}, s.line, false);
        }
        merge_moved(env, body);
        continue;
      }

      switch (s.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          string source = trim(s.b);
          auto source_type = inferred_expr_type(s.b, env.types);
          bool transfers = storage_location(s.b, env.types) && source_type &&
              effect_requires_borrow(*source_type);
          if (transfers) {
            if (simple_identifier(source)) {
              require_available(s.line, source, env);
              consume_binding(s.line, source, env, s.a);
            } else {
              check_ownership_expression(s.line, s.b, env, Effect::Consume);
            }
          } else {
            check_ownership_expression(s.line, s.b, env, Effect::Read);
          }
          std::optional<string> type;
          if (auto spawned = spawn_domain(s.b)) type = *spawned;
          else type = inferred_expr_type(s.b, env.types);

          env.types[s.a] = type.value_or("_value");
          env.moved.erase(s.a); // A declaration may intentionally shadow an older moved binding.
          ++index;
          break;
        }
        case Stmt::Kind::Assign: {
          string lhs_base, lhs_index;
          if (parse_index(s.a, lhs_base, lhs_index))
            check_ownership_expression(s.line, lhs_base, env, Effect::Write);
          else {
            auto dot = s.a.rfind('.');
            if (dot != string::npos)
              check_ownership_expression(s.line, s.a.substr(0, dot), env, Effect::Write);
          }
          string source = trim(s.b);
          auto source_type = inferred_expr_type(s.b, env.types);
          bool transfers = storage_location(s.b, env.types) && source_type &&
              effect_requires_borrow(*source_type);
          if (transfers) {
            if (simple_identifier(source)) {
              require_available(s.line, source, env);
              consume_binding(s.line, source, env, s.a);
            } else {
              check_ownership_expression(s.line, s.b, env, Effect::Consume);
            }
          } else {
            check_ownership_expression(s.line, s.b, env, Effect::Read);
          }
          if (auto spawned = spawn_domain(s.b)) {
            env.types[s.a] = *spawned;
            env.moved.erase(s.a);
            ++index;
            break;
          }
          if (simple_identifier(s.a)) {
            if (source_type) env.types[s.a] = *source_type;
            else if (!env.types.count(s.a)) env.types[s.a] = "_value";
            // Assignment creates or reinitializes the target, including a
            // binding that was consumed on an earlier path.
            env.moved.erase(s.a);
          }
          ++index;
          break;
        }
        case Stmt::Kind::AwaitMessage: {
          check_ownership_expression(s.line, s.b, env, Effect::Read);
          const Handler* awaited = check_call(s.line, s.b, s.c, s.args, env.types);
          for (size_t arg_index = 0; arg_index < s.args.size(); ++arg_index) {
            const auto& arg = s.args[arg_index];
            check_ownership_expression(s.line, arg, env, Effect::Read);
            require_cross_domain_value(s.line, arg, awaited->params[arg_index].type,
                                       env, "");
            warn_payload(s.line, awaited->params[arg_index].type);
          }
          if (auto receiver = env.types.find(s.b); receiver != env.types.end()) {
            if (auto domain = domains_.find(receiver->second); domain != domains_.end()) {
              if (const Handler* handler = find_handler(*domain->second, s.c); handler && handler->reply_type)
                env.types[s.a] = *handler->reply_type;
            }
          }
          if (!env.types.count(s.a)) env.types[s.a] = "_value";
          env.moved.erase(s.a);
          ++index;
          break;
        }
        case Stmt::Kind::Message: {
          check_ownership_expression(s.line, s.a, env, Effect::Read);
          const Handler* handler = check_call(s.line, s.a, s.b, s.args, env.types);
          for (size_t arg_index = 0; arg_index < s.args.size(); ++arg_index) {
            const auto& arg = s.args[arg_index];
            check_ownership_expression(s.line, arg, env, Effect::Read);
            if (arg_index < handler->params.size()) {
              require_cross_domain_value(s.line, arg, handler->params[arg_index].type,
                                         env, "");
              warn_payload(s.line, handler->params[arg_index].type);
            }
          }
          ++index;
          break;
        }
        case Stmt::Kind::Echo:
          for (const auto& arg : s.args) check_ownership_expression(s.line, arg, env, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Call:
          if (!s.b.empty())
            check_ownership_expression(s.line, s.text, env, Effect::Read);
          else
            check_ownership_expression(s.line, s.a + "(" + join_arguments(s.args) + ")",
                                       env, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Reply: {
          check_ownership_expression(s.line, s.a, env, Effect::Read);
          if (current_handler && current_handler->reply_type)
            require_cross_domain_value(s.line, s.a, *current_handler->reply_type,
                                       env, " in a reply");
          if (current_handler && current_handler->reply_type)
            warn_payload(s.line, *current_handler->reply_type);
          ++index;
          break;
        }
        case Stmt::Kind::Raw:
          check_ownership_expression(s.line, s.text, env, Effect::Read);
          ++index;
          break;
        case Stmt::Kind::Return:
          if (!s.a.empty()) check_ownership_expression(s.line, s.a, env, Effect::Consume);
          ++index;
          break;
        case Stmt::Kind::If:
        case Stmt::Kind::Else:
        case Stmt::Kind::While:
          return;
      }
    }
  }

  static bool concrete_specialization_type(const string& type) {
    return !type.empty() && !starts_with(type, "_");
  }

  static string type_list(const vector<string>& types) {
    std::ostringstream out;
    for (size_t index = 0; index < types.size(); ++index) {
      if (index) out << ", ";
      out << (types[index].empty() ? "unresolved" : canonical_type_name(types[index]));
    }
    return out.str();
  }

  void validate_method_requirements(int line, const Function& function,
                                    const vector<string>& parameter_types) const {
    auto instantiated = instantiated_function_env(function, parameter_types);
    std::optional<string> related_function_result;
    for (const auto& requirement : function.constraints) {
      if (requirement.kind != ConstraintKind::Method) continue;
      auto receiver = instantiated.find(requirement.subject);
      if (receiver == instantiated.end() ||
          !concrete_specialization_type(receiver->second) ||
          traits_.count(receiver->second))
        continue;
      vector<string> argument_types;
      for (const auto& argument : requirement.arguments)
        argument_types.push_back(inferred_expr_type(argument, instantiated).value_or(""));
      MethodResolutionFailure failure = MethodResolutionFailure::None;
      const Method* method = resolve_method(canonical_type_name(receiver->second),
                                            requirement.detail, argument_types,
                                            false, &failure);
      if (!method) {
        string prefix = "argument to function '" + function.name + "' has type '" +
            canonical_type_name(receiver->second) + "'";
        if (failure == MethodResolutionFailure::WrongArity)
          err(line, prefix + " with wrong arity for required method '" +
              requirement.detail + "' (requires " +
              std::to_string(requirement.arity) + ")");
        if (failure == MethodResolutionFailure::IncompatibleArguments)
          err(line, prefix + " whose method '" + requirement.detail +
              "' is incompatible with argument types (" + type_list(argument_types) + ")");
        if (failure == MethodResolutionFailure::Ambiguous)
          err(line, prefix + " with ambiguous required method '" +
              requirement.detail + "'");
        err(line, prefix + " missing required method '" + requirement.detail + "'");
      }
      string result = method->return_type.value_or("unit");
      if (!requirement.result.empty()) {
        if (related_function_result && !same_type(*related_function_result, result))
          err(line, "conflicting method result expectations in function '" +
              function.name + "': required methods resolve to both '" +
              *related_function_result + "' and '" + result + "'");
        related_function_result = result;
      }
      for (const auto& expected : requirement.result_expectations) {
        if (!same_type(result, expected))
          err(line, "required method '" + requirement.detail + "' on type '" +
              canonical_type_name(receiver->second) + "' returns '" + result +
              "', conflicting with expected result type '" + expected + "'");
      }
    }
  }

  void register_static_specialization(int line, Function& function,
                                      const vector<string>& parameter_types) {
    if (!function.static_dispatch) return;
    if (parameter_types.size() != function.params.size() ||
        !std::all_of(parameter_types.begin(), parameter_types.end(),
                     [&](const string& type) {
                       return concrete_specialization_type(type) &&
                              !traits_.count(canonical_type_name(type));
                     }))
      return;
    vector<string> canonical_types;
    for (const auto& type : parameter_types)
      canonical_types.push_back(canonical_type_name(type));
    auto existing = std::find_if(function.specializations.begin(),
                                 function.specializations.end(),
                                 [&](const FunctionSpecialization& specialization) {
                                   return specialization.parameter_types == canonical_types;
                                 });
    if (existing != function.specializations.end()) return;
    auto result = specialized_function_return_type(function, canonical_types);
    if (!result)
      err(line, "cannot determine the concrete result type for static specialization of function '" +
          function.name + "'");
    FunctionSpecialization specialization;
    specialization.generated_name = "__moss_specialize_" + function.name + "_" +
        std::to_string(function.specializations.size());
    specialization.parameter_types = canonical_types;
    specialization.return_type = canonical_type_name(*result);
    function.specializations.push_back(specialization);

    std::unordered_map<string,string> specialized_env;
    for (size_t index = 0; index < function.params.size(); ++index)
      specialized_env[function.params[index].name] = canonical_types[index];
    infer_statement_expressions(function.body, specialized_env);
    check_stmts(function.body, specialized_env, nullptr, nullptr, &function);
    if (function.result_expression)
      check_expression(function.result_line ? function.result_line : function.line,
                       *function.result_expression, specialized_env);
  }

  void check_function_call(int line, const string& name, const vector<string>& args,
                           const std::unordered_map<string,string>& env) {
    auto function = functions_.find(name);
    if (function == functions_.end()) {
      if (name == "sqrt" || name == "sum") return;
      if (current_object_) {
        vector<string> argument_types;
        for (const auto& arg : args) argument_types.push_back(inferred_expr_type(arg, env).value_or(""));
        auto method = resolve_method(current_object_->name, name, argument_types);
        if (method) return;
      }
      err(line, "unknown local function '" + name + "'");
    }
    if (function->second->params.size() != args.size())
      err(line, "function " + name + " expects " +
          std::to_string(function->second->params.size()) + " arguments, got " +
          std::to_string(args.size()));
    vector<string> actual_types;
    for (const auto& argument : args)
      actual_types.push_back(inferred_expr_type(argument, env).value_or(""));
    for (size_t index = 0; index < args.size(); ++index) {
      const auto& actual_type = actual_types[index];
      const auto& param = function->second->params[index];
      bool concrete_actual = concrete_specialization_type(actual_type) &&
          !traits_.count(canonical_type_name(actual_type));
      if (concrete_actual && traits_.count(param.type)) {
        string reason;
        if (!trait_conforms(actual_type, param.type, &reason))
          err(line, "argument " + std::to_string(index + 1) + " to function '" + name +
              "' has type '" + actual_type + "' which does not satisfy trait '" +
              param.type + "': " + reason);
      }
      bool container_match = !actual_type.empty() &&
          ((param.type == "vector" && starts_with(actual_type, "vector[")) ||
           (param.type == "map" && starts_with(actual_type, "map[")) ||
           (param.type == "queue" && starts_with(actual_type, "queue[")));
      if (!actual_type.empty() && !starts_with(actual_type, "_") &&
          !function->second->generic && !param.type.empty() &&
          !starts_with(param.type, "_") && !traits_.count(param.type) &&
          !container_match && !same_type(param.type, actual_type))
        err(line, "argument " + std::to_string(index + 1) + " to function '" + name +
            "' has type '" + actual_type + "', expected '" +
            param.type + "'");
      if (concrete_actual && has_constraint(*function->second, param.name)) {
        for (const auto& op : constraint_details(*function->second, param.name)) {
          if ((op == "+" || op == "-" || op == "*" || op == "/") && !numeric_type(actual_type) && !(op == "+" && actual_type == "string"))
            err(line, "argument " + std::to_string(index + 1) + " to function '" + name + "' does not support inferred operation '" + op + "'");
          if (op == "[]" && !(starts_with(actual_type, "vector[") || starts_with(actual_type, "map[") || starts_with(actual_type, "queue[")))
            err(line, "argument " + std::to_string(index + 1) + " to function '" + name + "' is not an indexable container");
        }
        for (const auto& c : function->second->constraints) if (c.subject == param.name) {
          if (c.kind == ConstraintKind::Field && !type_has_field(actual_type, c.detail))
            err(line, "argument " + std::to_string(index + 1) + " to function '" + name + "' has type '" + actual_type + "' missing required field '" + c.detail + "'");
          if (c.kind == ConstraintKind::Iterable &&
              !starts_with(actual_type, "vector[") &&
              !starts_with(actual_type, "seq["))
            err(line, "argument " + std::to_string(index + 1) +
                " to function '" + name +
                "' is not a statically typed functional collection");
          if (c.kind == ConstraintKind::Callable &&
              !starts_with(actual_type, "callable:"))
            err(line, "argument " + std::to_string(index + 1) +
                " to function '" + name +
                "' is not a statically bounded callable identity");
        }
      }
    }
    bool concrete_call = std::all_of(actual_types.begin(), actual_types.end(),
                                     [&](const string& type) {
                                       return concrete_specialization_type(type) &&
                                              !traits_.count(canonical_type_name(type));
                                     });
    if (concrete_call) {
      validate_method_requirements(line, *function->second, actual_types);
      register_static_specialization(line, *function->second, actual_types);
    }
  }

  std::optional<string> check_functional_callable(
      int line, const string& callable, const vector<string>& input_types,
      const TypeEnv& env) {
    string identity = trim(callable);
    if (identity.empty()) err(line, "functional stage requires a callable");
    if (expression_uses(identity, "_")) {
      if (input_types.size() != 1)
        err(line, "placeholder '_' is only valid for a one-argument functional stage");
      TypeEnv placeholder_env = env;
      placeholder_env["_"] = input_types.front();
      string capture_receiver, capture_method;
      vector<string> capture_arguments;
      if (parse_member_call(identity, capture_receiver, capture_method,
                            capture_arguments) &&
          trim(capture_receiver) != "_") {
        auto capture_type = inferred_expr_type(capture_receiver, env);
        bool mutates_collection = capture_type &&
            (canonical_type_name(*capture_type) == "vector" ||
             canonical_type_name(*capture_type) == "queue" ||
             starts_with(canonical_type_name(*capture_type), "vector[") ||
             starts_with(canonical_type_name(*capture_type), "queue[")) &&
            (capture_method == "push" || capture_method == "pop");
        bool mutates_object = false;
        if (capture_type) {
          vector<string> argument_types;
          for (const auto& argument : capture_arguments)
            argument_types.push_back(
                inferred_expr_type(argument, placeholder_env).value_or(""));
          if (const Method* method = resolve_method(
                  canonical_type_name(*capture_type), capture_method,
                  argument_types, true, nullptr))
            mutates_object = method->receiver_effect != Effect::Read;
        }
        if (mutates_collection || mutates_object)
          err(line, "functional placeholder cannot mutate captured binding '" +
              trim(capture_receiver) + "'");
      }
      check_expression(line, identity, placeholder_env);
      auto result = inferred_expr_type(identity, placeholder_env);
      if (!result)
        err(line, "cannot infer the result type of placeholder expression '" +
            identity + "'");
      return result;
    }

    string bound_receiver, bound_method;
    if (parse_bound_method_callable(identity, bound_receiver, bound_method)) {
      auto receiver_type = inferred_expr_type(bound_receiver, env);
      if (!receiver_type)
        err(line, "cannot statically resolve bound method receiver '" +
            bound_receiver + "'");
      string concrete = canonical_type_name(*receiver_type);
      if (domains_.count(concrete))
        err(line, "functional callable '" + identity +
            "' cannot hide a domain crossing; use explicit message or await");
      MethodResolutionFailure failure = MethodResolutionFailure::None;
      const Method* method = resolve_method(concrete, bound_method, input_types,
                                            false, &failure);
      if (!method) {
        string reason = failure == MethodResolutionFailure::MissingMethod
            ? "has no method named '" + bound_method + "'"
            : failure == MethodResolutionFailure::WrongArity
                ? "has no compatible method arity"
                : failure == MethodResolutionFailure::IncompatibleArguments
                    ? "has an incompatible method parameter type"
                    : failure == MethodResolutionFailure::Ambiguous
                        ? "has an ambiguous method overload"
                        : "is not a concrete object method receiver";
        err(line, "functional bound method '" + identity + "' " + reason);
      }
      if (method->receiver_effect != Effect::Read)
        err(line, "functional bound method '" + identity +
            "' cannot mutate or consume captured binding '" +
            bound_receiver + "'");
      if (!method->return_type)
        err(line, "functional bound method '" + identity +
            "' does not return a value");
      check_expression(line, bound_receiver, env);
      return canonical_type_name(*method->return_type);
    }

    auto local = env.find(identity);
    if (local != env.end()) {
      if (starts_with(local->second, "callable:")) {
        identity = local->second.substr(9);
      } else if (starts_with(local->second, "_generic:")) {
        return "_functional_callable_result:" + identity;
      } else {
        err(line, "callable identity '" + identity +
            "' is not statically bounded to one function");
      }
    }
    auto function = functions_.find(identity);
    if (function == functions_.end())
      err(line, "unresolved functional callable '" + identity + "'");
    if (function->second->params.size() != input_types.size())
      err(line, "functional callable '" + identity + "' expects " +
          std::to_string(function->second->params.size()) + " arguments, got " +
          std::to_string(input_types.size()));

    for (size_t index = 0; index < input_types.size(); ++index) {
      const auto& parameter = function->second->params[index];
      const auto& actual = input_types[index];
      if (!parameter.type.empty() && traits_.count(parameter.type) &&
          !unresolved_semantic_type(actual)) {
        string reason;
        if (!trait_conforms(actual, parameter.type, &reason))
          err(line, "functional callable '" + identity + "' cannot accept '" +
              actual + "': " + reason);
      } else if (!parameter.type.empty() &&
                 !unresolved_semantic_type(actual) &&
                 !same_type(parameter.type, actual)) {
        err(line, "functional callable '" + identity + "' argument " +
            std::to_string(index + 1) + " has type '" + actual +
            "', expected '" + parameter.type + "'");
      }
    }

    bool concrete = std::all_of(input_types.begin(), input_types.end(),
                                [&](const string& type) {
                                  return !unresolved_semantic_type(type) &&
                                      !traits_.count(canonical_type_name(type));
                                });
    if (concrete) {
      validate_method_requirements(line, *function->second, input_types);
      register_static_specialization(line, *function->second, input_types);
    }
    auto result = functional_callable_result(identity, input_types, env);
    if (!result && concrete)
      err(line, "cannot determine the result type of functional callable '" +
          identity + "'");
    return result;
  }

  bool check_functional_pipeline(int line, const string& expression,
                                 const TypeEnv& env) {
    auto parsed = parse_functional_pipeline(expression);
    if (!parsed) return false;
    auto source_type = inferred_expr_type(parsed->source, env);
    if (!source_type) return false;
    auto element = functional_element_type(*source_type, parsed->source);
    if (!element) return false;  // General/scalar pipeline compatibility.

    check_expression(line, parsed->source, env);
    string current_element = *element;
    bool terminal = false;
    for (size_t stage_index = 0; stage_index < parsed->stages.size(); ++stage_index) {
      const auto& stage = parsed->stages[stage_index];
      if (terminal)
        err(line, "functional terminal must be the final pipeline stage");
      auto require_arity = [&](size_t expected) {
        if (stage.arguments.size() != expected)
          err(line, string(functional_node_name(stage.kind)) +
              " stage expects " + std::to_string(expected) +
              " argument" + (expected == 1 ? "" : "s") + ", got " +
              std::to_string(stage.arguments.size()));
      };
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          require_arity(1);
          auto result = check_functional_callable(
              line, stage.arguments.front(), {current_element}, env);
          if (result) current_element = canonical_type_name(*result);
          break;
        }
        case FunctionalNodeKind::Filter: {
          require_arity(1);
          if (!unresolved_semantic_type(current_element) &&
              !copy_type(current_element))
            err(line, "filter over nontrivial element type '" + current_element +
                "' cannot produce a new collection without an explicit deep copy");
          auto result = check_functional_callable(
              line, stage.arguments.front(), {current_element}, env);
          if (result && !unresolved_semantic_type(*result) &&
              canonical_type_name(*result) != "bool")
            err(line, "filter predicate returns '" + *result +
                "'; expected 'bool'");
          break;
        }
        case FunctionalNodeKind::Reduce: {
          require_arity(2);
          check_expression(line, stage.arguments.front(), env);
          auto accumulator = inferred_expr_type(stage.arguments.front(), env);
          if (!accumulator)
            err(line, "cannot infer reduce initial accumulator type");
          auto result = check_functional_callable(
              line, stage.arguments[1], {*accumulator, current_element}, env);
          if (result && !unresolved_semantic_type(*result) &&
              !unresolved_semantic_type(*accumulator) &&
              !same_type(*result, *accumulator))
            err(line, "reduce callable returns '" + *result +
                "'; expected accumulator type '" + *accumulator + "'");
          terminal = true;
          break;
        }
        case FunctionalNodeKind::Sum:
          require_arity(0);
          if (!unresolved_semantic_type(current_element) &&
              !numeric_type(current_element))
            err(line, "sum requires numeric elements, found '" +
                current_element + "'");
          terminal = true;
          break;
        case FunctionalNodeKind::Count:
          require_arity(0);
          terminal = true;
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All: {
          if (stage.arguments.size() > 1)
            err(line, string(functional_node_name(stage.kind)) +
                " stage accepts zero or one predicate");
          string predicate_type = current_element;
          if (!stage.arguments.empty()) {
            auto result = check_functional_callable(
                line, stage.arguments.front(), {current_element}, env);
            if (result) predicate_type = *result;
          }
          if (!unresolved_semantic_type(predicate_type) &&
              canonical_type_name(predicate_type) != "bool")
            err(line, string(functional_node_name(stage.kind)) +
                " predicate returns '" + predicate_type +
                "'; expected 'bool'");
          terminal = true;
          break;
        }
        case FunctionalNodeKind::Source:
          err(line, "invalid Source functional stage");
      }
    }
    return true;
  }

  void check_expression(int line, const string& expression,
                        const std::unordered_map<string,string>& env) {
    string original = trim(expression);
    if (check_functional_pipeline(line, original, env)) return;
    string value = normalize_pipeline(std::move(original));
    string receiver, handler;
    vector<string> args;
    if (parse_member_call(value, receiver, handler, args)) {
      auto it = env.find(receiver);
      if (it != env.end() && domains_.count(it->second))
        err(line, "naked cross-domain call '" + receiver + "." + handler +
            "' requires 'message' or 'await'");
      if (it != env.end() && (it->second == "vector" || it->second == "queue" || it->second == "map" ||
          starts_with(it->second, "vector[") || starts_with(it->second, "queue[") || starts_with(it->second, "map["))) {
        if (handler == "push" && args.size() == 1) { check_expression(line, args[0], env); return; }
        if (handler == "pop" && args.empty() && (starts_with(it->second, "queue[") || starts_with(it->second, "vector["))) return;
        err(line, "invalid collection operation '" + handler + "'");
      }
      if (it != env.end() && traits_.count(it->second)) {
        const Trait* trait = traits_.at(it->second);
        auto named = std::find_if(trait->methods.begin(), trait->methods.end(),
                                  [&](const TraitMethod& method) {
                                    return method.name == handler;
                                  });
        if (named == trait->methods.end())
          err(line, "trait '" + it->second + "' has no method '" + handler + "'");
        auto method = std::find_if(trait->methods.begin(), trait->methods.end(),
                                   [&](const TraitMethod& candidate) {
                                     return candidate.name == handler &&
                                            candidate.params.size() == args.size();
                                   });
        if (method == trait->methods.end())
          err(line, "trait method '" + handler + "' expects " +
              std::to_string(named->params.size()) + " arguments");
        for (size_t index = 0; index < args.size(); ++index) {
          auto actual = inferred_expr_type(args[index], env);
          if (actual && !starts_with(*actual, "_") &&
              !method->params[index].type.empty() &&
              !same_type(*actual, method->params[index].type))
            err(line, "argument " + std::to_string(index + 1) +
                " to trait method '" + handler + "' has type '" + *actual +
                "', expected '" + method->params[index].type + "'");
          check_expression(line, args[index], env);
        }
        return;
      }
      if (it != env.end() && objects_.count(canonical_type_name(it->second))) {
        vector<string> argument_types;
        for (const auto& arg : args)
          argument_types.push_back(inferred_expr_type(arg, env).value_or(""));
        if (!resolve_method(canonical_type_name(it->second), handler, argument_types))
          err(line, "no matching method '" + canonical_type_name(it->second) + "." +
              handler + "' for supplied arguments");
      }
      check_expression(line, receiver, env);
      for (const auto& arg : args) check_expression(line, arg, env);
      return;
    }
    string ib, ii;
    if (parse_index(value, ib, ii)) {
      auto bt = inferred_expr_type(ib, env);
      if (!bt) err(line, "cannot infer indexed container type");
      if (starts_with(*bt, "vector[") || starts_with(*bt, "queue[") || *bt == "vector" || *bt == "queue") {
        auto it = inferred_expr_type(ii, env);
        if (!it || *it != "int") err(line, "vector and queue indices must be Int");
      } else if (starts_with(*bt, "map[")) {
        auto ps = split_top_level(bt->substr(4, bt->size()-5), ',');
        auto it = inferred_expr_type(ii, env);
        if (ps.size() != 2 || !it || !same_type(ps[0], *it)) err(line, "map key type mismatch");
      } else err(line, "value is not an indexable container");
      return;
    }
    string callee;
    if (parse_simple_call(value, callee, args) && callee.find('.') == string::npos) {
      if (objects_.count(callee) || callee == "Map" || callee == "Queue") {
        for (const auto& arg : args) {
          string field_name, field_value;
          if (!parse_named_argument(arg, field_name, field_value))
            err(line, "object constructor fields must be named for '" + callee + "'");
          check_expression(line, field_value, env);
        }
      } else {
        check_function_call(line, callee, args, env);
        for (const auto& arg : args) check_expression(line, arg, env);
      }
      return;
    }
    for (const auto& operators : vector<vector<string>>{{"==", "!=", "<=", ">=", "<", ">"},
                                                         {"+", "-", "*", "/"}}) {
      if (auto binary = split_binary(value, operators)) {
        check_expression(line, binary->first, env);
        check_expression(line, binary->second, env);
        return;
      }
    }
  }

  const Domain* bounded_await_target(const string& receiver,
                                     const TypeEnv& env) const {
    auto binding = env.find(receiver);
    if (binding == env.end() || starts_with(binding->second, "_")) return nullptr;
    auto domain = domains_.find(canonical_type_name(binding->second));
    return domain == domains_.end() ? nullptr : domain->second;
  }

  const Domain& require_bounded_await_target(int line, const string& receiver,
                                             const TypeEnv& env) const {
    const Domain* domain = bounded_await_target(receiver, env);
    if (!domain)
      err(line, "await target '" + receiver +
          "' cannot be statically and conservatively bounded to one concrete domain");
    return *domain;
  }

  TypeEnv check_stmts(const vector<Stmt>& statements, TypeEnv env,
                      const Domain* current, const Handler* current_handler,
                      const Function* current_function = nullptr) {
    TypeEnvVisitor check_statement = [&](const Stmt& statement,
                                         const TypeEnv& current_env) {
      if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        if (auto spawned = spawn_domain(statement.b)) {
          if (!domains_.count(*spawned))
            err(statement.line, "unknown domain in spawn: " + *spawned);
          if (current || current_function)
            err(statement.line, "spawning domains inside handlers or functions is not supported in v0.2; create them in main");
          return;
        }
        check_expression(statement.line, statement.b, current_env);
        if (statement.kind == Stmt::Kind::Assign &&
            simple_identifier(statement.a) && !current_env.count(statement.a)) {
          auto inferred = inferred_expr_type(statement.b, current_env);
          if (!inferred && starts_with(trim(statement.b), "["))
            err(statement.line, "heterogeneous or unresolved collection element type");
        }
        if (statement.kind == Stmt::Kind::Assign) {
          string base, index_expression;
          if (parse_index(statement.a, base, index_expression)) {
            auto container = current_env.find(base);
            auto value_type = inferred_expr_type(statement.b, current_env);
            if (container != current_env.end() && value_type &&
                container->second == "map" &&
                !inferred_expr_type(index_expression, current_env))
              err(statement.line, "cannot infer map key type");
          }
        }
        return;
      }

      if (statement.kind == Stmt::Kind::If || statement.kind == Stmt::Kind::While) {
        check_expression(statement.line, statement.a, current_env);
        return;
      }

      if (statement.kind == Stmt::Kind::Message) {
        for (const auto& argument : statement.args) {
          if (auto pipeline = parse_functional_pipeline(argument)) {
            auto source_type = inferred_expr_type(pipeline->source, current_env);
            if (source_type && functional_element_type(
                                   *source_type, pipeline->source) &&
                functional_pipeline_requires_materialization(*pipeline))
              err(statement.line,
                  "functional pipeline must be materialized in a local binding "
                  "before crossing a domain boundary");
          }
        }
        check_call(statement.line, statement.a, statement.b, statement.args,
                   current_env);
        for (const auto& argument : statement.args) {
          check_expression(statement.line, argument, current_env);
        }
        return;
      }

      if (statement.kind == Stmt::Kind::AwaitMessage) {
        if (current && statement.b == "self")
          err(statement.line,
              "a domain cannot await itself because handlers are non-reentrant");
        const Domain& target = require_bounded_await_target(
            statement.line, statement.b, current_env);
        for (const auto& argument : statement.args) {
          if (auto pipeline = parse_functional_pipeline(argument)) {
            auto source_type = inferred_expr_type(pipeline->source, current_env);
            if (source_type && functional_element_type(
                                   *source_type, pipeline->source) &&
                functional_pipeline_requires_materialization(*pipeline))
              err(statement.line,
                  "functional pipeline must be materialized in a local binding "
                  "before crossing a domain boundary");
          }
        }
        const Handler* handler = check_call(statement.line, statement.b,
                                            statement.c, statement.args,
                                            current_env);
        for (const auto& argument : statement.args) {
          check_expression(statement.line, argument, current_env);
        }
        if (!handler->reply_type)
          err(statement.line, "cannot await one-way handler '" + target.name + "." +
              statement.c + "'");
        if (!statement.declaration && current_env.count(statement.a) &&
            !same_type(current_env.at(statement.a), *handler->reply_type))
          err(statement.line, "await assignment to '" + statement.a +
              "' has type '" + current_env.at(statement.a) + "', expected '" +
              *handler->reply_type + "'");
        return;
      }

      if (statement.kind == Stmt::Kind::Call) {
        if (!statement.b.empty()) {
          auto receiver = current_env.find(statement.a);
          if (receiver != current_env.end() &&
              domains_.count(canonical_type_name(receiver->second)))
            err(statement.line, "naked cross-domain call '" + statement.a + "." +
                statement.b + "' requires 'message' or 'await'");
          bool collection = receiver != current_env.end() &&
              (receiver->second == "vector" || receiver->second == "queue" ||
               receiver->second == "map" || starts_with(receiver->second, "vector[") ||
               starts_with(receiver->second, "queue[") ||
               starts_with(receiver->second, "map["));
          if (!collection) {
            if (receiver != current_env.end() &&
                objects_.count(canonical_type_name(receiver->second))) {
              vector<string> argument_types;
              for (const auto& argument : statement.args)
                argument_types.push_back(
                    inferred_expr_type(argument, current_env).value_or(""));
              if (!resolve_method(canonical_type_name(receiver->second), statement.b,
                                  argument_types))
                err(statement.line, "no matching method '" + receiver->second + "." +
                    statement.b + "' for supplied arguments");
            } else if (receiver != current_env.end() && current_function &&
                       (starts_with(receiver->second, "_generic:") ||
                        traits_.count(receiver->second))) {
              bool constrained = std::any_of(
                  current_function->constraints.begin(),
                  current_function->constraints.end(), [&](const Constraint& constraint) {
                    return constraint.kind == ConstraintKind::Method &&
                           constraint.subject == statement.a &&
                           constraint.detail == statement.b &&
                           constraint.arity == statement.args.size();
                  });
              if (!constrained)
                err(statement.line, "unresolved statically dispatched method '" +
                    statement.a + "." + statement.b + "'");
            } else {
              err(statement.line,
                  "local member calls are not implemented; use a top-level function");
            }
          }
        } else {
          check_function_call(statement.line, statement.a, statement.args,
                              current_env);
        }
        if (!statement.b.empty())
          check_expression(statement.line, statement.text, current_env);
        for (const auto& argument : statement.args)
          check_expression(statement.line, argument, current_env);
        return;
      }

      if (statement.kind == Stmt::Kind::Echo) {
        for (const auto& argument : statement.args)
          check_expression(statement.line, argument, current_env);
        return;
      }

      if (statement.kind == Stmt::Kind::Reply) {
        if (!current || !current_handler || !current_handler->reply_type)
          err(statement.line,
              "reply is only valid in a handler declaring '-> Type'");
        auto actual = inferred_expr_type(statement.a, current_env);
        if (auto pipeline = parse_functional_pipeline(statement.a)) {
          auto source_type = inferred_expr_type(pipeline->source, current_env);
          if (source_type && functional_element_type(
                                 *source_type, pipeline->source) &&
              functional_pipeline_requires_materialization(*pipeline))
            err(statement.line,
                "functional pipeline must be materialized in a local binding "
                "before crossing a domain boundary");
        }
        if (!actual)
          err(statement.line, "cannot infer the type of this reply expression; add an annotation or use a statically typed value");
        if (!same_type(*actual, *current_handler->reply_type))
          err(statement.line, "reply type mismatch: handler expects '" +
              *current_handler->reply_type + "', expression has type '" + *actual +
              "'");
        check_expression(statement.line, statement.a, current_env);
        return;
      }

      if (statement.kind == Stmt::Kind::Return && current_handler &&
          !statement.a.empty())
        err(statement.line, "message handlers cannot return values; use 'reply value' in a handler declaring '-> Type'");
      if (statement.kind == Stmt::Kind::Return && !current_handler &&
          !current_function && !statement.a.empty())
        err(statement.line, "main cannot return a value");
      if (statement.kind == Stmt::Kind::Return && current_function &&
          !statement.a.empty()) {
        auto actual = inferred_expr_type(statement.a, current_env);
        if (!actual)
          err(statement.line, "cannot infer the type of this return expression in function '" +
              current_function->name + "'");
        bool trait_result_match = current_function->return_type &&
            traits_.count(canonical_type_name(*current_function->return_type)) &&
            concrete_specialization_type(*actual) &&
            trait_conforms(*actual, *current_function->return_type);
        if (current_function->return_type &&
            !starts_with(*current_function->return_type, "_") &&
            !starts_with(*actual, "_method_") && !trait_result_match &&
            !same_type(*actual, *current_function->return_type))
          err(statement.line, "function '" + current_function->name + "' returns '" +
              *actual + "' but is annotated '" + *current_function->return_type + "'");
      }
    };

    return walk_type_environment(statements, std::move(env), check_statement,
                                 true, true, true);
  }
};

// This is a Moss-level optimization plan over the authoritative typed
// functional/dataflow IR. Phase 4.5 augments those semantic nodes with
// terminal, liveness/materialization, cross-binding, and shared-source DAG
// facts. Rust generation consumes exact plan IDs and never asks an iterator
// library to recover the source structure.
class FunctionalOptimizer {
 public:
  explicit FunctionalOptimizer(Program& program) : program_(program) {}

  void run(bool enabled) {
    program_.functional_traversal_groups.clear();
    for (auto& pipeline : program_.functional_pipelines) {
      pipeline.count_uses_exact_length = false;
      pipeline.short_circuit_terminal = false;
      pipeline.virtual_upstream_pipeline_id = 0;
      pipeline.virtualized_into_pipeline_id = 0;
      pipeline.traversal_group_id = 0;
      pipeline.binding_name.clear();
      pipeline.binding_immutable = false;
      pipeline.binding_use_count = 0;
      pipeline.binding_materialization_reason.clear();
      pipeline.optimization_notes.clear();
      pipeline.fused = enabled && pipeline.fusion_eligible;
      pipeline.lowered_provenance.clear();
      for (auto& node : pipeline.nodes) {
        node.materialization_eliminated = false;
        node.materialization = FunctionalMaterializationKind::NotApplicable;
        node.escapes = false;
        node.multiple_consumers = false;
        node.barrier_required = false;
        node.dead_stage_eliminated = false;
        node.materialization_reason.clear();
      }
      if (pipeline.fused)
        pipeline.decision = "fused stages 1-" +
            std::to_string(pipeline.nodes.size() - 1) +
            "; intermediates eliminated";
      else if (!enabled && pipeline.fusion_eligible)
        pipeline.decision =
            "eager reference lowering (-O0); logical intermediates retained";

      plan_terminal_lowering(pipeline, enabled);
    }

    if (enabled) {
      for (auto& function : program_.functions) {
        std::set<string> known;
        for (const auto& parameter : function.params) known.insert(parameter.name);
        if (function.static_dispatch && !function.specializations.empty()) {
          for (const auto& specialization : function.specializations) {
            string context = functional_function_context(function, &specialization);
            plan_scope(function.body, context, known,
                       result_pipeline_id(function.result_functional_pipeline_ids,
                                          context),
                       function.result_expression);
          }
        } else {
          string context = functional_function_context(function);
          plan_scope(function.body, context, known,
                     result_pipeline_id(function.result_functional_pipeline_ids,
                                        context),
                     function.result_expression);
        }
      }
      for (auto& object : program_.objects) {
        for (auto& method : object.methods) {
          std::set<string> known{"self"};
          for (const auto& field : object.fields) known.insert(field.name);
          for (const auto& parameter : method.params) known.insert(parameter.name);
          string context = functional_method_context(object, method);
          plan_scope(method.body, context, known,
                     result_pipeline_id(method.result_functional_pipeline_ids,
                                        context),
                     method.result_expression);
        }
      }
      for (auto& domain : program_.domains) {
        for (auto& handler : domain.handlers) {
          std::set<string> known{"self"};
          for (const auto& field : domain.state) known.insert(field.name);
          for (const auto& parameter : handler.params) known.insert(parameter.name);
          plan_scope(handler.body, functional_handler_context(domain, handler),
                     known, 0, std::nullopt);
        }
      }
      if (program_.main)
        plan_scope(program_.main->body, "main", {}, 0, std::nullopt);
    }

    rebuild_provenance();
    plan_materializations(enabled);
  }

 private:
  struct ScopeOccurrence {
    size_t statement_index = 0;
    size_t pipeline_id = 0;
    Stmt* statement = nullptr;
    bool result_expression = false;
    bool new_binding = false;
    bool explicitly_immutable = false;
    string expression;
    string result_binding;
  };

  Program& program_;

  FunctionalPipeline* pipeline_by_id(size_t id) {
    auto found = std::find_if(
        program_.functional_pipelines.begin(),
        program_.functional_pipelines.end(),
        [&](const FunctionalPipeline& pipeline) {
          return pipeline.transient_id == id;
        });
    return found == program_.functional_pipelines.end() ? nullptr : &*found;
  }

  static size_t result_pipeline_id(
      const std::unordered_map<string,size_t>& ids, const string& context) {
    auto found = ids.find(context);
    return found == ids.end() ? 0 : found->second;
  }

  static size_t statement_pipeline_id(const Stmt& statement,
                                      const string& context) {
    auto found = statement.functional_pipeline_ids.find(context);
    if (found == statement.functional_pipeline_ids.end() ||
        found->second.empty())
      return 0;
    return found->second.front();
  }

  static size_t word_occurrences(const string& expression,
                                 const string& name) {
    size_t result = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0; index < expression.size();) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        ++index;
        continue;
      }
      if (ch == '"') {
        in_string = true;
        ++index;
        continue;
      }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      if (expression.substr(index, end - index) == name) ++result;
      index = end;
    }
    return result;
  }

  static bool terminal_pipeline(const FunctionalPipeline& pipeline) {
    return pipeline.nodes.size() > 1 &&
        functional_terminal_kind(pipeline.nodes.back().kind);
  }

  static bool transformation_pipeline(const FunctionalPipeline& pipeline) {
    return pipeline.nodes.size() > 1 && !terminal_pipeline(pipeline);
  }

  static bool exact_count_candidate(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() < 2 ||
        pipeline.nodes.back().kind != FunctionalNodeKind::Count)
      return false;
    return std::all_of(
        pipeline.nodes.begin() + 1, pipeline.nodes.end() - 1,
        [](const FunctionalNode& node) {
          return node.kind == FunctionalNodeKind::Map &&
              node.effects.safe_to_skip();
        });
  }

  // Sharing a traversal and omitting work have deliberately different proof
  // obligations. Potential divergence does not prevent ordinary ordered
  // fusion, but it does prevent an optimization from dropping an invocation.
  static bool callbacks_safe_to_skip(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() < 2) return true;
    return std::all_of(
        pipeline.nodes.begin() + 1, pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.effects.safe_to_skip();
        });
  }

  static bool callback_may_diverge(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() < 2) return false;
    return std::any_of(
        pipeline.nodes.begin() + 1, pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.effects.may_diverge;
        });
  }

  static bool has_map_stage(const FunctionalPipeline& pipeline) {
    return std::any_of(
        pipeline.nodes.begin(), pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.kind == FunctionalNodeKind::Map;
        });
  }

  static string barrier_reason(const FunctionalPipeline& pipeline) {
    for (const auto& node : pipeline.nodes) {
      const auto& effects = node.effects;
      if (effects.domain_write) return "observable domain WRITE";
      if (effects.domain_read) return "observable domain READ";
      if (effects.message) return "message send";
      if (effects.await) return "await";
      if (effects.external_io) return "observable callback effect";
      if (effects.local_mutation) return "observable local mutation";
      if (effects.may_fail) return "callback may fail";
      if (effects.may_diverge) return "callback may diverge";
      if (effects.unresolved) return "unresolved callback effect";
    }
    return "eager semantics required";
  }

  static string skipped_callback_barrier_reason(
      const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.size() >= 2) {
      for (auto node = pipeline.nodes.begin() + 1;
           node != pipeline.nodes.end(); ++node) {
        const auto& effects = node->effects;
        if (effects.domain_write) return "observable domain WRITE";
        if (effects.domain_read) return "observable domain READ";
        if (effects.message) return "message send";
        if (effects.await) return "await";
        if (effects.external_io) return "observable callback effect";
        if (effects.local_mutation) return "observable local mutation";
        if (effects.may_fail) return "callback may fail";
        if (effects.may_diverge) return "skipped callback may diverge";
        if (effects.unresolved) return "unresolved callback effect";
      }
    }
    return "eager semantics required";
  }

  static string count_elimination_barrier_reason(
      const FunctionalPipeline& pipeline) {
    string reason = skipped_callback_barrier_reason(pipeline);
    if (reason == "skipped callback may diverge")
      return "callback may diverge";
    return reason;
  }

  static void erase_notes_with_prefix(FunctionalPipeline& pipeline,
                                      const string& prefix) {
    pipeline.optimization_notes.erase(
        std::remove_if(
            pipeline.optimization_notes.begin(),
            pipeline.optimization_notes.end(),
            [&](const string& note) { return starts_with(note, prefix); }),
        pipeline.optimization_notes.end());
  }

  static void disable_count_elimination(FunctionalPipeline& pipeline,
                                        const string& reason,
                                        bool mapped_work) {
    pipeline.count_uses_exact_length = false;
    for (auto& node : pipeline.nodes) node.dead_stage_eliminated = false;
    erase_notes_with_prefix(pipeline, "count -> len");
    erase_notes_with_prefix(pipeline, "map stage eliminated");
    erase_notes_with_prefix(pipeline, "map elimination:");
    pipeline.optimization_notes.push_back(
        "count -> len disabled; reason: " + reason);
    if (mapped_work)
      pipeline.optimization_notes.push_back(
          "map elimination: disabled; reason: " + reason);
  }

  static void disable_short_circuit(FunctionalPipeline& pipeline,
                                    const string& reason) {
    pipeline.short_circuit_terminal = false;
    erase_notes_with_prefix(pipeline, "short-circuit ");
    pipeline.optimization_notes.push_back(
        "short-circuit " + terminal_name(pipeline) +
        " disabled; reason: " + reason);
  }

  static string terminal_name(const FunctionalPipeline& pipeline) {
    if (pipeline.nodes.empty()) return "terminal";
    string name = functional_node_name(pipeline.nodes.back().kind);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return name;
  }

  static bool pipeline_requires_traversal(
      const FunctionalPipeline& pipeline) {
    return !pipeline.count_uses_exact_length;
  }

  void plan_terminal_lowering(FunctionalPipeline& pipeline, bool enabled) {
    bool count_shape = pipeline.nodes.size() >= 2 &&
        pipeline.nodes.back().kind == FunctionalNodeKind::Count &&
        std::all_of(pipeline.nodes.begin() + 1, pipeline.nodes.end() - 1,
                    [](const FunctionalNode& node) {
                      return node.kind == FunctionalNodeKind::Map;
                    });
    if (enabled && pipeline.fusion_eligible &&
        exact_count_candidate(pipeline)) {
      pipeline.count_uses_exact_length = true;
      for (auto& node : pipeline.nodes)
        if (node.kind == FunctionalNodeKind::Map)
          node.dead_stage_eliminated = true;
      pipeline.optimization_notes.push_back(
          "count -> len; reason: exact source cardinality known");
      if (pipeline.nodes.size() > 2)
        pipeline.optimization_notes.push_back(
            "map stage eliminated; reason: output unused and callback is "
            "pure/non-failing/non-divergent");
    } else if (enabled && count_shape) {
      disable_count_elimination(
          pipeline, count_elimination_barrier_reason(pipeline),
          has_map_stage(pipeline));
    }

    if (pipeline.nodes.empty()) return;
    FunctionalNodeKind terminal = pipeline.nodes.back().kind;
    if (terminal != FunctionalNodeKind::Any &&
        terminal != FunctionalNodeKind::All)
      return;
    string name = terminal == FunctionalNodeKind::Any ? "any" : "all";
    if (enabled && pipeline.fusion_eligible &&
        callbacks_safe_to_skip(pipeline)) {
      pipeline.short_circuit_terminal = true;
      pipeline.optimization_notes.push_back(
          "short-circuit " + name + " enabled");
    } else {
      disable_short_circuit(
          pipeline, enabled
              ? skipped_callback_barrier_reason(pipeline)
              : "eager -O0 reference traversal");
    }
  }

  static bool statement_defines(const Stmt& statement, const string& name) {
    return (statement.kind == Stmt::Kind::Assign ||
            statement.kind == Stmt::Kind::Let ||
            statement.kind == Stmt::Kind::Var ||
            statement.kind == Stmt::Kind::AwaitMessage) &&
        trim(statement.a) == name;
  }

  static bool occurrence_is_terminal_assignment(
      const ScopeOccurrence& occurrence, const FunctionalPipeline& pipeline) {
    return occurrence.statement && occurrence.new_binding &&
        !occurrence.result_binding.empty() && terminal_pipeline(pipeline) &&
        (occurrence.statement->kind == Stmt::Kind::Let ||
         occurrence.statement->kind == Stmt::Kind::Assign);
  }

  vector<ScopeOccurrence> collect_scope_occurrences(
      vector<Stmt>& body, const string& context,
      std::set<string> known_bindings, size_t result_id,
      const std::optional<string>& result_expression) {
    vector<ScopeOccurrence> occurrences;
    for (size_t index = 0; index < body.size(); ++index) {
      Stmt& statement = body[index];
      if (statement.indent != 0) continue;
      size_t id = statement_pipeline_id(statement, context);
      bool binding_statement = statement.kind == Stmt::Kind::Assign ||
          statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var;
      bool simple_binding = binding_statement && plain_identifier(statement.a);
      bool is_new = simple_binding && !known_bindings.count(statement.a);
      if (id) {
        ScopeOccurrence occurrence;
        occurrence.statement_index = index;
        occurrence.pipeline_id = id;
        occurrence.statement = &statement;
        occurrence.new_binding = is_new;
        occurrence.explicitly_immutable =
            statement.kind == Stmt::Kind::Let;
        occurrence.expression = statement.b;
        if (simple_binding) occurrence.result_binding = statement.a;
        occurrences.push_back(std::move(occurrence));
      }
      if (simple_binding || statement.kind == Stmt::Kind::AwaitMessage)
        known_bindings.insert(statement.a);
      if (statement.kind == Stmt::Kind::If)
        for (const auto& joined : statement.joined_types)
          known_bindings.insert(joined.first);
    }
    if (result_id && result_expression) {
      ScopeOccurrence result;
      result.statement_index = body.size();
      result.pipeline_id = result_id;
      result.result_expression = true;
      result.expression = *result_expression;
      occurrences.push_back(std::move(result));
    }
    return occurrences;
  }

  size_t later_binding_uses(const vector<Stmt>& body, size_t producer_index,
                            const std::optional<string>& result_expression,
                            const string& binding,
                            bool& reassigned) const {
    size_t uses = 0;
    reassigned = false;
    for (size_t index = producer_index + 1; index < body.size(); ++index) {
      const Stmt& statement = body[index];
      size_t count = word_occurrences(statement.text, binding);
      if (statement_defines(statement, binding)) {
        reassigned = true;
        if (count) --count;
      }
      uses += count;
    }
    if (result_expression)
      uses += word_occurrences(*result_expression, binding);
    return uses;
  }

  void plan_cross_binding_fusion(
      vector<Stmt>& body, vector<ScopeOccurrence>& occurrences,
      const std::optional<string>& result_expression) {
    for (size_t index = 0; index < occurrences.size(); ++index) {
      ScopeOccurrence& producer_occurrence = occurrences[index];
      if (!producer_occurrence.statement ||
          producer_occurrence.result_binding.empty())
        continue;
      FunctionalPipeline* producer =
          pipeline_by_id(producer_occurrence.pipeline_id);
      if (!producer || !transformation_pipeline(*producer) ||
          producer->virtual_upstream_pipeline_id)
        continue;

      const string& binding = producer_occurrence.result_binding;
      bool reassigned = false;
      size_t uses = later_binding_uses(
          body, producer_occurrence.statement_index, result_expression,
          binding, reassigned);
      bool immutable = producer_occurrence.explicitly_immutable ||
          (producer_occurrence.new_binding && !reassigned &&
           producer_occurrence.statement->kind == Stmt::Kind::Assign);
      producer->binding_name = binding;
      producer->binding_immutable = immutable;
      producer->binding_use_count = uses;

      if (!immutable)
        producer->binding_materialization_reason =
            "binding is mutable or reassigned";
      else if (uses > 1)
        producer->binding_materialization_reason = "multiple consumers";
      else if (uses == 0)
        producer->binding_materialization_reason =
            "escapes optimization scope";
      else
        producer->binding_materialization_reason =
            "use is outside the adjacent functional region";

      if (!immutable || uses != 1 || index + 1 >= occurrences.size())
        continue;
      ScopeOccurrence& consumer_occurrence = occurrences[index + 1];
      bool adjacent = consumer_occurrence.result_expression
          ? producer_occurrence.statement_index + 1 == body.size()
          : consumer_occurrence.statement_index ==
                producer_occurrence.statement_index + 1;
      if (!adjacent) {
        bool observable = false;
        size_t end = consumer_occurrence.result_expression
            ? body.size() : consumer_occurrence.statement_index;
        for (size_t statement_index = producer_occurrence.statement_index + 1;
             statement_index < end; ++statement_index) {
          Stmt::Kind kind = body[statement_index].kind;
          observable = observable || kind == Stmt::Kind::Echo ||
              kind == Stmt::Kind::Message ||
              kind == Stmt::Kind::AwaitMessage ||
              kind == Stmt::Kind::Call || kind == Stmt::Kind::If ||
              kind == Stmt::Kind::While;
        }
        producer->binding_materialization_reason = observable
            ? "intervening observable effect"
            : "intervening statement prevents one lexical region";
        continue;
      }
      if (consumer_occurrence.statement &&
          (consumer_occurrence.statement->kind == Stmt::Kind::Assign ||
           consumer_occurrence.statement->kind == Stmt::Kind::Let ||
           consumer_occurrence.statement->kind == Stmt::Kind::Var) &&
          !consumer_occurrence.new_binding) {
        producer->binding_materialization_reason =
            "consumer assigns an existing binding";
        continue;
      }
      FunctionalPipeline* consumer =
          pipeline_by_id(consumer_occurrence.pipeline_id);
      if (!consumer || trim(consumer->source_expression) != binding) {
        producer->binding_materialization_reason =
            "use is outside a compatible functional consumer";
        continue;
      }
      if (!producer->fusion_eligible || !consumer->fusion_eligible) {
        producer->binding_materialization_reason =
            "functional barrier: " +
            barrier_reason(!producer->fusion_eligible ? *producer : *consumer);
        continue;
      }
      if (
          producer->nodes.empty() || consumer->nodes.empty() ||
          !producer->nodes.front().effects.fusion_safe() ||
          !consumer->nodes.front().effects.fusion_safe() ||
          consumer->virtual_upstream_pipeline_id) {
        producer->binding_materialization_reason =
            "source effect or existing dataflow link requires materialization";
        continue;
      }

      producer->virtualized_into_pipeline_id = consumer->transient_id;
      consumer->virtual_upstream_pipeline_id = producer->transient_id;
      producer->fused = true;
      producer->binding_materialization_reason.clear();
      consumer->fused = true;

      // Terminal plans were formed before lexical graphs were linked. Recheck
      // any transform that can now omit upstream callback executions against
      // the combined graph rather than just the terminal statement.
      if (consumer->count_uses_exact_length) {
        bool upstream_preserves_exact_count = std::all_of(
            producer->nodes.begin() + 1, producer->nodes.end(),
            [](const FunctionalNode& node) {
              return node.kind == FunctionalNodeKind::Map &&
                  node.effects.safe_to_skip();
            });
        if (upstream_preserves_exact_count) {
          for (auto& node : producer->nodes)
            if (node.kind == FunctionalNodeKind::Map)
              node.dead_stage_eliminated = true;
          if (has_map_stage(*producer) &&
              std::none_of(
                  consumer->optimization_notes.begin(),
                  consumer->optimization_notes.end(),
                  [](const string& note) {
                    return starts_with(note, "map stage eliminated");
                  }))
            consumer->optimization_notes.push_back(
                "map stage eliminated; reason: output unused and callback is "
                "pure/non-failing/non-divergent");
        } else {
          string reason = callback_may_diverge(*producer)
              ? "callback may diverge"
              : "upstream stage changes cardinality";
          for (auto& node : producer->nodes)
            node.dead_stage_eliminated = false;
          disable_count_elimination(
              *consumer, reason,
              has_map_stage(*producer) || has_map_stage(*consumer));
        }
      }
      if (consumer->short_circuit_terminal &&
          !callbacks_safe_to_skip(*producer))
        disable_short_circuit(
            *consumer, skipped_callback_barrier_reason(*producer));

      producer->optimization_notes.push_back(
          "intermediate " + binding +
          ": virtualized across immutable binding");
      consumer->optimization_notes.push_back(
          "cross-binding fusion source: " + binding);
      consumer->decision = "fused across immutable binding '" + binding +
          "'; intermediates eliminated";
    }
  }

  static bool captures_any_result(
      const FunctionalPipeline& pipeline,
      const std::set<string>& result_bindings) {
    for (const auto& node : pipeline.nodes)
      for (const auto& capture : node.captures)
        if (result_bindings.count(capture)) return true;
    return false;
  }

  void plan_shared_traversals(vector<ScopeOccurrence>& occurrences,
                              const string& context) {
    for (size_t first = 0; first < occurrences.size();) {
      ScopeOccurrence& initial = occurrences[first];
      FunctionalPipeline* initial_pipeline = pipeline_by_id(initial.pipeline_id);
      if (!initial_pipeline || initial.result_expression ||
          !occurrence_is_terminal_assignment(initial, *initial_pipeline) ||
          initial_pipeline->virtual_upstream_pipeline_id ||
          !initial_pipeline->fusion_eligible ||
          initial_pipeline->nodes.empty() ||
          !initial_pipeline->nodes.front().effects.fusion_safe() ||
          !plain_identifier(trim(initial_pipeline->source_expression))) {
        ++first;
        continue;
      }

      vector<size_t> members{first};
      for (size_t next = first + 1; next < occurrences.size(); ++next) {
        ScopeOccurrence& occurrence = occurrences[next];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        const ScopeOccurrence& previous = occurrences[members.back()];
        if (!pipeline || occurrence.result_expression ||
            occurrence.statement_index != previous.statement_index + 1 ||
            !occurrence_is_terminal_assignment(occurrence, *pipeline) ||
            pipeline->virtual_upstream_pipeline_id ||
            !pipeline->fusion_eligible ||
            pipeline->nodes.empty() ||
            !pipeline->nodes.front().effects.fusion_safe() ||
            trim(pipeline->source_expression) !=
                trim(initial_pipeline->source_expression))
          break;
        members.push_back(next);
      }
      if (members.size() < 2) {
        ++first;
        continue;
      }

      std::set<string> result_bindings;
      bool useful_traversal = false;
      for (size_t member : members) {
        ScopeOccurrence& occurrence = occurrences[member];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        result_bindings.insert(occurrence.result_binding);
        useful_traversal = useful_traversal ||
            pipeline_requires_traversal(*pipeline);
      }
      bool dependent = false;
      for (size_t member : members) {
        FunctionalPipeline* pipeline =
            pipeline_by_id(occurrences[member].pipeline_id);
        dependent = dependent || captures_any_result(*pipeline, result_bindings);
        for (const auto& result_binding : result_bindings)
          dependent = dependent ||
              word_occurrences(pipeline->expression, result_binding) != 0;
      }
      if (!useful_traversal || dependent) {
        first = members.back() + 1;
        continue;
      }

      FunctionalTraversalGroup group;
      group.transient_id = program_.functional_traversal_groups.size() + 1;
      group.context = context;
      group.line = initial.statement->line;
      group.source_expression = trim(initial_pipeline->source_expression);
      group.source_type = initial_pipeline->source_type;
      group.semantic_identity = context + "@" + std::to_string(group.line) +
          ":shared-source";
      for (size_t member : members) {
        ScopeOccurrence& occurrence = occurrences[member];
        FunctionalPipeline* pipeline = pipeline_by_id(occurrence.pipeline_id);
        pipeline->traversal_group_id = group.transient_id;
        pipeline->optimization_notes.push_back(
            "source traversal shared; group %" +
            std::to_string(group.transient_id));
        FunctionalTraversalConsumer consumer;
        consumer.pipeline_id = pipeline->transient_id;
        consumer.result_binding = occurrence.result_binding;
        consumer.line = occurrence.statement->line;
        consumer.mutable_binding =
            occurrence.statement->kind == Stmt::Kind::Assign;
        group.consumers.push_back(std::move(consumer));
        group.provenance.insert(group.provenance.end(),
                                pipeline->lowered_provenance.begin(),
                                pipeline->lowered_provenance.end());
      }
      std::ostringstream decision;
      decision << "source traversal shared; consumers:";
      for (size_t member : members) {
        FunctionalPipeline* pipeline = pipeline_by_id(
            occurrences[member].pipeline_id);
        decision << " " << terminal_name(*pipeline);
      }
      group.decision = decision.str();
      program_.functional_traversal_groups.push_back(std::move(group));
      first = members.back() + 1;
    }
  }

  void plan_scope(vector<Stmt>& body, const string& context,
                  const std::set<string>& known_bindings, size_t result_id,
                  const std::optional<string>& result_expression) {
    auto occurrences = collect_scope_occurrences(
        body, context, known_bindings, result_id, result_expression);
    plan_cross_binding_fusion(body, occurrences, result_expression);
    plan_shared_traversals(occurrences, context);
  }

  void rebuild_provenance() {
    for (auto& pipeline : program_.functional_pipelines) {
      pipeline.lowered_provenance.clear();
      if (pipeline.virtual_upstream_pipeline_id) {
        FunctionalPipeline* upstream =
            pipeline_by_id(pipeline.virtual_upstream_pipeline_id);
        if (upstream)
          for (const auto& node : upstream->nodes)
            pipeline.lowered_provenance.insert(
                pipeline.lowered_provenance.end(), node.provenance.begin(),
                node.provenance.end());
      }
      for (const auto& node : pipeline.nodes)
        pipeline.lowered_provenance.insert(
            pipeline.lowered_provenance.end(), node.provenance.begin(),
            node.provenance.end());
    }
    for (auto& group : program_.functional_traversal_groups) {
      group.provenance.clear();
      for (const auto& consumer : group.consumers) {
        FunctionalPipeline* pipeline = pipeline_by_id(consumer.pipeline_id);
        if (pipeline)
          group.provenance.insert(group.provenance.end(),
                                  pipeline->lowered_provenance.begin(),
                                  pipeline->lowered_provenance.end());
      }
    }
  }

  void plan_materializations(bool enabled) {
    for (auto& pipeline : program_.functional_pipelines) {
      vector<size_t> logical_nodes;
      for (size_t index = 0; index < pipeline.nodes.size(); ++index)
        if (pipeline.nodes[index].logical_materialization)
          logical_nodes.push_back(index);
      for (size_t position = 0; position < logical_nodes.size(); ++position) {
        FunctionalNode& node = pipeline.nodes[logical_nodes[position]];
        bool last = position + 1 == logical_nodes.size();
        if (node.dead_stage_eliminated) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason =
              "mapped output is dead before exact-length count";
        } else if (pipeline.virtualized_into_pipeline_id) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason =
              "single immutable consumer in the same lexical scope";
        } else if (!enabled || !pipeline.fused) {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.barrier_required = true;
          node.materialization_reason = !enabled
              ? "eager -O0 reference semantics"
              : "observable or failure-order barrier requires eager completion";
        } else if (terminal_pipeline(pipeline) || !last) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason =
              "single in-graph consumer shares the traversal";
        } else {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.escapes = true;
          node.materialization_reason = "escapes optimization scope";
        }

        if (last && !pipeline.binding_name.empty() &&
            !pipeline.virtualized_into_pipeline_id) {
          node.materialization = FunctionalMaterializationKind::Materialize;
          node.materialization_eliminated = false;
          node.escapes = true;
          if (pipeline.binding_use_count > 1) {
            node.multiple_consumers = true;
            node.materialization_reason = "multiple consumers";
          } else if (!pipeline.binding_materialization_reason.empty())
            node.materialization_reason =
                pipeline.binding_materialization_reason;
          if (node.materialization_reason.find("effect") != string::npos ||
              node.materialization_reason.find("barrier") != string::npos)
            node.barrier_required = true;
        }
        node.materialization_eliminated =
            node.materialization == FunctionalMaterializationKind::Virtual;
      }

      if (!pipeline.binding_name.empty() &&
          !pipeline.virtualized_into_pipeline_id && !logical_nodes.empty()) {
        const FunctionalNode& final = pipeline.nodes[logical_nodes.back()];
        pipeline.optimization_notes.push_back(
            "intermediate " + pipeline.binding_name + ": " +
            functional_materialization_name(final.materialization) +
            "; reason: " + final.materialization_reason);
      }
    }
  }
};

static string observable_effect_label(const ObservableEffects& effects) {
  vector<string> labels;
  if (effects.fusion_safe()) labels.push_back("PURE");
  if (effects.local_capture_read) labels.push_back("CAPTURE_READ");
  if (effects.local_mutation) labels.push_back("LOCAL_WRITE");
  if (effects.domain_read) labels.push_back("DOMAIN_READ");
  if (effects.domain_write) labels.push_back("DOMAIN_WRITE");
  if (effects.message) labels.push_back("MESSAGE");
  if (effects.await) labels.push_back("AWAIT");
  if (effects.external_io) labels.push_back("IO");
  if (effects.may_fail) labels.push_back("MAY_FAIL");
  if (effects.may_diverge) labels.push_back("MAY_DIVERGE");
  if (effects.unresolved) labels.push_back("UNRESOLVED");
  std::ostringstream out;
  for (size_t index = 0; index < labels.size(); ++index) {
    if (index) out << "+";
    out << labels[index];
  }
  return out.str();
}

static void dump_functional_ir(std::ostream& out, const Program& program,
                               bool decisions_only) {
  for (const auto& pipeline : program.functional_pipelines) {
    out << "Pipeline %" << pipeline.transient_id << " [" << pipeline.context
        << "] semantic=" << pipeline.semantic_identity
        << " line " << pipeline.line << "\n";
    if (!decisions_only) {
      for (const auto& node : pipeline.nodes) {
        out << "  %" << node.transient_id << " " << functional_node_name(node.kind)
            << " " << node.input_type;
        if (!node.output_type.empty() && node.output_type != node.input_type)
          out << " -> " << node.output_type;
        if (!node.callable_identity.empty())
          out << " callable=" << node.callable_identity;
        out << " " << observable_effect_label(node.effects)
            << " span=" << node.span.line << ":" << node.span.stage
            << " origin=" << node.semantic_identity;
        if (!node.captures.empty()) {
          out << " captures=";
          for (size_t index = 0; index < node.captures.size(); ++index) {
            if (index) out << ",";
            out << node.captures[index];
          }
        }
        if (node.materialization_eliminated)
          out << " materialization=eliminated plan=virtual";
        else if (node.logical_materialization) {
          out << " materialization=logical plan="
              << functional_materialization_name(node.materialization);
        }
        if (node.dead_stage_eliminated) out << " dead-stage=eliminated";
        if (node.escapes) out << " escapes=yes";
        if (node.multiple_consumers) out << " multiple-consumers=yes";
        if (node.barrier_required) out << " barrier-required=yes";
        if (!node.materialization_reason.empty())
          out << " reason=" << node.materialization_reason;
        out << "\n";
      }
      out << "  facts: element-independent="
          << (pipeline.element_independent ? "yes" : "no")
          << " deterministic=" << (pipeline.deterministic ? "yes" : "no")
          << " reduction-compatible="
          << (pipeline.reduction_compatible ? "yes" : "no") << "\n";
    }
    for (const auto& note : pipeline.optimization_notes)
      out << "  note: " << note << "\n";
    out << "  decision: " << pipeline.decision << "\n\n";
  }
  for (const auto& group : program.functional_traversal_groups) {
    out << "DataflowGroup %" << group.transient_id << " [" << group.context
        << "] semantic=" << group.semantic_identity << " line " << group.line
        << "\n  Source " << group.source_type << " expression="
        << group.source_expression << "\n";
    for (const auto& consumer : group.consumers) {
      auto pipeline = std::find_if(
          program.functional_pipelines.begin(),
          program.functional_pipelines.end(),
          [&](const FunctionalPipeline& candidate) {
            return candidate.transient_id == consumer.pipeline_id;
          });
      out << "  -> Pipeline %" << consumer.pipeline_id << " binding="
          << consumer.result_binding;
      if (pipeline != program.functional_pipelines.end())
        out << " terminal=" << functional_node_name(pipeline->nodes.back().kind);
      out << " line " << consumer.line << "\n";
    }
    if (!decisions_only) {
      out << "  provenance:";
      for (const auto& origin : group.provenance) out << " " << origin;
      out << "\n";
    }
    out << "  decision: " << group.decision << "\n\n";
  }
}

enum class DomainLowering {
  Mailbox,
  DirectMutex,
  DirectRwLock,
  DirectAtomic,
  ClusterLocal
};

struct HandlerEffectSummary {
  Effect state_effect = Effect::Read;
  bool touches_state = false;
  bool externally_observable = false;
  std::set<string> read_fields;
  std::set<string> written_fields;
};

enum class AtomicActionKind { Load, Store, FetchAdd, FetchSub, FetchXor, Swap };

struct AtomicHandlerPlan {
  AtomicActionKind action = AtomicActionKind::Load;
  string field;
  string operand;
  string result_expression;
};

struct BatchedSendRegion {
  int first_line = 0;
  size_t count = 0;
  string receiver;
  string target_domain;
};

struct CoalescedLockRegion {
  int first_line = 0;
  size_t count = 0;
  string receiver;
  string target_domain;
  bool shared_read = false;
};

struct OptimizationPlan {
  bool optimizations_enabled = false;
  std::unordered_map<string, DomainLowering> domain_lowerings;
  std::unordered_map<string, HandlerEffectSummary> handler_effects;
  std::unordered_map<string, AtomicHandlerPlan> atomic_handlers;
  vector<BatchedSendRegion> batched_send_regions;
  vector<CoalescedLockRegion> coalesced_lock_regions;
  vector<vector<string>> domain_clusters;

  std::optional<size_t> cluster_for(const string& domain) const {
    for (size_t index = 0; index < domain_clusters.size(); ++index) {
      const auto& members = domain_clusters[index];
      if (std::find(members.begin(), members.end(), domain) != members.end()) return index;
    }
    return std::nullopt;
  }

  bool same_cluster(const string& left, const string& right) const {
    auto a = cluster_for(left), b = cluster_for(right);
    return a && b && *a == *b;
  }

  DomainLowering lowering_for(const string& domain) const {
    auto found = domain_lowerings.find(domain);
    return found == domain_lowerings.end() ? DomainLowering::Mailbox : found->second;
  }

  DomainLowering lowering_for(const Domain& domain) const {
    return lowering_for(domain.name);
  }

  const BatchedSendRegion* batch_at(int line) const {
    auto found = std::find_if(batched_send_regions.begin(), batched_send_regions.end(),
                              [&](const BatchedSendRegion& region) {
                                return region.first_line == line;
                              });
    return found == batched_send_regions.end() ? nullptr : &*found;
  }

  const CoalescedLockRegion* coalesced_at(int line) const {
    auto found = std::find_if(coalesced_lock_regions.begin(), coalesced_lock_regions.end(),
                              [&](const CoalescedLockRegion& region) {
                                return region.first_line == line;
                              });
    return found == coalesced_lock_regions.end() ? nullptr : &*found;
  }

  bool needs_mailbox_runtime() const {
    if (!optimizations_enabled) return true;
    if (!domain_clusters.empty()) return true;
    return std::any_of(domain_lowerings.begin(), domain_lowerings.end(),
                       [](const auto& entry) {
                         return entry.second == DomainLowering::Mailbox;
                       });
  }

  bool has_lowering(DomainLowering lowering) const {
    return std::any_of(domain_lowerings.begin(), domain_lowerings.end(),
                       [&](const auto& entry) { return entry.second == lowering; });
  }

};

// Builds an explicit backend plan without changing Moss's message semantics. The
// order here is intentional: an explicit cluster wins first, then a complete
// atomic domain, then direct lock-backed dispatch. RwLock is selected only when a
// real state-reading handler can benefit; write-only and uncertain domains retain
// Mutex. Separate region facts record conservative batching and lock coalescing,
// leaving Rust generation to execute (rather than rediscover) these decisions.
class BackendOptimizer {
 public:
  explicit BackendOptimizer(const Program& program) : program_(program) {
    for (const auto& object : program_.objects) objects_[object.name] = &object;
    for (const auto& domain : program_.domains) domains_[domain.name] = &domain;
  }

  OptimizationPlan run(bool enabled, const vector<vector<string>>& requested_clusters = {}) const {
    OptimizationPlan plan;
    plan.optimizations_enabled = enabled;
    plan.domain_clusters = requested_clusters;
    validate_clusters(plan);

    for (const auto& domain : program_.domains) {
      plan.domain_lowerings[domain.name] = plan.cluster_for(domain.name)
          ? DomainLowering::ClusterLocal : DomainLowering::Mailbox;
      for (const auto& handler : domain.handlers)
        plan.handler_effects[handler_key(domain, handler)] =
            analyze_handler_effects(domain, handler);
    }

    std::set<string> asynchronously_called;
    bool call_graph_complete = true;
    for (const auto& domain : program_.domains) {
      for (const auto& handler : domain.handlers) {
        std::unordered_map<string, string> types;
        types["self"] = domain.name;
        for (const auto& field : domain.state) types[field.name] = field.type;
        for (const auto& param : handler.params) types[param.name] = param.type;
        scan_calls(handler.body, types, asynchronously_called, call_graph_complete);
      }
    }
    if (program_.main) {
      std::unordered_map<string, string> types;
      scan_calls(program_.main->body, types, asynchronously_called, call_graph_complete);
    }
    // Local functions can contain domain communication even though their calls are
    // ordinary Moss calls. Scan their bodies as part of the whole-program plan so a
    // message hidden behind a function cannot accidentally be promoted to direct
    // shared-memory dispatch. A function call itself remains a conservative barrier
    // because this pass does not yet build a typed interprocedural call graph.
    for (const auto& function : program_.functions) {
      std::unordered_map<string, string> types;
      for (const auto& param : function.params) types[param.name] = param.type;
      scan_calls(function.body, types, asynchronously_called, call_graph_complete);
      if (function.result_expression) call_graph_complete = false;
    }

    if (!enabled) return plan;

    for (const auto& domain : program_.domains) {
      if (plan.cluster_for(domain.name)) continue;

      std::unordered_map<string, AtomicHandlerPlan> atomic_handlers;
      if (atomic_domain_plan(domain, atomic_handlers)) {
        plan.domain_lowerings[domain.name] = DomainLowering::DirectAtomic;
        plan.atomic_handlers.insert(atomic_handlers.begin(), atomic_handlers.end());
        continue;
      }

      if (!call_graph_complete || asynchronously_called.count(domain.name) ||
          !domain_is_direct_candidate(domain))
        continue;

      bool has_read_handler = false;
      bool rwlock_safe = true;
      for (const auto& handler : domain.handlers) {
        const auto& effects = plan.handler_effects.at(handler_key(domain, handler));
        has_read_handler = has_read_handler ||
            (effects.state_effect == Effect::Read && effects.touches_state);
        rwlock_safe = rwlock_safe && !effects.externally_observable;
      }
      plan.domain_lowerings[domain.name] = has_read_handler && rwlock_safe
          ? DomainLowering::DirectRwLock : DomainLowering::DirectMutex;
    }

    plan_batched_sends(plan);
    plan_coalesced_locks(plan);
    return plan;
  }

 private:
  const Program& program_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const Domain*> domains_;

  static string handler_key(const Domain& domain, const Handler& handler) {
    return domain.name + "." + handler.name;
  }

  static Effect combine_effect(Effect left, Effect right) {
    if (left == Effect::Consume || right == Effect::Consume) return Effect::Consume;
    if (left == Effect::Write || right == Effect::Write) return Effect::Write;
    return Effect::Read;
  }

  static string root_name(const string& expression) {
    string value = trim(expression);
    if (starts_with(value, "self.")) value = trim(value.substr(5));
    size_t end = 0;
    while (end < value.size() &&
           (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_'))
      ++end;
    return value.substr(0, end);
  }

  static bool expression_mentions(const string& expression, const string& name) {
    for (size_t index = 0; index < expression.size();) {
      if (!(std::isalpha(static_cast<unsigned char>(expression[index])) ||
            expression[index] == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      if (expression.substr(index, end - index) == name) return true;
      index = end;
    }
    return false;
  }

  static bool expression_mentions_unqualified(const string& expression,
                                               const string& name) {
    for (size_t index = 0; index < expression.size();) {
      if (!(std::isalpha(static_cast<unsigned char>(expression[index])) ||
            expression[index] == '_')) {
        ++index;
        continue;
      }
      size_t start = index;
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      size_t previous = start;
      while (previous > 0 &&
             std::isspace(static_cast<unsigned char>(expression[previous - 1])))
        --previous;
      if (expression.substr(start, end - start) == name &&
          (previous == 0 || expression[previous - 1] != '.'))
        return true;
      index = end;
    }
    return false;
  }

  static bool expression_mentions_state(const string& expression,
                                        const string& name) {
    if (expression_mentions_unqualified(expression, name)) return true;
    string compact;
    compact.reserve(expression.size());
    bool in_string = false;
    for (char ch : expression) {
      if (ch == '"') in_string = !in_string;
      if (!in_string && std::isspace(static_cast<unsigned char>(ch))) continue;
      compact.push_back(ch);
    }
    return expression_mentions(compact, "self") &&
           compact.find("self." + name) != string::npos;
  }

  static bool expression_is_obviously_pure(const string& expression) {
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0; index < expression.size(); ++index) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        continue;
      }
      if (ch == '"') {
        in_string = true;
        continue;
      }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) continue;
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      size_t next = end;
      while (next < expression.size() &&
             std::isspace(static_cast<unsigned char>(expression[next])))
        ++next;
      if (next < expression.size() && expression[next] == '(') return false;
      index = end - 1;
    }
    return true;
  }

  static bool batch_payload_is_total(const string& expression) {
    string value = trim(expression);
    if (value == "true" || value == "false") return true;
    if (plain_identifier(value)) return true;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') return true;
    char* end = nullptr;
    (void)std::strtoll(value.c_str(), &end, 10);
    if (end && *end == '\0' && end != value.c_str()) return true;
    end = nullptr;
    (void)std::strtod(value.c_str(), &end);
    if (end && *end == '\0' && end != value.c_str()) return true;
    // A chain of field projections cannot call user code or fail after the
    // checker has established each concrete field.
    bool expect_identifier = true;
    for (size_t index = 0; index < value.size();) {
      if (expect_identifier) {
        if (!(std::isalpha(static_cast<unsigned char>(value[index])) ||
              value[index] == '_'))
          return false;
        while (index < value.size() &&
               (std::isalnum(static_cast<unsigned char>(value[index])) ||
                value[index] == '_'))
          ++index;
        expect_identifier = false;
      } else {
        if (value[index] != '.') return false;
        ++index;
        expect_identifier = true;
      }
    }
    return !expect_identifier;
  }

  static std::optional<std::pair<string, string>> split_binary(
      const string& expression, const vector<string>& operators) {
    int parens = 0, braces = 0, brackets = 0;
    bool in_string = false;
    for (size_t index = expression.size(); index-- > 0;) {
      char ch = expression[index];
      if (in_string) {
        if (ch == '"' && (index == 0 || expression[index - 1] != '\\'))
          in_string = false;
        continue;
      }
      if (ch == '"') { in_string = true; continue; }
      if (ch == ')') ++parens;
      else if (ch == '(') --parens;
      else if (ch == '}') ++braces;
      else if (ch == '{') --braces;
      else if (ch == ']') ++brackets;
      else if (ch == '[') --brackets;
      if (parens != 0 || braces != 0 || brackets != 0) continue;
      for (const auto& op : operators) {
        if (index + op.size() <= expression.size() &&
            expression.compare(index, op.size(), op) == 0)
          return std::make_pair(trim(expression.substr(0, index)),
                                trim(expression.substr(index + op.size())));
      }
    }
    return std::nullopt;
  }

  static void record_reads(const string& expression, const Domain& domain,
                           HandlerEffectSummary& summary) {
    for (const auto& field : domain.state) {
      if (!expression_mentions_state(expression, field.name)) continue;
      summary.touches_state = true;
      summary.read_fields.insert(field.name);
    }
  }

  HandlerEffectSummary analyze_handler_effects(const Domain& domain,
                                               const Handler& handler) const {
    HandlerEffectSummary summary;
    for (const auto& statement : handler.body) {
      switch (statement.kind) {
        case Stmt::Kind::Assign: {
          string root = root_name(statement.a);
          auto state_field = std::find_if(domain.state.begin(), domain.state.end(),
              [&](const Field& field) { return field.name == root; });
          if (state_field != domain.state.end()) {
            summary.touches_state = true;
            summary.written_fields.insert(root);
            summary.state_effect = combine_effect(summary.state_effect, Effect::Write);
          } else {
            record_reads(statement.a, domain, summary);
          }
          record_reads(statement.b, domain, summary);
          if (!expression_is_obviously_pure(statement.b))
            summary.externally_observable = true;
          break;
        }
        case Stmt::Kind::If:
        case Stmt::Kind::While:
          record_reads(statement.a, domain, summary);
          if (!expression_is_obviously_pure(statement.a))
            summary.externally_observable = true;
          break;
        case Stmt::Kind::Let:
        case Stmt::Kind::Var:
          record_reads(statement.b, domain, summary);
          if (!expression_is_obviously_pure(statement.b))
            summary.externally_observable = true;
          break;
        case Stmt::Kind::Reply:
        case Stmt::Kind::Return:
        case Stmt::Kind::Raw:
          record_reads(statement.a, domain, summary);
          if (!statement.a.empty() && !expression_is_obviously_pure(statement.a))
            summary.externally_observable = true;
          break;
        case Stmt::Kind::Call:
        case Stmt::Kind::Message:
        case Stmt::Kind::AwaitMessage:
        case Stmt::Kind::Echo:
          summary.externally_observable = true;
          record_reads(statement.a, domain, summary);
          record_reads(statement.b, domain, summary);
          record_reads(statement.c, domain, summary);
          for (const auto& argument : statement.args)
            record_reads(argument, domain, summary);
          break;
        case Stmt::Kind::Else:
          break;
      }
    }
    return summary;
  }

  const Field* find_state_field(const Domain& domain, const string& name) const {
    auto found = std::find_if(domain.state.begin(), domain.state.end(),
                              [&](const Field& field) { return field.name == name; });
    return found == domain.state.end() ? nullptr : &*found;
  }

  bool atomic_handler_plan(const Domain& domain, const Handler& handler,
                           AtomicHandlerPlan& plan) const {
    // This recognizer deliberately describes exactly one primitive state
    // operation. It never introduces a CAS loop, so Moss expressions are
    // evaluated once and multi-action/cross-field handlers fall through to a
    // lock-backed whole-domain representation.
    const Stmt* assignment = nullptr;
    const Stmt* captured_previous = nullptr;
    const Stmt* reply = nullptr;
    size_t assignment_index = 0, capture_index = 0, reply_index = 0;
    for (size_t index = 0; index < handler.body.size(); ++index) {
      const auto& statement = handler.body[index];
      if (statement.indent != 0) return false;
      if (statement.kind == Stmt::Kind::Assign &&
          find_state_field(domain, trim(statement.a))) {
        if (assignment) return false;
        assignment = &statement;
        assignment_index = index;
      } else if ((statement.kind == Stmt::Kind::Assign ||
                  statement.kind == Stmt::Kind::Let ||
                  statement.kind == Stmt::Kind::Var) &&
                 !find_state_field(domain, trim(statement.a)) &&
                 find_state_field(domain, trim(statement.b))) {
        if (captured_previous) return false;
        captured_previous = &statement;
        capture_index = index;
      } else if (statement.kind == Stmt::Kind::Reply) {
        if (reply) return false;
        reply = &statement;
        reply_index = index;
      } else {
        return false;
      }
    }

    if (handler.reply_type && !reply) return false;
    if (!handler.reply_type && reply) return false;

    if (!assignment) {
      if (!reply) return false;
      if (!expression_is_obviously_pure(reply->a) ||
          expression_mentions(reply->a, "self")) return false;
      const Field* field = nullptr;
      for (const auto& candidate : domain.state) {
        if (!expression_mentions_unqualified(reply->a, candidate.name)) continue;
        if (field) return false;
        field = &candidate;
      }
      if (!field) return false;
      plan.action = AtomicActionKind::Load;
      plan.field = field->name;
      if (trim(reply->a) != field->name)
        plan.result_expression = trim(reply->a);
      return true;
    }

    const Field* field = find_state_field(domain, trim(assignment->a));
    if (!field || !expression_is_obviously_pure(assignment->b) ||
        expression_mentions(assignment->b, "self")) return false;
    for (const auto& other : domain.state)
      if (other.name != field->name &&
          expression_mentions_unqualified(assignment->b, other.name))
        return false;
    if (captured_previous) {
      if (!reply || capture_index >= assignment_index || assignment_index >= reply_index ||
          trim(captured_previous->b) != field->name ||
          trim(reply->a) != trim(captured_previous->a))
        return false;
      for (const auto& state_field : domain.state)
        if (expression_mentions_unqualified(assignment->b, state_field.name)) return false;
      plan.action = AtomicActionKind::Swap;
      plan.field = field->name;
      plan.operand = trim(assignment->b);
      return true;
    }
    if (reply) {
      if (!expression_is_obviously_pure(reply->a) ||
          expression_mentions(reply->a, "self")) return false;
      for (const auto& other : domain.state)
        if (other.name != field->name &&
            expression_mentions_unqualified(reply->a, other.name))
          return false;
      if (trim(reply->a) != field->name)
        plan.result_expression = trim(reply->a);
    }

    string right = trim(assignment->b);
    if (field->type == "bool" && right == "not " + field->name) {
      plan.action = AtomicActionKind::FetchXor;
      plan.operand = "true";
    } else if (auto add = split_binary(right, {"+"})) {
      if (field->type != "int") return false;
      if (trim(add->first) == field->name &&
          !expression_mentions_unqualified(add->second, field->name)) {
        plan.action = AtomicActionKind::FetchAdd;
        plan.operand = add->second;
      } else if (trim(add->second) == field->name &&
                 !expression_mentions_unqualified(add->first, field->name)) {
        plan.action = AtomicActionKind::FetchAdd;
        plan.operand = add->first;
      } else {
        return false;
      }
    } else if (auto subtract = split_binary(right, {"-"})) {
      if (field->type != "int" || trim(subtract->first) != field->name ||
          expression_mentions_unqualified(subtract->second, field->name))
        return false;
      plan.action = AtomicActionKind::FetchSub;
      plan.operand = subtract->second;
    } else {
      if (expression_mentions_unqualified(right, field->name)) return false;
      plan.action = AtomicActionKind::Store;
      plan.operand = right;
    }
    plan.field = field->name;
    return true;
  }

  bool atomic_domain_plan(
      const Domain& domain,
      std::unordered_map<string, AtomicHandlerPlan>& handlers) const {
    if (domain.state.empty() || domain.handlers.empty()) return false;
    for (const auto& field : domain.state)
      if (field.type != "int" && field.type != "bool") return false;
    for (const auto& handler : domain.handlers) {
      for (const auto& param : handler.params)
        if (find_state_field(domain, param.name)) return false;
      for (const auto& param : handler.params) {
        std::set<string> visiting;
        if (!sendable_type(param.type, visiting)) return false;
      }
      if (handler.reply_type) {
        std::set<string> visiting;
        if (!sendable_type(*handler.reply_type, visiting)) return false;
      }
      AtomicHandlerPlan handler_plan;
      if (!atomic_handler_plan(domain, handler, handler_plan)) return false;
      handlers[handler_key(domain, handler)] = std::move(handler_plan);
    }
    return true;
  }

  const Domain* statement_target(
      const Stmt& statement, const Domain* source,
      const std::unordered_map<string, string>& types) const {
    string receiver;
    if (statement.kind == Stmt::Kind::Message) receiver = statement.a;
    else if (statement.kind == Stmt::Kind::AwaitMessage) receiver = statement.b;
    else return nullptr;
    if (receiver == "self") return source;
    auto found = types.find(receiver);
    if (found == types.end()) return nullptr;
    auto domain = domains_.find(canonical_type_name(found->second));
    return domain == domains_.end() ? nullptr : domain->second;
  }

  void update_statement_types(const Stmt& statement, const Domain* source,
                              std::unordered_map<string, string>& types) const {
    if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
        statement.kind == Stmt::Kind::Assign) {
      if (auto spawned = spawned_domain(statement.b)) {
        types[statement.a] = *spawned;
      } else {
        auto alias = types.find(trim(statement.b));
        if (alias != types.end()) types[statement.a] = alias->second;
      }
      return;
    }
    if (statement.kind != Stmt::Kind::AwaitMessage) return;
    const Domain* target = statement_target(statement, source, types);
    const Handler* handler = target ? find_handler(*target, statement.c) : nullptr;
    if (handler && handler->reply_type) types[statement.a] = *handler->reply_type;
  }

  bool batchable_message(const Stmt& statement, const Domain* source,
                         const std::unordered_map<string, string>& types,
                         const Domain*& target) const {
    if (statement.kind != Stmt::Kind::Message) return false;
    target = statement_target(statement, source, types);
    if (!target) return false;
    const Handler* handler = find_handler(*target, statement.b);
    if (!handler) return false;
    for (const auto& argument : statement.args)
      if (!batch_payload_is_total(argument)) return false;
    return true;
  }

  void plan_batches_in(const vector<Stmt>& statements, const Domain* source,
                       std::unordered_map<string, string> types,
                       OptimizationPlan& plan) const {
    for (size_t index = 0; index < statements.size();) {
      const Stmt& statement = statements[index];
      const Domain* target = nullptr;
      bool eligible = batchable_message(statement, source, types, target) &&
          plan.lowering_for(target->name) == DomainLowering::Mailbox;
      if (eligible) {
        size_t end = index + 1;
        for (; end < statements.size(); ++end) {
          const Stmt& candidate = statements[end];
          const Domain* candidate_target = nullptr;
          if (candidate.indent != statement.indent || candidate.a != statement.a ||
              !batchable_message(candidate, source, types, candidate_target) ||
              candidate_target != target)
            break;
        }
        if (end - index >= 2) {
          plan.batched_send_regions.push_back(
              BatchedSendRegion{statement.line, end - index, statement.a, target->name});
          index = end;
          continue;
        }
      }
      update_statement_types(statement, source, types);
      ++index;
    }
  }

  void plan_batched_sends(OptimizationPlan& plan) const {
    for (const auto& domain : program_.domains) {
      for (const auto& handler : domain.handlers) {
        std::unordered_map<string, string> types;
        types["self"] = domain.name;
        for (const auto& field : domain.state) types[field.name] = field.type;
        for (const auto& param : handler.params) types[param.name] = param.type;
        plan_batches_in(handler.body, &domain, std::move(types), plan);
      }
    }
    for (const auto& function : program_.functions) {
      std::unordered_map<string, string> types;
      for (const auto& param : function.params) types[param.name] = param.type;
      plan_batches_in(function.body, nullptr, std::move(types), plan);
    }
    if (program_.main)
      plan_batches_in(program_.main->body, nullptr, {}, plan);
  }

  void collect_domain_uses(
      const vector<Stmt>& statements, const Domain* source, const string& context,
      std::unordered_map<string, string> types,
      std::unordered_map<string, std::set<string>>& callers,
      std::set<string>& escaped) const {
    for (const auto& statement : statements) {
      if (statement.kind == Stmt::Kind::Message ||
          statement.kind == Stmt::Kind::AwaitMessage) {
        if (const Domain* target = statement_target(statement, source, types))
          callers[target->name].insert(context);
      }

      for (const auto& binding : types) {
        string domain_type = canonical_type_name(binding.second);
        if (!domains_.count(domain_type)) continue;
        bool used_outside_receiver = false;
        if ((statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
             statement.kind == Stmt::Kind::Assign) &&
            !spawned_domain(statement.b) &&
            expression_mentions(statement.b, binding.first))
          used_outside_receiver = true;
        if (statement.kind == Stmt::Kind::If || statement.kind == Stmt::Kind::While ||
            statement.kind == Stmt::Kind::Reply || statement.kind == Stmt::Kind::Return ||
            statement.kind == Stmt::Kind::Raw)
          used_outside_receiver = expression_mentions(statement.a, binding.first);
        if (statement.kind == Stmt::Kind::Call &&
            (expression_mentions(statement.a, binding.first) ||
             expression_mentions(statement.b, binding.first)))
          used_outside_receiver = true;
        for (const auto& argument : statement.args)
          used_outside_receiver = used_outside_receiver ||
              expression_mentions(argument, binding.first);
        if (used_outside_receiver) escaped.insert(domain_type);
      }

      if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        auto alias = types.find(trim(statement.b));
        if (alias != types.end() && domains_.count(canonical_type_name(alias->second)))
          escaped.insert(canonical_type_name(alias->second));
      }

      if (statement.kind == Stmt::Kind::Call ||
          statement.kind == Stmt::Kind::Message ||
          statement.kind == Stmt::Kind::AwaitMessage) {
        for (const auto& argument : statement.args) {
          auto value = types.find(trim(argument));
          if (value != types.end() && domains_.count(canonical_type_name(value->second)))
            escaped.insert(canonical_type_name(value->second));
        }
      }
      update_statement_types(statement, source, types);
    }
  }

  bool coalescable_await(const Stmt& statement,
                         const std::unordered_map<string, string>& types,
                         const std::set<string>& exclusive_domains,
                         const Domain*& target) const {
    if (statement.kind != Stmt::Kind::AwaitMessage) return false;
    target = statement_target(statement, nullptr, types);
    if (!target || !exclusive_domains.count(target->name)) return false;
    const Handler* handler = find_handler(*target, statement.c);
    if (!handler || !handler->reply_type) return false;
    for (const auto& argument : statement.args)
      if (!expression_is_obviously_pure(argument)) return false;
    return true;
  }

  void plan_coalesced_locks(OptimizationPlan& plan) const {
    if (!program_.main) return;

    // The initial exclusivity proof is intentionally narrow: one unconditional
    // main spawn, no capability-shaped storage/parameter/result, no alias or
    // expression escape, and no caller context other than main.
    std::unordered_map<string, size_t> spawn_counts;
    std::set<string> escaped;
    auto escape_type = [&](const string& type) {
      for (const auto& domain : program_.domains)
        if (expression_mentions(type, domain.name)) escaped.insert(domain.name);
    };
    for (const auto& statement : program_.main->body) {
      if (auto spawned = spawned_domain(statement.b)) {
        ++spawn_counts[*spawned];
        if (statement.indent != 0) escaped.insert(*spawned);
      }
    }
    for (const auto& object : program_.objects)
      for (const auto& field : object.fields) escape_type(field.type);
    for (const auto& domain : program_.domains) {
      for (const auto& field : domain.state) escape_type(field.type);
      for (const auto& handler : domain.handlers) {
        for (const auto& param : handler.params) escape_type(param.type);
        if (handler.reply_type) escape_type(*handler.reply_type);
        for (const auto& statement : handler.body)
          if (auto spawned = spawned_domain(statement.b)) escaped.insert(*spawned);
      }
    }
    for (const auto& function : program_.functions) {
      for (const auto& param : function.params) escape_type(param.type);
      if (function.return_type) escape_type(*function.return_type);
      for (const auto& statement : function.body)
        if (auto spawned = spawned_domain(statement.b)) escaped.insert(*spawned);
    }

    std::unordered_map<string, std::set<string>> callers;
    for (const auto& domain : program_.domains) {
      for (const auto& handler : domain.handlers) {
        std::unordered_map<string, string> types;
        types["self"] = domain.name;
        for (const auto& field : domain.state) types[field.name] = field.type;
        for (const auto& param : handler.params) types[param.name] = param.type;
        collect_domain_uses(handler.body, &domain, "domain:" + domain.name,
                            std::move(types), callers, escaped);
      }
    }
    for (const auto& function : program_.functions) {
      std::unordered_map<string, string> types;
      for (const auto& param : function.params) types[param.name] = param.type;
      collect_domain_uses(function.body, nullptr, "function:" + function.name,
                          std::move(types), callers, escaped);
    }
    collect_domain_uses(program_.main->body, nullptr, "main", {}, callers, escaped);

    std::set<string> exclusive_domains;
    for (const auto& domain : program_.domains) {
      DomainLowering lowering = plan.lowering_for(domain);
      if ((lowering == DomainLowering::DirectMutex ||
           lowering == DomainLowering::DirectRwLock) &&
          spawn_counts[domain.name] == 1 && !escaped.count(domain.name) &&
          callers[domain.name] == std::set<string>{"main"})
        exclusive_domains.insert(domain.name);
    }

    std::unordered_map<string, string> types;
    const auto& statements = program_.main->body;
    for (size_t index = 0; index < statements.size();) {
      const Stmt& statement = statements[index];
      const Domain* target = nullptr;
      if (coalescable_await(statement, types, exclusive_domains, target) &&
          (plan.lowering_for(target->name) == DomainLowering::DirectMutex ||
           plan.lowering_for(target->name) == DomainLowering::DirectRwLock)) {
        size_t end = index + 1;
        for (; end < statements.size(); ++end) {
          const Stmt& candidate = statements[end];
          const Domain* candidate_target = nullptr;
          if (candidate.indent != statement.indent || candidate.b != statement.b ||
              !coalescable_await(candidate, types, exclusive_domains, candidate_target) ||
              candidate_target != target)
            break;
        }
        if (end - index >= 2) {
          bool shared_read = plan.lowering_for(target->name) == DomainLowering::DirectRwLock;
          for (size_t item = index; item < end; ++item) {
            const auto& effects = plan.handler_effects.at(
                target->name + "." + statements[item].c);
            shared_read = shared_read && effects.state_effect == Effect::Read;
          }
          plan.coalesced_lock_regions.push_back(CoalescedLockRegion{
              statement.line, end - index, statement.b, target->name, shared_read});
          for (size_t item = index; item < end; ++item)
            update_statement_types(statements[item], nullptr, types);
          index = end;
          continue;
        }
      }
      update_statement_types(statement, nullptr, types);
      ++index;
    }
  }

  void validate_clusters(const OptimizationPlan& plan) const {
    std::set<string> seen;
    for (size_t index = 0; index < plan.domain_clusters.size(); ++index) {
      const auto& cluster = plan.domain_clusters[index];
      if (cluster.size() < 2)
        throw std::runtime_error("domain cluster " + std::to_string(index + 1) +
                                 " must contain at least two domain types");
      for (const auto& name : cluster) {
        if (!domains_.count(name))
          throw std::runtime_error("unknown domain in cluster: " + name);
        if (!seen.insert(name).second)
          throw std::runtime_error("domain appears in more than one cluster: " + name);
      }
    }

    if (plan.domain_clusters.empty()) return;
    std::unordered_map<string, size_t> spawn_counts;
    std::set<string> nested_spawns;
    if (program_.main) {
      for (const auto& statement : program_.main->body) {
        if (statement.kind != Stmt::Kind::Let && statement.kind != Stmt::Kind::Var) continue;
        if (auto spawned = spawned_domain(statement.b)) {
          ++spawn_counts[*spawned];
          if (statement.indent != 0) nested_spawns.insert(*spawned);
        }
      }
    }
    for (const auto& cluster : plan.domain_clusters) {
      for (const auto& name : cluster) {
        if (spawn_counts[name] != 1)
          throw std::runtime_error("clustered domain type '" + name +
              "' must be spawned exactly once in main (found " +
              std::to_string(spawn_counts[name]) + ")");
        if (nested_spawns.count(name))
          throw std::runtime_error("clustered domain type '" + name +
                                   "' must be spawned unconditionally at main scope");
      }
    }

  }

  bool sendable_type(const string& type, std::set<string>& visiting) const {
    string t = trim(type);
    if (t == "int" || t == "float" || t == "bool" || t == "string") return true;

    // Domain references are thread-safe capabilities in both backend transports.
    if (domains_.count(t)) return true;

    auto object = objects_.find(t);
    if (object != objects_.end()) {
      // Recursive value objects are not representable without indirection in the
      // current Rust backend, so keep this optimization conservative around them.
      if (!visiting.insert(t).second) return false;
      bool sendable = std::all_of(object->second->fields.begin(), object->second->fields.end(),
          [&](const Field& field) { return sendable_type(field.type, visiting); });
      visiting.erase(t);
      return sendable;
    }

    if ((starts_with(t, "seq[") || starts_with(t, "option[")) && ends_with(t, "]"))
      return sendable_type(trim(t.substr(t.find('[') + 1, t.size() - t.find('[') - 2)), visiting);
    if (starts_with(t, "table[") && ends_with(t, "]")) {
      auto parts = split_top_level(t.substr(6, t.size() - 7), ',');
      return parts.size() == 2 && sendable_type(parts[0], visiting) &&
             sendable_type(parts[1], visiting);
    }
    return false;
  }

  bool domain_is_direct_candidate(const Domain& domain) const {
    if (domain.handlers.empty()) return false;
    for (const auto& field : domain.state) {
      std::set<string> visiting;
      if (!sendable_type(field.type, visiting)) return false;
    }
    for (const auto& handler : domain.handlers) {
      if (!handler.reply_type) return false;
      for (const auto& param : handler.params) {
        std::set<string> visiting;
        if (!sendable_type(param.type, visiting)) return false;
      }
      std::set<string> visiting;
      if (!sendable_type(*handler.reply_type, visiting)) return false;
    }
    return true;
  }

  static std::optional<string> spawned_domain(const string& expression) {
    string value = trim(expression);
    if (!starts_with(value, "spawn ") || !ends_with(value, "()")) return std::nullopt;
    string name = trim(value.substr(6, value.size() - 8));
    return name.empty() ? std::nullopt : std::optional<string>(name);
  }

  const Handler* find_handler(const Domain& domain, const string& name) const {
    for (const auto& handler : domain.handlers)
      if (handler.name == name) return &handler;
    return nullptr;
  }

  void scan_calls(const vector<Stmt>& statements, std::unordered_map<string, string>& types,
                  std::set<string>& asynchronously_called, bool& complete) const {
    size_t index = 0;
    scan_call_block(statements, index, 0, types, asynchronously_called, complete);
    if (index != statements.size()) complete = false;
  }

  void scan_call_block(const vector<Stmt>& statements, size_t& index, int level,
                       std::unordered_map<string, string>& types,
                       std::set<string>& asynchronously_called, bool& complete) const {
    while (index < statements.size()) {
      const Stmt& statement = statements[index];
      if (statement.indent < level) return;
      if (statement.indent > level) {
        complete = false;
        return;
      }
      if (statement.kind == Stmt::Kind::Else) return;

      if (statement.kind == Stmt::Kind::If || statement.kind == Stmt::Kind::While) {
        bool is_if = statement.kind == Stmt::Kind::If;
        ++index;
        auto child_types = types;
        scan_call_block(statements, index, level + 1, child_types,
                        asynchronously_called, complete);
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          auto alternative_types = types;
          scan_call_block(statements, index, level + 1, alternative_types,
                          asynchronously_called, complete);
        }
        continue;
      }

      if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        if (auto spawned = spawned_domain(statement.b)) {
          types[statement.a] = *spawned;
        } else {
          auto source = types.find(trim(statement.b));
          types[statement.a] = source == types.end() ? "_value" : source->second;
        }
        ++index;
        continue;
      }

      if (statement.kind != Stmt::Kind::Message && statement.kind != Stmt::Kind::AwaitMessage) {
        if (statement.kind == Stmt::Kind::Call) complete = false;
        ++index;
        continue;
      }

      const string& receiver = statement.kind == Stmt::Kind::Message ? statement.a : statement.b;
      auto receiver_type = types.find(receiver);
      if (receiver_type == types.end() || !domains_.count(receiver_type->second)) {
        complete = false;
        ++index;
        continue;
      }
      const Domain& target = *domains_.at(receiver_type->second);
      if (statement.kind == Stmt::Kind::Message) {
        asynchronously_called.insert(target.name);
      } else {
        const Handler* handler = find_handler(target, statement.c);
        types[statement.a] = handler && handler->reply_type ? *handler->reply_type : "_value";
      }
      ++index;
    }
  }
};

class Generator {
 public:
  Generator(const Program& p, const OptimizationPlan& plan,
            bool await_error_handling = true)
      : p_(p), plan_(plan), await_error_handling_(await_error_handling) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    for (const auto& o : p.objects) objects_[o.name] = &o;
    for (const auto& f : p.functions) functions_[f.name] = &f;
  }

  string generate() {
    std::ostringstream o;
    o << "// Generated by Moss v0.2. Do not edit by hand.\n";
    if (plan_.needs_mailbox_runtime())
      o << "// Message transport: lock-backed shared-memory mailboxes.\n";
    else
      o << "// Message transport: mailbox-free statically selected shared memory.\n";
    o << "// Backend labels below record the lowering selected by the whole-program optimization plan.\n";
    o << "// Moss source remains message-based; these comments identify its Rust lowering.\n";
    if (!await_error_handling_) {
      o << "// Await error handling disabled: generated awaits use unchecked extraction; supervision owns failure handling.\n";
    }
    bool has_direct = plan_.has_lowering(DomainLowering::DirectMutex) ||
        plan_.has_lowering(DomainLowering::DirectRwLock) ||
        plan_.has_lowering(DomainLowering::DirectAtomic);
    if (has_direct) {
      o << "// Message optimization: direct shared-memory dispatch for ";
      bool first = true;
      for (const auto& domain : p_.domains) {
        DomainLowering lowering = plan_.lowering_for(domain);
        if (lowering != DomainLowering::DirectMutex &&
            lowering != DomainLowering::DirectRwLock &&
            lowering != DomainLowering::DirectAtomic)
          continue;
        if (!first) o << ", ";
        first = false;
        o << domain.name;
      }
      o << ".\n";
    }
    for (const auto& domain : p_.domains) {
      o << "// Moss backend plan: " << domain.name << " = ";
      switch (plan_.lowering_for(domain)) {
        case DomainLowering::Mailbox: o << "Mailbox"; break;
        case DomainLowering::DirectMutex: o << "DirectMutex"; break;
        case DomainLowering::DirectRwLock: o << "DirectRwLock"; break;
        case DomainLowering::DirectAtomic: o << "DirectAtomic"; break;
        case DomainLowering::ClusterLocal: o << "ClusterLocal"; break;
      }
      o << ".\n";
    }
    for (size_t index = 0; index < plan_.domain_clusters.size(); ++index) {
      o << "// Domain cluster " << index << ": static same-thread dispatch for ";
      for (size_t member = 0; member < plan_.domain_clusters[index].size(); ++member) {
        if (member) o << ", ";
        o << plan_.domain_clusters[index][member];
      }
      o << ".\n";
    }
    o << "#![allow(non_snake_case)]\n#![allow(non_camel_case_types)]\n#![allow(dead_code)]\n";
    o << "#![allow(unused_imports)]\n#![allow(unused_mut)]\n#![allow(unused_variables)]\n\n";
    o << "use std::collections::{HashMap, VecDeque};\n";
    o << "use std::cell::RefCell;\n";
    o << "use std::rc::Rc;\n";
    o << "use std::sync::Arc;\n";
    if (plan_.needs_mailbox_runtime()) o << "use std::sync::{Condvar, Mutex};\n";
    else if (plan_.has_lowering(DomainLowering::DirectMutex)) o << "use std::sync::Mutex;\n";
    if (plan_.has_lowering(DomainLowering::DirectRwLock)) o << "use std::sync::RwLock;\n";
    if (plan_.has_lowering(DomainLowering::DirectAtomic))
      o << "use std::sync::atomic::{AtomicBool, AtomicI64, Ordering};\n";
    if (plan_.needs_mailbox_runtime()) o << "use std::thread;\n";
    o << "\n";
    o << "fn __moss_require_send<T: Send>() {}\n\n";
    if (plan_.needs_mailbox_runtime()) gen_shared_channel(o);
    gen_tracker(o, plan_.needs_mailbox_runtime());

    for (const auto& t : p_.objects) gen_object(o, t);
    for (const auto& f : p_.functions) gen_function(o, f);
    // Refs first because handler message enums can mention refs to later domains.
    for (const auto& d : p_.domains) gen_ref_decl(o, d);
    for (const auto& d : p_.domains) gen_domain(o, d);
    for (size_t index = 0; index < plan_.domain_clusters.size(); ++index)
      gen_cluster(o, index, plan_.domain_clusters[index]);
    if (p_.main) gen_main(o, *p_.main);
    else o << "fn main() {}\n";
    return o.str();
  }

 private:
  const Program& p_;
  const OptimizationPlan& plan_;
  bool await_error_handling_ = true;
  std::unordered_map<string, const Domain*> domains_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const Function*> functions_;
  size_t reply_temp_ = 0;

  static string comment_text(string text) {
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r' || text[i] == '\n') {
        out.push_back(' ');
      } else if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
        out += "* /";
        ++i;
      } else {
        out.push_back(text[i]);
      }
    }
    return out;
  }

  static string handler_signature(const Handler& h) {
    if (!h.header.empty()) return h.header;
    std::ostringstream out;
    out << "on " << h.name << "(";
    for (size_t i = 0; i < h.params.size(); ++i) {
      if (i) out << ", ";
      out << h.params[i].name << ": " << h.params[i].type;
    }
    out << ")";
    if (h.reply_type) out << " -> " << *h.reply_type;
    return out.str();
  }

  static string state_field_signature(const Field& f) {
    if (!f.header.empty()) return f.header;
    string out = "var " + f.name + ": " + f.type;
    if (!f.init.empty()) out += " = " + f.init;
    return out;
  }

  static void source_comment(std::ostringstream& o, int spaces, int line,
                             const string& source) {
    if (line <= 0) return;
    o << string(spaces, ' ') << "// Moss line " << line;
    if (!source.empty()) o << ": " << comment_text(source);
    o << "\n";
  }

  static void backend_comment(std::ostringstream& o, int spaces, const string& text) {
    o << string(spaces, ' ') << "// Moss backend: " << comment_text(text) << "\n";
  }

  bool direct_shared_memory(const Domain& domain) const {
    DomainLowering lowering = plan_.lowering_for(domain);
    return lowering == DomainLowering::DirectMutex ||
           lowering == DomainLowering::DirectRwLock ||
           lowering == DomainLowering::DirectAtomic;
  }

  bool direct_lock(const Domain& domain) const {
    DomainLowering lowering = plan_.lowering_for(domain);
    return lowering == DomainLowering::DirectMutex ||
           lowering == DomainLowering::DirectRwLock;
  }

  bool direct_atomic(const Domain& domain) const {
    return plan_.lowering_for(domain) == DomainLowering::DirectAtomic;
  }

  std::optional<size_t> cluster_for(const Domain& domain) const {
    return plan_.cluster_for(domain.name);
  }

  bool local_cluster_call(const Domain* source, const Domain* target,
                          const std::optional<size_t>& cluster_context) const {
    return source && target && cluster_context &&
           plan_.same_cluster(source->name, target->name) &&
           plan_.cluster_for(source->name) == cluster_context;
  }

  string rust_type(const string& t) const {
    string x = canonical_type_name(t);
    if (x == "int") return "i64";
    if (x == "float") return "f64";
    if (x == "bool") return "bool";
    if (x == "string") return "String";
    if (x == "vector") return "Vec<T>";
    if (x == "map") return "HashMap<K, V>";
    if (x == "queue") return "VecDeque<T>";
    if (x == "unit") return "()";
    if (domains_.count(x)) return x + "Ref";
    if (objects_.count(x)) return x;
    if (starts_with(x, "seq[") && ends_with(x, "]"))
      return "Vec<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "vector[") && ends_with(x, "]")) return "Vec<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "queue[") && ends_with(x, "]")) return "VecDeque<" + rust_type(x.substr(6, x.size()-7)) + ">";
    if (starts_with(x, "map[") && ends_with(x, "]")) {
      auto ps = split_top_level(x.substr(4, x.size()-5), ',');
      return "HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    if (starts_with(x, "option[") && ends_with(x, "]"))
      return "Option<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "table[") && ends_with(x, "]")) {
      auto ps = split_top_level(x.substr(6, x.size()-7), ',');
      return "HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    return x;
  }

  std::set<string> constraint_ops(const Function& f, const string& subject) const {
    std::set<string> out;
    for (const auto& c : f.constraints) if (c.subject == subject) {
      if (c.kind == ConstraintKind::Operator) out.insert(c.detail);
      if (c.kind == ConstraintKind::Indexable) out.insert("[]");
    }
    return out;
  }

  string local_rust_type(const string& type, size_t cluster) const {
    string value = trim(type);
    auto domain = domains_.find(value);
    if (domain != domains_.end()) {
      if (plan_.cluster_for(value) == std::optional<size_t>(cluster))
        return value + "LocalRef";
      return "Rc<" + value + "Ref>";
    }
    return rust_type(value);
  }

  string default_value(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int") return "0";
    if (t == "float") return "0.0";
    if (t == "bool") return "false";
    if (t == "string") return "String::new()";
    if (starts_with(t, "seq[")) return "Vec::new()";
    if (starts_with(t, "table[")) return "HashMap::new()";
    if (starts_with(t, "option[")) return "None";
    return "Default::default()";
  }

  bool copy_type(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int" || t == "float" || t == "bool") return true;
    if (starts_with(t, "option[") && ends_with(t, "]"))
      return copy_type(trim(t.substr(7, t.size() - 8)));
    return false;
  }

  bool borrowable_type(const string& type) const {
    string t = canonical_type_name(type);
    if (t.empty() || starts_with(t, "_") || copy_type(t) || domains_.count(t)) return false;
    return true;
  }

  static Effect function_effect(const Function& function, size_t index) {
    return index < function.parameter_effects.size()
        ? function.parameter_effects[index] : Effect::Read;
  }

  static Effect method_effect(const Method& method, size_t index) {
    return index < method.parameter_effects.size()
        ? method.parameter_effects[index] : Effect::Read;
  }

  bool should_borrow_function_parameter(const Function& function, size_t index,
                                        const string& parameter_type,
                                        const string& actual_type) const {
    Effect effect = function_effect(function, index);
    if (effect == Effect::Consume) return false;
    const auto& parameter = function.params[index];
    auto ops = constraint_ops(function, parameter.name);
    if (parameter.type.empty() && !ops.empty() && !ops.count("[]")) return false;
    string candidate = parameter_type.empty() ? actual_type : parameter_type;
    if (candidate.empty() || starts_with(candidate, "_") ||
        (parameter.type.empty() && ops.empty() && !function.static_dispatch)) return false;
    return borrowable_type(candidate);
  }

  string function_call_argument(const Function& function, size_t index,
                                const string& argument, const Domain* d,
                                const std::set<string>& locals,
                                const std::unordered_map<string,string>* types,
                                size_t functional_pipeline_id = 0) const {
    string rendered = expr(argument, d, locals, types, functional_pipeline_id);
    if (index >= function.params.size()) return rendered;
    Effect effect = function_effect(function, index);
    string parameter_type = function.params[index].type;
    string actual_type = generated_expr_type(argument, types).value_or("");
    if (!should_borrow_function_parameter(function, index, parameter_type, actual_type))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + rendered + ")";
    return "&(" + rendered + ")";
  }

  string method_call_argument(const Method& method, size_t index,
                              const string& argument, const Domain* d,
                              const std::set<string>& locals,
                              const std::unordered_map<string,string>* types,
                              size_t functional_pipeline_id = 0) const {
    string rendered = expr(argument, d, locals, types, functional_pipeline_id);
    if (index >= method.params.size()) return rendered;
    Effect effect = method_effect(method, index);
    string type = method.params[index].type;
    if (effect == Effect::Consume || !borrowable_type(type)) return rendered;
    if (effect == Effect::Write) return "&mut (" + rendered + ")";
    return "&( " + rendered + ")";
  }

  struct GeneratedBinaryExpression {
    string left;
    string op;
    string right;
  };

  static bool generated_integer_literal(const string& expression) {
    string value = trim(expression);
    size_t start = !value.empty() && (value.front() == '+' || value.front() == '-')
        ? 1 : 0;
    return start < value.size() &&
        std::all_of(value.begin() + static_cast<std::ptrdiff_t>(start), value.end(),
                    [](char ch) {
                      return std::isdigit(static_cast<unsigned char>(ch));
                    });
  }

  static string generated_integer_literal_as_float(string expression) {
    expression = trim(std::move(expression));
    if (!expression.empty() && expression.front() == '+') expression.erase(expression.begin());
    return expression + ".0";
  }

  static std::optional<GeneratedBinaryExpression> generated_split_binary(
      const string& expression, const vector<string>& operators) {
    int parens = 0, braces = 0, brackets = 0;
    bool in_string = false;
    for (size_t index = expression.size(); index-- > 0;) {
      char ch = expression[index];
      if (in_string) {
        if (ch == '"' && (index == 0 || expression[index - 1] != '\\'))
          in_string = false;
        continue;
      }
      if (ch == '"') { in_string = true; continue; }
      if (ch == ')') ++parens;
      else if (ch == '(') --parens;
      else if (ch == '}') ++braces;
      else if (ch == '{') --braces;
      else if (ch == ']') ++brackets;
      else if (ch == '[') --brackets;
      if (parens != 0 || braces != 0 || brackets != 0) continue;
      for (const auto& op : operators) {
        if (index + op.size() > expression.size() ||
            expression.compare(index, op.size(), op) != 0)
          continue;
        string left = trim(expression.substr(0, index));
        string right = trim(expression.substr(index + op.size()));
        if (left.empty() || right.empty()) continue;
        if (op == "+" || op == "-") {
          size_t before = index;
          while (before > 0 &&
                 std::isspace(static_cast<unsigned char>(expression[before - 1])))
            --before;
          if (before == 0) continue;
          char previous = expression[before - 1];
          if (previous == '(' || previous == '[' || previous == '{' ||
              previous == ',' || previous == '+' || previous == '-' ||
              previous == '*' || previous == '/' || previous == '<' ||
              previous == '>' || previous == '=' || previous == '!')
            continue;
        }
        return GeneratedBinaryExpression{left, op, right};
      }
    }
    return std::nullopt;
  }

  static std::optional<string> generated_functional_element_type(
      const string& type) {
    string value = canonical_type_name(type);
    if (starts_with(value, "vector[") && ends_with(value, "]"))
      return trim(value.substr(7, value.size() - 8));
    if (starts_with(value, "seq[") && ends_with(value, "]"))
      return trim(value.substr(4, value.size() - 5));
    return std::nullopt;
  }

  string resolved_callable_identity(
      string callable, const std::unordered_map<string,string>* types) const {
    callable = trim(std::move(callable));
    if (types) {
      auto local = types->find(callable);
      if (local != types->end() && starts_with(local->second, "callable:"))
        return local->second.substr(9);
    }
    return callable;
  }

  static bool generated_expression_uses(const string& expression,
                                        const string& name) {
    bool in_string = false, escaped = false;
    for (size_t index = 0; index < expression.size();) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        ++index;
        continue;
      }
      if (ch == '"') { in_string = true; ++index; continue; }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      if (expression.compare(index, end - index, name) == 0) return true;
      index = end;
    }
    return false;
  }

  std::optional<string> generated_callable_result(
      const string& callable, const vector<string>& input_types,
      const std::unordered_map<string,string>* types) const {
    if (generated_expression_uses(callable, "_")) {
      if (input_types.size() != 1) return std::nullopt;
      std::unordered_map<string,string> placeholder_types = types
          ? *types : std::unordered_map<string,string>{};
      placeholder_types["_"] = input_types.front();
      return generated_expr_type(callable, &placeholder_types);
    }
    string bound_receiver, bound_method;
    if (parse_bound_method_callable(callable, bound_receiver, bound_method)) {
      auto receiver_type = generated_expr_type(bound_receiver, types);
      const Method* method = receiver_type
          ? resolve_object_method(objects_, canonical_type_name(*receiver_type),
                                  bound_method, input_types, false, nullptr)
          : nullptr;
      if (method && method->return_type)
        return canonical_type_name(*method->return_type);
      return std::nullopt;
    }
    string identity = resolved_callable_identity(callable, types);
    auto function = functions_.find(identity);
    if (function == functions_.end()) return std::nullopt;
    if (function->second->static_dispatch) {
      auto specialization = std::find_if(
          function->second->specializations.begin(),
          function->second->specializations.end(),
          [&](const FunctionSpecialization& candidate) {
            return candidate.parameter_types == input_types;
          });
      if (specialization != function->second->specializations.end())
        return specialization->return_type;
      return std::nullopt;
    }
    if (function->second->return_type &&
        !starts_with(*function->second->return_type, "_"))
      return canonical_type_name(*function->second->return_type);
    return std::nullopt;
  }

  std::optional<string> generated_functional_pipeline_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
    auto pipeline = parse_functional_pipeline(expression);
    if (!pipeline) return std::nullopt;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type) return std::nullopt;
    auto element = generated_functional_element_type(*source_type);
    if (!element) return std::nullopt;
    string current = *element;
    string output = "vector[" + current + "]";
    for (const auto& stage : pipeline->stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          if (stage.arguments.size() != 1) return std::nullopt;
          auto result = generated_callable_result(stage.arguments.front(),
                                                  {current}, types);
          if (!result) return std::nullopt;
          current = *result;
          output = "vector[" + current + "]";
          break;
        }
        case FunctionalNodeKind::Filter:
          output = "vector[" + current + "]";
          break;
        case FunctionalNodeKind::Reduce:
          if (stage.arguments.size() != 2) return std::nullopt;
          output = generated_expr_type(stage.arguments.front(), types).value_or("");
          break;
        case FunctionalNodeKind::Sum:
          output = current;
          break;
        case FunctionalNodeKind::Count:
          output = "int";
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All:
          output = "bool";
          break;
        case FunctionalNodeKind::Source:
          return std::nullopt;
      }
    }
    return output.empty() ? std::nullopt : std::optional<string>(output);
  }

  std::optional<string> generated_expr_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
    string original = trim(expression);
    if (auto pipeline = generated_functional_pipeline_type(original, types))
      return pipeline;
    string value = normalize_pipeline(std::move(original));
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')' &&
           matching_paren(value, 0) == value.size() - 1)
      value = trim(value.substr(1, value.size() - 2));
    if (value == "true" || value == "false") return string("bool");
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      return string("string");
    if (types) {
      auto local = types->find(value);
      if (local != types->end() && !local->second.empty() &&
          !starts_with(local->second, "_"))
        return canonical_type_name(local->second);
    }
    if (plain_identifier(value) && functions_.count(value))
      return "callable:" + value;
    size_t start = !value.empty() && (value.front() == '+' || value.front() == '-') ? 1 : 0;
    if (generated_integer_literal(value))
      return string("int");
    bool dot = false;
    bool numeric = start < value.size();
    for (size_t index = start; numeric && index < value.size(); ++index) {
      if (value[index] == '.' && !dot) dot = true;
      else if (!std::isdigit(static_cast<unsigned char>(value[index]))) numeric = false;
    }
    if (numeric && dot) return string("float");
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
      auto elements = split_top_level(value.substr(1, value.size() - 2), ',');
      if (elements.size() == 1 && elements.front().empty()) return std::nullopt;
      string element_type;
      for (const auto& element : elements) {
        auto current = generated_expr_type(element, types);
        if (!current) return std::nullopt;
        string concrete = canonical_type_name(*current);
        if (element_type.empty()) element_type = concrete;
        else if (element_type != concrete) return std::nullopt;
      }
      return "vector[" + element_type + "]";
    }

    string receiver, method_name;
    vector<string> method_args;
    if (parse_member_call(value, receiver, method_name, method_args)) {
      auto receiver_type = generated_expr_type(receiver, types);
      if (receiver_type) {
        vector<string> argument_types;
        for (const auto& argument : method_args)
          argument_types.push_back(generated_expr_type(argument, types).value_or(""));
        auto match = resolve_object_method(objects_, *receiver_type, method_name,
                                           argument_types, false, nullptr);
        if (match && match->return_type)
          return canonical_type_name(*match->return_type);
      }
    }

    string callee;
    vector<string> call_args;
    if (parse_simple_call(value, callee, call_args)) {
      if (objects_.count(callee)) return callee;
      auto function = functions_.find(callee);
      if (function != functions_.end() && function->second->return_type) {
        vector<string> argument_types;
        for (const auto& argument : call_args)
          argument_types.push_back(generated_expr_type(argument, types).value_or(""));
        if (function->second->static_dispatch) {
          auto specialization = std::find_if(
              function->second->specializations.begin(),
              function->second->specializations.end(),
              [&](const FunctionSpecialization& candidate) {
                return candidate.parameter_types == argument_types;
              });
          if (specialization != function->second->specializations.end())
            return specialization->return_type;
          return std::nullopt;
        }
        string result = canonical_type_name(*function->second->return_type);
        if (starts_with(result, "_generic:")) {
          string parameter = result.substr(9);
          for (size_t index = 0; index < function->second->params.size(); ++index)
            if (function->second->params[index].name == parameter &&
                index < argument_types.size() && !argument_types[index].empty())
              return argument_types[index];
          return std::nullopt;
        }
        if (!starts_with(result, "_")) return result;
      }
    }

    auto member = value.rfind('.');
    if (member != string::npos && value.find('(', member) == string::npos) {
      auto base = generated_expr_type(value.substr(0, member), types);
      if (base) {
        auto object = objects_.find(canonical_type_name(*base));
        if (object != objects_.end())
          for (const auto& field : object->second->fields)
            if (field.name == trim(value.substr(member + 1)))
              return canonical_type_name(field.type);
      }
    }
    if (generated_split_binary(value, {" or "}) ||
        generated_split_binary(value, {" and "}) ||
        generated_split_binary(value, {"==", "!=", "<=", ">=", "<", ">"}))
      return string("bool");
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/"}}) {
      auto binary = generated_split_binary(value, operators);
      if (!binary) continue;
      auto left = generated_expr_type(binary->left, types);
      auto right = generated_expr_type(binary->right, types);
      if (left && right) {
        if (*left == "string" && *right == "string" && binary->op == "+")
          return string("string");
        if ((*left == "int" || *left == "float") &&
            (*right == "int" || *right == "float"))
          return (*left == "float" || *right == "float") ? "float" : "int";
      }
    }
    return std::nullopt;
  }

  static string generated_type_list(const vector<string>& types) {
    std::ostringstream out;
    for (size_t index = 0; index < types.size(); ++index) {
      if (index) out << ", ";
      out << (types[index].empty() ? "unresolved" : types[index]);
    }
    return out.str();
  }

  string emitted_function_name(
      const string& name, const vector<string>& arguments,
      const std::unordered_map<string,string>* types) const {
    auto function = functions_.find(name);
    if (function == functions_.end() || !function->second->static_dispatch)
      return name;
    vector<string> argument_types;
    for (const auto& argument : arguments)
      argument_types.push_back(generated_expr_type(argument, types).value_or(""));
    auto specialization = std::find_if(
        function->second->specializations.begin(),
        function->second->specializations.end(),
        [&](const FunctionSpecialization& candidate) {
          return candidate.parameter_types == argument_types;
        });
    if (specialization == function->second->specializations.end())
      throw std::runtime_error("missing static specialization for function '" + name +
                               "' with argument types (" + generated_type_list(argument_types) + ")");
    return specialization->generated_name;
  }

  struct FunctionalValue {
    string expression;
    string type;
    bool reference = false;
  };

  const FunctionalPipeline* planned_functional_pipeline(
      size_t functional_pipeline_id) const {
    if (functional_pipeline_id == 0) return nullptr;
    auto planned = std::find_if(
        p_.functional_pipelines.begin(), p_.functional_pipelines.end(),
        [&](const FunctionalPipeline& candidate) {
          return candidate.transient_id == functional_pipeline_id;
        });
    if (planned == p_.functional_pipelines.end())
      throw std::runtime_error(
          "internal error: checked functional_pipeline_id has no IR plan");
    return &*planned;
  }

  const FunctionalTraversalGroup* functional_traversal_group(
      size_t group_id) const {
    if (!group_id) return nullptr;
    auto found = std::find_if(
        p_.functional_traversal_groups.begin(),
        p_.functional_traversal_groups.end(),
        [&](const FunctionalTraversalGroup& group) {
          return group.transient_id == group_id;
        });
    if (found == p_.functional_traversal_groups.end())
      throw std::runtime_error(
          "internal error: functional traversal group has no IR plan");
    return &*found;
  }

  string render_functional_callable(
      const string& callable, const vector<FunctionalValue>& arguments,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    if (generated_expression_uses(callable, "_")) {
      if (arguments.size() != 1)
        throw std::runtime_error("internal error: placeholder functional arity");
      string replaced = replace_unqualified_word(callable, "_",
                                                 arguments.front().expression);
      std::unordered_map<string,string> callback_types = types
          ? *types : std::unordered_map<string,string>{};
      callback_types[arguments.front().expression] = arguments.front().type;
      auto callback_locals = locals;
      callback_locals.insert(arguments.front().expression);
      return expr(replaced, domain, callback_locals, &callback_types);
    }

    string bound_receiver, bound_method;
    if (parse_bound_method_callable(callable, bound_receiver, bound_method)) {
      auto receiver_type = generated_expr_type(bound_receiver, types);
      vector<string> argument_types;
      for (const auto& argument : arguments)
        argument_types.push_back(argument.type);
      const Method* method = receiver_type
          ? resolve_object_method(objects_, canonical_type_name(*receiver_type),
                                  bound_method, argument_types, false, nullptr)
          : nullptr;
      if (!method)
        throw std::runtime_error(
            "missing statically resolved functional bound method '" +
            trim(callable) + "'");
      std::ostringstream rendered;
      rendered << expr(bound_receiver, domain, locals, types) << "."
               << bound_method << "(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        const auto& argument = arguments[index];
        Effect effect = method_effect(*method, index);
        bool borrow = effect != Effect::Consume &&
            borrowable_type(argument.type);
        if (domains_.count(canonical_type_name(argument.type))) {
          if (argument.reference)
            rendered << "(*" << argument.expression << ").clone()";
          else
            rendered << argument.expression;
        } else if (borrow && !argument.reference)
          rendered << (effect == Effect::Write ? "&mut (" : "&(")
                   << argument.expression << ")";
        else
          rendered << argument.expression;
      }
      rendered << ")";
      return rendered.str();
    }

    string identity = resolved_callable_identity(callable, types);
    auto function = functions_.find(identity);
    if (function == functions_.end())
      throw std::runtime_error("missing statically resolved functional callable '" +
                               identity + "'");
    string emitted = identity;
    vector<string> argument_types;
    for (const auto& argument : arguments) argument_types.push_back(argument.type);
    if (function->second->static_dispatch) {
      auto specialization = std::find_if(
          function->second->specializations.begin(),
          function->second->specializations.end(),
          [&](const FunctionSpecialization& candidate) {
            return candidate.parameter_types == argument_types;
          });
      if (specialization == function->second->specializations.end())
        throw std::runtime_error("missing functional specialization for '" +
                                 identity + "'");
      emitted = specialization->generated_name;
    }
    std::ostringstream rendered;
    rendered << emitted << "(";
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (index) rendered << ", ";
      const auto& argument = arguments[index];
      Effect effect = function_effect(*function->second, index);
      bool borrow = effect != Effect::Consume && borrowable_type(argument.type);
      if (domains_.count(canonical_type_name(argument.type))) {
        if (argument.reference)
          rendered << "(*" << argument.expression << ").clone()";
        else
          rendered << argument.expression;
      } else if (borrow && !argument.reference)
        rendered << (effect == Effect::Write ? "&mut (" : "&(")
                 << argument.expression << ")";
      else
        rendered << argument.expression;
    }
    rendered << ")";
    return rendered.str();
  }

  static string functional_zero(const string& type) {
    return canonical_type_name(type) == "float" ? "0.0_f64" : "0_i64";
  }

  string functional_terminal_initializer(
      const ParsedFunctionalStage& terminal, const string& element_type,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    switch (terminal.kind) {
      case FunctionalNodeKind::Reduce:
        return expr(terminal.arguments.front(), domain, locals, types);
      case FunctionalNodeKind::Sum:
        return functional_zero(element_type);
      case FunctionalNodeKind::Count:
        return "0_i64";
      case FunctionalNodeKind::Any:
        return "false";
      case FunctionalNodeKind::All:
        return "true";
      case FunctionalNodeKind::Source:
      case FunctionalNodeKind::Map:
      case FunctionalNodeKind::Filter:
        break;
    }
    throw std::runtime_error(
        "internal error: non-terminal functional traversal consumer");
  }

  void gen_shared_functional_traversal(
      std::ostringstream& out, const FunctionalTraversalGroup& group,
      const vector<Stmt>& statements, size_t statement_index,
      const Domain* domain, std::set<string>& locals,
      std::unordered_map<string,string>& types, int spaces,
      const string& context) const {
    if (group.consumers.size() < 2 ||
        statement_index + group.consumers.size() > statements.size())
      throw std::runtime_error(
          "internal error: invalid shared functional traversal region");

    vector<const FunctionalPipeline*> plans;
    vector<ParsedFunctionalPipeline> pipelines;
    vector<string> final_element_types;
    plans.reserve(group.consumers.size());
    pipelines.reserve(group.consumers.size());
    final_element_types.reserve(group.consumers.size());
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      const Stmt& statement = statements[statement_index + index];
      size_t exact_id = statement_functional_pipeline_id(statement, context, 0);
      if (exact_id != consumer.pipeline_id || trim(statement.a) !=
          consumer.result_binding)
        throw std::runtime_error(
            "internal error: stale shared functional traversal region");
      const FunctionalPipeline* plan = planned_functional_pipeline(exact_id);
      auto parsed = parse_functional_pipeline(statement.b);
      if (!plan || !parsed || parsed->stages.empty() ||
          !functional_terminal_kind(parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid shared functional consumer");
      string element_type = generated_functional_element_type(
          group.source_type).value_or("");
      if (element_type.empty())
        throw std::runtime_error(
            "internal error: untyped shared functional source");
      for (const auto& stage : parsed->stages) {
        if (stage.kind != FunctionalNodeKind::Map) continue;
        auto result = generated_callable_result(
            stage.arguments.front(), {element_type}, &types);
        if (!result)
          throw std::runtime_error(
              "internal error: untyped shared functional map");
        element_type = *result;
      }
      plans.push_back(plan);
      pipelines.push_back(std::move(*parsed));
      final_element_types.push_back(std::move(element_type));
    }

    for (size_t index = 1; index < group.consumers.size(); ++index)
      source_comment(out, spaces, statements[statement_index + index].line,
                     statements[statement_index + index].text);
    backend_comment(
        out, spaces,
        "SHARED FUNCTIONAL SOURCE TRAVERSAL (" +
            std::to_string(group.consumers.size()) +
            " terminal consumers); one dataflow DAG loop");
    out << string(spaces, ' ') << "let (";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      if (group.consumers[index].mutable_binding) out << "mut ";
      out << group.consumers[index].result_binding;
    }
    out << ") = {\n";
    string inner(spaces + 4, ' ');
    string deep(spaces + 8, ' ');
    out << inner << "// Moss dataflow group %" << group.transient_id
        << ", semantic " << group.semantic_identity << "; provenance";
    for (const auto& origin : group.provenance) out << " " << origin;
    out << "\n";
    out << inner << "let __moss_shared_source_" << group.transient_id
        << " = &(" << expr(group.source_expression, domain, locals, &types)
        << ");\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& terminal = pipelines[index].stages.back();
      out << inner << "let mut __moss_shared_result_" << group.transient_id
          << "_" << index << " = ";
      if (plans[index]->count_uses_exact_length)
        out << "__moss_shared_source_" << group.transient_id
            << ".len() as i64";
      else
        out << functional_terminal_initializer(
            terminal, final_element_types[index], domain, locals, &types);
      out << ";\n";
    }

    bool has_traversal = std::any_of(
        plans.begin(), plans.end(), [](const FunctionalPipeline* plan) {
          return !plan->count_uses_exact_length;
        });
    auto source_element = generated_functional_element_type(group.source_type);
    if (!source_element)
      throw std::runtime_error(
          "internal error: untyped shared functional source element");
    if (has_traversal) {
      out << inner << "for __moss_shared_item_ref_" << group.transient_id
          << " in __moss_shared_source_" << group.transient_id
          << ".iter() {\n";
      for (size_t consumer_index = 0;
           consumer_index < group.consumers.size(); ++consumer_index) {
        if (plans[consumer_index]->count_uses_exact_length) continue;
        const auto& pipeline = pipelines[consumer_index];
        const auto& terminal = pipeline.stages.back();
        string result = "__moss_shared_result_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        // A completed any/all branch may stop receiving elements only when
        // the semantic plan proves every skipped callback non-observable,
        // non-failing, and non-divergent. Otherwise the shared loop continues
        // invoking that branch exactly as eager execution would.
        bool guarded_any = terminal.kind == FunctionalNodeKind::Any &&
            plans[consumer_index]->short_circuit_terminal;
        bool guarded_all = terminal.kind == FunctionalNodeKind::All &&
            plans[consumer_index]->short_circuit_terminal;
        if (guarded_any || guarded_all)
          out << deep << "if " << (guarded_any ? "!" : "") << result
              << " {\n";
        string block_indent(spaces + (guarded_any || guarded_all ? 16 : 12),
                            ' ');
        string statement_indent(spaces +
            (guarded_any || guarded_all ? 20 : 16), ' ');
        string label = "'__moss_consumer_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        out << block_indent << label << ": {\n";
        string current_expression = "__moss_shared_value_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index) + "_0";
        bool source_copy = copy_type(*source_element);
        out << statement_indent << "let " << current_expression << " = "
            << (source_copy ? "*" : "") << "__moss_shared_item_ref_"
            << group.transient_id << ";\n";
        FunctionalValue current{current_expression, *source_element,
                                !source_copy};
        size_t map_number = 0;
        for (const auto& stage : pipeline.stages) {
          switch (stage.kind) {
            case FunctionalNodeKind::Map: {
              string next = "__moss_shared_value_" +
                  std::to_string(group.transient_id) + "_" +
                  std::to_string(consumer_index) + "_" +
                  std::to_string(++map_number);
              out << statement_indent << "let " << next << " = "
                  << render_functional_callable(stage.arguments.front(),
                                                {current}, domain, locals,
                                                &types)
                  << ";\n";
              auto mapped_type = generated_callable_result(
                  stage.arguments.front(), {current.type}, &types);
              if (!mapped_type)
                throw std::runtime_error(
                    "internal error: untyped shared functional map result");
              current = {next, *mapped_type, false};
              break;
            }
            case FunctionalNodeKind::Filter:
              out << statement_indent << "if !("
                  << render_functional_callable(stage.arguments.front(),
                                                {current}, domain, locals,
                                                &types)
                  << ") { break " << label << "; }\n";
              break;
            case FunctionalNodeKind::Reduce: {
              auto accumulator_type = generated_expr_type(
                  stage.arguments.front(), &types).value_or(current.type);
              out << statement_indent << result << " = "
                  << render_functional_callable(
                         stage.arguments[1],
                         {{result, accumulator_type, false}, current},
                         domain, locals, &types)
                  << ";\n";
              break;
            }
            case FunctionalNodeKind::Sum:
              if (canonical_type_name(current.type) == "int")
                out << statement_indent << result << " = " << result
                    << ".wrapping_add(" << current.expression << ");\n";
              else
                out << statement_indent << result << " += "
                    << current.expression << ";\n";
              break;
            case FunctionalNodeKind::Count:
              out << statement_indent << result << " = " << result
                  << ".wrapping_add(1_i64);\n";
              break;
            case FunctionalNodeKind::Any: {
              string predicate = stage.arguments.empty()
                  ? current.expression
                  : render_functional_callable(stage.arguments.front(),
                                               {current}, domain, locals,
                                               &types);
              out << statement_indent << "if " << predicate << " { "
                  << result << " = true; }\n";
              break;
            }
            case FunctionalNodeKind::All: {
              string predicate = stage.arguments.empty()
                  ? current.expression
                  : render_functional_callable(stage.arguments.front(),
                                               {current}, domain, locals,
                                               &types);
              out << statement_indent << "if !(" << predicate << ") { "
                  << result << " = false; }\n";
              break;
            }
            case FunctionalNodeKind::Source:
              break;
          }
        }
        out << block_indent << "}\n";
        if (guarded_any || guarded_all) out << deep << "}\n";
      }
      out << inner << "}\n";
    }
    out << inner << "(";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      out << "__moss_shared_result_" << group.transient_id << "_" << index;
    }
    out << ")\n" << string(spaces, ' ') << "};\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      locals.insert(consumer.result_binding);
      types[consumer.result_binding] = plans[index]->output_type;
    }
  }

  string gen_eager_functional_pipeline(
      const ParsedFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped functional source");
    string current_type = *element;
    string current_collection = "__moss_pipeline_source";
    std::ostringstream out;
    out << "{\n        // Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics";
    if (planned) out << " (plan %" << planned->transient_id
                     << ", semantic " << planned->semantic_identity << ")";
    out << "\n"
        << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";

    size_t stage_number = 0;
    for (const auto& stage : pipeline.stages) {
      if (stage.kind != FunctionalNodeKind::Map &&
          stage.kind != FunctionalNodeKind::Filter)
        continue;
      string output = "__moss_stage_" + std::to_string(stage_number);
      string item_ref = "__moss_item_ref_" + std::to_string(stage_number);
      string value = "__moss_value_" + std::to_string(stage_number);
      bool item_copy = copy_type(current_type);
      out << "        let mut " << output << " = Vec::new();\n"
          << "        for " << item_ref << " in " << current_collection
          << ".iter() {\n"
          << "            let " << value << " = "
          << (item_copy ? "*" : "") << item_ref << ";\n";
      FunctionalValue input{value, current_type, !item_copy};
      if (stage.kind == FunctionalNodeKind::Map) {
        string mapped = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types);
        out << "            " << output << ".push(" << mapped << ");\n";
        auto result = generated_callable_result(stage.arguments.front(),
                                                {current_type}, types);
        if (!result)
          throw std::runtime_error("internal error: untyped eager map result");
        current_type = *result;
      } else {
        string predicate = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types);
        out << "            if " << predicate << " { " << output << ".push("
            << value << "); }\n";
      }
      out << "        }\n";
      current_collection = output;
      ++stage_number;
    }

    const auto& terminal = pipeline.stages.back();
    bool has_terminal = terminal.kind == FunctionalNodeKind::Reduce ||
        terminal.kind == FunctionalNodeKind::Sum ||
        terminal.kind == FunctionalNodeKind::Count ||
        terminal.kind == FunctionalNodeKind::Any ||
        terminal.kind == FunctionalNodeKind::All;
    if (!has_terminal) {
      out << "        " << current_collection << "\n    }";
      return out.str();
    }

    string accumulator = "__moss_result";
    if (terminal.kind == FunctionalNodeKind::Reduce)
      out << "        let mut " << accumulator << " = "
          << expr(terminal.arguments.front(), domain, locals, types) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Sum)
      out << "        let mut " << accumulator << " = "
          << functional_zero(current_type) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Count)
      out << "        let mut " << accumulator << " = 0_i64;\n";
    else
      out << "        let mut " << accumulator << " = "
          << (terminal.kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";

    string item_ref = "__moss_terminal_ref";
    string value = "__moss_terminal_value";
    bool item_copy = copy_type(current_type);
    out << "        for " << item_ref << " in " << current_collection
        << ".iter() {\n"
        << "            let " << value << " = "
        << (item_copy ? "*" : "") << item_ref << ";\n";
    FunctionalValue input{value, current_type, !item_copy};
    if (terminal.kind == FunctionalNodeKind::Reduce) {
      auto accumulator_type = generated_expr_type(terminal.arguments.front(), types)
          .value_or(current_type);
      out << "            " << accumulator << " = "
          << render_functional_callable(
                 terminal.arguments[1],
                 {{accumulator, accumulator_type, false}, input},
                 domain, locals, types)
          << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Sum) {
      if (canonical_type_name(current_type) == "int")
        out << "            " << accumulator << " = " << accumulator
            << ".wrapping_add(" << value << ");\n";
      else
        out << "            " << accumulator << " += " << value << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Count) {
      out << "            " << accumulator << " = " << accumulator
          << ".wrapping_add(1_i64);\n";
    } else {
      string predicate = terminal.arguments.empty()
          ? value
          : render_functional_callable(terminal.arguments.front(), {input},
                                       domain, locals, types);
      if (terminal.kind == FunctionalNodeKind::Any)
        out << "            if " << predicate << " { " << accumulator
            << " = true; }\n";
      else
        out << "            if !(" << predicate << ") { " << accumulator
            << " = false; }\n";
    }
    out << "        }\n        " << accumulator << "\n    }";
    return out.str();
  }

  string gen_fused_functional_pipeline(
      const ParsedFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped fused functional source");
    string current_type = *element;
    string final_element_type = current_type;
    for (const auto& stage : pipeline.stages) {
      if (stage.kind != FunctionalNodeKind::Map) continue;
      auto result = generated_callable_result(stage.arguments.front(),
                                              {final_element_type}, types);
      if (!result)
        throw std::runtime_error("internal error: untyped fused map result");
      final_element_type = *result;
    }
    const auto& last = pipeline.stages.back();
    bool terminal = last.kind == FunctionalNodeKind::Reduce ||
        last.kind == FunctionalNodeKind::Sum ||
        last.kind == FunctionalNodeKind::Count ||
        last.kind == FunctionalNodeKind::Any ||
        last.kind == FunctionalNodeKind::All;
    std::ostringstream out;
    out << "{\n        // Moss backend: FUSED FUNCTIONAL PIPELINE; one explicit loop, intermediates eliminated";
    if (planned) {
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
    }
    out << "\n"
        << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";
    if (!terminal)
      out << "        let mut __moss_result = Vec::new();\n";
    else if (last.kind == FunctionalNodeKind::Reduce)
      out << "        let mut __moss_result = "
          << expr(last.arguments.front(), domain, locals, types) << ";\n";
    else if (last.kind == FunctionalNodeKind::Sum)
      out << "        let mut __moss_result = "
          << functional_zero(final_element_type) << ";\n";
    else if (last.kind == FunctionalNodeKind::Count)
      out << "        let mut __moss_result = 0_i64;\n";
    else
      out << "        let mut __moss_result = "
          << (last.kind == FunctionalNodeKind::All ? "true" : "false") << ";\n";
    bool source_copy = copy_type(current_type);
    out << "        for __moss_item_ref in __moss_pipeline_source.iter() {\n"
        << "            let __moss_value_0 = "
        << (source_copy ? "*" : "") << "__moss_item_ref;\n";
    FunctionalValue current{"__moss_value_0", current_type, !source_copy};
    size_t map_number = 0;
    for (const auto& stage : pipeline.stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          string next = "__moss_value_" + std::to_string(++map_number);
          out << "            let " << next << " = "
              << render_functional_callable(stage.arguments.front(), {current},
                                             domain, locals, types)
              << ";\n";
          auto result = generated_callable_result(stage.arguments.front(),
                                                  {current.type}, types);
          if (!result)
            throw std::runtime_error("internal error: untyped fused map result");
          current = {next, *result, false};
          break;
        }
        case FunctionalNodeKind::Filter:
          out << "            if !("
              << render_functional_callable(stage.arguments.front(), {current},
                                             domain, locals, types)
              << ") { continue; }\n";
          break;
        case FunctionalNodeKind::Reduce: {
          auto accumulator_type = generated_expr_type(stage.arguments.front(), types)
              .value_or(current.type);
          out << "            __moss_result = "
              << render_functional_callable(
                     stage.arguments[1],
                     {{"__moss_result", accumulator_type, false}, current},
                     domain, locals, types)
              << ";\n";
          break;
        }
        case FunctionalNodeKind::Sum:
          if (canonical_type_name(current.type) == "int")
            out << "            __moss_result = __moss_result.wrapping_add("
                << current.expression << ");\n";
          else
            out << "            __moss_result += " << current.expression << ";\n";
          break;
        case FunctionalNodeKind::Count:
          out << "            __moss_result = __moss_result.wrapping_add(1_i64);\n";
          break;
        case FunctionalNodeKind::Any: {
          string predicate = stage.arguments.empty()
              ? current.expression
              : render_functional_callable(stage.arguments.front(), {current},
                                           domain, locals, types);
          out << "            if " << predicate
              << " { __moss_result = true;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::All: {
          string predicate = stage.arguments.empty()
              ? current.expression
              : render_functional_callable(stage.arguments.front(), {current},
                                           domain, locals, types);
          out << "            if !(" << predicate
              << ") { __moss_result = false;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::Source:
          break;
      }
    }
    if (!terminal)
      out << "            __moss_result.push(" << current.expression << ");\n";
    out << "        }\n        __moss_result\n    }";
    return out.str();
  }

  std::optional<string> functional_expr(
      const string& expression, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id) const {
    auto pipeline = parse_functional_pipeline(expression);
    if (!pipeline) return std::nullopt;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type || !generated_functional_element_type(*source_type))
      return std::nullopt;
    const FunctionalPipeline* planned =
        planned_functional_pipeline(functional_pipeline_id);
    if (!planned)
      throw std::runtime_error(
          "internal error: typed functional pipeline reached Rust generation "
          "without its exact functional_pipeline_id");
    ParsedFunctionalPipeline lowered = *pipeline;
    if (planned->virtual_upstream_pipeline_id) {
      const FunctionalPipeline* upstream = planned_functional_pipeline(
          planned->virtual_upstream_pipeline_id);
      auto upstream_parsed = upstream
          ? parse_functional_pipeline(upstream->expression) : std::nullopt;
      if (!upstream_parsed || upstream_parsed->stages.empty() ||
          functional_terminal_kind(upstream_parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid virtual functional upstream plan");
      vector<ParsedFunctionalStage> stages = upstream_parsed->stages;
      stages.insert(stages.end(), lowered.stages.begin(), lowered.stages.end());
      lowered.source = upstream_parsed->source;
      lowered.stages = std::move(stages);
    }
    if (planned->count_uses_exact_length) {
      std::ostringstream out;
      out << "{\n        // Moss backend: COUNT -> EXACT LENGTH; mapped outputs are dead";
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "\n"
          << "        let __moss_pipeline_source = &("
          << expr(lowered.source, domain, locals, types) << ");\n"
          << "        __moss_pipeline_source.len() as i64\n    }";
      return out.str();
    }
    if (planned && planned->fused)
      return gen_fused_functional_pipeline(lowered, domain, locals, types,
                                           planned);
    return gen_eager_functional_pipeline(lowered, domain, locals, types,
                                         planned);
  }

  string expr(string e, const Domain* d, const std::set<string>& locals,
              const std::unordered_map<string,string>* types = nullptr,
              size_t functional_pipeline_id = 0) const {
    e = trim(std::move(e));
    if (auto functional = functional_expr(
            e, d, locals, types, functional_pipeline_id))
      return *functional;
    e = normalize_pipeline(std::move(e));
    if (e.size() >= 6 && e.find(".pop()") != string::npos) {
      auto pos = e.find(".pop()"); e.replace(pos, 6, ".pop_front()");
    }
    // Minimal surface rewrites.
    if (e == "true" || e == "false") return e;
    if (e.size() >= 2 && e.front() == '"' && e.back() == '"') return e + ".to_string()";
    if (generated_integer_literal(e)) {
      if (e.front() == '+') e.erase(e.begin());
      return e + "_i64";
    }
    if (e == "Map()") return "HashMap::new()";
    if (e == "Queue()") return "VecDeque::new()";
    if (e.size() >= 2 && e.front() == '(' && e.back() == ')' &&
        matching_paren(e, 0) == e.size() - 1)
      return "(" + expr(e.substr(1, e.size() - 2), d, locals, types) + ")";

    for (const auto& boolean_operator :
         vector<std::pair<string, string>>{{" or ", "||"}, {" and ", "&&"}}) {
      if (auto binary = generated_split_binary(e, {boolean_operator.first}))
        return "(" + expr(binary->left, d, locals, types) + ") " +
            boolean_operator.second + " (" +
            expr(binary->right, d, locals, types) + ")";
    }
    if (starts_with(e, "not "))
      return "!(" + expr(e.substr(4), d, locals, types) + ")";
    if (auto comparison =
            generated_split_binary(e, {"==", "!=", "<=", ">=", "<", ">"})) {
      string left = expr(comparison->left, d, locals, types);
      string right = expr(comparison->right, d, locals, types);
      auto left_type = generated_expr_type(comparison->left, types);
      auto right_type = generated_expr_type(comparison->right, types);
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int" &&
          generated_integer_literal(comparison->right))
        right = generated_integer_literal_as_float(comparison->right);
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int" &&
          generated_integer_literal(comparison->left))
        left = generated_integer_literal_as_float(comparison->left);
      return "(" + left + ") " + comparison->op + " (" + right + ")";
    }
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/"}}) {
      auto binary = generated_split_binary(e, operators);
      if (!binary) continue;
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      auto left_type = generated_expr_type(binary->left, types);
      auto right_type = generated_expr_type(binary->right, types);
      bool integer_operation = left_type && right_type &&
          canonical_type_name(*left_type) == "int" &&
          canonical_type_name(*right_type) == "int";
      if (integer_operation) {
        string method = binary->op == "+" ? "wrapping_add"
            : binary->op == "-" ? "wrapping_sub"
            : binary->op == "*" ? "wrapping_mul" : "wrapping_div";
        return "(" + left + ")." + method + "(" + right + ")";
      }
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int" &&
          generated_integer_literal(binary->right))
        right = generated_integer_literal_as_float(binary->right);
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int" &&
          generated_integer_literal(binary->left))
        left = generated_integer_literal_as_float(binary->left);
      return "(" + left + ") " + binary->op + " (" + right + ")";
    }
    if (e.size() >= 2 && e.front() == '[' && e.back() == ']') {
      auto parts = split_top_level(e.substr(1, e.size()-2), ',');
      std::ostringstream r; r << "vec![";
      for (size_t i=0;i<parts.size();++i) {
        if (i) r << ", ";
        string value = expr(parts[i], d, locals, types);
        auto value_type = generated_expr_type(parts[i], types);
        if (value_type && domains_.count(canonical_type_name(*value_type)))
          value = "(" + value + ").clone()";
        r << value;
      }
      r << "]"; return r.str();
    }
    string ib, ii;
    if (parse_index(e, ib, ii)) {
      bool string_index = ii.size() >= 2 && ii.front() == '"' && ii.back() == '"';
      string ir = string_index ? ii : expr(ii, d, locals, types);
      auto base_type = generated_expr_type(ib, types);
      bool map_index = base_type &&
          (canonical_type_name(*base_type) == "map" ||
           starts_with(canonical_type_name(*base_type), "map["));
      if (!string_index && !map_index) ir = "(" + ir + ") as usize";
      return "(" + expr(ib, d, locals, types) + "[" + ir + "]).clone()";
    }

    string member_receiver, member_name;
    vector<string> member_arguments;
    if (parse_member_call(e, member_receiver, member_name, member_arguments)) {
      string receiver_expression = expr(member_receiver, d, locals, types);
      auto receiver_type = generated_expr_type(member_receiver, types);
      if (receiver_type) {
        string concrete = canonical_type_name(*receiver_type);
        if (concrete == "queue" || starts_with(concrete, "queue["))
          member_name = member_name == "push" ? "push_back" : member_name == "pop" ? "pop_front" : member_name;
        if (auto method = resolve_object_method(objects_, concrete, member_name == "push_back" ? "push" : member_name == "pop_front" ? "pop" : member_name,
                                                [&]() {
                                                  vector<string> result;
                                                  for (const auto& argument : member_arguments)
                                                    result.push_back(generated_expr_type(argument, types).value_or(""));
                                                  return result;
                                                }(), true, nullptr)) {
          std::ostringstream rendered;
          rendered << receiver_expression << "." << member_name << "(";
          for (size_t index = 0; index < member_arguments.size(); ++index) {
            if (index) rendered << ", ";
            rendered << method_call_argument(*method, index, member_arguments[index], d, locals, types);
          }
          rendered << ")";
          return rendered.str();
        }
      }
      // Trait/duck-typed calls are already statically specialized by the
      // checker; retain their ordinary Rust method spelling when the concrete
      // method is not available to this expression pass.
      std::ostringstream rendered;
      rendered << receiver_expression << "." << member_name << "(";
      for (size_t index = 0; index < member_arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << expr(member_arguments[index], d, locals, types);
      }
      rendered << ")";
      return rendered.str();
    }

    string builtin;
    vector<string> builtin_args;
    if (parse_simple_call(e, builtin, builtin_args)) {
      if (builtin == "sqrt" && builtin_args.size() == 1)
        return "(" + expr(builtin_args.front(), d, locals, types) + ").sqrt()";
      if (builtin == "sum" && builtin_args.size() == 1) {
        auto argument_type = generated_expr_type(builtin_args.front(), types);
        string element_type;
        if (argument_type && starts_with(*argument_type, "vector[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              7, argument_type->size() - 8));
        else if (argument_type && starts_with(*argument_type, "seq[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              4, argument_type->size() - 5));
        string result = expr(builtin_args.front(), d, locals, types) +
            ".iter().copied()";
        if (element_type == "int")
          return result +
              ".fold(0_i64, |__moss_sum, __moss_value| "
              "__moss_sum.wrapping_add(__moss_value))";
        if (!element_type.empty())
          return result + ".sum::<" + rust_type(element_type) + ">()";
        return result + ".sum()";
      }
    }

    // Named value-object construction. Moss prefers `=` here; `:` remains a
    // compatibility spelling for existing sources.
    auto lp0 = e.find('(');
    if (lp0 != string::npos && ends_with(e, ")")) {
      string head = trim(e.substr(0, lp0));
      auto oit = objects_.find(head);
      if (oit != objects_.end()) {
        auto parts = split_top_level(e.substr(lp0 + 1, e.size() - lp0 - 2), ',');
        std::unordered_map<string,string> values;
        if (parts.size() == 1 && parts[0].empty()) parts.clear();
        for (const auto& part : parts) {
          string field_name, field_value;
          if (!parse_named_argument(part, field_name, field_value))
            throw std::runtime_error("object constructor fields must be named");
          values[field_name] = field_value;
        }
        std::ostringstream r;
        r << head << " { ";
        bool first = true;
        for (const auto& f : oit->second->fields) {
          auto vit = values.find(f.name);
          if (vit == values.end()) throw std::runtime_error("missing object constructor field: " + head + "." + f.name);
          string v = expr(vit->second, d, locals, types);
          if (!first) r << ", ";
          first = false;
          r << f.name << ": " << v;
        }
        r << " }";
        return r.str();
      }
      vector<string> call_args;
      if (parse_simple_call(e, head, call_args)) {
        bool known_function = functions_.count(head);
        std::ostringstream r; if (d && objects_.count(d->name) && !known_function) r << "self.";
        r << emitted_function_name(head, call_args, types) << "(";
        size_t emitted_arguments = 0;
        for (size_t i = 0; i < call_args.size(); ++i) {
          bool compile_time_callable = known_function &&
              generated_expr_type(call_args[i], types).value_or("").rfind(
                  "callable:", 0) == 0;
          if (compile_time_callable) continue;
          if (emitted_arguments++) r << ", ";
          if (known_function)
            r << function_call_argument(*functions_.at(head), i, call_args[i], d, locals, types);
          else
            r << expr(call_args[i], d, locals, types);
        }
        r << ")"; return r.str();
      }
    }

    // Replace domain state identifiers with state.<name>, respecting basic identifier boundaries.
    if (d) {
      std::set<string> fields;
      for (const auto& f : d->state) fields.insert(f.name);
      string out;
      bool in_str = false, esc = false;
      for (size_t i = 0; i < e.size();) {
        char c = e[i];
        if (in_str) {
          out.push_back(c);
          if (esc) esc = false;
          else if (c == '\\') esc = true;
          else if (c == '"') in_str = false;
          ++i;
          continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); ++i; continue; }
        if (std::isalpha((unsigned char)c) || c == '_') {
          size_t j = i + 1;
          while (j < e.size() && (std::isalnum((unsigned char)e[j]) || e[j] == '_')) ++j;
          string tok = e.substr(i, j-i);
          // If already preceded by '.', it is a member name, not state identifier.
          bool member = i > 0 && e[i-1] == '.';
          if (!member && fields.count(tok) && !locals.count(tok))
            out += (objects_.count(d->name) ? "self." : "state.") + tok;
          else out += tok;
          i = j;
        } else { out.push_back(c); ++i; }
      }
      e = out;
      if (objects_.count(d->name)) {
        for (const auto& f : d->state) if (canonical_type_name(f.type) == "float") {
          for (const auto& op : {string(" > "), string(" < "), string(" >= "), string(" <= "), string(" == "), string(" != ")}) {
            string needle = "self." + f.name + op + "0";
            auto pos = e.find(needle);
            if (pos != string::npos) e.replace(pos, needle.size(), "self." + f.name + op + "0.0");
          }
        }
      }
    }
    // Nim-ish boolean words.
    replace_word(e, "and", "&&");
    replace_word(e, "or", "||");
    replace_word(e, "not", "!");
    return e;
  }

  static void replace_word(string& s, const string& from, const string& to) {
    string out;
    for (size_t i = 0; i < s.size();) {
      if (i + from.size() <= s.size() && s.compare(i, from.size(), from) == 0 &&
          (i == 0 || !(std::isalnum((unsigned char)s[i-1]) || s[i-1] == '_')) &&
          (i+from.size() == s.size() || !(std::isalnum((unsigned char)s[i+from.size()]) || s[i+from.size()] == '_'))) {
        out += to; i += from.size();
      } else out.push_back(s[i++]);
    }
    s.swap(out);
  }

  static string replace_unqualified_word(const string& expression,
                                         const string& from, const string& to) {
    string out;
    for (size_t index = 0; index < expression.size();) {
      if (!(std::isalpha(static_cast<unsigned char>(expression[index])) ||
            expression[index] == '_')) {
        out.push_back(expression[index++]);
        continue;
      }
      size_t start = index;
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      size_t previous = start;
      while (previous > 0 &&
             std::isspace(static_cast<unsigned char>(expression[previous - 1])))
        --previous;
      if (expression.substr(start, end - start) == from &&
          (previous == 0 || expression[previous - 1] != '.'))
        out += to;
      else
        out.append(expression, start, end - start);
      index = end;
    }
    return out;
  }

  static string fresh_generated_name(const string& base, std::set<string>& used) {
    string candidate = base;
    for (size_t suffix = 0; used.count(candidate); ++suffix)
      candidate = base + "_" + std::to_string(suffix);
    used.insert(candidate);
    return candidate;
  }

  string message_arg(const string& e, const string& type, const Domain* d,
                     const std::set<string>& locals,
                     const std::unordered_map<string,string>* types = nullptr,
                     size_t functional_pipeline_id = 0) const {
    string r = expr(e, d, locals, types, functional_pipeline_id);
    string t = trim(type);
    if (copy_type(t) || t == "_") return r;
    // Messages are the explicit Moss semantic copy boundary.  A payload is
    // detached from the sender even when the source binding remains available;
    // the generated clone is an implementation of that boundary, never an
    // implicit copy for an ordinary local call.
    return "(" + r + ").clone()";
  }

  string cluster_call_arg(const string& expression, const string& type,
                          const Domain* source, const std::set<string>& locals,
                          size_t cluster, bool crosses_thread,
                          const std::unordered_map<string,string>* types = nullptr,
                          size_t functional_pipeline_id = 0) const {
    string value_type = trim(type);
    if (domains_.count(value_type)) {
      if (plan_.cluster_for(value_type) == std::optional<size_t>(cluster)) {
        if (crosses_thread)
          return "self." + snake_case(value_type) + "_ref.clone()";
        return value_type + "LocalRef";
      }
      string value = expr(expression, source, locals, types,
                          functional_pipeline_id);
      if (crosses_thread) return "(" + value + ").as_ref().clone()";
      return "(" + value + ").clone()";
    }
    return message_arg(expression, type, source, locals, types,
                       functional_pipeline_id);
  }

  void gen_tracker(std::ostringstream& o, bool synchronized) {
    if (!synchronized) {
      backend_comment(o, 0, "mailbox-free completion tracker; direct operations finish before returning");
      o << "struct MossTracker;\n";
      o << "impl MossTracker {\n";
      o << "    fn new() -> Self { Self }\n";
      o << "    fn wait_zero(&self) -> bool { true }\n";
      o << "}\n\n";
      return;
    }
    backend_comment(o, 0, "runtime completion tracking for generated domain work");
    o << "struct MossTrackerState { pending: usize, failed: bool }\n";
    o << "struct MossTracker { state: Mutex<MossTrackerState>, cv: Condvar }\n";
    o << "impl MossTracker {\n";
    o << "    fn new() -> Self { Self { state: Mutex::new(MossTrackerState { pending: 0, failed: false }), cv: Condvar::new() } }\n";
    o << "    fn begin(&self) { let mut state = self.state.lock().unwrap(); state.pending += 1; }\n";
    o << "    fn begin_n(&self, count: usize) { let mut state = self.state.lock().unwrap(); state.pending += count; }\n";
    o << "    fn end(&self) { let mut state = self.state.lock().unwrap(); state.pending -= 1; if state.pending == 0 { self.cv.notify_all(); } }\n";
    o << "    fn end_n(&self, count: usize) { let mut state = self.state.lock().unwrap(); state.pending -= count; if state.pending == 0 { self.cv.notify_all(); } }\n";
    o << "    fn fail(&self) { let mut state = self.state.lock().unwrap(); state.failed = true; self.cv.notify_all(); }\n";
    o << "    fn wait_zero(&self) -> bool {\n";
    o << "        let mut state = self.state.lock().unwrap();\n";
    o << "        while state.pending != 0 && !state.failed { state = self.cv.wait(state).unwrap(); }\n";
    o << "        !state.failed\n";
    o << "    }\n";
    o << "}\n\n";
  }

  void gen_shared_channel(std::ostringstream& o) {
    backend_comment(o, 0, "MESSAGE/MAILBOX implementation: Moss messages are enqueued in shared memory under Mutex and awaited with Condvar");
    o << "struct MossChannelState<T> { queue: VecDeque<T>, senders: usize, receiver_open: bool }\n";
    o << "struct MossChannel<T> { state: Mutex<MossChannelState<T>>, ready: Condvar }\n";
    o << "struct MossSender<T> { channel: Arc<MossChannel<T>> }\n";
    o << "struct MossReceiver<T> { channel: Arc<MossChannel<T>> }\n\n";
    o << "fn moss_channel<T>() -> (MossSender<T>, MossReceiver<T>) {\n";
    o << "    let channel = Arc::new(MossChannel {\n";
    o << "        state: Mutex::new(MossChannelState { queue: VecDeque::new(), senders: 1, receiver_open: true }),\n";
    o << "        ready: Condvar::new(),\n";
    o << "    });\n";
    o << "    (MossSender { channel: channel.clone() }, MossReceiver { channel })\n";
    o << "}\n\n";
    o << "impl<T> Clone for MossSender<T> {\n";
    o << "    fn clone(&self) -> Self {\n";
    o << "        let mut state = self.channel.state.lock().unwrap();\n";
    o << "        state.senders += 1;\n";
    o << "        drop(state);\n";
    o << "        Self { channel: self.channel.clone() }\n";
    o << "    }\n";
    o << "}\n\n";
    o << "impl<T> MossSender<T> {\n";
    o << "    fn send(&self, value: T) -> Result<(), T> {\n";
    o << "        let mut state = self.channel.state.lock().unwrap();\n";
    o << "        if !state.receiver_open { return Err(value); }\n";
    o << "        state.queue.push_back(value);\n";
    o << "        drop(state);\n";
    o << "        self.channel.ready.notify_one();\n";
    o << "        Ok(())\n";
    o << "    }\n";
    o << "    // Lock-coalesced enqueue path: append a contiguous batch under one mutex guard.\n";
    o << "    #[allow(dead_code)]\n";
    o << "    fn send_batch(&self, values: Vec<T>) -> Result<(), Vec<T>> {\n";
    o << "        let mut state = self.channel.state.lock().unwrap();\n";
    o << "        if !state.receiver_open { return Err(values); }\n";
    o << "        state.queue.extend(values);\n";
    o << "        drop(state);\n";
    o << "        self.channel.ready.notify_one();\n";
    o << "        Ok(())\n";
    o << "    }\n";
    o << "}\n\n";
    o << "impl<T> Drop for MossSender<T> {\n";
    o << "    fn drop(&mut self) {\n";
    o << "        let mut state = self.channel.state.lock().unwrap();\n";
    o << "        state.senders -= 1;\n";
    o << "        let closed = state.senders == 0;\n";
    o << "        drop(state);\n";
    o << "        if closed { self.channel.ready.notify_all(); }\n";
    o << "    }\n";
    o << "}\n\n";
    o << "impl<T> MossReceiver<T> {\n";
    o << "    fn recv(&self) -> Result<T, ()> {\n";
    o << "        let mut state = self.channel.state.lock().unwrap();\n";
    o << "        loop {\n";
    o << "            if let Some(value) = state.queue.pop_front() { return Ok(value); }\n";
    o << "            if state.senders == 0 { return Err(()); }\n";
    o << "            state = self.channel.ready.wait(state).unwrap();\n";
    o << "        }\n";
    o << "    }\n";
    o << "}\n\n";
    o << "impl<T> Drop for MossReceiver<T> {\n";
    o << "    fn drop(&mut self) {\n";
    o << "        let abandoned = {\n";
    o << "            let mut state = self.channel.state.lock().unwrap();\n";
    o << "            state.receiver_open = false;\n";
    o << "            std::mem::take(&mut state.queue)\n";
    o << "        };\n";
    o << "        self.channel.ready.notify_all();\n";
    o << "        drop(abandoned);\n";
    o << "    }\n";
    o << "}\n\n";
  }

  void gen_object(std::ostringstream& o, const ObjectType& t) {
    source_comment(o, 0, t.line, t.header.empty() ? "type " + t.name : t.header);
    backend_comment(o, 0, "value representation for the Moss object; object ownership stays in Moss");
    o << "#[derive(Clone, Debug)]\nstruct " << t.name << " {\n";
    for (const auto& f : t.fields) {
      source_comment(o, 4, f.line, f.header.empty() ? f.name + ": " + f.type : f.header);
      o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    }
    o << "}\n\n";
    for (const auto& method : t.methods) {
      o << "impl " << t.name << " {\n    fn " << method.name;
      if (method.receiver_effect == Effect::Consume) o << "(self";
      else if (method.receiver_effect == Effect::Write) o << "(&mut self";
      else o << "(&self";
      for (const auto& p : method.params) {
        if (p.type.empty()) throw std::runtime_error("unresolved concrete method parameter type: " + method.name + "." + p.name);
        size_t parameter_index = static_cast<size_t>(&p - method.params.data());
        Effect effect = method_effect(method, parameter_index);
        string parameter_type = rust_type(p.type);
        if (effect != Effect::Consume && borrowable_type(p.type)) {
          o << ", " << (effect == Effect::Write ? "" : "") << p.name << ": "
            << (effect == Effect::Write ? "&mut " : "&") << parameter_type;
        } else {
          o << ", mut " << p.name << ": " << parameter_type;
        }
      }
      if (method.return_type) o << ") -> " << rust_type(*method.return_type) << " {\n";
      else o << ") {\n";
      Domain receiver;
      receiver.name = t.name;
      receiver.state = t.fields;
      std::set<string> locals;
      std::unordered_map<string,string> types;
      for (const auto& field : t.fields) types[field.name] = field.type;
      for (const auto& p : method.params) { locals.insert(p.name); types[p.name] = p.type; }
      gen_stmts(o, method.body, &receiver, nullptr, "", locals, types, 2, false, false,
                std::nullopt, true, functional_method_context(t, method));
      if (method.result_expression) {
        string context = functional_method_context(t, method);
        auto plan = method.result_functional_pipeline_ids.find(context);
        source_comment(o, 8,
                       method.result_line ? method.result_line : method.line,
                       *method.result_expression);
        o << "        "
          << expr(*method.result_expression, &receiver, locals, &types,
                  plan == method.result_functional_pipeline_ids.end()
                      ? 0 : plan->second)
          << "\n";
      }
      o << "    }\n";
      o << "}\n\n";
    }
  }

  void gen_function_instance(std::ostringstream& o, const Function& f,
                             const FunctionSpecialization* specialization) {
    bool container_generic = !specialization &&
        std::any_of(f.params.begin(), f.params.end(), [](const Param& p) {
          return p.type == "vector" || p.type == "queue" || p.type == "map";
        });
    source_comment(o, 0, f.line, f.header.empty() ? "fn " + f.name : f.header);
    backend_comment(o, 0, specialization
        ? "STATIC specialization: concrete local function selected before Rust generation"
        : "LOCAL function: ordinary intra-domain call; no Moss mailbox or domain scheduling");
    o << "fn " << (specialization ? specialization->generated_name : f.name);
    if (!specialization && (f.generic || container_generic)) {
      o << "<";
      size_t gi = 0; for (const auto& p : f.params) if (constraint_ops(f, p.name).size() || p.type == "vector" || p.type == "queue") { if (gi++) o << ", "; o << "T_" << p.name; }
      o << ">";
    }
    o << "(";
    size_t emitted_parameters = 0;
    for (size_t index = 0; index < f.params.size(); ++index) {
      string pt = specialization ? specialization->parameter_types[index]
                                 : f.params[index].type;
      // A callable parameter in a static higher-order specialization is a
      // compile-time identity, not a Rust function value.
      if (specialization && starts_with(pt, "callable:")) continue;
      if (emitted_parameters++) o << ", ";
      auto ops = constraint_ops(f, f.params[index].name);
      string parameter_rust_type;
      if (!specialization && pt.empty() && !ops.empty()) {
        if (ops.count("[]")) parameter_rust_type = "Vec<T_" + f.params[index].name + ">";
        else parameter_rust_type = "T_" + f.params[index].name;
      } else if (!specialization && pt == "vector") {
        parameter_rust_type = "Vec<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "queue") {
        parameter_rust_type = "VecDeque<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "map") {
        parameter_rust_type = "HashMap<K_" + f.params[index].name + ", V_" + f.params[index].name + ">";
      } else {
        parameter_rust_type = rust_type(pt);
      }
      Effect effect = function_effect(f, index);
      bool generic_copy_value = !specialization && f.params[index].type.empty() &&
          !ops.empty() && !ops.count("[]");
      bool borrow = !generic_copy_value && effect != Effect::Consume &&
          borrowable_type(pt.empty() ? parameter_rust_type : pt);
      o << f.params[index].name << ": "
        << (borrow ? (effect == Effect::Write ? "&mut " : "&") : "")
        << parameter_rust_type;
    }
    string return_type = specialization ? specialization->return_type
                                        : f.return_type.value_or("unit");
    if (return_type != "unit") {
      string rr = return_type;
      if (!specialization && starts_with(rr, "_generic:")) rr = "T_" + rr.substr(9);
      else if (!specialization && starts_with(rr, "_element:_element:")) rr = "T_" + rr.substr(17);
      else if (!specialization && starts_with(rr, "_element:element:")) rr = "T_" + rr.substr(17);
      o << ") -> " << (rr == return_type ? rust_type(rr) : rr);
      if (!specialization && (f.generic || container_generic)) {
        o << " where "; size_t bi=0;
        for (const auto& p : f.params) { auto ops = constraint_ops(f, p.name); if (ops.empty() && p.type != "vector" && p.type != "queue") continue; if (bi++) o << ", "; o << "T_" << p.name << ": ";
          bool idx = ops.count("[]");
          if (idx) o << "Clone";
          else {
            string op = ops.count("+") ? "Add" : ops.count("*") ? "Mul" : ops.count("-") ? "Sub" : "Div";
            o << "std::ops::" << op << "<Output = T_" << p.name << "> + Copy";
          }
        }
      }
      o << " {\n";
    }
    else o << ") {\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    for (size_t index = 0; index < f.params.size(); ++index) {
      locals.insert(f.params[index].name);
      types[f.params[index].name] = specialization
          ? specialization->parameter_types[index] : f.params[index].type;
    }
    string functional_context = functional_function_context(f, specialization);
    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false, false,
              std::nullopt, true, functional_context);
    if (f.result_expression) {
      source_comment(o, 4, f.result_line, *f.result_expression);
      auto plan = f.result_functional_pipeline_ids.find(functional_context);
      o << "    " << expr(*f.result_expression, nullptr, locals, &types,
                            plan == f.result_functional_pipeline_ids.end()
                                ? 0 : plan->second)
        << "\n";
    }
    o << "}\n\n";
  }

  void gen_function(std::ostringstream& o, const Function& f) {
    if (f.static_dispatch) {
      for (const auto& specialization : f.specializations)
        gen_function_instance(o, f, &specialization);
      return;
    }
    gen_function_instance(o, f, nullptr);
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    source_comment(o, 0, d.line, d.header.empty() ? "domain " + d.name : d.header);
    if (cluster_for(d)) {
      backend_comment(o, 0, "CLUSTER ingress/message version of " + d.name + "Ref (shared-memory mailbox)");
    } else if (direct_atomic(d)) {
      backend_comment(o, 0, "ATOMIC DOMAIN reference (Arc<" + d.name + "State>; no mailbox or state lock)");
    } else if (plan_.lowering_for(d) == DomainLowering::DirectRwLock) {
      backend_comment(o, 0, "SHARED-MEMORY DIRECT version of " + d.name + "Ref (Arc<RwLock<" + d.name + "State>>)");
    } else if (plan_.lowering_for(d) == DomainLowering::DirectMutex) {
      backend_comment(o, 0, "SHARED-MEMORY DIRECT version of " + d.name + "Ref (Arc<Mutex<" + d.name + "State>>)");
    } else {
      backend_comment(o, 0, "MESSAGE/MAILBOX version of " + d.name + "Ref (shared-memory lock-backed queue)");
    }
    o << "#[derive(Clone)]\nstruct " << d.name << "Ref {\n";
    if (auto cluster = cluster_for(d)) {
      o << "    tx: MossSender<MossCluster" << *cluster << "SharedMsg>,\n";
      o << "    tracker: Arc<MossTracker>,\n";
    } else if (direct_atomic(d)) {
      o << "    state: Arc<" << d.name << "State>,\n";
    } else if (plan_.lowering_for(d) == DomainLowering::DirectRwLock) {
      o << "    state: Arc<RwLock<" << d.name << "State>>,\n";
    } else if (plan_.lowering_for(d) == DomainLowering::DirectMutex) {
      o << "    state: Arc<Mutex<" << d.name << "State>>,\n";
    } else {
      o << "    tx: MossSender<" << d.name << "Msg>,\n";
      o << "    tracker: Arc<MossTracker>,\n";
    }
    o << "}\n\n";
  }

  static string reply_binding(const Domain& d, const Handler& h) {
    std::set<string> used;
    for (const auto& f : d.state) used.insert(f.name);
    for (const auto& p : h.params) used.insert(p.name);
    for (const auto& s : h.body) {
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var || s.kind == Stmt::Kind::AwaitMessage)
        used.insert(s.a);
    }
    string name = "__moss_reply";
    for (size_t suffix = 0; used.count(name); ++suffix)
      name = "__moss_reply_" + std::to_string(suffix);
    return name;
  }

  static bool handler_needs_label(const Handler& h) {
    return std::any_of(h.body.begin(), h.body.end(), [](const Stmt& s) {
      return s.kind == Stmt::Kind::Return || s.kind == Stmt::Kind::Reply;
    });
  }

  const HandlerEffectSummary& handler_effects(const Domain& domain,
                                              const Handler& handler) const {
    return plan_.handler_effects.at(domain.name + "." + handler.name);
  }

  const AtomicHandlerPlan& atomic_handler(const Domain& domain,
                                          const Handler& handler) const {
    return plan_.atomic_handlers.at(domain.name + "." + handler.name);
  }

  void gen_direct_domain(std::ostringstream& o, const Domain& d) {
    bool rwlock = plan_.lowering_for(d) == DomainLowering::DirectRwLock;
    backend_comment(o, 0, "SHARED-MEMORY DIRECT implementation for domain " + d.name +
        (rwlock ? "; handler effects select RwLock read/write guards"
                : "; awaited Moss messages call through Arc<Mutex<State>>"));
    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      if (!h.reply_type)
        throw std::runtime_error("internal error: one-way handler reached direct shared-memory generation");
      string result_name = reply_binding(d, h);
      bool read_only = rwlock && handler_effects(d, h).state_effect == Effect::Read;
      source_comment(o, 4, h.line, handler_signature(h));
      backend_comment(o, 4, read_only
          ? "READ-SHARED RwLock handler body; locking is separated from implementation"
          : "SHARED-MEMORY DIRECT handler body; locking is separated from implementation");
      o << "    fn " << h.name << "_locked(&self, state: "
        << (read_only ? "&" : "&mut ") << d.name << "State";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      o << ") -> Option<" << rust_type(*h.reply_type) << "> {\n";
      o << "        let self_ref = self.clone();\n";
      o << "        let mut " << result_name << ": Option<" << rust_type(*h.reply_type)
        << "> = None;\n";
      o << "        'handler: {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      types["self"] = d.name;
      for (const auto& f : d.state) types[f.name] = f.type;
      for (const auto& p : h.params) {
        locals.insert(p.name);
        types[p.name] = p.type;
      }
      locals.insert("self_ref");
      locals.insert(result_name);
      gen_stmts(o, h.body, &d, &h, result_name, locals, types, 3, true, true,
                std::nullopt, false, functional_handler_context(d, h));
      o << "        }\n";
      o << "        " << result_name << "\n";
      o << "    }\n";

      backend_comment(o, 4, read_only
          ? "READ-SHARED RwLock handler wrapper"
          : "exclusive direct handler wrapper");
      o << "    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      o << ") -> Option<" << rust_type(*h.reply_type) << "> {\n";
      if (rwlock) {
        o << "        let " << (read_only ? "" : "mut ")
          << "state = self.state." << (read_only ? "read" : "write")
          << "().unwrap();\n";
      } else {
        o << "        let mut state = self.state.lock().unwrap();\n";
      }
      o << "        self." << h.name << "_locked("
        << (read_only ? "&state" : "&mut state");
      for (const auto& p : h.params) o << ", " << p.name;
      o << ")\n";
      o << "    }\n";
    }
    o << "}\n\n";

    o << "fn spawn_" << snake_case(d.name) << "(_tracker: Arc<MossTracker>) -> "
      << d.name << "Ref {\n";
    backend_comment(o, 4, "SHARED-MEMORY DIRECT state allocation for domain " + d.name);
    o << "    __moss_require_send::<" << d.name << "State>();\n";
    for (const auto& h : d.handlers) {
      for (const auto& p : h.params)
        o << "    __moss_require_send::<" << rust_type(p.type) << ">();\n";
      o << "    __moss_require_send::<" << rust_type(*h.reply_type) << ">();\n";
    }
    o << "    let state = " << d.name << "State {\n";
    for (const auto& f : d.state) {
      string init = f.init.empty() ? default_value(f.type) : expr(f.init, nullptr, {});
      if (f.type == "string" && !init.empty() && init.front() == '"' && init.back() == '"') init += ".to_string()";
      source_comment(o, 8, f.line, state_field_signature(f));
      o << "        " << f.name << ": " << init << ",\n";
    }
    o << "    };\n";
    o << "    " << d.name << "Ref { state: Arc::new("
      << (rwlock ? "RwLock" : "Mutex") << "::new(state)) }\n";
    o << "}\n\n";
  }

  void gen_atomic_domain(std::ostringstream& o, const Domain& d) {
    backend_comment(o, 0, "ATOMIC DOMAIN implementation for " + d.name +
        "; every handler is one SeqCst linearizable state action");
    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      const AtomicHandlerPlan& action = atomic_handler(d, h);
      source_comment(o, 4, h.line, handler_signature(h));
      backend_comment(o, 4, "ATOMIC DOMAIN handler; no worker, mailbox, or state lock");
      o << "    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      if (h.reply_type) o << ") -> Option<" << rust_type(*h.reply_type) << "> {\n";
      else o << ") {\n";

      std::set<string> locals;
      std::unordered_map<string, string> types;
      for (const auto& p : h.params) {
        locals.insert(p.name);
        types[p.name] = p.type;
      }
      std::set<string> used_names = locals;
      used_names.insert("self");
      for (const auto& field : d.state) used_names.insert(field.name);
      string value_name = fresh_generated_name("__moss_value", used_names);
      string operand_name = fresh_generated_name("__moss_operand", used_names);
      string previous_name = fresh_generated_name("__moss_previous", used_names);
      string next_name = fresh_generated_name("__moss_next", used_names);
      string operand = action.operand.empty()
          ? string() : expr(action.operand, nullptr, locals, &types);
      switch (action.action) {
        case AtomicActionKind::Load:
          o << "        let " << value_name << " = self.state." << action.field
            << ".load(Ordering::SeqCst);\n";
          break;
        case AtomicActionKind::Store:
          o << "        let " << value_name << " = " << operand << ";\n";
          o << "        self.state." << action.field
            << ".store(" << value_name << ", Ordering::SeqCst);\n";
          break;
        case AtomicActionKind::FetchAdd:
          o << "        let " << operand_name << " = " << operand << ";\n";
          o << "        let " << previous_name << " = self.state." << action.field
            << ".fetch_add(" << operand_name << ", Ordering::SeqCst);\n";
          o << "        let " << value_name << " = " << previous_name
            << ".wrapping_add(" << operand_name << ");\n";
          break;
        case AtomicActionKind::FetchSub:
          o << "        let " << operand_name << " = " << operand << ";\n";
          o << "        let " << previous_name << " = self.state." << action.field
            << ".fetch_sub(" << operand_name << ", Ordering::SeqCst);\n";
          o << "        let " << value_name << " = " << previous_name
            << ".wrapping_sub(" << operand_name << ");\n";
          break;
        case AtomicActionKind::FetchXor:
          o << "        let " << previous_name << " = self.state." << action.field
            << ".fetch_xor(true, Ordering::SeqCst);\n";
          o << "        let " << value_name << " = !" << previous_name << ";\n";
          break;
        case AtomicActionKind::Swap:
          o << "        let " << next_name << " = " << operand << ";\n";
          o << "        let " << value_name << " = self.state." << action.field
            << ".swap(" << next_name << ", Ordering::SeqCst);\n";
          break;
      }
      if (h.reply_type) {
        locals.insert(value_name);
        auto field = std::find_if(d.state.begin(), d.state.end(),
            [&](const Field& candidate) { return candidate.name == action.field; });
        if (field != d.state.end()) types[value_name] = field->type;
        string result_expression = action.result_expression.empty()
            ? value_name
            : replace_unqualified_word(action.result_expression, action.field,
                                       value_name);
        o << "        Some(" << message_arg(result_expression, *h.reply_type,
                                              nullptr, locals, &types) << ")\n";
      }
      o << "    }\n";
    }
    o << "}\n\n";

    o << "fn spawn_" << snake_case(d.name) << "(_tracker: Arc<MossTracker>) -> "
      << d.name << "Ref {\n";
    backend_comment(o, 4, "ATOMIC DOMAIN allocation; no dedicated thread or mailbox");
    o << "    __moss_require_send::<" << d.name << "State>();\n";
    for (const auto& h : d.handlers) {
      for (const auto& p : h.params)
        o << "    __moss_require_send::<" << rust_type(p.type) << ">();\n";
      if (h.reply_type)
        o << "    __moss_require_send::<" << rust_type(*h.reply_type) << ">();\n";
    }
    o << "    " << d.name << "Ref { state: Arc::new(" << d.name << "State {\n";
    for (const auto& f : d.state) {
      string init = f.init.empty() ? default_value(f.type) : expr(f.init, nullptr, {});
      source_comment(o, 8, f.line, state_field_signature(f));
      o << "        " << f.name << ": "
        << (f.type == "bool" ? "AtomicBool" : "AtomicI64")
        << "::new(" << init << "),\n";
    }
    o << "    }) }\n";
    o << "}\n\n";
  }

  void gen_domain(std::ostringstream& o, const Domain& d) {
    source_comment(o, 0, d.line, d.header.empty() ? "domain " + d.name : d.header);
    if (cluster_for(d)) {
      backend_comment(o, 0, "CLUSTER-LOCAL state representation for domain " + d.name + " (RefCell on the cluster worker)");
    } else if (direct_atomic(d)) {
      backend_comment(o, 0, "ATOMIC DOMAIN state representation for domain " + d.name + " (SeqCst linearization)");
    } else if (direct_lock(d)) {
      backend_comment(o, 0, "SHARED-MEMORY DIRECT state representation for domain " + d.name);
    } else {
      backend_comment(o, 0, "MESSAGE/MAILBOX state representation for domain " + d.name + " (one worker drains a shared-memory queue)");
    }
    o << "struct " << d.name << "State {\n";
    for (const auto& f : d.state) {
      source_comment(o, 4, f.line, state_field_signature(f));
      if (direct_atomic(d))
        o << "    " << f.name << ": " << (f.type == "bool" ? "AtomicBool" : "AtomicI64") << ",\n";
      else
        o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    }
    o << "}\n\n";

    if (cluster_for(d)) return;

    if (direct_atomic(d)) {
      gen_atomic_domain(o, d);
      return;
    }

    if (direct_lock(d)) {
      gen_direct_domain(o, d);
      return;
    }

    backend_comment(o, 0, "MESSAGE/MAILBOX handler messages for domain " + d.name + " (Rust enum plus shared-memory queue)");
    o << "enum " << d.name << "Msg {\n";
    for (const auto& h : d.handlers) {
      source_comment(o, 4, h.line, handler_signature(h));
      o << "    " << h.name;
      if (!h.params.empty() || h.reply_type) {
        o << "(";
        size_t count = 0;
        for (size_t i = 0; i < h.params.size(); ++i, ++count) {
          if (count) o << ", ";
          o << rust_type(h.params[i].type);
        }
        if (h.reply_type) {
          if (count) o << ", ";
          o << "MossSender<" << rust_type(*h.reply_type) << ">";
        }
        o << ")";
      }
      o << ",\n";
    }
    o << "}\n\n";

    backend_comment(o, 0, "MESSAGE/MAILBOX send methods for domain " + d.name);
    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      string reply_name = reply_binding(d, h);
      source_comment(o, 4, h.line, handler_signature(h));
      backend_comment(o, 4, "MESSAGE/MAILBOX version: enqueue the Moss handler invocation");
      o << "    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      if (h.reply_type) o << ", " << reply_name << ": MossSender<" << rust_type(*h.reply_type) << ">";
      o << ") {\n        self.tracker.begin();\n        if self.tx.send(" << d.name << "Msg::" << h.name;
      if (!h.params.empty() || h.reply_type) {
        o << "(";
        size_t count = 0;
        for (size_t i = 0; i < h.params.size(); ++i, ++count) {
          if (count) o << ", ";
          o << h.params[i].name;
        }
        if (h.reply_type) {
          if (count) o << ", ";
          o << reply_name;
        }
        o << ")";
      }
      o << ").is_err() { self.tracker.end(); }\n    }\n";
    }
    backend_comment(o, 4, "BATCHED MAILBOX SEND helper: one tracker update and one queue lock");
    o << "    fn __moss_send_batch(&self, messages: Vec<" << d.name << "Msg>) {\n";
    o << "        let count = messages.len();\n";
    o << "        self.tracker.begin_n(count);\n";
    o << "        if let Err(unsent) = self.tx.send_batch(messages) {\n";
    o << "            self.tracker.end_n(unsent.len());\n";
    o << "        }\n";
    o << "    }\n";
    o << "}\n\n";

    backend_comment(o, 0, "MESSAGE/MAILBOX worker for domain " + d.name + "; each dequeued Moss message runs to completion");
    o << "fn spawn_" << snake_case(d.name) << "(tracker: Arc<MossTracker>) -> " << d.name << "Ref {\n";
    o << "    __moss_require_send::<" << d.name << "Msg>();\n";
    o << "    let (tx, rx): (MossSender<" << d.name << "Msg>, MossReceiver<" << d.name
      << "Msg>) = moss_channel();\n";
    o << "    let actor = " << d.name << "Ref { tx: tx.clone(), tracker: tracker.clone() };\n";
    o << "    let self_ref = actor.clone();\n";
    o << "    thread::spawn(move || {\n";
    o << "        let mut state = " << d.name << "State {\n";
    for (const auto& f : d.state) {
      string init = f.init.empty() ? default_value(f.type) : expr(f.init, nullptr, {});
      if (f.type == "string" && !init.empty() && init.front() == '"' && init.back() == '"') init += ".to_string()";
      source_comment(o, 12, f.line, state_field_signature(f));
      o << "            " << f.name << ": " << init << ",\n";
    }
    o << "        };\n";
    o << "        while let Ok(msg) = rx.recv() {\n";
    o << "            match msg {\n";
    for (const auto& h : d.handlers) {
      string reply_name = reply_binding(d, h);
      source_comment(o, 16, h.line, handler_signature(h));
      backend_comment(o, 16, "MESSAGE/MAILBOX dispatch: run the dequeued Moss handler");
      o << "                " << d.name << "Msg::" << h.name;
      if (!h.params.empty() || h.reply_type) {
        o << "(";
        size_t count = 0;
        for (size_t i = 0; i < h.params.size(); ++i, ++count) {
          if (count) o << ", ";
          o << h.params[i].name;
        }
        if (h.reply_type) {
          if (count) o << ", ";
          o << reply_name;
        }
        o << ")";
      }
      o << " => {\n";
      o << "                    ";
      if (handler_needs_label(h)) o << "'handler: ";
      o << "{\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      types["self"] = d.name;
      for (const auto& f : d.state) types[f.name] = f.type;
      for (const auto& p : h.params) locals.insert(p.name);
      for (const auto& p : h.params) types[p.name] = p.type;
      locals.insert("self_ref");
      if (h.reply_type) locals.insert(reply_name);
      gen_stmts(o, h.body, &d, &h, reply_name, locals, types, 6, true, false,
                std::nullopt, false, functional_handler_context(d, h));
      o << "                    }\n";
      o << "                }\n";
    }
    o << "            }\n";
    o << "            tracker.end();\n";
    o << "        }\n    });\n";
    o << "    actor\n}\n\n";
  }

  void gen_cluster(std::ostringstream& o, size_t cluster_index,
                   const vector<string>& member_names) {
    vector<const Domain*> members;
    for (const auto& name : member_names) members.push_back(domains_.at(name));
    const string prefix = "MossCluster" + std::to_string(cluster_index);

    backend_comment(o, 0, "CLUSTER ingress/message representation: outside callers use one shared-memory mailbox");
    o << "enum " << prefix << "SharedMsg {\n";
    for (const Domain* domain : members) {
      for (const auto& handler : domain->handlers) {
        source_comment(o, 4, handler.line, "domain " + domain->name + ": " + handler_signature(handler));
        o << "    " << domain->name << "_" << handler.name;
        if (!handler.params.empty() || handler.reply_type) {
          o << "(";
          size_t count = 0;
          for (const auto& param : handler.params) {
            if (count++) o << ", ";
            o << rust_type(param.type);
          }
          if (handler.reply_type) {
            if (count) o << ", ";
            o << "MossSender<" << rust_type(*handler.reply_type) << ">";
          }
          o << ")";
        }
        o << ",\n";
      }
    }
    o << "}\n\n";

    backend_comment(o, 0, "CLUSTER-LOCAL representation: same-thread calls use local capabilities and no lock-backed transport");
    for (const Domain* domain : members)
      o << "#[derive(Clone, Copy)]\nstruct " << domain->name << "LocalRef;\n\n";

    o << "enum " << prefix << "LocalMsg {\n";
    for (const Domain* domain : members) {
      for (const auto& handler : domain->handlers) {
        source_comment(o, 4, handler.line, "domain " + domain->name + ": " + handler_signature(handler));
        o << "    " << domain->name << "_" << handler.name;
        if (!handler.params.empty()) {
          o << "(";
          for (size_t index = 0; index < handler.params.size(); ++index) {
            if (index) o << ", ";
            o << local_rust_type(handler.params[index].type, cluster_index);
          }
          o << ")";
        }
        o << ",\n";
      }
    }
    o << "}\n\n";

    for (const Domain* domain : members) {
      backend_comment(o, 0, "CLUSTER ingress/message methods for domain " + domain->name + " (shared-memory mailbox)");
      o << "impl " << domain->name << "Ref {\n";
      for (const auto& handler : domain->handlers) {
        string reply_name = reply_binding(*domain, handler);
        source_comment(o, 4, handler.line, handler_signature(handler));
        backend_comment(o, 4, "CLUSTER ingress/message version: enqueue the Moss handler invocation");
        o << "    fn " << handler.name << "_shared(&self";
        for (const auto& param : handler.params)
          o << ", " << param.name << ": " << rust_type(param.type);
        if (handler.reply_type)
          o << ", " << reply_name << ": MossSender<" << rust_type(*handler.reply_type) << ">";
        o << ") {\n";
        o << "        self.tracker.begin();\n";
        o << "        if self.tx.send(" << prefix << "SharedMsg::" << domain->name
          << "_" << handler.name;
        if (!handler.params.empty() || handler.reply_type) {
          o << "(";
          size_t count = 0;
          for (const auto& param : handler.params) {
            if (count++) o << ", ";
            o << param.name;
          }
          if (handler.reply_type) {
            if (count) o << ", ";
            o << reply_name;
          }
          o << ")";
        }
        o << ").is_err() { self.tracker.end(); }\n";
        o << "    }\n";
      }
      o << "}\n\n";
    }

    backend_comment(o, 0, "CLUSTER-LOCAL handler implementations: direct same-thread calls");
    o << "struct " << prefix << "Runtime {\n";
    for (const Domain* domain : members) {
      string stem = snake_case(domain->name);
      source_comment(o, 4, domain->line, "domain " + domain->name);
      o << "    " << stem << "_state: RefCell<" << domain->name << "State>,\n";
      o << "    " << stem << "_ref: " << domain->name << "Ref,\n";
    }
    o << "    local_queue: RefCell<VecDeque<" << prefix << "LocalMsg>>,\n";
    o << "}\n\n";

    o << "impl " << prefix << "Runtime {\n";
    for (const Domain* domain : members) {
      for (const auto& handler : domain->handlers) {
        string result_name = reply_binding(*domain, handler);
        source_comment(o, 4, handler.line, "domain " + domain->name + ": " + handler_signature(handler));
        backend_comment(o, 4, "CLUSTER-LOCAL version: invoke this Moss handler directly on the cluster thread");
        o << "    fn " << domain->name << "_" << handler.name << "_local(&self";
        for (const auto& param : handler.params)
          o << ", " << param.name << ": " << local_rust_type(param.type, cluster_index);
        if (handler.reply_type)
          o << ") -> Option<" << local_rust_type(*handler.reply_type, cluster_index) << "> {\n";
        else
          o << ") {\n";
        o << "        let mut state = self." << snake_case(domain->name)
          << "_state.borrow_mut();\n";
        o << "        let self_ref = " << domain->name << "LocalRef;\n";
        if (handler.reply_type)
          o << "        let mut " << result_name << ": Option<"
            << local_rust_type(*handler.reply_type, cluster_index) << "> = None;\n";
        o << "        ";
        if (handler_needs_label(handler)) o << "'handler: ";
        o << "{\n";
        std::set<string> locals;
        std::unordered_map<string, string> types;
        types["self"] = domain->name;
        for (const auto& field : domain->state) types[field.name] = field.type;
        for (const auto& param : handler.params) {
          locals.insert(param.name);
          types[param.name] = param.type;
        }
        locals.insert("self_ref");
        if (handler.reply_type) locals.insert(result_name);
        gen_stmts(o, handler.body, domain, &handler, result_name, locals, types,
                  3, true, true, cluster_index, false,
                  functional_handler_context(*domain, handler));
        o << "        }\n";
        if (handler.reply_type) o << "        " << result_name << "\n";
        o << "    }\n";
      }
    }

    o << "    fn __moss_enqueue_local(&self, message: " << prefix << "LocalMsg) {\n";
    o << "        self.local_queue.borrow_mut().push_back(message);\n";
    o << "    }\n";
    o << "    fn __moss_dispatch_local(&self, message: " << prefix << "LocalMsg) {\n";
    o << "        match message {\n";
    for (const Domain* domain : members) {
      for (const auto& handler : domain->handlers) {
        source_comment(o, 12, handler.line, "domain " + domain->name + ": " + handler_signature(handler));
        backend_comment(o, 12, "CLUSTER-LOCAL queue dispatch for this Moss message");
        o << "            " << prefix << "LocalMsg::" << domain->name << "_"
          << handler.name;
        if (!handler.params.empty()) {
          o << "(";
          for (size_t index = 0; index < handler.params.size(); ++index) {
            if (index) o << ", ";
            o << handler.params[index].name;
          }
          o << ")";
        }
        o << " => { ";
        if (handler.reply_type) o << "let _ = ";
        o << "self." << domain->name << "_" << handler.name << "_local(";
        for (size_t index = 0; index < handler.params.size(); ++index) {
          if (index) o << ", ";
          o << handler.params[index].name;
        }
        o << "); },\n";
      }
    }
    o << "        }\n";
    o << "    }\n";
    backend_comment(o, 4, "CLUSTER-LOCAL queue dispatch; asynchronous Moss sends stay ordered without a lock");
    o << "    fn __moss_drain_local(&self) {\n";
    o << "        loop {\n";
    o << "            let message = { self.local_queue.borrow_mut().pop_front() };\n";
    o << "            let Some(message) = message else { break; };\n";
    o << "            self.__moss_dispatch_local(message);\n";
    o << "        }\n";
    o << "    }\n";
    for (const Domain* domain : members) {
      if (domain->handlers.empty()) continue;
      o << "    fn __moss_flush_" << domain->name << "_local(&self) {\n";
      o << "        loop {\n";
      o << "            let message = {\n";
      o << "                let mut queue = self.local_queue.borrow_mut();\n";
      o << "                let position = queue.iter().position(|message| matches!(message, ";
      for (size_t index = 0; index < domain->handlers.size(); ++index) {
        if (index) o << " | ";
        const auto& handler = domain->handlers[index];
        o << prefix << "LocalMsg::" << domain->name << "_" << handler.name;
        if (!handler.params.empty()) o << "(..)";
      }
      o << "));\n";
      o << "                position.and_then(|position| queue.remove(position))\n";
      o << "            };\n";
      o << "            let Some(message) = message else { break; };\n";
      o << "            self.__moss_dispatch_local(message);\n";
      o << "        }\n";
      o << "    }\n";
    }
    o << "}\n\n";

    o << "fn spawn_moss_cluster_" << cluster_index << "(tracker: Arc<MossTracker>) -> (";
    for (size_t index = 0; index < members.size(); ++index) {
      if (index) o << ", ";
      o << members[index]->name << "Ref";
    }
    o << ") {\n";
    o << "    __moss_require_send::<" << prefix << "SharedMsg>();\n";
    o << "    let (tx, rx): (MossSender<" << prefix << "SharedMsg>, MossReceiver<"
      << prefix << "SharedMsg>) = moss_channel();\n";
    for (const Domain* domain : members) {
      string stem = snake_case(domain->name);
      o << "    let " << stem << "_ref = " << domain->name
        << "Ref { tx: tx.clone(), tracker: tracker.clone() };\n";
      o << "    let " << stem << "_runtime_ref = " << stem << "_ref.clone();\n";
    }
    o << "    thread::spawn(move || {\n";
    o << "        let runtime = " << prefix << "Runtime {\n";
    for (const Domain* domain : members) {
      string stem = snake_case(domain->name);
      o << "            " << stem << "_state: RefCell::new(" << domain->name << "State {\n";
      for (const auto& field : domain->state) {
        string init = field.init.empty() ? default_value(field.type) : expr(field.init, nullptr, {});
        if (field.type == "string" && !init.empty() && init.front() == '"' && init.back() == '"')
          init += ".to_string()";
        source_comment(o, 16, field.line, state_field_signature(field));
        o << "                " << field.name << ": " << init << ",\n";
      }
      o << "            }),\n";
      o << "            " << stem << "_ref: " << stem << "_runtime_ref,\n";
    }
    o << "            local_queue: RefCell::new(VecDeque::new()),\n";
    o << "        };\n";
    o << "        while let Ok(message) = rx.recv() {\n";
    o << "            match message {\n";
    for (const Domain* domain : members) {
      for (const auto& handler : domain->handlers) {
        string reply_name = reply_binding(*domain, handler);
        source_comment(o, 16, handler.line, "domain " + domain->name + ": " + handler_signature(handler));
        backend_comment(o, 16, "CLUSTER ingress/message dispatch: hand off to the cluster-local handler");
        o << "                " << prefix << "SharedMsg::" << domain->name << "_"
          << handler.name;
        if (!handler.params.empty() || handler.reply_type) {
          o << "(";
          size_t count = 0;
          for (const auto& param : handler.params) {
            if (count++) o << ", ";
            o << param.name;
          }
          if (handler.reply_type) {
            if (count) o << ", ";
            o << reply_name;
          }
          o << ")";
        }
        o << " => {\n";
        if (handler.reply_type) {
          o << "                    if let Some(value) = runtime." << domain->name << "_"
            << handler.name << "_local(";
        } else {
          o << "                    runtime." << domain->name << "_" << handler.name
            << "_local(";
        }
        for (size_t index = 0; index < handler.params.size(); ++index) {
          if (index) o << ", ";
          const auto& param = handler.params[index];
          if (domains_.count(trim(param.type)) &&
              plan_.cluster_for(trim(param.type)) == std::optional<size_t>(cluster_index))
            o << trim(param.type) << "LocalRef";
          else if (domains_.count(trim(param.type)))
            o << "Rc::new(" << param.name << ")";
          else
            o << param.name;
        }
        if (handler.reply_type) {
          o << ") { let _ = " << reply_name << ".send(";
          string reply_type = trim(*handler.reply_type);
          if (domains_.count(reply_type) &&
              plan_.cluster_for(reply_type) == std::optional<size_t>(cluster_index))
            o << "runtime." << snake_case(reply_type) << "_ref.clone()";
          else if (domains_.count(reply_type))
            o << "value.as_ref().clone()";
          else
            o << "value";
          o << "); }\n";
        } else {
          o << ");\n";
        }
        o << "                }\n";
      }
    }
    o << "            }\n";
    o << "            runtime.__moss_drain_local();\n";
    o << "            tracker.end();\n";
    o << "        }\n";
    o << "    });\n";
    o << "    (";
    for (size_t index = 0; index < members.size(); ++index) {
      if (index) o << ", ";
      o << snake_case(members[index]->name) << "_ref";
    }
    o << ")\n";
    o << "}\n\n";
  }

  void gen_main(std::ostringstream& o, const MainProc& m) {
    source_comment(o, 0, m.line, m.header.empty() ? "proc main()" : m.header);
    backend_comment(o, 0, "main entry point; domain calls below retain their statically selected lowering");
    o << "fn main() {\n";
    o << "    let __tracker = Arc::new(MossTracker::new());\n";
    for (size_t index = 0; index < plan_.domain_clusters.size(); ++index) {
      o << "    let (";
      for (size_t member = 0; member < plan_.domain_clusters[index].size(); ++member) {
        if (member) o << ", ";
        o << cluster_spawn_binding(index, plan_.domain_clusters[index][member]);
      }
      o << ") = spawn_moss_cluster_" << index << "(__tracker.clone());\n";
    }
    std::set<string> locals;
    std::unordered_map<string,string> types;
    gen_stmts(o, m.body, nullptr, nullptr, "", locals, types, 1, false, false,
              std::nullopt, false, "main");
    o << "    __tracker.wait_zero();\n";
    o << "}\n";
  }

  static size_t statement_functional_pipeline_id(
      const Stmt& statement, const string& context, size_t slot) {
    auto plans = statement.functional_pipeline_ids.find(context);
    if (plans == statement.functional_pipeline_ids.end() ||
        slot >= plans->second.size())
      return 0;
    return plans->second[slot];
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool direct_reply = false,
                 std::optional<size_t> cluster_context = std::nullopt,
                 bool in_function = false,
                 const string& functional_context = "") {
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_sender, locals, types, base,
              in_handler, direct_reply, cluster_context, in_function, {},
              functional_context);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool direct_reply,
                 std::optional<size_t> cluster_context, bool in_function,
                 std::set<string> join_assignments = {},
                 const string& functional_context = "") {
    auto indent = [&](int lev){ return string((base + lev) * 4, ' '); };
    while (i < ss.size()) {
      const auto& s = ss[i];
      if (s.indent < level) return;
      if (s.indent > level) throw std::runtime_error("internal error: unexpected statement indentation");
      if (s.kind == Stmt::Kind::Else) return; // consumed by the preceding if

      source_comment(o, (base + level) * 4, s.line, s.text);

      size_t direct_functional_id = statement_functional_pipeline_id(
          s, functional_context, 0);
      const FunctionalPipeline* direct_functional =
          planned_functional_pipeline(direct_functional_id);
      if (direct_functional &&
          direct_functional->virtualized_into_pipeline_id &&
          (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let)) {
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' VIRTUALIZED across one immutable binding; no collection allocated");
        locals.insert(s.a);
        types[s.a] = direct_functional->output_type;
        ++i;
        continue;
      }
      if (direct_functional && direct_functional->traversal_group_id) {
        const FunctionalTraversalGroup* group = functional_traversal_group(
            direct_functional->traversal_group_id);
        if (!group || group->consumers.empty() ||
            group->consumers.front().pipeline_id != direct_functional_id)
          throw std::runtime_error(
              "internal error: shared functional traversal did not start at "
              "its first consumer");
        gen_shared_functional_traversal(
            o, *group, ss, i, d, locals, types, (base + level) * 4,
            functional_context);
        i += group->consumers.size();
        continue;
      }
      if (direct_functional && !direct_functional->binding_name.empty() &&
          !direct_functional->virtualized_into_pipeline_id &&
          !direct_functional->binding_materialization_reason.empty())
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' MATERIALIZED; reason: " +
                direct_functional->binding_materialization_reason);

      switch (s.kind) {
        case Stmt::Kind::If: {
          vector<string> joined_bindings;
          for (const auto& entry : s.joined_types)
            if (!types.count(entry.first) && !starts_with(entry.second, "_"))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            const string& type = s.joined_types.at(binding);
            backend_comment(o, (base + level) * 4,
                            "CONTROL-FLOW JOIN: binding has one definite static type on every path");
            o << indent(level) << "let mut " << binding << ": "
              << (cluster_context ? local_rust_type(type, *cluster_context)
                                  : rust_type(type))
              << ";\n";
            locals.insert(binding);
            types[binding] = type;
          }
          o << indent(level) << "if "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          auto child_join_assignments = join_assignments;
          child_join_assignments.insert(joined_bindings.begin(), joined_bindings.end());
          gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                    child_locals, child_types, base, in_handler, direct_reply,
                    cluster_context, in_function, child_join_assignments,
                    functional_context);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            source_comment(o, (base + level + 1) * 4, ss[i].line, ss[i].text);
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                      else_locals, else_types, base, in_handler, direct_reply,
                      cluster_context, in_function, child_join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
          } else {
            o << "\n";
          }
          for (const auto& entry : s.joined_types) {
            if (starts_with(entry.second, "_")) continue;
            types[entry.first] = entry.second;
          }
          break;
        }
        case Stmt::Kind::While: {
          o << indent(level) << "while "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                    child_locals, child_types, base, in_handler, direct_reply,
                    cluster_context, in_function, join_assignments,
                    functional_context);
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::Echo: {
          o << indent(level);
          if (s.args.empty()) o << "println!();\n";
          else {
            o << "println!(\"";
            for (size_t k = 0; k < s.args.size(); ++k) { if (k) o << " "; o << "{}"; }
            o << "\"";
            for (size_t argument = 0; argument < s.args.size(); ++argument)
              o << ", "
                << expr(s.args[argument], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, functional_context, argument));
            o << ");\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Assign: {
          if (auto sd = CheckerSpawn(s.b)) {
            bool existing_binding = locals.count(s.a);
            if (auto cluster = plan_.cluster_for(*sd)) {
              backend_comment(o, (base + level) * 4,
                              "CLUSTER-LOCAL domain handle; spawn is bound to the cluster worker");
              o << indent(level) << (existing_binding ? "" : "let ") << s.a << " = "
                << cluster_spawn_binding(*cluster, *sd) << ".clone();\n";
            } else {
              DomainLowering lowering = plan_.lowering_for(*sd);
              if (lowering == DomainLowering::DirectAtomic)
                backend_comment(o, (base + level) * 4,
                                "ATOMIC DOMAIN handle; state actions use SeqCst atomics");
              else if (lowering == DomainLowering::DirectRwLock)
                backend_comment(o, (base + level) * 4,
                                "SHARED-MEMORY DIRECT domain handle; state uses Arc<RwLock<_>>");
              else if (lowering == DomainLowering::DirectMutex)
                backend_comment(o, (base + level) * 4,
                                "SHARED-MEMORY DIRECT domain handle; state uses Arc<Mutex<_>>");
              else
                backend_comment(o, (base + level) * 4,
                                "MESSAGE/MAILBOX domain handle; sends use a lock-backed shared-memory queue");
              o << indent(level) << (existing_binding ? "" : "let ") << s.a
                << " = spawn_" << snake_case(*sd)
                << "(__tracker.clone());\n";
            }
            locals.insert(s.a);
            types[s.a] = *sd;
            ++i;
            break;
          }
          string lhs;
          string lhs_base, lhs_index;
          if (parse_index(s.a, lhs_base, lhs_index)) {
            bool string_index = lhs_index.size() >= 2 && lhs_index.front() == '"' &&
                lhs_index.back() == '"';
            string ir = string_index ? lhs_index : expr(lhs_index, d, locals, &types);
            auto base_type = generated_expr_type(lhs_base, &types);
            bool map_index = base_type &&
                (canonical_type_name(*base_type) == "map" ||
                 starts_with(canonical_type_name(*base_type), "map["));
            if (!string_index && !map_index) ir = "(" + ir + ") as usize";
            lhs = expr(lhs_base, d, locals, &types) + "[" + ir + "]";
          }
          else lhs = expr(s.a, d, locals, &types);
          bool state_field = d && std::any_of(d->state.begin(), d->state.end(),
                                              [&](const Field& field) { return field.name == s.a; });
          if (plain_identifier(s.a) && !locals.count(s.a) && !state_field) {
            backend_comment(o, (base + level) * 4,
                            "LOCAL assignment: introduce an inferred Moss binding");
            string annotation;
            if (starts_with(s.semantic_type, "map[") && ends_with(s.semantic_type, "]")) annotation = rust_type(s.semantic_type);
            o << indent(level) << "let mut " << s.a;
            if (!annotation.empty()) o << ": " << annotation;
            o << " = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ";\n";
            locals.insert(s.a);
            auto generated_type = generated_expr_type(s.b, &types);
            types[s.a] = generated_type ? *generated_type
                : !s.semantic_type.empty() ? s.semantic_type
                : s.b == "Map()" ? "map"
                : s.b == "Queue()" ? "queue"
                : (s.b.size() && s.b.front() == '[' ? "vector" : "_value");
          } else {
            string mb, mi;
            if (parse_index(s.a, mb, mi) && types.count(mb) &&
                (types.at(mb) == "map" || starts_with(types.at(mb), "map[")))
              o << indent(level) << expr(mb, d, locals, &types) << ".insert(" << ((mi.size() >= 2 && mi.front() == '"' && mi.back() == '"') ? mi + ".to_string()" : expr(mi, d, locals, &types)) << ", " << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
            else o << indent(level) << lhs << " = " << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ";\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Call: {
          bool known_function = functions_.count(s.a);
          bool implicit_method = in_function && d && objects_.count(d->name) &&
              s.b.empty() && !known_function;
          o << indent(level) << (implicit_method ? "self." : "")
            << (s.b.empty() && known_function
                    ? emitted_function_name(s.a, s.args, &types)
                    : expr(s.a, d, locals, &types));
          if (!s.b.empty()) {
            string method = s.b;
            auto it = types.find(s.a);
            if (it != types.end() && (it->second == "queue" || starts_with(it->second, "queue["))) method = method == "push" ? "push_back" : method == "pop" ? "pop_front" : method;
            o << "." << method;
          }
          o << "(";
          size_t emitted_arguments = 0;
          for (size_t k = 0; k < s.args.size(); ++k) {
            bool compile_time_callable = known_function &&
                starts_with(generated_expr_type(s.args[k], &types).value_or(""),
                            "callable:");
            if (compile_time_callable) continue;
            if (emitted_arguments++) o << ", ";
            if (known_function) {
              o << function_call_argument(*functions_.at(s.a), k, s.args[k], d,
                                          locals, &types,
                                          statement_functional_pipeline_id(
                                              s, functional_context, k));
            } else if (!s.b.empty() || implicit_method) {
              auto receiver_type = implicit_method
                  ? std::optional<string>(d->name)
                  : generated_expr_type(s.a, &types);
              const Method* method = nullptr;
              if (receiver_type)
                method = resolve_object_method(objects_, canonical_type_name(*receiver_type),
                                               implicit_method ? s.a : s.b, [&]() {
                                                 vector<string> result;
                                                 for (const auto& argument : s.args)
                                                   result.push_back(generated_expr_type(argument, &types).value_or(""));
                                                 return result;
                                               }(), true, nullptr);
              if (method)
                o << method_call_argument(
                    *method, k, s.args[k], d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, k));
              else
                o << expr(s.args[k], d, locals, &types,
                          statement_functional_pipeline_id(
                              s, functional_context, k));
            } else {
              o << expr(s.args[k], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, functional_context, k));
            }
          }
          o << ");\n";
          ++i;
          break;
        }
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          bool joined_assignment = join_assignments.erase(s.a) != 0;
          auto sd = CheckerSpawn(s.b);
          if (sd) {
            if (auto cluster = plan_.cluster_for(*sd)) {
              backend_comment(o, (base + level) * 4,
                              "CLUSTER-LOCAL domain handle; spawn is bound to the cluster worker");
            } else if (plan_.lowering_for(*sd) == DomainLowering::DirectAtomic) {
              backend_comment(o, (base + level) * 4,
                              "ATOMIC DOMAIN handle; state actions use SeqCst atomics");
            } else if (plan_.lowering_for(*sd) == DomainLowering::DirectRwLock) {
              backend_comment(o, (base + level) * 4,
                              "SHARED-MEMORY DIRECT domain handle; state uses Arc<RwLock<_>>");
            } else if (plan_.lowering_for(*sd) == DomainLowering::DirectMutex) {
              backend_comment(o, (base + level) * 4,
                              "SHARED-MEMORY DIRECT domain handle; state uses Arc<Mutex<_>>");
            } else {
              backend_comment(o, (base + level) * 4,
                              "MESSAGE/MAILBOX domain handle; sends use a lock-backed shared-memory queue");
            }
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << s.a << " = ";
            if (auto cluster = plan_.cluster_for(*sd))
              o << cluster_spawn_binding(*cluster, *sd) << ".clone();\n";
            else
              o << "spawn_" << snake_case(*sd) << "(__tracker.clone());\n";
            types[s.a] = *sd;
          } else {
            auto source = types.find(trim(s.b));
            bool domain_capability = source != types.end() && domains_.count(source->second);
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << s.a << " = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0));
            if (domain_capability) o << ".clone()";
            o << ";\n";
            auto generated_type = generated_expr_type(s.b, &types);
            types[s.a] = domain_capability ? source->second
                : generated_type ? *generated_type
                : !s.semantic_type.empty() ? s.semantic_type : "_value";
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Message: {
          const Domain* target = nullptr;
          if (s.a == "self" && d) target = d;
          else {
            auto type = types.find(s.a);
            if (type != types.end() && domains_.count(type->second)) target = domains_.at(type->second);
          }
          string recv = (s.a == "self") ? "self_ref" : expr(s.a, d, locals, &types);
          const Handler* h = target ? find_handler(*target, s.b) : nullptr;
          if (const BatchedSendRegion* region = plan_.batch_at(s.line)) {
            if (!target || plan_.lowering_for(*target) != DomainLowering::Mailbox ||
                region->target_domain != target->name || region->receiver != s.a ||
                i + region->count > ss.size())
              throw std::runtime_error("internal error: invalid batched-send optimization region");
            size_t batch_id = reply_temp_++;
            std::set<string> batch_names = locals;
            string batch_variable = fresh_generated_name(
                "__moss_batch_" + std::to_string(batch_id), batch_names);
            backend_comment(o, (base + level) * 4,
                            "BATCHED MAILBOX SEND (" + std::to_string(region->count) +
                            " messages): payloads evaluate in source order before one enqueue");
            vector<string> ignored_reply_senders(region->count);
            vector<string> ignored_reply_receivers(region->count);
            for (size_t item = 0; item < region->count; ++item) {
              const Handler* batch_handler = find_handler(*target, ss[i + item].b);
              if (!batch_handler || !batch_handler->reply_type) continue;
              string suffix = std::to_string(batch_id) + "_" + std::to_string(item);
              ignored_reply_senders[item] = fresh_generated_name(
                  "__moss_batch_reply_tx_" + suffix, batch_names);
              ignored_reply_receivers[item] = fresh_generated_name(
                  "__moss_batch_reply_rx_" + suffix, batch_names);
              o << indent(level) << "let (" << ignored_reply_senders[item] << ", "
                << ignored_reply_receivers[item] << ") = moss_channel::<"
                << rust_type(*batch_handler->reply_type) << ">();\n";
            }
            o << indent(level) << "let " << batch_variable << " = vec![\n";
            for (size_t item = 0; item < region->count; ++item) {
              const Stmt& message = ss[i + item];
              const Handler* batch_handler = find_handler(*target, message.b);
              if (!batch_handler ||
                  message.kind != Stmt::Kind::Message || message.a != s.a)
                throw std::runtime_error("internal error: stale batched-send optimization region");
              if (item)
                source_comment(o, (base + level + 1) * 4, message.line, message.text);
              o << indent(level + 1) << target->name << "Msg::" << message.b;
              if (!message.args.empty() || batch_handler->reply_type) {
                o << "(";
                for (size_t argument = 0; argument < message.args.size(); ++argument) {
                  if (argument) o << ", ";
                  string type = batch_handler->params[argument].type;
                  if (cluster_context)
                    o << cluster_call_arg(message.args[argument], type, d, locals,
                                          *cluster_context, true, &types,
                                          statement_functional_pipeline_id(
                                              message, functional_context,
                                              argument));
                  else
                    o << message_arg(
                        message.args[argument], type, d, locals, &types,
                        statement_functional_pipeline_id(
                            message, functional_context, argument));
                }
                if (batch_handler->reply_type) {
                  if (!message.args.empty()) o << ", ";
                  o << ignored_reply_senders[item];
                }
                o << ")";
              }
              o << ",\n";
            }
            o << indent(level) << "];\n";
            o << indent(level) << recv << ".__moss_send_batch("
              << batch_variable << ");\n";
            for (const auto& receiver : ignored_reply_receivers)
              if (!receiver.empty()) o << indent(level) << "drop(" << receiver << ");\n";
            i += region->count;
            break;
          }
          if (local_cluster_call(d, target, cluster_context)) {
            backend_comment(o, (base + level) * 4,
                            "CLUSTER-LOCAL version: enqueue on the plain same-thread local queue");
            o << indent(level) << "self.__moss_enqueue_local(MossCluster" << *cluster_context
              << "LocalMsg::" << target->name << "_" << s.b;
            if (!s.args.empty()) {
              o << "(";
              for (size_t k = 0; k < s.args.size(); ++k) {
                if (k) o << ", ";
                string typ = h && k < h->params.size() ? h->params[k].type : "_";
                o << cluster_call_arg(s.args[k], typ, d, locals, *cluster_context,
                                      false, &types,
                                      statement_functional_pipeline_id(
                                          s, functional_context, k));
              }
              o << ")";
            }
            o << ");\n";
            ++i;
            break;
          }
          if (target && direct_lock(*target))
            throw std::runtime_error("internal error: asynchronous send reached direct shared-memory generation");
          if (target && direct_atomic(*target)) {
            backend_comment(o, (base + level) * 4,
                            "ATOMIC DOMAIN one-way execution; SeqCst preserves the domain total order");
            o << indent(level);
            if (h && h->reply_type) o << "let _ = ";
            o << recv << "." << s.b << "_shared(";
            for (size_t k = 0; k < s.args.size(); ++k) {
              if (k) o << ", ";
              string typ = h && k < h->params.size() ? h->params[k].type : "_";
              if (cluster_context)
                o << cluster_call_arg(s.args[k], typ, d, locals,
                                      *cluster_context, true, &types,
                                      statement_functional_pipeline_id(
                                          s, functional_context, k));
              else
                o << message_arg(
                    s.args[k], typ, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, k));
            }
            o << ");\n";
            ++i;
            break;
          }
          backend_comment(o, (base + level) * 4,
                          "MESSAGE/MAILBOX version: enqueue the Moss send in a lock-backed shared-memory queue");
          string reply_tx, reply_rx;
          if (h && h->reply_type) {
            size_t id = reply_temp_++;
            reply_tx = "__moss_reply_tx_" + std::to_string(id);
            reply_rx = "__moss_reply_rx_" + std::to_string(id);
            o << indent(level) << "let (" << reply_tx << ", " << reply_rx
              << ") = moss_channel::<" << rust_type(*h->reply_type) << ">();\n";
          }
          o << indent(level) << recv << "." << s.b << "_shared(";
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            string typ = h && k < h->params.size() ? h->params[k].type : "_";
            if (cluster_context)
              o << cluster_call_arg(s.args[k], typ, d, locals, *cluster_context,
                                    true, &types,
                                    statement_functional_pipeline_id(
                                        s, functional_context, k));
            else
              o << message_arg(
                  s.args[k], typ, d, locals, &types,
                  statement_functional_pipeline_id(
                      s, functional_context, k));
          }
          if (!reply_tx.empty()) {
            if (!s.args.empty()) o << ", ";
            o << reply_tx;
          }
          o << ");\n";
          if (!reply_rx.empty()) o << indent(level) << "drop(" << reply_rx << ");\n";
          ++i;
          break;
        }
        case Stmt::Kind::AwaitMessage: {
          auto type = types.find(s.b);
          const Domain* target = type != types.end() && domains_.count(type->second)
              ? domains_.at(type->second) : nullptr;
          const Handler* h = target ? find_handler(*target, s.c) : nullptr;
          if (!target || !h || !h->reply_type)
            throw std::runtime_error("internal error: unchecked await reached code generation");
          string recv = expr(s.b, d, locals, &types);
          if (const CoalescedLockRegion* region = plan_.coalesced_at(s.line)) {
            if (!direct_lock(*target) || region->target_domain != target->name ||
                region->receiver != s.b || i + region->count > ss.size())
              throw std::runtime_error("internal error: invalid coalesced-lock optimization region");
            size_t lock_id = reply_temp_++;
            std::set<string> lock_names = locals;
            string guard = fresh_generated_name(
                "__moss_guard_" + std::to_string(lock_id), lock_names);
            bool rwlock = plan_.lowering_for(*target) == DomainLowering::DirectRwLock;
            backend_comment(o, (base + level) * 4,
                            "COALESCED LOCK REGION (" + std::to_string(region->count) +
                            " operations): exclusive whole-program caller proof");
            o << indent(level) << "let " << (region->shared_read ? "" : "mut ")
              << guard << " = " << recv << ".state."
              << (rwlock ? (region->shared_read ? "read" : "write") : "lock")
              << "().unwrap();\n";
            for (size_t item = 0; item < region->count; ++item) {
              const Stmt& awaited = ss[i + item];
              const Handler* locked_handler = find_handler(*target, awaited.c);
              if (!locked_handler || !locked_handler->reply_type ||
                  awaited.kind != Stmt::Kind::AwaitMessage || awaited.b != s.b)
                throw std::runtime_error("internal error: stale coalesced-lock optimization region");
              if (item)
                source_comment(o, (base + level) * 4, awaited.line, awaited.text);
              bool bind = !locals.count(awaited.a);
              o << indent(level);
              if (bind) o << "let " << (awaited.is_mutable ? "mut " : "") << awaited.a;
              else o << awaited.a;
              o << " = ";
              if (!await_error_handling_) o << "unsafe { ";
              o << recv << "." << awaited.c << "_locked(";
              bool read_handler = rwlock &&
                  handler_effects(*target, *locked_handler).state_effect == Effect::Read;
              o << (read_handler ? "&" : "&mut ") << guard;
              for (size_t argument = 0; argument < awaited.args.size(); ++argument) {
                o << ", " << message_arg(awaited.args[argument],
                                          locked_handler->params[argument].type,
                                          d, locals, &types,
                                          statement_functional_pipeline_id(
                                              awaited, functional_context,
                                              argument));
              }
              if (await_error_handling_) {
                o << ").unwrap_or_else(|| {\n";
                o << indent(level + 1) << "panic!(\"Moss await failed: "
                  << target->name << "." << locked_handler->name
                  << " completed without a reply\")\n";
                o << indent(level) << "});\n";
              } else {
                o << ").unwrap_unchecked() };\n";
              }
              locals.insert(awaited.a);
              types[awaited.a] = *locked_handler->reply_type;
            }
            o << indent(level) << "drop(" << guard << ");\n";
            i += region->count;
            break;
          }
          if (local_cluster_call(d, target, cluster_context)) {
            backend_comment(o, (base + level) * 4,
                            "CLUSTER-LOCAL version: flush older local messages, then call the handler directly");
            o << indent(level) << "self.__moss_flush_" << target->name << "_local();\n";
            bool bind = !locals.count(s.a);
            if (bind)
              o << indent(level) << "let " << (s.is_mutable ? "mut " : "") << s.a << " = ";
            else
              o << indent(level) << s.a << " = ";
            if (!await_error_handling_) o << "unsafe { ";
            o << "self." << target->name << "_" << s.c << "_local(";
            for (size_t k = 0; k < s.args.size(); ++k) {
              if (k) o << ", ";
              o << cluster_call_arg(s.args[k], h->params[k].type, d, locals,
                                    *cluster_context, false, &types,
                                    statement_functional_pipeline_id(
                                        s, functional_context, k));
            }
            if (await_error_handling_) {
              o << ").unwrap_or_else(|| {\n";
              o << indent(level + 1) << "panic!(\"Moss await failed: " << target->name << "."
                << h->name << " completed without a reply\")\n";
              o << indent(level) << "});\n";
            } else {
              o << ").unwrap_unchecked() };\n";
            }
            locals.insert(s.a);
            types[s.a] = *h->reply_type;
            ++i;
            break;
          }
          if (direct_shared_memory(*target)) {
            backend_comment(o, (base + level) * 4,
                            direct_atomic(*target)
                                ? "ATOMIC DOMAIN await: execute one SeqCst state action directly"
                                : "SHARED-MEMORY DIRECT version: lock the target state and invoke the handler without a request message");
            bool localize_domain_reply = cluster_context &&
                domains_.count(trim(*h->reply_type));
            string result = s.a;
            if (localize_domain_reply)
              result = "__moss_reply_value_" + std::to_string(reply_temp_++);
            bool bind = !locals.count(s.a);
            if (bind)
              o << indent(level) << "let "
                << (!localize_domain_reply && s.is_mutable ? "mut " : "") << result
                << " = ";
            else
              o << indent(level) << result << " = ";
            if (!await_error_handling_) o << "unsafe { ";
            o << recv << "." << s.c << "_shared(";
            for (size_t k = 0; k < s.args.size(); ++k) {
              if (k) o << ", ";
              if (cluster_context)
                o << cluster_call_arg(s.args[k], h->params[k].type, d, locals,
                                      *cluster_context, true, &types,
                                      statement_functional_pipeline_id(
                                          s, functional_context, k));
              else
                o << message_arg(
                    s.args[k], h->params[k].type, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, k));
            }
            if (await_error_handling_) {
              o << ").unwrap_or_else(|| {\n";
              o << indent(level + 1) << "panic!(\"Moss await failed: " << target->name << "."
                << h->name << " completed without a reply\")\n";
              o << indent(level) << "});\n";
            } else {
              o << ").unwrap_unchecked() };\n";
            }
            if (localize_domain_reply) {
              string reply_type = trim(*h->reply_type);
              if (!locals.count(s.a))
                o << indent(level) << "let " << (s.is_mutable ? "mut " : "") << s.a << " = ";
              else
                o << indent(level) << s.a << " = ";
              if (plan_.cluster_for(reply_type) == cluster_context)
                o << "{ drop(" << result << "); " << reply_type << "LocalRef };\n";
              else
                o << "Rc::new(" << result << ");\n";
            }
            locals.insert(s.a);
            types[s.a] = *h->reply_type;
            ++i;
            break;
          }
          size_t id = reply_temp_++;
          string reply_tx = "__moss_reply_tx_" + std::to_string(id);
          string reply_rx = "__moss_reply_rx_" + std::to_string(id);
          backend_comment(o, (base + level) * 4,
                          "MESSAGE/MAILBOX version: send a request and await its lock-backed one-shot reply");
          bool localize_domain_reply = cluster_context &&
              domains_.count(trim(*h->reply_type));
          string result = localize_domain_reply
              ? "__moss_reply_value_" + std::to_string(id) : s.a;
          o << indent(level) << "let (" << reply_tx << ", " << reply_rx
            << ") = moss_channel::<" << rust_type(*h->reply_type) << ">();\n";
          o << indent(level) << recv << "." << s.c << "_shared(";
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            if (cluster_context)
              o << cluster_call_arg(s.args[k], h->params[k].type, d, locals,
                                    *cluster_context, true, &types,
                                    statement_functional_pipeline_id(
                                        s, functional_context, k));
            else
              o << message_arg(
                  s.args[k], h->params[k].type, d, locals, &types,
                  statement_functional_pipeline_id(
                      s, functional_context, k));
          }
          if (!s.args.empty()) o << ", ";
          o << reply_tx << ");\n";
          bool bind = !locals.count(s.a);
          if (bind)
            o << indent(level) << "let "
              << (!localize_domain_reply && s.is_mutable ? "mut " : "") << result
              << " = ";
          else
            o << indent(level) << result << " = ";
          if (!await_error_handling_) o << "unsafe { ";
          o << reply_rx << ".recv()";
          if (await_error_handling_) {
            o << ".unwrap_or_else(|_| {\n";
            o << indent(level + 1) << "panic!(\"Moss await failed: " << target->name << "."
              << h->name << " completed without a reply\")\n";
            o << indent(level) << "});\n";
          } else {
            o << ".unwrap_unchecked() };\n";
          }
          if (localize_domain_reply) {
            string reply_type = trim(*h->reply_type);
            o << indent(level) << "let " << (s.is_mutable ? "mut " : "") << s.a << " = ";
            if (plan_.cluster_for(reply_type) == cluster_context)
              o << "{ drop(" << result << "); " << reply_type << "LocalRef };\n";
            else
              o << "Rc::new(" << result << ");\n";
          }
          locals.insert(s.a);
          types[s.a] = *h->reply_type;
          ++i;
          break;
        }
        case Stmt::Kind::Reply:
          if (!current_handler || !current_handler->reply_type || reply_sender.empty())
            throw std::runtime_error("internal error: unchecked reply reached code generation");
          if (direct_reply) {
            backend_comment(o, (base + level) * 4,
                            cluster_context
                                ? "CLUSTER-LOCAL version: place the reply in the direct return slot"
                                : "SHARED-MEMORY DIRECT version: place the reply in the direct return slot");
            o << indent(level) << reply_sender << " = Some("
              << (cluster_context
                    ? cluster_call_arg(s.a, *current_handler->reply_type, d, locals,
                                       *cluster_context, false, &types,
                                       statement_functional_pipeline_id(
                                           s, functional_context, 0))
                    : message_arg(
                          s.a, *current_handler->reply_type, d, locals, &types,
                          statement_functional_pipeline_id(
                              s, functional_context, 0)))
              << ");\n";
          } else {
            backend_comment(o, (base + level) * 4,
                            "MESSAGE/MAILBOX version: send the reply through a lock-backed one-shot mailbox");
            o << indent(level) << "let _ = " << reply_sender << ".send("
              << message_arg(
                     s.a, *current_handler->reply_type, d, locals, &types,
                     statement_functional_pipeline_id(
                         s, functional_context, 0))
              << ");\n";
          }
          o << indent(level) << "break 'handler;\n";
          ++i;
          break;
        case Stmt::Kind::Return:
          if (in_function && !s.a.empty())
            o << indent(level) << "return "
              << expr(s.a, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ";\n";
          else
            o << indent(level) << (in_handler ? "break 'handler;" : "return;") << "\n";
          ++i;
          break;
        case Stmt::Kind::Raw:
          o << indent(level)
            << expr(s.text, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0))
            << ";\n";
          ++i;
          break;
        case Stmt::Kind::Else:
          return;
      }
    }
  }

  static std::optional<string> CheckerSpawn(const string& expr) {
    string e = trim(expr);
    if (!starts_with(e, "spawn ") || !ends_with(e, "()")) return std::nullopt;
    return trim(e.substr(6, e.size()-8));
  }

  static string cluster_spawn_binding(size_t cluster, const string& domain) {
    return "__moss_cluster_" + std::to_string(cluster) + "_" + snake_case(domain) + "_ref";
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

};

} // namespace moss

static void usage() {
  std::cerr << "Moss v0.2 - static compiler to Rust with domains and functional dataflow\n\n"
            << "Usage:\n"
            << "  moss <input.moss> [-Oshared-memory] [--dump-functional-ir] [--explain-fusion] [--no-await-error-handling] [--cluster=A,B] [-o output.rs]\n"
            << "  moss --check <input.moss>\n\n"
            << "Backend optimization:\n"
            << "  -O, -Oshared-memory    fuse safe functional pipelines and plan optimized domain lowering\n"
            << "  -O0                    retain eager pipelines and lock-backed mailbox dispatch\n\n"
            << "  --dump-functional-ir   print typed functional/dataflow nodes and optimization decisions\n"
            << "  --explain-fusion       print deterministic functional optimization decisions\n\n"
            << "  --no-await-error-handling  omit per-await reply checks (supervision owns failures)\n\n"
            << "  --cluster=A,B          place the listed domain types on one generated worker thread\n\n"
            << "Request/reply:\n"
            << "  message domain.Message(args...)\n"
            << "  fn Message(args...) [-> Type]\n"
            << "  reply value\n"
            << "  value = await domain.Message(args...)\n"
            << "  fn square(x) = x * x\n"
            << "  type Quote:\n";
}

int main(int argc, char** argv) {
  try {
    if (argc < 2) { usage(); return 2; }
    bool check_only = false;
    bool optimize_shared_memory = false;
    bool await_error_handling = true;
    bool dump_functional = false;
    bool explain_fusion = false;
    string input, output;
    vector<vector<string>> requested_clusters;
    for (int i = 1; i < argc; ++i) {
      string a = argv[i];
      if (a == "--check") check_only = true;
      else if (a == "--dump-functional-ir") dump_functional = true;
      else if (a == "--explain-fusion") explain_fusion = true;
      else if (a == "-O" || a == "-Oshared-memory" || a == "--optimize-shared-memory")
        optimize_shared_memory = true;
      else if (a == "-O0") optimize_shared_memory = false;
      else if (a == "--no-await-error-handling") await_error_handling = false;
      else if (a == "--cluster" || moss::starts_with(a, "--cluster=")) {
        string value;
        if (a == "--cluster") {
          if (++i >= argc) { usage(); return 2; }
          value = argv[i];
        } else {
          value = a.substr(string("--cluster=").size());
        }
        auto members = moss::split_top_level(value, ',');
        if (members.empty() || std::any_of(members.begin(), members.end(), [](const string& name) {
              return name.empty();
            })) {
          std::cerr << "moss: invalid empty domain name in --cluster\n";
          return 2;
        }
        requested_clusters.push_back(std::move(members));
      }
      else if (a == "-o") {
        if (++i >= argc) { usage(); return 2; }
        output = argv[i];
      } else if (a == "-h" || a == "--help") { usage(); return 0; }
      else if (input.empty()) input = a;
      else { std::cerr << "unexpected argument: " << a << "\n"; return 2; }
    }
    if (input.empty()) { usage(); return 2; }

    std::ifstream f(input);
    if (!f) { std::cerr << "moss: cannot open " << input << "\n"; return 1; }
    auto lines = moss::lex_lines(f);
    moss::Parser parser(std::move(lines));
    auto program = parser.parse();
    moss::Checker checker(program);
    checker.run();
    for (const auto& warning : checker.warnings())
      std::cerr << "moss:" << warning.line << ": warning: " << warning.message << "\n";

    moss::FunctionalOptimizer(program).run(optimize_shared_memory);
    if (dump_functional || explain_fusion)
      moss::dump_functional_ir(std::cout, program,
                               explain_fusion && !dump_functional);

    auto plan = moss::BackendOptimizer(program).run(
        optimize_shared_memory, requested_clusters);

    if (check_only) {
      std::cout << input << ": ok\n";
      return 0;
    }

    moss::Generator gen(program, plan, await_error_handling);
    string rust = gen.generate();
    if (output.empty()) {
      auto pos = input.find_last_of('.');
      output = (pos == string::npos ? input : input.substr(0, pos)) + ".rs";
    }
    std::ofstream out(output);
    if (!out) { std::cerr << "moss: cannot write " << output << "\n"; return 1; }
    out << rust;
    std::cout << "generated " << output << "\n";
    return 0;
  } catch (const moss::CompileError& e) {
    std::cerr << "moss:" << e.line << ": error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "moss: error: " << e.what() << "\n";
    return 1;
  }
}
