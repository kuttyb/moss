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
  enum class Kind { Raw, Send, Echo, If, Else, While, Let, Var, Return } kind = Kind::Raw;
  int line = 0;
  int indent = 0; // relative logical indent inside handler/main
  string text;
  string a, b; // generic payloads
  vector<string> args;
};

struct Handler {
  string name;
  vector<Param> params;
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
    if (lp == string::npos || rp == string::npos || rp < lp || rp != sig.size() - 1)
      fail(head, "handler must be 'on Name(args...)'");
    Handler h;
    h.name = trim(sig.substr(0, lp));
    h.params = parse_params(head, sig.substr(lp + 1, rp - lp - 1));
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

    static const vector<string> forbidden = {"await ", "async ", "yield ", "lock ", "shared ", "thread "};
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
    if (starts_with(L.text, "let ")) {
      s.kind = Stmt::Kind::Let;
      string rest = trim(L.text.substr(4));
      auto eq = rest.find('=');
      if (eq == string::npos) fail(L, "let requires an initializer");
      s.a = trim(rest.substr(0, eq));
      s.b = trim(rest.substr(eq + 1));
      return s;
    }
    if (starts_with(L.text, "var ")) {
      s.kind = Stmt::Kind::Var;
      string rest = trim(L.text.substr(4));
      auto eq = rest.find('=');
      if (eq == string::npos) fail(L, "local var requires an initializer in v0.1");
      s.a = trim(rest.substr(0, eq));
      s.b = trim(rest.substr(eq + 1));
      return s;
    }
    if (L.text == "return") { s.kind = Stmt::Kind::Return; return s; }
    if (starts_with(L.text, "return ")) fail(L, "message handlers cannot return values; send a response message instead");

    // Message send candidate: receiver.Message(args), as a standalone statement.
    auto dot = L.text.find('.');
    auto lp = L.text.find('(', dot == string::npos ? 0 : dot);
    auto rp = L.text.rfind(')');
    if (dot != string::npos && lp != string::npos && rp == L.text.size() - 1 && dot < lp) {
      string recv = trim(L.text.substr(0, dot));
      string meth = trim(L.text.substr(dot + 1, lp - dot - 1));
      bool simple_recv = !recv.empty() && std::all_of(recv.begin(), recv.end(), [](char c){ return std::isalnum((unsigned char)c) || c == '_'; });
      bool simple_meth = !meth.empty() && std::all_of(meth.begin(), meth.end(), [](char c){ return std::isalnum((unsigned char)c) || c == '_'; });
      if (simple_recv && simple_meth) {
        s.kind = Stmt::Kind::Send;
        s.a = recv;
        s.b = meth;
        string inside = L.text.substr(lp + 1, rp - lp - 1);
        if (!trim(inside).empty()) s.args = split_top_level(inside, ',');
        return s;
      }
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
      std::unordered_map<string,string> env;
      env["self"] = d.name;
      for (const auto& p : h.params) {
        if (!valid_type(p.type)) err(h.line, "unknown parameter type '" + p.type + "'");
        if (env.count(p.name)) err(h.line, "duplicate parameter '" + p.name + "'");
        env[p.name] = p.type;
      }
      for (const auto& f : d.state) env[f.name] = f.type;
      check_stmts(h.body, env, &d);
    }
  }

  void check_main(const MainProc& m) {
    std::unordered_map<string,string> env;
    check_stmts(m.body, env, nullptr);
  }

  static std::optional<string> spawn_domain(const string& expr) {
    string e = trim(expr);
    if (!starts_with(e, "spawn ") || !ends_with(e, "()")) return std::nullopt;
    string name = trim(e.substr(6, e.size() - 8));
    if (name.empty()) return std::nullopt;
    return name;
  }

  void check_stmts(const vector<Stmt>& ss, std::unordered_map<string,string> env, const Domain* current) {
    int prev_indent = 0;
    for (size_t i = 0; i < ss.size(); ++i) {
      const auto& s = ss[i];
      if (s.indent > prev_indent + 1) err(s.line, "indentation jumps more than one block level");
      prev_indent = s.indent;

      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var) {
        if (auto sd = spawn_domain(s.b)) {
          if (!domains_.count(*sd)) err(s.line, "unknown domain in spawn: " + *sd);
          if (current) err(s.line, "spawning domains inside handlers is not supported in v0.1; create them in main and pass ActorRefs in messages");
          env[s.a] = *sd;
        } else {
          env[s.a] = "_value";
          // Synchronous-looking cross-domain calls in expressions are forbidden.
          auto dot = s.b.find('.'), lp = s.b.find('(');
          if (dot != string::npos && lp != string::npos && dot < lp) {
            string recv = trim(s.b.substr(0, dot));
            auto it = env.find(recv);
            if (it != env.end() && domains_.count(it->second))
              err(s.line, "cross-domain messages have no return value; responses must be new messages");
          }
        }
      }

      if (s.kind == Stmt::Kind::Send) {
        auto it = env.find(s.a);
        if (it == env.end()) {
          // It may be a state field in current domain; state fields are already in env.
          err(s.line, "unknown message receiver '" + s.a + "'");
        }
        auto dit = domains_.find(it->second);
        if (dit == domains_.end()) {
          // A field access call like object.method() was parsed as Send. In v0.1 we only
          // permit message syntax on actor references. Use free/local expressions otherwise.
          err(s.line, "'" + s.a + "' is not an actor/domain reference; dotted standalone calls are reserved for message sends in v0.1");
        }
        const Handler* h = find_handler(*dit->second, s.b);
        if (!h) err(s.line, "domain " + dit->second->name + " has no message handler '" + s.b + "'");
        if (h->params.size() != s.args.size())
          err(s.line, "message " + dit->second->name + "." + s.b + " expects " + std::to_string(h->params.size()) + " arguments, got " + std::to_string(s.args.size()));
      }
    }
  }
};

class Generator {
 public:
  explicit Generator(const Program& p) : p_(p) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    for (const auto& o : p.objects) objects_[o.name] = &o;
    if (p.main) {
      for (const auto& s : p.main->body) {
        if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var) {
          if (auto sd = CheckerSpawn(s.b)) main_types_[s.a] = *sd;
        }
      }
    }
  }

  string generate() {
    std::ostringstream o;
    o << "// Generated by Moss v0.1. Do not edit by hand.\n";
    o << "#![allow(non_snake_case)]\n#![allow(dead_code)]\n\n";
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
  std::unordered_map<string, string> main_types_;

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
    // Moss messages have value semantics. v0.1 conservatively clones non-Copy payloads;
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

  void gen_domain(std::ostringstream& o, const Domain& d) {
    o << "struct " << d.name << "State {\n";
    for (const auto& f : d.state) o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    o << "}\n\n";

    o << "enum " << d.name << "Msg {\n";
    for (const auto& h : d.handlers) {
      o << "    " << h.name;
      if (!h.params.empty()) {
        o << "(";
        for (size_t i = 0; i < h.params.size(); ++i) {
          if (i) o << ", ";
          o << rust_type(h.params[i].type);
        }
        o << ")";
      }
      o << ",\n";
    }
    o << "}\n\n";

    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      o << "    fn " << h.name << "(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      o << ") {\n        self.tracker.begin();\n        if self.tx.send(" << d.name << "Msg::" << h.name;
      if (!h.params.empty()) {
        o << "(";
        for (size_t i = 0; i < h.params.size(); ++i) { if (i) o << ", "; o << h.params[i].name; }
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
      o << "                " << d.name << "Msg::" << h.name;
      if (!h.params.empty()) {
        o << "(";
        for (size_t i = 0; i < h.params.size(); ++i) { if (i) o << ", "; o << h.params[i].name; }
        o << ")";
      }
      o << " => {\n";
      o << "                    'handler: {\n";
      std::set<string> locals;
      for (const auto& p : h.params) locals.insert(p.name);
      locals.insert("self_ref");
      gen_stmts(o, h.body, &d, locals, 6, true);
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
    gen_stmts(o, m.body, nullptr, locals, 1, false);
    o << "    __tracker.wait_zero();\n";
    o << "}\n";
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 std::set<string>& locals, int base, bool in_handler) {
    size_t i = 0;
    gen_block(o, ss, i, 0, d, locals, base, in_handler);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, std::set<string>& locals, int base, bool in_handler) {
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
          gen_block(o, ss, i, level + 1, d, child_locals, base, in_handler);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            ++i;
            auto else_locals = locals;
            gen_block(o, ss, i, level + 1, d, else_locals, base, in_handler);
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
          gen_block(o, ss, i, level + 1, d, child_locals, base, in_handler);
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
          } else {
            o << indent(level) << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "")
              << s.a << " = " << expr(s.b, d, locals) << ";\n";
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Send: {
          const Domain* target = nullptr;
          string recvType;
          if (s.a == "self" && d) target = d;
          else {
            recvType = infer_receiver_type(s.a, d);
            if (!recvType.empty() && domains_.count(recvType)) target = domains_.at(recvType);
          }
          string recv = (s.a == "self") ? "self_ref" : expr(s.a, d, locals);
          o << indent(level) << recv << "." << s.b << "(";
          const Handler* h = target ? find_handler(*target, s.b) : nullptr;
          for (size_t k = 0; k < s.args.size(); ++k) {
            if (k) o << ", ";
            string typ = h && k < h->params.size() ? h->params[k].type : "_";
            o << message_arg(s.args[k], typ, d, locals);
          }
          o << ");\n";
          ++i;
          break;
        }
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

  string infer_receiver_type(const string& recv, const Domain* d) const {
    if (d) {
      for (const auto& f : d->state) if (f.name == recv && domains_.count(f.type)) return f.type;
      for (const auto& h : d->handlers) {
        // v0.1 uses parameter names to recover actor-ref types during lowering.
        for (const auto& p : h.params) if (p.name == recv && domains_.count(p.type)) return p.type;
      }
    } else {
      auto it = main_types_.find(recv);
      if (it != main_types_.end()) return it->second;
    }
    return "";
  }
};

} // namespace moss

static void usage() {
  std::cerr << "Moss v0.1 - actor/domain DSL to Rust\n\n"
            << "Usage:\n"
            << "  moss <input.moss> [-o output.rs]\n"
            << "  moss --check <input.moss>\n";
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
