#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
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
  enum class Kind { Raw, Send, Echo, If, Else, While, Let, Var, AwaitLet, Reply, Return } kind = Kind::Raw;
  int line = 0;
  int indent = 0; // relative logical indent inside handler/main
  string text;
  string a, b, c; // generic payloads
  vector<string> args;
  bool is_mutable = false;
};

struct Handler {
  string name;
  vector<Param> params;
  std::optional<string> reply_type;
  vector<Stmt> body;
  int line = 0;
};

struct Domain {
  string name;
  vector<Field> state;
  vector<Handler> handlers;
  int line = 0;
};

struct ObjectType {
  string name;
  vector<Field> fields;
  int line = 0;
};

struct MainProc {
  vector<Stmt> body;
  int line = 0;
};

struct Program {
  vector<ObjectType> objects;
  vector<Domain> domains;
  std::optional<MainProc> main;
};

class Parser {
 public:
  explicit Parser(vector<Line> lines) : lines_(std::move(lines)) {}

  Program parse() {
    Program p;
    while (i_ < lines_.size()) {
      const auto& L = lines_[i_];
      if (L.indent != 0) fail(L, "top-level declaration must start at indentation 0");
      if (starts_with(L.text, "domain ")) p.domains.push_back(parse_domain());
      else if (starts_with(L.text, "type ")) p.objects.push_back(parse_object());
      else if (L.text == "proc main()") {
        if (p.main) fail(L, "duplicate proc main()");
        p.main = parse_main();
      } else {
        fail(L, "expected 'domain', 'type ... = object', or 'proc main()'");
      }
    }
    return p;
  }

 private:
  vector<Line> lines_;
  size_t i_ = 0;

  [[noreturn]] void fail(const Line& L, const string& msg) { throw CompileError(L.no, msg); }

  static bool identifier(const string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
  }

  static bool parse_message_call(const string& text, string& receiver, string& handler,
                                 vector<string>& args) {
    auto dot = text.find('.');
    auto lp = text.find('(', dot == string::npos ? 0 : dot);
    auto rp = text.rfind(')');
    if (dot == string::npos || lp == string::npos || rp != text.size() - 1 || dot >= lp) return false;
    receiver = trim(text.substr(0, dot));
    handler = trim(text.substr(dot + 1, lp - dot - 1));
    if (!identifier(receiver) || !identifier(handler)) return false;
    string inside = text.substr(lp + 1, rp - lp - 1);
    args.clear();
    if (!trim(inside).empty()) args = split_top_level(inside, ',');
    return true;
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
      if (c == string::npos) throw CompileError(L.no, "parameter must be 'name: type': " + part);
      Param p{trim(part.substr(0, c)), trim(part.substr(c + 1))};
      if (p.name.empty() || p.type.empty()) throw CompileError(L.no, "invalid parameter: " + part);
      ps.push_back(std::move(p));
    }
    return ps;
  }

  ObjectType parse_object() {
    Line head = lines_[i_++];
    // type User = object
    string rest = trim(head.text.substr(5));
    auto eq = rest.find('=');
    if (eq == string::npos || trim(rest.substr(eq + 1)) != "object")
      fail(head, "object declaration must be: type Name = object");
    ObjectType o;
    o.name = trim(rest.substr(0, eq));
    o.line = head.no;
    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      auto L = lines_[i_++];
      if (L.indent != head.indent + 2) fail(L, "object fields must be indented by two spaces");
      auto c = L.text.find(':');
      if (c == string::npos) fail(L, "object field must be 'name: type'");
      Field f;
      f.name = trim(L.text.substr(0, c));
      f.type = trim(L.text.substr(c + 1));
      f.line = L.no;
      o.fields.push_back(std::move(f));
    }
    return o;
  }

  Domain parse_domain() {
    Line head = lines_[i_++];
    Domain d;
    d.name = trim(head.text.substr(7));
    d.line = head.no;
    if (d.name.empty()) fail(head, "domain name is required");

    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      const auto L = lines_[i_];
      if (L.indent != head.indent + 2) fail(L, "domain members must be indented by two spaces");
      if (starts_with(L.text, "var ")) {
        d.state.push_back(parse_state_field());
      } else if (starts_with(L.text, "on ")) {
        d.handlers.push_back(parse_handler(head.indent + 2));
      } else {
        fail(L, "domain member must be 'var' or 'on'");
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
      f.type = trim(rhs);
    } else {
      f.type = trim(rhs.substr(0, eq));
      f.init = trim(rhs.substr(eq + 1));
    }
    f.line = L.no;
    return f;
  }

  Handler parse_handler(int member_indent) {
    Line head = lines_[i_++];
    string sig = trim(head.text.substr(3));
    auto lp = sig.find('('), rp = sig.rfind(')');
    if (lp == string::npos || rp == string::npos || rp < lp)
      fail(head, "handler must be 'on Name(args...)' or 'on Name(args...) -> Type'");
    Handler h;
    h.name = trim(sig.substr(0, lp));
    if (!identifier(h.name)) fail(head, "invalid handler name '" + h.name + "'");
    h.params = parse_params(head, sig.substr(lp + 1, rp - lp - 1));
    string suffix = trim(sig.substr(rp + 1));
    if (!suffix.empty()) {
      if (!starts_with(suffix, "->"))
        fail(head, "handler must be 'on Name(args...)' or 'on Name(args...) -> Type'");
      string type = trim(suffix.substr(2));
      if (type.empty()) fail(head, "reply type is required after '->'");
      h.reply_type = std::move(type);
    }
    h.line = head.no;
    h.body = parse_stmt_block(member_indent + 2);
    if (h.body.empty()) fail(head, "handler body may not be empty");
    return h;
  }

  MainProc parse_main() {
    Line head = lines_[i_++];
    MainProc m;
    m.line = head.no;
    m.body = parse_stmt_block(2);
    if (m.body.empty()) fail(head, "main body may not be empty");
    return m;
  }

  vector<Stmt> parse_stmt_block(int base_indent) {
    vector<Stmt> out;
    while (i_ < lines_.size()) {
      Line L = lines_[i_];
      if (L.indent < base_indent) break;
      if (L.indent % 2 != 0) fail(L, "indentation must use multiples of two spaces");
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
    s.indent = (L.indent - base_indent) / 2;
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
    if (starts_with(L.text, "let ") || starts_with(L.text, "var ")) {
      bool is_var = starts_with(L.text, "var ");
      s.kind = is_var ? Stmt::Kind::Var : Stmt::Kind::Let;
      string rest = trim(L.text.substr(4));
      auto eq = rest.find('=');
      if (eq == string::npos) fail(L, is_var ? "local var requires an initializer in v0.2" : "let requires an initializer");
      s.a = trim(rest.substr(0, eq));
      s.b = trim(rest.substr(eq + 1));
      if (starts_with(s.b, "await ")) {
        s.kind = Stmt::Kind::AwaitLet;
        s.is_mutable = is_var;
        string call = trim(s.b.substr(6));
        if (!parse_message_call(call, s.b, s.c, s.args))
          fail(L, "await requires a message call 'receiver.Handler(args)'");
      } else if (contains_word_outside_string(s.b, "await")) {
        fail(L, "await is only supported as the complete initializer of let or local var");
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
    if (starts_with(L.text, "return ")) fail(L, "message handlers cannot return values; use 'reply value' in a handler declaring '-> Type'");

    if (contains_word_outside_string(L.text, "await"))
      fail(L, "await is only supported as the complete initializer of let or local var");

    // Message send candidate: receiver.Message(args), as a standalone statement.
    if (parse_message_call(L.text, s.a, s.b, s.args)) {
      s.kind = Stmt::Kind::Send;
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
    if (raw.find('\t') != string::npos) throw CompileError(no, "tabs are not allowed; use two-space indentation");

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
    out.push_back(Line{no, indent, trim(raw.substr(indent))});
  }
  return out;
}

class Checker {
 public:
  explicit Checker(const Program& p) : p_(p) {
    for (const auto& d : p_.domains) {
      if (!domains_.emplace(d.name, &d).second) err(d.line, "duplicate domain: " + d.name);
    }
    for (const auto& o : p_.objects) {
      if (!objects_.emplace(o.name, &o).second) err(o.line, "duplicate object type: " + o.name);
    }
  }

  void run() {
    for (const auto& d : p_.domains) check_domain(d);
    if (p_.main) check_main(*p_.main);
  }

 private:
  const Program& p_;
  std::unordered_map<string, const Domain*> domains_;
  std::unordered_map<string, const ObjectType*> objects_;

  [[noreturn]] void err(int line, const string& msg) const { throw CompileError(line, msg); }

  bool valid_type(const string& t) const {
    if (t == "int" || t == "float" || t == "bool" || t == "string") return true;
    if (domains_.count(t) || objects_.count(t)) return true;
    if ((starts_with(t, "seq[") || starts_with(t, "option[")) && ends_with(t, "]"))
      return valid_type(trim(t.substr(t.find('[')+1, t.size()-t.find('[')-2)));
    if (starts_with(t, "table[") && ends_with(t, "]")) {
      auto inside = t.substr(6, t.size()-7);
      auto ps = split_top_level(inside, ',');
      return ps.size() == 2 && valid_type(ps[0]) && valid_type(ps[1]);
    }
    return false;
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
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
        if (!valid_type(p.type)) err(h.line, "unknown parameter type '" + p.type + "'");
        if (env.count(p.name)) err(h.line, "duplicate parameter '" + p.name + "'");
        env[p.name] = p.type;
      }
      for (const auto& f : d.state) env[f.name] = f.type;
      check_stmts(h.body, env, &d, &h);
      OwnershipEnv ownership;
      ownership.types = env;
      for (const auto& f : d.state) ownership.state_fields.insert(f.name);
      check_ownership(h.body, std::move(ownership));
    }
  }

  void check_main(const MainProc& m) {
    std::unordered_map<string,string> env;
    check_stmts(m.body, env, nullptr, nullptr);
    OwnershipEnv ownership;
    ownership.types = std::move(env);
    check_ownership(m.body, std::move(ownership));
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
      err(line, "'" + receiver + "' is not an actor/domain reference; dotted standalone calls are reserved for message sends in v0.2");
    const Handler* h = find_handler(*dit->second, message);
    if (!h) err(line, "domain " + dit->second->name + " has no message handler '" + message + "'");
    if (h->params.size() != args.size())
      err(line, "message " + dit->second->name + "." + message + " expects " +
          std::to_string(h->params.size()) + " arguments, got " + std::to_string(args.size()));
    return h;
  }

  static std::optional<string> obvious_expr_type(const string& expression,
                                                  const std::unordered_map<string,string>& env) {
    string e = trim(expression);
    if (e == "true" || e == "false") return "bool";
    if (e.size() >= 2 && e.front() == '"' && e.back() == '"') return "string";
    auto local = env.find(e);
    if (local != env.end() && local->second != "_value") return local->second;
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
    string t = trim(type);
    if (t == "int" || t == "float" || t == "bool") return true;
    if (starts_with(t, "option[") && ends_with(t, "]"))
      return copy_type(trim(t.substr(7, t.size() - 8)));
    return false;
  }

  std::optional<string> inferred_expr_type(const string& expression,
                                           const std::unordered_map<string,string>& env) const {
    if (auto type = obvious_expr_type(expression, env)) return type;
    string e = trim(expression);
    auto lp = e.find('(');
    if (lp != string::npos && ends_with(e, ")")) {
      string constructor = trim(e.substr(0, lp));
      if (objects_.count(constructor)) return constructor;
    }
    return std::nullopt;
  }

  void require_available(int line, const string& expression, const OwnershipEnv& env) const {
    for (const auto& entry : env.moved) {
      if (expression_uses(expression, entry.first)) {
        err(line, "use of moved value '" + entry.first + "'; assignment to '" +
            entry.second.destination + "' transferred ownership at line " +
            std::to_string(entry.second.line));
      }
    }
  }

  static void merge_moved(OwnershipEnv& destination, const OwnershipEnv& branch) {
    for (const auto& entry : branch.moved) {
      if (destination.types.count(entry.first)) destination.moved.emplace(entry.first, entry.second);
    }
  }

  void check_ownership(const vector<Stmt>& statements, OwnershipEnv env) const {
    size_t index = 0;
    check_ownership_block(statements, index, 0, env);
  }

  void check_ownership_block(const vector<Stmt>& statements, size_t& index, int level,
                             OwnershipEnv& env) const {
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
        check_ownership_block(statements, index, level + 1, body);
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          OwnershipEnv alternative = env;
          check_ownership_block(statements, index, level + 1, alternative);
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
                           !copy_type(env.types.at(source));
          if (transfers && env.state_fields.count(source))
            err(s.line, "cannot move domain state field '" + source + "' into local '" + s.a +
                "'; the field must remain initialized for later messages");

          env.types[s.a] = type.value_or("_value");
          env.moved.erase(s.a); // A declaration may intentionally shadow an older moved binding.
          if (transfers && source != s.a) env.moved[source] = MoveInfo{s.line, s.a};
          ++index;
          break;
        }
        case Stmt::Kind::AwaitLet:
          require_available(s.line, s.b, env);
          for (const auto& arg : s.args) require_available(s.line, arg, env);
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
        case Stmt::Kind::Send:
          require_available(s.line, s.a, env);
          for (const auto& arg : s.args) require_available(s.line, arg, env);
          ++index;
          break;
        case Stmt::Kind::Echo:
          for (const auto& arg : s.args) require_available(s.line, arg, env);
          ++index;
          break;
        case Stmt::Kind::Reply:
          require_available(s.line, s.a, env);
          ++index;
          break;
        case Stmt::Kind::Raw:
          require_available(s.line, s.text, env);
          ++index;
          break;
        case Stmt::Kind::Return:
          ++index;
          break;
        case Stmt::Kind::If:
        case Stmt::Kind::Else:
        case Stmt::Kind::While:
          return;
      }
    }
  }

  void check_stmts(const vector<Stmt>& ss, std::unordered_map<string,string> env,
                   const Domain* current, const Handler* current_handler) {
    int prev_indent = 0;
    for (size_t i = 0; i < ss.size(); ++i) {
      const auto& s = ss[i];
      if (s.indent > prev_indent + 1) err(s.line, "indentation jumps more than one block level");
      prev_indent = s.indent;

      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var) {
        if (auto sd = spawn_domain(s.b)) {
          if (!domains_.count(*sd)) err(s.line, "unknown domain in spawn: " + *sd);
          if (current) err(s.line, "spawning domains inside handlers is not supported in v0.2; create them in main and pass ActorRefs in messages");
          env[s.a] = *sd;
        } else {
          env[s.a] = "_value";
          // Synchronous-looking cross-domain calls in expressions are forbidden.
          auto dot = s.b.find('.'), lp = s.b.find('(');
          if (dot != string::npos && lp != string::npos && dot < lp) {
            string recv = trim(s.b.substr(0, dot));
            auto it = env.find(recv);
            if (it != env.end() && domains_.count(it->second))
              err(s.line, "cross-domain messages have no direct return value; use await with a reply handler");
          }
        }
      }

      if (s.kind == Stmt::Kind::Send) {
        check_call(s.line, s.a, s.b, s.args, env);
      }

      if (s.kind == Stmt::Kind::AwaitLet) {
        if (current && s.b == "self")
          err(s.line, "a domain cannot await itself because handlers are non-reentrant");
        const Handler* h = check_call(s.line, s.b, s.c, s.args, env);
        if (!h->reply_type) {
          auto dit = domains_.find(env.at(s.b));
          err(s.line, "cannot await one-way handler '" + dit->second->name + "." + s.c + "'");
        }
        env[s.a] = *h->reply_type;
      }

      if (s.kind == Stmt::Kind::Reply) {
        if (!current || !current_handler || !current_handler->reply_type)
          err(s.line, "reply is only valid in a handler declaring '-> Type'");
        if (auto actual = obvious_expr_type(s.a, env); actual && *actual != *current_handler->reply_type)
          err(s.line, "reply type mismatch: handler expects '" + *current_handler->reply_type +
              "', expression has type '" + *actual + "'");
      }
    }
  }
};

class Generator {
 public:
  explicit Generator(const Program& p) : p_(p) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    for (const auto& o : p.objects) objects_[o.name] = &o;
  }

  string generate() {
    std::ostringstream o;
    o << "// Generated by Moss v0.2. Do not edit by hand.\n";
    o << "#![allow(non_snake_case)]\n#![allow(dead_code)]\n";
    o << "#![allow(unused_imports)]\n#![allow(unused_mut)]\n#![allow(unused_variables)]\n\n";
    o << "use std::collections::HashMap;\n";
    o << "use std::sync::mpsc::{self, Sender, Receiver};\n";
    o << "use std::sync::{Arc, Condvar, Mutex};\n";
    o << "use std::thread;\n\n";
    gen_tracker(o);

    for (const auto& t : p_.objects) gen_object(o, t);
    // Refs first because handler message enums can mention refs to later domains.
    for (const auto& d : p_.domains) gen_ref_decl(o, d);
    for (const auto& d : p_.domains) gen_domain(o, d);
    if (p_.main) gen_main(o, *p_.main);
    else o << "fn main() {}\n";
    return o.str();
  }

 private:
  const Program& p_;
  std::unordered_map<string, const Domain*> domains_;
  std::unordered_map<string, const ObjectType*> objects_;
  size_t reply_temp_ = 0;

  string rust_type(const string& t) const {
    string x = trim(t);
    if (x == "int") return "i64";
    if (x == "float") return "f64";
    if (x == "bool") return "bool";
    if (x == "string") return "String";
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

  string default_value(const string& type) const {
    string t = trim(type);
    if (t == "int") return "0";
    if (t == "float") return "0.0";
    if (t == "bool") return "false";
    if (t == "string") return "String::new()";
    if (starts_with(t, "seq[")) return "Vec::new()";
    if (starts_with(t, "table[")) return "HashMap::new()";
    if (starts_with(t, "option[")) return "None";
    return "Default::default()";
  }

  string expr(string e, const Domain* d, const std::set<string>& locals) const {
    e = trim(e);
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
    if (t == "int" || t == "float" || t == "bool" || t == "_") return r;
    // Moss messages have value semantics. v0.2 conservatively clones non-Copy payloads;
    // a later ownership/dataflow pass can elide the clone and lower it to a Rust move.
    return "(" + r + ").clone()";
  }

  void gen_tracker(std::ostringstream& o) {
    o << "struct MossTracker { pending: Mutex<usize>, cv: Condvar }\n";
    o << "impl MossTracker {\n";
    o << "    fn new() -> Self { Self { pending: Mutex::new(0), cv: Condvar::new() } }\n";
    o << "    fn begin(&self) { let mut n = self.pending.lock().unwrap(); *n += 1; }\n";
    o << "    fn end(&self) { let mut n = self.pending.lock().unwrap(); *n -= 1; if *n == 0 { self.cv.notify_all(); } }\n";
    o << "    fn wait_zero(&self) { let mut n = self.pending.lock().unwrap(); while *n != 0 { n = self.cv.wait(n).unwrap(); } }\n";
    o << "}\n\n";
  }

  void gen_object(std::ostringstream& o, const ObjectType& t) {
    o << "#[derive(Clone, Debug)]\nstruct " << t.name << " {\n";
    for (const auto& f : t.fields) o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    o << "}\n\n";
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    o << "#[derive(Clone)]\nstruct " << d.name << "Ref {\n    tx: Sender<" << d.name << "Msg>,\n    tracker: Arc<MossTracker>,\n}\n\n";
  }

  static string reply_binding(const Domain& d, const Handler& h) {
    std::set<string> used;
    for (const auto& f : d.state) used.insert(f.name);
    for (const auto& p : h.params) used.insert(p.name);
    for (const auto& s : h.body) {
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var || s.kind == Stmt::Kind::AwaitLet)
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

  void gen_domain(std::ostringstream& o, const Domain& d) {
    o << "struct " << d.name << "State {\n";
    for (const auto& f : d.state) o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    o << "}\n\n";

    o << "enum " << d.name << "Msg {\n";
    for (const auto& h : d.handlers) {
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
          o << "Sender<" << rust_type(*h.reply_type) << ">";
        }
        o << ")";
      }
      o << ",\n";
    }
    o << "}\n\n";

    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      string reply_name = reply_binding(d, h);
      o << "    fn " << h.name << "(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      if (h.reply_type) o << ", " << reply_name << ": Sender<" << rust_type(*h.reply_type) << ">";
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

    o << "fn spawn_" << snake_case(d.name) << "(tracker: Arc<MossTracker>) -> " << d.name << "Ref {\n";
    o << "    let (tx, rx): (Sender<" << d.name << "Msg>, Receiver<" << d.name << "Msg>) = mpsc::channel();\n";
    o << "    let actor = " << d.name << "Ref { tx: tx.clone(), tracker: tracker.clone() };\n";
    o << "    let self_ref = actor.clone();\n";
    o << "    thread::spawn(move || {\n";
    o << "        let mut state = " << d.name << "State {\n";
    for (const auto& f : d.state) {
      string init = f.init.empty() ? default_value(f.type) : expr(f.init, nullptr, {});
      if (f.type == "string" && !init.empty() && init.front() == '"' && init.back() == '"') init += ".to_string()";
      o << "            " << f.name << ": " << init << ",\n";
    }
    o << "        };\n";
    o << "        while let Ok(msg) = rx.recv() {\n";
    o << "            match msg {\n";
    for (const auto& h : d.handlers) {
      string reply_name = reply_binding(d, h);
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

  void gen_main(std::ostringstream& o, const MainProc& m) {
    o << "fn main() {\n";
    o << "    let __tracker = Arc::new(MossTracker::new());\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    gen_stmts(o, m.body, nullptr, nullptr, "", locals, types, 1, false);
    o << "    __tracker.wait_zero();\n";
    o << "}\n";
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler) {
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_sender, locals, types, base, in_handler);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_sender,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler) {
    auto indent = [&](int lev){ return string((base + lev) * 4, ' '); };
    while (i < ss.size()) {
      const auto& s = ss[i];
      if (s.indent < level) return;
      if (s.indent > level) throw std::runtime_error("internal error: unexpected statement indentation");
      if (s.kind == Stmt::Kind::Else) return; // consumed by the preceding if

      switch (s.kind) {
        case Stmt::Kind::If: {
          o << indent(level) << "if " << expr(s.a, d, locals) << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                    child_locals, child_types, base, in_handler);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_sender,
                      else_locals, else_types, base, in_handler);
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
                    child_locals, child_types, base, in_handler);
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
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          auto sd = CheckerSpawn(s.b);
          if (sd) {
            o << indent(level) << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "")
              << s.a << " = spawn_" << snake_case(*sd) << "(__tracker.clone());\n";
            types[s.a] = *sd;
          } else {
            o << indent(level) << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "")
              << s.a << " = " << expr(s.b, d, locals) << ";\n";
            types[s.a] = "_value";
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Send: {
          const Domain* target = nullptr;
          if (s.a == "self" && d) target = d;
          else {
            auto type = types.find(s.a);
            if (type != types.end() && domains_.count(type->second)) target = domains_.at(type->second);
          }
          string recv = (s.a == "self") ? "self_ref" : expr(s.a, d, locals);
          const Handler* h = target ? find_handler(*target, s.b) : nullptr;
          string reply_tx, reply_rx;
          if (h && h->reply_type) {
            size_t id = reply_temp_++;
            reply_tx = "__moss_reply_tx_" + std::to_string(id);
            reply_rx = "__moss_reply_rx_" + std::to_string(id);
            o << indent(level) << "let (" << reply_tx << ", " << reply_rx
              << ") = mpsc::channel::<" << rust_type(*h->reply_type) << ">();\n";
          }
          o << indent(level) << recv << "." << s.b << "(";
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            string typ = h && k < h->params.size() ? h->params[k].type : "_";
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
        case Stmt::Kind::AwaitLet: {
          auto type = types.find(s.b);
          const Domain* target = type != types.end() && domains_.count(type->second)
              ? domains_.at(type->second) : nullptr;
          const Handler* h = target ? find_handler(*target, s.c) : nullptr;
          if (!target || !h || !h->reply_type)
            throw std::runtime_error("internal error: unchecked await reached code generation");
          size_t id = reply_temp_++;
          string reply_tx = "__moss_reply_tx_" + std::to_string(id);
          string reply_rx = "__moss_reply_rx_" + std::to_string(id);
          o << indent(level) << "let (" << reply_tx << ", " << reply_rx
            << ") = mpsc::channel::<" << rust_type(*h->reply_type) << ">();\n";
          string recv = expr(s.b, d, locals);
          o << indent(level) << recv << "." << s.c << "(";
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            o << message_arg(s.args[k], h->params[k].type, d, locals);
          }
          if (!s.args.empty()) o << ", ";
          o << reply_tx << ");\n";
          o << indent(level) << "let " << (s.is_mutable ? "mut " : "") << s.a
            << " = " << reply_rx << ".recv().unwrap_or_else(|_| {\n";
          o << indent(level + 1) << "panic!(\"Moss await failed: " << target->name << "."
            << h->name << " completed without a reply\")\n";
          o << indent(level) << "});\n";
          locals.insert(s.a);
          types[s.a] = *h->reply_type;
          ++i;
          break;
        }
        case Stmt::Kind::Reply:
          if (!current_handler || !current_handler->reply_type || reply_sender.empty())
            throw std::runtime_error("internal error: unchecked reply reached code generation");
          o << indent(level) << "let _ = " << reply_sender << ".send("
            << message_arg(s.a, *current_handler->reply_type, d, locals) << ");\n";
          o << indent(level) << "break 'handler;\n";
          ++i;
          break;
        case Stmt::Kind::Return:
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

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

};

} // namespace moss

static void usage() {
  std::cerr << "Moss v0.2 - actor/domain DSL to Rust with await/reply\n\n"
            << "Usage:\n"
            << "  moss <input.moss> [-o output.rs]\n"
            << "  moss --check <input.moss>\n\n"
            << "Request/reply:\n"
            << "  on Message(args...) -> Type\n"
            << "  reply value\n"
            << "  let value = await domain.Message(args...)\n";
}

int main(int argc, char** argv) {
  try {
    if (argc < 2) { usage(); return 2; }
    bool check_only = false;
    string input, output;
    for (int i = 1; i < argc; ++i) {
      string a = argv[i];
      if (a == "--check") check_only = true;
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

    if (check_only) {
      std::cout << input << ": ok\n";
      return 0;
    }

    moss::Generator gen(program);
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
