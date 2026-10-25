#include <algorithm>
#include <cctype>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <istream>
#include "ast.hpp"
#include "compiler_stage_timer.hpp"
#include "diagnostics.hpp"
#include "numeric_literal.hpp"
#include "parser.hpp"
#include "parser_support.hpp"

using std::string;
using std::vector;

namespace moss {

class Parser {
 public:
  explicit Parser(vector<Line> lines,
                  std::set<string> standard_aliases = {})
      : lines_(std::move(lines)) {
    normalize_standard_error_lines(lines_, standard_aliases);
    int unit = 0;
    for (const auto& line : lines_) {
      if (line.indent > 0) unit = unit == 0 ? line.indent : std::gcd(unit, line.indent);
    }
    indent_unit_ = unit > 0 ? unit : 2;
    if (indent_unit_ < 2) indent_unit_ = 2;
  }

  Program parse() {
    Program p;
    p.source_lines = lines_;
    while (i_ < lines_.size()) {
      const auto& L = lines_[i_];
      if (L.indent != 0) fail(L, "top-level declaration must start at indentation 0");
      if (starts_with(L.text, "module ")) {
        string name = trim(L.text.substr(7));
        if (!plain_identifier(name)) fail(L, "module name must be an identifier");
        if (name == "std") {
          CompileError error(L.no, "module 'std' is reserved for compiler-defined standard types",
                             "RESERVED_STANDARD_NAMESPACE");
          error.source_file = L.source_file;
          throw error;
        }
        if (p.explicit_module && p.module_name != name)
          fail(L, "a source file may declare only one module");
        p.module_name = name;
        p.explicit_module = true;
        ++i_;
      } else if (starts_with(L.text, "import ")) {
        string name = trim(L.text.substr(7));
        if (!plain_identifier(name)) fail(L, "import name must be an identifier");
        // std is compiler-owned and needs no provider artifact.  Accept an
        // explicit import for discoverability without manufacturing a module.
        if (name != "std")
          p.imports.push_back(ModuleImport{name, {}, L.no, L.source_file});
        ++i_;
      } else {
        bool exported = false;
        string declaration = L.text;
        if (starts_with(declaration, "export ")) {
          exported = true;
          declaration = trim(declaration.substr(7));
          if (declaration.empty()) fail(L, "export requires a declaration");
        } else if (starts_with(declaration, "pub ")) {
          exported = true;
          declaration = trim(declaration.substr(4));
          if (declaration.empty()) fail(L, "pub requires a declaration");
        }
        if (starts_with(declaration, "domain ")) p.domains.push_back(parse_domain(exported));
        else if (starts_with(declaration, "type ")) p.objects.push_back(parse_object(exported));
        else if (starts_with(declaration, "enum ")) p.enums.push_back(parse_enum(exported));
        else if (starts_with(declaration, "trait ")) p.traits.push_back(parse_trait(exported));
        else if (starts_with(declaration, "fn ")) {
          auto function = parse_function(exported);
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
            p.main = MainProc{std::move(function.body), function.line,
                              function.header, function.source_file};
          } else {
            p.functions.push_back(std::move(function));
          }
        } else if (starts_with(declaration, "test ")) {
          if (exported) fail(L, "tests cannot be exported");
          p.tests.push_back(parse_test());
        } else if (starts_with(declaration, "bench ")) {
          if (exported) fail(L, "benchmarks cannot be exported");
          p.benchmarks.push_back(parse_benchmark());
        }
        else if (declaration == "proc main()" || declaration == "proc main():") {
          if (exported) fail(L, "main cannot be exported");
          if (p.main) fail(L, "duplicate proc main()");
          p.main = parse_main();
        } else {
          fail(L, "expected 'module', 'import', 'domain', 'type Name:', 'enum Name:', 'trait', 'fn', 'export', 'test', 'bench', or 'proc main()'");
        }
      }
    }
    return p;
  }

 private:
  vector<Line> lines_;
  size_t i_ = 0;
  int indent_unit_ = 2;

  [[noreturn]] void fail(const Line& L, const string& msg) {
    CompileError error(L.no, msg);
    error.source_file = L.source_file;
    throw error;
  }

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
        p.set_type(canonical_type_name(trim(part.substr(c + 1))));
      }
      if (p.name.empty() || !identifier(p.name)) throw CompileError(L.no, "invalid parameter: " + part);
      ps.push_back(std::move(p));
    }
    return ps;
  }

  ObjectType parse_object(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
    if (starts_with(head.text, "pub ")) head.text = trim(head.text.substr(4));
    string rest;
    if (starts_with(head.text, "type ")) rest = trim(head.text.substr(5));
    else rest = trim(head.text);
    auto eq = rest.find('=');
    bool legacy = eq != string::npos;
    if (legacy) {
      if (trim(rest.substr(eq + 1)) != "object")
        fail(head, "object declaration must be 'type Name:' or 'type Name = object'");
    } else if (!ends_with(rest, ":")) {
      fail(head, "object declaration must be 'type Name:'");
    }
    ObjectType o;
    o.header = head.text;
    o.name = legacy ? trim(rest.substr(0, eq)) : trim(rest.substr(0, rest.size() - 1));
    if (!identifier(o.name)) fail(head, "invalid type name '" + o.name + "'");
    o.line = head.no;
    o.exported = exported;
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
        if (!suffix.empty() && starts_with(suffix, "->")) {
          m.set_return_type(canonical_type_name(trim(suffix.substr(2))));
        }
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
      bool field_exported = exported;
      string line_text = L.text;
      if (starts_with(line_text, "pub ")) {
        field_exported = true;
        line_text = trim(line_text.substr(4));
      } else if (starts_with(line_text, "export ")) {
        field_exported = true;
        line_text = trim(line_text.substr(7));
      }
      auto c = line_text.find(':');
      Field f;
      f.header = L.text;
      f.exported = field_exported;
      f.source_file = L.source_file;
      if (c == string::npos) {
        f.name = trim(line_text);
        f.inferred = true;
      } else {
        f.name = trim(line_text.substr(0, c));
        f.type = canonical_type_name(trim(line_text.substr(c + 1)));
      }
      if (!identifier(f.name)) fail(L, "object field must be 'name' or 'name: type'");
      f.line = L.no;
      o.fields.push_back(std::move(f));
    }
    return o;
  }

  EnumType parse_enum(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
    if (starts_with(head.text, "pub ")) head.text = trim(head.text.substr(4));
    if (!ends_with(head.text, ":")) fail(head, "enum declaration must end with ':'");
    EnumType result;
    result.name = trim(head.text.substr(5, head.text.size() - 6));
    if (!identifier(result.name)) fail(head, "invalid enum name '" + result.name + "'");
    result.header = head.text;
    result.line = head.no;
    result.source_file = head.source_file;
    result.exported = exported;
    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      Line line = lines_[i_++];
      if (line.indent != head.indent + indent_unit_)
        fail(line, "enum cases must use one indentation level");
      if (starts_with(line.text, "fn "))
        fail(line, "enum methods are not supported; use an ordinary function and match on the enum");
      EnumCase item;
      item.line = line.no;
      item.source_file = line.source_file;
      auto lp = line.text.find('(');
      if (lp == string::npos) item.name = trim(line.text);
      else {
        auto rp = matching_paren(line.text, lp);
        if (rp == string::npos || rp + 1 != line.text.size())
          fail(line, "enum case fields must be named and typed");
        if (rp == lp + 1)
          fail(line, "tag-only enum cases are declared without parentheses");
        item.name = trim(line.text.substr(0, lp));
        for (const auto& param : parse_params(line, line.text.substr(lp + 1, rp - lp - 1))) {
          if (param.type.empty()) fail(line, "enum case field '" + param.name + "' requires a type");
          for (const auto& prior : item.fields)
            if (prior.name == param.name) fail(line, "duplicate enum case field '" + param.name + "'");
          Field field;
          field.name = param.name;
          field.type = param.type;
          field.line = line.no;
          field.source_file = line.source_file;
          item.fields.push_back(std::move(field));
        }
      }
      if (!identifier(item.name)) fail(line, "invalid enum case name '" + item.name + "'");
      for (const auto& prior : result.cases)
        if (prior.name == item.name) fail(line, "duplicate enum case '" + item.name + "'");
      result.cases.push_back(std::move(item));
    }
    if (result.cases.empty()) fail(head, "enum must declare at least one case");
    return result;
  }

  Trait parse_trait(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
    if (starts_with(head.text, "pub ")) head.text = trim(head.text.substr(4));
    string rest = trim(head.text.substr(6));
    if (ends_with(rest, ":")) rest.pop_back();
    Trait t; t.name = trim(rest); t.header = head.text; t.line = head.no; t.exported = exported;
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
        m.set_return_type(canonical_type_name(trim(suffix.substr(2))));
      }
      t.methods.push_back(std::move(m));
    }
    if (t.methods.empty()) fail(head, "trait must declare at least one method");
    return t;
  }

  Domain parse_domain(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
    if (starts_with(head.text, "pub ")) head.text = trim(head.text.substr(4));
    Domain d;
    d.header = head.text;
    d.name = trim(head.text.substr(7));
    if (ends_with(d.name, ":")) d.name = trim(d.name.substr(0, d.name.size() - 1));
    d.line = head.no;
    d.exported = exported;
    if (d.name.empty()) fail(head, "domain name is required");

    while (i_ < lines_.size() && lines_[i_].indent > head.indent) {
      const auto L = lines_[i_];
      if (L.indent != head.indent + indent_unit_)
        fail(L, "domain members must use one indentation level");
      if (starts_with(L.text, "var ")) {
        d.state.push_back(parse_state_field());
      } else if (starts_with(L.text, "domainroutes")) {
        parse_domain_routes(d, head.indent + indent_unit_);
      } else if (starts_with(L.text, "on ")) {
        d.handlers.push_back(parse_handler(head.indent + indent_unit_, "on"));
      } else if (starts_with(L.text, "fn ")) {
        d.handlers.push_back(parse_handler(head.indent + indent_unit_, "fn"));
      } else if (L.text == "on_fail" || L.text == "on_fail:" ||
                 starts_with(L.text, "on_fail ")) {
        fail(L, "on_fail must immediately follow the handler it attaches to");
      } else {
        // Julia-like state declarations omit both the var keyword and, when
        // inferable, the type annotation: `value = 0`.
        d.state.push_back(parse_state_field());
      }
    }
    return d;
  }

  void parse_domain_routes(Domain& domain, int member_indent) {
    Line head = lines_[i_++];
    string rest = trim(head.text.substr(std::string("domainroutes").size()));
    if (rest.empty()) rest = "(";
    if (rest.front() != '(')
      fail(head, "domainroutes must be a declaration such as 'domainroutes(worker: Worker)'");
    auto add_route = [&](const Line& line, string text) {
      text = trim(std::move(text));
      if (text.empty() || text == ",") return;
      if (text.back() == ',') text.pop_back();
      auto colon = top_level_colon(text);
      if (colon == string::npos)
        fail(line, "domain route must be 'name: DomainType'");
      DomainRoute route;
      route.name = trim(text.substr(0, colon));
      route.type = canonical_type_name(trim(text.substr(colon + 1)));
      route.line = line.no;
      route.source_file = line.source_file;
      if (!plain_identifier(route.name) || route.type.empty())
        fail(line, "domain route must be 'name: DomainType'");
      domain.routes.push_back(std::move(route));
    };
    string inline_body = rest.substr(1);
    if (!inline_body.empty()) {
      if (inline_body.back() != ')')
        fail(head, "domainroutes declaration must close with ')'");
      inline_body.pop_back();
      for (const auto& item : split_top_level(inline_body, ',')) add_route(head, item);
      return;
    }
    bool closed = false;
    while (i_ < lines_.size() &&
           (lines_[i_].indent > head.indent || trim(lines_[i_].text) == ")")) {
      Line line = lines_[i_++];
      if (trim(line.text) == ")") { closed = true; break; }
      if (line.indent != member_indent + indent_unit_ && line.indent != member_indent)
        fail(line, "domain route declarations must use one indentation level");
      add_route(line, line.text);
    }
    if (!closed) fail(head, "domainroutes declaration must close with ')'");
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
      h.set_reply_type(canonical_type_name(std::move(type)));
    }
    h.line = head.no;
    h.body = parse_stmt_block(member_indent + indent_unit_);
    if (h.body.empty()) fail(head, "handler body may not be empty");
    while (i_ < lines_.size() && lines_[i_].indent == member_indent &&
           (lines_[i_].text == "on_fail" ||
            lines_[i_].text == "on_fail:" ||
            starts_with(lines_[i_].text, "on_fail "))) {
      Line arm_head = lines_[i_++];
      if (!ends_with(arm_head.text, ":"))
        fail(arm_head, "on_fail arm must end with ':'");
      string selector = trim(arm_head.text.substr(
          std::string("on_fail").size(),
          arm_head.text.size() - std::string("on_fail").size() - 1));
      FailureArm arm;
      arm.line = arm_head.no;
      arm.source_file = arm_head.source_file;
      if (!selector.empty()) {
        auto alternatives = split_top_level(selector, ',');
        bool all_qualified = std::all_of(
            alternatives.begin(), alternatives.end(), [](const string& value) {
              return trim(value).find('.') != string::npos;
            });
        if (all_qualified) arm.patterns = std::move(alternatives);
        else if (alternatives.size() == 1 && plain_identifier(selector))
          arm.binding = selector;
        else
          fail(arm_head,
               "on_fail requires qualified error variants, a single error binding, or a catch-all");
      }
      arm.body = parse_stmt_block(member_indent + indent_unit_);
      if (arm.body.empty()) fail(arm_head, "on_fail arm body may not be empty");
      h.failure_arms.push_back(std::move(arm));
    }
    return h;
  }

  Function parse_function(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
    if (starts_with(head.text, "pub ")) head.text = trim(head.text.substr(4));
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
      f.set_return_type(canonical_type_name(type));
      suffix = trim(suffix.substr(end));
    }
    f.line = head.no;
    f.exported = exported;
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
    m.source_file = head.source_file;
    m.body = parse_stmt_block(indent_unit_);
    if (m.body.empty()) fail(head, "main body may not be empty");
    return m;
  }

  string parse_named_block_name(const Line& head, const string& keyword) {
    string rest = trim(head.text.substr(keyword.size()));
    if (!ends_with(rest, ":"))
      fail(head, keyword + " declaration must end with ':'");
    rest = trim(rest.substr(0, rest.size() - 1));
    if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"')
      fail(head, keyword + " declaration requires a quoted name");
    string name = rest.substr(1, rest.size() - 2);
    if (name.empty()) fail(head, keyword + " name may not be empty");
    if (name.find('"') != string::npos || name.find('\t') != string::npos)
      fail(head, keyword + " name contains an unsupported character");
    return name;
  }

  TestDecl parse_test() {
    Line head = lines_[i_++];
    TestDecl test;
    test.name = parse_named_block_name(head, "test");
    test.header = head.text;
    test.line = head.no;
    test.semantic_identity = "test:" + test.name + "@" +
        std::to_string(test.line);
    test.body = parse_stmt_block(head.indent + indent_unit_);
    if (test.body.empty()) fail(head, "test body may not be empty");
    return test;
  }

  BenchDecl parse_benchmark() {
    Line head = lines_[i_++];
    BenchDecl benchmark;
    benchmark.name = parse_named_block_name(head, "bench");
    benchmark.header = head.text;
    benchmark.line = head.no;
    benchmark.semantic_identity = "bench:" + benchmark.name + "@" +
        std::to_string(benchmark.line);
    benchmark.body = parse_stmt_block(head.indent + indent_unit_);
    if (benchmark.body.empty()) fail(head, "benchmark body may not be empty");
    return benchmark;
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
    s.source_file = L.source_file;

    static const vector<string> forbidden = {"async ", "yield ", "lock ", "shared ", "thread "};
    for (const auto& k : forbidden) if (starts_with(L.text, k)) fail(L, "'" + trim(k) + "' is not part of Moss's concurrency model");
    if (L.text == "pass") { s.kind = Stmt::Kind::Pass; return s; }

    if (L.text == "try" || L.text == "try:") {
      if (L.text != "try:") fail(L, "try statement must end with ':'");
      s.kind = Stmt::Kind::Try;
      return s;
    }
    if (L.text == "recover" || L.text == "recover:" ||
        starts_with(L.text, "recover ")) {
      if (!ends_with(L.text, ":")) fail(L, "recover arm must end with ':'");
      s.kind = Stmt::Kind::Recover;
      s.a = trim(L.text.substr(std::string("recover").size(),
                               L.text.size() - std::string("recover").size() - 1));
      return s;
    }
    if (L.text == "raise") {
      s.kind = Stmt::Kind::Raise;
      return s;
    }
    if (starts_with(L.text, "raise ")) {
      s.kind = Stmt::Kind::Raise;
      s.a = trim(L.text.substr(6));
      if (s.a.empty()) fail(L, "raise requires an error value");
      return s;
    }

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
    if (starts_with(L.text, "match ")) {
      if (!ends_with(L.text, ":")) fail(L, "match statement must end with ':'");
      s.a = trim(L.text.substr(6, L.text.size() - 7));
      if (starts_with(s.a, "consume ")) {
        s.a = trim(s.a.substr(8));
        s.b = "consume";
      }
      if (s.a.empty()) fail(L, "match requires an enum scrutinee");
      s.kind = Stmt::Kind::Match;
      return s;
    }
    if (starts_with(L.text, "case ")) {
      if (!ends_with(L.text, ":")) fail(L, "case statement must end with ':'; guards are unsupported");
      string pattern = trim(L.text.substr(5, L.text.size() - 6));
      if (pattern == "_")
        fail(L, "wildcard enum patterns are unsupported; list every case explicitly");
      auto lp = pattern.find('(');
      if (lp == string::npos) s.a = pattern;
      else {
        auto rp = matching_paren(pattern, lp);
        if (rp == string::npos || rp + 1 != pattern.size())
          fail(L, "enum patterns support only a case and plain field bindings");
        if (rp == lp + 1)
          fail(L, "tag-only enum patterns are written without parentheses");
        s.a = trim(pattern.substr(0, lp));
        string inside = pattern.substr(lp + 1, rp - lp - 1);
        if (!trim(inside).empty()) for (auto binding : split_top_level(inside, ',')) {
          binding = trim(binding);
          if (binding == "_")
            fail(L, "wildcard enum patterns are not supported");
          if (!identifier(binding)) {
            if (starts_with(binding, "move ") || starts_with(binding, "ref "))
              fail(L, "ownership modifiers are not allowed in enum patterns; use 'match value:' to READ-borrow or 'match consume value:' to consume the whole enum");
            fail(L, "enum patterns support only plain field bindings; wildcard and nested patterns are unsupported");
          }
          s.args.push_back(binding);
        }
      }
      if (!identifier(s.a))
        fail(L, "enum patterns require an unqualified case name; wildcard and guards are unsupported");
      s.kind = Stmt::Kind::Case;
      return s;
    }
    if (L.text == "else" || L.text == "else:") { s.kind = Stmt::Kind::Else; return s; }
    if (starts_with(L.text, "while ")) {
      s.kind = Stmt::Kind::While;
      s.a = trim(L.text.substr(6));
      if (ends_with(s.a, ":")) s.a = trim(s.a.substr(0, s.a.size() - 1));
      return s;
    }
    if (starts_with(L.text, "for ")) {
      string rest = trim(L.text.substr(4));
      if (ends_with(rest, ":")) rest = trim(rest.substr(0, rest.size() - 1));
      auto in = rest.find(" in ");
      if (in == string::npos)
        fail(L, "for loop must be 'for binding in expression:'");
      s.a = trim(rest.substr(0, in));
      s.b = trim(rest.substr(in + 4));
      if (!plain_identifier(s.a)) fail(L, "for loop binding must be an identifier");
      if (s.b.empty()) fail(L, "for loop source may not be empty");
      s.kind = Stmt::Kind::For;
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
      if (eq == string::npos) fail(L, is_var ? "local var requires an initializer" : "let requires an initializer");
      s.a = trim(rest.substr(0, eq));
      s.b = trim(rest.substr(eq + 1));
      if (s.a.find(':') != string::npos) {
        throw CompileError(L.no,
            "local type annotations are not supported in this form; use inferred local syntax such as 'var values = Vector[Int]()'",
            "LOCAL_TYPE_ANNOTATION_UNSUPPORTED");
      }
      if (starts_with(s.b, "await ")) {
        fail(L, "await is retired: message is synchronous; use '" + s.a +
             " = message receiver.Handler(...)'");
      } else if (starts_with(s.b, "message ")) {
        string call = trim(s.b.substr(8));
        string receiver, handler;
        if (!parse_message_call(call, receiver, handler, s.args))
          fail(L, "message requires a domain call 'receiver.Handler(args)'");
        s.kind = Stmt::Kind::Message;
        s.message_result = s.a;
        s.a = std::move(receiver);
        s.b = std::move(handler);
      } else if (contains_word_outside_string(s.b, "await")) {
        fail(L, "await is retired: message is synchronous");
      }
      return s;
    }
    auto assignment = top_level_assignment(L.text);
    if (assignment != string::npos) {
      s.a = trim(L.text.substr(0, assignment));
      s.b = trim(L.text.substr(assignment + 1));
      if (s.a.empty() || s.b.empty()) fail(L, "assignment requires a target and an expression");
      if (starts_with(s.b, "await ")) {
        fail(L, "await is retired: message is synchronous; use '" + s.a +
             " = message receiver.Handler(...)'");
      } else if (starts_with(s.b, "message ")) {
        string call = trim(s.b.substr(8));
        string receiver, handler;
        if (!parse_message_call(call, receiver, handler, s.args))
          fail(L, "message requires a domain call 'receiver.Handler(args)'");
        s.kind = Stmt::Kind::Message;
        s.message_result = s.a;
        s.a = std::move(receiver);
        s.b = std::move(handler);
      } else if (contains_word_outside_string(s.b, "await")) {
        fail(L, "await is retired: message is synchronous");
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
      fail(L, "await is retired: message is synchronous; use 'message receiver.Handler(...)'");

    if (contains_word_outside_string(L.text, "await"))
      fail(L, "await is retired: message is synchronous");

    // A naked dotted call is kept distinct so the checker can reject it for a domain
    // receiver while allowing future local member-call syntax.
    if (parse_member_call(L.text, s.a, s.b, s.args) && identifier(s.b)) {
      s.kind = Stmt::Kind::Call;
      return s;
    }
    string callee;
    if (parse_simple_call(L.text, callee, s.args)) {
      s.kind = Stmt::Kind::Call;
      s.a = callee;
      return s;
    }

    for (const string& assertion : {string("assert"), string("assertEqual")}) {
      if (L.text == assertion ||
          (starts_with(L.text, assertion) && L.text.size() > assertion.size() &&
           std::isspace(static_cast<unsigned char>(L.text[assertion.size()]))))
        throw CompileError(L.no,
            assertion + " uses call syntax; write " + assertion + "(...)",
            "INVALID_ASSERT_SYNTAX");
    }
    s.kind = Stmt::Kind::Raw;
    return s;
  }
};

// Moss expressions are scanned textually, so a Char literal holding a
// delimiter ('"', ',', '(', '#', '+', ...) would be read as syntax. Rewrite
// every Char literal other than an ASCII letter/digit/underscore to the
// equivalent '\u{HEX}' spelling before any other scanning. Malformed literals
// are left untouched for the checker to diagnose.
// Parses one Rust escape starting at line[at] == '\\'. Native lowering hands
// String and Char literals to Rust unchanged, so the accepted set is Rust's:
// \n \r \t \0 \\ \' \" \xHH (at most 0x7F) and \u{1-6 hex} naming a Unicode
// scalar. Returns the index just past the escape, or npos when invalid.
size_t parse_rust_escape(const string& line, size_t at, uint32_t& codepoint) {
  if (at + 1 >= line.size()) return string::npos;
  const char e = line[at + 1];
  const string simple = "nrt0'\\\"";
  const uint32_t values[] = {'\n', '\r', '\t', 0, '\'', '\\', '"'};
  if (size_t which = simple.find(e); which != string::npos) {
    codepoint = values[which];
    return at + 2;
  }
  if (e == 'x') {
    if (at + 3 >= line.size() || !std::isxdigit(static_cast<unsigned char>(line[at + 2])) ||
        !std::isxdigit(static_cast<unsigned char>(line[at + 3])))
      return string::npos;
    codepoint = static_cast<uint32_t>(std::stoul(line.substr(at + 2, 2), nullptr, 16));
    return codepoint <= 0x7F ? at + 4 : string::npos;
  }
  if (e == 'u' && at + 2 < line.size() && line[at + 2] == '{') {
    size_t close = line.find('}', at + 3);
    if (close == string::npos || close == at + 3 || close - (at + 3) > 6) return string::npos;
    for (size_t k = at + 3; k < close; ++k)
      if (!std::isxdigit(static_cast<unsigned char>(line[k]))) return string::npos;
    codepoint = static_cast<uint32_t>(std::stoul(line.substr(at + 3, close - (at + 3)), nullptr, 16));
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return string::npos;
    return close + 1;
  }
  return string::npos;
}

static string canonicalize_char_literals(const string& line, int line_no,
                                         const string& source_file) {
  const auto invalid = [&](size_t at) {
    size_t end = std::min(line.size(), at + 10);
    CompileError error(line_no, "invalid escape '" + line.substr(at, end - at) +
                       "'; use \\n \\r \\t \\0 \\\\ \\' \\\" \\xHH (ASCII) or \\u{HEX} (a Unicode scalar)",
                       "INVALID_ESCAPE");
    error.source_file = source_file;
    throw error;
  };
  string out;
  out.reserve(line.size());
  bool in_string = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (in_string) {
      if (c == '\\') {
        uint32_t ignored = 0;
        size_t next = parse_rust_escape(line, i, ignored);
        if (next == string::npos) invalid(i);
        out += line.substr(i, next - i);
        i = next - 1;
        continue;
      }
      out.push_back(c);
      if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') { in_string = true; out.push_back(c); continue; }
    if (c == '#') {  // a trailing comment is not code; lex_lines strips it
      out += line.substr(i);
      break;
    }
    if (numeric_literal_starts_at(line, i)) {
      // Every numeric literal is validated and decoded by the shared
      // specification in numeric_literal.hpp and rewritten to its canonical
      // decimal spelling, so the checker, Fast Debug, and native lowering all
      // see one decoded value. Only the negated magnitude 2^63 (Int minimum)
      // may exceed Int maximum.
      NumericLiteral literal = scan_numeric_literal(line, i);
      if (!literal.ok) {
        CompileError error(line_no, literal.message, literal.code);
        error.source_file = source_file;
        throw error;
      }
      const size_t end = literal.end;
      if (!int_literal_in_range(literal, false)) {
        bool negated = false;
        if (!literal.magnitude_overflow && literal.magnitude == kNumericIntMinMagnitude) {
          size_t k = i;
          while (k > 0 && line[k - 1] == ' ') --k;
          if (k > 0 && line[k - 1] == '-') {
            size_t before = k - 1;
            while (before > 0 && line[before - 1] == ' ') --before;
            if (before == 0 ||
                string("([{,=:+-*/%<>!&|^~").find(line[before - 1]) != string::npos) {
              negated = true;
            } else if (std::isalpha(static_cast<unsigned char>(line[before - 1]))) {
              size_t word = before;
              while (word > 0 && (std::isalnum(static_cast<unsigned char>(line[word - 1])) ||
                                  line[word - 1] == '_'))
                --word;
              static const std::set<string> keywords = {
                  "echo", "return", "reply", "and", "or", "xor", "not", "in",
                  "if", "while", "raise"};
              negated = keywords.count(line.substr(word, before - word)) != 0;
            }
          }
        }
        if (!negated) {
          CompileError error(line_no, "integer literal '" + line.substr(i, end - i) +
                             "' does not fit in Int (-9223372036854775808 through "
                             "9223372036854775807)", "INT_LITERAL_OUT_OF_RANGE");
          error.source_file = source_file;
          throw error;
        }
      }
      out += literal.canonical;
      i = end - 1;
      continue;
    }
    if (c != '\'') { out.push_back(c); continue; }
    uint32_t codepoint = 0;
    size_t end = string::npos;  // index of the closing quote
    if (i + 1 < line.size() && line[i + 1] == '\\') {
      size_t next = parse_rust_escape(line, i + 1, codepoint);
      if (next == string::npos) invalid(i + 1);
      if (next < line.size() && line[next] == '\'') end = next;
    } else if (i + 1 < line.size()) {
      const unsigned char b0 = static_cast<unsigned char>(line[i + 1]);
      size_t width = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3
                   : (b0 & 0xF8) == 0xF0 ? 4 : 0;
      if (width && b0 != '\'' && i + 1 + width < line.size() && line[i + 1 + width] == '\'') {
        codepoint = width == 1 ? b0 : b0 & (0x7F >> width);
        for (size_t k = 1; k < width; ++k)
          codepoint = (codepoint << 6) | (static_cast<unsigned char>(line[i + 1 + k]) & 0x3F);
        end = i + 1 + width;
      }
    }
    if (end == string::npos) { out.push_back(c); continue; }
    if (line[i + 1] != '\\' && codepoint < 0x80 &&
        (std::isalnum(static_cast<int>(codepoint)) || codepoint == '_')) {
      out += line.substr(i, end - i + 1);
    } else {
      char buffer[16];
      std::snprintf(buffer, sizeof buffer, "'\\u{%X}'", codepoint);
      out += buffer;
    }
    i = end;
  }
  return out;
}

static vector<Line> lex_lines(std::istream& in, const string& source_file = {}) {
  vector<Line> out;
  string raw;
  int no = 0;
  while (std::getline(in, raw)) {
    ++no;
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    size_t first = raw.find_first_not_of(' ');
    if (first == string::npos) continue;
    if (raw[first] == '#') continue;
    if (raw.find('\t') != string::npos) {
      CompileError error(no, "tabs are not allowed; use space indentation");
      error.source_file = source_file;
      throw error;
    }

    raw = canonicalize_char_literals(raw, no, source_file);
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
    out.push_back(Line{no, indent, std::move(text), {}, source_file});
  }
  return out;
}


Program parse_source_lines(vector<Line> lines,
                           const std::set<string>& standard_aliases) {
  return Parser(std::move(lines), standard_aliases).parse();
}

Program parse_source(std::istream& input, const string& source_file,
                     const std::set<string>& standard_aliases) {
  CompilerStageTimer timer("parsing");
  return parse_source_lines(lex_lines(input, source_file), standard_aliases);
}

}  // namespace moss
