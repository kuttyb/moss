#include <algorithm>
#include <cctype>
#include <fstream>
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

using std::string;
using std::vector;

namespace moss {

struct CompileError : std::runtime_error {
  int line;
  CompileError(int ln, const string& msg) : std::runtime_error(msg), line(ln) {}
};

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
  return type;
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

static string normalize_pipeline(string expression) {
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
  if (stages.empty()) return expression;
  stages.push_back(trim(expression.substr(start)));
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

struct Line {
  int no = 0;
  int indent = 0;
  string text;
};

struct Field { string name, type, init; int line = 0; };
struct Param { string name, type; };

struct Stmt {
  enum class Kind {
    Raw,
    Assign,
    Call,
    Message,
    Echo,
    If,
    Else,
    While,
    Let,
    Var,
    AwaitMessage,
    Reply,
    Return
  } kind = Kind::Raw;
  int line = 0;
  int indent = 0; // relative logical indent inside handler/main
  string text;
  string a, b, c; // generic payloads
  vector<string> args;
  bool is_mutable = false;
  bool declaration = true;
};

struct Handler {
  string name;
  string header;
  vector<Param> params;
  std::optional<string> reply_type;
  vector<Stmt> body;
  int line = 0;
};

struct Domain {
  string name;
  string header;
  vector<Field> state;
  vector<Handler> handlers;
  int line = 0;
};

struct ObjectType {
  string name;
  string header;
  vector<Field> fields;
  int line = 0;
};

struct MainProc {
  vector<Stmt> body;
  int line = 0;
  string header;
};

struct Function {
  string name;
  string header;
  vector<Param> params;
  std::optional<string> return_type;
  vector<Stmt> body;
  std::optional<string> result_expression;
  int result_line = 0;
  bool expression_body = false;
  int line = 0;
};

struct Program {
  vector<Function> functions;
  vector<ObjectType> objects;
  vector<Domain> domains;
  std::optional<MainProc> main;
};

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
      auto c = L.text.find(':');
      Field f;
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
        fail(L, "domain member must be 'var', 'on', or 'fn'");
      }
    }
    return d;
  }

  Field parse_state_field() {
    Line L = lines_[i_++];
    string rest = trim(L.text.substr(4));
    auto c = rest.find(':');
    if (c == string::npos) fail(L, "state declaration must be 'var name: type = value'");
    Field f;
    f.name = trim(rest.substr(0, c));
    string rhs = trim(rest.substr(c + 1));
    auto eq = rhs.find('=');
    if (eq == string::npos) {
      f.type = canonical_type_name(trim(rhs));
    } else {
      f.type = canonical_type_name(trim(rhs.substr(0, eq)));
      f.init = trim(rhs.substr(eq + 1));
    }
    f.line = L.no;
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
      continue;
    }
    out.push_back(Line{no, indent, std::move(text)});
  }
  return out;
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
  }

  void run() {
    infer_object_fields();
    infer_function_signatures();
    check_objects();
    for (const auto& f : p_.functions) check_function(f);
    for (const auto& d : p_.domains) check_domain(d);
    if (p_.main) check_main(*p_.main);
  }

 private:
  Program& p_;
  std::unordered_map<string, Function*> functions_;
  std::unordered_map<string, Domain*> domains_;
  std::unordered_map<string, ObjectType*> objects_;

  [[noreturn]] void err(int line, const string& msg) const { throw CompileError(line, msg); }

  bool valid_type(const string& t) const {
    string type = canonical_type_name(t);
    if (type == "int" || type == "float" || type == "bool" || type == "string") return true;
    if (domains_.count(type) || objects_.count(type)) return true;
    if ((starts_with(type, "seq[") || starts_with(type, "option[")) && ends_with(type, "]"))
      return valid_type(trim(type.substr(type.find('[')+1, type.size()-type.find('[')-2)));
    if (starts_with(type, "table[") && ends_with(type, "]")) {
      auto inside = type.substr(6, type.size()-7);
      auto ps = split_top_level(inside, ',');
      return ps.size() == 2 && valid_type(ps[0]) && valid_type(ps[1]);
    }
    return false;
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

  Handler* find_handler(Domain& d, const string& name) {
    for (auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

  void check_objects() const {
    for (const auto& object : p_.objects) {
      std::set<string> field_names;
      for (const auto& field : object.fields) {
        if (!valid_type(field.type)) err(field.line, "unknown field type '" + field.type + "'");
        if (!field_names.insert(field.name).second)
          err(field.line, "duplicate object field '" + field.name + "' in " + object.name);
      }
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
      if (h.reply_type && std::none_of(h.body.begin(), h.body.end(), [](const Stmt& s) {
            return s.kind == Stmt::Kind::Reply;
          }))
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
    if (f.return_type && *f.return_type != "unit" && !valid_type(*f.return_type))
      err(f.line, "unknown return type '" + *f.return_type + "' in function '" + f.name + "'");
    for (const auto& param : f.params) {
      if (param.type.empty())
        err(f.line, "cannot infer type for parameter '" + param.name +
            "' in function '" + f.name + "'");
      if (!valid_type(param.type)) err(f.line, "unknown parameter type '" + param.type + "'");
      if (!names.insert(param.name).second) err(f.line, "duplicate parameter: " + param.name);
      env[param.name] = param.type;
    }
    infer_statement_expressions(f.body, env);
    check_stmts(f.body, env, nullptr, nullptr, &f);
    OwnershipEnv ownership;
    ownership.types = env;
    check_ownership(f.body, std::move(ownership), nullptr, nullptr);
    if (f.result_expression) {
      check_expression(f.result_line ? f.result_line : f.line, *f.result_expression, env);
      auto actual = inferred_expr_type(*f.result_expression, env);
      if (!actual)
        err(f.result_line ? f.result_line : f.line,
            "cannot infer the result type of function '" + f.name + "'");
      if (!f.return_type) {
        // The signature pass normally fills this in. Keep this assignment as a
        // defensive fallback for a function whose result was inferred late.
        const_cast<Function&>(f).return_type = *actual;
      } else if (canonical_type_name(*f.return_type) != canonical_type_name(*actual)) {
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
    string e = normalize_pipeline(trim(expression));
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
        return argument_type;
      }
      auto function = functions_.find(callee);
      if (function != functions_.end() && function->second->return_type)
        return canonical_type_name(*function->second->return_type);
      return std::nullopt;
    }();
    if (object_call) return object_call;

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

    if (auto comparison = split_binary(e, {"==", "!=", "<=", ">=", "<", ">"}))
      return string("bool");
    if (auto arithmetic = split_binary(e, {"+", "-", "*", "/"})) {
      auto left = inferred_expr_type(arithmetic->first, env);
      auto right = inferred_expr_type(arithmetic->second, env);
      if (left && right) {
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
            if (parameter.type.empty()) parameter.type = *actual;
            else if (!same_type(parameter.type, *actual))
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
      auto colon = arg.find(':');
      if (colon == string::npos)
        err(line, "object constructor fields must be named for '" + constructor + "'");
      supplied[trim(arg.substr(0, colon))] = trim(arg.substr(colon + 1));
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

  void infer_statement_expressions(const vector<Stmt>& statements,
                                   std::unordered_map<string,string>& env) {
    for (const auto& statement : statements) {
      switch (statement.kind) {
        case Stmt::Kind::If:
        case Stmt::Kind::While:
          constrain_constructor_fields(statement.line, statement.a, env);
          break;
        case Stmt::Kind::Let:
        case Stmt::Kind::Var:
        case Stmt::Kind::Assign:
          constrain_constructor_fields(statement.line, statement.b, env);
          if (statement.kind == Stmt::Kind::Assign && simple_identifier(statement.a)) {
            if (auto spawned = spawn_domain(statement.b)) env[statement.a] = *spawned;
            else if (auto type = inferred_expr_type(statement.b, env)) env[statement.a] = *type;
          } else if (statement.kind != Stmt::Kind::Assign) {
            if (auto spawned = spawn_domain(statement.b)) env[statement.a] = *spawned;
            else if (auto type = inferred_expr_type(statement.b, env)) env[statement.a] = *type;
          }
          break;
        case Stmt::Kind::Message:
        case Stmt::Kind::AwaitMessage:
        case Stmt::Kind::Call:
          for (const auto& arg : statement.args) constrain_constructor_fields(statement.line, arg, env);
          if (statement.kind == Stmt::Kind::Message || statement.kind == Stmt::Kind::AwaitMessage) {
            const string& receiver_name = statement.kind == Stmt::Kind::Message
                ? statement.a : statement.b;
            const string& handler_name = statement.kind == Stmt::Kind::Message
                ? statement.b : statement.c;
            auto receiver = env.find(receiver_name);
            if (receiver != env.end() && domains_.count(receiver->second)) {
              auto* handler = find_handler(*domains_.at(receiver->second), handler_name);
              if (handler) {
                for (size_t index = 0;
                     index < statement.args.size() && index < handler->params.size(); ++index) {
                  auto actual = inferred_expr_type(statement.args[index], env);
                  if (!actual) continue;
                  auto& parameter = handler->params[index];
                  if (parameter.type.empty()) parameter.type = *actual;
                  else if (!same_type(parameter.type, *actual))
                    err(statement.line, "argument " + std::to_string(index + 1) +
                        " to message " + receiver->second + "." + handler_name +
                        " has type '" + *actual + "', expected '" + parameter.type + "'");
                }
              }
            }
          }
          if (statement.kind == Stmt::Kind::AwaitMessage) {
            auto receiver = env.find(statement.b);
            if (receiver != env.end() && domains_.count(receiver->second)) {
              const Handler* handler = find_handler(*domains_.at(receiver->second), statement.c);
              if (handler && handler->reply_type) env[statement.a] = *handler->reply_type;
            }
          }
          if (statement.kind == Stmt::Kind::Call && statement.b.empty()) {
            auto function = functions_.find(statement.a);
            if (function != functions_.end()) {
              for (size_t i = 0; i < statement.args.size() && i < function->second->params.size(); ++i) {
                auto actual = inferred_expr_type(statement.args[i], env);
                if (!actual) continue;
                auto& parameter = function->second->params[i];
                if (parameter.type.empty()) parameter.type = *actual;
                else if (!same_type(parameter.type, *actual))
                  err(statement.line, "argument " + std::to_string(i + 1) + " to function '" +
                      function->second->name + "' has type '" + *actual +
                      "', expected '" + parameter.type + "'");
              }
            }
          }
          break;
        case Stmt::Kind::Echo:
          for (const auto& arg : statement.args) constrain_constructor_fields(statement.line, arg, env);
          break;
        case Stmt::Kind::Reply:
        case Stmt::Kind::Return:
          if (!statement.a.empty()) constrain_constructor_fields(statement.line, statement.a, env);
          break;
        case Stmt::Kind::Raw:
          constrain_constructor_fields(statement.line, statement.text, env);
          break;
        case Stmt::Kind::Else:
          break;
      }
    }
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

  void infer_function_signatures() {
    for (size_t round = 0; round <= p_.functions.size() * 3 + 3; ++round) {
      for (auto& function : p_.functions) {
        std::unordered_map<string,string> env;
        for (const auto& parameter : function.params) env[parameter.name] = parameter.type;
        infer_statement_expressions(function.body, env);
        if (function.result_expression) {
          constrain_constructor_fields(function.result_line, *function.result_expression, env);
          if (auto result = inferred_expr_type(*function.result_expression, env)) {
            if (!function.return_type) function.return_type = *result;
            else if (!same_type(*function.return_type, *result))
              err(function.result_line, "function '" + function.name + "' returns '" + *result +
                  "' but is annotated '" + *function.return_type + "'");
          }
        } else if (!function.return_type) {
          for (const auto& statement : function.body) {
            if (statement.kind != Stmt::Kind::Return || statement.a.empty()) continue;
            auto result = inferred_expr_type(statement.a, env);
            if (!result) continue;
            if (!function.return_type) function.return_type = *result;
            else if (!same_type(*function.return_type, *result))
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
    for (auto& function : p_.functions) {
      for (const auto& parameter : function.params) {
        if (parameter.type.empty())
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

  void require_available(int line, const string& expression, const OwnershipEnv& env) const {
    for (const auto& entry : env.moved) {
      if (expression_uses(expression, entry.first)) {
        err(line, "value '" + entry.first + "' was transferred to '" +
            entry.second.destination + "' at line " + std::to_string(entry.second.line) +
            ". Create an explicit deep copy if both values must remain independently usable.");
      }
    }
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
    if (!transfer_type(expected_type)) return;
    string value = trim(expression);

    auto object = objects_.find(trim(expected_type));
    auto lp = value.find('(');
    if (object != objects_.end() && lp != string::npos && ends_with(value, ")") &&
        trim(value.substr(0, lp)) == object->first) {
      auto parts = split_top_level(value.substr(lp + 1, value.size() - lp - 2), ',');
      std::unordered_map<string, string> fields;
      if (parts.size() == 1 && parts.front().empty()) parts.clear();
      for (const auto& part : parts) {
        auto colon = part.find(':');
        if (colon != string::npos)
          fields[trim(part.substr(0, colon))] = trim(part.substr(colon + 1));
      }
      for (const auto& field : object->second->fields) {
        auto supplied = fields.find(field.name);
        if (supplied != fields.end())
          require_cross_domain_value(line, supplied->second, field.type, env, action);
      }
      return;
    }

    for (const auto& binding : env.types) {
      if (!transfer_type(binding.second) || !expression_uses(value, binding.first)) continue;
      if (env.state_fields.count(binding.first))
        err(line, "domain state '" + binding.first + "' cannot be transferred" +
            (action.empty() ? " by message" : action));
      err(line, "owned non-primitive value '" + binding.first +
          "' cannot cross a domain boundary" + action +
          "; construct a fresh message value instead");
    }
  }

  void check_ownership(const vector<Stmt>& statements, OwnershipEnv env,
                       const Domain* current_domain,
                       const Handler* current_handler) const {
    size_t index = 0;
    check_ownership_block(statements, index, 0, env, current_domain, current_handler);
  }

  void check_ownership_block(const vector<Stmt>& statements, size_t& index, int level,
                             OwnershipEnv& env, const Domain* current_domain,
                             const Handler* current_handler) const {
    while (index < statements.size()) {
      const Stmt& s = statements[index];
      if (s.indent < level) return;
      if (s.indent > level) return;
      if (s.kind == Stmt::Kind::Else) return;

      if (s.kind == Stmt::Kind::If || s.kind == Stmt::Kind::While) {
        require_available(s.line, s.a, env);
        bool is_if = s.kind == Stmt::Kind::If;
        ++index;
        OwnershipEnv body = env;
        check_ownership_block(statements, index, level + 1, body, current_domain, current_handler);
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          OwnershipEnv alternative = env;
          check_ownership_block(statements, index, level + 1, alternative,
                                current_domain, current_handler);
          merge_moved(env, alternative);
        }
        merge_moved(env, body);
        continue;
      }

      switch (s.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          require_available(s.line, s.b, env);
          std::optional<string> type;
          if (auto spawned = spawn_domain(s.b)) type = *spawned;
          else type = inferred_expr_type(s.b, env.types);

          string source = trim(s.b);
          bool transfers = simple_identifier(source) && env.types.count(source) &&
                           transfer_type(env.types.at(source));
          if (transfers && env.state_fields.count(source))
            err(s.line, "domain state '" + source + "' cannot be transferred into local '" + s.a +
                "'; the field must remain available for later messages");

          env.types[s.a] = type.value_or("_value");
          env.moved.erase(s.a); // A declaration may intentionally shadow an older moved binding.
          if (transfers && source != s.a) env.moved[source] = MoveInfo{s.line, s.a};
          ++index;
          break;
        }
        case Stmt::Kind::Assign: {
          require_available(s.line, s.b, env);
          if (auto spawned = spawn_domain(s.b)) {
            env.types[s.a] = *spawned;
            ++index;
            break;
          }
          if (simple_identifier(s.a) && env.types.count(s.a)) {
            string source = trim(s.b);
            bool transfers = simple_identifier(source) && env.types.count(source) &&
                             transfer_type(env.types.at(source));
            if (transfers && env.state_fields.count(source))
              err(s.line, "domain state '" + source + "' cannot be transferred into '" + s.a + "'");
            if (transfers && source != s.a) {
              env.moved.erase(s.a);
              env.moved[source] = MoveInfo{s.line, s.a};
            }
          }
          ++index;
          break;
        }
        case Stmt::Kind::AwaitMessage: {
          require_available(s.line, s.b, env);
          const Handler* awaited = check_call(s.line, s.b, s.c, s.args, env.types);
          for (size_t arg_index = 0; arg_index < s.args.size(); ++arg_index) {
            const auto& arg = s.args[arg_index];
            require_available(s.line, arg, env);
            require_cross_domain_value(s.line, arg, awaited->params[arg_index].type,
                                       env, "");
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
          require_available(s.line, s.a, env);
          const Handler* handler = check_call(s.line, s.a, s.b, s.args, env.types);
          const auto receiver = env.types.find(s.a);
          const Domain* target = receiver != env.types.end() && domains_.count(receiver->second)
              ? domains_.at(receiver->second) : nullptr;
          for (size_t arg_index = 0; arg_index < s.args.size(); ++arg_index) {
            const auto& arg = s.args[arg_index];
            require_available(s.line, arg, env);
            bool stays_in_domain = current_domain && s.a == "self";
            if (!stays_in_domain)
              require_cross_domain_value(s.line, arg, handler->params[arg_index].type,
                                         env, "");
            string source = trim(arg);
            if (!simple_identifier(source) || !env.types.count(source)) continue;
            string source_type = env.types.at(source);
            if (arg_index < handler->params.size() && transfer_type(source_type)) {
              if (env.state_fields.count(source))
                err(s.line, "domain state '" + source + "' cannot be transferred by message");
              if (!stays_in_domain)
                err(s.line, "owned non-primitive value '" + source +
                    "' cannot cross a domain boundary; construct a fresh message value instead");
              string destination = target ? target->name + "." + handler->name : handler->name;
              env.moved[source] = MoveInfo{s.line, destination};
            }
          }
          ++index;
          break;
        }
        case Stmt::Kind::Echo:
          for (const auto& arg : s.args) require_available(s.line, arg, env);
          ++index;
          break;
        case Stmt::Kind::Call:
          require_available(s.line, s.a, env);
          for (const auto& arg : s.args) require_available(s.line, arg, env);
          ++index;
          break;
        case Stmt::Kind::Reply: {
          require_available(s.line, s.a, env);
          if (current_handler && current_handler->reply_type)
            require_cross_domain_value(s.line, s.a, *current_handler->reply_type,
                                       env, " in a reply");
          ++index;
          break;
        }
        case Stmt::Kind::Raw:
          require_available(s.line, s.text, env);
          ++index;
          break;
        case Stmt::Kind::Return:
          if (!s.a.empty()) require_available(s.line, s.a, env);
          ++index;
          break;
        case Stmt::Kind::If:
        case Stmt::Kind::Else:
        case Stmt::Kind::While:
          return;
      }
    }
  }

  void check_function_call(int line, const string& name, const vector<string>& args,
                           const std::unordered_map<string,string>& env) const {
    auto function = functions_.find(name);
    if (function == functions_.end()) {
      if (name == "sqrt" || name == "sum") return;
      err(line, "unknown local function '" + name + "'");
    }
    if (function->second->params.size() != args.size())
      err(line, "function " + name + " expects " +
          std::to_string(function->second->params.size()) + " arguments, got " +
          std::to_string(args.size()));
    for (size_t index = 0; index < args.size(); ++index) {
      auto actual = inferred_expr_type(args[index], env);
      if (actual && !same_type(function->second->params[index].type, *actual))
        err(line, "argument " + std::to_string(index + 1) + " to function '" + name +
            "' has type '" + *actual + "', expected '" +
            function->second->params[index].type + "'");
    }
  }

  void check_expression(int line, const string& expression,
                        const std::unordered_map<string,string>& env) const {
    string value = normalize_pipeline(trim(expression));
    string receiver, handler;
    vector<string> args;
    if (parse_member_call(value, receiver, handler, args)) {
      auto it = env.find(receiver);
      if (it != env.end() && domains_.count(it->second))
        err(line, "naked cross-domain call '" + receiver + "." + handler +
            "' requires 'message' or 'await'");
      check_expression(line, receiver, env);
      for (const auto& arg : args) check_expression(line, arg, env);
      return;
    }
    string callee;
    if (parse_simple_call(value, callee, args) && callee.find('.') == string::npos) {
      if (objects_.count(callee)) {
        for (const auto& arg : args) {
          auto colon = arg.find(':');
          check_expression(line, colon == string::npos ? arg : arg.substr(colon + 1), env);
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

  void check_stmts(const vector<Stmt>& ss, std::unordered_map<string,string> env,
                   const Domain* current, const Handler* current_handler,
                   const Function* current_function = nullptr) {
    int prev_indent = 0;
    for (size_t i = 0; i < ss.size(); ++i) {
      const auto& s = ss[i];
      if (s.indent > prev_indent + 1) err(s.line, "indentation jumps more than one block level");
      prev_indent = s.indent;

      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var ||
          s.kind == Stmt::Kind::Assign) {
        if (s.kind == Stmt::Kind::Assign) {
          if (auto sd = spawn_domain(s.b)) {
            if (!domains_.count(*sd)) err(s.line, "unknown domain in spawn: " + *sd);
            if (current || current_function)
              err(s.line, "spawning domains inside handlers or functions is not supported in v0.2; create them in main");
            env[s.a] = *sd;
            continue;
          }
          check_expression(s.line, s.b, env);
          if (simple_identifier(s.a) && !env.count(s.a))
            env[s.a] = inferred_expr_type(s.b, env).value_or("_value");
          continue;
        }
        if (auto sd = spawn_domain(s.b)) {
          if (!domains_.count(*sd)) err(s.line, "unknown domain in spawn: " + *sd);
          if (current || current_function)
            err(s.line, "spawning domains inside handlers or functions is not supported in v0.2; create them in main");
          env[s.a] = *sd;
        } else {
          auto source = env.find(trim(s.b));
          if (source != env.end() && domains_.count(source->second))
            env[s.a] = source->second;
          else
            env[s.a] = inferred_expr_type(s.b, env).value_or("_value");
          check_expression(s.line, s.b, env);
        }
      }

      if (s.kind == Stmt::Kind::If || s.kind == Stmt::Kind::While)
        check_expression(s.line, s.a, env);

      if (s.kind == Stmt::Kind::Message) {
        check_call(s.line, s.a, s.b, s.args, env);
        for (const auto& arg : s.args) check_expression(s.line, arg, env);
      }

      if (s.kind == Stmt::Kind::AwaitMessage) {
        if (current && s.b == "self")
          err(s.line, "a domain cannot await itself because handlers are non-reentrant");
        const Handler* h = check_call(s.line, s.b, s.c, s.args, env);
        for (const auto& arg : s.args) check_expression(s.line, arg, env);
        if (!h->reply_type) {
          auto dit = domains_.find(env.at(s.b));
          err(s.line, "cannot await one-way handler '" + dit->second->name + "." + s.c + "'");
        }
        if (!s.declaration && env.count(s.a) &&
            !same_type(env.at(s.a), *h->reply_type))
          err(s.line, "await assignment to '" + s.a + "' has type '" + env.at(s.a) +
              "', expected '" + *h->reply_type + "'");
        env[s.a] = *h->reply_type;
      }

      if (s.kind == Stmt::Kind::Call) {
        if (!s.b.empty()) {
          auto receiver = env.find(s.a);
          if (receiver != env.end() && domains_.count(receiver->second))
            err(s.line, "naked cross-domain call '" + s.a + "." + s.b +
                "' requires 'message' or 'await'");
          err(s.line, "local member calls are not implemented; use a top-level function");
        } else {
          check_function_call(s.line, s.a, s.args, env);
        }
      }

      if (s.kind == Stmt::Kind::Echo) {
        for (const auto& arg : s.args) check_expression(s.line, arg, env);
      }

      if (s.kind == Stmt::Kind::Reply) {
        if (!current || !current_handler || !current_handler->reply_type)
          err(s.line, "reply is only valid in a handler declaring '-> Type'");
        auto actual = inferred_expr_type(s.a, env);
        if (!actual)
          err(s.line, "cannot infer the type of this reply expression; add an annotation or use a statically typed value");
        if (!same_type(*actual, *current_handler->reply_type))
          err(s.line, "reply type mismatch: handler expects '" + *current_handler->reply_type +
              "', expression has type '" + *actual + "'");
        check_expression(s.line, s.a, env);
      }

      if (s.kind == Stmt::Kind::Return && current_handler && !s.a.empty())
        err(s.line, "message handlers cannot return values; use 'reply value' in a handler declaring '-> Type'");
      if (s.kind == Stmt::Kind::Return && !current_handler && !current_function && !s.a.empty())
        err(s.line, "main cannot return a value");
      if (s.kind == Stmt::Kind::Return && current_function && !s.a.empty()) {
        auto actual = inferred_expr_type(s.a, env);
        if (!actual)
          err(s.line, "cannot infer the type of this return expression in function '" +
              current_function->name + "'");
        if (current_function->return_type && !same_type(*actual, *current_function->return_type))
          err(s.line, "function '" + current_function->name + "' returns '" + *actual +
              "' but is annotated '" + *current_function->return_type + "'");
      }
    }
  }
};

enum class MessageTransport { SharedMailbox, DirectSharedMemory };

struct OptimizationPlan {
  std::set<string> direct_shared_memory_domains;
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

  MessageTransport transport_for(const Domain& domain) const {
    return direct_shared_memory_domains.count(domain.name) ? MessageTransport::DirectSharedMemory
                                                            : MessageTransport::SharedMailbox;
  }

};

// Finds domains whose request messages can be eliminated in favor of locked state.
// This is deliberately a backend analysis: it does not add a Moss type, expression,
// or concurrency rule. A domain is promoted only when all of its handlers reply and
// every whole-program call to it uses await, so no asynchronous send can be made
// synchronous by the lowering. Rust verifies the selected state is Send when a
// reference to it crosses a caller-domain thread boundary.
class MessageTransportOptimizer {
 public:
  explicit MessageTransportOptimizer(const Program& program) : program_(program) {
    for (const auto& object : program_.objects) objects_[object.name] = &object;
    for (const auto& domain : program_.domains) domains_[domain.name] = &domain;
  }

  OptimizationPlan run(bool enabled, const vector<vector<string>>& requested_clusters = {}) const {
    OptimizationPlan plan;
    plan.domain_clusters = requested_clusters;
    validate_clusters(plan);

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

    for (const auto& domain : program_.domains) {
      if (enabled && !plan.cluster_for(domain.name) && call_graph_complete &&
          !asynchronously_called.count(domain.name) &&
          domain_is_direct_candidate(domain))
        plan.direct_shared_memory_domains.insert(domain.name);
    }
    return plan;
  }

 private:
  const Program& program_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const Domain*> domains_;

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

    for (const auto& cluster : plan.domain_clusters) {
      std::unordered_map<string, size_t> position;
      for (size_t index = 0; index < cluster.size(); ++index) position[cluster[index]] = index;
      vector<vector<bool>> awaits(cluster.size(), vector<bool>(cluster.size(), false));
      for (const auto& source_name : cluster) {
        const Domain& source = *domains_.at(source_name);
        for (const auto& handler : source.handlers) {
          std::unordered_map<string, string> types;
          types["self"] = source.name;
          for (const auto& field : source.state) types[field.name] = field.type;
          for (const auto& param : handler.params) types[param.name] = param.type;
          for (const auto& statement : handler.body) {
            if (statement.kind == Stmt::Kind::AwaitMessage) {
              auto receiver = types.find(statement.b);
              if (receiver != types.end() && position.count(receiver->second)) {
                awaits[position.at(source.name)][position.at(receiver->second)] = true;
              }
              if (receiver != types.end() && domains_.count(receiver->second)) {
                const Handler* target = find_handler(*domains_.at(receiver->second), statement.c);
                if (target && target->reply_type) types[statement.a] = *target->reply_type;
              }
            } else if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var) {
              auto source_type = types.find(trim(statement.b));
              types[statement.a] = source_type != types.end() && domains_.count(source_type->second)
                  ? source_type->second : "_value";
            }
          }
        }
      }
      for (size_t via = 0; via < cluster.size(); ++via)
        for (size_t from = 0; from < cluster.size(); ++from)
          for (size_t to = 0; to < cluster.size(); ++to)
            awaits[from][to] = awaits[from][to] || (awaits[from][via] && awaits[via][to]);
      for (size_t index = 0; index < cluster.size(); ++index) {
        if (awaits[index][index])
          throw std::runtime_error("clustered await cycle involving '" + cluster[index] +
              "' cannot use direct same-thread dispatch");
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
  }

  string generate() {
    std::ostringstream o;
    o << "// Generated by Moss v0.2. Do not edit by hand.\n";
    o << "// Message transport: lock-backed shared-memory mailboxes.\n";
    o << "// Backend labels below distinguish MESSAGE/MAILBOX, SHARED-MEMORY DIRECT, and CLUSTER-LOCAL code.\n";
    o << "// Moss source remains message-based; these comments identify its Rust lowering.\n";
    if (!await_error_handling_) {
      o << "// Await error handling disabled: generated awaits use unchecked extraction; supervision owns failure handling.\n";
    }
    if (!plan_.direct_shared_memory_domains.empty()) {
      o << "// Message optimization: direct shared-memory dispatch for ";
      bool first = true;
      for (const auto& name : plan_.direct_shared_memory_domains) {
        if (!first) o << ", ";
        first = false;
        o << name;
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
    o << "use std::panic::{catch_unwind, AssertUnwindSafe};\n";
    o << "use std::sync::{Arc, Condvar, Mutex};\n";
    o << "use std::thread;\n\n";
    o << "fn __moss_require_send<T: Send>() {}\n\n";
    gen_shared_channel(o);
    gen_tracker(o);

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
    return plan_.transport_for(domain) == MessageTransport::DirectSharedMemory;
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
    if (x == "unit") return "()";
    if (domains_.count(x)) return x + "Ref";
    if (objects_.count(x)) return x;
    if (starts_with(x, "seq[") && ends_with(x, "]"))
      return "Vec<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "option[") && ends_with(x, "]"))
      return "Option<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "table[") && ends_with(x, "]")) {
      auto ps = split_top_level(x.substr(6, x.size()-7), ',');
      return "HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    return x;
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

  string expr(string e, const Domain* d, const std::set<string>& locals) const {
    e = normalize_pipeline(trim(e));
    // Minimal surface rewrites.
    if (e == "true" || e == "false") return e;
    if (e.size() >= 2 && e.front() == '"' && e.back() == '"') return e + ".to_string()";

    // Nim-like value-object construction: User(name: "a", score: 1)
    auto lp0 = e.find('(');
    if (lp0 != string::npos && ends_with(e, ")")) {
      string head = trim(e.substr(0, lp0));
      auto oit = objects_.find(head);
      if (oit != objects_.end()) {
        auto parts = split_top_level(e.substr(lp0 + 1, e.size() - lp0 - 2), ',');
        std::unordered_map<string,string> values;
        if (parts.size() == 1 && parts[0].empty()) parts.clear();
        for (const auto& part : parts) {
          auto c = part.find(':');
          if (c == string::npos) throw std::runtime_error("object constructor fields must be named");
          values[trim(part.substr(0, c))] = trim(part.substr(c + 1));
        }
        std::ostringstream r;
        r << head << " { ";
        bool first = true;
        for (const auto& f : oit->second->fields) {
          auto vit = values.find(f.name);
          if (vit == values.end()) throw std::runtime_error("missing object constructor field: " + head + "." + f.name);
          string v = expr(vit->second, d, locals);
          string ft = trim(f.type);
          if (!(ft == "int" || ft == "float" || ft == "bool")) v = "(" + v + ").clone()";
          if (!first) r << ", ";
          first = false;
          r << f.name << ": " << v;
        }
        r << " }";
        return r.str();
      }
    }

    string builtin;
    vector<string> builtin_args;
    if (parse_simple_call(e, builtin, builtin_args)) {
      if (builtin == "sqrt" && builtin_args.size() == 1)
        return "(" + expr(builtin_args.front(), d, locals) + ").sqrt()";
      if (builtin == "sum" && builtin_args.size() == 1)
        return expr(builtin_args.front(), d, locals) + ".iter().copied().sum()";
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
          if (!member && fields.count(tok) && !locals.count(tok)) out += "state." + tok;
          else out += tok;
          i = j;
        } else { out.push_back(c); ++i; }
      }
      e = out;
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

  string message_arg(const string& e, const string& type, const Domain* d, const std::set<string>& locals) const {
    string r = expr(e, d, locals);
    string t = trim(type);
    if (copy_type(t) || domains_.count(t) || t == "_") return domains_.count(t) ? "(" + r + ").clone()" : r;
    // Detached non-copy values transfer ownership through a message. The checker
    // diagnoses subsequent source uses; no hidden deep copy is inserted here.
    return r;
  }

  string cluster_call_arg(const string& expression, const string& type,
                          const Domain* source, const std::set<string>& locals,
                          size_t cluster, bool crosses_thread) const {
    string value_type = trim(type);
    if (domains_.count(value_type)) {
      if (plan_.cluster_for(value_type) == std::optional<size_t>(cluster)) {
        if (crosses_thread)
          return "self." + snake_case(value_type) + "_ref.clone()";
        return value_type + "LocalRef";
      }
      string value = expr(expression, source, locals);
      if (crosses_thread) return "(" + value + ").as_ref().clone()";
      return "(" + value + ").clone()";
    }
    return message_arg(expression, type, source, locals);
  }

  void gen_tracker(std::ostringstream& o) {
    backend_comment(o, 0, "runtime completion tracking for generated domain work");
    o << "struct MossTrackerState { pending: usize, failed: bool }\n";
    o << "struct MossTracker { state: Mutex<MossTrackerState>, cv: Condvar }\n";
    o << "impl MossTracker {\n";
    o << "    fn new() -> Self { Self { state: Mutex::new(MossTrackerState { pending: 0, failed: false }), cv: Condvar::new() } }\n";
    o << "    fn begin(&self) { let mut state = self.state.lock().unwrap(); state.pending += 1; }\n";
    o << "    fn end(&self) { let mut state = self.state.lock().unwrap(); state.pending -= 1; if state.pending == 0 { self.cv.notify_all(); } }\n";
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
      source_comment(o, 4, f.line, f.name + ": " + f.type);
      o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    }
    o << "}\n\n";
  }

  void gen_function(std::ostringstream& o, const Function& f) {
    source_comment(o, 0, f.line, f.header.empty() ? "fn " + f.name : f.header);
    backend_comment(o, 0, "LOCAL function: ordinary intra-domain call; no Moss mailbox or domain scheduling");
    o << "fn " << f.name << "(";
    for (size_t index = 0; index < f.params.size(); ++index) {
      if (index) o << ", ";
      o << f.params[index].name << ": " << rust_type(f.params[index].type);
    }
    string return_type = f.return_type.value_or("unit");
    if (return_type != "unit") o << ") -> " << rust_type(return_type) << " {\n";
    else o << ") {\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    for (const auto& parameter : f.params) {
      locals.insert(parameter.name);
      types[parameter.name] = parameter.type;
    }
    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false, false,
              std::nullopt, true);
    if (f.result_expression) {
      source_comment(o, 4, f.result_line, *f.result_expression);
      o << "    " << expr(*f.result_expression, nullptr, locals) << "\n";
    }
    o << "}\n\n";
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    source_comment(o, 0, d.line, d.header.empty() ? "domain " + d.name : d.header);
    if (cluster_for(d)) {
      backend_comment(o, 0, "CLUSTER ingress/message version of " + d.name + "Ref (shared-memory mailbox)");
    } else if (direct_shared_memory(d)) {
      backend_comment(o, 0, "SHARED-MEMORY DIRECT version of " + d.name + "Ref (Arc<Mutex<" + d.name + "State>>)");
    } else {
      backend_comment(o, 0, "MESSAGE/MAILBOX version of " + d.name + "Ref (shared-memory lock-backed queue)");
    }
    o << "#[derive(Clone)]\nstruct " << d.name << "Ref {\n";
    if (auto cluster = cluster_for(d)) {
      o << "    tx: MossSender<MossCluster" << *cluster << "SharedMsg>,\n";
      o << "    tracker: Arc<MossTracker>,\n";
    } else if (direct_shared_memory(d)) {
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

  void gen_direct_domain(std::ostringstream& o, const Domain& d) {
    backend_comment(o, 0, "SHARED-MEMORY DIRECT implementation for domain " + d.name + "; awaited Moss messages call through Arc<Mutex<State>>");
    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      if (!h.reply_type)
        throw std::runtime_error("internal error: one-way handler reached direct shared-memory generation");
      string result_name = reply_binding(d, h);
      source_comment(o, 4, h.line, handler_signature(h));
      backend_comment(o, 4, "SHARED-MEMORY DIRECT handler body; the request mailbox is elided");
      o << "    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      o << ") -> Option<" << rust_type(*h.reply_type) << "> {\n";
      o << "        let mut state = self.state.lock().unwrap();\n";
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
      gen_stmts(o, h.body, &d, &h, result_name, locals, types, 3, true, true);
      o << "        }\n";
      o << "        " << result_name << "\n";
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
    o << "    " << d.name << "Ref { state: Arc::new(Mutex::new(state)) }\n";
    o << "}\n\n";
  }

  void gen_domain(std::ostringstream& o, const Domain& d) {
    source_comment(o, 0, d.line, d.header.empty() ? "domain " + d.name : d.header);
    if (cluster_for(d)) {
      backend_comment(o, 0, "CLUSTER-LOCAL state representation for domain " + d.name + " (RefCell on the cluster worker)");
    } else if (direct_shared_memory(d)) {
      backend_comment(o, 0, "SHARED-MEMORY DIRECT state representation for domain " + d.name);
    } else {
      backend_comment(o, 0, "MESSAGE/MAILBOX state representation for domain " + d.name + " (one worker drains a shared-memory queue)");
    }
    o << "struct " << d.name << "State {\n";
    for (const auto& f : d.state) {
      source_comment(o, 4, f.line, state_field_signature(f));
      o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    }
    o << "}\n\n";

    if (cluster_for(d)) return;

    if (direct_shared_memory(d)) {
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
      gen_stmts(o, h.body, &d, &h, reply_name, locals, types, 6, true);
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
                  3, true, true, cluster_index);
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
    gen_stmts(o, m.body, nullptr, nullptr, "", locals, types, 1, false);
    o << "    __tracker.wait_zero();\n";
    o << "}\n";
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool direct_reply = false,
                 std::optional<size_t> cluster_context = std::nullopt,
                 bool in_function = false) {
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_sender, locals, types, base,
              in_handler, direct_reply, cluster_context, in_function);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool direct_reply,
                 std::optional<size_t> cluster_context, bool in_function) {
    auto indent = [&](int lev){ return string((base + lev) * 4, ' '); };
    while (i < ss.size()) {
      const auto& s = ss[i];
      if (s.indent < level) return;
      if (s.indent > level) throw std::runtime_error("internal error: unexpected statement indentation");
      if (s.kind == Stmt::Kind::Else) return; // consumed by the preceding if

      source_comment(o, (base + level) * 4, s.line, s.text);

      switch (s.kind) {
        case Stmt::Kind::If: {
          o << indent(level) << "if " << expr(s.a, d, locals) << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                    child_locals, child_types, base, in_handler, direct_reply,
                    cluster_context, in_function);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            source_comment(o, (base + level + 1) * 4, ss[i].line, ss[i].text);
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                      else_locals, else_types, base, in_handler, direct_reply,
                      cluster_context, in_function);
            o << indent(level) << "}\n";
          } else {
            o << "\n";
          }
          break;
        }
        case Stmt::Kind::While: {
          o << indent(level) << "while " << expr(s.a, d, locals) << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                    child_locals, child_types, base, in_handler, direct_reply,
                    cluster_context, in_function);
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
            for (const auto& a : s.args) o << ", " << expr(a, d, locals);
            o << ");\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Assign: {
          if (auto sd = CheckerSpawn(s.b)) {
            if (auto cluster = plan_.cluster_for(*sd)) {
              backend_comment(o, (base + level) * 4,
                              "CLUSTER-LOCAL domain handle; spawn is bound to the cluster worker");
              o << indent(level) << "let " << s.a << " = "
                << cluster_spawn_binding(*cluster, *sd) << ".clone();\n";
            } else {
              backend_comment(o, (base + level) * 4,
                              "MESSAGE/MAILBOX domain handle; sends use a lock-backed shared-memory queue");
              o << indent(level) << "let " << s.a << " = spawn_" << snake_case(*sd)
                << "(__tracker.clone());\n";
            }
            locals.insert(s.a);
            types[s.a] = *sd;
            ++i;
            break;
          }
          string lhs = expr(s.a, d, locals);
          bool state_field = d && std::any_of(d->state.begin(), d->state.end(),
                                              [&](const Field& field) { return field.name == s.a; });
          if (plain_identifier(s.a) && !locals.count(s.a) && !state_field) {
            backend_comment(o, (base + level) * 4,
                            "LOCAL assignment: introduce an inferred Moss binding");
            o << indent(level) << "let mut " << s.a << " = " << expr(s.b, d, locals) << ";\n";
            locals.insert(s.a);
            types[s.a] = "_value";
          } else {
            o << indent(level) << lhs << " = " << expr(s.b, d, locals) << ";\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Call: {
          o << indent(level) << expr(s.a, d, locals);
          if (!s.b.empty()) o << "." << s.b;
          o << "(";
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            o << expr(s.args[k], d, locals);
          }
          o << ");\n";
          ++i;
          break;
        }
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          auto sd = CheckerSpawn(s.b);
          if (sd) {
            if (auto cluster = plan_.cluster_for(*sd)) {
              backend_comment(o, (base + level) * 4,
                              "CLUSTER-LOCAL domain handle; spawn is bound to the cluster worker");
            } else if (domains_.count(*sd) && direct_shared_memory(*domains_.at(*sd))) {
              backend_comment(o, (base + level) * 4,
                              "SHARED-MEMORY DIRECT domain handle; state is protected by Arc<Mutex<_>>");
            } else {
              backend_comment(o, (base + level) * 4,
                              "MESSAGE/MAILBOX domain handle; sends use a lock-backed shared-memory queue");
            }
            o << indent(level) << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "")
              << s.a << " = ";
            if (auto cluster = plan_.cluster_for(*sd))
              o << cluster_spawn_binding(*cluster, *sd) << ".clone();\n";
            else
              o << "spawn_" << snake_case(*sd) << "(__tracker.clone());\n";
            types[s.a] = *sd;
          } else {
            auto source = types.find(trim(s.b));
            bool domain_capability = source != types.end() && domains_.count(source->second);
            o << indent(level) << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "")
              << s.a << " = " << expr(s.b, d, locals);
            if (domain_capability) o << ".clone()";
            o << ";\n";
            types[s.a] = domain_capability ? source->second : "_value";
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
          string recv = (s.a == "self") ? "self_ref" : expr(s.a, d, locals);
          const Handler* h = target ? find_handler(*target, s.b) : nullptr;
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
                o << cluster_call_arg(s.args[k], typ, d, locals, *cluster_context, false);
              }
              o << ")";
            }
            o << ");\n";
            ++i;
            break;
          }
          if (target && direct_shared_memory(*target))
            throw std::runtime_error("internal error: asynchronous send reached direct shared-memory generation");
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
              o << cluster_call_arg(s.args[k], typ, d, locals, *cluster_context, true);
            else
              o << message_arg(s.args[k], typ, d, locals);
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
          string recv = expr(s.b, d, locals);
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
                                    *cluster_context, false);
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
                            "SHARED-MEMORY DIRECT version: lock the target state and invoke the handler without a request message");
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
                                      *cluster_context, true);
              else
                o << message_arg(s.args[k], h->params[k].type, d, locals);
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
                                    *cluster_context, true);
            else
              o << message_arg(s.args[k], h->params[k].type, d, locals);
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
                                       *cluster_context, false)
                    : message_arg(s.a, *current_handler->reply_type, d, locals))
              << ");\n";
          } else {
            backend_comment(o, (base + level) * 4,
                            "MESSAGE/MAILBOX version: send the reply through a lock-backed one-shot mailbox");
            o << indent(level) << "let _ = " << reply_sender << ".send("
              << message_arg(s.a, *current_handler->reply_type, d, locals) << ");\n";
          }
          o << indent(level) << "break 'handler;\n";
          ++i;
          break;
        case Stmt::Kind::Return:
          if (in_function && !s.a.empty())
            o << indent(level) << "return " << expr(s.a, d, locals) << ";\n";
          else
            o << indent(level) << (in_handler ? "break 'handler;" : "return;") << "\n";
          ++i;
          break;
        case Stmt::Kind::Raw:
          o << indent(level) << expr(s.text, d, locals) << ";\n";
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
  std::cerr << "Moss v0.2 - actor/domain DSL to Rust with await/reply\n\n"
            << "Usage:\n"
            << "  moss <input.moss> [-Oshared-memory] [--no-await-error-handling] [--cluster=A,B] [-o output.rs]\n"
            << "  moss --check <input.moss>\n\n"
            << "Backend optimization:\n"
            << "  -O, -Oshared-memory    eliminate eligible awaited messages with lock-backed state\n"
            << "  -O0                    retain lock-backed mailbox dispatch for every domain\n\n"
            << "  --no-await-error-handling  omit per-await reply checks (supervision owns failures)\n\n"
            << "  --cluster=A,B          place the listed domain types on one generated worker thread\n\n"
            << "Request/reply:\n"
            << "  message domain.Message(args...)\n"
            << "  on Message(args...) -> Type\n"
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
    string input, output;
    vector<vector<string>> requested_clusters;
    for (int i = 1; i < argc; ++i) {
      string a = argv[i];
      if (a == "--check") check_only = true;
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

    auto plan = moss::MessageTransportOptimizer(program).run(
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
