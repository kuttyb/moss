#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ast.hpp"
#include "debug_map.hpp"
#include "diagnostics.hpp"
#include "interpreter.hpp"
#include "handler_runtime.hpp"
#include "synchronization_lowering.hpp"

using std::string;
using std::vector;

namespace moss {

// Opt-in developer timing, kept out of semantic state and deterministic JSON.
// Durations include each named stage's ordinary checks; no timing gate exists.
class CompilerStageTimer {
  const char* stage_;
  bool enabled_ = std::getenv("MOSS_PROFILE_COMPILER") != nullptr;
  std::chrono::steady_clock::time_point begin_;
 public:
  explicit CompilerStageTimer(const char* stage) : stage_(stage) {
    if (enabled_) begin_ = std::chrono::steady_clock::now();
  }
  ~CompilerStageTimer() {
    if (enabled_) std::cerr << "MOSS_PROFILE|" << stage_ << '|' <<
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin_).count() << '\n';
  }
};

static constexpr const char* kCompilerVersion = "0.1.0";
static constexpr const char* kMossLanguageVersion = "moss-0.1";
static constexpr int kAgentProtocolVersion = 1;
static constexpr const char* kAgentSchemaVersion = "moss-agent-1";

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

static string stable_hash(const string& value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

static string tooling_name(string value) {
  for (char& c : value) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
  }
  while (!value.empty() && value.back() == '_') value.pop_back();
  return value.empty() ? "anonymous" : value;
}

static string tooling_native_symbol(const string& kind, const string& name,
                                    const string& semantic_identity) {
  return "moss__" + tooling_name(kind) + "__" + tooling_name(name) + "__" +
      stable_hash(semantic_identity);
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
  auto bracket = type.find('[');
  if (bracket != string::npos && ends_with(type, "]")) {
    string head = canonical_type_name(type.substr(0, bracket));
    auto arguments = split_top_level(type.substr(bracket + 1,
                                                  type.size() - bracket - 2), ',');
    for (auto& argument : arguments) argument = canonical_type_name(argument);
    std::ostringstream normalized;
    normalized << head << "[";
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (index) normalized << ",";
      normalized << arguments[index];
    }
    normalized << "]";
    return normalized.str();
  }
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
  // A member call inside an argument is not the outer call's receiver.
  // In particular f(message route.Run()) must check route as a message
  // target, not as the ordinary value "f(message route".
  size_t dot = string::npos;
  int nesting = 0;
  bool quoted = false, escaped = false;
  for (size_t index = 0; index < value.size(); ++index) {
    char c = value[index];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') { quoted = true; continue; }
    if (c == '(' || c == '[') ++nesting;
    else if (c == ')' || c == ']') --nesting;
    else if (c == '.' && nesting == 0) { dot = index; break; }
  }
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

// The code generator receives this post-rewrite form from the exact
// FunctionalPipeline plan. A composed step owns several original operations;
// this preserves callable/type/provenance identities while making the
// Moss-level composition explicit before loop lowering.
struct LoweredFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  vector<ParsedFunctionalStage> operations;
};

struct LoweredFunctionalPipeline {
  string source;
  vector<LoweredFunctionalStage> stages;
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
      if (starts_with(L.text, "module ")) {
        string name = trim(L.text.substr(7));
        if (!plain_identifier(name)) fail(L, "module name must be an identifier");
        if (p.explicit_module && p.module_name != name)
          fail(L, "a source file may declare only one module");
        p.module_name = name;
        p.explicit_module = true;
        ++i_;
      } else if (starts_with(L.text, "import ")) {
        string name = trim(L.text.substr(7));
        if (!plain_identifier(name)) fail(L, "import name must be an identifier");
        p.imports.push_back(ModuleImport{name, {}, L.no, L.source_file});
        ++i_;
      } else {
        bool exported = false;
        string declaration = L.text;
        if (starts_with(declaration, "export ")) {
          exported = true;
          declaration = trim(declaration.substr(7));
          if (declaration.empty()) fail(L, "export requires a declaration");
        }
        if (starts_with(declaration, "domain ")) p.domains.push_back(parse_domain(exported));
        else if (starts_with(declaration, "type ")) p.objects.push_back(parse_object(exported));
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
          fail(L, "expected 'module', 'import', 'domain', 'type Name:', 'trait', 'fn', 'export', 'test', 'bench', or 'proc main()'");
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
        p.type = canonical_type_name(trim(part.substr(c + 1)));
      }
      if (p.name.empty() || !identifier(p.name)) throw CompileError(L.no, "invalid parameter: " + part);
      ps.push_back(std::move(p));
    }
    return ps;
  }

  ObjectType parse_object(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
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

  Trait parse_trait(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
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
        m.return_type = canonical_type_name(trim(suffix.substr(2)));
      }
      t.methods.push_back(std::move(m));
    }
    if (t.methods.empty()) fail(head, "trait must declare at least one method");
    return t;
  }

  Domain parse_domain(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
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
      h.reply_type = canonical_type_name(std::move(type));
    }
    h.line = head.no;
    h.body = parse_stmt_block(member_indent + indent_unit_);
    if (h.body.empty()) fail(head, "handler body may not be empty");
    return h;
  }

  Function parse_function(bool exported = false) {
    Line head = lines_[i_++];
    if (starts_with(head.text, "export ")) head.text = trim(head.text.substr(7));
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
      if (f.name == "assert" || f.name == "assertEqual")
        err(f.source_file, f.line, "function name '" + f.name +
                        "' is reserved by Moss test assertions");
      if (!functions_.emplace(f.name, &f).second)
        err(f.source_file, f.line, "duplicate function: " + f.name);
    }
    for (auto& d : p_.domains) {
      if (!domains_.emplace(d.name, &d).second)
        err(d.source_file, d.line, "duplicate domain: " + d.name);
    }
    for (auto& o : p_.objects) {
      if (!objects_.emplace(o.name, &o).second)
        err(o.source_file, o.line, "duplicate object type: " + o.name);
    }
    for (auto& t : p_.traits) {
      if (!traits_.emplace(t.name, &t).second)
        err(t.source_file, t.line, "duplicate trait: " + t.name);
    }
    iterator_trait_.name = "Iterator";
    iterator_trait_.header = "trait Iterator";
    TraitMethod next;
    next.name = "next";
    next.return_type = "option[_]";
    iterator_trait_.methods.push_back(std::move(next));
    if (!traits_.emplace(iterator_trait_.name, &iterator_trait_).second)
      err(p_.traits.front().source_file, p_.traits.front().line,
          "trait name 'Iterator' is reserved by the standard iteration contract");
  }

  void run() {
    check_domain_handle_declarations();
    // Seed internal structural/generic parameter relations before cross-reference inference.
    infer_function_signatures(false);
    infer_object_fields();
    check_traits();
    check_objects();
    // Function results and handler replies can constrain each other through an
    // synchronous message or local call. Run non-final inference rounds before the
    // final unresolved-type diagnostics.
    for (size_t round = 0; round < 3; ++round) {
      infer_domain_state_fields(false);
      infer_function_signatures(false);
      infer_handler_reply_types(false);
    }
    infer_domain_specializations();
    infer_domain_state_fields(true);
    infer_handler_reply_types(true);
    infer_function_signatures(true);
    check_domain_handle_declarations();
    check_objects();
    check_local_call_cycles();
    { CompilerStageTimer timer("effects"); infer_effects(); infer_observable_effects(); }
    { CompilerStageTimer timer("concrete_graph"); build_concrete_domain_graph(); }
    check_method_ownership();
    for (const auto& f : p_.functions) check_function(f);
    for (const auto& d : p_.domains) check_domain(d);
    if (p_.main) check_main(*p_.main);
    check_test_and_benchmark_declarations();
    p_.concrete_domain_graph.closed = true;
    std::ostringstream graph_material;
    for (const auto& instance : p_.concrete_domain_graph.instances)
      graph_material << std::quoted(instance.identity) << std::quoted(instance.specialization)
                     << instance.domain_rank;
    for (const auto& edge : p_.concrete_domain_graph.edges)
      graph_material << std::quoted(edge.source_instance) << std::quoted(edge.route)
                     << std::quoted(edge.target_instance);
    p_.concrete_domain_graph.identity = "domain-graph:" + stable_hash(graph_material.str());
    build_functional_ir();
    { CompilerStageTimer timer("synchronization_plan"); build_synchronization_plan(p_.concrete_domain_graph); }
  }

  const vector<Warning>& warnings() const { return warnings_; }

 private:
  Program& p_;
  std::unordered_map<string, Function*> functions_;
  std::unordered_map<string, Domain*> domains_;
  std::unordered_map<string, ObjectType*> objects_;
  std::unordered_map<string, Trait*> traits_;
  Trait iterator_trait_;
  const ObjectType* current_object_ = nullptr;
  vector<Warning> warnings_;

  using TypeEnv = std::unordered_map<string,string>;
  using TypeEnvVisitor = std::function<void(const Stmt&, const TypeEnv&)>;

  [[noreturn]] void err(int line, const string& msg) const {
    throw CompileError(line, msg);
  }

  [[noreturn]] void err(const string& source_file, int line,
                        const string& msg) const {
    CompileError error(line, msg);
    error.source_file = source_file;
    throw error;
  }

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
  static bool option_none_compatible(const string& actual, const string& expected) {
    return canonical_type_name(actual) == "_none" &&
        starts_with(canonical_type_name(expected), "option[");
  }
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
      if (method.return_type && *method.return_type == "option[_]" &&
          found->return_type && starts_with(canonical_type_name(*found->return_type), "option[")) {
        // The standard Iterator contract leaves the element type open.  The
        // concrete next() result supplies it at the use site.
      } else if (method.return_type &&
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
              else if (!option_none_compatible(*result, *method.return_type) &&
                       !same_type(*method.return_type, *result))
                err(s.line, "method '" + object.name + "." + method.name + "' returns '" + *result +
                    "' but another return path has type '" + *method.return_type + "'");
            }
          }
        }
        if (method.result_expression) {
          auto result = inferred_expr_type(*method.result_expression, inferred_env);
          if (result) {
            if (!method.return_type) method.return_type = *result;
            else if (!option_none_compatible(*result, *method.return_type) &&
                     !same_type(*method.return_type, *result))
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

  bool every_handler_path_replies(const vector<Stmt>& statements,
                                  size_t& index, int level) const {
    while (index < statements.size()) {
      const Stmt& statement = statements[index];
      if (statement.indent < level) return false;
      if (statement.indent > level) {
        ++index;
        continue;
      }
      if (statement.kind == Stmt::Kind::Else) return false;
      if (statement.kind == Stmt::Kind::Reply) {
        ++index;
        return true;
      }
      if (statement.kind == Stmt::Kind::If) {
        ++index;
        bool then_replies = every_handler_path_replies(statements, index, level + 1);
        bool else_replies = false;
        if (index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          else_replies = every_handler_path_replies(statements, index, level + 1);
        }
        if (then_replies && else_replies) return true;
        continue;
      }
      if (statement.kind == Stmt::Kind::While || statement.kind == Stmt::Kind::For) {
        ++index;
        (void)every_handler_path_replies(statements, index, level + 1);
        continue;
      }
      ++index;
    }
    return false;
  }

  // Domain handles are a compiler category, not ordinary structural values.
  // Type applications are traversed structurally, including nested containers.
  bool contains_domain_handle(const string& type) const {
    string value = canonical_type_name(type);
    if (domains_.count(value)) return true;
    auto bracket = value.find('[');
    if (bracket != string::npos && value.back() == ']')
      for (const auto& argument : split_top_level(
               value.substr(bracket + 1, value.size() - bracket - 2), ','))
        if (contains_domain_handle(trim(argument))) return true;
    return false;
  }

  void check_domain_handle_declarations() {
    auto reject = [&](const string& type, const string& file, int line,
                      const string& role) {
      if (contains_domain_handle(type))
        err(file, line, "domain handles cannot be " + role +
            "; declare an immutable domainroutes dependency instead");
    };
    for (const auto& domain : p_.domains) {
      for (const auto& field : domain.state)
        reject(field.type, domain.source_file, field.line, "stored as ordinary state");
      for (const auto& handler : domain.handlers) {
        for (const auto& parameter : handler.params)
          reject(parameter.type, handler.source_file, handler.line, "passed as handler payloads");
        if (handler.reply_type)
          reject(*handler.reply_type, handler.source_file, handler.line, "returned through reply");
      }
    }
    for (const auto& function : p_.functions) {
      for (const auto& parameter : function.params)
        reject(parameter.type, function.source_file, function.line, "passed as ordinary function parameters");
      if (function.return_type)
        reject(*function.return_type, function.source_file, function.line, "returned as ordinary values");
    }
    for (const auto& object : p_.objects) {
      for (const auto& field : object.fields)
        reject(field.type, object.source_file, field.line, "stored in aggregates");
      for (const auto& method : object.methods) {
        for (const auto& parameter : method.params)
          reject(parameter.type, method.source_file, method.line, "passed as ordinary method parameters");
        if (method.return_type)
          reject(*method.return_type, method.source_file, method.line, "returned as ordinary values");
      }
    }
    for (const auto& trait : p_.traits)
      for (const auto& method : trait.methods) {
        for (const auto& parameter : method.params)
          reject(parameter.type, trait.source_file, trait.line, "passed as ordinary trait parameters");
        if (method.return_type)
          reject(*method.return_type, trait.source_file, trait.line, "returned as ordinary values");
      }
  }

  void check_static_message_receiver(int line, const string& receiver,
                                     const TypeEnv& env) const {
    auto self = env.find("self");
    if (self != env.end() && domains_.count(self->second)) {
      const Domain& domain = *domains_.at(self->second);
      if (receiver == "self")
        err(line, "self-send is not allowed; move shared logic to an ordinary helper function");
      auto route = std::find_if(domain.routes.begin(), domain.routes.end(),
          [&](const DomainRoute& slot) { return slot.name == receiver; });
      if (route == domain.routes.end())
        err(line, "message target '" + receiver +
            "' must resolve through a declared domainroutes slot");
      // Uninstantiated module declarations have structural routes; every
      // instantiated copy must have that route in the authoritative graph.
      for (const auto& instance : p_.concrete_domain_graph.instances)
        if (instance.domain == domain.name &&
            std::none_of(p_.concrete_domain_graph.edges.begin(),
                         p_.concrete_domain_graph.edges.end(),
                [&](const ConcreteRouteEdge& edge) {
                  return edge.source_instance == instance.identity && edge.route == receiver;
                }))
          err(line, "message route is absent from ConcreteDomainGraph");
    } else if (std::none_of(p_.concrete_domain_graph.instances.begin(),
                           p_.concrete_domain_graph.instances.end(),
                 [&](const ConcreteDomainInstance& instance) {
                   auto binding = env.find(receiver);
                   return instance.binding == receiver && binding != env.end() &&
                       binding->second == instance.domain;
                 })) {
      err(line, "message target '" + receiver +
          "' is not a concrete composition binding or declared domainroutes slot");
    }
  }

  void check_domain(const Domain& d) {
    std::set<string> state_names, handler_names;
    for (const auto& f : d.state) {
      if (!valid_type(f.type)) err(f.line, "unknown state type '" + f.type + "'");
      if (!state_names.insert(f.name).second) err(f.line, "duplicate state field '" + f.name + "'");
    }
    std::set<string> route_names;
    for (const auto& route : d.routes) {
      if (!domains_.count(route.type))
        err(route.source_file, route.line,
            "unknown domain route type '" + route.type + "'");
      if (!route_names.insert(route.name).second)
        err(route.source_file, route.line,
            "duplicate domain route '" + route.name + "'");
      if (state_names.count(route.name))
        err(route.source_file, route.line,
            "duplicate domain member '" + route.name + "' (state and route share one namespace)");
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
      if (h.reply_type) {
        size_t reply_index = 0;
        if (!every_handler_path_replies(h.body, reply_index, 0))
          err(h.line, "reply handler '" + d.name + "." + h.name +
              "' must reply on every normal control-flow path");
      }
      std::unordered_map<string,string> env;
      env["self"] = d.name;
      for (const auto& p : h.params) {
        if (p.type.empty())
          err(h.line, "cannot infer type for parameter '" + p.name +
              "' in handler '" + d.name + "." + h.name + "'");
        if (!valid_type(p.type)) err(h.line, "unknown parameter type '" + p.type + "'");
        if (env.count(p.name)) err(h.line, "duplicate parameter '" + p.name + "'");
        if (route_names.count(p.name))
          err(h.line, "handler parameter shadows immutable domain route '" + p.name + "'");
        env[p.name] = p.type;
      }
      for (const auto& f : d.state) env[f.name] = f.type;
      for (const auto& route : d.routes) env[route.name] = route.type;
      check_stmts(h.body, env, &d, &h);
      OwnershipEnv ownership;
      ownership.types = env;
      for (const auto& f : d.state) ownership.state_fields.insert(f.name);
      for (const auto& parameter : h.params)
        ownership.message_payloads.insert(parameter.name);
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

  static bool legacy_spawn_expression(const string& expression) {
    return starts_with(trim(expression), "spawn ");
  }

  void build_concrete_domain_graph() {
    p_.concrete_domain_graph = {};
    if (!p_.main) return;
    bool composition_open = true;
    std::unordered_map<string,size_t> by_binding;
    std::unordered_map<string,string> binding_types;

    auto fail_at = [&](const Stmt& statement, const string& message) {
      err(statement.source_file.empty() ? p_.main->source_file : statement.source_file,
          statement.line, message);
    };
    auto parse_bindings = [&](const string& expression) {
      vector<std::pair<string,string>> result;
      string callee; vector<string> args;
      if (!parse_simple_call(expression, callee, args)) return result;
      for (const auto& arg : args) {
        string name, value;
        if (!parse_named_argument(arg, name, value)) continue;
        result.emplace_back(std::move(name), std::move(value));
      }
      return result;
    };

    // Collect the complete static universe before validating route arguments.
    // This makes source order a deterministic tie-break only; it is not a
    // semantic requirement and permits cycle diagnostics over forward edges.
    for (const auto& statement : p_.main->body) {
      auto constructed = domain_constructor(statement.b);
      if (!constructed || statement.indent != 0) continue;
      if (!plain_identifier(statement.a)) continue;
      if (by_binding.count(statement.a))
        fail_at(statement, "duplicate domain instance binding '" + statement.a + "'");
      const Domain& domain = *domains_.at(*constructed);
      ConcreteDomainInstance instance;
      instance.binding = statement.a;
      instance.identity = "main::" + statement.a;
      instance.domain = domain.name;
      instance.source_domain_index = static_cast<size_t>(&domain - p_.domains.data());
      instance.specialization = "domain:" + domain.name;
      for (size_t index = 0; index < p_.domain_specializations.size(); ++index) {
        const auto& specialization = p_.domain_specializations[index];
        if (specialization.source_domain == domain.name &&
            specialization.instance == statement.a) {
          instance.specialization_index = index;
          instance.specialization = specialization.identity();
          break;
        }
      }
      if (!instance.specialization_index) {
        DomainSpecialization concrete;
        concrete.source_domain = domain.name;
        concrete.instance = statement.a;
        concrete.materialized_layout = false;
        instance.specialization_index = p_.domain_specializations.size();
        instance.specialization = concrete.identity();
        p_.domain_specializations.push_back(std::move(concrete));
      }
      auto& specialization = p_.domain_specializations.at(*instance.specialization_index);
      for (const auto& field : domain.state)
        specialization.state_types.emplace(field.name, field.type);
      for (const auto& handler : domain.handlers) {
        auto& slots = specialization.handler_parameter_types[handler.name];
        slots.resize(handler.params.size());
        for (size_t index = 0; index < slots.size(); ++index)
          if (slots[index].empty()) slots[index] = handler.params[index].type;
        if (handler.reply_type)
          specialization.handler_reply_types.emplace(handler.name, *handler.reply_type);
      }
      instance.source_file = statement.source_file.empty() ? p_.main->source_file : statement.source_file;
      instance.line = statement.line;
      by_binding[statement.a] = p_.concrete_domain_graph.instances.size();
      binding_types[statement.a] = domain.name;
      p_.concrete_domain_graph.instances.push_back(std::move(instance));
    }

    for (const auto& statement : p_.main->body) {
      auto constructed = domain_constructor(statement.b);
      if (legacy_spawn_expression(statement.b))
        fail_at(statement,
                "'spawn' is retired: construct domain instances directly in the main composition prefix");
      if (constructed) {
        if (statement.indent != 0 ||
            !(statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
              statement.kind == Stmt::Kind::Assign) || !plain_identifier(statement.a))
          fail_at(statement,
                  "domain construction is only allowed as a top-level binding in main's composition prefix");
        if (!composition_open)
          fail_at(statement,
                  "domain construction must occur before ordinary execution begins in main");
        const Domain& domain = *domains_.at(*constructed);
        if (!by_binding.count(statement.a))
          fail_at(statement, "domain construction requires a named binding");

        string constructor_name;
        vector<string> constructor_arguments;
        if (!parse_simple_call(statement.b, constructor_name, constructor_arguments))
          fail_at(statement, "domain construction requires a direct constructor call");
        for (const auto& argument : constructor_arguments) {
          string member_name, member_value;
          if (!parse_named_argument(argument, member_name, member_value))
            fail_at(statement,
                   "domain constructors require named member bindings such as 'field: value'");
        }
        auto supplied = parse_bindings(statement.b);
        std::set<string> names;
        for (const auto& pair : supplied) {
          if (!names.insert(pair.first).second)
            fail_at(statement, "duplicate constructor member binding '" + pair.first + "'");
          auto route = std::find_if(domain.routes.begin(), domain.routes.end(),
                                    [&](const DomainRoute& candidate) { return candidate.name == pair.first; });
          auto field = std::find_if(domain.state.begin(), domain.state.end(),
                                    [&](const Field& candidate) { return candidate.name == pair.first; });
          if (route == domain.routes.end() && field == domain.state.end())
            fail_at(statement, "unknown member '" + pair.first + "' in domain " + domain.name + " constructor");
          if (route != domain.routes.end()) {
            auto target = binding_types.find(trim(pair.second));
            if (target == binding_types.end())
              fail_at(statement, "route '" + pair.first + "' must bind a concrete domain instance in main");
            if (!same_type(target->second, route->type))
              fail_at(statement, "route '" + pair.first + "' expects domain " + route->type +
                              " but received " + target->second);
            ConcreteRouteEdge edge;
            edge.source_instance = p_.concrete_domain_graph.instances[by_binding.at(statement.a)].identity;
            edge.route = pair.first;
            edge.target_instance = p_.concrete_domain_graph.instances[by_binding.at(trim(pair.second))].identity;
            edge.source_file = statement.source_file.empty() ? p_.main->source_file : statement.source_file;
            edge.line = statement.line;
            p_.concrete_domain_graph.edges.push_back(std::move(edge));
          } else {
            if (domains_.count(canonical_type_name(inferred_expr_type(pair.second, binding_types).value_or(""))))
              fail_at(statement, "domain handle '" + pair.second + "' cannot be used as state data");
            // State initialization is part of static composition, not an
            // executable topology-building hook.  Reuse the authoritative
            // observable-effect summaries so messages, domain access, I/O,
            // failure, divergence, and unresolved calls cannot make the
            // concrete graph depend on runtime behavior.
            ObservableEffects initializer_effects =
                observable_expression_effects(pair.second, binding_types);
            if (initializer_effects.message ||
                initializer_effects.domain_read || initializer_effects.domain_write ||
                initializer_effects.local_mutation || initializer_effects.external_io ||
                initializer_effects.may_fail || initializer_effects.may_diverge ||
                initializer_effects.unresolved)
              fail_at(statement,
                      "domain state initializers must be side-effect-free and cannot perform message, domain, I/O, failing, or divergent work");
            auto actual = inferred_expr_type(pair.second, binding_types);
            if (actual && !field->type.empty() && !same_type(field->type, *actual))
              fail_at(statement, "state member '" + pair.first + "' has type '" + field->type +
                              "' but initializer has type '" + *actual + "'");
          }
        }
        for (const auto& route : domain.routes)
          if (!names.count(route.name))
            fail_at(statement, "missing required route binding '" + route.name + "'");
        composition_open = true;
      } else if (statement.indent == 0) {
        composition_open = false;
      }
    }

    // A direct constructor nested in a control-flow block is not a prefix
    // binding even when its expression itself is otherwise well typed.
    for (const auto& statement : p_.main->body) {
      if (statement.indent > 0 && domain_constructor(statement.b))
        fail_at(statement, "domain construction is not allowed in control flow; construct all domains in main's composition prefix");
    }

    const size_t n = p_.concrete_domain_graph.instances.size();
    vector<vector<std::pair<size_t,size_t>>> adjacency(n);
    vector<int> indegree(n, 0);
    std::unordered_map<string,size_t> by_identity;
    for (size_t index = 0; index < n; ++index)
      by_identity[p_.concrete_domain_graph.instances[index].identity] = index;
    for (size_t edge_index = 0;
         edge_index < p_.concrete_domain_graph.edges.size(); ++edge_index) {
      const auto& edge = p_.concrete_domain_graph.edges[edge_index];
      size_t source = by_identity.at(edge.source_instance);
      size_t target = by_identity.at(edge.target_instance);
      adjacency[source].push_back({target, edge_index});
      ++indegree[target];
    }
    vector<size_t> ready;
    for (size_t index = 0; index < n; ++index) if (indegree[index] == 0) ready.push_back(index);
    auto stable_less = [&](size_t left, size_t right) {
      const auto& a = p_.concrete_domain_graph.instances[left];
      const auto& b = p_.concrete_domain_graph.instances[right];
      return std::tie(a.source_file, a.line, a.identity) <
             std::tie(b.source_file, b.line, b.identity);
    };
    std::sort(ready.begin(), ready.end(), stable_less);
    int rank = 0;
    size_t visited = 0;
    while (!ready.empty()) {
      size_t current = ready.front();
      ready.erase(ready.begin());
      p_.concrete_domain_graph.instances[current].domain_rank = rank++;
      ++visited;
      for (const auto& route : adjacency[current]) {
        size_t target = route.first;
        if (--indegree[target] == 0) {
          ready.push_back(target);
          std::sort(ready.begin(), ready.end(), stable_less);
        }
      }
    }
    if (visited != n) {
      std::ostringstream cycle;
      cycle << "concrete domain route cycle detected";
      vector<int> colors(n, 0);
      vector<std::pair<size_t,size_t>> path;
      vector<size_t> witness;
      std::function<bool(size_t)> find_cycle = [&](size_t source) {
        colors[source] = 1;
        for (const auto& route : adjacency[source]) {
          size_t target = route.first;
          size_t edge_index = route.second;
          if (colors[target] == 0) {
            path.push_back({source, edge_index});
            if (find_cycle(target)) return true;
            path.pop_back();
          } else if (colors[target] == 1) {
            size_t begin = 0;
            while (begin < path.size() && path[begin].first != target) ++begin;
            for (size_t index = begin; index < path.size(); ++index)
              witness.push_back(path[index].second);
            witness.push_back(edge_index);
            return true;
          }
        }
        colors[source] = 2;
        return false;
      };
      for (size_t index = 0; index < n && witness.empty(); ++index)
        if (colors[index] == 0) find_cycle(index);
      if (witness.empty()) {
        for (size_t index = 0; index < p_.concrete_domain_graph.edges.size(); ++index)
          witness.push_back(index);
      }
      for (size_t edge_index : witness) {
        const auto& edge = p_.concrete_domain_graph.edges[edge_index];
        cycle << "\n  " << edge.source_instance << " --" << edge.route
              << "--> " << edge.target_instance << " at "
              << edge.source_file << ":" << edge.line;
      }
      const auto& first = p_.concrete_domain_graph.edges.at(witness.front());
      err(first.source_file, first.line, cycle.str());
    }
  }

  void check_test_and_benchmark_declarations() {
    std::set<string> test_names;
    for (const auto& test : p_.tests) {
      if (!test_names.insert(test.name).second)
        err(test.source_file, test.line,
            "duplicate test name '" + test.name + "'");
      TypeEnv env;
      check_stmts(test.body, env, nullptr, nullptr);
      OwnershipEnv ownership;
      ownership.types = std::move(env);
      check_ownership(test.body, std::move(ownership), nullptr, nullptr);
    }
    std::set<string> benchmark_names;
    for (const auto& benchmark : p_.benchmarks) {
      if (!benchmark_names.insert(benchmark.name).second)
        err(benchmark.source_file, benchmark.line,
            "duplicate benchmark name '" + benchmark.name + "'");
      TypeEnv env;
      check_stmts(benchmark.body, env, nullptr, nullptr);
      OwnershipEnv ownership;
      ownership.types = std::move(env);
      check_ownership(benchmark.body, std::move(ownership), nullptr, nullptr);
    }
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
                 !option_none_compatible(*actual, *f.return_type) &&
                 canonical_type_name(*f.return_type) != canonical_type_name(*actual)) {
        err(f.result_line ? f.result_line : f.line,
            "function '" + f.name + "' returns '" + *actual +
            "' but is annotated '" + *f.return_type + "'");
      }
    } else if (!f.return_type) {
      const_cast<Function&>(f).return_type = "unit";
    }
  }

  std::optional<string> domain_constructor(const string& expr) const {
    string e = trim(expr);
    string name;
    vector<string> args;
    if (!parse_simple_call(e, name, args) || !domains_.count(name))
      return std::nullopt;
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
      if (actual && !h->params[index].inferred &&
          !same_type(h->params[index].type, *actual))
        err(line, "argument " + std::to_string(index + 1) + " to message " +
            dit->second->name + "." + message + " has type '" + *actual +
            "', expected '" + h->params[index].type + "'");
    }
    return h;
  }

  static std::optional<string> obvious_expr_type(const string& expression,
                                                  const std::unordered_map<string,string>& env) {
    string e = trim(expression);
    if (e == "None") return "_none";
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
    // Incoming handler arguments are immutable message snapshots.  This is
    // an authoritative ownership capability consumed by the ordinary
    // READ/WRITE/CONSUME checker; it is not a parser-level convention.
    std::set<string> message_payloads;
    // READ traversals remain active for the duration of their body.  This is
    // a compile-time borrow marker used to reject structural mutation of the
    // traversed collection.
    std::set<string> active_read_traversals;
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
    if (starts_with(original, "message ")) {
      string receiver, handler;
      vector<string> args;
      if (!parse_member_call(trim(original.substr(8)), receiver, handler, args) ||
          !plain_identifier(receiver) || !plain_identifier(handler))
        return std::nullopt;
      auto binding = env.find(receiver);
      if (binding == env.end()) return std::nullopt;
      auto domain = domains_.find(canonical_type_name(binding->second));
      if (domain == domains_.end()) return std::nullopt;
      const Handler* target = find_handler(*domain->second, handler);
      return target && target->reply_type ? *target->reply_type : string("unit");
    }
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
      if (callee == "assert" || callee == "assertEqual")
        return string("unit");
      if (callee == "sqrt") return string("float");
      if (callee == "sum" && args.size() == 1) {
        auto argument_type = inferred_expr_type(args.front(), env);
        if (argument_type && starts_with(*argument_type, "seq[") && ends_with(*argument_type, "]"))
          return trim(argument_type->substr(4, argument_type->size() - 5));
        if (argument_type && starts_with(*argument_type, "vector[") && ends_with(*argument_type, "]"))
          return trim(argument_type->substr(7, argument_type->size() - 8));
        return argument_type;
      }
      if (callee == "Some" && args.size() == 1) {
        auto value_type = inferred_expr_type(args.front(), env);
        if (value_type) return "option[" + *value_type + "]";
        return "option[_]";
      }
      if (callee == "range" && (args.size() == 2 || args.size() == 3))
        return "range[int]";
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
  // local-call discovery, ownership type state, and concrete message discovery. A
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

  const DomainSpecialization* domain_specialization_for(
      const string& receiver, const TypeEnv& env) const {
    auto source = env.find(receiver);
    if (source == env.end()) return nullptr;
    for (const auto& specialization : p_.domain_specializations)
      if (specialization.instance == receiver &&
          same_type(specialization.source_domain, source->second))
        return &specialization;
    return nullptr;
  }

  std::optional<string> specialized_handler_reply_type(
      const string& receiver, const string& handler, const TypeEnv& env) const {
    auto specialization = domain_specialization_for(receiver, env);
    if (specialization == nullptr) return std::nullopt;
    auto reply = specialization->handler_reply_types.find(handler);
    if (reply == specialization->handler_reply_types.end()) return std::nullopt;
    return reply->second;
  }

  void advance_type_environment(const Stmt& statement, TypeEnv& env,
                                const vector<Stmt>& statements,
                                bool record_semantic_types) const {
    if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
        statement.kind == Stmt::Kind::Assign) {
      if (statement.kind != Stmt::Kind::Assign || simple_identifier(statement.a)) {
        auto existing = env.find(statement.a);
        if (auto constructed = domain_constructor(statement.b)) {
          env[statement.a] = *constructed;
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

    if (statement.kind == Stmt::Kind::Message && !statement.message_result.empty()) {
      auto receiver = env.find(statement.a);
      if (receiver != env.end()) {
        auto domain = domains_.find(canonical_type_name(receiver->second));
        if (domain != domains_.end())
          if (const Handler* handler = find_handler(*domain->second, statement.b))
            if (handler->reply_type)
              env[statement.message_result] = *handler->reply_type;
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

      if (statement.kind == Stmt::Kind::For) {
        visitor(statement, env);
        TypeEnv incoming = env;
        ++index;
        TypeEnv body_env = incoming;
        string element_type = iterator_element_type(
            statement.line, statement.b, incoming).value_or("_value");
        body_env[statement.a] = element_type;
        if (record_semantic_types)
          const_cast<Stmt&>(statement).semantic_type = element_type;
        walk_type_environment_block(statements, index, level + 1, body_env,
                                    visitor, reject_conflicts, record_join_types,
                                    record_semantic_types);
        body_env.erase(statement.a);
        env = std::move(body_env);
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
        case Stmt::Kind::For:
          constrain_constructor_fields(statement.line, statement.b, current_env);
          break;
        case Stmt::Kind::Let:
        case Stmt::Kind::Var:
        case Stmt::Kind::Assign:
          constrain_constructor_fields(statement.line, statement.b, current_env);
          break;
        case Stmt::Kind::Message:
        case Stmt::Kind::Call:
          for (const auto& arg : statement.args)
            constrain_constructor_fields(statement.line, arg, current_env);
          if (statement.kind == Stmt::Kind::Message) {
            const string& receiver_name = statement.a;
            const string& handler_name = statement.b;
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
                  bool untyped_parameter = parameter.type.empty();
                  if (untyped_parameter || inferred_container) {
                    parameter.type = *actual;
                    parameter.inferred = parameter.inferred || untyped_parameter;
                  } else if (!parameter.inferred && !same_type(parameter.type, *actual))
                    err(statement.line, "argument " + std::to_string(index + 1) +
                        " to message " + receiver->second + "." + handler_name +
                        " has type '" + *actual + "', expected '" + parameter.type + "'");
                }
              }
              if (statement.kind == Stmt::Kind::Message &&
                  !statement.message_result.empty() && handler && handler->reply_type)
                env[statement.message_result] = *handler->reply_type;
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
          for (const auto& route : domain.routes) env[route.name] = route.type;
          infer_statement_expressions(handler.body, env);
        }
      }
      if (p_.main) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(p_.main->body, env);
      }
      for (const auto& test : p_.tests) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(test.body, env);
      }
      for (const auto& benchmark : p_.benchmarks) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(benchmark.body, env);
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

  void infer_domain_specializations() {
    p_.domain_specializations.clear();
    if (!p_.main) return;
    bool has_implicit_domain = std::any_of(
        p_.domains.begin(), p_.domains.end(), [](const Domain& domain) {
          if (std::any_of(domain.state.begin(), domain.state.end(),
                          [](const Field& field) { return field.inferred; }))
            return true;
          return std::any_of(
              domain.handlers.begin(), domain.handlers.end(),
              [](const Handler& handler) {
                return std::any_of(handler.params.begin(), handler.params.end(),
                                   [](const Param& parameter) {
                                     return parameter.inferred;
                                   });
              });
        });
    if (!has_implicit_domain) return;

    TypeEnv main_env;
    std::unordered_map<string,const Domain*> bindings;
    // A specialization belongs to one declared instance, not to the source
    // domain declaration.  Remember the first concrete constraint so a later
    // call cannot silently replace that instance's layout.
    std::unordered_map<string,std::pair<int,string>> specialization_sites;
    auto specialization_conflict = [&](const string& instance,
                                       const string& member,
                                       const string& existing,
                                       const string& incoming,
                                       int line) -> void {
      string key = instance + "\n" + member;
      auto prior = specialization_sites.find(key);
      std::ostringstream message;
      message << "conflicting domain specialization for instance '" << instance
              << "'\n\n" << member << " inferred as:\n  " << existing;
      if (prior != specialization_sites.end())
        message << " from line " << prior->second.first;
      message << "\n  " << incoming << " from line " << line
              << "\n\neach declared domain instance must have one concrete state layout";
      CompileError error(line, message.str());
      error.source_file = p_.main->source_file;
      throw error;
    };
    auto find_specialization = [&](const string& instance,
                                   const string& source_domain) -> DomainSpecialization& {
      for (auto& specialization : p_.domain_specializations)
        if (specialization.instance == instance &&
            specialization.source_domain == source_domain)
          return specialization;
      DomainSpecialization specialization;
      specialization.instance = instance;
      specialization.source_domain = source_domain;
      p_.domain_specializations.push_back(std::move(specialization));
      return p_.domain_specializations.back();
    };

    for (const auto& statement : p_.main->body) {
      if (auto constructed = domain_constructor(statement.b)) {
        auto domain = domains_.find(*constructed);
        if (domain != domains_.end()) {
          bindings[statement.a] = domain->second;
          main_env[statement.a] = domain->second->name;
          bool implicit_domain = std::any_of(
              domain->second->state.begin(), domain->second->state.end(),
              [](const Field& field) { return field.inferred; });
          implicit_domain = implicit_domain || std::any_of(
              domain->second->handlers.begin(), domain->second->handlers.end(),
              [](const Handler& handler) {
                return std::any_of(handler.params.begin(), handler.params.end(),
                                   [](const Param& parameter) {
                                     return parameter.inferred;
                                   });
              });
          if (implicit_domain)
            find_specialization(statement.a, domain->second->name);
        }
      }

      if (statement.kind == Stmt::Kind::Message) {
        const string& receiver = statement.a;
        auto binding = bindings.find(receiver);
        if (binding != bindings.end()) {
          const Domain& domain = *binding->second;
          const string& handler_name = statement.b;
          const Handler* handler = find_handler(domain, handler_name);
          if (handler) {
            bool implicit_domain = std::any_of(
                domain.state.begin(), domain.state.end(),
                [](const Field& field) { return field.inferred; });
            implicit_domain = implicit_domain || std::any_of(
                domain.handlers.begin(), domain.handlers.end(),
                [](const Handler& candidate) {
                  return std::any_of(candidate.params.begin(), candidate.params.end(),
                                     [](const Param& parameter) {
                                       return parameter.inferred;
                                     });
                });
            if (!implicit_domain) {
              advance_type_environment(statement, main_env, p_.main->body, false);
              continue;
            }
            auto& specialization = find_specialization(receiver, domain.name);
            vector<string> actual_types;
            for (const auto& argument : statement.args)
              actual_types.push_back(
                  inferred_expr_type(argument, main_env).value_or(""));
            for (size_t index = 0;
                 index < handler->params.size() && index < actual_types.size();
                 ++index) {
              const auto& parameter = handler->params[index];
              string type = parameter.inferred && !actual_types[index].empty()
                  ? actual_types[index] : parameter.type;
              // Handler specialization is nominal: a typed domain-handle
              // parameter is constrained by its declared domain type.  The
              // exact constructed instance supplied at this call site is tracked
              // separately by concrete topology and must not split the handler
              // layout (Worker__w1 versus Worker__w2).
              auto& parameter_types =
                  specialization.handler_parameter_types[handler_name];
              if (parameter_types.size() <= index)
                parameter_types.resize(index + 1);
              string& existing = parameter_types[index];
              if (existing.empty() && !type.empty()) {
                existing = type;
                specialization_sites[
                    receiver + "\nhandler '" + handler_name + "' parameter " +
                    std::to_string(index)] = {statement.line, type};
              } else if (!existing.empty() && !type.empty() &&
                         !same_type(existing, type)) {
                bool reported_state_conflict = false;
                for (const auto& field : domain.state) {
                  auto state = specialization.state_types.find(field.name);
                  if (field.inferred && state != specialization.state_types.end() &&
                      !same_type(state->second, type)) {
                    specialization_conflict(
                        receiver, "field '" + field.name + "'", state->second,
                        type, statement.line);
                    reported_state_conflict = true;
                  }
                }
                if (!reported_state_conflict)
                  specialization_conflict(
                      receiver, "handler '" + handler_name + "' parameter " +
                          std::to_string(index), existing, type,
                      statement.line);
              }
            }

            TypeEnv handler_env;
            handler_env["self"] = domain.name;
            for (size_t index = 0; index < handler->params.size(); ++index) {
              const auto& parameter = handler->params[index];
              string type = parameter.type;
              if (parameter.inferred && index < actual_types.size() &&
                  !actual_types[index].empty())
                type = actual_types[index];
              handler_env[parameter.name] = type;
            }
            for (const auto& field : domain.state) {
              auto prior = specialization.state_types.find(field.name);
              handler_env[field.name] = prior != specialization.state_types.end()
                  ? prior->second
                  : field.inferred ? "_value" : field.type;
            }
            for (const auto& body_statement : handler->body) {
              for (const auto& expression : statement_expressions(body_statement)) {
                string callee;
                vector<string> arguments;
                if (!parse_simple_call(expression, callee, arguments)) continue;
                auto function = functions_.find(callee);
                if (function == functions_.end() || !function->second->static_dispatch)
                  continue;
                vector<string> argument_types;
                for (const auto& argument : arguments)
                  argument_types.push_back(
                      inferred_expr_type(argument, handler_env).value_or(""));
                register_static_specialization(body_statement.line, *function->second,
                                               argument_types);
              }
            }
            infer_statement_expressions(handler->body, handler_env);
            for (const auto& field : domain.state) {
              auto inferred = handler_env.find(field.name);
              if (inferred != handler_env.end() &&
                  concrete_environment_type(inferred->second)) {
                string incoming = canonical_type_name(inferred->second);
                auto prior = specialization.state_types.find(field.name);
                if (prior == specialization.state_types.end()) {
                  specialization.state_types[field.name] = incoming;
                  specialization_sites[receiver + "\nfield '" + field.name +
                                      "'"] = {statement.line, incoming};
                } else if (!same_type(prior->second, incoming)) {
                  specialization_conflict(
                      receiver, "field '" + field.name + "'", prior->second,
                      incoming, statement.line);
                }
              }
            }
            for (const auto& body_statement : handler->body) {
              if (body_statement.kind != Stmt::Kind::Reply) continue;
              if (auto reply = inferred_expr_type(body_statement.a, handler_env)) {
                string incoming = canonical_type_name(*reply);
                auto prior = specialization.handler_reply_types.find(handler_name);
                if (prior == specialization.handler_reply_types.end()) {
                  specialization.handler_reply_types[handler_name] = incoming;
                  specialization_sites[receiver + "\nreply '" + handler_name +
                                      "'"] = {statement.line, incoming};
                } else if (!same_type(prior->second, incoming)) {
                  specialization_conflict(
                      receiver, "reply '" + handler_name + "'", prior->second,
                      incoming, statement.line);
                }
              }
            }
          }
        }
      }

      advance_type_environment(statement, main_env, p_.main->body, false);
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
              if (field.type.empty()) {
                field.type = *actual;
                // An initializer fixes one source-domain layout.  Only a
                // field inferred from an untyped handler assignment is
                // eligible for per-instance specialization.
                field.inferred = field.init.empty();
              }
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
          for (const auto& route : domain.routes) env[route.name] = route.type;
          infer_statement_expressions(handler.body, env);
          for (auto& field : domain.state) {
            auto inferred = env.find(field.name);
            if (inferred == env.end() || inferred->second.empty() || inferred->second == "_value") continue;
            if (field.type.empty()) {
              field.type = inferred->second;
              field.inferred = true;
            } else if (!field.inferred && !same_type(field.type, inferred->second))
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
          for (const auto& route : domain.routes) env[route.name] = route.type;
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
      // An untyped identity helper is a statically specialized generic.  It
      // must be recognized before domain inference reaches its first call;
      // otherwise the first domain instance would permanently type the
      // helper and poison later domain specializations.
      for (const auto& parameter : function.params) {
        bool identity_result = function.result_expression &&
            trim(*function.result_expression) == parameter.name;
        if (!identity_result) {
          identity_result = std::any_of(
              function.body.begin(), function.body.end(),
              [&](const Stmt& statement) {
                return statement.kind == Stmt::Kind::Return &&
                    trim(statement.a) == parameter.name;
              });
        }
        if (parameter.type.empty() && identity_result) {
          function.generic = true;
          function.static_dispatch = true;
          function.generic_results[parameter.name] = parameter.name;
          function.return_type = "_generic:" + parameter.name;
          break;
        }
      }
      for (const auto& parameter : function.params)
        if (!parameter.type.empty() && traits_.count(parameter.type))
          function.static_dispatch = true;
      for (const auto& parameter : function.params) {
        if (parameter.type != "Iterator") continue;
        function.generic = true;
        function.static_dispatch = true;
        if (!has_structural_requirement(function, parameter.name,
                                         ConstraintKind::Iterable,
                                         "static iterator"))
          function.constraints.push_back({ConstraintKind::Iterable,
                                          parameter.name,
                                          "static iterator", ""});
      }
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
        if (s.kind == Stmt::Kind::For) {
          for (const auto& parameter : function.params) {
            if (parameter.type.empty() && trim(s.b) == parameter.name) {
              if (!has_structural_requirement(function, parameter.name,
                                               ConstraintKind::Iterable,
                                               "static iterator"))
                function.constraints.push_back({ConstraintKind::Iterable,
                                                parameter.name,
                                                "static iterator", ""});
              function.generic = true;
              function.static_dispatch = true;
            }
          }
        }
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
          for (const auto& route : domain.routes) env[route.name] = route.type;
          infer_statement_expressions(handler.body, env);
        }
      }
      if (p_.main) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(p_.main->body, env);
      }
      for (const auto& test : p_.tests) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(test.body, env);
      }
      for (const auto& benchmark : p_.benchmarks) {
        std::unordered_map<string,string> env;
        infer_statement_expressions(benchmark.body, env);
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
      if (!function.generic) {
        for (const auto& parameter : function.params) {
          bool identity_result = function.result_expression &&
              trim(*function.result_expression) == parameter.name;
          if (!identity_result) {
            identity_result = std::any_of(
                function.body.begin(), function.body.end(),
                [&](const Stmt& statement) {
                  return statement.kind == Stmt::Kind::Return &&
                      trim(statement.a) == parameter.name;
                });
          }
          if (parameter.type.empty() && identity_result) {
            function.generic = true;
            function.static_dispatch = true;
            function.generic_results[parameter.name] = parameter.name;
            function.return_type = "_generic:" + parameter.name;
            break;
          }
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
    if (dot != string::npos && simple_identifier(trim(value.substr(dot + 1)))) {
      auto location = storage_location(value.substr(0, dot), env);
      if (!location) return std::nullopt;
      location->path.push_back(trim(value.substr(dot + 1)));
      return location;
    }
    return std::nullopt;
  }

  // Optional observation sink on the existing semantic effect traversal.
  // No ownership summaries are rewritten to encode synchronization modes.
  struct LeafEffectCapture {
    std::map<string,string> roots;
    std::map<string,StorageLocation> aliases;
    StateLeafEffects effects;
  };
  mutable LeafEffectCapture* leaf_effect_capture_ = nullptr;
  struct LeafCaptureScope {
    LeafEffectCapture*& slot;
    LeafEffectCapture* previous;
    LeafCaptureScope(LeafEffectCapture*& target, LeafEffectCapture* value)
        : slot(target), previous(target) { slot = value; }
    ~LeafCaptureScope() { slot = previous; }
  };

  void checked_state_leaves(const string& path, const string& type,
                            StateLeafSet& leaves, std::set<string> active = {}) const {
    string concrete = canonical_type_name(type);
    synchronization_require(!concrete.empty() && !starts_with(concrete, "_") &&
                            !traits_.count(concrete), "unresolved state layout");
    auto object = objects_.find(concrete);
    if (object == objects_.end()) {
      // Collections, optional values, and runtime indices retain the checked
      // storage-location aggregate granularity. No dynamic key locks.
      leaves.insert(path);
      return;
    }
    synchronization_require(active.insert(concrete).second, "recursive state layout");
    for (const auto& field : object->second->fields)
      checked_state_leaves(path + "." + field.name, field.type, leaves, active);
  }

  void observe_leaf_access(const StorageLocation& location, Effect effect) const {
    if (!leaf_effect_capture_) return;
    auto& capture = *leaf_effect_capture_;
    string root = location.root;
    vector<string> path = location.path;
    auto alias = capture.aliases.find(root);
    if (alias != capture.aliases.end()) {
      path.insert(path.begin(), alias->second.path.begin(), alias->second.path.end());
      root = alias->second.root;
    }
    auto found = capture.roots.find(root);
    if (found == capture.roots.end()) return;
    string leaf = root, type = found->second;
    for (const auto& member : path) {
      if (member == "[]") break;
      auto object = objects_.find(canonical_type_name(type));
      synchronization_require(object != objects_.end(), "unresolved state projection");
      auto field = std::find_if(object->second->fields.begin(), object->second->fields.end(),
          [&](const Field& candidate) { return candidate.name == member; });
      synchronization_require(field != object->second->fields.end(), "unknown checked state field");
      leaf += "." + member;
      type = field->type;
    }
    StateLeafSet leaves;
    checked_state_leaves(leaf, type, leaves);
    for (const auto& item : leaves) capture.effects.observe(item, effect);
  }

  // Reuse the same effect walker with concrete parameter/receiver types. Its
  // formal access paths are substituted onto the caller's checked locations.
  void capture_callable_effects(
      const vector<Stmt>& body, const std::optional<string>& result,
      const vector<Param>& formals, const vector<string>& arguments,
      const std::unordered_map<string,string>& caller_env,
      const vector<Param>& caller_params, vector<Effect>& caller_effects,
      Effect& caller_receiver, const std::set<string>& caller_fields,
      const ObjectType* object = nullptr, const string& receiver = "",
      const StateLeafEffects* recorded = nullptr) const {
    LeafEffectCapture capture;
    std::unordered_map<string,string> env;
    std::map<string,string> substitutions;
    std::set<string> fields;
    for (size_t i = 0; i < formals.size(); ++i) {
      synchronization_require(i < arguments.size(), "unresolved callable arity");
      auto type = inferred_expr_type(arguments[i], caller_env);
      synchronization_require(type.has_value(), "unresolved callable argument");
      env[formals[i].name] = *type;
      capture.roots[formals[i].name] = *type;
      substitutions[formals[i].name] = arguments[i];
    }
    if (object) {
      env["self"] = object->name;
      capture.roots["self"] = object->name;
      substitutions["self"] = receiver;
      for (const auto& field : object->fields) {
        env[field.name] = field.type;
        capture.aliases[field.name] = StorageLocation{"self", {field.name}};
        fields.insert(field.name);
      }
    }
    if (recorded) capture.effects = *recorded;
    else {
      LeafCaptureScope scope(leaf_effect_capture_, &capture);
      vector<Effect> unused(formals.size(), Effect::Read);
      Effect unused_receiver = Effect::Read;
      size_t index = 0;
      analyze_effect_block(body, index, 0, env, formals, unused, unused_receiver, fields);
      if (result) analyze_effect_expression(*result, env, formals, unused,
                                           unused_receiver, fields, Effect::Consume);
    }
    auto project = [&](const StateLeafSet& leaves, Effect effect) {
      for (const auto& leaf : leaves) {
        size_t dot = leaf.find('.');
        string root = leaf.substr(0, dot);
        auto actual = substitutions.find(root);
        synchronization_require(actual != substitutions.end(), "unbound formal effect");
        // Preserve semantic effects independently of backend representation.
        // In particular, a primitive parameter's inferred WRITE must not be
        // silently weakened because the current emitter passes it by value.
        string expression = actual->second;
        if (dot != string::npos) expression = "(" + expression + ")" + leaf.substr(dot);
        if (auto location = storage_location(expression, caller_env))
          observe_leaf_access(*location, effect);
        else
          analyze_effect_expression(expression, caller_env, caller_params, caller_effects,
                                    caller_receiver, caller_fields, effect);
      }
    };
    project(capture.effects.reads, Effect::Read);
    project(capture.effects.writes, Effect::Write);
    project(capture.effects.consumes, Effect::Consume);
    // Evaluating an argument expression can itself call effectful code, even
    // when the formal is unused. Direct storage is handled by the summary.
    for (const auto& argument : arguments)
      if (!storage_location(argument, caller_env) ||
          copy_type(inferred_expr_type(argument, caller_env).value_or("")))
        analyze_effect_expression(argument, caller_env, caller_params, caller_effects,
                                  caller_receiver, caller_fields, Effect::Read);
  }

  StateLeafEffects specialized_handler_effects(
      const Domain& domain, const Handler& handler,
      const DomainSpecialization* specialization) const {
    for (const auto& module : p_.external_modules)
      if (starts_with(domain.name, module + "__")) {
        if (handler.state_effects) return *handler.state_effects;
        throw CompileError(handler.line, "compiled domain lacks handler state effects; rebuild its .mossi provider");
      }
    LeafEffectCapture capture;
    std::unordered_map<string,string> env;
    std::set<string> fields;
    for (const auto& field : domain.state) {
      string type = specialization ? specialization->state_types.at(field.name) : field.type;
      capture.roots[field.name] = type;
      env[field.name] = type;
      fields.insert(field.name);
    }
    for (size_t i = 0; i < handler.params.size(); ++i)
      env[handler.params[i].name] = specialization
          ? specialization->handler_parameter_types.at(handler.name).at(i) : handler.params[i].type;
    for (const auto& route : domain.routes) env[route.name] = route.type;
    LeafCaptureScope scope(leaf_effect_capture_, &capture);
    vector<Effect> unused(handler.params.size(), Effect::Read);
    Effect unused_receiver = Effect::Read;
    size_t index = 0;
    analyze_effect_block(handler.body, index, 0, env, handler.params, unused, unused_receiver, fields);
    return capture.effects;
  }

  // These helpers deliberately reuse the checked effect traversal.  Path
  // placement is a backend choice; it must not become a second effect system.
  StateLeafEffects specialized_handler_body_effects(
      const Domain& domain, const Handler& handler, const vector<Stmt>& body,
      const DomainSpecialization* specialization) const {
    Handler fragment = handler;
    fragment.body = body;
    return specialized_handler_effects(domain, fragment, specialization);
  }

  StateLeafEffects specialized_handler_expression_effects(
      const Domain& domain, const Handler& handler, const string& expression,
      const DomainSpecialization* specialization) const {
    LeafEffectCapture capture;
    std::unordered_map<string,string> env;
    std::set<string> fields;
    for (const auto& field : domain.state) {
      string type = specialization ? specialization->state_types.at(field.name) : field.type;
      capture.roots[field.name] = type;
      env[field.name] = type;
      fields.insert(field.name);
    }
    for (size_t i = 0; i < handler.params.size(); ++i)
      env[handler.params[i].name] = specialization
          ? specialization->handler_parameter_types.at(handler.name).at(i) : handler.params[i].type;
    for (const auto& route : domain.routes) env[route.name] = route.type;
    LeafCaptureScope scope(leaf_effect_capture_, &capture);
    vector<Effect> unused(handler.params.size(), Effect::Read);
    Effect unused_receiver = Effect::Read;
    analyze_effect_expression(expression, env, handler.params, unused,
                              unused_receiver, fields, Effect::Read);
    return capture.effects;
  }

  static vector<Stmt> normalized_statement_slice(const vector<Stmt>& body,
                                                  size_t begin, size_t end,
                                                  int parent_indent) {
    vector<Stmt> result;
    for (size_t i = begin; i < end; ++i) {
      Stmt statement = body[i];
      statement.indent -= parent_indent;
      result.push_back(std::move(statement));
    }
    return result;
  }

  static size_t statement_subtree_end(const vector<Stmt>& body, size_t begin) {
    const int indent = body.at(begin).indent;
    size_t end = begin + 1;
    while (end < body.size() && body[end].indent > indent) ++end;
    return end;
  }

  static std::map<size_t, SynchronizationMode> path_class_modes(
      const DomainSynchronizationPlan& domain, const HandlerSynchronizationPlan& whole,
      const StateLeafEffects& effects) {
    std::map<size_t, SynchronizationMode> result;
    for (const auto* leaves : {&effects.reads, &effects.writes, &effects.consumes}) {
      for (const auto& leaf : *leaves) {
        auto member = domain.leaf_to_class.find(leaf);
        if (member == domain.leaf_to_class.end()) continue;
        auto mode = whole.class_modes.find(member->second);
        synchronization_require(mode != whole.class_modes.end(), "path class absent from static ClassSet");
        result.emplace(member->second, mode->second);
      }
    }
    return result;
  }

  void derive_path_placement(const Domain& domain, const Handler& source,
                             const DomainSpecialization& specialization,
                             DomainSynchronizationPlan& planned,
                             HandlerSynchronizationPlan& whole) const {
    // Continuation splitting is intentionally bounded to a handler whose
    // first executable statement is a top-level if.  This is the shape in
    // which the condition has no predecessor-local bindings to transport and
    // all path state can stay statically typed. Other CFG shapes retain the
    // already-correct conservative entry plan.
    if (source.body.empty() || source.body.front().kind != Stmt::Kind::If ||
        source.body.front().indent != 0) {
      whole.path_placement.reason = "conservative entry placement: no eligible leading conditional";
      return;
    }
    const auto& branch = source.body.front();
    // A condition is evaluated before a branch-local acquisition.  Reuse the
    // authoritative observable-effect walk rather than recognizing messages
    // with a string test here: an observable condition must retain the full
    // entry ClassSet so no managed lock is acquired after O(h).
    TypeEnv condition_env;
    std::set<string> domain_fields;
    for (const auto& field : domain.state) {
      condition_env[field.name] = specialization.state_types.at(field.name);
      domain_fields.insert(field.name);
    }
    for (size_t i = 0; i < source.params.size(); ++i)
      condition_env[source.params[i].name] =
          specialization.handler_parameter_types.at(source.name).at(i);
    for (const auto& route : domain.routes) condition_env[route.name] = route.type;
    const ObservableEffects condition_observables =
        observable_expression_effects(branch.a, condition_env, domain_fields);
    if (condition_observables.message || condition_observables.external_io ||
        condition_observables.unresolved) {
      whole.path_placement.reason =
          "conservative entry placement: leading condition crosses observable-action barrier";
      return;
    }
    size_t then_begin = 1;
    size_t then_end = then_begin;
    while (then_end < source.body.size() && source.body[then_end].indent > branch.indent) ++then_end;
    size_t else_begin = then_end, else_end = then_end;
    if (then_end < source.body.size() && source.body[then_end].kind == Stmt::Kind::Else &&
        source.body[then_end].indent == branch.indent) {
      else_begin = then_end + 1;
      else_end = else_begin;
      while (else_end < source.body.size() && source.body[else_end].indent > branch.indent) ++else_end;
    }
    const size_t suffix_begin = else_begin == then_end ? then_end : else_end;
    auto then_body = normalized_statement_slice(source.body, then_begin, then_end, branch.indent + 1);
    auto else_body = normalized_statement_slice(source.body, else_begin, else_end, branch.indent + 1);
    auto suffix = normalized_statement_slice(source.body, suffix_begin, source.body.size(), 0);
    then_body.insert(then_body.end(), suffix.begin(), suffix.end());
    else_body.insert(else_body.end(), suffix.begin(), suffix.end());

    auto& placement = whole.path_placement;
    placement.enabled = true;
    placement.branch_line = branch.line;
    placement.condition_effects = specialized_handler_expression_effects(
        domain, source, branch.a, &specialization);
    placement.then_effects = specialized_handler_body_effects(
        domain, source, then_body, &specialization);
    placement.else_effects = specialized_handler_body_effects(
        domain, source, else_body, &specialization);
    auto condition = path_class_modes(planned, whole, placement.condition_effects);
    auto then_modes = path_class_modes(planned, whole, placement.then_effects);
    auto else_modes = path_class_modes(planned, whole, placement.else_effects);
    placement.entry_modes = condition;
    // A class needed regardless of the selected continuation is still an
    // entry acquisition. Phase 15.3 defers branch-only classes, not every
    // first use.
    for (const auto& entry : then_modes)
      if (else_modes.count(entry.first)) placement.entry_modes.emplace(entry);
    auto hoist_for_rank = [&] {
      size_t highest = 0;
      bool has_entry = false;
      for (const auto& entry : placement.entry_modes) {
        highest = std::max(highest, entry.first); has_entry = true;
      }
      bool changed = false;
      for (const auto* modes : {&then_modes, &else_modes})
        for (const auto& entry : *modes)
          if (!placement.entry_modes.count(entry.first) && has_entry && entry.first <= highest) {
            placement.entry_modes.emplace(entry); changed = true;
          }
      return changed;
    };
    while (hoist_for_rank()) {}
    for (const auto& entry : then_modes)
      if (!placement.entry_modes.count(entry.first)) placement.then_modes.emplace(entry);
    for (const auto& entry : else_modes)
      if (!placement.entry_modes.count(entry.first)) placement.else_modes.emplace(entry);
    for (const auto& entry : placement.entry_modes) {
      if (!condition.count(entry.first) && !then_modes.count(entry.first)) placement.cancel_then.insert(entry.first);
      if (!condition.count(entry.first) && !else_modes.count(entry.first)) placement.cancel_else.insert(entry.first);
    }
    placement.reason = "leading conditional continuation split; rank-forced entries may cancel untouched guards";
  }

  void build_synchronization_plan(const ConcreteDomainGraph& graph) {
    synchronization_require(graph.closed, "graph is not closed");
    SynchronizationPlan plan;
    plan.graph_identity = graph.identity;
    std::set<int> ranks;
    for (const auto& instance : graph.instances) {
      synchronization_require(instance.domain_rank >= 0 && ranks.insert(instance.domain_rank).second,
                              "duplicate or missing domain rank");
      synchronization_require(instance.specialization_index.has_value(), "missing exact specialization");
      const auto& specialization = p_.domain_specializations.at(*instance.specialization_index);
      const auto& domain = p_.domains.at(instance.source_domain_index);
      synchronization_require(specialization.identity() == instance.specialization &&
                              specialization.source_domain == domain.name, "specialization mismatch");
      DomainSynchronizationPlan planned;
      planned.concrete_instance_id = instance.identity;
      planned.specialization_id = instance.specialization;
      planned.domain_rank = instance.domain_rank;
      planned.domain = domain.name;
      planned.source_file = instance.source_file;
      planned.line = instance.line;
      for (const auto& field : domain.state)
        checked_state_leaves(field.name, specialization.state_types.at(field.name), planned.state_leaves);
      for (const auto& handler : domain.handlers) {
        HandlerSynchronizationPlan h;
        h.name = handler.name;
        h.handler_identity = specialization.identity() + "::handler:" + handler.name;
        h.effects = specialized_handler_effects(domain, handler, &specialization);
        for (const auto* leaves : {&h.effects.reads, &h.effects.writes, &h.effects.consumes})
          for (const auto& leaf : *leaves)
            synchronization_require(planned.state_leaves.count(leaf), "semantic effect outside specialization");
        planned.handlers.push_back(std::move(h));
      }
      derive_domain_synchronization(planned);
      for (auto& handler : planned.handlers) {
        auto source = std::find_if(domain.handlers.begin(), domain.handlers.end(),
            [&](const Handler& candidate) { return candidate.name == handler.name; });
        synchronization_require(source != domain.handlers.end(), "missing source handler for path placement");
        derive_path_placement(domain, *source, specialization, planned, handler);
      }
      plan.domains.push_back(std::move(planned));
    }
    std::sort(plan.domains.begin(), plan.domains.end(),
        [](const auto& a, const auto& b) { return a.domain_rank < b.domain_rank; });
    p_.synchronization_plan = std::move(plan);
    for (auto& function : p_.functions) {
      if (!function.exported || function.generic ||
          (function.body.empty() && !function.result_expression)) continue;
      LeafEffectCapture capture;
      std::unordered_map<string,string> env;
      for (const auto& parameter : function.params) {
        capture.roots[parameter.name] = parameter.type;
        env[parameter.name] = parameter.type;
      }
      LeafCaptureScope scope(leaf_effect_capture_, &capture);
      vector<Effect> unused(function.params.size(), Effect::Read);
      Effect receiver = Effect::Read;
      size_t index = 0;
      analyze_effect_block(function.body, index, 0, env, function.params, unused, receiver, {});
      if (function.result_expression)
        analyze_effect_expression(*function.result_expression, env, function.params, unused,
                                  receiver, {}, Effect::Consume);
      function.parameter_leaf_effects = std::move(capture.effects);
    }
    // Source declarations retain semantic observations for .mossi export only
    // after instance plans have consumed their exact specialization contexts.
    for (auto& domain : p_.domains)
      for (auto& handler : domain.handlers)
        handler.state_effects = specialized_handler_effects(domain, handler, nullptr);
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
    if (leaf_effect_capture_)
      if (auto location = storage_location(value, env)) observe_leaf_access(*location, effect);
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
    if (leaf_effect_capture_) {
      if (auto pipeline = parse_functional_pipeline(expression)) {
        auto source_type = inferred_expr_type(pipeline->source, env);
        auto element = source_type ? functional_element_type(*source_type, pipeline->source) : std::nullopt;
        synchronization_require(element.has_value(), "unresolved functional effect source");
        analyze_effect_expression(pipeline->source, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        string current_element = *element;
        for (const auto& stage : pipeline->stages) {
          if (stage.arguments.empty()) continue;
          auto callable_env = env;
          callable_env["_"] = current_element;
          string callable = stage.arguments.back();
          vector<string> inputs{current_element};
          vector<string> arguments{"_"};
          if (stage.kind == FunctionalNodeKind::Reduce && stage.arguments.size() == 2) {
            analyze_effect_expression(stage.arguments.front(), env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Consume);
            auto accumulator = inferred_expr_type(stage.arguments.front(), env);
            synchronization_require(accumulator.has_value(), "unresolved reduce accumulator");
            callable_env["__sync_accumulator"] = *accumulator;
            inputs.insert(inputs.begin(), *accumulator);
            arguments.insert(arguments.begin(), "__sync_accumulator");
          }
          if (expression_uses(callable, "_"))
            analyze_effect_expression(callable, callable_env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          else
            analyze_effect_expression(callable + "(" + join_arguments(arguments) + ")",
                                      callable_env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
          if (stage.kind == FunctionalNodeKind::Map) {
            auto result = functional_callable_result(callable, inputs, env);
            synchronization_require(result.has_value(), "unresolved functional callable");
            current_element = *result;
          }
        }
        return;
      }
    }
    // The observation path uses checked storage locations before the legacy
    // parameter summary collapses projections to their root.
    string value = strip_expression_parens(normalize_pipeline(trim(expression)));
    if (value.empty()) return;
    if (leaf_effect_capture_) {
      if (auto location = storage_location(value, env)) {
        observe_leaf_access(*location, projection_effect(value, requested, env));
        string projected = value, base, index;
        while (!simple_identifier(projected)) {
          if (parse_index(projected, base, index)) {
            analyze_effect_expression(index, env, params, parameter_effects,
                                      receiver_effect, receiver_fields, Effect::Read);
            projected = base;
          } else {
            auto dot = projected.rfind('.');
            if (dot == string::npos) break;
            projected = strip_expression_parens(projected.substr(0, dot));
          }
        }
        return;
      }
    }

    if (leaf_effect_capture_) {
      if (value.front() == '[' && value.back() == ']') {
        for (const auto& item : split_top_level(value.substr(1, value.size() - 2), ','))
          analyze_effect_expression(item, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
        return;
      }
      if (starts_with(value, "not ")) {
        analyze_effect_expression(value.substr(4), env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        return;
      }
      if (auto binary = split_binary(value, {" and ", " or "})) {
        analyze_effect_expression(binary->first, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        analyze_effect_expression(binary->second, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        return;
      }
    }

    if (starts_with(value, "message ")) {
      string receiver, handler;
      vector<string> arguments;
      if (parse_member_call(trim(value.substr(8)), receiver, handler, arguments)) {
        analyze_effect_expression(receiver, env, params, parameter_effects,
                                  receiver_effect, receiver_fields, Effect::Read);
        for (const auto& argument : arguments)
          analyze_effect_expression(argument, env, params, parameter_effects,
                                    receiver_effect, receiver_fields, Effect::Read);
      }
      return;
    }

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
        vector<string> concrete_arguments;
        if (leaf_effect_capture_)
          for (const auto& argument : arguments)
            concrete_arguments.push_back(inferred_expr_type(argument, env).value_or(""));
        if (auto object_method = resolve_method(concrete, method, concrete_arguments, true, nullptr)) {
          if (leaf_effect_capture_) {
            capture_callable_effects(object_method->body, object_method->result_expression,
                object_method->params, arguments, env, params, parameter_effects,
                receiver_effect, receiver_fields, objects_.at(concrete), receiver);
            return;
          }
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
      synchronization_require(!leaf_effect_capture_, "unresolved concrete method effect target");
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
      if (leaf_effect_capture_) {
        auto callable = env.find(callee);
        if (callable != env.end() && starts_with(callable->second, "callable:"))
          callee = callable->second.substr(9);
      }
      auto function = functions_.find(callee);
      if (function != functions_.end()) {
        if (leaf_effect_capture_) {
          const auto& target = *function->second;
          const StateLeafEffects* recorded = nullptr;
          if (target.body.empty() && !target.result_expression) {
            if (!target.parameter_leaf_effects)
              throw CompileError(target.line, "compiled function lacks parameter leaf effects; rebuild its .mossi provider");
            recorded = &*target.parameter_leaf_effects;
          }
          capture_callable_effects(target.body, target.result_expression,
              target.params, call_arguments, env, params, parameter_effects,
              receiver_effect, receiver_fields, nullptr, "", recorded);
          return;
        }
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
          if (leaf_effect_capture_) {
            capture_callable_effects(object_method->body, object_method->result_expression,
                object_method->params, call_arguments, env, params, parameter_effects,
                receiver_effect, receiver_fields, objects_.at(canonical_type_name(self->second)), "self");
            return;
          }
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
    if (!leaf_effect_capture_ && dot != string::npos && simple_identifier(trim(value.substr(dot + 1)))) {
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
    if (leaf_effect_capture_)
      for (const auto& root : leaf_effect_capture_->roots)
        if (expression_uses(value, root.first)) observe_leaf_access({root.first, {}}, Effect::Read);
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
      if (statement.kind == Stmt::Kind::If || statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::For) {
        Effect iteration_effect = Effect::Read;
        if (statement.kind == Stmt::Kind::For) {
          auto source_type = inferred_expr_type(statement.b, env);
          if (source_type && objects_.count(canonical_type_name(*source_type))) {
            if (const Method* next = resolve_method(
                    canonical_type_name(*source_type), "next", {}, true, nullptr))
              iteration_effect = next->receiver_effect;
          }
        }
        analyze_effect_expression(statement.kind == Stmt::Kind::For
                                      ? statement.b : statement.a,
                                  env, params, parameter_effects,
                                  receiver_effect, receiver_fields,
                                  iteration_effect);
        bool is_if = statement.kind == Stmt::Kind::If;
        ++index;
        auto child_env = env;
        if (statement.kind == Stmt::Kind::For)
          child_env[statement.a] =
              iterator_element_type(statement.line, statement.b, env)
                  .value_or("_value");
        std::optional<std::map<string,StorageLocation>> saved_aliases;
        if (leaf_effect_capture_ && statement.kind == Stmt::Kind::For &&
            !copy_type(child_env.at(statement.a))) {
          if (auto location = storage_location(statement.b, env)) {
            saved_aliases = leaf_effect_capture_->aliases;
            location->path.push_back("[]");
            leaf_effect_capture_->aliases[statement.a] = *location;
          }
        }
        analyze_effect_block(statements, index, level + 1, child_env, params,
                             parameter_effects, receiver_effect, receiver_fields);
        if (saved_aliases) leaf_effect_capture_->aliases = std::move(*saved_aliases);
        auto other_env = env;
        if (is_if && index < statements.size() && statements[index].indent == level &&
            statements[index].kind == Stmt::Kind::Else) {
          ++index;
          analyze_effect_block(statements, index, level + 1, other_env, params,
                               parameter_effects, receiver_effect, receiver_fields);
        }
        if (leaf_effect_capture_)
          env = merge_type_environments({child_env, other_env}, statement.line, false);
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
          else if (auto constructed = domain_constructor(statement.b)) env[statement.a] = *constructed;
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
          if (leaf_effect_capture_ && !statement.message_result.empty()) {
            auto receiver = env.find(statement.a);
            synchronization_require(receiver != env.end() && domains_.count(receiver->second), "unresolved message receiver");
            auto target = find_handler(*domains_.at(receiver->second), statement.b);
            synchronization_require(target && target->reply_type.has_value(), "unresolved message reply");
            env[statement.message_result] = *target->reply_type;
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
        case Stmt::Kind::For:
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
      case Stmt::Kind::Echo:
        expressions.insert(expressions.end(), statement.args.begin(), statement.args.end());
        break;
      case Stmt::Kind::If:
      case Stmt::Kind::While:
        expressions.push_back(statement.a);
        break;
      case Stmt::Kind::For:
        expressions.push_back(statement.b);
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
      case Stmt::Kind::Echo:
        expressions.insert(expressions.end(), statement.args.begin(),
                           statement.args.end());
        break;
      case Stmt::Kind::If:
      case Stmt::Kind::While:
        expressions.push_back(statement.a);
        break;
      case Stmt::Kind::For:
        expressions.push_back(statement.b);
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
      std::map<string,int>& edge_lines, const string& physical_source) {
    TypeEnvVisitor collect_statement = [&](const Stmt& statement,
                                            const TypeEnv& current_env) {
      for (const auto& expression : statement_expressions(statement)) {
        vector<LocalCallSite> calls;
        collect_local_call_sites(statement.line, expression, current_env,
                                 implicit_owner, calls);
        for (const auto& call : calls) {
          graph[source].insert(call.target);
          edge_lines.emplace(source + "\n" + call.target, statement.line);
          SemanticCallEdge edge{source, call.target, statement.line,
                                call.argument_types,
                                statement.source_file.empty()
                                    ? physical_source
                                    : statement.source_file};
          if (std::find_if(
                  p_.semantic_call_edges.begin(),
                  p_.semantic_call_edges.end(),
                  [&](const SemanticCallEdge& existing) {
                    return existing.source == edge.source &&
                        existing.target == edge.target &&
                        existing.line == edge.line &&
                        existing.argument_types == edge.argument_types &&
                        existing.source_file == edge.source_file;
                  }) == p_.semantic_call_edges.end())
            p_.semantic_call_edges.push_back(std::move(edge));
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
        SemanticCallEdge edge{source, call.target, result_line,
                              call.argument_types, physical_source};
        if (std::find_if(
                p_.semantic_call_edges.begin(),
                p_.semantic_call_edges.end(),
                [&](const SemanticCallEdge& existing) {
                  return existing.source == edge.source &&
                      existing.target == edge.target &&
                      existing.line == edge.line &&
                        existing.argument_types == edge.argument_types &&
                        existing.source_file == edge.source_file;
                }) == p_.semantic_call_edges.end())
          p_.semantic_call_edges.push_back(std::move(edge));
      }
    }
  }

  void check_local_call_cycles() {
    std::map<string,std::set<string>> graph;
    std::map<string,int> edge_lines;
    p_.semantic_call_edges.clear();
    for (const auto& function : p_.functions) {
      graph["fn:" + function.name];
      TypeEnv env;
      for (const auto& parameter : function.params)
        env[parameter.name] = parameter.type.empty()
            ? "_generic:" + parameter.name : parameter.type;
      collect_callable_edges(function.body, function.result_expression,
                             function.result_line, std::move(env), nullptr,
                             "fn:" + function.name, graph, edge_lines,
                             function.source_file);
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
                               graph, edge_lines, method.source_file);
      }
    }
    for (const auto& domain : p_.domains) {
      for (const auto& handler : domain.handlers) {
        string source = "handler:" + domain.name + "." + handler.name;
        graph[source];
        TypeEnv env;
        env["self"] = domain.name;
        for (const auto& field : domain.state) env[field.name] = field.type;
        for (const auto& route : domain.routes) env[route.name] = route.type;
        for (const auto& parameter : handler.params)
          env[parameter.name] = parameter.type;
        collect_callable_edges(handler.body, std::nullopt, 0, std::move(env),
                               nullptr, source, graph, edge_lines,
                               handler.source_file);
      }
    }
    if (p_.main) {
      graph["main"];
      collect_callable_edges(p_.main->body, std::nullopt, 0, {}, nullptr,
                             "main", graph, edge_lines, p_.main->source_file);
    }
    for (const auto& test : p_.tests) {
      string source = "test:" + test.name;
      graph[source];
      collect_callable_edges(test.body, std::nullopt, 0, {}, nullptr, source,
                             graph, edge_lines, test.source_file);
    }
    for (const auto& benchmark : p_.benchmarks) {
      string source = "bench:" + benchmark.name;
      graph[source];
      collect_callable_edges(benchmark.body, std::nullopt, 0, {}, nullptr,
                             source, graph, edge_lines, benchmark.source_file);
    }

    std::sort(p_.semantic_call_edges.begin(), p_.semantic_call_edges.end(),
              [](const SemanticCallEdge& left,
                 const SemanticCallEdge& right) {
                if (left.source != right.source)
                  return left.source < right.source;
                if (left.line != right.line) return left.line < right.line;
                if (left.target != right.target)
                  return left.target < right.target;
                if (left.argument_types != right.argument_types)
                  return left.argument_types < right.argument_types;
                return left.source_file < right.source_file;
              });

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

  void infer_effects() {
    for (auto& function : p_.functions)
      if (!function.body.empty() || function.result_expression || !function.parameter_leaf_effects)
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
        for (const auto& constraint : function.constraints) {
          if (constraint.kind != ConstraintKind::Iterable ||
              constraint.detail != "static iterator")
            continue;
          auto parameter = std::find_if(
              function.params.begin(), function.params.end(),
              [&](const Param& candidate) {
                return candidate.name == constraint.subject;
              });
          if (parameter != function.params.end()) {
            size_t parameter_index = static_cast<size_t>(
                parameter - function.params.begin());
            if (parameter_index < inferred.size())
              inferred[parameter_index] = join_effect(
                  inferred[parameter_index], Effect::Write);
          }
        }
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
        left.message == right.message &&
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

    if (starts_with(original, "message ")) {
      effects.message = true;
      string receiver, handler;
      vector<string> arguments;
      if (parse_member_call(trim(original.substr(8)), receiver, handler, arguments)) {
        effects.merge(observable_expression_effects(
            receiver, env, domain_fields, implicit_object));
        for (const auto& argument : arguments)
          effects.merge(observable_expression_effects(
              argument, env, domain_fields, implicit_object));
      } else {
        effects.unresolved = true;
      }
      return effects;
    }

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
      if (callee == "assert" || callee == "assertEqual") {
        effects.may_fail = true;
        return effects;
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
      if (statement.kind == Stmt::Kind::For &&
          !(starts_with(trim(statement.b), "range(") ||
            (starts_with(trim(statement.b), "[") &&
             ends_with(trim(statement.b), "]"))))
        effects.may_diverge = true;
      if (statement.kind == Stmt::Kind::Echo) effects.external_io = true;
      if (statement.kind == Stmt::Kind::Message) effects.message = true;
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
          for (const auto& route : domain.routes) env[route.name] = route.type;
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
    const bool deferred_generic_callable =
        local != env.end() && starts_with(local->second, "_generic:");
    if (local != env.end() && starts_with(local->second, "callable:"))
      identity = local->second.substr(9);
    auto function = functions_.find(identity);
    if (function == functions_.end()) {
#ifndef NDEBUG
      // The only permitted unresolved callable at this stage is a generic
      // higher-order declaration awaiting a concrete specialization.
      assert(deferred_generic_callable &&
             "unresolved indirect callable reached effect analysis");
#endif
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
      const vector<int>& continuation_lines = {},
      const string& source_file = {}) {
    auto parsed = parse_functional_pipeline(expression);
    if (!parsed) return std::nullopt;
    auto source_type = inferred_expr_type(parsed->source, env);
    if (!source_type) return std::nullopt;
    auto element = functional_element_type(*source_type, parsed->source);
    if (!element || starts_with(*source_type, "_")) return std::nullopt;

    FunctionalPipeline pipeline;
    pipeline.transient_id = next_pipeline_id++;
    pipeline.semantic_identity = semantic_identity;
    pipeline.source_file = source_file;
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
#ifndef NDEBUG
        // Generic higher-order bodies are intentionally unresolved until a
        // concrete call site specializes them.  Every other pipeline reaching
        // the checked functional IR must have a concrete callable identity;
        // an unresolved indirect callable here would make effect propagation
        // silently incomplete.
        auto callable_binding = env.find(trim(callable));
        const bool deferred_generic_callable =
            callable_binding != env.end() &&
            starts_with(callable_binding->second, "_generic:");
        if (!deferred_generic_callable)
          assert(!node.effects.unresolved &&
                 !node.callable_identity.empty());
#endif
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
            implicit_object, statement.continuation_lines,
            statement.source_file);
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
          implicit_object, result_continuation_lines,
          owning_function ? owning_function->source_file : string());
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
        for (const auto& route : domain.routes) env[route.name] = route.type;
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
    for (const auto& test : p_.tests)
      collect_functional_ir_from_body(
          test.body, std::nullopt, 0, {}, nullptr, {},
          "test:" + test.name, nullptr, next_pipeline_id, next_node_id);
    for (const auto& benchmark : p_.benchmarks)
      collect_functional_ir_from_body(
          benchmark.body, std::nullopt, 0, {}, nullptr, {},
          "bench:" + benchmark.name, nullptr, next_pipeline_id,
          next_node_id);
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

  void warn_payload(int line, const string& type, bool materializes = true) {
    // A Moss message is always a semantic value boundary.  The warning is a
    // physical-cost fact, so an internal `_shared` borrow must not report a
    // clone that the selected backend representation does not perform.
    if (!materializes) return;
    std::set<string> visiting;
    auto size = static_payload_size(type, visiting);
    if (!size || *size <= 1024) return;
    string message = "message payload materializes " + std::to_string(*size) +
        " bytes at an owned domain boundary";
    if (std::any_of(warnings_.begin(), warnings_.end(), [&](const Warning& warning) {
          return warning.line == line && warning.message == message;
        })) return;
    Warning warning{line, std::move(message), "MESSAGE_PAYLOAD_COPY_LARGE", {}};
    warnings_.push_back(std::move(warning));
  }

  bool message_payload_materializes(const string& receiver,
                                    const OwnershipEnv& env) const {
    auto type = env.types.find(receiver);
    if (type == env.types.end()) return true;
    auto domain = domains_.find(canonical_type_name(type->second));
    // This mirrors the generator's explicit exported/native bridge decision.
    // A missing domain is conservatively an ownership boundary; checked Moss
    // messages normally reach the non-exported synchronous `_shared` path.
    return domain == domains_.end() || domain->second->exported;
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
    if (env.message_payloads.count(name)) {
      err(line, "cannot CONSUME incoming message payload '" + name +
          "'; message payloads may only be read or forwarded");
      return;
    }
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
    if (requested != Effect::Read) {
      auto location = storage_location(value, env.types);
      if (location && env.message_payloads.count(location->root)) {
        if (requested == Effect::Write)
          err(line, "cannot WRITE incoming message payload '" +
              location->root + "'; message payloads are immutable snapshots");
        else
          err(line, "cannot CONSUME incoming message payload '" +
              location->root + "'; message payloads may only be read or forwarded");
        return;
      }
    }
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
          auto location = storage_location(receiver, env.types);
          if ((method == "push" || method == "pop") && location &&
              env.active_read_traversals.count(location->root))
            err(line, "cannot structurally mutate collection '" + location->root +
                "' during an active READ traversal");
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
    // remains available after a message/reply; the backend materializes a
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

      if (s.kind == Stmt::Kind::For) {
        auto element = iterator_element_type(s.line, s.b, env.types);
        string source_root;
        if (auto location = storage_location(s.b, env.types))
          source_root = location->root;
        Effect source_effect = Effect::Read;
        auto source_type = inferred_expr_type(s.b, env.types);
        if (source_type && objects_.count(canonical_type_name(*source_type))) {
          if (const Method* next = resolve_method(
                  canonical_type_name(*source_type), "next", {}, true, nullptr))
            source_effect = next->receiver_effect;
        }
        check_ownership_expression(s.line, s.b, env, source_effect);
        OwnershipEnv body = env;
        body.types[s.a] = element.value_or("_value");
        if (!source_root.empty() && source_effect == Effect::Read)
          body.active_read_traversals.insert(source_root);
        ++index;
        check_ownership_block(statements, index, level + 1, body,
                              current_domain, current_handler);
        body.types.erase(s.a);
        body.moved.erase(s.a);
        env = std::move(body);
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
          if (auto constructed = domain_constructor(s.b)) type = *constructed;
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
            else if (simple_identifier(s.a) && env.message_payloads.count(s.a))
              check_ownership_expression(s.line, s.a, env, Effect::Write);
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
          if (auto constructed = domain_constructor(s.b)) {
            env.types[s.a] = *constructed;
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

        case Stmt::Kind::Message: {
          check_ownership_expression(s.line, s.a, env, Effect::Read);
          const Handler* handler = check_call(s.line, s.a, s.b, s.args, env.types);
          for (size_t arg_index = 0; arg_index < s.args.size(); ++arg_index) {
            const auto& arg = s.args[arg_index];
            check_ownership_expression(s.line, arg, env, Effect::Read);
            if (arg_index < handler->params.size()) {
              require_cross_domain_value(s.line, arg, handler->params[arg_index].type,
                                         env, "");
              warn_payload(s.line, handler->params[arg_index].type,
                           message_payload_materializes(s.a, env));
            }
          }
          if (!s.message_result.empty()) {
            if (handler->reply_type) env.types[s.message_result] = *handler->reply_type;
            env.moved.erase(s.message_result);
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
        case Stmt::Kind::For:
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
    if (name == "range") {
      if (args.size() != 2 && args.size() != 3)
        err(line, "range expects range(start, end) or range(start, end, step)");
      for (const auto& argument : args) {
        auto actual = inferred_expr_type(argument, env);
        if (!actual || canonical_type_name(*actual) != "int")
          err(line, "range arguments must have type 'Int'");
      }
      if (args.size() == 3) {
        string step = trim(args[2]);
        size_t first_digit = !step.empty() && step.front() == '+' ? 1 : 0;
        bool literal = first_digit < step.size() &&
            std::all_of(step.begin() + static_cast<std::ptrdiff_t>(first_digit),
                        step.end(), [](char value) {
                          return std::isdigit(static_cast<unsigned char>(value));
                        });
        if (!literal || (step == "0" || step == "+0"))
          err(line, "range step must be a positive non-zero Int literal");
      }
      return;
    }
    if (name == "Some") {
      if (args.size() != 1) err(line, "Some expects one value");
      return;
    }
    if (name == "assert") {
      if (args.size() != 1)
        err(line, "assert expects 1 argument, got " +
            std::to_string(args.size()));
      auto condition = inferred_expr_type(args.front(), env);
      if (!condition || canonical_type_name(*condition) != "bool")
        err(line, "assert condition must have type 'bool'");
      return;
    }
    if (name == "assertEqual") {
      if (args.size() != 2)
        err(line, "assertEqual expects 2 arguments, got " +
            std::to_string(args.size()));
      auto actual = inferred_expr_type(args[0], env);
      auto expected = inferred_expr_type(args[1], env);
      if (!actual || !expected)
        err(line, "cannot infer assertEqual operand types");
      if (!same_type(*actual, *expected))
        err(line, "assertEqual operands have types '" + *actual +
            "' and '" + *expected + "'");
      string type = canonical_type_name(*actual);
      bool printable = type == "int" || type == "float" || type == "bool" ||
          type == "string" || starts_with(type, "vector[");
      if (!printable)
        err(line, "assertEqual currently supports scalar, string, and Vector values; compare a field for other values");
      return;
    }
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
          if (c.kind == ConstraintKind::Iterable && c.detail != "static iterator" &&
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
            "' cannot hide a domain crossing; use explicit message");
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
    if (domain_constructor(original))
      err(line, "domain construction is only allowed as a binding in main's composition prefix");
    if (starts_with(original, "message ")) {
      string receiver, handler;
      vector<string> args;
      if (!parse_member_call(trim(original.substr(8)), receiver, handler, args) ||
          !plain_identifier(receiver) || !plain_identifier(handler))
        err(line, "message requires a domain call 'receiver.Handler(args)'");
      auto binding = env.find(receiver);
      if (binding == env.end() || !domains_.count(canonical_type_name(binding->second)))
        err(line, "message receiver '" + receiver + "' is not a domain instance");
      if (receiver == "self")
        err(line, "self-send is not allowed; move shared logic to an ordinary helper function");
      check_static_message_receiver(line, receiver, env);
      auto self_binding = env.find("self");
      if (self_binding != env.end() && receiver != "self" &&
          canonical_type_name(binding->second) == canonical_type_name(self_binding->second))
        err(line,
            "same-domain handler chaining is not allowed; move shared logic to an ordinary helper function");
      const Handler* target = check_call(line, receiver, handler, args, env);
      (void)target;
      for (const auto& argument : args) check_expression(line, argument, env);
      return;
    }
    if (auto type = inferred_expr_type(original, env))
      if (contains_domain_handle(*type))
        err(line, "domain handles cannot be used as ordinary values or payloads; "
            "use only static domainroutes bindings and message targets");
    // Parentheses, literals and unary forms must not hide a routing capability
    // from the ordinary-expression checker.
    if (original.size() >= 2 && original.front() == '(' && original.back() == ')' &&
        matching_paren(original, 0) == original.size() - 1) {
      check_expression(line, original.substr(1, original.size() - 2), env);
      return;
    }
    if (original.size() >= 2 && original.front() == '[' && original.back() == ']') {
      for (const auto& element : split_top_level(original.substr(1, original.size() - 2), ','))
        check_expression(line, element, env);
      return;
    }
    if (check_functional_pipeline(line, original, env)) return;
    string value = normalize_pipeline(std::move(original));
    string receiver, handler;
    vector<string> args;
    if (parse_member_call(value, receiver, handler, args)) {
      auto it = env.find(receiver);
      if (it != env.end() && domains_.count(it->second))
        err(line, "naked cross-domain call '" + receiver + "." + handler +
            "' requires 'message'");
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
      check_expression(line, ib, env);
      check_expression(line, ii, env);
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
    for (const auto& operators : vector<vector<string>>{{" and ", " or "}, {"==", "!=", "<=", ">=", "<", ">"},
                                                         {"+", "-", "*", "/"}}) {
      if (auto binary = split_binary(value, operators)) {
        check_expression(line, binary->first, env);
        check_expression(line, binary->second, env);
        return;
      }
    }
    for (const auto& binding : functional_captures(value, env))
      if (contains_domain_handle(env.at(binding)))
        err(line, "domain handle '" + binding +
            "' is not an ordinary value; use domainroutes and message targets only");
  }

  std::optional<string> iterator_element_type(int line, const string& source,
                                              const TypeEnv& env) const {
    string callee;
    vector<string> arguments;
    if (parse_simple_call(trim(source), callee, arguments) && callee == "range") {
      if (arguments.size() != 2 && arguments.size() != 3)
        err(line, "range expects range(start, end) or range(start, end, step)");
      return "int";
    }
    auto source_type = inferred_expr_type(source, env);
    if (!source_type) {
      auto local = env.find(trim(source));
      if (local != env.end() && local->second.empty())
        return "_iterator_element:" + trim(source);
      err(line, "cannot infer the static iterator source type");
    }
    string type = canonical_type_name(*source_type);
    if (starts_with(type, "_generic:"))
      return "_iterator_element:" + type.substr(9);
    if (starts_with(type, "vector[") && ends_with(type, "]"))
      return trim(type.substr(7, type.size() - 8));
    if (starts_with(type, "seq[") && ends_with(type, "]"))
      return trim(type.substr(4, type.size() - 5));

    auto next_element = [&](const string& iterator_type) -> std::optional<string> {
      MethodResolutionFailure failure = MethodResolutionFailure::None;
      const Method* next = resolve_method(iterator_type, "next", {}, true, &failure);
      if (!next) return std::nullopt;
      if (!next->return_type || !starts_with(canonical_type_name(*next->return_type), "option[") ||
          !ends_with(canonical_type_name(*next->return_type), "]"))
        err(line, "iterator type '" + iterator_type + "' has next() without an Option result");
      string result = canonical_type_name(*next->return_type);
      string element = trim(result.substr(7, result.size() - 8));
      if (element.empty() || starts_with(element, "_"))
        err(line, "iterator type '" + iterator_type + "' has an unresolved next() element type");
      return element;
    };

    if (objects_.count(type)) {
      if (auto element = next_element(type)) return element;
      MethodResolutionFailure failure = MethodResolutionFailure::None;
      const Method* iter = resolve_method(type, "iter", {}, true, &failure);
      if (!iter || !iter->return_type)
        err(line, "type '" + type + "' does not satisfy Iterator: missing next() or iter()");
      if (auto element = next_element(canonical_type_name(*iter->return_type)))
        return element;
      err(line, "type '" + type + "' iter() result does not satisfy Iterator");
    }
    if (type == "Iterator")
      return "_iterator_element:Iterator";
    if (traits_.count(type))
      err(line, "for loop source has an open Iterator element type; use a concrete iterator type");
    err(line, "type '" + type + "' is not statically iterable; expected Vector, range, or Iterator");
    return std::nullopt;
  }

  void check_for_source(int line, const string& source, const TypeEnv& env) {
    string callee;
    vector<string> arguments;
    if (parse_simple_call(trim(source), callee, arguments) && callee == "range") {
      check_function_call(line, callee, arguments, env);
      for (const auto& argument : arguments) check_expression(line, argument, env);
    } else {
      check_expression(line, source, env);
    }
    (void)iterator_element_type(line, source, env);
  }

  TypeEnv check_stmts(const vector<Stmt>& statements, TypeEnv env,
                      const Domain* current, const Handler* current_handler,
                      const Function* current_function = nullptr) {
    TypeEnvVisitor check_statement = [&](const Stmt& statement,
                                         const TypeEnv& current_env) {
      if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        if (legacy_spawn_expression(statement.b))
          err(statement.line,
              "'spawn' is retired: construct domain instances directly in the main composition prefix");
        if (current &&
            std::any_of(current->routes.begin(), current->routes.end(),
                        [&](const DomainRoute& route) {
                          return route.name == statement.a;
                        }))
          err(statement.line, "domain route '" + statement.a +
              "' is immutable; bind routes in main's domain construction prefix");
        if (auto constructed = domain_constructor(statement.b)) {
          if (!domains_.count(*constructed))
            err(statement.line, "unknown domain in construction: " + *constructed);
          if (current || current_function || current_object_ || !p_.main ||
              &statements != &p_.main->body)
            err(statement.line, "domain construction is allowed only in the main composition prefix");
          return;
        }
        auto existing = current_env.find(statement.a);
        if (existing != current_env.end() && contains_domain_handle(existing->second))
          err(statement.line, "domain handle binding '" + statement.a + "' is immutable");
        for (const auto& binding : functional_captures(statement.a, current_env))
          if (contains_domain_handle(current_env.at(binding)))
            err(statement.line, "domain handle '" + binding +
                "' cannot be used as ordinary writable storage");
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

      if (statement.kind == Stmt::Kind::For) {
        auto prior = current_env.find(statement.a);
        if (prior != current_env.end() && contains_domain_handle(prior->second))
          err(statement.line, "domain handle binding '" + statement.a + "' is immutable");
        check_for_source(statement.line, statement.b, current_env);
        return;
      }

      if (statement.kind == Stmt::Kind::Message) {
        check_static_message_receiver(statement.line, statement.a, current_env);
        auto destination = current_env.find(statement.message_result);
        if (!statement.message_result.empty() && destination != current_env.end() &&
            contains_domain_handle(destination->second))
          err(statement.source_file, statement.line,
              "domain handle binding '" + statement.message_result + "' is immutable");
        if (current && statement.a == "self")
          err(statement.line,
              "self-send is not allowed; move shared logic to an ordinary helper function");
        if (current) {
          auto receiver = current_env.find(statement.a);
          if (receiver != current_env.end() &&
              canonical_type_name(receiver->second) == current->name)
            err(statement.line,
                "same-domain handler chaining is not allowed; move shared logic to an ordinary helper function");
        }
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

      if (statement.kind == Stmt::Kind::Call) {
        if (!statement.b.empty()) {
          auto receiver = current_env.find(statement.a);
          if (receiver != current_env.end() &&
              domains_.count(canonical_type_name(receiver->second)))
            err(statement.line, "naked cross-domain call '" + statement.a + "." +
                statement.b + "' requires 'message'");
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
        if (!statement.message_result.empty()) {
          const Domain* target = nullptr;
          auto receiver = current_env.find(statement.a);
          if (receiver != current_env.end()) {
            auto domain = domains_.find(canonical_type_name(receiver->second));
            if (domain != domains_.end()) target = domain->second;
          }
          const Handler* handler = target ? find_handler(*target, statement.b) : nullptr;
          if (!handler || !handler->reply_type)
            err(statement.line, "message assignment requires a value-returning handler");
          if (current_env.count(statement.message_result) &&
              !same_type(current_env.at(statement.message_result), *handler->reply_type))
            err(statement.line, "message assignment to '" + statement.message_result +
                "' has type '" + current_env.at(statement.message_result) +
                "', expected '" + *handler->reply_type + "'");
        }
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
            !option_none_compatible(*actual, *current_function->return_type) &&
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
// facts. Phase 4.6 then performs a bounded sequence of Moss-to-Moss rewrites
// over explicit semantic_steps. Rust generation consumes exact plan IDs and
// that rewritten sequence; it never asks an iterator library or source-text
// matcher to recover the semantic structure.
class FunctionalOptimizer {
 public:
  explicit FunctionalOptimizer(Program& program) : program_(program) {}

  void run(bool enabled) {
    program_.functional_traversal_groups.clear();
    for (auto& pipeline : program_.functional_pipelines) {
      pipeline.count_uses_exact_length = false;
      pipeline.count_uses_known_size = false;
      pipeline.known_source_size = 0;
      pipeline.short_circuit_terminal = false;
      pipeline.virtual_upstream_pipeline_id = 0;
      pipeline.virtualized_into_pipeline_id = 0;
      pipeline.traversal_group_id = 0;
      pipeline.binding_name.clear();
      pipeline.binding_immutable = false;
      pipeline.binding_use_count = 0;
      pipeline.binding_materialization_reason.clear();
      pipeline.optimization_notes.clear();
      pipeline.semantic_rewrites.clear();
      pipeline.fused = enabled && pipeline.fusion_eligible;
      pipeline.lowered_provenance.clear();
      for (auto& node : pipeline.nodes) {
        node.materialization_eliminated = false;
        node.materialization = FunctionalMaterializationKind::NotApplicable;
        node.escapes = false;
        node.multiple_consumers = false;
        node.barrier_required = false;
        node.dead_stage_eliminated = false;
        node.semantic_work_eliminated = false;
        node.semantic_elimination_reason.clear();
        node.materialization_reason.clear();
      }
      initialize_semantic_plan(pipeline);
      if (pipeline.fused)
        pipeline.decision = "fused stages 1-" +
            std::to_string(pipeline.nodes.size() - 1) +
            "; intermediates eliminated";
      else if (!enabled && pipeline.fusion_eligible)
        pipeline.decision =
            "eager reference lowering (-O0); logical intermediates retained";

      // This deliberately bounded sequence is the Phase 4.6 semantic-space
      // optimizer. Each pass either reduces the number of semantic steps or
      // groups adjacent steps once, so no general fixed point is required.
      if (enabled) {
        rewrite_identity_map_filter(pipeline);
        compose_adjacent_steps(pipeline, FunctionalNodeKind::Map,
                               "compose-map");
        compose_adjacent_steps(pipeline, FunctionalNodeKind::Filter,
                               "compose-filter");
      }
      plan_terminal_lowering(pipeline, enabled);
      if (enabled) plan_terminal_aware_semantics(pipeline);
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
      for (auto& test : program_.tests)
        plan_scope(test.body, "test:" + test.name, {}, 0, std::nullopt);
      for (auto& benchmark : program_.benchmarks)
        plan_scope(benchmark.body, "bench:" + benchmark.name, {}, 0,
                   std::nullopt);
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

  static void initialize_semantic_plan(FunctionalPipeline& pipeline) {
    pipeline.semantic_steps.clear();
    for (size_t node_index = 1; node_index < pipeline.nodes.size();
         ++node_index) {
      FunctionalSemanticStep step;
      step.kind = pipeline.nodes[node_index].kind;
      step.source_node_indices.push_back(node_index);
      step.provenance = pipeline.nodes[node_index].provenance;
      pipeline.semantic_steps.push_back(std::move(step));
    }
  }

  static void record_semantic_rewrite(
      FunctionalPipeline& pipeline, string name, string detail,
      vector<string> provenance) {
    FunctionalSemanticRewrite rewrite;
    rewrite.name = std::move(name);
    rewrite.detail = std::move(detail);
    rewrite.provenance = std::move(provenance);
    pipeline.semantic_rewrites.push_back(std::move(rewrite));
  }

  static bool step_nodes_satisfy(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step,
      const std::function<bool(const FunctionalNode&)>& predicate) {
    if (step.source_node_indices.empty()) return false;
    for (size_t node_index : step.source_node_indices) {
      if (node_index == 0 || node_index >= pipeline.nodes.size() ||
          !predicate(pipeline.nodes[node_index]))
        return false;
    }
    return true;
  }

  static bool trivial_pipeline_element_type(const string& collection_type) {
    string type = canonical_type_name(collection_type);
    string element;
    if (starts_with(type, "vector[") && ends_with(type, "]"))
      element = trim(type.substr(7, type.size() - 8));
    else if (starts_with(type, "seq[") && ends_with(type, "]"))
      element = trim(type.substr(4, type.size() - 5));
    return element == "int" || element == "float" || element == "bool";
  }

  static bool identity_map_step(const FunctionalPipeline& pipeline,
                                const FunctionalSemanticStep& step) {
    if (step.kind != FunctionalNodeKind::Map ||
        step.source_node_indices.size() != 1)
      return false;
    size_t node_index = step.source_node_indices.front();
    if (node_index == 0 || node_index >= pipeline.nodes.size()) return false;
    const FunctionalNode& node = pipeline.nodes[node_index];
    return trim(node.callable_expression) == "_" &&
        canonical_type_name(node.input_type) ==
            canonical_type_name(node.output_type) &&
        trivial_pipeline_element_type(node.input_type) &&
        node.ownership == Effect::Read && node.effects.fusion_safe();
  }

  // Predicate pushdown is intentionally limited to a proof the current IR
  // can make without algebra: a trivial-element `map(_)` is the identity. The
  // following predicate can therefore consume the earlier value unchanged,
  // and the redundant map can disappear. All non-identity maps remain in
  // source order.
  static void rewrite_identity_map_filter(FunctionalPipeline& pipeline) {
    for (size_t index = 0; index + 1 < pipeline.semantic_steps.size();) {
      FunctionalSemanticStep& map_step = pipeline.semantic_steps[index];
      const FunctionalSemanticStep& filter_step =
          pipeline.semantic_steps[index + 1];
      if (map_step.kind != FunctionalNodeKind::Map ||
          filter_step.kind != FunctionalNodeKind::Filter) {
        ++index;
        continue;
      }

      string disabled_reason;
      if (!pipeline.fusion_eligible)
        disabled_reason = "pipeline has " + barrier_reason(pipeline);
      else if (!identity_map_step(pipeline, map_step))
        disabled_reason = "map is not a proven identity";
      else if (!step_nodes_satisfy(
                   pipeline, filter_step,
                   [](const FunctionalNode& node) {
                     return node.effects.fusion_safe() &&
                         node.ownership == Effect::Read;
                   }))
        disabled_reason = "predicate has an observable or ownership barrier";

      if (!disabled_reason.empty()) {
        pipeline.optimization_notes.push_back(
            "semantic-opt: predicate-pushdown disabled; reason: " +
            disabled_reason);
        ++index;
        continue;
      }

      vector<string> provenance = map_step.provenance;
      provenance.insert(provenance.end(), filter_step.provenance.begin(),
                        filter_step.provenance.end());
      for (size_t node_index : map_step.source_node_indices) {
        FunctionalNode& node = pipeline.nodes[node_index];
        node.semantic_work_eliminated = true;
        node.semantic_elimination_reason =
            "predicate moved through a proven identity map";
      }
      record_semantic_rewrite(
          pipeline, "predicate-pushdown",
          "filter moved to the earlier value through map(_)", provenance);
      pipeline.optimization_notes.push_back(
          "semantic-opt: predicate-pushdown through proven identity map");
      pipeline.semantic_steps.erase(
          pipeline.semantic_steps.begin() +
          static_cast<std::ptrdiff_t>(index));
      // Keep the same index so another immediately preceding identity map can
      // be considered against the now-earlier filter.
    }
  }

  static void compose_adjacent_steps(FunctionalPipeline& pipeline,
                                     FunctionalNodeKind kind,
                                     const string& rewrite_name) {
    if (!pipeline.fusion_eligible) {
      for (size_t index = 0; index + 1 < pipeline.semantic_steps.size();
           ++index) {
        if (pipeline.semantic_steps[index].kind == kind &&
            pipeline.semantic_steps[index + 1].kind == kind) {
          pipeline.optimization_notes.push_back(
              "semantic-opt: " + rewrite_name +
              " disabled; reason: " + barrier_reason(pipeline));
          break;
        }
      }
      return;
    }

    for (size_t index = 0; index < pipeline.semantic_steps.size();) {
      if (pipeline.semantic_steps[index].kind != kind) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < pipeline.semantic_steps.size() &&
             pipeline.semantic_steps[end].kind == kind &&
             step_nodes_satisfy(
                 pipeline, pipeline.semantic_steps[end],
                 [](const FunctionalNode& node) {
                   return node.effects.fusion_safe();
                 }))
        ++end;
      if (end == index + 1) {
        ++index;
        continue;
      }

      FunctionalSemanticStep composed = pipeline.semantic_steps[index];
      for (size_t part = index + 1; part < end; ++part) {
        const auto& step = pipeline.semantic_steps[part];
        composed.source_node_indices.insert(
            composed.source_node_indices.end(),
            step.source_node_indices.begin(), step.source_node_indices.end());
        composed.provenance.insert(composed.provenance.end(),
                                   step.provenance.begin(),
                                   step.provenance.end());
      }
      record_semantic_rewrite(
          pipeline, rewrite_name,
          "composed " + std::to_string(end - index) +
              " adjacent " +
              string(kind == FunctionalNodeKind::Map ? "maps" : "filters"),
          composed.provenance);
      pipeline.optimization_notes.push_back(
          "semantic-opt: " + rewrite_name + " (" +
          std::to_string(end - index) + " stages)");
      pipeline.semantic_steps.erase(
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(index),
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(end));
      pipeline.semantic_steps.insert(
          pipeline.semantic_steps.begin() +
              static_cast<std::ptrdiff_t>(index),
          std::move(composed));
      ++index;
    }
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
      if (effects.external_io) return "observable callback effect";
      if (effects.local_mutation) return "observable local mutation";
      if (effects.may_fail) return "callback may fail";
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
    pipeline.count_uses_known_size = false;
    pipeline.known_source_size = 0;
    erase_notes_with_prefix(pipeline, "count -> len");
    erase_notes_with_prefix(pipeline, "count -> constant");
    erase_notes_with_prefix(pipeline, "map elimination:");
    pipeline.semantic_rewrites.erase(
        std::remove_if(
            pipeline.semantic_rewrites.begin(),
            pipeline.semantic_rewrites.end(),
            [](const FunctionalSemanticRewrite& rewrite) {
              return rewrite.name == "terminal-count" ||
                  rewrite.name == "known-size-count";
            }),
        pipeline.semantic_rewrites.end());
    pipeline.optimization_notes.push_back(
        "count -> len disabled; reason: " + reason);
    bool map_was_eliminated = std::any_of(
        pipeline.nodes.begin(), pipeline.nodes.end(),
        [](const FunctionalNode& node) {
          return node.kind == FunctionalNodeKind::Map &&
              node.dead_stage_eliminated;
        });
    if (mapped_work && !map_was_eliminated)
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

  static bool semantic_step_safe_to_skip(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step) {
    return step_nodes_satisfy(
        pipeline, step,
        [](const FunctionalNode& node) {
          return node.effects.safe_to_skip();
        });
  }

  static string semantic_step_skip_barrier(
      const FunctionalPipeline& pipeline,
      const FunctionalSemanticStep& step) {
    for (size_t node_index : step.source_node_indices) {
      if (node_index == 0 || node_index >= pipeline.nodes.size())
        return "invalid semantic stage";
      const auto& effects = pipeline.nodes[node_index].effects;
      if (effects.domain_write) return "observable domain WRITE";
      if (effects.domain_read) return "observable domain READ";
      if (effects.message) return "message send";
      if (effects.external_io) return "observable callback effect";
      if (effects.local_mutation) return "observable local mutation";
      if (effects.may_fail) return "callback may fail";
      if (effects.may_diverge) return "callback may diverge";
      if (effects.unresolved) return "unresolved callback effect";
    }
    return "ownership or eager-ordering barrier";
  }

  static bool inert_literal(string value) {
    value = trim(std::move(value));
    if (value == "true" || value == "false") return true;
    size_t index = 0;
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      ++index;
    bool digit = false;
    bool dot = false;
    for (; index < value.size(); ++index) {
      char ch = value[index];
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        digit = true;
        continue;
      }
      if (ch == '.' && !dot) {
        dot = true;
        continue;
      }
      return false;
    }
    if (!digit) return false;
    try {
      size_t consumed = 0;
      if (dot)
        (void)std::stod(value, &consumed);
      else
        (void)std::stoll(value, &consumed);
      return consumed == value.size();
    } catch (const std::exception&) {
      // Do not let an optimization hide an invalid/out-of-range literal that
      // ordinary Rust lowering would reject.
      return false;
    }
  }

  static std::optional<size_t> inert_literal_vector_size(
      const string& expression) {
    string value = trim(expression);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']')
      return std::nullopt;
    auto elements = split_top_level(value.substr(1, value.size() - 2), ',');
    if (elements.size() == 1 && trim(elements.front()).empty())
      return size_t{0};
    if (!std::all_of(elements.begin(), elements.end(),
                     [](const string& element) {
                       return inert_literal(element);
                     }))
      return std::nullopt;
    return elements.size();
  }

  static void eliminate_semantic_step(
      FunctionalPipeline& pipeline, size_t step_index, const string& reason,
      const string& rewrite_name) {
    FunctionalSemanticStep step = pipeline.semantic_steps[step_index];
    for (size_t node_index : step.source_node_indices) {
      FunctionalNode& node = pipeline.nodes[node_index];
      node.dead_stage_eliminated = true;
      node.semantic_work_eliminated = true;
      node.semantic_elimination_reason = reason;
    }
    record_semantic_rewrite(pipeline, rewrite_name, reason, step.provenance);
    pipeline.semantic_steps.erase(
        pipeline.semantic_steps.begin() +
        static_cast<std::ptrdiff_t>(step_index));
  }

  static bool eliminate_trailing_maps_before_count(
      FunctionalPipeline& pipeline) {
    if (pipeline.semantic_steps.empty() ||
        pipeline.semantic_steps.back().kind != FunctionalNodeKind::Count)
      return false;
    bool eliminated = false;
    while (pipeline.semantic_steps.size() >= 2) {
      size_t map_index = pipeline.semantic_steps.size() - 2;
      const auto& step = pipeline.semantic_steps[map_index];
      if (step.kind != FunctionalNodeKind::Map) break;
      if (!semantic_step_safe_to_skip(pipeline, step)) break;
      eliminate_semantic_step(
          pipeline, map_index,
          "mapped values are unused by terminal count and the callback is "
          "pure/non-failing/non-divergent",
          "dead-map");
      eliminated = true;
    }
    if (eliminated)
      pipeline.optimization_notes.push_back(
          "map stage eliminated; reason: output unused and callback is "
          "pure/non-failing/non-divergent");
    return eliminated;
  }

  static bool eliminate_trailing_maps_for_downstream_count(
      FunctionalPipeline& pipeline) {
    bool eliminated = false;
    while (!pipeline.semantic_steps.empty()) {
      size_t map_index = pipeline.semantic_steps.size() - 1;
      const auto& step = pipeline.semantic_steps[map_index];
      if (step.kind != FunctionalNodeKind::Map ||
          !semantic_step_safe_to_skip(pipeline, step))
        break;
      eliminate_semantic_step(
          pipeline, map_index,
          "mapped values are unused by downstream terminal count and the "
          "callback is pure/non-failing/non-divergent",
          "dead-map");
      eliminated = true;
    }
    if (eliminated)
      pipeline.optimization_notes.push_back(
          "map stage eliminated; reason: downstream count uses only "
          "cardinality");
    return eliminated;
  }

  void plan_terminal_lowering(FunctionalPipeline& pipeline, bool enabled) {
    bool count_terminal = !pipeline.semantic_steps.empty() &&
        pipeline.semantic_steps.back().kind == FunctionalNodeKind::Count;
    bool mapped_count = count_terminal && has_map_stage(pipeline);
    if (enabled && count_terminal) {
      if (pipeline.fusion_eligible) {
        bool eliminated = eliminate_trailing_maps_before_count(pipeline);
        bool blocked_trailing_map = pipeline.semantic_steps.size() >= 2 &&
            pipeline.semantic_steps[pipeline.semantic_steps.size() - 2].kind ==
                FunctionalNodeKind::Map;
        if (blocked_trailing_map) {
          const auto& blocked =
              pipeline.semantic_steps[pipeline.semantic_steps.size() - 2];
          disable_count_elimination(
              pipeline, semantic_step_skip_barrier(pipeline, blocked),
              mapped_count);
        } else if (pipeline.semantic_steps.size() == 1) {
          pipeline.count_uses_exact_length = true;
          vector<string> provenance;
          for (const auto& node : pipeline.nodes)
            provenance.insert(provenance.end(), node.provenance.begin(),
                              node.provenance.end());
          record_semantic_rewrite(
              pipeline, "terminal-count",
              "replaced traversal with exact source cardinality", provenance);
          pipeline.optimization_notes.push_back(
              "count -> len; reason: exact source cardinality known");
          if (auto size = inert_literal_vector_size(
                  pipeline.source_expression)) {
            pipeline.count_uses_known_size = true;
            pipeline.known_source_size = *size;
            record_semantic_rewrite(
                pipeline, "known-size-count",
                "inert vector literal has " + std::to_string(*size) +
                    " elements",
                pipeline.nodes.front().provenance);
            pipeline.optimization_notes.push_back(
                "count -> constant " + std::to_string(*size) +
                "; reason: inert literal cardinality known");
          }
        } else if (eliminated) {
          pipeline.optimization_notes.push_back(
              "terminal count retains only cardinality-changing stages");
        }
      } else if (mapped_count) {
        disable_count_elimination(
            pipeline, count_elimination_barrier_reason(pipeline), true);
      }
    }

    if (pipeline.semantic_steps.empty()) return;
    FunctionalNodeKind terminal = pipeline.semantic_steps.back().kind;
    if (terminal != FunctionalNodeKind::Any &&
        terminal != FunctionalNodeKind::All)
      return;
    string name = terminal == FunctionalNodeKind::Any ? "any" : "all";
    if (enabled && pipeline.fusion_eligible &&
        callbacks_safe_to_skip(pipeline)) {
      pipeline.short_circuit_terminal = true;
      record_semantic_rewrite(
          pipeline, "short-circuit-" + name,
          "later callback invocations are safe to skip",
          pipeline.semantic_steps.back().provenance);
      pipeline.optimization_notes.push_back(
          "short-circuit " + name + " enabled");
    } else {
      disable_short_circuit(
          pipeline, enabled
              ? skipped_callback_barrier_reason(pipeline)
              : "eager -O0 reference traversal");
    }
  }

  static void plan_terminal_aware_semantics(FunctionalPipeline& pipeline) {
    if (!pipeline.fused || pipeline.semantic_steps.empty()) return;
    FunctionalNodeKind terminal = pipeline.semantic_steps.back().kind;
    if (terminal == FunctionalNodeKind::Count &&
        !pipeline.count_uses_exact_length) {
      bool has_filter = std::any_of(
          pipeline.semantic_steps.begin(), pipeline.semantic_steps.end(),
          [](const FunctionalSemanticStep& step) {
            return step.kind == FunctionalNodeKind::Filter;
          });
      if (has_filter)
        record_semantic_rewrite(
            pipeline, "terminal-filter-count",
            "count matching elements without materializing filter output",
            pipeline.semantic_steps.back().provenance);
    } else if (terminal == FunctionalNodeKind::Sum ||
               terminal == FunctionalNodeKind::Reduce) {
      record_semantic_rewrite(
          pipeline,
          terminal == FunctionalNodeKind::Sum ? "terminal-sum"
                                               : "terminal-reduce",
          "terminal accumulator consumes upstream values without an "
          "intermediate collection",
          pipeline.semantic_steps.back().provenance);
    }
  }

  static bool statement_defines(const Stmt& statement, const string& name) {
    return (statement.kind == Stmt::Kind::Assign ||
            statement.kind == Stmt::Kind::Let ||
            statement.kind == Stmt::Kind::Var) &&
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
      if (simple_binding)
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

      vector<string> collapsed_provenance = producer->lowered_provenance;
      if (collapsed_provenance.empty()) {
        for (const auto& node : producer->nodes)
          collapsed_provenance.insert(collapsed_provenance.end(),
                                      node.provenance.begin(),
                                      node.provenance.end());
      }
      record_semantic_rewrite(
          *producer, "collapse-single-use-temporary",
          "binding '" + binding +
              "' remains semantic but needs no physical collection",
          collapsed_provenance);

      // Terminal plans were formed before lexical graphs were linked. Recheck
      // any transform that can now omit upstream callback executions against
      // the combined graph rather than just the terminal statement.
      if (consumer->count_uses_exact_length) {
        if (producer->fusion_eligible)
          eliminate_trailing_maps_for_downstream_count(*producer);
        bool upstream_preserves_exact_count =
            producer->semantic_steps.empty();
        if (upstream_preserves_exact_count) {
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
          if (auto size = inert_literal_vector_size(
                  producer->source_expression)) {
            consumer->count_uses_known_size = true;
            consumer->known_source_size = *size;
            record_semantic_rewrite(
                *consumer, "known-size-count",
                "virtual upstream has inert literal cardinality " +
                    std::to_string(*size),
                producer->nodes.front().provenance);
          }
        } else {
          string reason = callback_may_diverge(*producer)
              ? "callback may diverge"
              : "upstream stage changes cardinality";
          disable_count_elimination(
              *consumer, reason,
              std::any_of(
                  producer->semantic_steps.begin(),
                  producer->semantic_steps.end(),
                  [](const FunctionalSemanticStep& step) {
                    return step.kind == FunctionalNodeKind::Map;
                  }) || has_map_stage(*consumer));
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
      record_semantic_rewrite(
          *consumer, "consume-virtual-temporary",
          "continued pipeline through single-use binding '" + binding + "'",
          collapsed_provenance);
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
      group.source_file = initial.statement->source_file;
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
        if (node.semantic_work_eliminated) {
          node.materialization = FunctionalMaterializationKind::Virtual;
          node.materialization_reason = node.semantic_elimination_reason;
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
        if (node.semantic_work_eliminated) {
          out << " semantic-work=eliminated";
          if (!node.semantic_elimination_reason.empty())
            out << " semantic-reason=" << node.semantic_elimination_reason;
        }
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
      out << "  semantic-plan:";
      if (pipeline.semantic_steps.empty()) out << " <no element work>";
      out << "\n";
      for (const auto& step : pipeline.semantic_steps) {
        string name = functional_node_name(step.kind);
        if (step.source_node_indices.size() > 1 &&
            step.kind == FunctionalNodeKind::Map)
          name = "ComposedMap";
        else if (step.source_node_indices.size() > 1 &&
                 step.kind == FunctionalNodeKind::Filter)
          name = "ComposedFilter";
        out << "    " << name << " origins=";
        for (size_t index = 0; index < step.provenance.size(); ++index) {
          if (index) out << ",";
          out << step.provenance[index];
        }
        out << "\n";
      }
    }
    for (const auto& rewrite : pipeline.semantic_rewrites) {
      out << "  semantic-opt: " << rewrite.name;
      if (!rewrite.detail.empty()) out << "; " << rewrite.detail;
      if (!decisions_only && !rewrite.provenance.empty()) {
        out << "; provenance=";
        for (size_t index = 0; index < rewrite.provenance.size(); ++index) {
          if (index) out << ",";
          out << rewrite.provenance[index];
        }
      }
      out << "\n";
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

struct OptimizationPlan { bool optimizations_enabled = false; };

enum class ProgramGenerationMode { Application, Tests, Benchmarks };

class Generator {
 public:
  Generator(const Program& p, const OptimizationPlan&,
            bool debug_build = false,
            ProgramGenerationMode mode = ProgramGenerationMode::Application,
            vector<string> rust_dependencies = {},
            const Program* resolution_program = nullptr,
            bool emit_static_specializations = true,
            bool public_specializations = false)
      : p_(p),
        debug_build_(debug_build), mode_(mode),
        rust_dependencies_(std::move(rust_dependencies)),
        emit_static_specializations_(emit_static_specializations),
        public_specializations_(public_specializations) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    const Program& semantic_program = resolution_program ? *resolution_program : p;
    semantic_program_ = &semantic_program;
    std::set<string> used_domain_names;
    for (const auto& domain : semantic_program.domains) used_domain_names.insert(domain.name);
    for (const auto& specialization : semantic_program.domain_specializations) {
      if (!specialization.materialized_layout) continue;
      auto source = std::find_if(
          semantic_program.domains.begin(), semantic_program.domains.end(),
          [&](const Domain& domain) {
            return domain.name == specialization.source_domain;
          });
      if (source == semantic_program.domains.end()) continue;
      Domain specialized = *source;
      string backend_name = specialization.source_domain + "__" +
          specialization.instance;
      if (used_domain_names.count(backend_name)) {
        backend_name += "__specialized_" +
            stable_hash(specialization.source_domain + "\n" +
                        specialization.instance).substr(0, 12);
        size_t suffix = 0;
        string candidate = backend_name;
        while (used_domain_names.count(candidate))
          candidate = backend_name + "_" + std::to_string(++suffix);
        backend_name = std::move(candidate);
      }
      specialized.name = backend_name;
      used_domain_names.insert(specialized.name);
      for (auto& field : specialized.state) {
        auto type = specialization.state_types.find(field.name);
        if (type != specialization.state_types.end()) field.type = type->second;
      }
      for (auto& handler : specialized.handlers) {
        auto parameters = specialization.handler_parameter_types.find(handler.name);
        if (parameters != specialization.handler_parameter_types.end()) {
          for (size_t index = 0;
               index < handler.params.size() && index < parameters->second.size();
               ++index)
            if (!parameters->second[index].empty())
              handler.params[index].type = parameters->second[index];
        }
        auto reply = specialization.handler_reply_types.find(handler.name);
        if (reply != specialization.handler_reply_types.end())
          handler.reply_type = reply->second;
      }
      specialized_domains_.push_back(std::move(specialized));
      specialization_sources_[specialized_domains_.back().name] = specialization.source_domain;
      specialization_names_[specialization.source_domain + "\n" +
                            specialization.instance] =
          specialized_domains_.back().name;
    }
    for (const auto& d : specialized_domains_) {
      domains_[d.name] = &d;
    }
    for (const auto& o : p.objects) objects_[o.name] = &o;
    for (const auto& f : p.functions) functions_[f.name] = &f;
    if (resolution_program) {
      for (const auto& d : resolution_program->domains)
        domains_.emplace(d.name, &d);
      for (const auto& o : resolution_program->objects)
        objects_.emplace(o.name, &o);
      for (const auto& f : resolution_program->functions)
        functions_.emplace(f.name, &f);
    }
  }

  string generate() {
    CompilerStageTimer timer("rust_generation");
    if (!p_.concrete_domain_graph.instances.empty() && !p_.concrete_domain_graph.closed)
      throw std::runtime_error("internal error: lowering requires a closed ConcreteDomainGraph");
    validate_synchronization_lowering(*semantic_program_);
    std::ostringstream o;
    o << "// Generated by Moss v0.1. Do not edit by hand.\n";
    o << "// Synchronous domain calls enter compiler-planned handler-level 2PL.\n";
    for (const auto& domain : p_.domains) o << "// Moss backend plan: " << domain.name << " = Handler2PL.\n";
    o << "#![allow(non_snake_case)]\n#![allow(non_camel_case_types)]\n#![allow(dead_code)]\n";
    o << "#![allow(unused_imports)]\n#![allow(unused_mut)]\n#![allow(unused_variables)]\n\n";
    for (const auto& dependency : rust_dependencies_) {
      string crate = dependency == "__moss_specializations__"
          ? "moss_specializations" : tooling_name("moss_" + dependency);
      o << "extern crate " << crate << ";\n";
      // Moss's namespace pass has already qualified imported names.  Bringing
      // the dependency's public Rust surface into this crate keeps those
      // generated expressions direct calls to the materialized dependency
      // symbol; no dependency implementation is copied here.
      o << "use " << crate << "::*;\n";
    }
    if (!rust_dependencies_.empty()) o << "\n";
    o << "use std::collections::{HashMap, VecDeque};\n";
    o << "use std::sync::Arc;\n";
    o << "fn __moss_require_send<T: Send>() {}\n\n";

    o << handler_runtime_rust();
    for (const auto& t : p_.objects) gen_object(o, t);
    std::map<string, const ObjectType*> view_objects(objects_.begin(), objects_.end());
    for (const auto& entry : view_objects) {
      if (entry.second->fields.empty()) continue;
      bool owns = std::any_of(p_.objects.begin(), p_.objects.end(), [&](const auto& object) { return object.name == entry.first; });
      gen_object_access(o, *entry.second, owns);
    }
    for (const auto& f : p_.functions) gen_function(o, f);
    auto body_owner = [&](const Domain& d) {
      return !p_.explicit_module || starts_with(d.name, p_.module_name + "__");
    };
    for (const auto& d : p_.domains) if (body_owner(d)) gen_domain_body_abi(o, d);
    for (const auto& d : specialized_domains_)
      if (owns_specialization(d) && body_owner(d)) gen_domain_body_abi(o, d);
    if (!p_.explicit_module || p_.main) {
      // The final application owns every physical layout, including source-free
      // provider domains. Provider bodies expose only semantic borrowed access.
      for (const auto& d : p_.domains) gen_ref_decl(o, d);
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) gen_ref_decl(o, d);
      for (const auto& d : p_.domains) gen_nominal_handle(o, d);
      for (const auto& d : p_.domains) {
        gen_domain(o, d);
        gen_route_contract(o, d, d.name + "Ref");
        if (has_domain_specializations(d.name)) gen_route_contract(o, d, d.name + "Handle");
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) {
        gen_domain(o, d);
        // Exact specialized routes use their nominal source contract where
        // parameter/reply types are compatible (existing handle adapter).
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
    }
    if (mode_ == ProgramGenerationMode::Tests)
      gen_test_harness(o);
    else if (mode_ == ProgramGenerationMode::Benchmarks)
      gen_benchmark_harness(o);
    else if (p_.main)
      gen_main(o, *p_.main);
    else
      o << "fn main() {}\n";
    return o.str();
  }

 private:
  const Program& p_;
  const Program* semantic_program_ = nullptr;
  std::set<string> write_through_parameters_;
  string construction_binding_;
  bool view_domain_ = false;
  bool view_domain_access_ = false;
  bool view_method_ = false;
  std::set<string> view_parameters_;
  // Non-Copy incoming message parameters use a borrowed internal ABI.  This
  // is physical lowering metadata only: the checked Moss fact remains an
  // immutable value snapshot.  Keeping the set while rendering a handler
  // prevents a forwarded String/Vector/etc. from becoming &&T.
  std::set<string> borrowed_message_parameters_;
  mutable bool mutable_projection_ = false;
  bool debug_build_ = false;
  ProgramGenerationMode mode_ = ProgramGenerationMode::Application;
  vector<string> rust_dependencies_;
  bool emit_static_specializations_ = true;
  bool public_specializations_ = false;
  bool benchmark_body_ = false;
  bool exported_domain_bridge_body_ = false;
  std::unordered_map<string, const Domain*> domains_;
  vector<Domain> specialized_domains_;
  std::unordered_map<string,string> specialization_sources_;
  std::unordered_map<string,string> specialization_names_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const Function*> functions_;
  struct DomainInstanceBinding {
    // This is the semantic specialization key carried by a source binding;
    // it is intentionally independent of the generated Rust type name.
    string source_domain;
    string instance;
  };
  std::unordered_map<string, DomainInstanceBinding> domain_instance_bindings_;
  std::set<string> ambiguous_domain_instance_bindings_;
  size_t assertion_temp_ = 0;

  bool owns_specialization(const Domain& specialized) const {
    const string& source = specialization_sources_.at(specialized.name);
    return std::any_of(p_.domains.begin(), p_.domains.end(),
        [&](const Domain& domain) { return domain.name == source; });
  }

  bool has_domain_specializations(const string& domain) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == domain;
        });
  }

  bool is_domain_specialization_instance(const string& source_domain,
                                         const string& instance) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == source_domain &&
              specialization.instance == instance;
        });
  }

  string nominalized_generated_argument_type(
      const string& expression, const string& inferred) const {
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (plain_identifier(binding) && identity != domain_instance_bindings_.end() &&
        is_domain_specialization_instance(identity->second.source_domain,
                                          identity->second.instance))
      return identity->second.source_domain;
    return inferred;
  }

  void remember_domain_instance_binding(const string& binding,
                                        const string& source_domain,
                                        const string& instance) {
    if (ambiguous_domain_instance_bindings_.count(binding)) return;
    auto found = domain_instance_bindings_.find(binding);
    if (found == domain_instance_bindings_.end()) {
      domain_instance_bindings_[binding] = {source_domain, instance};
      return;
    }
    if (found->second.source_domain != source_domain ||
        found->second.instance != instance) {
      domain_instance_bindings_.erase(found);
      ambiguous_domain_instance_bindings_.insert(binding);
    }
  }

  void forget_domain_instance_binding(const string& binding) {
    domain_instance_bindings_.erase(binding);
    ambiguous_domain_instance_bindings_.erase(binding);
  }

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

  static void tooling_begin(std::ostringstream& o, int spaces,
                            const string& kind, const string& semantic_identity,
                            const string& generated_symbol,
                            const string& native_symbol = "") {
    o << string(spaces, ' ') << "// Moss tooling begin|" << kind << "|"
      << semantic_identity << "|" << generated_symbol << "|" << native_symbol
      << "\n";
  }

  static void tooling_end(std::ostringstream& o, int spaces,
                          const string& semantic_identity) {
    o << string(spaces, ' ') << "// Moss tooling end|" << semantic_identity
      << "\n";
  }

  void debug_symbol_attributes(std::ostringstream& o, int spaces,
                               const string& native_symbol,
                               bool can_export = true) const {
    if (debug_build_) o << string(spaces, ' ') << "#[inline(never)]\n";
    if (can_export)
      o << string(spaces, ' ') << "#[export_name = \"" << native_symbol
        << "\"]\n";
  }

  static string function_semantic_identity(
      const Function& function,
      const FunctionSpecialization* specialization = nullptr) {
    return functional_function_context(function, specialization) + "@" +
        std::to_string(function.line);
  }

  static string method_semantic_identity(const ObjectType& object,
                                         const Method& method) {
    return functional_method_context(object, method) + "@" +
        std::to_string(method.line);
  }

  static string handler_semantic_identity(const Domain& domain,
                                          const Handler& handler) {
    return functional_handler_context(domain, handler) + "@" +
        std::to_string(handler.line);
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
    if (domains_.count(x))
      return has_domain_specializations(x) ? x + "Handle" : x + "Ref";
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

  string default_value(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int") return "0";
    if (t == "float") return "0.0";
    if (t == "bool") return "false";
    if (t == "string") return "String::new()";
    if (starts_with(t, "seq[")) return "Vec::new()";
    if (starts_with(t, "table[")) return "HashMap::new()";
    if (starts_with(t, "option[")) return "None";
    auto object = objects_.find(t);
    if (object != objects_.end()) {
      string result = rust_type(t) + " { ";
      for (const auto& field : object->second->fields)
        result += field.name + ": " + default_value(field.type) + ", ";
      return result + "}";
    }
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
    if (effect == Effect::Write) return true;
    const auto& parameter = function.params[index];
    auto ops = constraint_ops(function, parameter.name);
    if (parameter.type.empty() && !ops.empty() && !ops.count("[]")) return false;
    string candidate = parameter_type.empty() ? actual_type : parameter_type;
    if (candidate.empty() || starts_with(candidate, "_") ||
        (parameter.type.empty() && ops.empty() && !function.static_dispatch)) return false;
    return borrowable_type(candidate);
  }

  string nominal_domain_handle_argument(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id = 0) const {
    string rendered = expr(expression, d, locals, types, functional_pipeline_id);
    string nominal = canonical_type_name(type);
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (types && plain_identifier(binding) && domains_.count(nominal) &&
        identity != domain_instance_bindings_.end() &&
        identity->second.source_domain == nominal &&
        is_domain_specialization_instance(nominal, identity->second.instance)) {
      rendered = nominal + "Handle::from(" + rendered + ")";
    }
    return rendered;
  }

  string write_call_place(const string& argument, const Domain* d,
                          const std::set<string>& locals,
                          const std::unordered_map<string,string>* types) const {
    string base, index;
    if (!parse_index(trim(argument), base, index)) return place_expr(argument, d, locals, types);
    string storage = write_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get_mut(&(" + key + ")).expect(\"Moss missing map key\")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string read_call_place(const string& argument, const Domain* d,
                         const std::set<string>& locals,
                         const std::unordered_map<string,string>* types) const {
    string base, index;
    if (!parse_index(trim(argument), base, index)) return expr(argument, d, locals, types);
    string storage = read_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get(&(" + key + ")).expect(\"Moss missing map key\")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string function_call_argument(const Function& function, size_t index,
                                const string& argument, const Domain* d,
                                const std::set<string>& locals,
                                const std::unordered_map<string,string>* types,
                                size_t functional_pipeline_id = 0) const {
    string rendered = nominal_domain_handle_argument(
        argument, index < function.params.size() ? function.params[index].type : "",
        d, locals, types, functional_pipeline_id);
    if (index >= function.params.size()) return rendered;
    Effect effect = function_effect(function, index);
    string parameter_type = function.params[index].type;
    string actual_type = generated_expr_type(argument, types).value_or("");
    if (!should_borrow_function_parameter(function, index, parameter_type, actual_type))
      return rendered;
    // Non-object payloads arrive through the internal handler ABI as `&T`.
    // Preserve that borrow when handing it to a READ helper. Object payloads
    // retain the extra reference below because view helpers take
    // `&impl MossAccess_T`.
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(actual_type))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
  }

  string method_call_argument(const Method& method, size_t index,
                              const string& argument, const Domain* d,
                              const std::set<string>& locals,
                              const std::unordered_map<string,string>* types,
                              size_t functional_pipeline_id = 0) const {
    string rendered = nominal_domain_handle_argument(
        argument, index < method.params.size() ? method.params[index].type : "",
        d, locals, types, functional_pipeline_id);
    if (index >= method.params.size()) return rendered;
    Effect effect = method_effect(method, index);
    string type = method.params[index].type;
    if (effect == Effect::Consume || (effect != Effect::Write && !borrowable_type(type))) return rendered;
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(generated_expr_type(argument, types).value_or("")))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
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
    if (types && starts_with(original, "message ")) {
      string receiver, handler;
      vector<string> arguments;
      if (parse_member_call(trim(original.substr(8)), receiver, handler, arguments)) {
        auto type = types->find(receiver);
        if (type != types->end()) {
          auto domain = domains_.find(type->second);
          if (domain != domains_.end())
            if (const Handler* target = find_handler(*domain->second, handler))
              return target->reply_type.value_or("unit");
        }
      }
      return std::nullopt;
    }
    if (auto pipeline = generated_functional_pipeline_type(original, types))
      return pipeline;
    string value = normalize_pipeline(std::move(original));
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')' &&
           matching_paren(value, 0) == value.size() - 1)
      value = trim(value.substr(1, value.size() - 2));
    if (value == "true" || value == "false") return string("bool");
    if (value == "None") return string("_none");
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
          argument_types.push_back(nominalized_generated_argument_type(
              argument, generated_expr_type(argument, types).value_or("")));
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
      if (callee == "Some" && call_args.size() == 1) {
        auto value_type = generated_expr_type(call_args.front(), types);
        return value_type ? "option[" + *value_type + "]" : "option[_]";
      }
      if (callee == "range" && (call_args.size() == 2 || call_args.size() == 3))
        return "range[int]";
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

  static LoweredFunctionalPipeline lower_functional_semantic_plan(
      const ParsedFunctionalPipeline& parsed,
      const FunctionalPipeline& planned) {
    LoweredFunctionalPipeline lowered;
    lowered.source = parsed.source;
    for (const auto& semantic_step : planned.semantic_steps) {
      if (semantic_step.kind == FunctionalNodeKind::Source ||
          semantic_step.source_node_indices.empty())
        throw std::runtime_error(
            "internal error: invalid functional semantic step");
      LoweredFunctionalStage stage;
      stage.kind = semantic_step.kind;
      for (size_t node_index : semantic_step.source_node_indices) {
        if (node_index == 0 || node_index > parsed.stages.size() ||
            node_index >= planned.nodes.size())
          throw std::runtime_error(
              "internal error: functional semantic step has stale source node");
        const ParsedFunctionalStage& operation =
            parsed.stages[node_index - 1];
        if (operation.kind != semantic_step.kind ||
            planned.nodes[node_index].kind != semantic_step.kind)
          throw std::runtime_error(
              "internal error: functional semantic rewrite changed operator kind");
        stage.operations.push_back(operation);
      }
      lowered.stages.push_back(std::move(stage));
    }
    return lowered;
  }

  static const ParsedFunctionalStage& terminal_operation(
      const LoweredFunctionalPipeline& pipeline) {
    if (pipeline.stages.empty() ||
        pipeline.stages.back().operations.size() != 1 ||
        !functional_terminal_kind(pipeline.stages.back().kind))
      throw std::runtime_error(
          "internal error: lowered functional pipeline has no terminal");
    return pipeline.stages.back().operations.front();
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
            (effect == Effect::Write || borrowable_type(argument.type));
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
      bool borrow = effect != Effect::Consume && (effect == Effect::Write || borrowable_type(argument.type));
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
    vector<LoweredFunctionalPipeline> pipelines;
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
      LoweredFunctionalPipeline lowered =
          lower_functional_semantic_plan(*parsed, *plan);
      if (lowered.stages.empty() ||
          !functional_terminal_kind(lowered.stages.back().kind))
        throw std::runtime_error(
            "internal error: rewritten shared functional consumer lost terminal");
      for (const auto& stage : lowered.stages) {
        if (stage.kind != FunctionalNodeKind::Map) continue;
        for (const auto& operation : stage.operations) {
          auto result = generated_callable_result(
              operation.arguments.front(), {element_type}, &types);
          if (!result)
            throw std::runtime_error(
                "internal error: untyped shared functional map");
          element_type = *result;
        }
      }
      plans.push_back(plan);
      pipelines.push_back(std::move(lowered));
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
      const auto& terminal = terminal_operation(pipelines[index]);
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
        const auto& terminal = terminal_operation(pipeline);
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
              for (const auto& operation : stage.operations) {
                string next = "__moss_shared_value_" +
                    std::to_string(group.transient_id) + "_" +
                    std::to_string(consumer_index) + "_" +
                    std::to_string(++map_number);
                out << statement_indent << "let " << next << " = "
                    << render_functional_callable(
                           operation.arguments.front(), {current}, domain,
                           locals, &types)
                    << ";\n";
                auto mapped_type = generated_callable_result(
                    operation.arguments.front(), {current.type}, &types);
                if (!mapped_type)
                  throw std::runtime_error(
                      "internal error: untyped shared functional map result");
                current = {next, *mapped_type, false};
              }
              break;
            }
            case FunctionalNodeKind::Filter: {
              out << statement_indent << "if !(";
              for (size_t predicate = 0;
                   predicate < stage.operations.size(); ++predicate) {
                if (predicate) out << " && ";
                out << "("
                    << render_functional_callable(
                           stage.operations[predicate].arguments.front(),
                           {current}, domain, locals, &types)
                    << ")";
              }
              out << ") { break " << label << "; }\n";
              break;
            }
            case FunctionalNodeKind::Reduce: {
              const auto& operation = stage.operations.front();
              auto accumulator_type = generated_expr_type(
                  operation.arguments.front(), &types).value_or(current.type);
              out << statement_indent << result << " = "
                  << render_functional_callable(
                         operation.arguments[1],
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
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types);
              out << statement_indent << "if " << predicate << " { "
                  << result << " = true; }\n";
              break;
            }
            case FunctionalNodeKind::All: {
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types);
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
    if (planned) {
      out << " (plan %" << planned->transient_id
          << ", semantic " << planned->semantic_identity << ")";
    }
    out << "\n";
    if (planned && !planned->nodes.empty())
      out << "        // Moss functional origin|"
          << planned->nodes.front().semantic_identity << "\n";
    out << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";

    size_t stage_number = 0;
    for (size_t pipeline_stage = 0; pipeline_stage < pipeline.stages.size();
         ++pipeline_stage) {
      const auto& stage = pipeline.stages[pipeline_stage];
      if (stage.kind != FunctionalNodeKind::Map &&
          stage.kind != FunctionalNodeKind::Filter)
        continue;
      if (planned && pipeline_stage + 1 < planned->nodes.size())
        out << "        // Moss functional origin|"
            << planned->nodes[pipeline_stage + 1].semantic_identity << "\n";
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
    if (planned && pipeline.stages.size() < planned->nodes.size())
      out << "        // Moss functional origin|"
          << planned->nodes[pipeline.stages.size()].semantic_identity << "\n";
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
      const LoweredFunctionalPipeline& pipeline, const Domain* domain,
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
      for (const auto& operation : stage.operations) {
        auto result = generated_callable_result(
            operation.arguments.front(), {final_element_type}, types);
        if (!result)
          throw std::runtime_error("internal error: untyped fused map result");
        final_element_type = *result;
      }
    }
    FunctionalNodeKind last_kind = pipeline.stages.empty()
        ? FunctionalNodeKind::Source : pipeline.stages.back().kind;
    bool terminal = functional_terminal_kind(last_kind);
    const ParsedFunctionalStage* last = terminal
        ? &terminal_operation(pipeline) : nullptr;
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
    else if (last_kind == FunctionalNodeKind::Reduce)
      out << "        let mut __moss_result = "
          << expr(last->arguments.front(), domain, locals, types) << ";\n";
    else if (last_kind == FunctionalNodeKind::Sum)
      out << "        let mut __moss_result = "
          << functional_zero(final_element_type) << ";\n";
    else if (last_kind == FunctionalNodeKind::Count)
      out << "        let mut __moss_result = 0_i64;\n";
    else
      out << "        let mut __moss_result = "
          << (last_kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";
    bool source_copy = copy_type(current_type);
    out << "        for __moss_item_ref in __moss_pipeline_source.iter() {\n"
        << "            let __moss_value_0 = "
        << (source_copy ? "*" : "") << "__moss_item_ref;\n";
    FunctionalValue current{"__moss_value_0", current_type, !source_copy};
    size_t map_number = 0;
    for (const auto& stage : pipeline.stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          for (const auto& operation : stage.operations) {
            string next = "__moss_value_" + std::to_string(++map_number);
            out << "            let " << next << " = "
                << render_functional_callable(
                       operation.arguments.front(), {current}, domain,
                       locals, types)
                << ";\n";
            auto result = generated_callable_result(
                operation.arguments.front(), {current.type}, types);
            if (!result)
              throw std::runtime_error(
                  "internal error: untyped fused map result");
            current = {next, *result, false};
          }
          break;
        }
        case FunctionalNodeKind::Filter: {
          out << "            if !(";
          for (size_t predicate = 0;
               predicate < stage.operations.size(); ++predicate) {
            if (predicate) out << " && ";
            out << "("
                << render_functional_callable(
                       stage.operations[predicate].arguments.front(),
                       {current}, domain, locals, types)
                << ")";
          }
          out << ") { continue; }\n";
          break;
        }
        case FunctionalNodeKind::Reduce: {
          const auto& operation = stage.operations.front();
          auto accumulator_type = generated_expr_type(
              operation.arguments.front(), types).value_or(current.type);
          out << "            __moss_result = "
              << render_functional_callable(
                     operation.arguments[1],
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
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types);
          out << "            if " << predicate
              << " { __moss_result = true;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::All: {
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types);
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
    LoweredFunctionalPipeline lowered =
        lower_functional_semantic_plan(*pipeline, *planned);
    if (planned->virtual_upstream_pipeline_id) {
      const FunctionalPipeline* upstream = planned_functional_pipeline(
          planned->virtual_upstream_pipeline_id);
      auto upstream_parsed = upstream
          ? parse_functional_pipeline(upstream->expression) : std::nullopt;
      if (!upstream_parsed || upstream_parsed->stages.empty() ||
          functional_terminal_kind(upstream_parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid virtual functional upstream plan");
      LoweredFunctionalPipeline upstream_lowered =
          lower_functional_semantic_plan(*upstream_parsed, *upstream);
      vector<LoweredFunctionalStage> stages =
          std::move(upstream_lowered.stages);
      stages.insert(stages.end(), lowered.stages.begin(), lowered.stages.end());
      lowered.source = upstream_parsed->source;
      lowered.stages = std::move(stages);
    }
    if (planned->count_uses_exact_length) {
      std::ostringstream out;
      if (planned->count_uses_known_size)
        out << "{\n        // Moss backend: COUNT -> KNOWN SIZE; inert literal work eliminated";
      else
        out << "{\n        // Moss backend: COUNT -> EXACT LENGTH; mapped outputs are dead";
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
      if (planned->count_uses_known_size)
        out << "\n        " << planned->known_source_size << "_i64\n    }";
      else
        out << "\n"
            << "        let __moss_pipeline_source = &("
            << expr(lowered.source, domain, locals, types) << ");\n"
            << "        __moss_pipeline_source.len() as i64\n    }";
      return out.str();
    }
    if (planned && planned->fused)
      return gen_fused_functional_pipeline(lowered, domain, locals, types,
                                           planned);
    return gen_eager_functional_pipeline(*pipeline, domain, locals, types,
                                         planned);
  }

  string expr(string e, const Domain* d, const std::set<string>& locals,
              const std::unordered_map<string,string>* types = nullptr,
              size_t functional_pipeline_id = 0,
              const vector<size_t>* message_argument_plans = nullptr) const {
    e = trim(std::move(e));
    if (auto place = view_place(e, d, locals, types)) return *place;
    if (write_through_parameters_.count(e) && types && types->count(e) && copy_type(types->at(e)))
      return "*" + e;
    // `message` is a Moss expression as well as a statement.  Assignment
    // statements have a dedicated lowering path, but expression positions
    // (for example `echo message d.Get()`) must retain the same synchronous
    // completion and reply semantics through synchronized handler entry.
    if (starts_with(e, "message ")) {
      string receiver, handler;
      vector<string> arguments;
      if (!parse_member_call(trim(e.substr(8)), receiver, handler, arguments) ||
          !types || !plain_identifier(receiver))
        throw std::runtime_error("internal error: malformed message expression");
      auto receiver_type = types->find(receiver);
      if (receiver_type == types->end())
        throw std::runtime_error("internal error: unresolved message receiver");
      auto domain_it = domains_.find(canonical_type_name(receiver_type->second));
      if (domain_it == domains_.end())
        throw std::runtime_error("internal error: message expression receiver is not a domain");
      const Domain& target_domain = *domain_it->second;
      const Handler* target_handler = find_handler(target_domain, handler);
      if (!target_handler)
        throw std::runtime_error("internal error: unresolved message expression handler");
      if (target_domain.exported && !exported_domain_bridge_body_) {
        std::ostringstream call;
        call << expr(receiver, d, locals, types) << ".__moss_message_" << handler << "(";
        for (size_t index = 0; index < arguments.size(); ++index) {
          if (index) call << ", ";
          call << message_arg(arguments[index], target_handler->params.at(index).type,
                              d, locals, types, message_argument_plans ? message_argument_plans->at(index) : 0);
        }
        call << ")";
        return call.str();
      }
      std::ostringstream rendered;
      rendered << expr(receiver, d, locals, types) << "." << handler << "_shared(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << shared_message_arg(arguments[index], target_handler->params.at(index).type,
                                       d, locals, types,
                                       message_argument_plans ? message_argument_plans->at(index) : 0);
      }
      rendered << ")";
      if (target_handler->reply_type)
        rendered << ".unwrap_or_else(|| std::process::abort())";
      return rendered.str();
    }
    if (auto functional = functional_expr(
            e, d, locals, types, functional_pipeline_id))
      return *functional;
    e = normalize_pipeline(std::move(e));
    if (e.size() >= 6 && e.find(".pop()") != string::npos) {
      auto pos = e.find(".pop()"); e.replace(pos, 6, ".pop_front()");
    }
    // Minimal surface rewrites.
    if (e == "true" || e == "false") return e;
    if (e == "None") return "None";
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
      left = value_from_borrowed_message_parameter(comparison->left, left, types);
      right = value_from_borrowed_message_parameter(comparison->right, right, types);
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
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
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
      string indexed = "(" + expr(ib, d, locals, types) + ")[" + ir + "]";
      return borrowed_view_expression(ib, d, locals, types) ? indexed : "(" + indexed + ").clone()";
    }

    string member_receiver, member_name;
    vector<string> member_arguments;
    if (parse_member_call(e, member_receiver, member_name, member_arguments)) {
      string receiver_expression = method_receiver_place(member_receiver, member_name, member_arguments, d, locals, types);
      if (starts_with(receiver_expression, "*")) receiver_expression = "(" + receiver_expression + ")";
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
          rendered << receiver_expression << "." << viewed_method_name(member_receiver, member_name, member_arguments, d, locals, types) << "(";
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
      rendered << receiver_expression << "." << viewed_method_name(member_receiver, member_name, member_arguments, d, locals, types) << "(";
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
      if (builtin == "Some" && builtin_args.size() == 1)
        return "Some(" + expr(builtin_args.front(), d, locals, types) + ")";
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
        r << viewed_function_name(head, call_args, d, locals, types) << "(";
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

    // Expand borrowed access paths before the ordinary state identifier rewrite.
    if (view_domain_ || view_method_ || !view_parameters_.empty()) {
      string result;
      bool quoted = false, escaped = false;
      for (size_t i = 0; i < e.size();) {
        char ch = e[i];
        if (quoted) {
          result += ch; ++i;
          if (escaped) escaped = false;
          else if (ch == '\\') escaped = true;
          else if (ch == '\"') quoted = false;
        } else if (ch == '\"') { quoted = true; result += ch; ++i; }
        else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
          size_t end = i + 1;
          while (end < e.size() && (std::isalnum(static_cast<unsigned char>(e[end])) || e[end] == '_' || e[end] == '.')) ++end;
          string path = e.substr(i, end - i);
          auto place = view_place(path, d, locals, types);
          result += place ? *place : path; i = end;
        } else { result += ch; ++i; }
      }
      e = result;
    }
    // Replace domain state identifiers with state.<name>, respecting basic identifier boundaries.
    if (d && !view_domain_ && !view_method_) {
      std::set<string> fields;
      for (const auto& f : d->state) fields.insert(f.name);
      for (const auto& route : d->routes) fields.insert(route.name);
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
    for (const auto& parameter : write_through_parameters_)
      e = replace_unqualified_word(e, parameter, "(*" + parameter + ")");
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

  // Message values have one Moss-level rule (an immutable value snapshot),
  // but two backend representations.  The owned form is retained at an ABI
  // boundary and for replies.  The borrowed forms are valid only while an
  // internal synchronous `_shared` call is active; Handler2PL retains every
  // guard needed by a state view until that nested call returns.
  enum class MessagePayloadLowering {
    CopyValue,
    BorrowOwned,
    BorrowView,
    MaterializeOwned,
  };

  bool borrowed_message_parameter(const string& expression) const {
    return plain_identifier(trim(expression)) &&
        borrowed_message_parameters_.count(trim(expression));
  }

  bool borrowed_message_type(const string& type) const {
    const string concrete = canonical_type_name(type);
    return !concrete.empty() && !copy_type(concrete) && concrete != "_";
  }

  string shared_message_parameter(const Param& parameter) const {
    const string type = canonical_type_name(parameter.type);
    if (!borrowed_message_type(type))
      return parameter.name + ": " + rust_type(type);
    // A view is a static access capability. The internal ABI receives an
    // immutable reference whether this is an owned object, a temporary state
    // projection, or an already-borrowed incoming payload.
    if (view_object_type(type))
      return parameter.name + ": &impl " + access_trait(type);
    return parameter.name + ": &" + rust_type(type);
  }

  string shared_message_forward_argument(const Param& parameter,
                                         bool from_owned_boundary) const {
    if (!borrowed_message_type(parameter.type)) return parameter.name;
    // An exported/native boundary owns its Rust argument.  It deliberately
    // materializes that boundary, then lends it to the controlled internal
    // call.  `_shared` forwarding already receives the required borrowed or
    // access-view representation.
    return from_owned_boundary ? "&" + parameter.name : parameter.name;
  }

  MessagePayloadLowering message_payload_lowering(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      bool owned_boundary) const {
    if (owned_boundary) return MessagePayloadLowering::MaterializeOwned;
    if (!borrowed_message_type(type)) return MessagePayloadLowering::CopyValue;
    if (is_object_view(expression, d, locals, types))
      return MessagePayloadLowering::BorrowView;
    return MessagePayloadLowering::BorrowOwned;
  }

  string message_arg(const string& e, const string& type, const Domain* d,
                     const std::set<string>& locals,
                     const std::unordered_map<string,string>* types = nullptr,
                     size_t functional_pipeline_id = 0) const {
    string r = nominal_domain_handle_argument(e, type, d, locals, types, functional_pipeline_id);
    string t = trim(type);
    if (is_object_view(e, d, locals, types)) return "(" + r + ").__moss_value()";
    if (copy_type(t) || t == "_") return r;
    // Messages are the explicit Moss semantic copy boundary.  A payload is
    // detached from the sender even when the source binding remains available;
    // the generated clone is an implementation of that boundary, never an
    // implicit copy for an ordinary local call.
    return "(" + r + ").clone()";
  }

  string shared_message_arg(const string& e, const string& type,
                            const Domain* d, const std::set<string>& locals,
                            const std::unordered_map<string,string>* types = nullptr,
                            size_t functional_pipeline_id = 0) const {
    const auto lowering = message_payload_lowering(
        e, type, d, locals, types, false);
    string rendered = nominal_domain_handle_argument(
        e, type, d, locals, types, functional_pipeline_id);
    switch (lowering) {
      case MessagePayloadLowering::CopyValue:
        return rendered;
      case MessagePayloadLowering::BorrowOwned:
        // A temporary owner is naturally extended through this synchronous
        // call.  No clone implements the Moss snapshot here.
        return borrowed_message_parameter(e) ? trim(e) : "&(" + rendered + ")";
      case MessagePayloadLowering::BorrowView:
        // An incoming object payload is already `&impl MossAccess_T` and can
        // be forwarded directly. State projections are compact values holding
        // field borrows, so lend the temporary view for this nested call.
        return borrowed_message_parameter(e) ? trim(e) : "&(" + rendered + ")";
      case MessagePayloadLowering::MaterializeOwned:
        break;
    }
    throw std::runtime_error("internal error: shared message selected owned boundary");
  }

  string value_from_borrowed_message_parameter(
      const string& source, const string& rendered,
      const std::unordered_map<string,string>* types) const {
    auto type = generated_expr_type(source, types);
    if (borrowed_message_parameter(source) && type &&
        !view_object_type(*type))
      return "*(" + rendered + ")";
    return rendered;
  }

  void gen_object(std::ostringstream& o, const ObjectType& t) {
    string type_identity = "type:" + t.name + "@" + std::to_string(t.line);
    tooling_begin(o, 0, "type", type_identity, t.name);
    source_comment(o, 0, t.line, t.header.empty() ? "type " + t.name : t.header);
    backend_comment(o, 0, "value representation for the Moss object; object ownership stays in Moss");
    o << "#[derive(Clone, Debug)]\n"
      << ((t.exported || p_.explicit_module) ? "pub " : "") << "struct " << t.name << " {\n";
    for (const auto& f : t.fields) {
      source_comment(o, 4, f.line, f.header.empty() ? f.name + ": " + f.type : f.header);
      o << "    " << f.name << ": " << rust_type(f.type) << ",\n";
    }
    o << "}\n\n";
    tooling_end(o, 0, type_identity);
    // Native provider storage decomposition moves fields without exposing Moss
    // field access or requiring the consumer to read Rust-private fields.
    o << "impl " << t.name << " {\n    " << ((t.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_into_parts(self) -> (";
    for (const auto& field : t.fields) o << rust_type(field.type) << ",";
    o << ") { (";
    for (const auto& field : t.fields) o << "self." << field.name << ",";
    o << ") }\n}\n";
    for (const auto& method : t.methods) for (bool view : {false, true}) {
      bool needs_view = false;
      for (size_t i = 0; i < method.params.size(); ++i)
        needs_view |= method_effect(method, i) != Effect::Consume && view_object_type(method.params[i].type);
      if (view && !needs_view) continue;
      const string method_name = (view ? "__moss_view_" : "") + method.name;
      string semantic_identity = method_semantic_identity(t, method);
      string generated_symbol = t.name + "::" + method_name;
      string native_symbol = tooling_native_symbol(
          "method", t.name + "__" + method.name, semantic_identity);
      tooling_begin(o, 0, "method", semantic_identity, generated_symbol,
                    view ? "" : native_symbol);
      o << "impl " << t.name << " {\n";
      source_comment(o, 4, method.line,
                     "fn " + method.name);
      debug_symbol_attributes(o, 4, native_symbol, !view);
      o << "    fn " << method_name;
      if (method.receiver_effect == Effect::Consume) o << "(self";
      else if (method.receiver_effect == Effect::Write) o << "(&mut self";
      else o << "(&self";
      for (const auto& p : method.params) {
        if (p.type.empty()) throw std::runtime_error("unresolved concrete method parameter type: " + method.name + "." + p.name);
        size_t parameter_index = static_cast<size_t>(&p - method.params.data());
        Effect effect = method_effect(method, parameter_index);
        string parameter_type = view && effect != Effect::Consume && view_object_type(p.type)
            ? "impl " + access_trait(p.type) : rust_type(p.type);
        if (effect != Effect::Consume && (effect == Effect::Write || borrowable_type(p.type))) {
          o << ", " << (effect == Effect::Write ? "" : "") << p.name << ": "
            << (effect == Effect::Write ? "&mut " : "&") << parameter_type;
        } else {
          o << ", mut " << p.name << ": " << parameter_type;
        }
      }
      if (method.return_type) o << ") -> " << rust_type(*method.return_type) << " {\n";
      else o << ") {\n";
      write_through_parameters_.clear(); view_parameters_.clear();
      for (size_t index = 0; index < method.params.size(); ++index) {
        if (view && method_effect(method, index) != Effect::Consume && view_object_type(method.params[index].type))
          view_parameters_.insert(method.params[index].name);
        else if (method_effect(method, index) == Effect::Write)
          write_through_parameters_.insert(method.params[index].name);
      }
      Domain receiver;
      receiver.name = t.name;
      receiver.state = t.fields;
      std::set<string> locals;
      std::unordered_map<string,string> types;
      for (const auto& field : t.fields) types[field.name] = field.type;
      for (const auto& p : method.params) { locals.insert(p.name); types[p.name] = p.type; }
      gen_stmts(o, method.body, &receiver, nullptr, "", locals, types, 2, false, true, functional_method_context(t, method));
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
      tooling_end(o, 0, semantic_identity);
      write_through_parameters_.clear(); view_parameters_.clear();
    }
  }

  void gen_function_instance(std::ostringstream& o, const Function& f,
                             const FunctionSpecialization* specialization, bool view = false) {
    bool container_generic = !specialization &&
        std::any_of(f.params.begin(), f.params.end(), [](const Param& p) {
          return p.type == "vector" || p.type == "queue" || p.type == "map";
        });
    string semantic_identity = function_semantic_identity(f, specialization);
    string generated_symbol = specialization ? specialization->generated_name : f.name;
    if (view) generated_symbol = "__moss_view_" + generated_symbol;
    bool can_export = !view && (specialization || (!f.generic && !container_generic));
    string native_symbol = tooling_native_symbol(
        "function", f.name, semantic_identity);
    tooling_begin(o, 0, "function", semantic_identity, generated_symbol,
                  can_export ? native_symbol : "");
    source_comment(o, 0, f.line, f.header.empty() ? "fn " + f.name : f.header);
    backend_comment(o, 0, specialization
        ? "STATIC specialization: concrete local function selected before Rust generation"
        : "LOCAL function: ordinary intra-domain call; no domain dispatch");
    debug_symbol_attributes(o, 0, native_symbol, can_export);
    bool materialized_export = f.exported && !specialization &&
        !f.generic && !container_generic;
    o << ((materialized_export || (specialization && public_specializations_)) ? "pub " : "")
      << "fn " << generated_symbol;
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
      if (view && effect != Effect::Consume && view_object_type(pt)) {
        o << f.params[index].name << ": " << (effect == Effect::Write ? "&mut " : "&") << "impl " << access_trait(pt);
        continue;
      }
      bool generic_copy_value = !specialization && f.params[index].type.empty() &&
          !ops.empty() && !ops.count("[]");
      bool borrow = effect == Effect::Write || (!generic_copy_value && effect != Effect::Consume &&
          borrowable_type(pt.empty() ? parameter_rust_type : pt));
      o << (borrow && effect == Effect::Write ? "mut " : "")
        << f.params[index].name << ": "
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
    write_through_parameters_.clear(); view_parameters_.clear();
    for (size_t index = 0; index < f.params.size(); ++index) {
      if (view && function_effect(f, index) != Effect::Consume && view_object_type(types.at(f.params[index].name)))
        view_parameters_.insert(f.params[index].name);
      else if (function_effect(f, index) == Effect::Write)
        write_through_parameters_.insert(f.params[index].name);
    }
    string functional_context = functional_function_context(f, specialization);
    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false, true, functional_context);
    if (f.result_expression) {
      source_comment(o, 4, f.result_line, *f.result_expression);
      auto plan = f.result_functional_pipeline_ids.find(functional_context);
      o << "    " << expr(*f.result_expression, nullptr, locals, &types,
                            plan == f.result_functional_pipeline_ids.end()
                                ? 0 : plan->second)
        << "\n";
    }
    o << "}\n\n";
    tooling_end(o, 0, semantic_identity);
    write_through_parameters_.clear(); view_parameters_.clear();
    bool needs_view = false;
    for (size_t i = 0; i < f.params.size(); ++i)
      needs_view |= function_effect(f, i) != Effect::Consume &&
          view_object_type(specialization ? specialization->parameter_types[i] : f.params[i].type);
    if (!view && needs_view) gen_function_instance(o, f, specialization, true);
  }

  void gen_function(std::ostringstream& o, const Function& f) {
    if (f.static_dispatch) {
      if (!emit_static_specializations_) return;
      for (const auto& specialization : f.specializations)
        gen_function_instance(o, f, &specialization);
      return;
    }
    gen_function_instance(o, f, nullptr);
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    o << "#[derive(Clone)]\n" << (d.exported ? "pub " : "") << "struct " << d.name << "Ref {\n"
      << "    state: Arc<" << d.name << "Runtime>,\n";
    for (const auto& route : d.routes)
      o << "    " << route.name << ": " << rust_type(route.type) << ",\n";
    o << "}\n\n";
  }

  static string reply_binding(const Domain& d, const Handler& h) {
    std::set<string> used;
    for (const auto& f : d.state) used.insert(f.name);
    for (const auto& p : h.params) used.insert(p.name);
    for (const auto& s : h.body) {
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var)
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

#include "handler_lowering.inc"
#include "borrowed_views.inc"

  static string nominal_handle_variant(const string& domain) {
    return "Specialized_" + stable_hash(domain).substr(0, 12);
  }

  void gen_nominal_handle(std::ostringstream& o, const Domain& source) {
    if (!has_domain_specializations(source.name)) return;
    vector<const Domain*> routes{&source};
    for (const auto& specialization : p_.domain_specializations) {
      if (specialization.source_domain != source.name) continue;
      auto generated = specialization_names_.find(
          specialization.source_domain + "\n" + specialization.instance);
      if (generated == specialization_names_.end()) continue;
      auto specialized = std::find_if(
          specialized_domains_.begin(), specialized_domains_.end(),
          [&](const Domain& domain) { return domain.name == generated->second; });
      if (specialized != specialized_domains_.end()) routes.push_back(&*specialized);
    }
    // The route list above is keyed by semantic specialization records. It
    // deliberately does not infer membership from generated Rust names: a
    // source domain may collide with a would-be specialized layout spelling.
    if (routes.size() < 2) return;

    const string handle = source.name + "Handle";
    const string route = handle + "Route";
    o << "// Moss backend: nominal " << source.name
      << " handle routes statically to each materialized instance\n";
    o << "#[derive(Clone)]\n" << "enum " << route << " {\n";
    o << "    Base(" << source.name << "Ref),\n";
    for (size_t index = 1; index < routes.size(); ++index)
      o << "    " << nominal_handle_variant(routes[index]->name) << "("
        << routes[index]->name << "Ref),\n";
    o << "}\n";
    o << "#[derive(Clone)]\n" << (source.exported ? "pub " : "")
      << "struct " << handle << " { route: " << route << " }\n\n";
    o << "impl From<" << source.name << "Ref> for " << handle << " {\n"
      << "    fn from(value: " << source.name << "Ref) -> Self { Self { route: "
      << route << "::Base(value) } }\n}\n";
    for (size_t index = 1; index < routes.size(); ++index) {
      o << "impl From<" << routes[index]->name << "Ref> for " << handle << " {\n"
        << "    fn from(value: " << routes[index]->name << "Ref) -> Self { Self { route: "
        << route << "::" << nominal_handle_variant(routes[index]->name)
        << "(value) } }\n}\n";
    }
    o << "\nimpl " << handle << " {\n";
    for (const auto& handler : source.handlers) {
      const Handler* route_handler = find_handler(*routes.front(), handler.name);
      if (!route_handler) continue;
      bool compatible = true;
      for (size_t index = 0; index < handler.params.size(); ++index) {
        string expected = rust_type(handler.params[index].type);
        for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
          const Handler* candidate = find_handler(*routes[route_index], handler.name);
          if (!candidate || index >= candidate->params.size() ||
              rust_type(candidate->params[index].type) != expected)
            compatible = false;
        }
      }
      for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
        const Handler* candidate = find_handler(*routes[route_index], handler.name);
        if (!candidate || candidate->reply_type.has_value() !=
                handler.reply_type.has_value() ||
            (candidate->reply_type && handler.reply_type &&
             rust_type(*candidate->reply_type) != rust_type(*handler.reply_type)))
          compatible = false;
      }
      if (!compatible) continue;
      if (source.exported) {
        o << "    pub fn __moss_message_" << handler.name << "(&self";
        string arguments;
        for (const auto& parameter : handler.params) {
          o << ", " << parameter.name << ": " << rust_type(parameter.type);
          if (!arguments.empty()) arguments += ", ";
          arguments += parameter.name;
        }
        o << ") -> " << rust_type(handler.reply_type.value_or("unit"))
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index)
          o << "            " << route << "::"
            << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => inner.__moss_message_" << handler.name
            << "(" << arguments << "),\n";
        o << "        }\n    }\n";
      }

        o << "    fn " << handler.name << "_shared(&self";
        for (const auto& parameter : handler.params)
          o << ", " << shared_message_parameter(parameter);
        o << ") -> " << (handler.reply_type ? "Option<" + rust_type(*handler.reply_type) + ">" : "()")
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index) {
          o << "            " << route << "::" << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => inner." << handler.name << "_shared(";
          for (const auto& parameter : handler.params) o << parameter.name << ", ";
          o << "),\n";
        }
        o << "        }\n    }\n";
          }
    o << "}\n\n";
  }

  // Expose a synchronous provider entry over its private state representation.
  // The target is fixed by the checked concrete route graph.
  void gen_exported_domain_bridge(std::ostringstream& out, const Domain& domain) {
    out << "impl " << domain.name << "Ref {\n";
    exported_domain_bridge_body_ = true;
    for (const auto& handler : domain.handlers) {
      std::set<string> locals{"self"};
      std::unordered_map<string,string> types{{"self", domain.name}};
      string call = "message self." + handler.name + "(";
      out << "    pub fn __moss_message_" << handler.name << "(&self";
      for (size_t index = 0; index < handler.params.size(); ++index) {
        const auto& parameter = handler.params[index];
        out << ", " << parameter.name << ": " << rust_type(parameter.type);
        locals.insert(parameter.name);
        types[parameter.name] = parameter.type;
        if (index) call += ", ";
        call += parameter.name;
      }
      call += ")";
      out << ") -> " << rust_type(handler.reply_type.value_or("unit")) << " {\n"
          << "        " << expr(call, nullptr, locals, &types) << "\n    }\n";
    }
    exported_domain_bridge_body_ = false;
    out << "}\n\n";
  }

  void gen_domain(std::ostringstream& o, const Domain& d) { gen_planned_domain(o, d); }

  static string rust_string_literal(const string& value) {
    return "\"" + debug_json_escape(value) + "\"";
  }

  static string test_or_benchmark_function_name(
      const string& prefix, const string& semantic_identity) {
    return "__moss_" + prefix + "_" +
        stable_hash(semantic_identity).substr(0, 12);
  }

  static void gen_protocol_hex_helper(std::ostringstream& o) {
    o << "fn __moss_protocol_hex(value: &str) -> String {\n";
    o << "    let mut output = String::with_capacity(value.len() * 2);\n";
    o << "    for byte in value.as_bytes() { use std::fmt::Write as _; let _ = write!(&mut output, \"{:02x}\", byte); }\n";
    o << "    output\n}\n\n";
  }

  void gen_test_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      tooling_begin(o, 0, "test", test.semantic_identity, function_name);
      source_comment(o, 0, test.line, test.header);
      o << "#[inline(never)]\nfn " << function_name << "() {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      gen_stmts(o, test.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "test:" + test.name);
      o << "}\n";
      tooling_end(o, 0, test.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    o << "    let mut failures = 0_usize;\n";
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      o << "    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| "
        << function_name << "()));\n";
      o << "    match outcome {\n";
      o << "        Ok(()) => println!(\"MOSS_TEST|PASS|{}|{}|\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << ")),\n";
      o << "        Err(payload) => {\n";
      o << "            failures += 1;\n";
      o << "            let detail = if let Some(value) = payload.downcast_ref::<String>() { value.clone() } else if let Some(value) = payload.downcast_ref::<&str>() { (*value).to_string() } else { \"test panicked without a Moss assertion message\".to_string() };\n";
      o << "            println!(\"MOSS_TEST|FAIL|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << "), __moss_protocol_hex(&detail));\n";
      o << "        }\n";
      o << "    }\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "    if failures != 0 { std::process::exit(1); }\n";
    o << "}\n";
  }

  void gen_benchmark_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      tooling_begin(o, 0, "benchmark", benchmark.semantic_identity,
                    function_name);
      source_comment(o, 0, benchmark.line, benchmark.header);
      o << "#[inline(never)]\nfn " << function_name
        << "() {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      bool previous_benchmark_body = benchmark_body_;
      benchmark_body_ = true;
      gen_stmts(o, benchmark.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "bench:" + benchmark.name);
      benchmark_body_ = previous_benchmark_body;
      for (const auto& local : locals)
        o << "    std::hint::black_box(&" << local << ");\n";
      o << "}\n";
      tooling_end(o, 0, benchmark.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    const WARMUP: usize = 5;\n";
    o << "    const SAMPLES: usize = 31;\n";
    o << "    const ITERATIONS: usize = 1000;\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      o << "    for _ in 0..WARMUP { for _ in 0..ITERATIONS { std::hint::black_box("
        << function_name << "()); } }\n";
      o << "    let mut samples: Vec<u128> = Vec::with_capacity(SAMPLES);\n";
      o << "    for _ in 0..SAMPLES {\n";
      o << "        let started = std::time::Instant::now();\n";
      o << "        for _ in 0..ITERATIONS { std::hint::black_box("
        << function_name << "()); }\n";
      o << "        let elapsed = started.elapsed().as_nanos();\n";
      o << "        samples.push((elapsed + ITERATIONS as u128 - 1) / ITERATIONS as u128);\n";
      o << "    }\n";
      o << "    samples.sort_unstable();\n";
      o << "    let p25 = samples[SAMPLES / 4];\n";
      o << "    let median = samples[SAMPLES / 2];\n";
      o << "    let p75 = samples[(SAMPLES * 3) / 4];\n";
      o << "    let sample_text = samples.iter().map(|value| value.to_string()).collect::<Vec<_>>().join(\",\");\n";
      o << "    println!(\"MOSS_BENCH|{}|{}|{}|{}|{}|{}|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(benchmark.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(benchmark.name)
        << "), median, p25, p75, SAMPLES, WARMUP, ITERATIONS, sample_text);\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "}\n";
  }

  void gen_main(std::ostringstream& o, const MainProc& m) {
    string semantic_identity = "main@" + std::to_string(m.line);
    tooling_begin(o, 0, "main", semantic_identity, "main",
                  "moss__main");
    source_comment(o, 0, m.line, m.header.empty() ? "proc main()" : m.header);
    backend_comment(o, 0, "main entry point; domain calls below retain their statically selected lowering");
    debug_symbol_attributes(o, 0, "moss__main");
    o << "fn main() {\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    // Domain construction is a structural composition prefix.  Emit that
    // prefix in the graph's deterministic topological order so a route may
    // name a concrete instance whose source binding appears later in main.
    // This keeps source order from becoming a hidden dependency while leaving
    // ordinary executable statements in their source order.
    vector<Stmt> constructions;
    vector<Stmt> executable;
    for (const auto& statement : m.body) {
      if (domain_construction(statement.b)) constructions.push_back(statement);
      else executable.push_back(statement);
    }
    std::unordered_map<string,int> domain_ranks;
    std::unordered_map<string,string> domain_identities;
    for (const auto& instance : p_.concrete_domain_graph.instances) {
      domain_ranks[instance.binding] = instance.domain_rank;
      domain_identities[instance.binding] = instance.identity;
    }
    std::stable_sort(constructions.begin(), constructions.end(),
                     [&](const Stmt& left, const Stmt& right) {
                       // A route target must exist before its owner can be
                       // constructed; domain_rank itself points from owner
                       // to target, so construction runs in reverse rank.
                       return domain_ranks[left.a] > domain_ranks[right.a];
                     });
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    size_t construction_index = 0;
    gen_block(o, constructions, construction_index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    size_t index = 0;
    gen_block(o, executable, index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    o << "}\n";
    tooling_end(o, 0, semantic_identity);
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
                 const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler,
                 bool in_function = false,
                 const string& functional_context = "") {
    // Binding metadata is lexical to this generated Moss body.  Keeping it
    // separate from Rust type strings prevents a local/backend name collision
    // from being mistaken for a specialization relationship.
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_slot, locals, types, base,
              in_handler, in_function, {},
              functional_context);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool in_function,
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
              << rust_type(type)
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
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, child_join_assignments,
                    functional_context);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            source_comment(o, (base + level + 1) * 4, ss[i].line, ss[i].text);
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      else_locals, else_types, base, in_handler, in_function, child_join_assignments,
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
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    functional_context);
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::For: {
          string source = trim(s.b);
          string range_name;
          vector<string> range_args;
          bool is_range = parse_simple_call(source, range_name, range_args) &&
              range_name == "range";
          auto source_type = generated_expr_type(source, &types);
          string element_type = source_type ? canonical_type_name(*source_type) : "";
          if (is_range) {
            o << indent(level) << "for " << s.a << " in "
              << expr(range_args[0], d, locals, &types) << ".."
              << expr(range_args[1], d, locals, &types);
            if (range_args.size() == 3)
              o << ".step_by((" << expr(range_args[2], d, locals, &types)
                << ") as usize)";
            o << " {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "int";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
            break;
          }

          bool vector_source = source_type &&
              (starts_with(element_type, "vector[") ||
               starts_with(element_type, "seq["));
          if (vector_source) {
            string element = starts_with(element_type, "vector[")
                ? trim(element_type.substr(7, element_type.size() - 8))
                : trim(element_type.substr(4, element_type.size() - 5));
            string collection_expression = expr(source, d, locals, &types);
            string traversal = "(" + collection_expression + ").iter()";
            if (copy_type(element)) traversal += ".copied()";
            o << indent(level) << "for " << s.a << " in " << traversal
              << " {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = element;
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
            break;
          }

          if (!source_type)
            throw std::runtime_error("internal error: unresolved static iterator source");
          string concrete = canonical_type_name(*source_type);
          const Method* next = resolve_object_method(objects_, concrete, "next", {}, true, nullptr);
          bool direct_iterator = next != nullptr;
          const Method* iterator_factory = nullptr;
          if (!direct_iterator) {
            iterator_factory = resolve_object_method(objects_, concrete, "iter", {}, true, nullptr);
            if (iterator_factory && iterator_factory->return_type)
              next = resolve_object_method(
                  objects_, canonical_type_name(*iterator_factory->return_type),
                  "next", {}, true, nullptr);
          }
          string iterator_expression;
          if (direct_iterator && next->receiver_effect == Effect::Consume)
            iterator_expression = expr(source, d, locals, &types);
          else if (direct_iterator)
            iterator_expression = "&mut " + expr(source, d, locals, &types);
          else
            iterator_expression = expr(source, d, locals, &types) + ".iter()";
          o << indent(level) << "let mut __moss_iterator = "
            << iterator_expression << ";\n";
          o << indent(level) << "loop {\n";
          o << indent(level + 1) << "match __moss_iterator.next() {\n";
          o << indent(level + 2) << "Some(" << s.a << ") => {\n";
          auto child_locals = locals;
          auto child_types = types;
          child_locals.insert(s.a);
          string next_type = next && next->return_type
              ? canonical_type_name(*next->return_type) : "option[_]";
          string element = starts_with(next_type, "option[") && ends_with(next_type, "]")
              ? trim(next_type.substr(7, next_type.size() - 8)) : "_value";
          child_types[s.a] = element;
          ++i;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    functional_context);
          o << indent(level + 2) << "}\n";
          o << indent(level + 2) << "None => break,\n";
          o << indent(level + 1) << "}\n";
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
          if (auto sd = domain_construction(s.b)) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            bool existing_binding = locals.count(s.a);
            o << indent(level) << (existing_binding ? "" : "let ") << s.a << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
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
            lhs = "(" + place_expr(lhs_base, d, locals, &types) + ")[" + ir + "]";
          }
          else lhs = place_expr(s.a, d, locals, &types);
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
            auto source_binding = domain_instance_bindings_.find(trim(s.b));
            if (source_binding != domain_instance_bindings_.end())
              remember_domain_instance_binding(
                  s.a, source_binding->second.source_domain,
                  source_binding->second.instance);
            else
              forget_domain_instance_binding(s.a);
          } else {
            string mb, mi;
            if (parse_index(s.a, mb, mi) && types.count(mb) &&
                (types.at(mb) == "map" || starts_with(types.at(mb), "map[")))
              o << indent(level) << "(" << place_expr(mb, d, locals, &types) << ").insert(" << ((mi.size() >= 2 && mi.front() == '"' && mi.back() == '"') ? mi + ".to_string()" : expr(mi, d, locals, &types)) << ", " << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
            else if (is_object_view(s.a, d, locals, &types))
              o << indent(level) << "(" << lhs << ").__moss_replace(" << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
            else o << indent(level) << lhs << " = " << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ";\n";
            if (plain_identifier(s.a)) forget_domain_instance_binding(s.a);
          }
          ++i;
          break;
        }
        case Stmt::Kind::Call: {
          if (s.b.empty() && s.a == "assert") {
            o << indent(level) << "if !("
              << expr(s.args.front(), d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ") { panic!("
              << rust_string_literal(
                     "Moss assertion failed at line " +
                     std::to_string(s.line) + ": " + s.text)
              << "); }\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "assertEqual") {
            size_t id = assertion_temp_++;
            string actual = "__moss_assert_actual_" + std::to_string(id);
            string expected = "__moss_assert_expected_" + std::to_string(id);
            o << indent(level) << "let " << actual << " = &("
              << expr(s.args[0], d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ");\n";
            o << indent(level) << "let " << expected << " = &("
              << expr(s.args[1], d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 1))
              << ");\n";
            string message = "Moss assertion failed at line " +
                std::to_string(s.line) + ": " + s.text +
                "; actual={:?}, expected={:?}";
            o << indent(level) << "if " << actual << " != " << expected
              << " { panic!(" << rust_string_literal(message) << ", "
              << actual << ", " << expected << "); }\n";
            ++i;
            break;
          }
          bool known_function = functions_.count(s.a);
          bool implicit_method = in_function && d && objects_.count(d->name) &&
              s.b.empty() && !known_function;
          o << indent(level);
          if (benchmark_body_) o << "std::hint::black_box(";
          o << (implicit_method ? "self." : "")
            << (s.b.empty() && known_function
                    ? viewed_function_name(s.a, s.args, d, locals, &types)
                    : !s.b.empty() ? method_receiver_place(s.a, s.b, s.args, d, locals, &types)
                    : expr(s.a, d, locals, &types));
          if (!s.b.empty()) {
            string method = s.b;
            auto it = types.find(s.a);
            if (it != types.end() && (it->second == "queue" || starts_with(it->second, "queue["))) method = method == "push" ? "push_back" : method == "pop" ? "pop_front" : method;
            o << "." << viewed_method_name(s.a, method, s.args, d, locals, &types);
          }
          o << "(";
          size_t emitted_arguments = 0;
          for (size_t k = 0; k < s.args.size(); ++k) {
            bool compile_time_callable = known_function &&
                starts_with(generated_expr_type(s.args[k], &types).value_or(""),
                            "callable:");
            if (compile_time_callable) continue;
            if (emitted_arguments++) o << ", ";
            if (benchmark_body_) o << "std::hint::black_box(";
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
                                                   result.push_back(nominalized_generated_argument_type(
                                                       argument, generated_expr_type(argument, &types).value_or("")));
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
            if (benchmark_body_) o << ")";
          }
          o << ")";
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        }
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          bool joined_assignment = join_assignments.erase(s.a) != 0;
          auto sd = domain_construction(s.b);
          if (sd) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << s.a << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
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
            if (domain_capability) {
              auto source_binding = domain_instance_bindings_.find(trim(s.b));
              if (source_binding != domain_instance_bindings_.end())
                remember_domain_instance_binding(
                    s.a, source_binding->second.source_domain,
                    source_binding->second.instance);
              else
                forget_domain_instance_binding(s.a);
            } else {
              forget_domain_instance_binding(s.a);
            }
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Message: {
          string call = "message " + s.a + "." + s.b + "(";
          for (size_t argument = 0; argument < s.args.size(); ++argument) {
            if (argument) call += ", ";
            call += s.args[argument];
          }
          call += ")";
          o << indent(level);
          if (!s.message_result.empty()) {
            if (!locals.count(s.message_result)) o << "let mut ";
            o << s.message_result << " = ";
          }
          vector<size_t> argument_plans;
          for (size_t argument = 0; argument < s.args.size(); ++argument)
            argument_plans.push_back(statement_functional_pipeline_id(s, functional_context, argument));
          o << expr(call, d, locals, &types, 0, &argument_plans) << ";\n";
          if (!s.message_result.empty()) {
            const Domain& target = *domains_.at(canonical_type_name(types.at(s.a)));
            locals.insert(s.message_result);
            types[s.message_result] = find_handler(target, s.b)->reply_type.value_or("unit");
          }
          ++i;
          break;
        }
        case Stmt::Kind::Reply:
          if (!current_handler || !current_handler->reply_type || reply_slot.empty())
            throw std::runtime_error("internal error: unchecked reply reached code generation");
          o << indent(level) << reply_slot << " = Some("
            << message_arg(s.a, *current_handler->reply_type, d, locals, &types,
                statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
          o << indent(level) << "break 'handler;\n";
          ++i;
          // `reply` terminates the handler.  Do not emit statements that are
          // lexically after an unconditional reply: rustc would otherwise
          // diagnose the generated code as unreachable, even though Moss's
          // control-flow checker has already established the terminating
          // semantics.
          while (i < ss.size() && ss[i].indent >= level) ++i;
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
          o << indent(level);
          // A bare expression is a valid benchmark body.  Protect its value
          // just like an ordinary call result; otherwise rustc may erase the
          // very arithmetic or functional pipeline being measured.
          if (benchmark_body_) o << "std::hint::black_box(";
          o << expr(s.text, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0));
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        case Stmt::Kind::Else:
          return;
      }
    }
  }

  std::optional<string> domain_construction(const string& expr) const {
    string e = trim(expr);
    string callee; vector<string> args;
    if (!parse_simple_call(e, callee, args)) return std::nullopt;
    return domains_.count(callee) ? std::optional<string>(callee) : std::nullopt;
  }

  void emit_domain_construction(std::ostringstream& o, const string& domain,
                       const string& expression, const Domain* context,
                       const std::set<string>& locals,
                       const std::unordered_map<string,string>* types) const {
    auto found = domains_.find(domain);
    if (found == domains_.end()) {
      throw std::runtime_error("internal error: unknown concrete domain constructor");
    }
    const Domain& definition = *found->second;
    o << "construct_" << snake_case(domain) << "(";
    string constructor = trim(expression);
    string callee; vector<string> arguments;
    std::unordered_map<string,string> named;
    if (parse_simple_call(constructor, callee, arguments)) {
      for (const auto& argument : arguments) {
        string name, value;
        if (parse_named_argument(argument, name, value)) named[name] = value;
      }
    }
    bool constructor_argument = false;
    {
      for (const auto& route : definition.routes) {
        auto value = named.find(route.name);
        if (value == named.end())
          throw std::runtime_error("internal error: missing route binding during lowering");
        // The checked graph has already proved nominal route compatibility.
        // Adapt the concrete storage reference to the existing route handle;
        // never use a generated type spelling to establish compatibility.
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << "(" << expr(value->second, context, locals, types) << ").clone().into()";
      }
      for (const auto& field : definition.state) {
        auto value = named.find(field.name);
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << (value == named.end()
            ? (field.init.empty() ? default_value(field.type)
                                  : expr(field.init, context, locals, types))
            : expr(value->second, context, locals, types));
      }
    }
    if (constructor_argument) o << ", ";
    const auto& plan = physical_domain_plan(construction_binding_);
    o << rust_string_literal(plan.concrete_instance_id) << ", " << plan.domain_rank;
    for (const auto& h : definition.handlers) {
      auto found_handler = std::find_if(plan.handlers.begin(), plan.handlers.end(),
          [&](const auto& candidate) { return candidate.name == h.name; });
      synchronization_require(found_handler != plan.handlers.end(), "missing typed handler identity");
      o << ", " << rust_string_literal(found_handler->handler_identity);
    }
    o << ");\n";
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

};

static vector<string> debug_split_fields(const string& value) {
  vector<string> fields;
  size_t begin = 0;
  while (true) {
    size_t delimiter = value.find('|', begin);
    if (delimiter == string::npos) {
      fields.push_back(value.substr(begin));
      return fields;
    }
    fields.push_back(value.substr(begin, delimiter - begin));
    begin = delimiter + 1;
  }
}

static vector<string> generated_lines(const string& rust) {
  vector<string> lines;
  std::istringstream input(rust);
  string line;
  while (std::getline(input, line)) lines.push_back(line);
  if (lines.empty()) lines.push_back("");
  return lines;
}

static bool generated_noncode_line(const string& line) {
  string value = trim(line);
  return value.empty() || starts_with(value, "//") ||
      starts_with(value, "#[");
}

static int next_generated_code_line(const vector<string>& lines, int line,
                                    int limit) {
  int candidate = std::max(1, line);
  int end = std::min(limit, static_cast<int>(lines.size()));
  while (candidate <= end &&
         generated_noncode_line(lines[static_cast<size_t>(candidate - 1)]))
    ++candidate;
  return candidate <= end ? candidate : std::max(1, std::min(line, end));
}

static int previous_generated_code_line(const vector<string>& lines, int line,
                                        int limit) {
  int candidate = std::min(line, static_cast<int>(lines.size()));
  while (candidate >= limit && trim(lines[static_cast<size_t>(candidate - 1)]).empty())
    --candidate;
  return candidate >= limit ? candidate : limit;
}

static std::optional<int> moss_comment_line(const string& line) {
  string value = trim(line);
  static const string prefix = "// Moss line ";
  if (!starts_with(value, prefix)) return std::nullopt;
  size_t begin = prefix.size();
  size_t end = begin;
  while (end < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[end])))
    ++end;
  if (end == begin) return std::nullopt;
  return std::stoi(value.substr(begin, end - begin));
}

struct DebugMarkerRegion {
  string kind;
  string semantic_identity;
  string generated_symbol;
  string native_symbol;
  int begin_line = 0;
  int end_line = 0;
};

static vector<DebugMarkerRegion> collect_debug_regions(
    const vector<string>& lines) {
  static const string begin_prefix = "// Moss tooling begin|";
  static const string end_prefix = "// Moss tooling end|";
  vector<DebugMarkerRegion> regions;
  std::unordered_map<string, size_t> open;
  for (size_t index = 0; index < lines.size(); ++index) {
    string value = trim(lines[index]);
    if (starts_with(value, begin_prefix)) {
      auto fields = debug_split_fields(value.substr(begin_prefix.size()));
      if (fields.size() != 4)
        throw std::runtime_error("internal error: malformed Moss tooling marker");
      DebugMarkerRegion region;
      region.kind = fields[0];
      region.semantic_identity = fields[1];
      region.generated_symbol = fields[2];
      region.native_symbol = fields[3];
      region.begin_line = static_cast<int>(index + 1);
      if (!open.emplace(region.semantic_identity, regions.size()).second)
        throw std::runtime_error(
            "internal error: duplicate open Moss tooling identity: " +
            region.semantic_identity);
      regions.push_back(std::move(region));
      continue;
    }
    if (starts_with(value, end_prefix)) {
      string identity = value.substr(end_prefix.size());
      auto found = open.find(identity);
      if (found == open.end())
        throw std::runtime_error(
            "internal error: unmatched Moss tooling end marker: " + identity);
      regions[found->second].end_line = static_cast<int>(index + 1);
      open.erase(found);
    }
  }
  if (!open.empty())
    throw std::runtime_error("internal error: unclosed Moss tooling marker");
  return regions;
}

static DebugMapEntry debug_entry_from_region(
    const DebugMarkerRegion& region, const vector<string>& lines,
    const string& source_file, const string& generated_file) {
  DebugMapEntry entry;
  entry.semantic_identity = region.semantic_identity;
  entry.construct_kind = region.kind;
  entry.generated_symbol = region.generated_symbol;
  entry.native_symbol = region.native_symbol;
  entry.provenance.push_back(region.semantic_identity);
  entry.source.file = source_file;
  entry.generated.file = generated_file;
  entry.generated.start_line = next_generated_code_line(
      lines, region.begin_line + 1, region.end_line - 1);
  entry.generated.end_line = previous_generated_code_line(
      lines, region.end_line - 1, entry.generated.start_line);

  std::set<int> mapped_source_lines;
  for (int line = region.begin_line + 1; line < region.end_line; ++line) {
    auto moss_line = moss_comment_line(lines[static_cast<size_t>(line - 1)]);
    if (!moss_line) continue;
    entry.source.start_line = entry.source.start_line == 0
        ? *moss_line : std::min(entry.source.start_line, *moss_line);
    entry.source.end_line = std::max(entry.source.end_line, *moss_line);
    if (mapped_source_lines.insert(*moss_line).second) {
      entry.line_mappings.push_back({
          *moss_line,
          next_generated_code_line(lines, line + 1, region.end_line - 1)});
    }
  }
  if (entry.source.start_line == 0) {
    entry.source.start_line = 1;
    entry.source.end_line = 1;
  }
  return entry;
}

static const DebugMapEntry* debug_parent_for_context(
    const vector<DebugMapEntry>& entries, const string& context) {
  string prefix;
  if (context == "main") prefix = "main@";
  else prefix = context + "@";
  auto found = std::find_if(
      entries.begin(), entries.end(), [&](const DebugMapEntry& entry) {
        return starts_with(entry.semantic_identity, prefix) &&
            (entry.construct_kind == "function" ||
             entry.construct_kind == "method" ||
             entry.construct_kind == "handler" ||
             entry.construct_kind == "main");
      });
  return found == entries.end() ? nullptr : &*found;
}

static int find_generated_identity_line(const vector<string>& lines,
                                        const string& identity,
                                        const DebugMapEntry* parent) {
  int begin = parent ? parent->generated.start_line : 1;
  int end = parent ? parent->generated.end_line
                   : static_cast<int>(lines.size());
  for (int line = begin; line <= end; ++line)
    if (lines[static_cast<size_t>(line - 1)].find(identity) != string::npos)
      return line;
  return 0;
}

static int functional_generated_end_line(const vector<string>& lines,
                                         int marker_line, int limit) {
  if (marker_line <= 0 || marker_line > static_cast<int>(lines.size()))
    return marker_line;
  const string& marker = lines[static_cast<size_t>(marker_line - 1)];
  bool origin_marker = starts_with(trim(marker), "// Moss functional origin|");
  size_t marker_indent = marker.find_first_not_of(" \t");
  if (marker_indent == string::npos) marker_indent = 0;
  for (int line = marker_line + 1;
       line <= limit && line <= static_cast<int>(lines.size()); ++line) {
    const string& candidate = lines[static_cast<size_t>(line - 1)];
    if (trim(candidate).empty()) continue;
    if (origin_marker &&
        starts_with(trim(candidate), "// Moss functional origin|"))
      return previous_generated_code_line(lines, line - 1, marker_line + 1);
    size_t candidate_indent = candidate.find_first_not_of(" \t");
    if (candidate_indent == string::npos) continue;
    if (candidate_indent < marker_indent) return line;
  }
  return std::max(marker_line, limit);
}

static string debug_source_for_identity(const Program& program,
                                        const string& identity,
                                        const string& fallback) {
  for (const auto& function : program.functions)
    if (identity == functional_function_context(function) + "@" +
            std::to_string(function.line) ||
        (starts_with(identity, "fn:" + function.name + "<") &&
         identity.find("@" + std::to_string(function.line)) != string::npos))
      return function.source_file.empty() ? fallback : function.source_file;
  for (const auto& object : program.objects)
    for (const auto& method : object.methods)
      if (identity == functional_method_context(object, method) + "@" +
              std::to_string(method.line))
        return method.source_file.empty() ? fallback : method.source_file;
  for (const auto& domain : program.domains)
    for (const auto& handler : domain.handlers)
      if (identity == functional_handler_context(domain, handler) + "@" +
              std::to_string(handler.line))
        return handler.source_file.empty() ? fallback : handler.source_file;
  if (program.main && identity == "main@" +
      std::to_string(program.main->line))
    return program.main->source_file.empty() ? fallback : program.main->source_file;
  for (const auto& trait : program.traits)
    if (identity == "trait:" + trait.name + "@" + std::to_string(trait.line))
      return trait.source_file.empty() ? fallback : trait.source_file;
  for (const auto& test : program.tests)
    if (identity == test.semantic_identity)
      return test.source_file.empty() ? fallback : test.source_file;
  for (const auto& benchmark : program.benchmarks)
    if (identity == benchmark.semantic_identity)
      return benchmark.source_file.empty() ? fallback : benchmark.source_file;
  for (const auto& pipeline : program.functional_pipelines)
    if (identity == pipeline.semantic_identity && !pipeline.source_file.empty())
      return pipeline.source_file;
  for (const auto& group : program.functional_traversal_groups)
    if (identity == group.semantic_identity && !group.source_file.empty())
      return group.source_file;
  return fallback;
}

static vector<string> pipeline_provenance(const FunctionalPipeline& pipeline) {
  if (!pipeline.lowered_provenance.empty()) return pipeline.lowered_provenance;
  vector<string> provenance;
  for (const auto& node : pipeline.nodes)
    provenance.push_back(node.semantic_identity);
  return provenance;
}

static DebugMap build_debug_map(const Program& program, const string& rust,
                                const string& source_file,
                                const string& generated_file,
                                const string& native_executable,
                                bool debug_build, bool optimized) {
  DebugMap map;
  map.source_file = source_file;
  map.generated_rust_file = generated_file;
  map.native_executable = native_executable;
  map.debug_build = debug_build;
  map.optimized = optimized;
  vector<string> lines = generated_lines(rust);
  for (const auto& region : collect_debug_regions(lines)) {
    auto entry = debug_entry_from_region(region, lines,
        debug_source_for_identity(program, region.semantic_identity, source_file), generated_file);
    auto existing = std::find_if(map.entries.begin(), map.entries.end(),
        [&](const auto& candidate) { return candidate.semantic_identity == entry.semantic_identity; });
    if (existing == map.entries.end()) map.entries.push_back(std::move(entry));
    else {
      // Owned and borrowed implementations retain one Moss identity and its
      // original native symbol. Exact mappings cover BOTH physical bodies, so
      // source breakpoints/reverse lookup also work inside borrowed helpers.
      existing->line_mappings.insert(existing->line_mappings.end(), entry.line_mappings.begin(), entry.line_mappings.end());
    }
  }

  // Traits are erased through static specialization, but retain source-only
  // entries so editor navigation can explain that they have no runtime object.
  for (const auto& trait : program.traits) {
    DebugMapEntry entry;
    entry.semantic_identity = "trait:" + trait.name + "@" +
        std::to_string(trait.line);
    entry.construct_kind = "trait";
    entry.source = {trait.source_file.empty() ? source_file : trait.source_file,
                    trait.line, 1, trait.line, 1};
    entry.generated.file = generated_file;
    entry.generated_symbol = trait.name;
    entry.provenance.push_back(entry.semantic_identity);
    map.entries.push_back(std::move(entry));
  }

  size_t functional_entry_count = program.functional_traversal_groups.size();
  for (const auto& pipeline : program.functional_pipelines)
    functional_entry_count += 1 + pipeline.nodes.size();
  // Parent lookups below return pointers into this vector. Reserve every
  // functional entry up front so adding child provenance cannot invalidate the
  // containing function/method/handler entry.
  map.entries.reserve(map.entries.size() + functional_entry_count);

  // Functional nodes are many-to-one by design.  Every node in a fused plan
  // therefore points at the same generated region and records every origin
  // contributing to that region.
  for (const auto& pipeline : program.functional_pipelines) {
    const DebugMapEntry* parent = debug_parent_for_context(
        map.entries, pipeline.context);
    int marker_line = find_generated_identity_line(
        lines, pipeline.semantic_identity, parent);
    if (marker_line == 0 && !pipeline.nodes.empty())
      marker_line = find_generated_identity_line(
          lines, pipeline.nodes.front().semantic_identity, parent);
    if (marker_line == 0)
      marker_line = parent ? parent->generated.start_line : 1;
    int generated_line = next_generated_code_line(
        lines, marker_line + 1,
        parent ? parent->generated.end_line : static_cast<int>(lines.size()));
    int generated_end = functional_generated_end_line(
        lines, marker_line,
        parent ? parent->generated.end_line : static_cast<int>(lines.size()));
    vector<string> provenance = pipeline_provenance(pipeline);

    DebugMapEntry pipeline_entry;
    pipeline_entry.semantic_identity = pipeline.semantic_identity;
    pipeline_entry.construct_kind = "functional_pipeline";
    pipeline_entry.source = {pipeline.source_file.empty() ? source_file
                                                           : pipeline.source_file,
                             pipeline.line, 1, pipeline.line, 1};
    pipeline_entry.generated = {
        generated_file, generated_line, 1, generated_end, 1};
    if (parent) {
      pipeline_entry.generated_symbol = parent->generated_symbol;
      pipeline_entry.native_symbol = parent->native_symbol;
    }
    pipeline_entry.provenance = provenance;
    pipeline_entry.line_mappings.push_back({pipeline.line, generated_line});
    map.entries.push_back(pipeline_entry);

    for (const auto& node : pipeline.nodes) {
      int node_marker_line = find_generated_identity_line(
          lines, node.semantic_identity, parent);
      if (node_marker_line == 0) node_marker_line = marker_line;
      int node_generated_line = next_generated_code_line(
          lines, node_marker_line + 1,
          parent ? parent->generated.end_line : static_cast<int>(lines.size()));
      int node_generated_end = functional_generated_end_line(
          lines, node_marker_line,
          parent ? parent->generated.end_line : static_cast<int>(lines.size()));
      DebugMapEntry node_entry;
      node_entry.semantic_identity = node.semantic_identity;
      node_entry.construct_kind = "functional_node";
      node_entry.source = {pipeline.source_file.empty() ? source_file
                                                         : pipeline.source_file,
                           node.span.line, 1, node.span.line, 1};
      node_entry.generated = {
          generated_file, node_generated_line, 1, node_generated_end, 1};
      node_entry.generated_symbol = pipeline_entry.generated_symbol;
      node_entry.native_symbol = pipeline_entry.native_symbol;
      node_entry.provenance = provenance;
      node_entry.line_mappings.push_back({node.span.line, node_generated_line});
      map.entries.push_back(std::move(node_entry));
    }
  }

  for (const auto& group : program.functional_traversal_groups) {
    const DebugMapEntry* parent = debug_parent_for_context(map.entries,
                                                           group.context);
    int marker_line = find_generated_identity_line(lines, group.semantic_identity,
                                                   parent);
    if (marker_line == 0)
      marker_line = parent ? parent->generated.start_line : 1;
    int generated_line = next_generated_code_line(
        lines, marker_line + 1,
        parent ? parent->generated.end_line : static_cast<int>(lines.size()));
    int generated_end = functional_generated_end_line(
        lines, marker_line,
        parent ? parent->generated.end_line : static_cast<int>(lines.size()));
    DebugMapEntry entry;
    entry.semantic_identity = group.semantic_identity;
    entry.construct_kind = "functional_dataflow_group";
    entry.source = {group.source_file.empty() ? source_file : group.source_file,
                    group.line, 1, group.line, 1};
    entry.generated = {generated_file, generated_line, 1, generated_end, 1};
    if (parent) {
      entry.generated_symbol = parent->generated_symbol;
      entry.native_symbol = parent->native_symbol;
    }
    entry.provenance = group.provenance;
    entry.line_mappings.push_back({group.line, generated_line});
    map.entries.push_back(std::move(entry));
  }

  std::sort(map.entries.begin(), map.entries.end(),
            [](const DebugMapEntry& left, const DebugMapEntry& right) {
    if (left.source.start_line != right.source.start_line)
      return left.source.start_line < right.source.start_line;
    if (left.construct_kind != right.construct_kind)
      return left.construct_kind < right.construct_kind;
    return left.semantic_identity < right.semantic_identity;
  });
  return map;
}

// Phase 6A exposes the compiler's retained semantic facts.  This layer is
// deliberately serialization and lookup only: it consumes Program,
// FunctionalPipeline, and OptimizationPlan records produced by the ordinary
// compiler passes and never performs a second type/effect analysis.
struct SemanticParameterFact {
  string name;
  string type;
  Effect ownership = Effect::Read;
};

struct SemanticTargetFact {
  string semantic_identity;
  string source_file;
  // Phase 5 provenance remains line-oriented.  Phase 6 entity identities are
  // a separate contract intended to survive unrelated source movement.
  string durable_identity;
  string context;
  string kind;
  string name;
  string type;
  int line = 0;
  vector<SemanticParameterFact> parameters;
  vector<std::pair<string,string>> specialization_fields;
  ObservableEffects observable_effects;
  bool has_observable_effects = false;
  ObservableEffects enclosing_callable_effects;
  bool has_enclosing_callable_effects = false;
  vector<string> provenance;
  vector<string> explanations;
  string implementation_hash;
  string semantic_interface_hash;
  string module_identity;
  string export_visibility;
  string export_kind;
};

static string semantic_module_name(const string& name) {
  auto separator = name.find("__");
  return separator == string::npos ? string() : name.substr(0, separator);
}

static string ownership_effect_name(Effect effect) {
  switch (effect) {
    case Effect::Read: return "READ";
    case Effect::Write: return "WRITE";
    case Effect::Consume: return "CONSUME";
  }
  return "READ";
}

static ObservableEffects resolved_empty_effects() {
  ObservableEffects effects;
  effects.unresolved = false;
  return effects;
}

static string compact_semantic_text(const string& text) {
  string result;
  bool in_string = false;
  bool escaped = false;
  for (char character : text) {
    if (in_string) {
      result.push_back(character);
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') in_string = false;
      continue;
    }
    if (character == '"') {
      in_string = true;
      result.push_back(character);
    } else if (!std::isspace(static_cast<unsigned char>(character))) {
      result.push_back(character);
    }
  }
  return result;
}

static string observable_effect_fingerprint(const ObservableEffects& effects) {
  return string(effects.local_capture_read ? "1" : "0") +
      (effects.local_mutation ? "1" : "0") +
      (effects.domain_read ? "1" : "0") +
      (effects.domain_write ? "1" : "0") +
      (effects.message ? "1" : "0") +
      (effects.external_io ? "1" : "0") +
      (effects.may_fail ? "1" : "0") +
      (effects.may_diverge ? "1" : "0") +
      (effects.unresolved ? "1" : "0");
}

static string statement_fingerprint_text(const Stmt& statement) {
  std::ostringstream out;
  out << static_cast<int>(statement.kind) << ":" << statement.indent << ":"
      << compact_semantic_text(statement.a) << ":"
      << compact_semantic_text(statement.b) << ":"
      << compact_semantic_text(statement.message_result) << ":"
      << compact_semantic_text(statement.semantic_type) << ":"
      << "statement";
  for (const auto& argument : statement.args)
    out << compact_semantic_text(argument) << ",";
  if (statement.kind == Stmt::Kind::Raw)
    out << compact_semantic_text(statement.text);
  vector<std::pair<string,string>> joined(statement.joined_types.begin(),
                                          statement.joined_types.end());
  std::sort(joined.begin(), joined.end());
  for (const auto& entry : joined)
    out << "|" << entry.first << "=" << entry.second;
  return out.str();
}

static string body_fingerprint_text(const vector<Stmt>& body) {
  std::ostringstream out;
  for (const auto& statement : body)
    out << statement_fingerprint_text(statement) << "\n";
  return out.str();
}

static string durable_identity_base(const SemanticTargetFact& fact) {
  string prefix = "entity-v1:";
  if (fact.kind == "function") return prefix + "function:" + fact.name;
  if (fact.kind == "specialization")
    return prefix + "specialization:" + fact.context.substr(3);
  if (fact.kind == "type") return prefix + "type:" + fact.name;
  if (fact.kind == "field") return prefix + "field:" + fact.name;
  if (fact.kind == "method") return prefix + "method:" + fact.name;
  if (fact.kind == "trait") return prefix + "trait:" + fact.name;
  if (fact.kind == "domain") return prefix + "domain:" + fact.name;
  if (fact.kind == "domain_state") return prefix + "state:" + fact.name;
  if (fact.kind == "domain_specialization")
    return prefix + "domain-specialization:" + fact.name;
  if (fact.kind == "handler") return prefix + "handler:" + fact.name;
  if (fact.kind == "test") {
    string identity = fact.semantic_identity;
    return prefix + (starts_with(identity, "test:") ? identity
                                                     : "test:" + fact.name);
  }
  if (fact.kind == "benchmark") {
    string identity = fact.semantic_identity;
    return prefix + (starts_with(identity, "bench:") ? identity
                                                      : "bench:" + fact.name);
  }
  if (fact.kind == "main") return prefix + "main";
  if (fact.kind == "binding")
    return prefix + "binding:" + fact.context + ":" + fact.name;
  if (fact.kind == "functional_pipeline")
    return prefix + "pipeline:" + fact.context + ":" +
        stable_hash(fact.name);
  if (fact.kind == "functional_node")
    return prefix + "functional-node:" + fact.context + ":" + fact.name +
        ":" + stable_hash(fact.semantic_identity);
  if (fact.kind == "call")
    return prefix + "call:" + fact.context + ":" + fact.name;
  return prefix + "statement:" + fact.context + ":" +
      stable_hash(compact_semantic_text(fact.name));
}

static string semantic_parameter_fingerprint(
    const vector<SemanticParameterFact>& parameters) {
  std::ostringstream out;
  for (const auto& parameter : parameters)
    out << parameter.name << ":" << parameter.type << ":"
        << ownership_effect_name(parameter.ownership) << "\n";
  return out.str();
}

static void finalize_semantic_target_facts(
    const Program& program, const OptimizationPlan&,
    vector<SemanticTargetFact>& targets) {
  std::unordered_map<string,string> bodies;
  for (const auto& function : program.functions)
    bodies["fn:" + function.name] = body_fingerprint_text(function.body) +
        compact_semantic_text(function.result_expression.value_or(""));
  for (const auto& object : program.objects)
    for (const auto& method : object.methods)
      bodies["method:" + object.name + "." + method.name] =
          body_fingerprint_text(method.body) +
          compact_semantic_text(method.result_expression.value_or(""));
  for (const auto& domain : program.domains)
    for (const auto& handler : domain.handlers)
      bodies["handler:" + domain.name + "." + handler.name] =
          body_fingerprint_text(handler.body);
  for (const auto& test : program.tests)
    bodies["test:" + test.name] = body_fingerprint_text(test.body);
  for (const auto& benchmark : program.benchmarks)
    bodies["bench:" + benchmark.name] = body_fingerprint_text(benchmark.body);
  if (program.main) bodies["main"] = body_fingerprint_text(program.main->body);

  std::unordered_map<string,const FunctionalPipeline*> pipelines;
  for (const auto& pipeline : program.functional_pipelines)
    pipelines[pipeline.semantic_identity] = &pipeline;

  std::unordered_map<string,size_t> identity_occurrences;
  for (auto& target : targets) {
    string base = durable_identity_base(target);
    if (target.kind == "functional_pipeline") {
      auto pipeline = pipelines.find(target.semantic_identity);
      if (pipeline != pipelines.end())
        base = "entity-v1:pipeline:" + target.context + ":" +
            stable_hash(compact_semantic_text(pipeline->second->expression));
    } else if (target.kind == "functional_node") {
      for (const auto& pipeline : program.functional_pipelines) {
        auto node = std::find_if(
            pipeline.nodes.begin(), pipeline.nodes.end(),
            [&](const FunctionalNode& candidate) {
              return candidate.semantic_identity == target.semantic_identity;
            });
        if (node == pipeline.nodes.end()) continue;
        base = "entity-v1:functional-node:" + pipeline.context + ":" +
            stable_hash(compact_semantic_text(pipeline.expression)) + ":" +
            std::to_string(node->span.stage) + ":" +
            functional_node_name(node->kind);
        break;
      }
    }
    size_t occurrence = identity_occurrences[base]++;
    target.durable_identity = occurrence == 0
        ? base : base + ":occurrence:" + std::to_string(occurrence + 1);

    std::ostringstream interface_material;
    interface_material << "semantic-interface-v1\n" << target.kind << "\n"
                       << target.name << "\n" << target.type << "\n"
                       << semantic_parameter_fingerprint(target.parameters);
    if (target.has_observable_effects)
      interface_material << "effects:"
                         << observable_effect_fingerprint(
                                target.observable_effects)
                         << "\n";
    if (target.kind == "type") {
      auto object = std::find_if(
          program.objects.begin(), program.objects.end(),
          [&](const ObjectType& candidate) { return candidate.name == target.name; });
      if (object != program.objects.end()) {
        for (const auto& field : object->fields)
          interface_material << "field:" << field.name << ":" << field.type
                             << "\n";
        for (const auto& method : object->methods)
          interface_material << "method:" << method.name << ":"
                             << method.return_type.value_or("unit") << ":"
                             << method.params.size() << "\n";
      }
    } else if (target.kind == "trait") {
      auto trait = std::find_if(
          program.traits.begin(), program.traits.end(),
          [&](const Trait& candidate) { return candidate.name == target.name; });
      if (trait != program.traits.end())
        for (const auto& method : trait->methods)
          interface_material << "trait-method:" << method.name << ":"
                             << method.return_type.value_or("unit") << ":"
                             << method.params.size() << "\n";
    } else if (target.kind == "domain") {
      auto domain = std::find_if(
          program.domains.begin(), program.domains.end(),
          [&](const Domain& candidate) { return candidate.name == target.name; });
      if (domain != program.domains.end()) {
        for (const auto& field : domain->state)
          interface_material << "state:" << field.name << ":" << field.type
                             << "\n";
        for (const auto& handler : domain->handlers)
          interface_material << "handler:" << handler.name << ":"
                             << handler.reply_type.value_or("unit") << ":"
                             << handler.params.size() << "\n";
      }
    }
    if (target.kind == "domain") {
      auto domain = std::find_if(
          program.domains.begin(), program.domains.end(),
          [&](const Domain& candidate) {
            return candidate.name == target.name;
          });
      if (domain != program.domains.end())
        interface_material << "backend:"
                           << "Handler2PL"
                           << "\n";
    }
    target.semantic_interface_hash = stable_hash(interface_material.str());

    std::ostringstream implementation_material;
    implementation_material << "implementation-v1\n"
                            << interface_material.str();
    auto body = bodies.find(target.context);
    if (body != bodies.end()) implementation_material << body->second;
    for (const auto& edge : program.semantic_call_edges)
      if (edge.source == target.context) {
        implementation_material << "call:" << edge.target << ":";
        for (const auto& type : edge.argument_types)
          implementation_material << type << ",";
        implementation_material << "\n";
      }
    for (const auto& explanation : target.explanations)
      implementation_material << "\nplan:" << explanation;
    auto pipeline = pipelines.find(target.semantic_identity);
    if (pipeline != pipelines.end()) {
      implementation_material << "\npipeline:"
                              << compact_semantic_text(
                                     pipeline->second->expression);
      for (const auto& step : pipeline->second->semantic_steps) {
        implementation_material << "\nstep:"
                                << functional_node_name(step.kind) << ":";
        for (size_t index : step.source_node_indices)
          implementation_material << index << ",";
      }
    }
    target.implementation_hash = stable_hash(implementation_material.str());
  }
}

static void append_statement_targets(
    vector<SemanticTargetFact>& targets, const vector<Stmt>& body,
    const string& context, const ObservableEffects* enclosing_effects) {
  for (const auto& statement : body) {
    SemanticTargetFact fact;
    fact.semantic_identity = context + "@" + std::to_string(statement.line) +
        ":statement";
    fact.context = context;
    fact.kind = (statement.kind == Stmt::Kind::Let ||
                 statement.kind == Stmt::Kind::Var ||
                 statement.kind == Stmt::Kind::Assign)
        ? "binding" : "statement";
    fact.name = fact.kind == "binding" ? statement.a : statement.text;
    fact.type = statement.semantic_type;
    fact.line = statement.line;
    if (enclosing_effects) {
      // A callable's transitive summary is useful context, but it is not a
      // precise summary for each statement in that callable.  Phase 6A is a
      // serialization layer, so it must not manufacture target-local facts.
      fact.enclosing_callable_effects = *enclosing_effects;
      fact.has_enclosing_callable_effects = true;
    }
    fact.provenance.push_back(fact.semantic_identity);
    if (fact.kind == "binding" && !fact.type.empty())
      fact.explanations.push_back(
          "binding type was resolved by the control-flow-aware type environment");
    targets.push_back(std::move(fact));
  }
}

static vector<SemanticTargetFact> semantic_target_facts(
    const Program& program, const OptimizationPlan& plan) {
  vector<SemanticTargetFact> targets;
  for (const auto& function : program.functions) {
    SemanticTargetFact fact;
    fact.semantic_identity = "fn:" + function.name + "@" +
        std::to_string(function.line);
    fact.context = "fn:" + function.name;
    fact.kind = "function";
    fact.name = function.name;
    fact.type = function.return_type.value_or("unit");
    fact.line = function.line;
    fact.module_identity = semantic_module_name(function.name);
    fact.export_visibility = function.exported ? "exported" : "private";
    fact.export_kind = function.exported
        ? (function.generic ? "generic" : "concrete") : "private";
    fact.observable_effects = function.observable_effects;
    fact.has_observable_effects = true;
    fact.provenance.push_back(fact.semantic_identity);
    for (size_t index = 0; index < function.params.size(); ++index) {
      Effect effect = index < function.parameter_effects.size()
          ? function.parameter_effects[index] : Effect::Read;
      fact.parameters.push_back(
          {function.params[index].name, function.params[index].type, effect});
    }
    fact.explanations.push_back(
        "parameter ownership and observable effects were inferred bottom-up over the static call graph");
    targets.push_back(fact);
    append_statement_targets(targets, function.body, fact.context,
                             &function.observable_effects);

    for (const auto& specialization : function.specializations) {
      SemanticTargetFact specialized = fact;
      std::ostringstream context;
      context << "fn:" << function.name << "<";
      for (size_t index = 0;
           index < specialization.parameter_types.size(); ++index) {
        if (index) context << ",";
        context << specialization.parameter_types[index];
        if (index < specialized.parameters.size())
          specialized.parameters[index].type =
              specialization.parameter_types[index];
      }
      context << ">";
      specialized.context = context.str();
      specialized.kind = "specialization";
      specialized.semantic_identity = specialized.context + "@" +
          std::to_string(function.line);
      specialized.type = specialization.return_type;
      specialized.provenance = {specialized.semantic_identity,
                                fact.semantic_identity};
      targets.push_back(std::move(specialized));
    }
  }

  for (const auto& object : program.objects) {
    SemanticTargetFact object_fact;
    object_fact.semantic_identity = "type:" + object.name + "@" +
        std::to_string(object.line);
    object_fact.context = "type:" + object.name;
    object_fact.kind = "type";
    object_fact.name = object.name;
    object_fact.type = object.name;
    object_fact.line = object.line;
    object_fact.module_identity = semantic_module_name(object.name);
    object_fact.export_visibility = object.exported ? "exported" : "private";
    object_fact.export_kind = object.exported ? "nominal_type" : "private";
    object_fact.provenance.push_back(object_fact.semantic_identity);
    targets.push_back(std::move(object_fact));
    for (const auto& field : object.fields) {
      SemanticTargetFact field_fact;
      field_fact.semantic_identity = "field:" + object.name + "." +
          field.name + "@" + std::to_string(field.line);
      field_fact.context = "type:" + object.name;
      field_fact.kind = "field";
      field_fact.name = object.name + "." + field.name;
      field_fact.type = field.type;
      field_fact.line = field.line;
      field_fact.provenance.push_back(field_fact.semantic_identity);
      targets.push_back(std::move(field_fact));
    }
    for (const auto& method : object.methods) {
      SemanticTargetFact method_fact;
      method_fact.semantic_identity = "method:" + object.name + "." +
          method.name + "@" + std::to_string(method.line);
      method_fact.context = "method:" + object.name + "." + method.name;
      method_fact.kind = "method";
      method_fact.name = object.name + "." + method.name;
      method_fact.type = method.return_type.value_or("unit");
      method_fact.line = method.line;
      method_fact.observable_effects = method.observable_effects;
      method_fact.has_observable_effects = true;
      method_fact.parameters.push_back(
          {"self", object.name, method.receiver_effect});
      for (size_t index = 0; index < method.params.size(); ++index) {
        Effect effect = index < method.parameter_effects.size()
            ? method.parameter_effects[index] : Effect::Read;
        method_fact.parameters.push_back(
            {method.params[index].name, method.params[index].type, effect});
      }
      method_fact.provenance.push_back(method_fact.semantic_identity);
      method_fact.explanations.push_back(
          "receiver and parameter ownership were inferred from the method body and static callees");
      targets.push_back(method_fact);
      append_statement_targets(targets, method.body, method_fact.context,
                               &method.observable_effects);
    }
  }

  for (const auto& trait : program.traits) {
    SemanticTargetFact fact;
    fact.semantic_identity = "trait:" + trait.name + "@" +
        std::to_string(trait.line);
    fact.context = "trait:" + trait.name;
    fact.kind = "trait";
    fact.name = trait.name;
    fact.type = trait.name;
    fact.line = trait.line;
    fact.module_identity = semantic_module_name(trait.name);
    fact.export_visibility = trait.exported ? "exported" : "private";
    fact.export_kind = trait.exported ? "trait" : "private";
    fact.provenance.push_back(fact.semantic_identity);
    fact.explanations.push_back(
        "named trait conformance is resolved statically through concrete methods");
    targets.push_back(std::move(fact));
  }

  for (const auto& domain : program.domains) {
    SemanticTargetFact domain_fact;
    domain_fact.semantic_identity = "domain:" + domain.name + "@" +
        std::to_string(domain.line);
    domain_fact.context = "domain:" + domain.name;
    domain_fact.kind = "domain";
    domain_fact.name = domain.name;
    domain_fact.type = domain.name;
    domain_fact.line = domain.line;
    domain_fact.module_identity = semantic_module_name(domain.name);
    domain_fact.export_visibility = domain.exported ? "exported" : "private";
    domain_fact.export_kind = domain.exported ? "domain" : "private";
    domain_fact.provenance.push_back(domain_fact.semantic_identity);
    domain_fact.explanations.push_back("production synchronization: Handler2PL");
    targets.push_back(std::move(domain_fact));
    for (const auto& field : domain.state) {
      SemanticTargetFact field_fact;
      field_fact.semantic_identity = "state:" + domain.name + "." +
          field.name + "@" + std::to_string(field.line);
      field_fact.context = "domain:" + domain.name;
      field_fact.kind = "domain_state";
      field_fact.name = domain.name + "." + field.name;
      field_fact.type = field.type;
      field_fact.line = field.line;
      field_fact.provenance.push_back(field_fact.semantic_identity);
      targets.push_back(std::move(field_fact));
    }
    for (const auto& handler : domain.handlers) {
      SemanticTargetFact handler_fact;
      handler_fact.semantic_identity = "handler:" + domain.name + "." +
          handler.name + "@" + std::to_string(handler.line);
      handler_fact.context = "handler:" + domain.name + "." + handler.name;
      handler_fact.kind = "handler";
      handler_fact.name = domain.name + "." + handler.name;
      handler_fact.type = handler.reply_type.value_or("unit");
      handler_fact.line = handler.line;
      handler_fact.observable_effects = handler.observable_effects;
      handler_fact.has_observable_effects = true;
      for (const auto& parameter : handler.params)
        handler_fact.parameters.push_back(
            {parameter.name, parameter.type, Effect::Read});
      handler_fact.provenance.push_back(handler_fact.semantic_identity);
      targets.push_back(handler_fact);
      append_statement_targets(targets, handler.body, handler_fact.context,
                               &handler.observable_effects);
    }
  }

  for (const auto& specialization : program.domain_specializations) {
    auto source = std::find_if(
        program.domains.begin(), program.domains.end(),
        [&](const Domain& domain) {
          return domain.name == specialization.source_domain;
        });
    if (source == program.domains.end()) continue;
    SemanticTargetFact fact;
    fact.semantic_identity = specialization.identity();
    fact.context = fact.semantic_identity;
    fact.kind = "domain_specialization";
    fact.name = specialization.source_domain + "." + specialization.instance;
    fact.type = specialization.source_domain;
    fact.line = source->line;
    fact.source_file = source->source_file;
    fact.provenance.push_back("domain:" + specialization.source_domain + "@" +
                              std::to_string(source->line));
    for (const auto& field : source->state) {
      auto type = specialization.state_types.find(field.name);
      if (type != specialization.state_types.end())
        fact.specialization_fields.push_back({field.name, type->second});
    }
    std::sort(fact.specialization_fields.begin(),
              fact.specialization_fields.end());
    fact.explanations.push_back(
        "declared instance specializes source domain " +
        specialization.source_domain + " without runtime dynamic typing");
    targets.push_back(std::move(fact));
  }

  for (const auto& test : program.tests) {
    SemanticTargetFact fact;
    fact.semantic_identity = test.semantic_identity;
    fact.context = "test:" + test.name;
    fact.kind = "test";
    fact.name = test.name;
    fact.type = "unit";
    fact.line = test.line;
    fact.provenance.push_back(fact.semantic_identity);
    fact.explanations.push_back(
        "Moss-native test discovered from its project source declaration");
    targets.push_back(fact);
    append_statement_targets(targets, test.body, fact.context, nullptr);
  }
  for (const auto& benchmark : program.benchmarks) {
    SemanticTargetFact fact;
    fact.semantic_identity = benchmark.semantic_identity;
    fact.context = "bench:" + benchmark.name;
    fact.kind = "benchmark";
    fact.name = benchmark.name;
    fact.type = "unit";
    fact.line = benchmark.line;
    fact.provenance.push_back(fact.semantic_identity);
    fact.explanations.push_back(
        "Moss-native benchmark uses the release/highest-optimization profile");
    targets.push_back(fact);
    append_statement_targets(targets, benchmark.body, fact.context, nullptr);
  }

  if (program.main) {
    SemanticTargetFact fact;
    fact.semantic_identity = "main@" + std::to_string(program.main->line);
    fact.context = "main";
    fact.kind = "main";
    fact.name = "main";
    fact.type = "unit";
    fact.line = program.main->line;
    fact.provenance.push_back(fact.semantic_identity);
    targets.push_back(fact);
    append_statement_targets(targets, program.main->body, "main", nullptr);
  }

  for (const auto& pipeline : program.functional_pipelines) {
    ObservableEffects aggregate = resolved_empty_effects();
    for (const auto& node : pipeline.nodes) aggregate.merge(node.effects);
    SemanticTargetFact fact;
    fact.semantic_identity = pipeline.semantic_identity;
    fact.context = pipeline.context;
    fact.kind = "functional_pipeline";
    fact.name = pipeline.semantic_identity;
    fact.type = pipeline.output_type;
    fact.line = pipeline.line;
    fact.observable_effects = aggregate;
    fact.has_observable_effects = true;
    fact.provenance = pipeline_provenance(pipeline);
    fact.explanations.push_back(pipeline.decision);
    fact.explanations.insert(fact.explanations.end(),
                             pipeline.optimization_notes.begin(),
                             pipeline.optimization_notes.end());
    for (const auto& rewrite : pipeline.semantic_rewrites)
      fact.explanations.push_back(
          "semantic rewrite " + rewrite.name + ": " + rewrite.detail);
    targets.push_back(fact);
    for (const auto& node : pipeline.nodes) {
      SemanticTargetFact node_fact;
      node_fact.semantic_identity = node.semantic_identity;
      node_fact.context = pipeline.context;
      node_fact.kind = "functional_node";
      node_fact.name = functional_node_name(node.kind);
      node_fact.type = node.output_type;
      node_fact.line = node.span.line;
      node_fact.observable_effects = node.effects;
      node_fact.has_observable_effects = true;
      node_fact.parameters.push_back(
          {"element", node.input_type, node.ownership});
      node_fact.provenance = node.provenance;
      if (!node.materialization_reason.empty())
        node_fact.explanations.push_back(
            string("materialization: ") +
            functional_materialization_name(node.materialization) +
            "; reason: " + node.materialization_reason);
      if (node.semantic_work_eliminated)
        node_fact.explanations.push_back(
            "semantic work eliminated: " +
            node.semantic_elimination_reason);
      targets.push_back(std::move(node_fact));
    }
  }

  for (const auto& call : program.semantic_call_edges) {
    SemanticTargetFact fact;
    fact.semantic_identity = call.source + "@" + std::to_string(call.line) +
        ":call:" + call.target;
    fact.context = call.source;
    fact.kind = "call";
    fact.name = call.target;
    fact.line = call.line;
    fact.source_file = call.source_file;
    fact.provenance.push_back(fact.semantic_identity);
    fact.explanations.push_back(
        "call target was resolved statically by the Moss checker");
    targets.push_back(std::move(fact));
  }

  auto source_for_context = [&](const string& context, int line) -> string {
    for (const auto& function : program.functions)
      if (context == "fn:" + function.name ||
          starts_with(context, "fn:" + function.name + "<"))
        return function.source_file;
    for (const auto& object : program.objects)
      for (const auto& method : object.methods)
        if (context == "method:" + object.name + "." + method.name)
          return method.source_file;
    for (const auto& domain : program.domains) {
      for (const auto& handler : domain.handlers)
        if (context == "handler:" + domain.name + "." + handler.name)
          return handler.source_file;
      if (context == "domain:" + domain.name) return domain.source_file;
    }
    for (const auto& test : program.tests)
      if (context == "test:" + test.name) return test.source_file;
    for (const auto& benchmark : program.benchmarks)
      if (context == "bench:" + benchmark.name) return benchmark.source_file;
    if (context == "main" && program.main) return program.main->source_file;
    for (const auto& pipeline : program.functional_pipelines)
      if (pipeline.semantic_identity == context && !pipeline.source_file.empty())
        return pipeline.source_file;
    (void)line;
    return {};
  };
  for (auto& target : targets) {
    if (target.kind == "functional_pipeline") {
      auto pipeline = std::find_if(
          program.functional_pipelines.begin(), program.functional_pipelines.end(),
          [&](const FunctionalPipeline& candidate) {
            return candidate.semantic_identity == target.semantic_identity;
          });
      if (pipeline != program.functional_pipelines.end())
        target.source_file = pipeline->source_file;
    } else if (target.kind == "functional_node") {
      for (const auto& pipeline : program.functional_pipelines)
        for (const auto& node : pipeline.nodes)
          if (node.semantic_identity == target.semantic_identity)
            target.source_file = pipeline.source_file;
    }
    if (target.source_file.empty())
      target.source_file = source_for_context(target.context, target.line);
  }

  finalize_semantic_target_facts(program, plan, targets);

  std::sort(targets.begin(), targets.end(),
            [](const SemanticTargetFact& left,
               const SemanticTargetFact& right) {
              if (left.line != right.line) return left.line < right.line;
              if (left.kind != right.kind) return left.kind < right.kind;
              return left.semantic_identity < right.semantic_identity;
            });
  return targets;
}

static int semantic_target_rank(const SemanticTargetFact& fact) {
  if (fact.kind == "functional_pipeline") return 0;
  if (fact.kind == "functional_node") return 1;
  if (fact.kind == "binding") return 2;
  if (fact.kind == "statement") return 3;
  return 4;
}

static const SemanticTargetFact* resolve_semantic_target(
    const vector<SemanticTargetFact>& targets, string selector,
    const string& source_file = {}) {
  selector = trim(std::move(selector));
  if (starts_with(selector, "line:")) selector = selector.substr(5);
  bool numeric = !selector.empty() &&
      std::all_of(selector.begin(), selector.end(), [](unsigned char ch) {
        return std::isdigit(ch);
      });
  if (numeric) {
    int line = std::stoi(selector);
    const SemanticTargetFact* best = nullptr;
    for (const auto& target : targets) {
      if (target.line != line) continue;
      if (!source_file.empty() && !target.source_file.empty() &&
          target.source_file != source_file) continue;
      if (!best || semantic_target_rank(target) < semantic_target_rank(*best) ||
          (semantic_target_rank(target) == semantic_target_rank(*best) &&
           target.semantic_identity < best->semantic_identity))
        best = &target;
    }
    return best;
  }
  auto exact = std::find_if(targets.begin(), targets.end(),
                            [&](const SemanticTargetFact& target) {
                              return target.semantic_identity == selector ||
                                  target.durable_identity == selector;
                            });
  if (exact != targets.end()) return &*exact;
  if (!source_file.empty()) {
    auto scoped = std::find_if(
        targets.begin(), targets.end(), [&](const SemanticTargetFact& target) {
          return (target.source_file.empty() ||
                  target.source_file == source_file) &&
              (target.context == selector || target.name == selector ||
               target.kind + ":" + target.name == selector);
        });
    if (scoped != targets.end()) return &*scoped;
  }
  auto named = std::find_if(targets.begin(), targets.end(),
                            [&](const SemanticTargetFact& target) {
                              return target.context == selector ||
                                  target.name == selector ||
                                  target.kind + ":" + target.name == selector;
                            });
  return named == targets.end() ? nullptr : &*named;
}

static void write_agent_envelope_begin(std::ostream& out,
                                       const string& command, bool ok) {
  out << "{\n  \"protocol_version\": " << kAgentProtocolVersion
      << ",\n  \"schema_version\": ";
  write_debug_json_string(out, kAgentSchemaVersion);
  out << ",\n  \"compiler_version\": ";
  write_debug_json_string(out, kCompilerVersion);
  out << ",\n  \"command\": ";
  write_debug_json_string(out, command);
  out << ",\n  \"ok\": " << (ok ? "true" : "false") << ",\n";
}

static void write_agent_string_array(std::ostream& out,
                                     const vector<string>& values) {
  out << "[";
  for (size_t index = 0; index < values.size(); ++index) {
    if (index) out << ", ";
    write_debug_json_string(out, values[index]);
  }
  out << "]";
}

struct AgentCapabilityDescriptor {
  const char* id;
  const char* purpose;
  const char* entrypoint;
};

static const vector<AgentCapabilityDescriptor>& agent_capability_catalog() {
  static const vector<AgentCapabilityDescriptor> catalog = {
      {"structured_diagnostics", "Stable machine-readable Moss diagnostics, locations, identities, and repair alternatives.", "moss check <source> --json"},
      {"semantic_queries", "Checked-program type, ownership, effect, call, explanation, and cost facts.", "moss inspect|type|effects|ownership|calls|why|cost <target> --source <source> --json"},
      {"durable_semantic_identities", "entity-v1 identities correlate diagnostics, queries, traces, impact, and exact edits.", "semantic query result.target.durable_identity"},
      {"impact_analysis", "Changed semantic facts, dependents, affected tests, and reuse facts.", "moss impact <target> --source <source> --json"},
      {"formatter", "Canonical Moss formatting or formatting drift detection.", "moss fmt [--check] [--json]"},
      {"semantic_edits", "Exact compiler-resolved rename, expression, or argument edits.", "moss edit rename|replace-expression|change-argument ... --json"},
      {"affected_tests", "Conservative semantic-impact selected verification.", "moss test --affected --json"},
      {"static_cost_facts", "Known materialization, traversal, specialization, message-materialization, and backend facts; not runtime predictions.", "moss cost <target> --source <source> --json"},
      {"synchronization_plan", "Concrete graph plus R/W/C/X*/ProtectedRead/LockSet/ClassSet, modes, ranks, and conflict witnesses.", "moss inspect|effects|why <target> --source <source> --json"},
      {"package_project_driver", "Resolve Moss packages and orchestrate project build, run, test, benchmark, and clean operations.", "margo build|run|test|bench|clean"},
      {"package_dependencies", "Resolve local path and Git package dependencies declared in Moss.toml.", "Moss.toml [dependencies] with path or git/rev/tag/branch"},
      {"package_lockfile", "Freeze resolved Git dependency commits deterministically.", "Moss.lock"},
      {"module_interfaces", "Generated .mossi semantic interfaces are source-free provider truth, not generated Rust.", "margo build --json -> result.artifacts.module_interfaces"},
      {"fast_debug", "Direct execution of checked reachable Moss source when behavior is wrong.", "moss run --interp <source> | moss debug <project-or-source>"},
      {"structured_execution_trace", "Bounded deterministic newline-delimited semantic events during Fast Debug.", "moss run --interp --trace <source> | moss debug <target> --trace"},
      {"project_workflow", "Margo owns canonical package/project orchestration; Moss remains the module and semantic authority.", "margo build|run|test|bench|clean"},
  };
  return catalog;
}

static void write_agent_capability_catalog(std::ostream& out, bool detailed) {
  out << "[";
  const auto& catalog = agent_capability_catalog();
  for (size_t index = 0; index < catalog.size(); ++index) {
    if (index) out << ", ";
    out << "{\"id\":";
    write_debug_json_string(out, catalog[index].id);
    if (detailed) {
      out << ",\"purpose\":";
      write_debug_json_string(out, catalog[index].purpose);
      out << ",\"entrypoint\":";
      write_debug_json_string(out, catalog[index].entrypoint);
    }
    out << "}";
  }
  out << "]";
}

static void write_agent_command_schema(
    std::ostream& out, const string& name, const string& purpose,
    const vector<string>& required, const vector<string>& optional,
    const string& response_shape, const vector<string>& identities,
    const vector<string>& failures) {
  out << "{\"name\":";
  write_debug_json_string(out, name);
  out << ",\"purpose\":";
  write_debug_json_string(out, purpose);
  out << ",\"required_inputs\":";
  write_agent_string_array(out, required);
  out << ",\"optional_inputs\":";
  write_agent_string_array(out, optional);
  out << ",\"response_shape\":";
  write_debug_json_string(out, response_shape);
  out << ",\"stable_identifiers\":";
  write_agent_string_array(out, identities);
  out << ",\"common_failure_modes\":";
  write_agent_string_array(out, failures);
  out << "}";
}

static void write_observable_effects_json(
    std::ostream& out, const ObservableEffects& effects) {
  out << "{\"local_capture_read\": "
      << (effects.local_capture_read ? "true" : "false")
      << ", \"local_mutation\": "
      << (effects.local_mutation ? "true" : "false")
      << ", \"domain_read\": "
      << (effects.domain_read ? "true" : "false")
      << ", \"domain_write\": "
      << (effects.domain_write ? "true" : "false")
      << ", \"message\": " << (effects.message ? "true" : "false")
      << ", \"external_io\": "
      << (effects.external_io ? "true" : "false")
      << ", \"may_fail\": " << (effects.may_fail ? "true" : "false")
      << ", \"may_diverge\": "
      << (effects.may_diverge ? "true" : "false")
      << ", \"unresolved\": "
      << (effects.unresolved ? "true" : "false") << "}";
}

static void write_semantic_target_json(std::ostream& out,
                                       const SemanticTargetFact& target,
                                       const string& source_file) {
  out << "{\"semantic_identity\": ";
  write_debug_json_string(out, target.semantic_identity);
  out << ", \"durable_identity\": ";
  write_debug_json_string(out, target.durable_identity);
  out << ", \"identity_version\": \"entity-v1\"";
  out << ", \"construct_kind\": ";
  write_debug_json_string(out, target.kind);
  out << ", \"source_identity\": ";
  write_debug_json_string(out, target.semantic_identity);
  out << ", \"specialization_identity\": ";
  if (target.kind == "domain_specialization" ||
      target.kind == "specialization")
    write_debug_json_string(out, target.semantic_identity);
  else
    out << "null";
  out << ", \"name\": ";
  write_debug_json_string(out, target.name);
  out << ", \"module_identity\": ";
  if (target.module_identity.empty()) out << "null";
  else write_debug_json_string(out, target.module_identity);
  out << ", \"export_visibility\": ";
  if (target.export_visibility.empty()) out << "null";
  else write_debug_json_string(out, target.export_visibility);
  out << ", \"export_kind\": ";
  if (target.export_kind.empty()) out << "null";
  else write_debug_json_string(out, target.export_kind);
  out << ", \"source\": {\"file\": ";
  write_debug_json_string(out, target.source_file.empty()
      ? source_file : target.source_file);
  out << ", \"line\": " << target.line
      << ", \"column\": 1, \"span\": {\"start_line\": "
      << target.line << ", \"start_column\": 1, \"end_line\": "
      << target.line << ", \"end_column\": 1}}, \"type\": ";
  if (target.type.empty()) out << "null";
  else write_debug_json_string(out, target.type);
  out << ", \"specialization_fields\": [";
  for (size_t index = 0; index < target.specialization_fields.size(); ++index) {
    if (index) out << ", ";
    out << "{\"name\": ";
    write_debug_json_string(out, target.specialization_fields[index].first);
    out << ", \"type\": ";
    write_debug_json_string(out, target.specialization_fields[index].second);
    out << "}";
  }
  out << "]";
  out << ", \"provenance\": ";
  write_agent_string_array(out, target.provenance);
  out << ", \"implementation_hash\": ";
  write_debug_json_string(out, target.implementation_hash);
  out << ", \"semantic_interface_hash\": ";
  write_debug_json_string(out, target.semantic_interface_hash);
  out << "}";
}

static void write_parameters_json(std::ostream& out,
                                  const SemanticTargetFact& target) {
  out << "[";
  for (size_t index = 0; index < target.parameters.size(); ++index) {
    if (index) out << ", ";
    const auto& parameter = target.parameters[index];
    out << "{\"name\": ";
    write_debug_json_string(out, parameter.name);
    out << ", \"type\": ";
    if (parameter.type.empty()) out << "null";
    else write_debug_json_string(out, parameter.type);
    out << ", \"effect\": ";
    write_debug_json_string(out, ownership_effect_name(parameter.ownership));
    out << "}";
  }
  out << "]";
}

static vector<const SemanticCallEdge*> calls_for_target(
    const Program& program, const SemanticTargetFact& target) {
  vector<const SemanticCallEdge*> result;
  for (const auto& edge : program.semantic_call_edges)
    if (edge.source == target.context) result.push_back(&edge);
  return result;
}

static vector<const SemanticCallEdge*> callers_for_target(
    const Program& program, const SemanticTargetFact& target) {
  vector<const SemanticCallEdge*> result;
  for (const auto& edge : program.semantic_call_edges)
    if (edge.target == target.context) result.push_back(&edge);
  return result;
}

static string semantic_call_target_kind(const string& target) {
  if (starts_with(target, "fn:")) return "function";
  if (starts_with(target, "method:")) return "method";
  if (starts_with(target, "handler:")) return "handler";
  return "resolved_callable";
}

static std::optional<string> specialization_identity_for_call(
    const Program& program, const SemanticCallEdge& edge) {
  if (!starts_with(edge.target, "fn:") || edge.argument_types.empty())
    return std::nullopt;
  string name = edge.target.substr(3);
  auto function = std::find_if(
      program.functions.begin(), program.functions.end(),
      [&](const Function& candidate) { return candidate.name == name; });
  if (function == program.functions.end()) return std::nullopt;
  for (const auto& specialization : function->specializations) {
    if (specialization.parameter_types != edge.argument_types) continue;
    string identity = "specialization:" + name + "<";
    for (size_t index = 0; index < specialization.parameter_types.size(); ++index) {
      if (index) identity += ",";
      identity += specialization.parameter_types[index];
    }
    identity += ">";
    return identity;
  }
  return std::nullopt;
}

static string diagnostic_code_for_message(const string& message) {
  if (message.find("duplicate function") != string::npos ||
      message.find("duplicate domain") != string::npos ||
      message.find("duplicate object type") != string::npos ||
      message.find("duplicate trait") != string::npos ||
      message.find("duplicate exported name") != string::npos)
    return "DUPLICATE_SYMBOL";
  if (message.find("duplicate test name") != string::npos)
    return "TEST_DISCOVERY_ERROR";
  if (message.find("duplicate benchmark name") != string::npos)
    return "BENCHMARK_CONFIGURATION_ERROR";
  if (starts_with(message, "assert") ||
      message.find("assertEqual") != string::npos ||
      message.find("test assertions") != string::npos)
    return "TEST_ASSERTION_CONFIGURATION_ERROR";
  if (message.find("recursive local call cycle") != string::npos)
    return "RECURSION_CYCLE";
  if (message.find("conflicting domain types across control-flow paths") !=
      string::npos)
    return "TYPE_BRANCH_CONFLICT";
  if (message.find("was transferred") != string::npos ||
      message.find("was consumed") != string::npos)
    return "OWNERSHIP_USE_AFTER_CONSUME";
  if (message.find("conflicting") != string::npos &&
      message.find("access") != string::npos)
    return "OWNERSHIP_CONFLICTING_ACCESS";
  if (message.find("cannot infer") != string::npos)
    return "TYPE_INFERENCE_FAILED";
  if (message.find("unknown") != string::npos)
    return "UNKNOWN_SYMBOL_OR_TYPE";
  if (message.find("expects") != string::npos ||
      message.find("type mismatch") != string::npos ||
      message.find("has type") != string::npos)
    return "TYPE_MISMATCH";
  if (message.find("functional") != string::npos)
    return "FUNCTIONAL_SEMANTIC_ERROR";
  return "MOSS_COMPILE_ERROR";
}

static string semantic_identity_near_line(const Program* program, int line) {
  if (!program) return {};
  string best;
  int best_line = -1;
  auto consider = [&](int candidate_line, const string& identity) {
    if (candidate_line <= line && candidate_line >= best_line) {
      best_line = candidate_line;
      best = identity;
    }
  };
  for (const auto& function : program->functions)
    consider(function.line,
             "fn:" + function.name + "@" + std::to_string(function.line));
  for (const auto& object : program->objects) {
    consider(object.line,
             "type:" + object.name + "@" + std::to_string(object.line));
    for (const auto& method : object.methods)
      consider(method.line, "method:" + object.name + "." + method.name +
                               "@" + std::to_string(method.line));
  }
  for (const auto& domain : program->domains) {
    consider(domain.line,
             "domain:" + domain.name + "@" + std::to_string(domain.line));
    for (const auto& handler : domain.handlers)
      consider(handler.line, "handler:" + domain.name + "." + handler.name +
                                "@" + std::to_string(handler.line));
  }
  for (const auto& test : program->tests)
    consider(test.line, test.semantic_identity);
  for (const auto& benchmark : program->benchmarks)
    consider(benchmark.line, benchmark.semantic_identity);
  if (program->main)
    consider(program->main->line,
             "main@" + std::to_string(program->main->line));
  for (const auto& pipeline : program->functional_pipelines)
    consider(pipeline.line, pipeline.semantic_identity);
  return best;
}

static void write_structured_error(
    std::ostream& out, const string& command, const string& code,
    const string& message, const string& source_file = {}, int line = 0,
    const string& semantic_identity = {}, const string& symbol = {}) {
  write_agent_envelope_begin(out, command, false);
  out << "  \"error\": {\"code\": ";
  write_debug_json_string(out, code);
  out << ", \"severity\": \"error\", \"message\": ";
  write_debug_json_string(out, message);
  out << ", \"source_file\": ";
  if (source_file.empty()) out << "null";
  else write_debug_json_string(out, source_file);
  out << ", \"line\": " << line << ", \"column\": 1, \"span\": ";
  if (line <= 0) out << "null";
  else
    out << "{\"start_line\": " << line
        << ", \"start_column\": 1, \"end_line\": " << line
        << ", \"end_column\": 1}";
  out << ", \"semantic_identity\": ";
  if (semantic_identity.empty()) out << "null";
  else write_debug_json_string(out, semantic_identity);
  out << ", \"symbol\": ";
  if (symbol.empty()) out << "null";
  else write_debug_json_string(out, symbol);
  out << ", \"details\": {\"ownership_expected\": ";
  if (code == "OWNERSHIP_USE_AFTER_CONSUME")
    write_debug_json_string(out, "AVAILABLE");
  else out << "null";
  out << ", \"ownership_actual\": ";
  if (code == "OWNERSHIP_USE_AFTER_CONSUME")
    write_debug_json_string(out, "CONSUMED");
  else out << "null";
  out << ", \"consume_line\": ";
  size_t consume_marker = message.find(" at line ");
  if (code == "OWNERSHIP_USE_AFTER_CONSUME" &&
      consume_marker != string::npos) {
    consume_marker += string(" at line ").size();
    size_t end = consume_marker;
    while (end < message.size() &&
           std::isdigit(static_cast<unsigned char>(message[end])))
      ++end;
    out << message.substr(consume_marker, end - consume_marker);
  } else {
    out << "null";
  }
  out << ", \"recursion_witness\": ";
  if (code == "RECURSION_CYCLE") write_debug_json_string(out, message);
  else out << "null";
  out << "}, \"fixes\": [";
  bool has_fix = false;
  if (message.find("tabs are not allowed") != string::npos) {
    out << "{\"kind\": \"canonical_format\", \"confidence\": "
           "\"unique\", \"command\": \"moss fmt\", \"target\": ";
    write_debug_json_string(out, source_file);
    out << "}";
    has_fix = true;
  }
  if (message.find("declaration must end with ':'") != string::npos) {
    if (has_fix) out << ", ";
    out << "{\"kind\": \"add_block_colon\", \"confidence\": "
           "\"unique\", \"target\": {\"source_file\": ";
    write_debug_json_string(out, source_file);
    out << ", \"line\": " << line << "}}";
    has_fix = true;
  }
  out << "], \"legal_alternatives\": ";
  vector<string> alternatives;
  if (code == "OWNERSHIP_USE_AFTER_CONSUME")
    alternatives = {
        "make the callee READ the value if that matches program intent",
        "keep the ownership transfer and stop using the old binding",
        "construct an explicit new owned value or explicit deep copy"};
  else if (code == "TYPE_INFERENCE_FAILED")
    alternatives = {
        "add a concrete type annotation",
        "use the value in a context that determines one static type"};
  else if (code == "UNKNOWN_SYMBOL_OR_TYPE")
    alternatives = {
        "rename the reference to an existing static symbol",
        "declare the missing symbol or concrete type"};
  write_agent_string_array(out, alternatives);
  out << "}\n}\n";
}

static void write_bootstrap_json(std::ostream& out,
                                 const string& command,
                                 const std::optional<string>& project_root) {
  write_agent_envelope_begin(out, "agent " + command, true);
  out << "  \"result\": {\n    \"language_version\": ";
  write_debug_json_string(out, kMossLanguageVersion);
  out << ",\n    \"agent_protocol_version\": " << kAgentProtocolVersion;
  out << ",\n    \"compiler_version\": ";
  write_debug_json_string(out, kCompilerVersion);
  out << ",\n    \"project_root\": ";
  if (project_root) write_debug_json_string(out, *project_root);
  else out << "null";
  out << ",\n    \"capabilities\": ";
  write_agent_string_array(
      out, {"structured_diagnostics", "semantic_inspection",
            "static_call_graph",
            "ownership_effects", "observable_effects",
            "functional_optimization_explanations",
            "backend_lowering_explanations", "project_builds",
            "native_tests", "native_benchmarks", "benchmark_baselines",
            "durable_semantic_identities", "semantic_hashing",
            "impact_analysis", "incremental_verification",
            "modules", "qualified_imports", "module_interfaces",
            "generic_specialization_identity", "interpreted_domains",
            "fast_debug",
            "affected_tests", "formatter", "canonical_formatter", "semantic_edits",
            "repair_actions", "static_cost_facts", "source_provenance",
            "first_order_effect_graph", "structured_execution_trace",
            "synchronization_schema", "synchronization_plan", "concrete_domain_graph",
            "domain_ranks", "package_project_driver", "package_dependencies",
            "package_lockfile"});
  out << ",\n    \"capability_catalog\": ";
  write_agent_capability_catalog(out, command == "capabilities" || command == "schema");
  out << ",\n    \"capability_flags\": {"
         "\"impact_analysis\": true, "
         "\"incremental_verification\": true, "
         "\"affected_tests\": true, "
         "\"formatter\": true, "
         "\"semantic_edits\": true, "
         "\"repair_actions\": true, "
         "\"cost_facts\": true, "
         "\"package_project_driver\": true, "
         "\"package_dependencies\": true, "
         "\"package_lockfile\": true}";
  out << ",\n    \"commands\": ";
  write_agent_string_array(
      out, {"moss check <source> --json",
            "moss inspect <target> --source <source> --json",
            "moss type <target> --source <source> --json",
            "moss effects <target> --source <source> --json",
            "moss ownership <target> --source <source> --json",
            "moss calls <target> --source <source> --json",
            "moss why <target> --source <source> --json",
            "moss cost <target> --source <source> --json",
            "moss inspect main --source <source> --json (includes concrete_domain_graph)",
            "moss impact <target> [--source <source>] --json",
            "moss edit rename <entity-id> <new-name> --json",
            "moss edit replace-expression <entity-id> <expression> --json",
            "moss edit change-argument <call-id> <index> <expression> --json",
            "moss fmt [--check] [--json]",
            "margo build [--release] [--json]",
            "margo run [--release]",
            "margo test [filter] [--release] [--json]",
            "margo bench [filter] [--json]",
            "margo clean",
            "Moss.toml package manifest; Moss.lock resolved Git dependency lock",
            "module-qualified imports and versioned .mossi interfaces",
            "moss test --affected [--json] (semantic reduced verification)",
            "moss build|test|bench|clean (project-command compatibility paths)"});
  out << ",\n    \"semantic_queries\": ["
         "{\"name\":\"inspect\",\"purpose\":\"compact checked target summary, callers, topology, and known planning facts\",\"command\":\"moss inspect <target> --source <source> --json\"},"
         "{\"name\":\"type\",\"purpose\":\"statically resolved type and specialization facts\",\"command\":\"moss type <target> --source <source> --json\"},"
         "{\"name\":\"effects\",\"purpose\":\"READ/WRITE/CONSUME and separate observable effects; includes synchronization plan\",\"command\":\"moss effects <target> --source <source> --json\"},"
         "{\"name\":\"ownership\",\"purpose\":\"inferred access capability and its checked reason\",\"command\":\"moss ownership <target> --source <source> --json\"},"
         "{\"name\":\"calls\",\"purpose\":\"direct statically resolved callers and callees\",\"command\":\"moss calls <target> --source <source> --json\"},"
         "{\"name\":\"why\",\"purpose\":\"stored functional, backend, and synchronization decision explanations\",\"command\":\"moss why <target> --source <source> --json\"},"
         "{\"name\":\"cost\",\"purpose\":\"known static cost facts, not runtime predictions\",\"command\":\"moss cost <target> --source <source> --json\"},"
         "{\"name\":\"impact\",\"purpose\":\"changed semantic unit, dependents, affected tests, and reuse facts\",\"command\":\"moss impact <target> --source <source> --json\"}]";
  out << ",\n    \"actions\": ["
         "{\"name\":\"check\",\"command\":\"moss check <source> --json\",\"purpose\":\"structured Moss diagnostics\"},"
         "{\"name\":\"format\",\"command\":\"moss fmt [--check] [--json]\",\"purpose\":\"canonical source formatting\"},"
         "{\"name\":\"semantic_edit\",\"command\":\"moss edit rename|replace-expression|change-argument ... --json\",\"purpose\":\"exact compiler-resolved source edit\"},"
         "{\"name\":\"affected_test\",\"command\":\"moss test --affected --json\",\"purpose\":\"conservative impact-selected verification\"},"
         "{\"name\":\"package_build\",\"command\":\"margo build [--release] [--json]\",\"purpose\":\"canonical package build and dependency resolution\"},"
         "{\"name\":\"package_run\",\"command\":\"margo run [--release]\",\"purpose\":\"canonical package build and execution\"},"
         "{\"name\":\"package_test\",\"command\":\"margo test [filter] [--release] [--json]\",\"purpose\":\"canonical root-package test execution\"},"
         "{\"name\":\"package_bench\",\"command\":\"margo bench [filter] [--json]\",\"purpose\":\"canonical package benchmark execution\"},"
         "{\"name\":\"package_clean\",\"command\":\"margo clean\",\"purpose\":\"remove project-local artifacts without clearing the shared Margo cache\"}]";
  out << ",\n    \"debugging_features\": ["
         "{\"name\":\"fast_debug\",\"command\":\"moss run --interp <source> | moss debug <project-or-source>\",\"purpose\":\"execute checked reachable Moss source without rustc\",\"limitations\":[\"no mixed interpreted/native Moss closure\",\"source-free providers require source\"]},"
         "{\"name\":\"structured_execution_trace\",\"command\":\"moss run --interp --trace <source> | moss debug <target> --trace\",\"format\":\"newline-delimited JSON on stderr\",\"events\":[\"function/handler entry and exit\",\"local/state access\",\"branch\",\"return/reply\",\"message\",\"assertion\"],\"limitations\":[\"no trace slicing/query API\",\"no physical lock or schedule simulation\"]}]";
  out << ",\n    \"discovery\": {\"capabilities_command\": \"moss agent capabilities --json\", \"schema_command\": \"moss agent schema --json\", \"protocol_vendor\": \"Moss\"}";
  out << ",\n    \"recommended_workflow\": ";
  write_agent_string_array(
      out, {"Run moss agent bootstrap --json before modifying Moss source.",
            "Use Margo for package/project operations: margo build|run|test|bench|clean.",
            "Run moss check --json before guessing at a Moss error.",
            "Use inspect, why, effects, ownership, and cost as needed.",
            "Edit Moss source, never generated Rust.",
            "Run moss fmt.",
            "Run moss impact <target> --json.",
            "Run moss test --affected during iteration.",
            "Run the full root-package tests with margo test when appropriate.",
            "Prefer structured --json output for automation.",
            "Moss owns module/.mossi/semantic truth; Margo supplies package artifacts."});
  out << ",\n    \"workflow_hints\": ["
         "{\"question\":\"What is this symbol or concrete domain instance?\",\"capability\":\"inspect\",\"command\":\"moss inspect <target> --source <source> --json\"},"
         "{\"question\":\"What type or specialization did this resolve to?\",\"capability\":\"type\",\"command\":\"moss type <target> --source <source> --json\"},"
         "{\"question\":\"What does this READ, WRITE, or CONSUME?\",\"capability\":\"effects\",\"command\":\"moss effects <target> --source <source> --json\"},"
         "{\"question\":\"What access capability does this call require?\",\"capability\":\"ownership\",\"command\":\"moss ownership <target> --source <source> --json\"},"
         "{\"question\":\"What direct calls and callers are known?\",\"capability\":\"calls\",\"command\":\"moss calls <target> --source <source> --json\"},"
         "{\"question\":\"Why was a semantic, optimization, backend, or synchronization decision made?\",\"capability\":\"why\",\"command\":\"moss why <target> --source <source> --json\"},"
         "{\"question\":\"What static cost facts are known?\",\"capability\":\"cost\",\"command\":\"moss cost <target> --source <source> --json\"},"
         "{\"question\":\"What could this edit affect?\",\"capability\":\"impact\",\"command\":\"moss impact <target> --source <source> --json\"},"
         "{\"question\":\"How do I resolve, build, run, test, benchmark, or clean a package project?\",\"capability\":\"package_project_driver\",\"command\":\"margo build|run|test|bench|clean\"},"
         "{\"question\":\"What synchronization classes, ranks, modes, or conflict witnesses are derived?\",\"capability\":\"synchronization_plan\",\"command\":\"moss inspect|effects|why <target> --source <source> --json\"},"
         "{\"question\":\"What happened when checked code executed?\",\"capability\":\"fast_debug\",\"command\":\"moss run --interp --trace <source>\"}]";
  out << ",\n    \"safety_rules\": ";
  write_agent_string_array(
      out, {"Do not edit generated Rust.",
            "Do not infer dynamic targets; Moss dispatch is statically closed.",
            "Treat synchronous message as an explicit domain value boundary; await is retired.",
            "Do not self-send or chain handlers on the same domain; use an ordinary helper.",
            "Domain handles are static routing capabilities and do not cross ordinary value boundaries.",
            "Do not write locks or dynamically create domains; the compiler owns synchronization and topology.",
            "Preserve Moss diagnostics and source provenance."});
  out << ",\n    \"language_constraints\": ";
  write_agent_string_array(
      out, {"message is synchronous and reply terminates a handler",
            "await and spawn are retired source syntax",
            "incoming message payloads are immutable snapshots",
            "domain routes and concrete instances are statically composed",
            "traits are structural and dispatch/specialization are static",
            "ordinary recursion and general first-class closures are unsupported in v0.1"});
  out << ",\n    \"agents_md_snippet\": ";
  write_debug_json_string(
      out,
      "This repository uses Moss.\n\nBefore changing Moss source, run:\n\n"
      "    ./moss agent bootstrap --json\n\nUse Margo for package/project "
      "operations (./margo build|run|test|bench|clean). Use Moss semantic queries and "
      "structured diagnostics instead of reverse-engineering generated "
      "Rust.\n\nAfter edits, follow the workflow returned by bootstrap.");
  if (command == "schema") {
    out << ",\n    \"schema\": {\"envelope_fields\": ";
    write_agent_string_array(
        out, {"protocol_version", "schema_version", "compiler_version",
              "command", "ok", "result", "error"});
    out << ", \"target_selectors\": ";
    write_agent_string_array(
        out, {"durable entity-v1 identity", "debug/source provenance identity",
              "construct name", "line:<number>"});
    out << ", \"project_result_kinds\": ";
    write_agent_string_array(out, {"build", "test", "bench", "impact",
                                   "fmt", "edit", "cost"});
    out << ", \"stable_project_identities\": ";
      write_agent_string_array(
      out, {"test:<relative-source>:<name>",
              "bench:<relative-source>:<name>",
              "module:<project>::<module>",
              "specialization:<module>:<generic>:<type-tuple>"});
    out << ", \"synchronization_diagnostics\": {"
           "\"availability\": \"derived\", "
           "\"fields\": [\"graph_identity\", \"domains\", \"sync_classes\", "
           "\"handlers\", \"leaf_to_class\", \"conflicts\", \"metrics\", \"handler_order\", \"conflict_matrix\", \"class_opportunities\", \"class_splits\"], "
           "\"semantics\": \"compiler-owned graph-relative SynchronizationPlan; production handler-level 2PL\"}";
    out << ", \"diagnostic_codes_are_stable\": true, "
           "\"diagnostic_repair_fields\": [\"fixes\", "
           "\"legal_alternatives\"], "
           "\"identity_contracts\": {"
           "\"debug_provenance\": \"build/source-layout scoped\", "
           "\"durable_semantic_entity\": \"entity-v1\"}, "
           "\"command_schemas\": [";
    write_agent_command_schema(
        out, "agent_bootstrap", "Discover the concise live Moss agent capability manifest.",
        {"--json"}, {}, "moss-agent-1 envelope with result manifest",
        {"language_version", "agent_protocol_version", "compiler_version"},
        {"AGENT_COMMAND_INVALID", "AGENT_ARGUMENT_INVALID"});
    out << ',';
    write_agent_command_schema(
        out, "agent_capabilities", "Discover detailed capability purposes and entrypoints.",
        {"--json"}, {}, "moss-agent-1 envelope with capability_catalog",
        {"capability id"}, {"AGENT_COMMAND_INVALID", "AGENT_ARGUMENT_INVALID"});
    out << ',';
    write_agent_command_schema(
        out, "agent_schema", "Discover machine contracts for public agent-facing command groups.",
        {"--json"}, {}, "moss-agent-1 envelope with schema.command_schemas",
        {"identity contracts"}, {"AGENT_COMMAND_INVALID", "AGENT_ARGUMENT_INVALID"});
    out << ',';
    write_agent_command_schema(
        out, "check", "Check Moss and return stable structured diagnostics.",
        {"source", "--json"}, {}, "moss-agent-1 envelope with diagnostics or error",
        {"source_identity when available"},
        {"MOSS_COMPILE_ERROR", "OWNERSHIP_USE_AFTER_CONSUME", "TYPE_INFERENCE_FAILED"});
    out << ',';
    write_agent_command_schema(
        out, "semantic_query", "Inspect existing checked facts; operations are inspect, type, effects, ownership, calls, why, and cost.",
        {"operation", "target", "--source", "--json"}, {"-O"},
        "moss-agent-1 envelope with target plus operation-specific facts",
        {"entity-v1", "source_identity", "specialization_identity"},
        {"QUERY_SOURCE_REQUIRED", "QUERY_TARGET_NOT_FOUND"});
    out << ',';
    write_agent_command_schema(
        out, "impact", "Report changed semantic facts, dependents, affected tests, and incremental reuse.",
        {"target", "--json"}, {"--source"}, "moss-agent-1 envelope with impact result",
        {"entity-v1", "test/benchmark project identities"},
        {"IMPACT_JSON_REQUIRED", "IMPACT_TARGET_INVALID", "IMPACT_SOURCE_REQUIRED"});
    out << ',';
    write_agent_command_schema(
        out, "format", "Canonicalize Moss source or report formatting drift.",
        {}, {"--check", "--json", "source"},
        "moss-agent-1 envelope in JSON mode with changed file ranges",
        {"physical source path"}, {"FORMAT_SOURCE_REQUIRED", "MOSS_COMPILE_ERROR"});
    out << ',';
    write_agent_command_schema(
        out, "semantic_edit", "Perform an exact compiler-resolved rename, expression replacement, or argument replacement.",
        {"operation", "semantic identity", "operands", "--json"}, {"--source"},
        "moss-agent-1 envelope with changed files and resulting identity",
        {"entity-v1", "physical source ranges"},
        {"EDIT_ARGUMENT_INVALID", "EDIT_TARGET_STALE", "QUERY_TARGET_NOT_FOUND"});
    out << ',';
    write_agent_command_schema(
        out, "package_project_driver", "Resolve packages and build, run, test, benchmark, or clean the root project through Margo.",
        {"Margo project command"}, {"--json", "--release", "filter"},
        "Margo project result with resolved package dependency/artifact facts",
        {"package identity", "resolved Git commit", "module artifact identities"},
        {"package dependency cycle", "lockfile error", "dependency acquisition/build failure"});
    out << ',';
    write_agent_command_schema(
        out, "project_build_test_bench_compatibility", "Use Moss's retained compatibility project commands when a compiler-only workflow requires them.",
        {"project command"}, {"--json", "--release", "filter", "--affected", "baseline options"},
        "moss-agent-1 envelope with project artifacts/results",
        {"test", "bench", "module", "specialization project identities"},
        {"PROJECT_MANIFEST_ERROR", "TEST_DISCOVERY_ERROR", "BENCHMARK_CONFIGURATION_ERROR"});
    out << ',';
    write_agent_command_schema(
        out, "synchronization_introspection", "Read the authoritative graph-relative SynchronizationPlan through inspect, effects, or why.",
        {"semantic query inputs"}, {},
        "synchronization_plan with instances, R/W/C/X*/ProtectedRead/LockSet/ClassSet, modes, ranks, and witnesses",
        {"concrete_instance_id", "specialization_id", "class_id", "handler_identity"},
        {"QUERY_SOURCE_REQUIRED", "QUERY_TARGET_NOT_FOUND"});
    out << ',';
    write_agent_command_schema(
        out, "fast_debug", "Execute checked reachable Moss source directly; trace is an optional deterministic semantic event stream.",
        {"source or project"}, {"--trace"},
        "program output; --trace emits newline-delimited JSON on stderr",
        {"source_identity", "entity-v1 where emitted"},
        {"unsupported interpreter construct", "source-free provider requires source"});
    out << ',';
    write_agent_command_schema(
        out, "module_interface", "Discover .mossi semantic interface artifacts from a successful module project build.",
        {"margo build --json"}, {},
        "build result.artifacts.module_interfaces paths; .mossi holds semantic export truth",
        {"module identity", "specialization identity"},
        {"PROJECT_MANIFEST_ERROR", "incompatible provider interface"});
    out << "]}";
  } else if (command == "session-report-template") {
    out << ",\n    \"session_report_questions\": ";
    write_agent_string_array(
        out, {"Which Moss agent/compiler features did you use?",
              "Which features materially reduced iterations or ambiguity?",
              "How many significant edit -> check -> repair cycles occurred?",
              "Where did you still have to guess?",
              "Did impact analysis or affected testing avoid unnecessary work?",
              "Which compiler-agent improvement would have saved the most time?"});
  }
  out << "\n  }\n}\n";
}

static void write_check_json(std::ostream& out, const string& source_file,
                             const vector<Warning>& warnings,
                             const Program* program) {
  write_agent_envelope_begin(out, "check", true);
  out << "  \"result\": {\"source_file\": ";
  write_debug_json_string(out, source_file);
  out << ", \"diagnostics\": [";
  for (size_t index = 0; index < warnings.size(); ++index) {
    if (index) out << ", ";
    const auto& warning = warnings[index];
    out << "{\"code\": ";
    write_debug_json_string(out, warning.code);
    out << ", \"severity\": \"warning\", \"message\": ";
    write_debug_json_string(out, warning.message);
    out << ", \"source_file\": ";
    write_debug_json_string(out, source_file);
    out << ", \"line\": " << warning.line
        << ", \"column\": 1, \"span\": {\"start_line\": "
        << warning.line
        << ", \"start_column\": 1, \"end_line\": " << warning.line
        << ", \"end_column\": 1}, \"semantic_identity\": ";
    string identity = semantic_identity_near_line(program, warning.line);
    if (identity.empty()) out << "null";
    else write_debug_json_string(out, identity);
    out << ", \"symbol\": null, \"details\": {}, \"fixes\": [], "
           "\"legal_alternatives\": []}";
  }
  out << "]}\n}\n";
}

static void write_calls_result(std::ostream& out, const Program& program,
                               const SemanticTargetFact& target) {
  auto calls = calls_for_target(program, target);
  out << "[";
  for (size_t index = 0; index < calls.size(); ++index) {
    if (index) out << ", ";
    out << "{\"target\": ";
    write_debug_json_string(out, calls[index]->target);
    out << ", \"resolved\": true, \"target_kind\": ";
    write_debug_json_string(out,
                            semantic_call_target_kind(calls[index]->target));
    out << ", \"line\": " << calls[index]->line
        << ", \"source_file\": ";
    write_debug_json_string(out, calls[index]->source_file);
    out << ", \"argument_types\": ";
    write_agent_string_array(out, calls[index]->argument_types);
    out << ", \"specialization_identity\": ";
    auto specialization = specialization_identity_for_call(program, *calls[index]);
    if (specialization) write_debug_json_string(out, *specialization);
    else out << "null";
    out << "}";
  }
  out << "]";
}

static void write_callers_result(std::ostream& out, const Program& program,
                                 const SemanticTargetFact& target) {
  auto callers = callers_for_target(program, target);
  out << "[";
  for (size_t index = 0; index < callers.size(); ++index) {
    if (index) out << ", ";
    out << "{\"source\": ";
    write_debug_json_string(out, callers[index]->source);
    out << ", \"line\": " << callers[index]->line
        << ", \"source_file\": ";
    write_debug_json_string(out, callers[index]->source_file);
    out << "}";
  }
  out << "]";
}

static void write_cost_result(std::ostream& out, const Program& program,
                              const OptimizationPlan&,
                              const vector<Warning>& warnings,
                              const SemanticTargetFact& target) {
  size_t specialization_count = 0;
  if (target.kind == "function") {
    auto function = std::find_if(
        program.functions.begin(), program.functions.end(),
        [&](const Function& candidate) {
          return candidate.name == target.name;
        });
    if (function != program.functions.end())
      specialization_count = function->specializations.size();
  }
  const FunctionalPipeline* selected_pipeline = nullptr;
  if (target.kind == "functional_pipeline" ||
      target.kind == "functional_node") {
    for (const auto& pipeline : program.functional_pipelines) {
      if (pipeline.semantic_identity == target.semantic_identity) {
        selected_pipeline = &pipeline;
        break;
      }
      if (target.kind == "functional_node" &&
          std::any_of(pipeline.nodes.begin(), pipeline.nodes.end(),
                      [&](const FunctionalNode& node) {
                        return node.semantic_identity ==
                            target.semantic_identity;
                      })) {
        selected_pipeline = &pipeline;
        break;
      }
    }
  }
  size_t materialized = 0;
  size_t eliminated = 0;
  size_t traversals = 0;
  if (selected_pipeline) {
    for (const auto& node : selected_pipeline->nodes) {
      if (node.materialization ==
              FunctionalMaterializationKind::Materialize &&
          !node.materialization_eliminated)
        ++materialized;
      if (node.semantic_work_eliminated || node.dead_stage_eliminated)
        ++eliminated;
    }
    traversals = selected_pipeline->virtualized_into_pipeline_id ? 0 : 1;
  }
  string domain_name;
  if (target.kind == "domain") domain_name = target.name;
  else if (target.kind == "handler") {
    size_t dot = target.name.find('.');
    if (dot != string::npos) domain_name = target.name.substr(0, dot);
  }
  out << "{\"allocations\": {\"materialized_functional_intermediates\": "
      << materialized << "}, \"message_copy_sizes\": [";
  bool first_copy = true;
  for (const auto& warning : warnings) {
    if (warning.code != "MESSAGE_PAYLOAD_COPY_LARGE") continue;
    const string materialization_marker = "materializes ";
    size_t marker = warning.message.find(materialization_marker);
    size_t end = marker == string::npos
        ? string::npos
        : warning.message.find(" bytes", marker + materialization_marker.size());
    if (marker == string::npos || end == string::npos) continue;
    if (!first_copy) out << ", ";
    first_copy = false;
    out << "{\"line\": " << warning.line << ", \"bytes\": "
        << warning.message.substr(marker + materialization_marker.size(),
                                  end - marker - materialization_marker.size())
        << ", \"known_statically\": true}";
  }
  out << "], \"source_traversals\": " << traversals
      << ", \"specialization_count\": " << specialization_count
      << ", \"functional\": ";
  if (!selected_pipeline) {
    out << "null";
  } else {
    out << "{\"fused\": " << (selected_pipeline->fused ? "true" : "false")
        << ", \"semantic_work_eliminated\": " << eliminated
        << ", \"materialization_decision\": ";
    write_debug_json_string(
        out, selected_pipeline->binding_materialization_reason.empty()
            ? selected_pipeline->decision
            : selected_pipeline->binding_materialization_reason);
    out << ", \"traversal_group_id\": "
        << selected_pipeline->traversal_group_id << "}";
  }
  out << ", \"domain_backend\": ";
  if (domain_name.empty()) {
    out << "null";
  } else {
    out << "{\"domain\": ";
    write_debug_json_string(out, domain_name);
    out << ", \"lowering\": \"Handler2PL\", \"lock_type\": \"RwLock per synchronization class\", \"locks_held_across_message\": true}";
  }
  out << ", \"predictive_estimates\": null}";
}

#include "synchronization_diagnostics.inc"

static void write_synchronization_plan_json(std::ostream& out, const SynchronizationPlan& plan) {
  auto leaves = [&](const StateLeafSet& values) {
    write_agent_string_array(out, vector<string>(values.begin(), values.end()));
  };
  out << "{\"graph_identity\":";
  write_debug_json_string(out, plan.graph_identity);
  out << ",\"physical_lowering\":\"handler_2pl\",\"domains\":[";
  for (size_t d = 0; d < plan.domains.size(); ++d) {
    if (d) out << ',';
    const auto& domain = plan.domains[d];
    out << "{\"concrete_instance_id\":"; write_debug_json_string(out, domain.concrete_instance_id);
    out << ",\"specialization_id\":"; write_debug_json_string(out, domain.specialization_id);
    out << ",\"domain\":"; write_debug_json_string(out, domain.domain);
    out << ",\"source_file\":"; write_debug_json_string(out, domain.source_file);
    out << ",\"line\":" << domain.line << ",\"domain_rank\":" << domain.domain_rank;
    out << ",\"state_leaves\":"; leaves(domain.state_leaves);
    out << ",\"protected_mutable_leaves\":"; leaves(domain.protected_mutable_leaves);
    out << ",\"leaf_to_class\":{";
    bool first = true;
    for (const auto& entry : domain.leaf_to_class) {
      if (!first) out << ',';
      first = false;
      write_debug_json_string(out, entry.first); out << ':';
      write_debug_json_string(out, domain.classes.at(entry.second).class_id);
    }
    out << "},\"sync_classes\":[";
    for (size_t c = 0; c < domain.classes.size(); ++c) {
      if (c) out << ',';
      const auto& cls = domain.classes[c];
      out << "{\"class_id\":"; write_debug_json_string(out, cls.class_id);
      out << ",\"class_rank\":" << cls.class_rank << ",\"member_leaves\":";
      leaves(cls.member_leaves);
      out << ",\"mode_signature\":{";
      for (size_t h = 0; h < domain.handlers.size(); ++h) {
        if (h) out << ',';
        write_debug_json_string(out, domain.handlers[h].handler_identity); out << ':';
        write_debug_json_string(out, synchronization_mode_name(cls.mode_signature[h]));
      }
      out << "}}";
    }
    out << "],\"handlers\":[";
    for (size_t h = 0; h < domain.handlers.size(); ++h) {
      if (h) out << ',';
      const auto& handler = domain.handlers[h];
      out << "{\"handler_identity\":"; write_debug_json_string(out, handler.handler_identity);
      out << ",\"name\":"; write_debug_json_string(out, handler.name);
      out << ",\"read_set\":"; leaves(handler.effects.reads);
      out << ",\"write_set\":"; leaves(handler.effects.writes);
      out << ",\"consume_set\":"; leaves(handler.effects.consumes);
      out << ",\"exclusive_set\":"; leaves(handler.exclusive_set);
      out << ",\"protected_read_set\":"; leaves(handler.protected_read_set);
      out << ",\"lock_set\":"; leaves(handler.lock_set);
      out << ",\"normalized_effects\":{";
      bool first_effect = true;
      for (const auto& leaf : domain.state_leaves) {
        auto effect = handler.effects.normalized(leaf);
        if (!effect) continue;
        if (!first_effect) out << ',';
        first_effect = false;
        write_debug_json_string(out, leaf); out << ':';
        write_debug_json_string(out, ownership_effect_name(*effect));
      }
      out << "},\"class_set\":[";
      first = true;
      for (const auto& entry : handler.class_modes) {
        if (!first) out << ',';
        first = false;
        write_debug_json_string(out, domain.classes.at(entry.first).class_id);
      }
      out << "],\"class_modes\":{";
      first = true;
      for (const auto& entry : handler.class_modes) {
        if (!first) out << ',';
        first = false;
        write_debug_json_string(out, domain.classes.at(entry.first).class_id); out << ':';
        write_debug_json_string(out, synchronization_mode_name(entry.second));
      }
      out << "},\"path_placement\":{";
      write_debug_json_string(out, "kind"); out << ':';
      write_debug_json_string(out, handler.path_placement.enabled
          ? "leading_conditional_continuation_split" : "entry");
      out << ",\"branch_line\":" << handler.path_placement.branch_line;
      auto write_modes = [&](const std::map<size_t, SynchronizationMode>& modes) {
        out << '{'; bool first_mode = true;
        for (const auto& entry : modes) {
          if (!first_mode) out << ',';
          first_mode = false;
          write_debug_json_string(out, domain.classes.at(entry.first).class_id); out << ':';
          write_debug_json_string(out, synchronization_mode_name(entry.second));
        }
        out << '}';
      };
      out << ",\"entry\":"; write_modes(handler.path_placement.entry_modes);
      out << ",\"then\":"; write_modes(handler.path_placement.then_modes);
      out << ",\"else\":"; write_modes(handler.path_placement.else_modes);
      out << ",\"cancel_then\":[";
      first = true; for (const auto rank : handler.path_placement.cancel_then) {
        if (!first) out << ',';
        first = false;
        write_debug_json_string(out, domain.classes.at(rank).class_id);
      }
      out << "],\"cancel_else\":[";
      first = true; for (const auto rank : handler.path_placement.cancel_else) {
        if (!first) out << ',';
        first = false;
        write_debug_json_string(out, domain.classes.at(rank).class_id);
      }
      out << "],\"reason\":"; write_debug_json_string(out, handler.path_placement.reason);
      out << "},\"class_set_size\":" << handler.class_modes.size()
          << ",\"shared_acquisitions\":" << shared_acquisitions(handler)
          << ",\"exclusive_acquisitions\":" << handler.class_modes.size() - shared_acquisitions(handler)
          << ",\"read_only\":" << (handler.effects.writes.empty() && handler.effects.consumes.empty() ? "true" : "false")
          << ",\"self_conflict\":" << (handler.self_conflict() ? "true" : "false") << '}';
    }
    out << "],\"conflicts\":[";
    first = true;
    size_t conflicting = 0, disjoint = 0, shared_pairs = 0;
    for (size_t a = 0; a < domain.handlers.size(); ++a) {
      for (size_t b = a; b < domain.handlers.size(); ++b) {
        const auto& left = domain.handlers[a];
        const auto& right = domain.handlers[b];
        auto witnesses = synchronization_conflicts(left, right);
        if (a != b) {
          if (witnesses.empty()) ++disjoint; else ++conflicting;
          bool shared = false;
          for (const auto& entry : left.class_modes) {
            auto other = right.class_modes.find(entry.first);
            shared |= other != right.class_modes.end() && entry.second == SynchronizationMode::Shared &&
                other->second == SynchronizationMode::Shared;
          }
          if (shared && witnesses.empty()) ++shared_pairs;
        }
        if (witnesses.empty()) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"left\":"; write_debug_json_string(out, left.handler_identity);
        out << ",\"right\":"; write_debug_json_string(out, right.handler_identity);
        out << ",\"witnesses\":[";
        for (size_t i = 0; i < witnesses.size(); ++i) {
          if (i) out << ',';
          const auto& cls = domain.classes.at(witnesses[i]);
          out << "{\"class_id\":"; write_debug_json_string(out, cls.class_id);
          out << ",\"member_leaves\":"; leaves(cls.member_leaves); out << '}';
        }
        out << "]}";
      }
    }
    std::set<string> roots;
    for (const auto& leaf : domain.protected_mutable_leaves) roots.insert(leaf.substr(0, leaf.find('.')));
    out << "],\"metrics\":{\"class_count\":" << domain.classes.size()
        << ",\"root_count\":" << roots.size()
        << ",\"protected_leaf_count\":" << domain.protected_mutable_leaves.size()
        << ",\"handler_count\":" << domain.handlers.size()
        << ",\"conflicting_handler_pairs\":" << conflicting
        << ",\"disjoint_handler_pairs\":" << disjoint
        << ",\"shared_reader_pairs\":" << shared_pairs;
    const auto metrics = synchronization_metrics(domain);
    out << ",\"state_leaf_count\":" << domain.state_leaves.size()
        << ",\"read_only_handler_count\":" << metrics.readonly_handlers
        << ",\"self_conflicting_handler_count\":" << metrics.self_conflicting
        << ",\"total_handler_pairs\":" << metrics.pairs
        << ",\"non_conflicting_handler_pairs\":" << disjoint
        << ",\"non_conflicting_percentage\":";
    if (metrics.pairs) out << (100.0 * disjoint / metrics.pairs); else out << "null";
    out << ",\"class_compression_ratio\":";
    if (domain.protected_mutable_leaves.empty()) out << "null";
    else out << (double(domain.classes.size()) / domain.protected_mutable_leaves.size());
    out << '}';
    write_partition_diagnostics(out, domain);
    out << '}';
  }
  out << "]}";
}

static string synchronization_plan_dump(const SynchronizationPlan& plan) {
  std::ostringstream out;
  auto leaves = [&](const StateLeafSet& values) {
    bool first = true;
    for (const auto& leaf : values) { if (!first) out << ", "; first = false; out << leaf; }
    out << '\n';
  };
  out << "SynchronizationPlan " << plan.graph_identity << '\n';
  for (const auto& domain : plan.domains) {
    out << "Domain " << domain.domain << "\ninstance: " << domain.concrete_instance_id
        << "\nspecialization: " << domain.specialization_id
        << "\ndomain_rank: " << domain.domain_rank << "\nX*: "; leaves(domain.protected_mutable_leaves);
    const auto metrics = synchronization_metrics(domain);
    out << "Metrics: state_leaves=" << domain.state_leaves.size()
        << " protected_leaves=" << domain.protected_mutable_leaves.size()
        << " classes=" << domain.classes.size() << " compression=";
    if (domain.protected_mutable_leaves.empty()) out << "n/a";
    else out << double(domain.classes.size()) / domain.protected_mutable_leaves.size();
    out << "\n  handler_pairs=" << metrics.pairs << " conflicting=" << metrics.conflicting
        << " non_conflicting=" << metrics.pairs - metrics.conflicting
        << " shared_reader_pairs=" << metrics.shared_pairs
        << " read_only_handlers=" << metrics.readonly_handlers
        << " self_conflicting_handlers=" << metrics.self_conflicting << '\n';
    for (const auto& cls : domain.classes) {
      out << "Class " << cls.class_id << "\n  rank: " << cls.class_rank << "\n  leaves: "; leaves(cls.member_leaves);
      out << "  Members share the complete per-handler mode signature.\n";
      for (size_t h = 0; h < domain.handlers.size(); ++h) {
        out << "  " << domain.handlers[h].name << ": " << synchronization_mode_name(cls.mode_signature[h]);
        for (const auto& leaf : cls.member_leaves) {
          auto effect = domain.handlers[h].effects.normalized(leaf);
          if (effect) out << " " << leaf << "=" << ownership_effect_name(*effect);
        }
        out << '\n';
      }
    }
    for (const auto& h : domain.handlers) {
      out << "Handler " << h.name << "\n  R: "; leaves(h.effects.reads);
      out << "  W: "; leaves(h.effects.writes);
      out << "  C: "; leaves(h.effects.consumes);
      out << "  X: "; leaves(h.exclusive_set);
      out << "  ProtectedRead: "; leaves(h.protected_read_set);
      out << "  LockSet (leaves): "; leaves(h.lock_set);
      out << "  ClassSet (rank order):";
      for (const auto& entry : h.class_modes)
        out << ' ' << domain.classes.at(entry.first).class_id << '=' << synchronization_mode_name(entry.second);
      out << "\n  Static acquisition placement:\n";
      const auto& entry_modes = h.path_placement.enabled ? h.path_placement.entry_modes : h.class_modes;
      for (const auto& entry : entry_modes)
        out << "    (domain_rank=" << domain.domain_rank << ", class_rank="
            << domain.classes.at(entry.first).class_rank << ") " << synchronization_mode_name(entry.second) << '\n';
      if (h.path_placement.enabled) {
        out << "  branch line " << h.path_placement.branch_line << " then acquisitions:";
        for (const auto& entry : h.path_placement.then_modes) out << ' ' << domain.classes.at(entry.first).class_id;
        out << "\n  branch line " << h.path_placement.branch_line << " else acquisitions:";
        for (const auto& entry : h.path_placement.else_modes) out << ' ' << domain.classes.at(entry.first).class_id;
        out << "\n  untouched cancellations are statically emitted before later branch acquisition.\n";
      }
      out << "  Hold through complete handler and nested messages; release at completion.\n";
      out << "  acquisitions: " << h.class_modes.size() << " (SHARED=" << shared_acquisitions(h)
          << " EXCLUSIVE=" << h.class_modes.size() - shared_acquisitions(h) << ")\n";
      out << "  self-conflict: " << (h.self_conflict() ? "yes" : "no") << '\n';
    }
    for (size_t a = 0; a < domain.handlers.size(); ++a)
      for (size_t b = a + 1; b < domain.handlers.size(); ++b)
        for (size_t rank : synchronization_conflicts(domain.handlers[a], domain.handlers[b])) {
          out << "Conflict " << domain.handlers[a].name << " / " << domain.handlers[b].name
              << " via " << domain.classes[rank].class_id << ": ";
          leaves(domain.classes[rank].member_leaves);
        }
    for (size_t a = 0; a < domain.classes.size(); ++a)
      for (size_t b = a + 1; b < domain.classes.size(); ++b)
        for (size_t h = 0; h < domain.handlers.size(); ++h) {
          const auto& left = domain.classes[a]; const auto& right = domain.classes[b];
          if (left.mode_signature[h] == right.mode_signature[h]) continue;
          out << "Split " << left.class_id << " / " << right.class_id << " because "
              << domain.handlers[h].name << " has " << synchronization_mode_name(left.mode_signature[h])
              << " / " << synchronization_mode_name(right.mode_signature[h]) << '\n';
          break;
        }
    out << '\n';
  }
  return out.str();
}

static bool write_semantic_query_json(
    std::ostream& out, const string& command, const string& selector,
    const string& source_file, const Program& program,
    const OptimizationPlan& plan, const vector<Warning>& warnings) {
  vector<SemanticTargetFact> targets = semantic_target_facts(program, plan);
  const SemanticTargetFact* target = resolve_semantic_target(
      targets, selector, source_file);
  if (!target) {
    write_structured_error(
        out, command, "QUERY_TARGET_NOT_FOUND",
        "no exact Moss semantic target matches '" + selector + "'",
        source_file);
    return false;
  }
  write_agent_envelope_begin(out, command, true);
  out << "  \"result\": {\"target\": ";
  write_semantic_target_json(out, *target, source_file);
  if (command == "inspect" || command == "type") {
    out << ", \"resolved_type\": ";
    if (target->type.empty()) out << "null";
    else write_debug_json_string(out, target->type);
  }
  if (command == "inspect" || command == "effects") {
    out << ", \"ownership\": ";
    write_parameters_json(out, *target);
    out << ", \"observable_effects\": ";
    if (target->has_observable_effects)
      write_observable_effects_json(out, target->observable_effects);
    else out << "null";
    out << ", \"enclosing_callable_effects\": ";
    if (target->has_enclosing_callable_effects)
      write_observable_effects_json(
          out, target->enclosing_callable_effects);
    else out << "null";
  }
  if (command == "ownership") {
    out << ", \"parameters_and_values\": ";
    write_parameters_json(out, *target);
    out << ", \"reason\": ";
    write_debug_json_string(
        out, "effects are inferred from the checked body and its statically resolved callees");
  }
  if (command == "inspect" || command == "calls") {
    out << ", \"direct_calls\": ";
    write_calls_result(out, program, *target);
    out << ", \"callers\": ";
    write_callers_result(out, program, *target);
  }

  if (command == "inspect" || command == "why") {
    out << ", \"explanations\": ";
    write_agent_string_array(out, target->explanations);
  }
  if (command == "inspect" || command == "cost") {
    out << ", \"cost_facts\": ";
    write_cost_result(out, program, plan, warnings, *target);
  }
  if (command == "inspect" || command == "effects" || command == "why") {
    out << ", \"synchronization_plan\":";
    write_synchronization_plan_json(out, program.synchronization_plan);
    out << ", \"synchronization_dump\":";
    write_debug_json_string(out, synchronization_plan_dump(program.synchronization_plan));
  }
  if (command == "inspect") {
    out << ", \"concrete_domain_graph\": {\"graph_identity\":";
    write_debug_json_string(out, program.concrete_domain_graph.identity);
    out << ",\"closed\":"
        << (program.concrete_domain_graph.closed ? "true" : "false")
        << ",\"instances\": [";
    for (size_t index = 0; index < program.concrete_domain_graph.instances.size(); ++index) {
      if (index) out << ",";
      const auto& instance = program.concrete_domain_graph.instances[index];
      out << "{\"identity\":"; write_debug_json_string(out, instance.identity);
      out << ",\"binding\":"; write_debug_json_string(out, instance.binding);
      out << ",\"domain\":"; write_debug_json_string(out, instance.domain);
      out << ",\"concrete_instance_id\":"; write_debug_json_string(out, instance.identity);
      out << ",\"source_domain_id\":";
      write_debug_json_string(out, "domain:" + program.domains.at(instance.source_domain_index).name);
      out << ",\"specialization_id\":"; write_debug_json_string(out, instance.specialization);
      out << ",\"source_file\":"; write_debug_json_string(out, instance.source_file);
      out << ",\"line\":" << instance.line << ",\"domain_rank\":"
          << instance.domain_rank << "}";
    }
    out << "],\"edges\":[";
    for (size_t index = 0; index < program.concrete_domain_graph.edges.size(); ++index) {
      if (index) out << ",";
      const auto& edge = program.concrete_domain_graph.edges[index];
      out << "{\"source_instance\":"; write_debug_json_string(out, edge.source_instance);
      out << ",\"route\":"; write_debug_json_string(out, edge.route);
      out << ",\"target_instance\":"; write_debug_json_string(out, edge.target_instance);
      out << ",\"source_file\":"; write_debug_json_string(out, edge.source_file);
      out << ",\"line\":" << edge.line << "}";
    }
    out << "]}";
  }
  out << "}\n}\n";
  return true;
}

// Phase 7 project commands deliberately reuse the ordinary compiler pipeline.
// Tests and benchmarks change only the generated entry harness; parsing,
// typing, effects, ownership, functional optimization, backend planning, and
// debug provenance remain the same compiler-owned facts used by applications.
struct ProjectError : std::runtime_error {
  string code;
  string source_file;
  int line = 0;

  ProjectError(string error_code, string message, string source = {},
               int source_line = 0)
      : std::runtime_error(std::move(message)), code(std::move(error_code)),
        source_file(std::move(source)), line(source_line) {}
};

struct ProjectManifest {
  std::filesystem::path root;
  std::filesystem::path manifest_file;
  string name;
  string version;
  std::filesystem::path source = "src";
};

static std::optional<std::filesystem::path> find_project_root(
    std::filesystem::path start) {
  std::error_code error;
  start = std::filesystem::absolute(start, error).lexically_normal();
  if (error) return std::nullopt;
  if (!std::filesystem::is_directory(start, error)) start = start.parent_path();
  while (!start.empty()) {
    if (std::filesystem::is_regular_file(start / "Moss.toml", error) ||
        std::filesystem::is_regular_file(start / "moss.toml", error))
      return start;
    std::filesystem::path parent = start.parent_path();
    if (parent == start) break;
    start = std::move(parent);
  }
  return std::nullopt;
}

static string manifest_string_value(const string& text, int line,
                                    const std::filesystem::path& file) {
  string value = trim(text);
  if (value.size() < 2 || value.front() != '"' || value.back() != '"')
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR",
        "manifest values must be quoted strings", file.string(), line);
  value = value.substr(1, value.size() - 2);
  if (value.find('"') != string::npos)
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR",
        "manifest string contains an unsupported quote", file.string(), line);
  return value;
}

static ProjectManifest load_project_manifest(
    const std::filesystem::path& start) {
  auto root = find_project_root(start);
  if (!root)
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR",
        "no Moss.toml was found in this directory or any parent");
  ProjectManifest manifest;
  manifest.root = *root;
  manifest.manifest_file = std::filesystem::is_regular_file(manifest.root / "Moss.toml")
      ? manifest.root / "Moss.toml" : manifest.root / "moss.toml";
  std::ifstream input(manifest.manifest_file);
  if (!input)
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR", "cannot read Moss.toml",
        manifest.manifest_file.string());
  string section;
  string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    bool in_string = false;
    size_t comment = string::npos;
    for (size_t index = 0; index < line.size(); ++index) {
      if (line[index] == '"') in_string = !in_string;
      else if (line[index] == '#' && !in_string) {
        comment = index;
        break;
      }
    }
    if (comment != string::npos) line.erase(comment);
    line = trim(std::move(line));
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      if (section != "project" && section != "package" && section != "build" &&
          section != "dependencies")
        throw ProjectError(
            "PROJECT_MANIFEST_ERROR",
            "unknown manifest section '[" + section + "]'",
            manifest.manifest_file.string(), line_number);
      continue;
    }
    // Dependency acquisition is owned by Margo.  The compiler accepts this
    // package metadata so its legacy project entry points remain compatible,
    // but never interprets package sources or dependencies itself.
    if (section == "dependencies") continue;
    size_t equals = line.find('=');
    if (equals == string::npos || section.empty())
      throw ProjectError(
          "PROJECT_MANIFEST_ERROR",
          "expected a key = \"value\" inside a manifest section",
          manifest.manifest_file.string(), line_number);
    string key = trim(line.substr(0, equals));
    string value = manifest_string_value(
        line.substr(equals + 1), line_number, manifest.manifest_file);
    if ((section == "project" || section == "package") && key == "name") manifest.name = value;
    else if ((section == "project" || section == "package") && key == "version")
      manifest.version = value;
    else if (section == "build" && key == "source")
      manifest.source = value;
    else
      throw ProjectError(
          "PROJECT_MANIFEST_ERROR",
          "unknown manifest key '" + section + "." + key + "'",
          manifest.manifest_file.string(), line_number);
  }
  if (manifest.name.empty())
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR", "[package].name (or legacy [project].name) is required",
        manifest.manifest_file.string());
  if (manifest.version.empty())
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR", "[package].version (or legacy [project].version) is required",
        manifest.manifest_file.string());
  if (manifest.source.empty() || manifest.source.is_absolute())
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR",
        "[build].source must be a non-empty project-relative path",
        manifest.manifest_file.string());
  if (std::find(manifest.source.begin(), manifest.source.end(), "..") !=
      manifest.source.end())
    throw ProjectError(
        "PROJECT_MANIFEST_ERROR",
        "[build].source may not escape the project root",
        manifest.manifest_file.string());
  return manifest;
}

static vector<std::filesystem::path> moss_files_under(
    const std::filesystem::path& root, const string& error_code);

static std::filesystem::path project_source_file(
    const ProjectManifest& manifest) {
  std::filesystem::path source = manifest.root / manifest.source;
  std::error_code error;
  if (std::filesystem::is_directory(source, error)) {
    std::filesystem::path conventional = source / "main.moss";
    if (std::filesystem::is_regular_file(conventional, error))
      source = conventional;
    else {
      auto files = moss_files_under(source, "PROJECT_SOURCE_NOT_FOUND");
      if (!files.empty()) return files.front();
      source = conventional;
    }
  }
  if (!std::filesystem::is_regular_file(source, error))
    throw ProjectError(
        "PROJECT_SOURCE_NOT_FOUND",
        "project source was not found at '" + source.string() + "'",
        source.string());
  return source.lexically_normal();
}

static vector<std::filesystem::path> moss_files_under(
    const std::filesystem::path& root, const string& error_code) {
  vector<std::filesystem::path> files;
  std::error_code error;
  if (std::filesystem::is_regular_file(root, error)) {
    if (root.extension() == ".moss") files.push_back(root.lexically_normal());
    return files;
  }
  if (error || !std::filesystem::is_directory(root, error)) {
    if (error == std::errc::no_such_file_or_directory) return files;
    if (error)
      throw ProjectError(error_code,
                         "cannot scan project source directory '" +
                             root.string() + "': " + error.message(),
                         root.string());
    return files;
  }
  for (std::filesystem::recursive_directory_iterator iterator(root, error),
       end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_regular_file(error) &&
        iterator->path().extension() == ".moss")
      files.push_back(iterator->path().lexically_normal());
  }
  if (error)
    throw ProjectError(error_code,
                       "cannot scan project source directory '" +
                           root.string() + "': " + error.message(),
                       root.string());
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

static vector<std::filesystem::path> project_source_files(
    const ProjectManifest& manifest) {
  vector<std::filesystem::path> files = moss_files_under(
      manifest.root / manifest.source, "PROJECT_SOURCE_NOT_FOUND");
  if (files.empty())
    throw ProjectError(
        "PROJECT_SOURCE_NOT_FOUND",
        "project source contains no .moss files at '" +
            (manifest.root / manifest.source).string() + "'");
  return files;
}

struct ProcessResult {
  int exit_code = -1;
  string output;
};

static ProcessResult run_process(const vector<string>& arguments) {
  if (arguments.empty())
    throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                       "cannot launch an empty command");
  int descriptors[2];
  if (::pipe(descriptors) != 0)
    throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                       "cannot create child-process pipe: " +
                           string(std::strerror(errno)));
  pid_t child = ::fork();
  if (child < 0) {
    ::close(descriptors[0]);
    ::close(descriptors[1]);
    throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                       "cannot launch child process: " +
                           string(std::strerror(errno)));
  }
  if (child == 0) {
    ::close(descriptors[0]);
    if (::dup2(descriptors[1], STDOUT_FILENO) < 0 ||
        ::dup2(descriptors[1], STDERR_FILENO) < 0)
      _exit(126);
    ::close(descriptors[1]);
    vector<char*> command;
    command.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
      command.push_back(const_cast<char*>(argument.c_str()));
    command.push_back(nullptr);
    ::execvp(command.front(), command.data());
    _exit(127);
  }
  ::close(descriptors[1]);
  ProcessResult result;
  char buffer[4096];
  while (true) {
    ssize_t count = ::read(descriptors[0], buffer, sizeof(buffer));
    if (count > 0) result.output.append(buffer, static_cast<size_t>(count));
    else if (count == 0) break;
    else if (errno != EINTR) {
      ::close(descriptors[0]);
      (void)::waitpid(child, nullptr, 0);
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "cannot read child-process output: " +
                             string(std::strerror(errno)));
    }
  }
  ::close(descriptors[0]);
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR)
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "cannot wait for child process: " +
                             string(std::strerror(errno)));
  }
  if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
  return result;
}

struct BackendToolchainIdentity {
  string rustc_executable;
  string version_verbose;
  string profile;
  vector<string> compile_flags;
  string fingerprint;
};

static std::optional<std::filesystem::path> resolve_executable(
    const string& configured) {
  vector<std::filesystem::path> candidates;
  if (configured.find('/') != string::npos) {
    candidates.emplace_back(configured);
  } else {
    const char* path_environment = std::getenv("PATH");
    string search = path_environment ? path_environment : "/usr/bin:/bin";
    size_t begin = 0;
    while (begin <= search.size()) {
      size_t end = search.find(':', begin);
      string directory = search.substr(
          begin, end == string::npos ? string::npos : end - begin);
      candidates.push_back(
          (directory.empty() ? std::filesystem::path(".")
                             : std::filesystem::path(directory)) /
          configured);
      if (end == string::npos) break;
      begin = end + 1;
    }
  }
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error ||
        ::access(candidate.c_str(), X_OK) != 0)
      continue;
    auto absolute = std::filesystem::absolute(candidate, error);
    if (error) continue;
    // Preserve the resolved executable's symlink name.  rustup dispatches by
    // argv[0], so canonicalizing `rustc` to the `rustup` binary changes the
    // program being invoked.
    return absolute.lexically_normal();
  }
  return std::nullopt;
}

static string backend_flags_text(const vector<string>& flags) {
  std::ostringstream out;
  for (size_t index = 0; index < flags.size(); ++index) {
    if (index) out << "\n";
    out << flags[index];
  }
  return out.str();
}

static string backend_identity_payload(
    const BackendToolchainIdentity& identity) {
  std::ostringstream out;
  out << "moss-codegen\nstatic-typed-domains-1\nresolved-rustc\n" << identity.rustc_executable
      << "\nversion-verbose-bytes\n" << identity.version_verbose.size()
      << "\n" << identity.version_verbose
      << "\nprofile\n" << identity.profile
      << "\ncompile-flags\n" << backend_flags_text(identity.compile_flags)
      << "\n";
  return out.str();
}

static BackendToolchainIdentity inspect_backend_toolchain(
    bool optimized, bool debug_build, ProgramGenerationMode mode) {
  const char* configured_environment = std::getenv("RUSTC");
  string configured = configured_environment && *configured_environment
      ? configured_environment : "rustc";
  auto executable = resolve_executable(configured);
  if (!executable)
    throw ProjectError(
        "BUILD_TOOL_NOT_FOUND",
        "rustc was not found; install Rust or set RUSTC to an executable");

  ProcessResult version = run_process(
      {executable->string(), "--version", "--verbose"});
  if (version.exit_code != 0 || trim(version.output).empty())
    throw ProjectError(
        "BUILD_BACKEND_ERROR",
        "could not identify the configured Rust compiler with "
        "'rustc --version --verbose'");

  BackendToolchainIdentity identity;
  identity.rustc_executable = executable->string();
  identity.version_verbose = trim(version.output);
  if (optimized) identity.profile = "release";
  else if (mode == ProgramGenerationMode::Tests)
    identity.profile = "test-debug";
  else
    identity.profile = debug_build ? "debug" : "reference";
  identity.compile_flags = {"--edition=2021", "-D", "warnings"};
  if (optimized) {
    identity.compile_flags.insert(
        identity.compile_flags.end(), {"-O", "-C", "debuginfo=1"});
  } else {
    identity.compile_flags.insert(
        identity.compile_flags.end(), {"-g", "-C", "opt-level=0"});
  }
  identity.fingerprint = stable_hash(backend_identity_payload(identity));
  return identity;
}

static bool same_backend_toolchain(
    const BackendToolchainIdentity& left,
    const BackendToolchainIdentity& right) {
  return left.fingerprint == right.fingerprint &&
      left.rustc_executable == right.rustc_executable &&
      left.version_verbose == right.version_verbose &&
      left.profile == right.profile &&
      left.compile_flags == right.compile_flags;
}

static string backend_cache_metadata(
    const BackendToolchainIdentity& identity,
    const string& source_fingerprint) {
  return "moss-native-cache-v2\nsource-fingerprint\n" +
      source_fingerprint + "\nfingerprint\n" + identity.fingerprint +
      "\n" + backend_identity_payload(identity);
}

static void write_backend_toolchain_json(
    std::ostream& out, const BackendToolchainIdentity& identity) {
  out << "{\"fingerprint\": ";
  write_debug_json_string(out, identity.fingerprint);
  out << ", \"resolved_rustc\": ";
  write_debug_json_string(out, identity.rustc_executable);
  out << ", \"version_verbose\": ";
  write_debug_json_string(out, identity.version_verbose);
  out << ", \"profile\": ";
  write_debug_json_string(out, identity.profile);
  out << ", \"compile_flags\": ";
  write_agent_string_array(out, identity.compile_flags);
  out << "}";
}

static string project_relative_path(const ProjectManifest& manifest,
                                    const std::filesystem::path& source) {
  std::error_code error;
  auto relative = std::filesystem::relative(source, manifest.root, error);
  return error ? source.filename().generic_string() : relative.generic_string();
}

static void assign_project_declaration_identities(
    Program& program, const string& relative_source) {
  for (auto& test : program.tests)
    test.semantic_identity = "test:" + relative_source + ":" + test.name;
  for (auto& benchmark : program.benchmarks)
    benchmark.semantic_identity =
        "bench:" + relative_source + ":" + benchmark.name;
}

// Parser line numbers are intentionally physical-file line numbers.  Project
// linking annotates the already parsed nodes instead of concatenating source
// text, so diagnostics and debug/provenance consumers retain the origin file.
static void annotate_program_source(Program& program, const string& source_file) {
  auto annotate_body = [&](vector<Stmt>& body) {
    for (auto& statement : body) statement.source_file = source_file;
  };
  for (auto& function : program.functions) {
    function.source_file = source_file;
    annotate_body(function.body);
  }
  for (auto& object : program.objects) {
    object.source_file = source_file;
    for (auto& field : object.fields) field.source_file = source_file;
    for (auto& method : object.methods) {
      method.source_file = source_file;
      for (auto& field : method.body) field.source_file = source_file;
    }
  }
  for (auto& trait : program.traits) {
    trait.source_file = source_file;
    for (auto& method : trait.methods) method.source_file = source_file;
  }
  for (auto& domain : program.domains) {
    domain.source_file = source_file;
    for (auto& field : domain.state) field.source_file = source_file;
    for (auto& route : domain.routes) route.source_file = source_file;
    for (auto& handler : domain.handlers) {
      handler.source_file = source_file;
      annotate_body(handler.body);
    }
  }
  for (auto& test : program.tests) {
    test.source_file = source_file;
    annotate_body(test.body);
  }
  for (auto& benchmark : program.benchmarks) {
    benchmark.source_file = source_file;
    annotate_body(benchmark.body);
  }
  if (program.main) {
    program.main->source_file = source_file;
    annotate_body(program.main->body);
  }
}

static void merge_project_program(Program& destination, Program source,
                                  const string& source_file) {
  // Each physical file was parsed independently, but all declarations are
  // checked together below.  This is temporary global linkage until Moss has
  // explicit modules/imports; no synthetic source file or implicit namespace
  // is introduced.
  annotate_program_source(source, source_file);
  destination.imports.insert(destination.imports.end(), source.imports.begin(),
                             source.imports.end());
  destination.functions.insert(destination.functions.end(),
                               std::make_move_iterator(source.functions.begin()),
                               std::make_move_iterator(source.functions.end()));
  destination.traits.insert(destination.traits.end(),
                            std::make_move_iterator(source.traits.begin()),
                            std::make_move_iterator(source.traits.end()));
  destination.objects.insert(destination.objects.end(),
                             std::make_move_iterator(source.objects.begin()),
                             std::make_move_iterator(source.objects.end()));
  destination.domains.insert(destination.domains.end(),
                             std::make_move_iterator(source.domains.begin()),
                             std::make_move_iterator(source.domains.end()));
  destination.tests.insert(destination.tests.end(),
                           std::make_move_iterator(source.tests.begin()),
                           std::make_move_iterator(source.tests.end()));
  destination.benchmarks.insert(destination.benchmarks.end(),
                                std::make_move_iterator(source.benchmarks.begin()),
                                std::make_move_iterator(source.benchmarks.end()));
  if (source.main) {
    if (destination.main) {
      CompileError error(source.main->line, "duplicate proc main()");
      error.source_file = source_file;
      throw error;
    }
    destination.main = std::move(source.main);
  }
}

// Phase 6 persists a compact view of the authoritative semantic model.  The
// snapshot is not an alternate type checker: every record below is copied
// from the checked Program/optimization plan and is used only for comparing
// successive builds and selecting verification work.
struct SemanticSnapshotUnit {
  string durable_identity;
  string semantic_identity;
  string source_file;
  string context;
  string kind;
  string name;
  string implementation_hash;
  string interface_hash;
  vector<string> dependencies;
};

struct SemanticSnapshotRouteEdge {
  string source_domain;
  string target_domain;
  int line = 0;
  string source_instance;
  string target_instance;
};

struct SemanticSnapshot {
  string relative_source;
  string source_hash;
  vector<SemanticSnapshotUnit> units;
  vector<SemanticSnapshotRouteEdge> route_edges;
};

static string read_text_file(const std::filesystem::path& path,
                             const string& error_code) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw ProjectError(error_code,
                       "cannot read '" + path.string() + "'", path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

static std::filesystem::path semantic_snapshot_file(
    const ProjectManifest& manifest, const std::filesystem::path& source) {
  string relative = project_relative_path(manifest, source);
  return manifest.root / ".moss" / "semantic-cache-v1" /
      (stable_hash(relative) + ".snapshot");
}

static string source_set_content_hash(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources) {
  if (sources.size() == 1)
    return stable_hash(read_text_file(sources.front(),
                                      "PROJECT_SOURCE_NOT_FOUND"));
  std::ostringstream content;
  for (const auto& source : sources)
    content << project_relative_path(manifest, source) << "\n"
            << read_text_file(source, "PROJECT_SOURCE_NOT_FOUND") << "\n";
  return stable_hash(content.str());
}

static string source_set_key(const ProjectManifest& manifest,
                             const vector<std::filesystem::path>& sources) {
  if (sources.size() == 1)
    return project_relative_path(manifest, sources.front());
  std::ostringstream key;
  for (const auto& source : sources)
    key << project_relative_path(manifest, source) << "\n";
  return key.str();
}

static std::filesystem::path semantic_snapshot_file(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources) {
  return manifest.root / ".moss" / "semantic-cache-v1" /
      (stable_hash(source_set_key(manifest, sources)) + ".snapshot");
}

static SemanticSnapshot make_semantic_snapshot(
    const ProjectManifest& manifest, const std::filesystem::path& source,
    const Program& program, const OptimizationPlan& plan) {
  SemanticSnapshot snapshot;
  snapshot.relative_source = project_relative_path(manifest, source);
  snapshot.source_hash = stable_hash(
      read_text_file(source, "PROJECT_SOURCE_NOT_FOUND"));
  vector<SemanticTargetFact> facts = semantic_target_facts(program, plan);
  std::unordered_map<string,string> context_id;
  for (const auto& fact : facts) {
    bool owns_context = fact.kind == "function" || fact.kind == "method" ||
        fact.kind == "handler" || fact.kind == "test" ||
        fact.kind == "benchmark" || fact.kind == "main" ||
        fact.kind == "domain";
    if (owns_context && !context_id.count(fact.context))
      context_id[fact.context] = fact.durable_identity;
  }
  std::unordered_map<string,vector<string>> dependencies;
  for (const auto& edge : program.semantic_call_edges) {
    auto source_id = context_id.find(edge.source);
    auto target_id = context_id.find(edge.target);
    if (source_id != context_id.end() && target_id != context_id.end())
      dependencies[source_id->second].push_back(target_id->second);
  }
  for (const auto& pipeline : program.functional_pipelines) {
    auto source_id = context_id.find(pipeline.context);
    if (source_id == context_id.end()) continue;
    for (const auto& node : pipeline.nodes) {
      if (node.callable_identity.empty()) continue;
      auto target_id = context_id.find(node.callable_identity);
      if (target_id != context_id.end())
        dependencies[source_id->second].push_back(target_id->second);
    }
  }
  for (auto& entry : dependencies) {
    std::sort(entry.second.begin(), entry.second.end());
    entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                       entry.second.end());
  }
  for (const auto& fact : facts) {
    // Statement and node facts remain queryable but are not independent
    // compiler invalidation units. Their enclosing semantic unit owns them.
    bool unit = fact.kind == "function" || fact.kind == "specialization" ||
        fact.kind == "method" || fact.kind == "handler" ||
        fact.kind == "test" || fact.kind == "benchmark" ||
        fact.kind == "main" || fact.kind == "type" ||
        fact.kind == "trait" || fact.kind == "domain";
    if (!unit) continue;
    SemanticSnapshotUnit record;
    record.durable_identity = fact.durable_identity;
    record.semantic_identity = fact.semantic_identity;
    record.source_file = fact.source_file;
    record.context = fact.context;
    record.kind = fact.kind;
    record.name = fact.name;
    record.implementation_hash = fact.implementation_hash;
    record.interface_hash = fact.semantic_interface_hash;
    record.dependencies = dependencies[fact.durable_identity];
    snapshot.units.push_back(std::move(record));
  }
  std::sort(snapshot.units.begin(), snapshot.units.end(),
            [](const SemanticSnapshotUnit& left,
               const SemanticSnapshotUnit& right) {
              return left.durable_identity < right.durable_identity;
            });
  for (const auto& edge : program.concrete_domain_graph.edges)
    snapshot.route_edges.push_back({
        std::find_if(program.concrete_domain_graph.instances.begin(), program.concrete_domain_graph.instances.end(),
            [&](const auto& instance) { return instance.identity == edge.source_instance; })->domain,
        std::find_if(program.concrete_domain_graph.instances.begin(), program.concrete_domain_graph.instances.end(),
            [&](const auto& instance) { return instance.identity == edge.target_instance; })->domain,
        edge.line, edge.source_instance, edge.target_instance});
  std::sort(snapshot.route_edges.begin(), snapshot.route_edges.end(),
            [](const SemanticSnapshotRouteEdge& left,
               const SemanticSnapshotRouteEdge& right) {
              if (left.source_domain != right.source_domain)
                return left.source_domain < right.source_domain;
              if (left.target_domain != right.target_domain)
                return left.target_domain < right.target_domain;
              if (left.source_instance != right.source_instance)
                return left.source_instance < right.source_instance;
              if (left.target_instance != right.target_instance)
                return left.target_instance < right.target_instance;
              return left.line < right.line;
            });
  return snapshot;
}

static SemanticSnapshot make_semantic_snapshot(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources, const Program& program,
    const OptimizationPlan& plan) {
  SemanticSnapshot snapshot = make_semantic_snapshot(
      manifest, sources.front(), program, plan);
  if (sources.size() == 1) return snapshot;
  snapshot.relative_source = source_set_key(manifest, sources);
  snapshot.source_hash = source_set_content_hash(manifest, sources);
  return snapshot;
}

static void write_semantic_snapshot(const ProjectManifest& manifest,
                                    const std::filesystem::path& source,
                                    const SemanticSnapshot& snapshot) {
  std::filesystem::path file = semantic_snapshot_file(manifest, source);
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error)
    throw ProjectError("INCREMENTAL_CACHE_ERROR",
                       "cannot create semantic cache directory: " +
                           error.message(), file.string());
  std::ostringstream content;
  content << "moss-semantic-snapshot-v2\n"
          << std::quoted(snapshot.relative_source) << " "
          << std::quoted(snapshot.source_hash) << "\n";
  for (const auto& unit : snapshot.units) {
    content << "unit " << std::quoted(unit.durable_identity) << " "
            << std::quoted(unit.semantic_identity) << " "
            << std::quoted(unit.source_file) << " "
            << std::quoted(unit.context) << " " << std::quoted(unit.kind)
            << " " << std::quoted(unit.name) << " "
            << std::quoted(unit.implementation_hash) << " "
            << std::quoted(unit.interface_hash) << " "
            << unit.dependencies.size();
    for (const auto& dependency : unit.dependencies)
      content << " " << std::quoted(dependency);
    content << "\n";
  }
  for (const auto& edge : snapshot.route_edges)
    content << "route " << std::quoted(edge.source_domain) << " "
            << std::quoted(edge.target_domain) << " " << edge.line << " "
            << std::quoted(edge.source_instance) << " "
            << std::quoted(edge.target_instance) << "\n";
  string text = content.str();
  std::ifstream prior(file, std::ios::binary);
  if (prior) {
    std::ostringstream buffer;
    buffer << prior.rdbuf();
    if (buffer.str() == text) return;
  }
  std::ofstream output(file, std::ios::binary);
  if (!output)
    throw ProjectError("INCREMENTAL_CACHE_ERROR",
                       "cannot write semantic cache '" + file.string() + "'",
                       file.string());
  output << text;
}

static void write_semantic_snapshot(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources,
    const SemanticSnapshot& snapshot) {
  std::filesystem::path file = semantic_snapshot_file(manifest, sources);
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error)
    throw ProjectError("INCREMENTAL_CACHE_ERROR",
                       "cannot create semantic cache directory: " +
                           error.message(), file.string());
  std::ostringstream content;
  content << "moss-semantic-snapshot-v2\n"
          << std::quoted(snapshot.relative_source) << " "
          << std::quoted(snapshot.source_hash) << "\n";
  for (const auto& unit : snapshot.units) {
    content << "unit " << std::quoted(unit.durable_identity) << " "
            << std::quoted(unit.semantic_identity) << " "
            << std::quoted(unit.source_file) << " "
            << std::quoted(unit.context) << " " << std::quoted(unit.kind)
            << " " << std::quoted(unit.name) << " "
            << std::quoted(unit.implementation_hash) << " "
            << std::quoted(unit.interface_hash) << " "
            << unit.dependencies.size();
    for (const auto& dependency : unit.dependencies)
      content << " " << std::quoted(dependency);
    content << "\n";
  }
  for (const auto& edge : snapshot.route_edges)
    content << "route " << std::quoted(edge.source_domain) << " "
            << std::quoted(edge.target_domain) << " " << edge.line << " "
            << std::quoted(edge.source_instance) << " "
            << std::quoted(edge.target_instance) << "\n";
  string text = content.str();
  std::ifstream prior(file, std::ios::binary);
  if (prior) {
    std::ostringstream buffer;
    buffer << prior.rdbuf();
    if (buffer.str() == text) return;
  }
  std::ofstream output(file, std::ios::binary);
  if (!output)
    throw ProjectError("INCREMENTAL_CACHE_ERROR",
                       "cannot write semantic cache '" + file.string() +
                           "'", file.string());
  output << text;
}

static std::optional<SemanticSnapshot> read_semantic_snapshot(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources) {
  std::filesystem::path file = semantic_snapshot_file(manifest, sources);
  std::ifstream input(file, std::ios::binary);
  if (!input) return std::nullopt;
  string header;
  std::getline(input, header);
  if (header != "moss-semantic-snapshot-v2") return std::nullopt;
  SemanticSnapshot snapshot;
  if (!(input >> std::quoted(snapshot.relative_source) >>
        std::quoted(snapshot.source_hash))) return std::nullopt;
  string record;
  while (input >> record) {
    if (record == "unit") {
      SemanticSnapshotUnit unit;
      size_t dependency_count = 0;
      if (!(input >> std::quoted(unit.durable_identity) >>
            std::quoted(unit.semantic_identity) >> std::quoted(unit.source_file) >>
            std::quoted(unit.context) >>
            std::quoted(unit.kind) >> std::quoted(unit.name) >>
            std::quoted(unit.implementation_hash) >>
            std::quoted(unit.interface_hash) >> dependency_count))
        return std::nullopt;
      for (size_t index = 0; index < dependency_count; ++index) {
        string dependency;
        if (!(input >> std::quoted(dependency))) return std::nullopt;
        unit.dependencies.push_back(std::move(dependency));
      }
      snapshot.units.push_back(std::move(unit));
    } else if (record == "route") {
      SemanticSnapshotRouteEdge edge;
      if (!(input >> std::quoted(edge.source_domain) >>
            std::quoted(edge.target_domain) >> edge.line >>
            std::quoted(edge.source_instance) >>
            std::quoted(edge.target_instance))) return std::nullopt;
      snapshot.route_edges.push_back(std::move(edge));
    } else return std::nullopt;
  }
  return snapshot;
}

struct CompiledProjectUnit {
  Program program;
  OptimizationPlan plan;
  vector<Warning> warnings;
  string rust;
  // The source-free provider selected during semantic module resolution. This
  // is carried to native lowering so its rlib cannot be found independently
  // by a later filename scan.
  std::map<string,std::filesystem::path> external_module_interfaces;
};

// Explicit modules are lowered into the existing whole-program semantic
// pipeline.  The namespace pass below is deliberately source-level: it keeps
// Moss's checker, ownership analysis, concrete routing, and Rust generator as
// the single semantic authority while giving declarations stable qualified
// identities during project composition.
struct ParsedModuleUnit {
  string name;
  bool explicit_module = false;
  vector<std::pair<Program,string>> files;
  vector<ModuleImport> imports;
};

static string module_symbol(const string& module, const string& name) {
  return module + "__" + name;
}

static std::set<string> module_function_names(const ParsedModuleUnit& unit) {
  std::set<string> result;
  for (const auto& file : unit.files)
    for (const auto& function : file.first.functions) result.insert(function.name);
  return result;
}

static std::set<string> module_type_names(const ParsedModuleUnit& unit) {
  std::set<string> result;
  for (const auto& file : unit.files) {
    for (const auto& object : file.first.objects) result.insert(object.name);
    for (const auto& domain : file.first.domains) result.insert(domain.name);
    for (const auto& trait : file.first.traits) result.insert(trait.name);
  }
  return result;
}

static std::set<string> module_export_names(const ParsedModuleUnit& unit) {
  std::set<string> result;
  for (const auto& file : unit.files) {
    for (const auto& function : file.first.functions)
      if (function.exported) result.insert(function.name);
    for (const auto& object : file.first.objects)
      if (object.exported) result.insert(object.name);
    for (const auto& domain : file.first.domains)
      if (domain.exported) result.insert(domain.name);
    for (const auto& trait : file.first.traits)
      if (trait.exported) result.insert(trait.name);
  }
  return result;
}

static string rewrite_module_type(string type, const string& module,
                                  const std::map<string,ParsedModuleUnit>& modules,
                                  const std::map<string,std::set<string>>& public_exports) {
  type = trim(std::move(type));
  if (type.empty()) return type;
  // Nominal names nested in Vector[T], Map[K,V], and the other existing
  // container spellings are rewritten token-by-token. Primitive/container
  // words are unaffected because they are not module declarations.
  string result;
  for (size_t i = 0; i < type.size();) {
    if (!(std::isalpha(static_cast<unsigned char>(type[i])) || type[i] == '_')) {
      result.push_back(type[i++]);
      continue;
    }
    size_t end = i + 1;
    while (end < type.size() &&
           (std::isalnum(static_cast<unsigned char>(type[end])) || type[end] == '_')) ++end;
    string token = type.substr(i, end - i);
    if (starts_with(token, module + "__")) {
      result += token;
      i = end;
      continue;
    }
    size_t dot = end;
    while (dot < type.size() && std::isspace(static_cast<unsigned char>(type[dot]))) ++dot;
    if (dot < type.size() && type[dot] == '.') {
      size_t member = dot + 1;
      while (member < type.size() && std::isspace(static_cast<unsigned char>(type[member]))) ++member;
      size_t member_end = member;
      while (member_end < type.size() &&
             (std::isalnum(static_cast<unsigned char>(type[member_end])) || type[member_end] == '_')) ++member_end;
      string name = type.substr(member, member_end - member);
      auto imported = public_exports.find(token);
      if (imported != public_exports.end() && imported->second.count(name)) {
        result += module_symbol(token, name);
        i = member_end;
        continue;
      }
      if (imported != public_exports.end()) {
        // Keep the diagnostic in Moss's ordinary unknown-symbol path while
        // preventing a private declaration from becoming a qualified escape
        // hatch.
        result += "__moss_private__" + token + "__" + name;
        i = member_end;
        continue;
      }
    }
    auto local_types = module_type_names(modules.at(module));
    if (local_types.count(token)) result += module_symbol(module, token);
    else result += token;
    i = end;
  }
  return result;
}

static string rewrite_module_expression(
    string expression, const string& module,
    const std::map<string,ParsedModuleUnit>& modules,
    const std::map<string,std::set<string>>& public_exports) {
  std::set<string> functions = module_function_names(modules.at(module));
  std::set<string> types = module_type_names(modules.at(module));
  string result;
  bool in_string = false;
  for (size_t i = 0; i < expression.size();) {
    char c = expression[i];
    if (in_string) {
      result.push_back(c);
      if (c == '"' && (i == 0 || expression[i - 1] != '\\')) in_string = false;
      ++i;
      continue;
    }
    if (c == '"') { in_string = true; result.push_back(c); ++i; continue; }
    if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
      result.push_back(c); ++i; continue;
    }
    size_t end = i + 1;
    while (end < expression.size() &&
           (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_')) ++end;
    string token = expression.substr(i, end - i);
    if (starts_with(token, module + "__")) {
      result += token;
      i = end;
      continue;
    }
    // Test assertions are compiler-recognized builtins, never module-local
    // functions. Keeping their source spelling also lets explicit-module test
    // targets share the normal assertion checker and Rust lowering.
    if (token == "assert" || token == "assertEqual") {
      result += token;
      i = end;
      continue;
    }
    size_t cursor = end;
    while (cursor < expression.size() && std::isspace(static_cast<unsigned char>(expression[cursor]))) ++cursor;
    if (cursor < expression.size() && expression[cursor] == '.') {
      size_t member = cursor + 1;
      while (member < expression.size() && std::isspace(static_cast<unsigned char>(expression[member]))) ++member;
      size_t member_end = member;
      while (member_end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[member_end])) || expression[member_end] == '_')) ++member_end;
      string name = expression.substr(member, member_end - member);
      auto imported = public_exports.find(token);
      if (imported != public_exports.end() && imported->second.count(name)) {
        result += module_symbol(token, name);
        i = member_end;
        continue;
      }
      if (imported != public_exports.end()) {
        result += "__moss_private__" + token + "__" + name;
        i = member_end;
        continue;
      }
    }
    size_t after = end;
    while (after < expression.size() && std::isspace(static_cast<unsigned char>(expression[after]))) ++after;
    size_t before = i;
    while (before > 0 && std::isspace(static_cast<unsigned char>(expression[before - 1]))) --before;
    bool member = before > 0 && expression[before - 1] == '.';
    if (!member && (functions.count(token) || types.count(token) ||
         (after < expression.size() && expression[after] == '(' &&
          plain_identifier(token))) &&
        after < expression.size() && expression[after] == '(')
      result += module_symbol(module, token);
    else
      result += token;
    i = end;
  }
  return result;
}

static void rewrite_module_program(
    Program& program, const string& module,
    const std::map<string,ParsedModuleUnit>& modules,
    const std::map<string,std::set<string>>& public_exports) {
  auto type = [&](string value) {
    return rewrite_module_type(std::move(value), module, modules, public_exports);
  };
  auto expression = [&](string value) {
    return rewrite_module_expression(std::move(value), module, modules, public_exports);
  };
  auto params = [&](vector<Param>& values) {
    for (auto& param : values) param.type = type(param.type);
  };
  auto body = [&](vector<Stmt>& statements) {
    for (auto& statement : statements) {
      if (statement.kind == Stmt::Kind::Call && !statement.a.empty()) {
        string callee = trim(statement.a);
        if (callee.find('.') == string::npos && plain_identifier(callee) &&
            callee != "assert" && callee != "assertEqual" &&
            !starts_with(callee, module + "__"))
          statement.a = module_symbol(module, callee);
        else {
          string rewritten = expression(callee + "()");
          if (ends_with(rewritten, "()")) rewritten.resize(rewritten.size() - 2);
          statement.a = std::move(rewritten);
        }
      }
      statement.a = expression(statement.a);
      statement.b = expression(statement.b);
      for (auto& argument : statement.args) argument = expression(argument);
      if (!statement.text.empty()) statement.text = expression(statement.text);
    }
  };
  for (auto& function : program.functions) {
    string old = function.name;
    function.name = module_symbol(module, old);
    params(function.params);
    if (function.return_type) *function.return_type = type(*function.return_type);
    body(function.body);
    if (function.result_expression) *function.result_expression = expression(*function.result_expression);
  }
  for (auto& object : program.objects) {
    string old = object.name;
    object.name = module_symbol(module, old);
    for (auto& field : object.fields) field.type = type(field.type);
    for (auto& method : object.methods) {
      method.owner = object.name;
      params(method.params);
      if (method.return_type) *method.return_type = type(*method.return_type);
      body(method.body);
      if (method.result_expression) *method.result_expression = expression(*method.result_expression);
    }
  }
  for (auto& trait : program.traits) {
    trait.name = module_symbol(module, trait.name);
    for (auto& method : trait.methods) {
      params(method.params);
      if (method.return_type) *method.return_type = type(*method.return_type);
    }
  }
  for (auto& domain : program.domains) {
    domain.name = module_symbol(module, domain.name);
    for (auto& field : domain.state) field.type = type(field.type);
    for (auto& route : domain.routes) route.type = type(route.type);
    for (auto& handler : domain.handlers) {
      params(handler.params);
      if (handler.reply_type) *handler.reply_type = type(*handler.reply_type);
      body(handler.body);
    }
  }
  if (program.main) body(program.main->body);
  for (auto& test : program.tests) body(test.body);
  for (auto& benchmark : program.benchmarks) body(benchmark.body);
}

static void validate_module_exports(const std::map<string,ParsedModuleUnit>& modules,
                                    const std::map<string,std::set<string>>& public_exports) {
  for (const auto& entry : modules) {
    const string& module = entry.first;
    std::map<string,int> export_counts;
    for (const auto& file : entry.second.files) {
      for (const auto& function : file.first.functions)
        if (function.exported) ++export_counts[function.name];
      for (const auto& object : file.first.objects)
        if (object.exported) ++export_counts[object.name];
      for (const auto& trait : file.first.traits)
        if (trait.exported) ++export_counts[trait.name];
      for (const auto& domain : file.first.domains)
        if (domain.exported) ++export_counts[domain.name];
    }
    for (const auto& exported : export_counts) {
      if (exported.second > 1)
        throw CompileError(1, "duplicate exported name '" + exported.first +
                           "' in module '" + module + "'");
    }
    for (const auto& import : entry.second.imports) {
      if (!modules.count(import.name)) {
        CompileError error(import.line, "imported module '" + import.name + "' was not found");
        error.source_file = import.source_file;
        throw error;
      }
      if (import.name == module) {
        CompileError error(import.line, "module import cycle detected: " + module + " -> " + module);
        error.source_file = import.source_file;
        throw error;
      }
    }
  }
  std::map<string,int> state;
  vector<string> stack;
  std::function<void(const string&)> visit = [&](const string& module) {
    state[module] = 1;
    stack.push_back(module);
    for (const auto& import : modules.at(module).imports) {
      if (state[import.name] == 0) visit(import.name);
      else if (state[import.name] == 1) {
        auto begin = std::find(stack.begin(), stack.end(), import.name);
        std::ostringstream cycle;
        cycle << "module import cycle detected: ";
        for (auto at = begin; at != stack.end(); ++at) {
          if (at != begin) cycle << " -> ";
          cycle << *at;
        }
        cycle << " -> " << import.name;
        CompileError error(import.line, cycle.str());
        error.source_file = import.source_file;
        throw error;
      }
    }
    stack.pop_back();
    state[module] = 2;
  };
  for (const auto& entry : modules) if (state[entry.first] == 0) visit(entry.first);
  (void)public_exports;
}

static bool type_contains_domain(const string& type,
                                 const std::set<string>& domains) {
  string value = canonical_type_name(type);
  for (const auto& domain : domains) {
    if (value == domain) return true;
    if (value.find("[" + domain + "]") != string::npos ||
        value.find("," + domain + "]") != string::npos ||
        value.find("[" + domain + ",") != string::npos ||
        value.find(", " + domain) != string::npos)
      return true;
  }
  return false;
}

static void prepare_and_validate_module_exports(Program& program,
                                                bool finalize = true) {
  std::set<string> domains;
  for (const auto& domain : program.domains) domains.insert(domain.name);
  for (auto& function : program.functions) {
    if (!function.exported) continue;
    bool open = function.generic || std::any_of(
        function.params.begin(), function.params.end(),
        [](const Param& parameter) { return parameter.type.empty(); });
    // An explicitly untyped exported parameter is a Moss generic even when
    // its body happens not to exercise a structural operation.
    if (open) {
      function.generic = true;
      function.static_dispatch = true;
      if (!function.return_type) {
        string source_parameter;
        for (const auto& parameter : function.params)
          if (parameter.type.empty()) { source_parameter = parameter.name; break; }
        bool has_value_result = function.result_expression.has_value() ||
            std::any_of(function.body.begin(), function.body.end(),
                        [](const Stmt& statement) {
                          return statement.kind == Stmt::Kind::Return &&
                              !statement.a.empty();
                        });
        if (has_value_result && !source_parameter.empty()) {
          function.generic_results[source_parameter] = source_parameter;
          function.return_type = "_generic:" + source_parameter;
        }
      }
      for (const auto& parameter : function.params)
        if (type_contains_domain(parameter.type, domains)) {
          CompileError error(function.line,
              "generic exported function '" + function.name +
              "' may not accept domain-typed parameters");
          error.source_file = function.source_file;
          throw error;
        }
      if (!finalize) continue;
      continue;
    }
    if (!finalize) continue;
    for (const auto& parameter : function.params) {
      if (parameter.type == "vector" || parameter.type == "map" ||
          parameter.type == "queue" || starts_with(parameter.type, "_")) {
        CompileError error(function.line,
            "typed export '" + function.name + "' has an unresolved parameter type");
        error.source_file = function.source_file;
        throw error;
      }
    }
    if (!function.return_type || starts_with(*function.return_type, "_") ||
        *function.return_type == "vector" || *function.return_type == "map" ||
        *function.return_type == "queue") {
      CompileError error(function.line,
          "typed export '" + function.name + "' must have a closed concrete return type");
      error.source_file = function.source_file;
      throw error;
    }
  }
}

static std::optional<Effect> interface_effect(const string& value) {
  if (value == "READ") return Effect::Read;
  if (value == "WRITE") return Effect::Write;
  if (value == "CONSUME") return Effect::Consume;
  return std::nullopt;
}

static string interface_field(const string& text, const string& key) {
  string marker = key + "=";
  size_t begin = text.find(marker);
  if (begin == string::npos) return {};
  begin += marker.size();
  if (begin >= text.size()) return {};
  if (text[begin] == '"') {
    size_t end = text.find('"', begin + 1);
    return end == string::npos ? string() : text.substr(begin + 1, end - begin - 1);
  }
  size_t end = text.find_first_of(" \t\r\n", begin);
  return text.substr(begin, end == string::npos ? string::npos : end - begin);
}

static ObservableEffects interface_effects(const string& text) {
  ObservableEffects effects;
  auto flag = [&](const string& key) {
    return interface_field(text, key) == "1";
  };
  effects.local_capture_read = flag("local_capture_read");
  effects.local_mutation = flag("local_mutation");
  effects.domain_read = flag("domain_read");
  effects.domain_write = flag("domain_write");
  effects.message = flag("message");
  effects.external_io = flag("external_io");
  effects.may_fail = flag("may_fail");
  effects.may_diverge = flag("may_diverge");
  effects.unresolved = flag("unresolved");
  return effects;
}

static string interface_module_name(const string& text,
                                    const std::filesystem::path& file) {
  std::istringstream input(text);
  string record, value;
  while (input >> record) {
    if (record != "module_id") {
      std::getline(input, value);
      continue;
    }
    input >> value;
    size_t separator = value.rfind("::");
    return separator == string::npos ? value : value.substr(separator + 2);
  }
  return file.stem().string();
}

static void parse_generic_ir_line(const string& line, Function& function) {
  std::istringstream input(line);
  string record;
  input >> record;
  if (record == "generic_param") {
    size_t index = 0;
    string name, type;
    if (input >> index >> std::quoted(name) >> std::quoted(type)) {
      if (function.params.size() <= index) function.params.resize(index + 1);
      function.params[index] = {std::move(name), std::move(type)};
    }
  } else if (record == "generic_constraint") {
    int kind = 0;
    string subject, detail, result;
    if (input >> kind >> std::quoted(subject) >> std::quoted(detail) >>
        std::quoted(result))
      function.constraints.emplace_back(static_cast<ConstraintKind>(kind),
                                        std::move(subject), std::move(detail),
                                        std::move(result));
  } else if (record == "generic_stmt") {
    int kind = 0;
    Stmt statement;
    size_t argument_count = 0;
    if (!(input >> kind >> statement.line >> statement.indent >> std::quoted(statement.text) >>
          std::quoted(statement.a) >> std::quoted(statement.b) >>
          std::quoted(statement.semantic_type) >>
          argument_count)) return;
    statement.kind = static_cast<Stmt::Kind>(kind);
    for (size_t index = 0; index < argument_count; ++index) {
      string argument;
      if (!(input >> std::quoted(argument))) return;
      statement.args.push_back(std::move(argument));
    }
    function.body.push_back(std::move(statement));
  } else if (record == "generic_result") {
    if (input >> function.result_line) {
      string expression;
      if (input >> std::quoted(expression)) function.result_expression = std::move(expression);
    }
  }
}

static ParsedModuleUnit load_module_interface(
    const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  std::ostringstream content;
  content << input.rdbuf();
  string text = content.str();
  if (text.find("\nnative_abi 5\n") == string::npos)
    throw CompileError(0, "compiled provider uses an incompatible native calling convention; rebuild its .mossi provider");
  std::istringstream lines(text);
  ParsedModuleUnit unit;
  unit.name = interface_module_name(text, file);
  unit.explicit_module = true;
  Program provider;
  provider.explicit_module = true;
  provider.module_name = unit.name;
  string line;
  Function* current = nullptr;
  ObjectType* current_object = nullptr;
  Domain* current_domain = nullptr;
  bool in_semantic_exports = false;
  bool in_imports = false;
  while (std::getline(lines, line)) {
    line = trim(std::move(line));
    if (line == "semantic_exports") {
      in_semantic_exports = true;
      in_imports = false;
      current = nullptr;
      current_object = nullptr;
      current_domain = nullptr;
      continue;
    }
    if (line == "imports") {
      in_imports = true;
      continue;
    }
    if (line == "contract") {
      in_imports = false;
      continue;
    }
    if (starts_with(line, "generic_dependency_begin ")) {
      std::istringstream header(line.substr(25));
      string name, rest;
      header >> std::quoted(name);
      std::getline(header, rest);
      Function function;
      function.name = name;
      function.exported = false;
      function.generic = interface_field(rest, "kind") == "generic";
      function.static_dispatch = function.generic;
      string return_type = interface_field(rest, "return");
      if (!return_type.empty()) function.return_type = std::move(return_type);
      function.observable_effects = interface_effects(rest);
      provider.functions.push_back(std::move(function));
      current = &provider.functions.back();
      in_semantic_exports = true;
      continue;
    }
    if (line == "generic_dependency_end") {
      current = nullptr;
      continue;
    }
    if (in_imports && !line.empty() && line.find(' ') == string::npos) {
      ModuleImport import;
      import.name = line;
      import.owner_module = unit.name;
      import.source_file = file.string();
      unit.imports.push_back(std::move(import));
      continue;
    }
    if (!in_semantic_exports) {
      if (starts_with(line, "  ")) continue;
      if (line.empty()) continue;
      if (line[0] == ' ' || line[0] == '\t') continue;
    }
    if (starts_with(line, "  ")) line = trim(line);
    if (starts_with(line, "  ")) continue;
    if (starts_with(line, "export fn ")) {
      std::istringstream header(line.substr(10));
      string name, kind;
      header >> name;
      string rest;
      std::getline(header, rest);
      kind = interface_field(rest, "kind");
      if (name.empty()) continue;
      Function function;
      // The normal module namespace pass qualifies declarations.  Keep the
      // artifact-loaded declaration local here so the same pass can apply the
      // exact identity once, just as it does for source providers.
      function.name = name;
      function.exported = true;
      function.generic = kind.find("generic") != string::npos;
      function.static_dispatch = function.generic;
      string return_type = interface_field(rest, "return");
      if (!return_type.empty()) function.return_type = std::move(return_type);
      function.observable_effects = interface_effects(rest);
      provider.functions.push_back(std::move(function));
      current = &provider.functions.back();
      current_object = nullptr;
      current_domain = nullptr;
      continue;
    }
    if (starts_with(line, "export type ")) {
      std::istringstream header(line.substr(12));
      string name;
      header >> name;
      ObjectType object;
      object.name = name;
      object.exported = true;
      provider.objects.push_back(std::move(object));
      current_object = &provider.objects.back();
      current = nullptr;
      current_domain = nullptr;
      continue;
    }
    if (starts_with(line, "export domain ")) {
      std::istringstream header(line.substr(14));
      string name;
      header >> name;
      Domain domain;
      domain.name = name;
      domain.exported = true;
      provider.domains.push_back(std::move(domain));
      current_domain = &provider.domains.back();
      current = nullptr;
      current_object = nullptr;
      continue;
    }
    if (starts_with(line, "route ") && current_domain) {
      string declaration = trim(line.substr(6));
      size_t colon = declaration.find(':');
      if (colon != string::npos) {
        DomainRoute route;
        route.name = trim(declaration.substr(0, colon));
        route.type = canonical_type_name(trim(declaration.substr(colon + 1)));
        route.source_file = file.string();
        current_domain->routes.push_back(std::move(route));
      }
      continue;
    }
    if (starts_with(line, "construction_state ") && current_domain) {
      std::istringstream fields(line.substr(19));
      Field field;
      fields >> std::quoted(field.name) >> std::quoted(field.type)
             >> std::quoted(field.init);
      if (!fields)
        throw CompileError(0, "invalid construction state in module interface");
      field.source_file = file.string();
      current_domain->state.push_back(std::move(field));
      continue;
    }
    if (starts_with(line, "handler_state_effects ") && current_domain) {
      std::istringstream fields(line.substr(22));
      string handler_name;
      fields >> std::quoted(handler_name);
      auto handler = std::find_if(current_domain->handlers.begin(), current_domain->handlers.end(),
          [&](const Handler& candidate) { return candidate.name == handler_name; });
      if (!fields || handler == current_domain->handlers.end())
        throw CompileError(0, "invalid handler state effects in module interface");
      StateLeafEffects effects;
      for (auto* leaves : {&effects.reads, &effects.writes, &effects.consumes}) {
        size_t count = 0;
        if (!(fields >> count)) throw CompileError(0, "incomplete handler state effects in module interface");
        for (size_t i = 0; i < count; ++i) {
          string leaf;
          if (!(fields >> std::quoted(leaf))) throw CompileError(0, "invalid state leaf in module interface");
          leaves->insert(leaf);
        }
      }
      handler->state_effects = std::move(effects);
      continue;
    }
    if (starts_with(line, "handler_param ") && current_domain) {
      std::istringstream fields(line.substr(14));
      string handler_name;
      Param parameter;
      fields >> std::quoted(handler_name) >> std::quoted(parameter.name)
             >> std::quoted(parameter.type);
      auto handler = std::find_if(current_domain->handlers.begin(),
          current_domain->handlers.end(), [&](const Handler& candidate) {
            return candidate.name == handler_name;
          });
      if (!fields || handler == current_domain->handlers.end())
        throw CompileError(0, "invalid handler parameter in module interface");
      handler->params.push_back(std::move(parameter));
      continue;
    }
    if (starts_with(line, "export trait ")) {
      std::istringstream header(line.substr(13));
      string name;
      header >> name;
      Trait trait;
      trait.name = name;
      trait.exported = true;
      provider.traits.push_back(std::move(trait));
      current = nullptr;
      current_object = nullptr;
      current_domain = nullptr;
      continue;
    }
    if (starts_with(line, "public_representation field ") && current_object) {
      std::istringstream field(line.substr(string("public_representation field ").size()));
      Field value;
      field >> value.name >> value.type;
      current_object->fields.push_back(std::move(value));
      continue;
    }
    if (starts_with(line, "handler ") && current_domain) {
      std::istringstream handler_line(line.substr(8));
      string name;
      handler_line >> name;
      Handler handler;
      handler.name = name;
      string reply = interface_field(line, "reply");
      if (!reply.empty() && reply != "unit") handler.reply_type = std::move(reply);
      handler.observable_effects = interface_effects(line);
      if (handler.reply_type) {
        Stmt reply_statement;
        reply_statement.kind = Stmt::Kind::Reply;
        reply_statement.line = 0;
        string type = canonical_type_name(*handler.reply_type);
        reply_statement.a = type == "bool" ? "false"
            : type == "float" ? "0.0"
            : type == "string" ? "\"\"" : "0";
        handler.body.push_back(std::move(reply_statement));
      }
      current_domain->handlers.push_back(std::move(handler));
      continue;
    }
    if (starts_with(line, "parameter_leaf_effects ") && current) {
      std::istringstream fields(line.substr(23));
      StateLeafEffects effects;
      for (auto* leaves : {&effects.reads, &effects.writes, &effects.consumes}) {
        size_t count = 0;
        if (!(fields >> count)) throw CompileError(0, "incomplete parameter leaf effects in module interface");
        for (size_t i = 0; i < count; ++i) {
          string leaf;
          if (!(fields >> std::quoted(leaf))) throw CompileError(0, "invalid parameter leaf in module interface");
          leaves->insert(leaf);
        }
      }
      current->parameter_leaf_effects = std::move(effects);
      continue;
    }
    if (starts_with(line, "param ") && current) {
      std::istringstream parameter(line.substr(6));
      size_t index = 0;
      string type, mode_token, name_token;
      parameter >> index >> type >> mode_token >> name_token;
      if (starts_with(type, "mode=")) {
        name_token = mode_token;
        mode_token = type;
        type.clear();
      }
      if (current->params.size() <= index) current->params.resize(index + 1);
      current->params[index].type = type;
      current->params[index].name = name_token;
      if (starts_with(current->params[index].name, "name=\"")) {
        current->params[index].name = current->params[index].name.substr(6);
        if (!current->params[index].name.empty() && current->params[index].name.back() == '"')
          current->params[index].name.pop_back();
      }
      if (starts_with(mode_token, "mode=")) {
        auto effect = interface_effect(mode_token.substr(5));
        if (current->parameter_effects.size() <= index)
          current->parameter_effects.resize(index + 1, Effect::Read);
        if (effect) current->parameter_effects[index] = *effect;
      }
      continue;
    }

    if (starts_with(line, "open_parameters") && current) {
      current->generic = true;
      current->static_dispatch = true;
      continue;
    }
    if (starts_with(line, "generic_ir_version") ||
        starts_with(line, "generic_entity_id") ||
        starts_with(line, "generic_param") ||
        starts_with(line, "generic_constraint") ||
        starts_with(line, "generic_stmt") ||
        starts_with(line, "generic_result")) {
      if (current) parse_generic_ir_line(line, *current);
      continue;
    }
  }
  for (auto& function : provider.functions) {
    function.source_file = file.string();
    function.header = "fn " + function.name;
    // A generic interface records open parameters separately from its body;
    // preserve the semantic marker used by normal Moss specialization.
    if (function.generic && function.return_type &&
        starts_with(*function.return_type, "_generic:"))
      function.generic_results[function.return_type->substr(9)] =
          function.return_type->substr(9);
  }
  unit.files.push_back({std::move(provider), file.string()});
  return unit;
}

static vector<std::filesystem::path> compiled_interface_candidates(
    const ProjectManifest& manifest) {
  vector<std::filesystem::path> roots = {
      manifest.root / "build" / "debug", manifest.root / "deps"};
  if (const char* path = std::getenv("MOSS_MODULE_PATH")) {
    string value = path;
    size_t begin = 0;
    while (begin <= value.size()) {
      size_t end = value.find(':', begin);
      roots.emplace_back(value.substr(begin, end == string::npos
                                                ? string::npos : end - begin));
      if (end == string::npos) break;
      begin = end + 1;
    }
  }
  vector<std::filesystem::path> result;
  std::error_code error;
  for (const auto& root : roots) {
    if (!std::filesystem::is_directory(root, error)) continue;
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         !error && it != end; it.increment(error))
      if (it->is_regular_file(error) && it->path().extension() == ".mossi")
        {
          std::error_code canonical_error;
          auto path = std::filesystem::weakly_canonical(it->path(), canonical_error);
          result.push_back(canonical_error ? it->path().lexically_normal()
                                           : path.lexically_normal());
        }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

struct CompiledModuleProvider {
  string module;
  std::filesystem::path interface_file;
};

// Module names in source are semantic identities. A filename is merely an
// artifact convention, so read the declared module_id before considering a
// compiled provider for an import.
static vector<CompiledModuleProvider> compiled_module_providers(
    const ProjectManifest& manifest) {
  vector<CompiledModuleProvider> providers;
  for (const auto& file : compiled_interface_candidates(manifest)) {
    string text = read_text_file(file, "MOSS_INTERFACE_ERROR");
    providers.push_back({interface_module_name(text, file), file});
  }
  std::sort(providers.begin(), providers.end(),
            [](const CompiledModuleProvider& left,
               const CompiledModuleProvider& right) {
              return std::tie(left.module, left.interface_file) <
                     std::tie(right.module, right.interface_file);
            });
  providers.erase(std::unique(providers.begin(), providers.end(),
                              [](const CompiledModuleProvider& left,
                                 const CompiledModuleProvider& right) {
                                return left.module == right.module &&
                                       left.interface_file == right.interface_file;
                              }),
                  providers.end());
  return providers;
}

static const CompiledModuleProvider* unique_compiled_module_provider(
    const vector<CompiledModuleProvider>& providers, const ModuleImport& import) {
  auto begin = std::lower_bound(
      providers.begin(), providers.end(), import.name,
      [](const CompiledModuleProvider& provider, const string& name) {
        return provider.module < name;
      });
  auto end = begin;
  while (end != providers.end() && end->module == import.name) ++end;
  if (begin == end) return nullptr;
  if (std::next(begin) == end) return &*begin;
  std::ostringstream message;
  message << "ambiguous imported module '" << import.name << "'\nprovided by:";
  for (auto provider = begin; provider != end; ++provider)
    message << "\n  " << provider->interface_file.string();
  CompileError error(import.line, message.str());
  error.code = "MODULE_IMPORT_AMBIGUOUS";
  error.source_file = import.source_file;
  throw error;
}

static CompiledProjectUnit analyze_project_sources(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources,
    bool optimized, bool debug_build, ProgramGenerationMode mode,
    const string& declaration_filter = {},
    const std::set<string>* declaration_ids = nullptr) {
  try {
    Program program;
    std::map<string,ParsedModuleUnit> modules;
    std::map<string,ModuleImport> requested_imports;
    std::set<string> loaded_external_modules;
    std::map<string,std::filesystem::path> external_module_interfaces;
    bool has_explicit_modules = false;
    for (const auto& source : sources) {
      std::ifstream input(source);
      if (!input)
        throw ProjectError("PROJECT_SOURCE_NOT_FOUND",
                           "cannot read Moss source '" + source.string() + "'",
                           source.string());
      Program parsed = Parser(lex_lines(input, source.string())).parse();
      assign_project_declaration_identities(
          parsed, project_relative_path(manifest, source));
      has_explicit_modules = has_explicit_modules || parsed.explicit_module;
      string module = parsed.explicit_module && !parsed.module_name.empty()
          ? parsed.module_name : manifest.name;
      auto& unit = modules[module];
      unit.name = module;
      unit.explicit_module = unit.explicit_module || parsed.explicit_module;
      unit.imports.insert(unit.imports.end(), parsed.imports.begin(), parsed.imports.end());
      unit.files.push_back({std::move(parsed), source.string()});
    }
    // A dependency may be represented only by its compiled Moss interface.
    // Load it into the semantic namespace, but keep it out of this build's
    // declaration set so its Rust implementation is consumed from its rlib.
    if (has_explicit_modules) {
      auto request_import = [&](const ModuleImport& import) {
        if (!modules.count(import.name)) requested_imports.emplace(import.name, import);
      };
      for (const auto& entry : modules)
        for (const auto& import : entry.second.imports)
          request_import(import);
      // Interfaces may themselves import another package interface.  Resolve
      // that declared module closure from the existing MOSS_MODULE_PATH rather
      // than requiring a package driver to fold provider source into this
      // compilation unit.
      const auto providers = compiled_module_providers(manifest);
      bool loaded = true;
      while (loaded) {
        loaded = false;
        for (const auto& requested : requested_imports) {
          const string& name = requested.first;
          if (modules.count(name)) continue;
          const auto* selected = unique_compiled_module_provider(
              providers, requested.second);
          if (!selected) continue;
          ParsedModuleUnit provider = load_module_interface(selected->interface_file);
          // The provider map was indexed by the interface's module_id; retain
          // that invariant here rather than accepting a filename coincidence.
          if (provider.name != name)
            throw CompileError(requested.second.line,
                "compiled provider identity changed while resolving module '" + name + "'");
          loaded_external_modules.insert(name);
          external_module_interfaces.emplace(name, selected->interface_file);
          for (const auto& import : provider.imports)
            request_import(import);
          modules.emplace(name, std::move(provider));
          loaded = true;
        }
      }
      for (const auto& name : requested_imports)
        if (!modules.count(name.first))
          throw CompileError(name.second.line, "imported module '" + name.first +
                             "' was not found in source or compiled interfaces");
    }
    if (has_explicit_modules) {
      program.explicit_module = true;
      std::map<string,std::set<string>> public_exports;
      for (const auto& entry : modules)
        public_exports[entry.first] = module_export_names(entry.second);
      validate_module_exports(modules, public_exports);
      // Rewriting/moving one file must not erase names needed to resolve the
      // next file's route types. Keep the source namespace immutable.
      const auto source_modules = modules;
      for (auto& entry : modules) {
        bool external = loaded_external_modules.count(entry.first) != 0;
        if (external) program.external_modules.insert(entry.first);
        for (auto& file : entry.second.files) {
          for (auto& import : file.first.imports) import.owner_module = entry.first;
          rewrite_module_program(file.first, entry.first, source_modules, public_exports);
          if (file.first.main && !external) program.main_module = entry.first;
          merge_project_program(program, std::move(file.first), file.second);
        }
      }
    } else {
      // Legacy projects remain one implicit module and retain their existing
      // source order-independent whole-project semantics.
      for (auto& entry : modules) {
        for (auto& file : entry.second.files) {
          if (file.first.main) program.main_module = manifest.name;
          merge_project_program(program, std::move(file.first), file.second);
        }
      }
    }
    prepare_and_validate_module_exports(program, false);
    Checker checker(program);
    checker.run();
    prepare_and_validate_module_exports(program, true);
    vector<Warning> warnings = checker.warnings();
    if (!declaration_filter.empty() || declaration_ids) {
      if (mode == ProgramGenerationMode::Tests) {
        program.tests.erase(
            std::remove_if(
                program.tests.begin(), program.tests.end(),
                [&](const TestDecl& test) {
                  if (declaration_ids)
                    return !declaration_ids->count(test.semantic_identity);
                  return test.name.find(declaration_filter) == string::npos &&
                      test.semantic_identity.find(declaration_filter) ==
                          string::npos;
                }),
            program.tests.end());
      } else if (mode == ProgramGenerationMode::Benchmarks) {
        program.benchmarks.erase(
            std::remove_if(
                program.benchmarks.begin(), program.benchmarks.end(),
                [&](const BenchDecl& benchmark) {
                  if (declaration_ids)
                    return !declaration_ids->count(
                        benchmark.semantic_identity);
                  return benchmark.name.find(declaration_filter) ==
                             string::npos &&
                      benchmark.semantic_identity.find(declaration_filter) ==
                          string::npos;
                }),
            program.benchmarks.end());
      }
    }
    FunctionalOptimizer(program).run(optimized);
    OptimizationPlan plan = OptimizationPlan{optimized};
    string rust = Generator(program, plan, debug_build, mode).generate();
    return {std::move(program), std::move(plan), std::move(warnings),
            std::move(rust), std::move(external_module_interfaces)};
  } catch (const CompileError& error) {
    throw ProjectError(
        error.code.empty() ? diagnostic_code_for_message(error.what())
                           : error.code,
        error.what(), error.source_file.empty()
            ? (sources.empty() ? string() : sources.front().string())
            : error.source_file,
        error.line);
  }
}

static CompiledProjectUnit analyze_project_texts(
    const ProjectManifest& manifest,
    const vector<std::pair<std::filesystem::path, string>>& files,
    bool optimized, bool debug_build, ProgramGenerationMode mode) {
  try {
    Program program;
    for (const auto& entry : files) {
      const auto& source = entry.first;
      std::istringstream input(entry.second);
      Program parsed = Parser(lex_lines(input, source.string())).parse();
      assign_project_declaration_identities(
          parsed, project_relative_path(manifest, source));
      merge_project_program(program, std::move(parsed), source.string());
    }
    Checker checker(program);
    checker.run();
    vector<Warning> warnings = checker.warnings();
    FunctionalOptimizer(program).run(optimized);
    OptimizationPlan plan = OptimizationPlan{optimized};
    string rust = Generator(program, plan, debug_build, mode).generate();
    return {std::move(program), std::move(plan), std::move(warnings),
            std::move(rust), {}};
  } catch (const CompileError& error) {
    throw ProjectError(
        error.code.empty() ? diagnostic_code_for_message(error.what())
                           : error.code,
        error.what(), error.source_file.empty()
            ? (files.empty() ? string() : files.front().first.string())
            : error.source_file,
        error.line);
  }
}

static CompiledProjectUnit analyze_project_source(
    const ProjectManifest& manifest, const std::filesystem::path& source,
    bool optimized, bool debug_build, ProgramGenerationMode mode,
    const string& declaration_filter = {},
    const std::set<string>* declaration_ids = nullptr) {
  return analyze_project_sources(manifest, {source}, optimized, debug_build,
                                 mode, declaration_filter, declaration_ids);
}

static SemanticSnapshot analyze_project_snapshot(
    const ProjectManifest& manifest, const std::filesystem::path& source) {
  CompiledProjectUnit unit = analyze_project_source(
      manifest, source, true, false, ProgramGenerationMode::Application);
  return make_semantic_snapshot(manifest, source, unit.program, unit.plan);
}

static SemanticSnapshot analyze_project_snapshot(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources) {
  CompiledProjectUnit unit = analyze_project_sources(
      manifest, sources, true, false, ProgramGenerationMode::Application);
  return make_semantic_snapshot(manifest, sources, unit.program, unit.plan);
}

static const SemanticSnapshotUnit* resolve_snapshot_unit(
    const SemanticSnapshot& snapshot, const string& selector,
    bool* ambiguous = nullptr) {
  if (ambiguous) *ambiguous = false;
  const SemanticSnapshotUnit* match = nullptr;
  auto consider = [&](const SemanticSnapshotUnit& unit) {
    bool matches = unit.durable_identity == selector ||
        unit.semantic_identity == selector || unit.context == selector ||
        unit.name == selector || unit.kind + ":" + unit.name == selector;
    if (!matches) return;
    if (match && match->durable_identity != unit.durable_identity) {
      if (ambiguous) *ambiguous = true;
      return;
    }
    match = &unit;
  };
  for (const auto& unit : snapshot.units) consider(unit);
  return match;
}

struct SemanticImpact {
  string classification;
  const SemanticSnapshotUnit* current = nullptr;
  const SemanticSnapshotUnit* previous = nullptr;
  vector<string> direct_dependents;
  vector<string> transitive_dependents;
  vector<string> invalidated_dependents;
  vector<string> affected_specializations;
  vector<string> affected_domains;
  vector<string> affected_tests;
  vector<string> affected_benchmarks;
  vector<SemanticSnapshotRouteEdge> affected_route_edges;
  size_t units_analyzed = 0;
  size_t units_reused = 0;
  size_t dependent_units_invalidated = 0;
};

static SemanticImpact compute_semantic_impact(
    const SemanticSnapshot& current,
    const std::optional<SemanticSnapshot>& previous,
    const string& selector) {
  bool current_ambiguous = false;
  bool previous_ambiguous = false;
  const SemanticSnapshotUnit* current_target =
      resolve_snapshot_unit(current, selector, &current_ambiguous);
  const SemanticSnapshotUnit* previous_target = previous
      ? resolve_snapshot_unit(*previous, selector, &previous_ambiguous)
      : nullptr;
  if (current_ambiguous || previous_ambiguous)
    throw ProjectError(
        "IMPACT_TARGET_AMBIGUOUS",
        "impact target '" + selector + "' matches multiple semantic units");
  if (!current_target && !previous_target)
    throw ProjectError(
        "IMPACT_TARGET_NOT_FOUND",
        "no exact Moss semantic unit matches '" + selector + "'");

  SemanticImpact impact;
  impact.current = current_target;
  impact.previous = previous_target;
  if (!previous) impact.classification = "unknown_baseline";
  else if (!previous_target) impact.classification = "added";
  else if (!current_target) impact.classification = "removed";
  else if (current_target->implementation_hash ==
               previous_target->implementation_hash &&
           current_target->interface_hash == previous_target->interface_hash)
    impact.classification = "unchanged";
  else if (current_target->interface_hash == previous_target->interface_hash)
    impact.classification = "implementation_only";
  else
    impact.classification = "semantic_interface_change";

  std::unordered_map<string,const SemanticSnapshotUnit*> units;
  for (const auto& unit : current.units) units[unit.durable_identity] = &unit;
  if (previous)
    for (const auto& unit : previous->units)
      if (!units.count(unit.durable_identity))
        units[unit.durable_identity] = &unit;
  std::unordered_map<string,std::set<string>> reverse;
  auto add_edges = [&](const SemanticSnapshot& snapshot) {
    for (const auto& unit : snapshot.units)
      for (const auto& dependency : unit.dependencies)
        reverse[dependency].insert(unit.durable_identity);
  };
  add_edges(current);
  if (previous) add_edges(*previous);
  string changed = current_target ? current_target->durable_identity
                                  : previous_target->durable_identity;
  auto direct = reverse.find(changed);
  if (direct != reverse.end())
    impact.direct_dependents.assign(direct->second.begin(),
                                    direct->second.end());
  std::set<string> visited;
  std::set<string> pending(impact.direct_dependents.begin(),
                           impact.direct_dependents.end());
  while (!pending.empty()) {
    string next = *pending.begin();
    pending.erase(pending.begin());
    if (!visited.insert(next).second) continue;
    auto dependents = reverse.find(next);
    if (dependents != reverse.end())
      pending.insert(dependents->second.begin(), dependents->second.end());
  }
  impact.transitive_dependents.assign(visited.begin(), visited.end());
  bool invalidate_dependents = impact.classification ==
      "semantic_interface_change" || impact.classification == "added" ||
      impact.classification == "removed" ||
      impact.classification == "unknown_baseline";
  if (invalidate_dependents)
    impact.invalidated_dependents = impact.transitive_dependents;

  std::set<string> verification_cone = visited;
  verification_cone.insert(changed);
  for (const auto& identity : verification_cone) {
    auto found = units.find(identity);
    if (found == units.end()) continue;
    const auto& unit = *found->second;
    if (unit.kind == "test") impact.affected_tests.push_back(identity);
    if (unit.kind == "benchmark")
      impact.affected_benchmarks.push_back(identity);
    if (unit.kind == "specialization")
      impact.affected_specializations.push_back(identity);
    if (unit.kind == "domain")
      impact.affected_domains.push_back(identity);
    if (unit.kind == "handler") {
      impact.affected_domains.push_back(identity);
      // A handler is the executable member of a domain.  Report both
      // identities so callers can invalidate the concrete operation while
      // still selecting domain-level route/backend facts.
      size_t dot = unit.name.find('.');
      if (dot != string::npos) {
        string domain_name = unit.name.substr(0, dot);
        for (const auto& candidate : units)
          if (candidate.second->kind == "domain" &&
              candidate.second->name == domain_name)
            impact.affected_domains.push_back(candidate.second->durable_identity);
      }
    }
  }
  if ((current_target && current_target->kind == "function") ||
      (previous_target && previous_target->kind == "function")) {
    string name = current_target ? current_target->name : previous_target->name;
    string marker = "entity-v1:specialization:" + name + "<";
    for (const auto& entry : units)
      if (starts_with(entry.first, marker))
        impact.affected_specializations.push_back(entry.first);
  }
  auto add_route_edges = [&](const SemanticSnapshot& snapshot) {
    for (const auto& edge : snapshot.route_edges) {
      bool relevant = false;
      for (const auto& identity : impact.affected_domains)
        if (identity.find(":" + edge.source_domain) != string::npos ||
            identity.find(":" + edge.target_domain) != string::npos)
          relevant = true;
      const SemanticSnapshotUnit* target = current_target
          ? current_target : previous_target;
      if (target && (target->name == edge.source_domain ||
                     target->name == edge.target_domain ||
                     starts_with(target->name, edge.source_domain + ".")))
        relevant = true;
      if (!relevant) continue;
      auto duplicate = std::find_if(
          impact.affected_route_edges.begin(),
          impact.affected_route_edges.end(),
          [&](const SemanticSnapshotRouteEdge& candidate) {
            return candidate.source_domain == edge.source_domain &&
                candidate.target_domain == edge.target_domain &&
                candidate.line == edge.line;
          });
      if (duplicate == impact.affected_route_edges.end())
        impact.affected_route_edges.push_back(edge);
    }
  };
  add_route_edges(current);
  if (previous) add_route_edges(*previous);
  auto unique_sort = [](vector<string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
  };
  unique_sort(impact.affected_specializations);
  unique_sort(impact.affected_domains);
  unique_sort(impact.affected_tests);
  unique_sort(impact.affected_benchmarks);

  impact.units_analyzed = current.units.size();
  if (previous) {
    std::unordered_map<string,const SemanticSnapshotUnit*> old;
    for (const auto& unit : previous->units) old[unit.durable_identity] = &unit;
    for (const auto& unit : current.units) {
      auto prior = old.find(unit.durable_identity);
      if (prior != old.end() &&
          prior->second->implementation_hash == unit.implementation_hash &&
          prior->second->interface_hash == unit.interface_hash)
        ++impact.units_reused;
    }
  }
  impact.dependent_units_invalidated = impact.invalidated_dependents.size();
  return impact;
}

static void write_impact_unit_json(std::ostream& out,
                                   const SemanticSnapshotUnit* unit) {
  if (!unit) {
    out << "null";
    return;
  }
  out << "{\"durable_identity\": ";
  write_debug_json_string(out, unit->durable_identity);
  out << ", \"semantic_identity\": ";
  write_debug_json_string(out, unit->semantic_identity);
  out << ", \"source_file\": ";
  write_debug_json_string(out, unit->source_file);
  out << ", \"kind\": ";
  write_debug_json_string(out, unit->kind);
  out << ", \"name\": ";
  write_debug_json_string(out, unit->name);
  out << ", \"implementation_hash\": ";
  write_debug_json_string(out, unit->implementation_hash);
  out << ", \"semantic_interface_hash\": ";
  write_debug_json_string(out, unit->interface_hash);
  out << "}";
}

static bool path_within(const std::filesystem::path& path,
                        const std::filesystem::path& root);
static vector<std::filesystem::path> project_declaration_sources(
    const ProjectManifest& manifest, const string& directory_name);

static int run_project_impact(const ProjectManifest& manifest,
                              const std::filesystem::path& source,
                              const string& selector, bool json) {
  vector<std::filesystem::path> sources;
  auto source_root = manifest.root / manifest.source;
  auto tests_root = manifest.root / "tests";
  auto benches_root = manifest.root / "benches";
  if (path_within(source, tests_root))
    sources = project_declaration_sources(manifest, "tests");
  else if (path_within(source, benches_root))
    sources = project_declaration_sources(manifest, "benches");
  else if (path_within(source, source_root))
    sources = project_source_files(manifest);
  else
    sources = {source};
  auto previous = read_semantic_snapshot(manifest, sources);
  SemanticSnapshot current = analyze_project_snapshot(manifest, sources);
  SemanticImpact impact = compute_semantic_impact(current, previous, selector);
  if (!json)
    throw ProjectError("IMPACT_JSON_REQUIRED",
                       "moss impact currently requires --json");
  write_agent_envelope_begin(std::cout, "impact", true);
  std::cout << "  \"result\": {\"source_file\": ";
  write_debug_json_string(std::cout, source.string());
  std::cout << ", \"baseline_available\": "
            << (previous ? "true" : "false")
            << ", \"change_kind\": ";
  write_debug_json_string(std::cout, impact.classification);
  std::cout << ", \"changed_semantic_unit\": ";
  write_impact_unit_json(std::cout, impact.current ? impact.current
                                                   : impact.previous);
  std::cout << ", \"previous_unit\": ";
  write_impact_unit_json(std::cout, impact.previous);
  std::cout << ", \"direct_dependents\": ";
  write_agent_string_array(std::cout, impact.direct_dependents);
  std::cout << ", \"transitive_dependents\": ";
  write_agent_string_array(std::cout, impact.transitive_dependents);
  std::cout << ", \"invalidated_dependents\": ";
  write_agent_string_array(std::cout, impact.invalidated_dependents);
  std::cout << ", \"affected_specializations\": ";
  write_agent_string_array(std::cout, impact.affected_specializations);
  std::cout << ", \"affected_domains\": ";
  write_agent_string_array(std::cout, impact.affected_domains);
  std::cout << ", \"affected_route_dependencies\": [";
  for (size_t index = 0; index < impact.affected_route_edges.size(); ++index) {
    if (index) std::cout << ", ";
    const auto& edge = impact.affected_route_edges[index];
    std::cout << "{\"source_domain\": ";
    write_debug_json_string(std::cout, edge.source_domain);
    std::cout << ", \"target_domain\": ";
    write_debug_json_string(std::cout, edge.target_domain);
    std::cout << ", \"line\": " << edge.line << "}";
  }
  std::cout << "], \"affected_tests\": ";
  write_agent_string_array(std::cout, impact.affected_tests);
  std::cout << ", \"affected_benchmarks\": ";
  write_agent_string_array(std::cout, impact.affected_benchmarks);
  std::cout << ", \"incremental\": {\"units_analyzed\": "
            << impact.units_analyzed << ", \"units_reused\": "
            << impact.units_reused
            << ", \"generated_rust_units_rewritten\": 0"
            << ", \"dependent_units_invalidated\": "
            << impact.dependent_units_invalidated << "}}\n}\n";
  return 0;
}

static string module_name_from_symbol(const string& symbol) {
  auto separator = symbol.find("__");
  return separator == string::npos ? string() : symbol.substr(0, separator);
}

static bool belongs_to_module(const string& qualified, const string& module) {
  return qualified == module || starts_with(qualified, module + "__");
}

// The semantic checker still sees the composed project.  Once that single
// authority has produced its facts, this projection is what gives rustc one
// crate per Moss module.  It intentionally copies no declarations from an
// imported module; imported names remain Rust crate references emitted by the
// module generator.
static Program module_program(const Program& whole, const string& module) {
  Program result;
  result.module_name = module;
  result.explicit_module = true;
  result.imports = whole.imports;
  std::set<string> generic_support;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& edge : whole.semantic_call_edges) {
      bool source_is_generic = starts_with(edge.source, "fn:");
      string source_name = source_is_generic ? edge.source.substr(3) : string();
      auto source = std::find_if(whole.functions.begin(), whole.functions.end(),
          [&](const Function& function) { return function.name == source_name; });
      if (source == whole.functions.end() ||
          !(source->generic || generic_support.count(source->name))) continue;
      for (const auto& function : whole.functions)
        if (!function.exported && edge.target.find(function.name) != string::npos &&
            generic_support.insert(function.name).second)
          changed = true;
    }
  }
  for (const auto& function : whole.functions)
    if (belongs_to_module(function.name, module) &&
        !generic_support.count(function.name)) result.functions.push_back(function);
  for (const auto& object : whole.objects)
    if (belongs_to_module(object.name, module)) result.objects.push_back(object);
  for (const auto& trait : whole.traits)
    if (belongs_to_module(trait.name, module)) result.traits.push_back(trait);
  for (const auto& domain : whole.domains)
    if (whole.main_module == module || belongs_to_module(domain.name, module)) result.domains.push_back(domain);
  if (whole.main && whole.main_module == module) result.main = whole.main;
  for (const auto& test : whole.tests)
    if (whole.main_module.empty() || whole.main_module == module)
      result.tests.push_back(test);
  for (const auto& benchmark : whole.benchmarks)
    if (whole.main_module.empty() || whole.main_module == module)
      result.benchmarks.push_back(benchmark);
  // Functional plans are compiler-owned IR referenced by statements.  They
  // are cheap metadata and retaining the complete plan avoids re-analysis or
  // source reconstruction in a module crate.
  result.functional_pipelines = whole.functional_pipelines;
  result.functional_traversal_groups = whole.functional_traversal_groups;
  result.semantic_call_edges = whole.semantic_call_edges;
  result.concrete_domain_graph = whole.concrete_domain_graph;
  result.synchronization_plan = whole.synchronization_plan;
  result.domain_specializations = whole.domain_specializations;
  return result;
}

static OptimizationPlan module_plan(const OptimizationPlan& whole, const Program&) { return whole; }

static vector<string> module_names_for_program(const Program& program) {
  std::set<string> names;
  for (const auto& function : program.functions)
    if (!module_name_from_symbol(function.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(function.name)))
      names.insert(module_name_from_symbol(function.name));
  for (const auto& object : program.objects)
    if (!module_name_from_symbol(object.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(object.name)))
      names.insert(module_name_from_symbol(object.name));
  for (const auto& domain : program.domains)
    if (!module_name_from_symbol(domain.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(domain.name)))
      names.insert(module_name_from_symbol(domain.name));
  if (!program.main_module.empty()) names.insert(program.main_module);
  return vector<string>(names.begin(), names.end());
}

static vector<string> module_dependencies(const Program& program,
                                          const string& module) {
  std::set<string> result;
  for (const auto& import : program.imports)
    if (import.owner_module == module) result.insert(import.name);
  return vector<string>(result.begin(), result.end());
}

static vector<string> module_topological_order(const Program& program) {
  vector<string> names = module_names_for_program(program);
  std::set<string> known(names.begin(), names.end());
  known.insert(program.external_modules.begin(), program.external_modules.end());
  std::map<string,int> state;
  vector<string> order;
  std::function<void(const string&)> visit = [&](const string& module) {
    if (program.external_modules.count(module)) return;
    if (state[module] == 2) return;
    if (state[module] == 1)
      throw ProjectError("MODULE_IMPORT_CYCLE", "module import cycle detected at '" + module + "'");
    state[module] = 1;
    for (const auto& dependency : module_dependencies(program, module)) {
      if (!known.count(dependency))
        throw ProjectError("MODULE_PROVIDER_NOT_FOUND",
                           "no provider for imported module '" + dependency + "'");
      visit(dependency);
    }
    state[module] = 2;
    order.push_back(module);
  };
  for (const auto& module : names) visit(module);
  return order;
}

static string interface_effects_text(const ObservableEffects& effects) {
  std::ostringstream out;
  out << "local_capture_read=" << (effects.local_capture_read ? 1 : 0)
      << ";local_mutation=" << (effects.local_mutation ? 1 : 0)
      << ";domain_read=" << (effects.domain_read ? 1 : 0)
      << ";domain_write=" << (effects.domain_write ? 1 : 0)
      << ";message=" << (effects.message ? 1 : 0)
      << ";external_io=" << (effects.external_io ? 1 : 0)
      << ";may_fail=" << (effects.may_fail ? 1 : 0)
      << ";may_diverge=" << (effects.may_diverge ? 1 : 0)
      << ";unresolved=" << (effects.unresolved ? 1 : 0);
  return out.str();
}

static vector<std::filesystem::path> write_module_interfaces(
    const ProjectManifest& manifest, const Program& program,
    const std::filesystem::path& directory,
    const BackendToolchainIdentity& backend) {
  std::map<string,std::ostringstream> contents;
  std::map<string,std::ostringstream> interface_contents;
  std::set<string> module_names;
  for (const auto& function : program.functions)
    if (!module_name_from_symbol(function.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(function.name)))
      module_names.insert(module_name_from_symbol(function.name));
  for (const auto& object : program.objects)
    if (!module_name_from_symbol(object.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(object.name)))
      module_names.insert(module_name_from_symbol(object.name));
  for (const auto& domain : program.domains)
    if (!module_name_from_symbol(domain.name).empty() &&
        !program.external_modules.count(module_name_from_symbol(domain.name)))
      module_names.insert(module_name_from_symbol(domain.name));
  for (const auto& import : program.imports)
    if (!import.owner_module.empty()) module_names.insert(import.owner_module);
  for (const auto& module : module_names) {
    (void)contents[module];
    (void)interface_contents[module];
  }
  for (const auto& function : program.functions) {
    if (!function.exported) continue;
    string module = module_name_from_symbol(function.name);
    if (module.empty() || program.external_modules.count(module)) continue;
    auto& out = contents[module];
    auto& abi = interface_contents[module];
    string public_name = function.name.substr(module.size() + 2);
    string symbol = tooling_native_symbol(
        "function", function.name,
        "fn:" + function.name + "@" + std::to_string(function.line));
    abi << "fn " << public_name << " kind="
        << (function.generic ? "generic" : "concrete")
        << " return=" << function.return_type.value_or("unit")
        << " effects=" << interface_effects_text(function.observable_effects)
        << " symbol=" << symbol << "\n";
    out << "export fn " << function.name.substr(module.size() + 2)
        << " kind=" << (function.generic ? "generic" : "concrete")
        << " semantic_hash=";
    std::ostringstream body;
    body << function.header << "\n";
    for (const auto& statement : function.body) body << statement.text << "\n";
    if (function.result_expression) body << *function.result_expression << "\n";
    string semantic_hash = stable_hash(body.str());
    out << semantic_hash << " return=" << function.return_type.value_or("unit")
        << " effects=" << interface_effects_text(function.observable_effects) << "\n";
    if (function.parameter_leaf_effects) {
      std::ostringstream effects;
      effects << "  parameter_leaf_effects";
      for (const auto* leaves : {&function.parameter_leaf_effects->reads,
                                 &function.parameter_leaf_effects->writes,
                                 &function.parameter_leaf_effects->consumes}) {
        effects << " " << leaves->size();
        for (const auto& leaf : *leaves) effects << " " << std::quoted(leaf);
      }
      effects << "\n";
      out << effects.str();
      abi << effects.str();
    }
    for (size_t index = 0; index < function.params.size(); ++index) {
      Effect effect = index < function.parameter_effects.size()
          ? function.parameter_effects[index] : Effect::Read;
      out << "  param " << index << " " << function.params[index].type
          << " mode=" << ownership_effect_name(effect)
          << " name=" << std::quoted(function.params[index].name) << "\n";
      abi << "  param " << index << " " << function.params[index].type
          << " mode=" << ownership_effect_name(effect)
          << " name=" << std::quoted(function.params[index].name) << "\n";
    }
    if (function.generic) {
      out << "  open_parameters";
      for (size_t index = 0; index < function.params.size(); ++index)
        if (function.params[index].type.empty()) out << " " << index;
      out << "\n  requirements\n";
      for (const auto& constraint : function.constraints)
        out << "    " << static_cast<int>(constraint.kind) << " "
            << constraint.subject << " " << constraint.detail << "\n";
      out << "  specialization_dependencies\n";
      for (const auto& edge : program.semantic_call_edges)
        if (edge.source == "fn:" + function.name)
          out << "    " << edge.target << "\n";
      out << "  semantic_ir_body_hash=" << semantic_hash << "\n";
      out << "  generic_ir_version 1\n";
      out << "  generic_entity_id " << std::quoted("fn:" + function.name +
                                                    "@" + std::to_string(function.line)) << "\n";
      for (size_t index = 0; index < function.params.size(); ++index)
        out << "  generic_param " << index << " "
            << std::quoted(function.params[index].name) << " "
            << std::quoted(function.params[index].type) << "\n";
      for (const auto& constraint : function.constraints) {
        out << "  generic_constraint " << static_cast<int>(constraint.kind)
            << " " << std::quoted(constraint.subject) << " "
            << std::quoted(constraint.detail) << " "
            << std::quoted(constraint.result) << "\n";
      }
      for (const auto& statement : function.body) {
        out << "  generic_stmt " << static_cast<int>(statement.kind) << " "
            << statement.line << " " << statement.indent << " "
            << std::quoted(statement.text) << " "
            << std::quoted(statement.a) << " "
            << std::quoted(statement.b) << " "
            << std::quoted(statement.semantic_type) << " "
            << statement.args.size();
        for (const auto& argument : statement.args)
          out << " " << std::quoted(argument);
        out << "\n";
      }
      if (function.result_expression)
        out << "  generic_result " << function.result_line << " "
            << std::quoted(*function.result_expression) << "\n";
      out << "  generic_ir_end\n";
      abi << "  semantic_ir_body_hash=" << semantic_hash << "\n";
    }
  }
  // Exported generics close over private Moss helpers.  Preserve those
  // helpers as semantic IR (not source text and not Rust MIR) so an importer
  // can reconstruct the same Moss callable graph after the source tree is
  // absent.  Keeping the closure private is enforced by the dependency
  // record kind and by not adding these names to the module export set.
  for (const auto& function : program.functions) {
    if (function.exported || function.name.empty()) continue;
    string module = module_name_from_symbol(function.name);
    if (module.empty() || program.external_modules.count(module)) continue;
    bool reachable = std::any_of(
        program.functions.begin(), program.functions.end(),
        [](const Function& exported) { return exported.exported && exported.generic; });
    if (!reachable) continue;
    auto& out = contents[module];
    out << "  generic_dependency_begin "
        << std::quoted(function.name.substr(module.size() + 2))
        << " kind=" << (function.generic ? "generic" : "concrete")
        << " return=" << function.return_type.value_or("unit")
        << " effects=" << interface_effects_text(function.observable_effects)
        << "\n";
    for (size_t index = 0; index < function.params.size(); ++index)
      out << "  generic_param " << index << " "
          << std::quoted(function.params[index].name) << " "
          << std::quoted(function.params[index].type) << "\n";
    for (const auto& constraint : function.constraints)
      out << "  generic_constraint " << static_cast<int>(constraint.kind)
          << " " << std::quoted(constraint.subject) << " "
          << std::quoted(constraint.detail) << " "
          << std::quoted(constraint.result) << "\n";
    for (const auto& statement : function.body) {
      out << "  generic_stmt " << static_cast<int>(statement.kind) << " "
          << statement.line << " " << statement.indent << " "
          << std::quoted(statement.text) << " " << std::quoted(statement.a)
          << " " << std::quoted(statement.b)
          << " " << std::quoted(statement.semantic_type) << " "
          << statement.args.size();
      for (const auto& argument : statement.args) out << " " << std::quoted(argument);
      out << "\n";
    }
    if (function.result_expression)
      out << "  generic_result " << function.result_line << " "
          << std::quoted(*function.result_expression) << "\n";
    out << "  generic_dependency_end\n";
  }
  for (const auto& object : program.objects) {
    if (!object.exported) continue;
    string module = module_name_from_symbol(object.name);
    if (module.empty() || program.external_modules.count(module)) continue;
    auto& out = contents[module];
    interface_contents[module] << "type " << object.name.substr(module.size() + 2)
                               << " nominal\n";
    out << "export type " << object.name.substr(module.size() + 2) << " nominal\n";
    for (const auto& field : object.fields)
      out << "  public_representation field " << field.name << " " << field.type << "\n";
  }
  for (const auto& trait : program.traits) {
    if (!trait.exported) continue;
    string module = module_name_from_symbol(trait.name);
    if (module.empty() || program.external_modules.count(module)) continue;
    contents[module] << "export trait " << trait.name.substr(module.size() + 2) << "\n";
  }
  for (const auto& domain : program.domains) {
    if (!domain.exported) continue;
    string module = module_name_from_symbol(domain.name);
    if (module.empty() || program.external_modules.count(module)) continue;
    auto& out = contents[module];
    interface_contents[module] << "domain " << domain.name.substr(module.size() + 2)
                               << " opaque\n";
    out << "export domain " << domain.name.substr(module.size() + 2) << " opaque\n";
    for (const auto& field : domain.state) {
      std::ostringstream member;
      member << "  construction_state " << std::quoted(field.name) << " "
             << std::quoted(field.type) << " " << std::quoted(field.init) << "\n";
      interface_contents[module] << member.str();
      out << member.str();
    }
    for (const auto& route : domain.routes) {
      interface_contents[module] << "  route " << route.name << ": " << route.type << "\n";
      out << "  route " << route.name << ": " << route.type << "\n";
    }
    for (const auto& handler : domain.handlers) {
      out << "  handler " << handler.name << " reply="
          << handler.reply_type.value_or("unit") << " effects="
          << interface_effects_text(handler.observable_effects) << "\n";
      synchronization_require(handler.state_effects.has_value(), "missing exported handler effects");
      std::ostringstream semantic_effects;
      semantic_effects << "  handler_state_effects " << std::quoted(handler.name);
      for (const auto* leaves : {&handler.state_effects->reads, &handler.state_effects->writes,
                                 &handler.state_effects->consumes}) {
        semantic_effects << " " << leaves->size();
        for (const auto& leaf : *leaves) semantic_effects << " " << std::quoted(leaf);
      }
      semantic_effects << "\n";
      out << semantic_effects.str();
      interface_contents[module] << semantic_effects.str();
      for (const auto& parameter : handler.params) {
        std::ostringstream slot;
        slot << "  handler_param " << std::quoted(handler.name) << " "
             << std::quoted(parameter.name) << " " << std::quoted(parameter.type) << "\n";
        interface_contents[module] << slot.str();
        out << slot.str();
      }

    }
  }
  vector<std::filesystem::path> result;
  for (auto& entry : contents) {
    std::ostringstream header;
    header << "moss-module-interface-v1\n"
           << "module_id " << manifest.name << "::" << entry.first << "\n"
           << "compiler " << kCompilerVersion << "\n"
           << "native_abi 5\n"
           << "backend_fingerprint " << backend.fingerprint << "\n"
           << "backend_rustc " << backend.version_verbose << "\n"
           << "concrete_interface_hash "
           << stable_hash(interface_contents[entry.first].str()) << "\n"
           << "generic_semantic_hash "
           << stable_hash(entry.second.str()) << "\n"
           << "interface_hash "
           << stable_hash(interface_contents[entry.first].str()) << "\n"
           << "imports\n";
    for (const auto& import : program.imports)
      if (import.owner_module == entry.first)
        header << "  " << import.name << "\n";
    header << "contract\n" << interface_contents[entry.first].str()
           << "semantic_exports\n" << entry.second.str();
    std::filesystem::path file = directory / (tooling_name(entry.first) + ".mossi");
    std::ifstream prior(file, std::ios::binary);
    std::ostringstream prior_text;
    if (prior) prior_text << prior.rdbuf();
    string text = header.str();
    if (!prior || prior_text.str() != text) {
      std::ofstream output(file, std::ios::binary);
      if (!output)
        throw ProjectError("MOSS_INTERFACE_ERROR",
                           "cannot write Moss module interface '" + file.string() + "'",
                           file.string());
      output << text;
    }
    result.push_back(file);
  }
  return result;
}

struct NativeArtifact {
  std::filesystem::path rust;
  std::filesystem::path debug_map;
  std::filesystem::path executable;
  std::filesystem::path cache_metadata;
  vector<std::filesystem::path> module_interfaces;
  vector<std::filesystem::path> module_rlibs;
  std::filesystem::path specialization_rlib;
  BackendToolchainIdentity backend_toolchain;
  vector<TestDecl> tests;
  vector<BenchDecl> benchmarks;
  bool reused = false;
  bool semantic_analysis_reused = false;
  bool generated_rust_rewritten = false;
};

static string artifact_stem(const ProjectManifest& manifest,
                            const std::filesystem::path& source) {
  string relative = project_relative_path(manifest, source);
  string readable = tooling_name(source.stem().string());
  return readable + "_" + stable_hash(relative).substr(0, 10);
}

static NativeArtifact compile_native_artifact(
    const ProjectManifest& manifest,
    const vector<std::filesystem::path>& sources,
    const std::filesystem::path& directory, bool optimized,
    bool debug_build, ProgramGenerationMode mode,
    const std::optional<string>& name_override = std::nullopt,
    const string& declaration_filter = {},
    const std::set<string>* declaration_ids = nullptr) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error)
    throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                       "cannot create artifact directory '" +
                           directory.string() + "': " + error.message());
  if (sources.empty())
    throw ProjectError("PROJECT_SOURCE_NOT_FOUND",
                       "project target contains no Moss source files");
  const std::filesystem::path& primary_source = sources.front();
  string stem = name_override.value_or(artifact_stem(manifest, primary_source));
  NativeArtifact artifact;
  artifact.rust = directory / (stem + ".rs");
  artifact.debug_map = directory / (stem + ".mossmap");
  artifact.executable = directory / stem;
  artifact.cache_metadata = directory / (stem + ".mossbuild");
  {
    std::error_code interface_error;
    if (std::filesystem::is_directory(directory, interface_error)) {
      for (const auto& entry : std::filesystem::directory_iterator(directory, interface_error))
        if (!interface_error && entry.is_regular_file() &&
            entry.path().extension() == ".mossi")
          artifact.module_interfaces.push_back(entry.path());
      std::sort(artifact.module_interfaces.begin(), artifact.module_interfaces.end());
      for (const auto& interface_file : artifact.module_interfaces) {
        std::filesystem::path rlib = interface_file;
        rlib.replace_extension(".rlib");
        if (!std::filesystem::is_regular_file(rlib, interface_error)) {
          rlib = interface_file.parent_path() /
              ("lib" + interface_file.stem().string() + ".rlib");
        }
        if (std::filesystem::is_regular_file(rlib, interface_error))
          artifact.module_rlibs.push_back(std::move(rlib));
      }
    }
  }
  artifact.backend_toolchain = inspect_backend_toolchain(
      optimized, debug_build, mode);
  std::ostringstream source_key;
  source_key << "sources\n";
  for (const auto& source : sources)
    source_key << project_relative_path(manifest, source) << "\n"
               << stable_hash(read_text_file(source, "PROJECT_SOURCE_NOT_FOUND"))
               << "\n";
  // Compiled providers are semantic inputs too.  Their .mossi contract (and
  // backend fingerprint below) must invalidate a consumer even when the
  // consumer's Moss source is unchanged.
  for (const auto& interface_file : artifact.module_interfaces) {
    std::ifstream interface_input(interface_file, std::ios::binary);
    std::ostringstream interface_text;
    if (interface_input) interface_text << interface_input.rdbuf();
    source_key << "provider-interface " << interface_file.string() << "\n"
               << stable_hash(interface_text.str()) << "\n";
    std::filesystem::path provider_rlib = interface_file;
    provider_rlib.replace_extension(".rlib");
    if (!std::filesystem::is_regular_file(provider_rlib))
      provider_rlib = interface_file.parent_path() /
          ("lib" + interface_file.stem().string() + ".rlib");
    if (std::filesystem::is_regular_file(provider_rlib)) {
      std::ifstream rlib_input(provider_rlib, std::ios::binary);
      std::ostringstream rlib_text;
      rlib_text << rlib_input.rdbuf();
      source_key << "provider-rlib " << provider_rlib.string() << "\n"
                 << stable_hash(rlib_text.str()) << "\n";
    }
  }
  source_key << "compiler\n" << kCompilerVersion << "\nmode\n"
             << static_cast<int>(mode) << "\nfilter\n" << declaration_filter
             << "\n";
  if (declaration_ids)
    for (const auto& identity : *declaration_ids)
      source_key << identity << "\n";
  string source_fingerprint = stable_hash(source_key.str());
  string cache_text = backend_cache_metadata(
      artifact.backend_toolchain, source_fingerprint);
  std::ifstream early_cache(artifact.cache_metadata, std::ios::binary);
  std::ostringstream early_cache_stream;
  if (early_cache) early_cache_stream << early_cache.rdbuf();
  if (mode == ProgramGenerationMode::Application && early_cache &&
      early_cache_stream.str() == cache_text &&
      std::filesystem::is_regular_file(artifact.rust) &&
      std::filesystem::is_regular_file(artifact.debug_map) &&
      std::filesystem::is_regular_file(artifact.executable) &&
      artifact.module_rlibs.size() == artifact.module_interfaces.size()) {
    artifact.reused = true;
    artifact.semantic_analysis_reused = true;
    return artifact;
  }

  CompiledProjectUnit unit = analyze_project_sources(
      manifest, sources, optimized, debug_build, mode, declaration_filter,
      declaration_ids);
  vector<std::filesystem::path> preexisting_interfaces = artifact.module_interfaces;
  vector<string> local_module_names = module_names_for_program(unit.program);
  artifact.module_interfaces = write_module_interfaces(
      manifest, unit.program, directory, artifact.backend_toolchain);
  for (const auto& interface_file : preexisting_interfaces) {
    string module = interface_file.stem().string();
    if (std::find(local_module_names.begin(), local_module_names.end(), module) ==
            local_module_names.end() &&
        std::find(artifact.module_interfaces.begin(), artifact.module_interfaces.end(),
                  interface_file) == artifact.module_interfaces.end())
      artifact.module_interfaces.push_back(interface_file);
  }
  std::sort(artifact.module_interfaces.begin(), artifact.module_interfaces.end());
  artifact.tests = unit.program.tests;
  artifact.benchmarks = unit.program.benchmarks;
  auto update_file = [](const std::filesystem::path& path,
                        const string& content) {
    std::ifstream prior(path, std::ios::binary);
    if (prior) {
      std::ostringstream buffer;
      buffer << prior.rdbuf();
      if (buffer.str() == content) return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output)
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "cannot write generated artifact '" +
                             path.string() + "'");
    output << content;
    return true;
  };

  // Legacy projects retain the historical single Rust unit.  Explicit
  // modules use the same semantic result, projected into one Rust crate per
  // module.  The projection is deliberately after checking so no second Moss
  // type/effect/ownership implementation can drift from the authoritative
  // analysis.
  bool rust_changed = false;
  std::map<string,std::filesystem::path> module_rust;
  std::map<string,std::filesystem::path> module_rlib;
  vector<string> module_order;
  string root_module;
  bool has_specializations = false;
  if (unit.program.explicit_module) {
    module_order = module_topological_order(unit.program);
    root_module = unit.program.main_module.empty()
        ? (module_order.empty() ? string() : module_order.back())
        : unit.program.main_module;
    has_specializations = std::any_of(
        unit.program.functions.begin(), unit.program.functions.end(),
        [](const Function& function) {
          return function.static_dispatch && !function.specializations.empty();
        });
    std::filesystem::path specialization_rust =
        directory / "moss-specializations.rs";
    artifact.specialization_rlib = directory / "libmoss_specializations.rlib";
    if (has_specializations) {
      Program specialization_program;
      specialization_program.explicit_module = true;
      specialization_program.functions.reserve(unit.program.functions.size());
      bool has_exported_generic = std::any_of(
          unit.program.functions.begin(), unit.program.functions.end(),
          [](const Function& function) {
            return function.exported && function.generic;
          });
      for (const auto& function : unit.program.functions)
        if (function.static_dispatch && !function.specializations.empty())
          specialization_program.functions.push_back(function);
      // Concrete private helpers reachable from a deferred generic are
      // compiler-generated internal support in the specialization crate.  A
      // private helper never becomes a Moss export or an imported source
      // symbol, but its Moss body must remain available after source removal.
      if (has_exported_generic)
        {
          std::set<string> support;
          bool changed = true;
          while (changed) {
            changed = false;
            for (const auto& edge : unit.program.semantic_call_edges) {
              string source_name = starts_with(edge.source, "fn:")
                  ? edge.source.substr(3) : string();
              auto source = std::find_if(
                  unit.program.functions.begin(), unit.program.functions.end(),
                  [&](const Function& function) {
                    return function.name == source_name;
                  });
              if (source == unit.program.functions.end() ||
                  !(source->generic || support.count(source->name))) continue;
              for (const auto& function : unit.program.functions)
                if (!function.exported &&
                    edge.target.find(function.name) != string::npos &&
                    support.insert(function.name).second)
                  changed = true;
            }
          }
          for (const auto& function : unit.program.functions)
            if (!function.exported && !function.static_dispatch &&
                support.count(function.name))
              specialization_program.functions.push_back(function);
        }
      specialization_program.functional_pipelines = unit.program.functional_pipelines;
      string generated = Generator(
          specialization_program, unit.plan, debug_build, mode, {},
          &unit.program, true, true).generate();
      rust_changed = update_file(specialization_rust, generated) || rust_changed;
      vector<string> specialization_command = {
          artifact.backend_toolchain.rustc_executable};
      specialization_command.insert(
          specialization_command.end(), artifact.backend_toolchain.compile_flags.begin(),
          artifact.backend_toolchain.compile_flags.end());
      specialization_command.push_back("--crate-type=rlib");
      specialization_command.push_back("--crate-name");
      specialization_command.push_back("moss_specializations");
      specialization_command.push_back(specialization_rust.string());
      specialization_command.push_back("-o");
      specialization_command.push_back(artifact.specialization_rlib.string());
      ProcessResult specialization_compiled = run_process(specialization_command);
      if (specialization_compiled.exit_code != 0)
        throw ProjectError(
            "BUILD_BACKEND_ERROR",
            "Moss specialization crate compilation failed\n" +
                trim(specialization_compiled.output), specialization_rust.string());
    }
    for (const auto& module : module_order) {
      std::filesystem::path rust_file = directory / (tooling_name(module) + ".rs");
      module_rust[module] = rust_file;
      // rustc requires an rlib passed through --extern to use the conventional
      // lib<crate>.rlib filename.  The Moss interface remains the stable
      // module-named contract; this is ordinary Rust crate layout.
      module_rlib[module] = directory /
          ("lib" + tooling_name(module) + ".rlib");
      Program projected = module_program(unit.program, module);
      OptimizationPlan projected_plan = module_plan(unit.plan, projected);
      vector<string> dependencies = module_dependencies(unit.program, module);
      // The final/root crate owns the graph-wide specialization crate.  A
      // dependency module is linked to it only when it actually needs to call
      // a specialization; keeping ordinary module rlibs independent makes a
      // generic provider consumable from just its .mossi + concrete rlib.
      if (has_specializations && module == root_module)
        dependencies.push_back("__moss_specializations__");
      string generated = Generator(projected, projected_plan, debug_build,
                                   mode, dependencies, &unit.program, false).generate();
      rust_changed = update_file(rust_file, generated) || rust_changed;
      if (module == root_module) artifact.rust = rust_file;
    }
    for (const auto& external : unit.program.external_modules) {
      // Pair native code with the exact .mossi interface selected during
      // semantic import resolution. Never re-scan by filename here: another
      // package may legitimately publish a different artifact with the same
      // module spelling.
      auto selected = unit.external_module_interfaces.find(external);
      if (selected == unit.external_module_interfaces.end())
        throw ProjectError("MODULE_PROVIDER_NOT_FOUND",
                           "no selected provider for imported module '" + external + "'");
      const auto& interface_file = selected->second;
      std::filesystem::path rlib = interface_file;
      rlib.replace_extension(".rlib");
      if (!std::filesystem::is_regular_file(rlib))
        rlib = interface_file.parent_path() /
            ("lib" + interface_file.stem().string() + ".rlib");
      if (std::filesystem::is_regular_file(rlib)) {
        module_rlib[external] = rlib;
        // Source-free providers need the same transitive rustc discovery
        // alias as locally compiled modules; it is not provider ABI data.
        auto alias = directory / ("lib" + tooling_name("moss_" + external) + ".rlib");
        if (alias.lexically_normal() != rlib.lexically_normal())
          std::filesystem::copy_file(rlib, alias, std::filesystem::copy_options::overwrite_existing);
      }
    }
  } else {
    rust_changed = update_file(artifact.rust, unit.rust);
    root_module = manifest.name;
  }
  artifact.generated_rust_rewritten = rust_changed;

  // Compile the Rust crates in the already validated Moss import order.  A
  // consumer receives only dependency rlibs and public Rust declarations; it
  // never receives or regenerates a dependency's Moss implementation.
  artifact.module_rlibs.clear();
  if (unit.program.explicit_module) {
    for (const auto& module : module_order) {
      std::filesystem::path rlib = module_rlib.at(module);
      vector<string> module_command = {artifact.backend_toolchain.rustc_executable};
      module_command.insert(module_command.end(),
                            artifact.backend_toolchain.compile_flags.begin(),
                            artifact.backend_toolchain.compile_flags.end());
      module_command.push_back("--crate-type=rlib");
      module_command.push_back("--crate-name");
      module_command.push_back(tooling_name("moss_" + module));
      module_command.push_back(module_rust.at(module).string());
      module_command.push_back("-o");
      module_command.push_back(rlib.string());
      module_command.push_back("-L");
      module_command.push_back("dependency=" + directory.string());
      for (const auto& dependency : module_dependencies(unit.program, module)) {
        module_command.push_back("--extern");
        module_command.push_back(tooling_name("moss_" + dependency) + "=" +
                                 module_rlib.at(dependency).string());
      }
      if (has_specializations && module == root_module) {
        module_command.push_back("--extern");
        module_command.push_back("moss_specializations=" +
                                 artifact.specialization_rlib.string());
      }
      ProcessResult module_compiled = run_process(module_command);
      if (module_compiled.exit_code != 0)
        throw ProjectError(
            "BUILD_BACKEND_ERROR",
            "module Rust crate compilation failed for '" + module + "'\n" +
                trim(module_compiled.output),
            module_rust.at(module).string());
      // Keep Moss's provider filename and publish rustc's crate-name alias
      // for transitive -L resolution. --extern accepts libC.rlib directly;
      // indirect dependencies are discovered using libmoss_C.rlib.
      std::filesystem::copy_file(rlib,
          directory / ("lib" + tooling_name("moss_" + module) + ".rlib"),
          std::filesystem::copy_options::overwrite_existing);
      artifact.module_rlibs.push_back(std::move(rlib));
    }
  }

  std::ostringstream map_stream;
  string root_rust = unit.program.explicit_module
      ? [&]() {
          std::ifstream input(artifact.rust, std::ios::binary);
          std::ostringstream text;
          text << input.rdbuf();
          return text.str();
        }()
      : unit.rust;
  write_debug_map(
      map_stream,
      build_debug_map(
          unit.program, root_rust,
          std::filesystem::absolute(primary_source).lexically_normal().string(),
          std::filesystem::absolute(artifact.rust).lexically_normal().string(),
          std::filesystem::absolute(artifact.executable)
              .lexically_normal()
              .string(),
          debug_build, optimized));
  string map_text = map_stream.str();

  // The final executable is the root Moss module and links its dependency
  // rlibs through ordinary rustc --extern options.
  if (unit.program.explicit_module) {
    std::filesystem::remove(artifact.executable, error);
    if (error)
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "cannot replace native artifact '" +
                             artifact.executable.string() + "': " + error.message());
    vector<string> command = {artifact.backend_toolchain.rustc_executable};
    command.insert(command.end(), artifact.backend_toolchain.compile_flags.begin(),
                   artifact.backend_toolchain.compile_flags.end());
    command.push_back(artifact.rust.string());
    command.push_back("-o");
    command.push_back(artifact.executable.string());
    command.push_back("-L");
    command.push_back("dependency=" + directory.string());
    for (const auto& dependency : module_dependencies(unit.program, root_module)) {
      command.push_back("--extern");
      command.push_back(tooling_name("moss_" + dependency) + "=" +
                        module_rlib.at(dependency).string());
    }
    if (std::filesystem::is_regular_file(artifact.specialization_rlib)) {
      command.push_back("--extern");
      command.push_back("moss_specializations=" +
                        artifact.specialization_rlib.string());
    }
    ProcessResult compiled = run_process(command);
    if (compiled.exit_code != 0)
      throw ProjectError("BUILD_BACKEND_ERROR",
                         "native compilation failed for '" +
                             project_relative_path(manifest, primary_source) +
                             "'\n" + trim(compiled.output), primary_source.string());
  }
  bool map_changed = update_file(artifact.debug_map, map_text);
  std::ifstream prior_cache(artifact.cache_metadata, std::ios::binary);
  std::ostringstream prior_cache_stream;
  if (prior_cache) prior_cache_stream << prior_cache.rdbuf();
  bool backend_matches = prior_cache && prior_cache_stream.str() == cache_text;
  if (!rust_changed && !map_changed && backend_matches &&
      std::filesystem::is_regular_file(artifact.executable)) {
    artifact.reused = true;
    return artifact;
  }

  if (!unit.program.explicit_module) {
    std::filesystem::remove(artifact.executable, error);
    if (error)
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "cannot replace native artifact '" +
                             artifact.executable.string() + "': " +
                             error.message());

    vector<string> command = {artifact.backend_toolchain.rustc_executable};
    command.insert(command.end(), artifact.backend_toolchain.compile_flags.begin(),
                   artifact.backend_toolchain.compile_flags.end());
    command.push_back(artifact.rust.string());
    command.push_back("-o");
    command.push_back(artifact.executable.string());
    ProcessResult compiled = run_process(command);
    if (compiled.exit_code != 0)
      throw ProjectError(
          "BUILD_BACKEND_ERROR",
          "native compilation failed for '" +
              project_relative_path(manifest, primary_source) + "'\n" +
              trim(compiled.output),
          primary_source.string());
  }
  (void)update_file(artifact.cache_metadata, cache_text);
  return artifact;
}

static vector<std::filesystem::path> project_declaration_sources(
    const ProjectManifest& manifest, const string& directory_name) {
  vector<std::filesystem::path> sources = project_source_files(manifest);
  std::filesystem::path directory = manifest.root / directory_name;
  const string error_code = directory_name == "tests"
      ? "TEST_DISCOVERY_ERROR" : "BENCHMARK_CONFIGURATION_ERROR";
  vector<std::filesystem::path> declarations =
      moss_files_under(directory, error_code);
  sources.insert(sources.end(), declarations.begin(), declarations.end());
  std::sort(sources.begin(), sources.end());
  sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
  return sources;
}

// The semantic oracle and semantic edits use the same logical source universe
// as project build/test/bench.  Physical paths remain selectors and are never
// replaced by a synthetic concatenated source file.
struct SourceCompilationContext {
  bool project = false;
  ProjectManifest manifest;
  std::filesystem::path requested_source;
  vector<std::filesystem::path> sources;
  ProgramGenerationMode mode = ProgramGenerationMode::Application;
};

static bool path_within(const std::filesystem::path& path,
                        const std::filesystem::path& root) {
  std::error_code error;
  auto relative = std::filesystem::relative(path, root, error);
  if (error || relative.empty()) return false;
  auto text = relative.generic_string();
  return text != ".." && !starts_with(text, "../");
}

static SourceCompilationContext analyze_source_context(
    const std::filesystem::path& requested) {
  SourceCompilationContext context;
  context.requested_source = std::filesystem::absolute(requested)
                                 .lexically_normal();
  auto root = find_project_root(context.requested_source);
  if (!root) {
    context.sources = {context.requested_source};
    return context;
  }
  context.manifest = load_project_manifest(*root);
  auto source_root = context.manifest.root / context.manifest.source;
  auto tests_root = context.manifest.root / "tests";
  auto benches_root = context.manifest.root / "benches";
  if (path_within(context.requested_source, tests_root)) {
    context.project = true;
    context.mode = ProgramGenerationMode::Tests;
    context.sources = project_declaration_sources(context.manifest, "tests");
  } else if (path_within(context.requested_source, benches_root)) {
    context.project = true;
    context.mode = ProgramGenerationMode::Benchmarks;
    context.sources = project_declaration_sources(context.manifest, "benches");
  } else if (path_within(context.requested_source, source_root)) {
    context.project = true;
    context.mode = ProgramGenerationMode::Application;
    context.sources = project_source_files(context.manifest);
  } else {
    // A file near a project but outside a participating target remains a
    // standalone source, matching the pre-project query/edit behavior.
    context.sources = {context.requested_source};
  }
  return context;
}

// Fast Debug uses the same project source universe as the native compiler,
// but explicit-module projects can narrow that universe to the transitive
// source-module closure of the requested entry module.  This is a loading
// decision only: the selected files still pass through analyze_project_sources
// and therefore the ordinary Moss parser/checker remains authoritative.
static vector<std::filesystem::path> fast_debug_source_closure(
    const SourceCompilationContext& context) {
  if (!context.project || context.mode != ProgramGenerationMode::Application)
    return context.sources;

  struct ModuleSources {
    vector<std::filesystem::path> files;
    vector<string> imports;
  };
  std::map<string, ModuleSources> modules;
  string entry_module;
  bool has_explicit_modules = false;
  for (const auto& source : context.sources) {
    std::ifstream input(source);
    if (!input)
      throw ProjectError("PROJECT_SOURCE_NOT_FOUND",
                         "cannot read Moss source '" + source.string() + "'",
                         source.string());
    Program parsed = Parser(lex_lines(input, source.string())).parse();
    string module = parsed.explicit_module && !parsed.module_name.empty()
        ? parsed.module_name : context.manifest.name;
    auto& record = modules[module];
    record.files.push_back(source);
    for (const auto& import : parsed.imports)
      record.imports.push_back(import.name);
    has_explicit_modules = has_explicit_modules || parsed.explicit_module;
    if (source == context.requested_source) entry_module = module;
    if (entry_module.empty() && parsed.main) entry_module = module;
  }
  if (!has_explicit_modules || entry_module.empty()) return context.sources;

  std::set<string> reachable;
  std::function<void(const string&)> visit = [&](const string& module) {
    if (!reachable.insert(module).second) return;
    auto found = modules.find(module);
    if (found == modules.end()) return;  // analyze_project_sources will
                                         // validate an external interface.
    for (const auto& imported : found->second.imports) visit(imported);
  };
  visit(entry_module);

  vector<std::filesystem::path> result;
  for (const auto& entry : modules)
    if (reachable.count(entry.first))
      result.insert(result.end(), entry.second.files.begin(),
                    entry.second.files.end());
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result.empty() ? context.sources : result;
}

static int hex_digit_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static string decode_protocol_hex(const string& value) {
  if (value.size() % 2 != 0)
    throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                       "malformed generated harness record");
  string result;
  result.reserve(value.size() / 2);
  for (size_t index = 0; index < value.size(); index += 2) {
    int high = hex_digit_value(value[index]);
    int low = hex_digit_value(value[index + 1]);
    if (high < 0 || low < 0)
      throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                         "malformed generated harness record");
    result.push_back(static_cast<char>((high << 4) | low));
  }
  return result;
}

static void write_project_error_json(std::ostream& out,
                                     const string& command,
                                     const ProjectError& error) {
  write_structured_error(out, command, error.code, error.what(),
                         error.source_file, error.line);
}

static int run_project_build(const ProjectManifest& manifest, bool release,
                             bool json) {
  vector<std::filesystem::path> sources = project_source_files(manifest);
  std::filesystem::path source = sources.front();
  string profile = release ? "release" : "debug";
  NativeArtifact artifact = compile_native_artifact(
      manifest, sources, manifest.root / "build" / profile, release,
      !release, ProgramGenerationMode::Application,
      tooling_name(manifest.name));
  auto previous_snapshot = read_semantic_snapshot(manifest, sources);
  string current_source_hash = source_set_content_hash(manifest, sources);
  size_t units_analyzed = 0;
  size_t units_reused = 0;
  if (previous_snapshot &&
      previous_snapshot->source_hash == current_source_hash) {
    units_reused = previous_snapshot->units.size();
  } else {
    SemanticSnapshot current_snapshot = analyze_project_snapshot(
        manifest, sources);
    units_analyzed = current_snapshot.units.size();
    if (previous_snapshot) {
      std::unordered_map<string,const SemanticSnapshotUnit*> prior;
      for (const auto& unit : previous_snapshot->units)
        prior[unit.durable_identity] = &unit;
      for (const auto& unit : current_snapshot.units) {
        auto old = prior.find(unit.durable_identity);
        if (old != prior.end() &&
            old->second->implementation_hash == unit.implementation_hash &&
            old->second->interface_hash == unit.interface_hash)
          ++units_reused;
      }
    }
    write_semantic_snapshot(manifest, sources, current_snapshot);
  }
  if (json) {
    write_agent_envelope_begin(std::cout, "build", true);
    std::cout << "  \"result\": {\"project\": ";
    write_debug_json_string(std::cout, manifest.name);
    std::cout << ", \"profile\": ";
    write_debug_json_string(std::cout, profile);
    std::cout << ", \"source\": ";
    write_debug_json_string(std::cout, source.string());
    std::cout << ", \"sources\": [";
    for (size_t index = 0; index < sources.size(); ++index) {
      if (index) std::cout << ", ";
      write_debug_json_string(std::cout, sources[index].string());
    }
    std::cout << "], \"artifacts\": {\"executable\": ";
    write_debug_json_string(std::cout, artifact.executable.string());
    std::cout << ", \"generated_rust\": ";
    write_debug_json_string(std::cout, artifact.rust.string());
    std::cout << ", \"debug_map\": ";
    write_debug_json_string(std::cout, artifact.debug_map.string());
    std::cout << ", \"module_interfaces\": [";
    for (size_t index = 0; index < artifact.module_interfaces.size(); ++index) {
      if (index) std::cout << ", ";
      write_debug_json_string(std::cout, artifact.module_interfaces[index].string());
    }
    std::cout << "], \"module_rlibs\": [";
    for (size_t index = 0; index < artifact.module_rlibs.size(); ++index) {
      if (index) std::cout << ", ";
      write_debug_json_string(std::cout, artifact.module_rlibs[index].string());
    }
    std::cout << "], \"specialization_rlib\": ";
    if (artifact.specialization_rlib.empty()) std::cout << "null";
    else write_debug_json_string(std::cout, artifact.specialization_rlib.string());
    std::cout << ", \"cache_metadata\": ";
    write_debug_json_string(std::cout, artifact.cache_metadata.string());
    std::cout << "}, \"backend_toolchain\": ";
    write_backend_toolchain_json(std::cout, artifact.backend_toolchain);
    std::cout << ", \"reused\": "
              << (artifact.reused ? "true" : "false")
              << ", \"incremental\": {\"units_analyzed\": "
              << units_analyzed << ", \"units_reused\": " << units_reused
              << ", \"generated_rust_units_rewritten\": "
              << (artifact.generated_rust_rewritten ? 1 : 0)
              << ", \"native_artifact_reused\": "
              << (artifact.reused ? "true" : "false") << "}}\n}\n";
  } else {
    std::cout << "Built " << manifest.name << " (" << profile << ")\n"
              << "  " << artifact.executable.string() << "\n";
  }
  return 0;
}

struct ProjectTestResult {
  string id;
  string name;
  string source_file;
  int line = 0;
  bool passed = false;
  string detail;
  string expression;
  std::optional<string> actual;
  std::optional<string> expected;
};

struct AffectedTestChoice {
  string id;
  string name;
  string source_file;
  bool selected = false;
  vector<string> reasons;
  vector<string> dependency_path;
};

struct AffectedTestPlan {
  std::map<string,std::set<string>> selected_by_source;
  vector<AffectedTestChoice> choices;
  std::map<string,SemanticSnapshot> current_snapshots;
  bool conservative_fallback = false;
};

static vector<string> dependency_path_to(
    const SemanticSnapshot& snapshot, const string& changed,
    const string& destination) {
  std::unordered_map<string,vector<string>> reverse;
  for (const auto& unit : snapshot.units)
    for (const auto& dependency : unit.dependencies)
      reverse[dependency].push_back(unit.durable_identity);
  std::set<string> visited{changed};
  std::map<string,string> parent;
  vector<string> pending{changed};
  for (size_t index = 0; index < pending.size(); ++index) {
    const string current = pending[index];
    auto next = reverse.find(current);
    if (next == reverse.end()) continue;
    std::sort(next->second.begin(), next->second.end());
    for (const auto& dependent : next->second) {
      if (!visited.insert(dependent).second) continue;
      parent[dependent] = current;
      if (dependent == destination) {
        vector<string> path{destination};
        while (path.back() != changed) path.push_back(parent[path.back()]);
        std::reverse(path.begin(), path.end());
        return path;
      }
      pending.push_back(dependent);
    }
  }
  return {};
}

static AffectedTestPlan select_affected_tests(
    const ProjectManifest& manifest, const string& filter) {
  AffectedTestPlan plan;
  auto sources = project_declaration_sources(manifest, "tests");
  size_t discovered = 0;
  {
    SemanticSnapshot current = analyze_project_snapshot(manifest, sources);
    auto previous = read_semantic_snapshot(manifest, sources);
    string source_key = semantic_snapshot_file(manifest, sources).string();
    plan.current_snapshots[source_key] = current;

    std::unordered_map<string,const SemanticSnapshotUnit*> current_units;
    std::unordered_map<string,const SemanticSnapshotUnit*> previous_units;
    for (const auto& unit : current.units)
      current_units[unit.durable_identity] = &unit;
    if (previous)
      for (const auto& unit : previous->units)
        previous_units[unit.durable_identity] = &unit;

    vector<string> changed;
    bool uncertain = !previous.has_value();
    if (previous) {
      for (const auto& unit : current.units) {
        auto prior = previous_units.find(unit.durable_identity);
        if (prior == previous_units.end() ||
            prior->second->implementation_hash != unit.implementation_hash ||
            prior->second->interface_hash != unit.interface_hash)
          changed.push_back(unit.durable_identity);
      }
      for (const auto& unit : previous->units)
        if (!current_units.count(unit.durable_identity)) {
          changed.push_back(unit.durable_identity);
          uncertain = true;
        }
      if (current.route_edges.size() != previous->route_edges.size())
        uncertain = true;
      else {
        for (size_t index = 0; index < current.route_edges.size(); ++index)
          if (current.route_edges[index].source_domain !=
                  previous->route_edges[index].source_domain ||
              current.route_edges[index].target_domain !=
                  previous->route_edges[index].target_domain ||
              current.route_edges[index].source_instance != previous->route_edges[index].source_instance ||
              current.route_edges[index].target_instance != previous->route_edges[index].target_instance)
            uncertain = true;
      }
    }

    std::set<string> selected;
    std::unordered_map<string,vector<string>> reasons;
    std::unordered_map<string,vector<string>> paths;
    if (uncertain) plan.conservative_fallback = true;
    for (const auto& unit : current.units) {
      if (unit.kind != "test") continue;
      ++discovered;
      string harness_id = unit.semantic_identity;
      bool matches_filter = filter.empty() ||
          unit.name.find(filter) != string::npos ||
          harness_id.find(filter) != string::npos;
      if (!matches_filter) continue;
      if (uncertain) {
        selected.insert(harness_id);
        reasons[harness_id].push_back(
            previous ? "conservative fallback: dependency shape changed"
                     : "conservative fallback: no semantic baseline");
        continue;
      }
      for (const auto& identity : changed) {
        if (identity == unit.durable_identity) {
          selected.insert(harness_id);
          reasons[harness_id].push_back("test implementation changed");
          paths[harness_id] = {identity};
          continue;
        }
        vector<string> path = dependency_path_to(
            current, identity, unit.durable_identity);
        if (path.empty() && previous)
          path = dependency_path_to(*previous, identity,
                                    unit.durable_identity);
        if (!path.empty()) {
          selected.insert(harness_id);
          reasons[harness_id].push_back(
              "dependency reaches changed semantic unit " + identity);
          if (paths[harness_id].empty()) paths[harness_id] = std::move(path);
        }
      }
    }
    plan.selected_by_source[source_key] = selected;
    for (const auto& unit : current.units) {
      if (unit.kind != "test") continue;
      AffectedTestChoice choice;
      choice.id = unit.semantic_identity;
      choice.name = unit.name;
      choice.source_file = source_key;
      choice.selected = selected.count(choice.id) != 0;
      if (choice.selected) {
        choice.reasons = reasons[choice.id];
        choice.dependency_path = paths[choice.id];
      } else if (!filter.empty() &&
                 unit.name.find(filter) == string::npos &&
                 unit.semantic_identity.find(filter) == string::npos) {
        choice.reasons.push_back("does not match test filter");
      } else {
        choice.reasons.push_back("semantic dependency cone is unchanged");
      }
      plan.choices.push_back(std::move(choice));
    }
  }
  if (discovered == 0)
    throw ProjectError("TEST_DISCOVERY_ERROR", "no Moss tests were found");
  std::sort(plan.choices.begin(), plan.choices.end(),
            [](const AffectedTestChoice& left,
               const AffectedTestChoice& right) {
              return left.id < right.id;
            });
  return plan;
}

static void parse_test_failure_detail(ProjectTestResult& result) {
  static const string prefix = "Moss assertion failed at line ";
  if (!starts_with(result.detail, prefix)) return;
  size_t line_begin = prefix.size();
  size_t line_end = line_begin;
  while (line_end < result.detail.size() &&
         std::isdigit(static_cast<unsigned char>(result.detail[line_end])))
    ++line_end;
  if (line_end == line_begin ||
      result.detail.compare(line_end, 2, ": ") != 0)
    return;
  result.line = std::stoi(result.detail.substr(
      line_begin, line_end - line_begin));
  size_t expression_begin = line_end + 2;
  size_t actual_marker = result.detail.find("; actual=", expression_begin);
  if (actual_marker == string::npos) {
    result.expression = result.detail.substr(expression_begin);
    return;
  }
  result.expression = result.detail.substr(
      expression_begin, actual_marker - expression_begin);
  size_t expected_marker = result.detail.rfind(", expected=");
  if (expected_marker == string::npos || expected_marker < actual_marker) return;
  size_t actual_begin = actual_marker + string("; actual=").size();
  result.actual = result.detail.substr(
      actual_begin, expected_marker - actual_begin);
  result.expected = result.detail.substr(
      expected_marker + string(", expected=").size());
}

static vector<ProjectTestResult> run_project_tests(
    const ProjectManifest& manifest, const string& filter,
    const AffectedTestPlan* affected = nullptr) {
  vector<ProjectTestResult> results;
  auto sources = project_declaration_sources(manifest, "tests");
  std::filesystem::path directory = manifest.root / "build" / "test";
  std::set<string> selected_ids;
  if (affected)
    for (const auto& entry : affected->selected_by_source)
      selected_ids.insert(entry.second.begin(), entry.second.end());
  if (affected && selected_ids.empty()) return results;
  NativeArtifact artifact = compile_native_artifact(
      manifest, sources, directory, false, true, ProgramGenerationMode::Tests,
      tooling_name(manifest.name) + "_tests", filter,
      affected ? &selected_ids : nullptr);
  if (artifact.tests.empty()) {
    if (affected) return results;
    throw ProjectError(
        "TEST_DISCOVERY_ERROR",
        filter.empty() ? "no Moss tests were found"
                       : "no Moss tests matched filter '" + filter + "'",
        sources.front().string());
  }
  std::unordered_map<string, const TestDecl*> declarations;
  for (const auto& test : artifact.tests)
    declarations[test.semantic_identity] = &test;
  ProcessResult process = run_process({artifact.executable.string()});
  std::istringstream lines(process.output);
  string line;
  size_t records = 0;
  while (std::getline(lines, line)) {
    if (!starts_with(line, "MOSS_TEST|")) continue;
    auto fields = debug_split_fields(line);
    if (fields.size() != 5 || (fields[1] != "PASS" && fields[1] != "FAIL"))
      throw ProjectError("TEST_RUNTIME_ERROR",
                         "test harness emitted a malformed result",
                         sources.front().string());
    ProjectTestResult result;
    result.id = decode_protocol_hex(fields[2]);
    result.name = decode_protocol_hex(fields[3]);
    result.passed = fields[1] == "PASS";
    result.detail = decode_protocol_hex(fields[4]);
    auto declaration = declarations.find(result.id);
    if (declaration == declarations.end())
      throw ProjectError("TEST_RUNTIME_ERROR",
                         "test harness returned an unknown test identity",
                         sources.front().string());
    result.source_file = declaration->second->source_file;
    result.line = declaration->second->line;
    if (!result.passed) parse_test_failure_detail(result);
    results.push_back(std::move(result));
    ++records;
  }
  if (records != artifact.tests.size())
    throw ProjectError(
        "TEST_RUNTIME_ERROR",
        "test process ended before reporting every discovered test" +
            (process.output.empty() ? string() : "\n" + trim(process.output)),
        sources.front().string());
  bool has_failure = std::any_of(
      results.end() - static_cast<std::ptrdiff_t>(records), results.end(),
      [](const ProjectTestResult& result) { return !result.passed; });
  if (process.exit_code != (has_failure ? 1 : 0))
    throw ProjectError(
        "TEST_RUNTIME_ERROR",
        "test executable exited unexpectedly with status " +
            std::to_string(process.exit_code),
        sources.front().string());
  if (results.empty() && !affected)
    throw ProjectError(
        "TEST_DISCOVERY_ERROR",
        filter.empty() ? "no Moss tests were found"
                       : "no Moss tests matched filter '" + filter + "'");
  std::sort(results.begin(), results.end(),
            [](const ProjectTestResult& left,
               const ProjectTestResult& right) { return left.id < right.id; });
  return results;
}

static int report_project_tests(const ProjectManifest& manifest,
                                const string& filter, bool json,
                                bool affected_only = false) {
  std::optional<AffectedTestPlan> affected;
  if (affected_only) affected = select_affected_tests(manifest, filter);
  vector<ProjectTestResult> results = run_project_tests(
      manifest, filter, affected ? &*affected : nullptr);
  size_t passed = static_cast<size_t>(std::count_if(
      results.begin(), results.end(),
      [](const ProjectTestResult& result) { return result.passed; }));
  size_t failed = results.size() - passed;
  if (json) {
    write_agent_envelope_begin(std::cout, "test", failed == 0);
    std::cout << "  \"result\": {\"project\": ";
    write_debug_json_string(std::cout, manifest.name);
    std::cout << ", \"filter\": ";
    if (filter.empty()) std::cout << "null";
    else write_debug_json_string(std::cout, filter);
    std::cout << ", \"affected_only\": "
              << (affected_only ? "true" : "false");
    std::cout << ", \"tests\": [";
    for (size_t index = 0; index < results.size(); ++index) {
      if (index) std::cout << ", ";
      const auto& result = results[index];
      std::cout << "{\"id\": ";
      write_debug_json_string(std::cout, result.id);
      std::cout << ", \"name\": ";
      write_debug_json_string(std::cout, result.name);
      std::cout << ", \"status\": \""
                << (result.passed ? "pass" : "fail")
                << "\", \"source_file\": ";
      write_debug_json_string(std::cout, result.source_file);
      std::cout << ", \"line\": " << result.line
                << ", \"diagnostic\": ";
      if (result.passed) std::cout << "null";
      else {
        std::cout << "{\"code\": \"TEST_ASSERTION_FAILED\", "
                     "\"severity\": \"error\", \"message\": ";
        write_debug_json_string(std::cout, result.detail);
        std::cout << ", \"details\": {\"expression\": ";
        if (result.expression.empty()) std::cout << "null";
        else write_debug_json_string(std::cout, result.expression);
        std::cout << ", \"actual\": ";
        if (result.actual) write_debug_json_string(std::cout, *result.actual);
        else std::cout << "null";
        std::cout << ", \"expected\": ";
        if (result.expected)
          write_debug_json_string(std::cout, *result.expected);
        else
          std::cout << "null";
        std::cout << "}, \"fixes\": [], \"legal_alternatives\": []}";
      }
      std::cout << "}";
    }
    std::cout << "], \"selection\": ";
    if (!affected) {
      std::cout << "null";
    } else {
      std::cout << "{\"conservative_fallback\": "
                << (affected->conservative_fallback ? "true" : "false")
                << ", \"selected\": [";
      bool first_selected = true;
      for (const auto& choice : affected->choices) {
        if (!choice.selected) continue;
        if (!first_selected) std::cout << ", ";
        first_selected = false;
        std::cout << "{\"id\": ";
        write_debug_json_string(std::cout, choice.id);
        std::cout << ", \"reasons\": ";
        write_agent_string_array(std::cout, choice.reasons);
        std::cout << ", \"dependency_path\": ";
        write_agent_string_array(std::cout, choice.dependency_path);
        std::cout << "}";
      }
      std::cout << "], \"skipped\": [";
      bool first_skipped = true;
      for (const auto& choice : affected->choices) {
        if (choice.selected) continue;
        if (!first_skipped) std::cout << ", ";
        first_skipped = false;
        std::cout << "{\"id\": ";
        write_debug_json_string(std::cout, choice.id);
        std::cout << ", \"reasons\": ";
        write_agent_string_array(std::cout, choice.reasons);
        std::cout << "}";
      }
      std::cout << "]}";
    }
    std::cout << ", \"summary\": {\"passed\": " << passed
              << ", \"failed\": " << failed << ", \"total\": "
              << results.size() << "}}\n}\n";
  } else {
    for (const auto& result : results) {
      string display = starts_with(result.id, "test:")
          ? result.id.substr(5) : result.id;
      std::cout << (result.passed ? "PASS " : "FAIL ") << display
                << "\n";
      if (!result.passed)
        std::cout << "  " << result.source_file << ":" << result.line
                  << ": " << result.detail << "\n";
    }
    std::cout << "\n" << passed << " passed\n" << failed << " failed\n";
  }
  if (failed == 0 && filter.empty()) {
    if (affected) {
      auto test_sources = project_declaration_sources(manifest, "tests");
      if (!affected->current_snapshots.empty())
        write_semantic_snapshot(manifest, test_sources,
                                affected->current_snapshots.begin()->second);
    } else {
      auto test_sources = project_declaration_sources(manifest, "tests");
      write_semantic_snapshot(manifest, test_sources,
                              analyze_project_snapshot(manifest, test_sources));
    }
  }
  return failed == 0 ? 0 : 1;
}

struct ProjectBenchmarkResult {
  string id;
  string name;
  string source_file;
  int line = 0;
  std::uint64_t median_ns = 0;
  std::uint64_t p25_ns = 0;
  std::uint64_t p75_ns = 0;
  size_t samples = 0;
  size_t warmup = 0;
  size_t iterations = 0;
  vector<std::uint64_t> sample_values;
};

struct ProjectBenchmarkRun {
  vector<ProjectBenchmarkResult> results;
  BackendToolchainIdentity backend_toolchain;
};

static std::uint64_t parse_unsigned_record_field(
    const string& value, const string& field,
    const std::filesystem::path& source) {
  try {
    size_t used = 0;
    unsigned long long parsed = std::stoull(value, &used);
    if (used != value.size()) throw std::invalid_argument("trailing data");
    return static_cast<std::uint64_t>(parsed);
  } catch (const std::exception&) {
    throw ProjectError(
        "BENCHMARK_RUNTIME_ERROR",
        "benchmark harness emitted an invalid " + field,
        source.string());
  }
}

static ProjectBenchmarkRun run_project_benchmarks(
    const ProjectManifest& manifest, const string& filter) {
  vector<ProjectBenchmarkResult> results;
  auto sources = project_declaration_sources(manifest, "benches");
  std::filesystem::path directory = manifest.root / "build" / "bench";
  NativeArtifact artifact = compile_native_artifact(
      manifest, sources, directory, true, false,
      ProgramGenerationMode::Benchmarks,
      tooling_name(manifest.name) + "_benches", filter);
  if (artifact.benchmarks.empty())
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        filter.empty() ? "no Moss benchmarks were found"
                       : "no Moss benchmarks matched filter '" + filter + "'");
  BackendToolchainIdentity backend_toolchain = artifact.backend_toolchain;
  std::unordered_map<string, const BenchDecl*> declarations;
  for (const auto& benchmark : artifact.benchmarks)
    declarations[benchmark.semantic_identity] = &benchmark;
  ProcessResult process = run_process({artifact.executable.string()});
  if (process.exit_code != 0)
    throw ProjectError(
        "BENCHMARK_RUNTIME_ERROR",
        "benchmark executable failed with status " +
            std::to_string(process.exit_code) +
            (process.output.empty() ? string() : "\n" + trim(process.output)),
        sources.front().string());
  std::istringstream lines(process.output);
  string line;
  size_t records = 0;
  while (std::getline(lines, line)) {
    if (!starts_with(line, "MOSS_BENCH|")) continue;
    auto fields = debug_split_fields(line);
    if (fields.size() != 10)
      throw ProjectError("BENCHMARK_RUNTIME_ERROR",
                         "benchmark harness emitted a malformed result",
                         sources.front().string());
    ProjectBenchmarkResult result;
    result.id = decode_protocol_hex(fields[1]);
    result.name = decode_protocol_hex(fields[2]);
    auto declaration = declarations.find(result.id);
    if (declaration == declarations.end())
      throw ProjectError(
          "BENCHMARK_RUNTIME_ERROR",
          "benchmark harness returned an unknown benchmark identity",
          sources.front().string());
    result.source_file = declaration->second->source_file;
    result.line = declaration->second->line;
    result.median_ns = parse_unsigned_record_field(
        fields[3], "median", sources.front());
    result.p25_ns = parse_unsigned_record_field(fields[4], "p25", sources.front());
    result.p75_ns = parse_unsigned_record_field(fields[5], "p75", sources.front());
    result.samples = static_cast<size_t>(parse_unsigned_record_field(
        fields[6], "sample count", sources.front()));
    result.warmup = static_cast<size_t>(parse_unsigned_record_field(
        fields[7], "warmup count", sources.front()));
    result.iterations = static_cast<size_t>(parse_unsigned_record_field(
        fields[8], "iteration count", sources.front()));
    for (const auto& sample : split_top_level(fields[9], ',')) {
      if (sample.empty()) continue;
      result.sample_values.push_back(parse_unsigned_record_field(
          sample, "sample value", sources.front()));
    }
    if (result.sample_values.size() != result.samples)
      throw ProjectError(
          "BENCHMARK_RUNTIME_ERROR",
          "benchmark sample count does not match its result record",
          sources.front().string());
    results.push_back(std::move(result));
    ++records;
  }
  if (records != artifact.benchmarks.size())
    throw ProjectError(
        "BENCHMARK_RUNTIME_ERROR",
        "benchmark process ended before reporting every discovered benchmark",
        sources.front().string());
  if (results.empty())
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        filter.empty() ? "no Moss benchmarks were found"
                       : "no Moss benchmarks matched filter '" + filter + "'");
  std::sort(results.begin(), results.end(),
            [](const ProjectBenchmarkResult& left,
               const ProjectBenchmarkResult& right) {
              return left.id < right.id;
            });
  return {std::move(results), std::move(backend_toolchain)};
}

static string encode_protocol_hex(const string& value) {
  static constexpr char digits[] = "0123456789abcdef";
  string result;
  result.reserve(value.size() * 2);
  for (unsigned char byte : value) {
    result.push_back(digits[(byte >> 4) & 0x0f]);
    result.push_back(digits[byte & 0x0f]);
  }
  return result;
}

static string benchmark_platform() {
  struct utsname information {};
  if (::uname(&information) != 0) return "unknown";
  return string(information.sysname) + " " + information.release + " " +
      information.machine;
}

static string utc_timestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm value {};
  if (::gmtime_r(&time, &value) == nullptr) return "unknown";
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &value) ==
      0)
    return "unknown";
  return buffer;
}

static void validate_baseline_name(const string& name) {
  if (name.empty() ||
      !std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' ||
            character == '_' || character == '.';
      }))
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        "baseline name must contain only letters, numbers, '.', '-', or '_'");
}

struct BenchmarkBaseline {
  string compiler_version;
  string profile;
  string platform;
  BackendToolchainIdentity backend_toolchain;
  bool has_backend_toolchain = false;
  std::unordered_map<string, ProjectBenchmarkResult> benchmarks;
};

static std::filesystem::path baseline_file(
    const ProjectManifest& manifest, const string& name) {
  validate_baseline_name(name);
  return manifest.root / ".moss" / "benchmarks" / (name + ".json");
}

static void save_benchmark_baseline(
    const ProjectManifest& manifest, const string& name,
    const vector<ProjectBenchmarkResult>& results,
    const BackendToolchainIdentity& backend_toolchain) {
  std::filesystem::path file = baseline_file(manifest, name);
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error)
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        "cannot create benchmark baseline directory: " + error.message(),
        file.string());
  std::ofstream output(file);
  if (!output)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "cannot write benchmark baseline '" + file.string() +
                           "'",
                       file.string());
  output << "{\n  \"format\": \"moss-benchmark-baseline\",\n"
         << "  \"version\": 2,\n"
         << "  \"compiler_version\": \"" << kCompilerVersion << "\",\n"
         << "  \"profile\": \"release\",\n"
         << "  \"platform_hex\": \""
         << encode_protocol_hex(benchmark_platform()) << "\",\n"
         << "  \"backend_toolchain\": {\n"
         << "    \"fingerprint\": \"" << backend_toolchain.fingerprint
         << "\",\n"
         << "    \"resolved_rustc_hex\": \""
         << encode_protocol_hex(backend_toolchain.rustc_executable)
         << "\",\n"
         << "    \"version_verbose_hex\": \""
         << encode_protocol_hex(backend_toolchain.version_verbose)
         << "\",\n"
         << "    \"backend_profile\": \"" << backend_toolchain.profile
         << "\",\n"
         << "    \"compile_flags_hex\": \""
         << encode_protocol_hex(
                backend_flags_text(backend_toolchain.compile_flags))
         << "\"\n  },\n"
         << "  \"timestamp_utc\": \"" << utc_timestamp() << "\",\n"
         << "  \"benchmarks\": [\n";
  for (size_t index = 0; index < results.size(); ++index) {
    const auto& result = results[index];
    output << "    {\"id_hex\": \"" << encode_protocol_hex(result.id)
           << "\", \"name_hex\": \"" << encode_protocol_hex(result.name)
           << "\", \"median_ns\": " << result.median_ns
           << ", \"p25_ns\": " << result.p25_ns
           << ", \"p75_ns\": " << result.p75_ns
           << ", \"samples\": " << result.samples
           << ", \"warmup\": " << result.warmup
           << ", \"iterations\": " << result.iterations
           << ", \"sample_values_ns\": [";
    for (size_t sample = 0; sample < result.sample_values.size(); ++sample) {
      if (sample) output << ", ";
      output << result.sample_values[sample];
    }
    output << "]}" << (index + 1 == results.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

static string baseline_string_field(const string& text, const string& key,
                                    const std::filesystem::path& file) {
  string marker = "\"" + key + "\": \"";
  size_t begin = text.find(marker);
  if (begin == string::npos)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "benchmark baseline is missing '" + key + "'",
                       file.string());
  begin += marker.size();
  size_t end = text.find('"', begin);
  if (end == string::npos)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "benchmark baseline has an invalid '" + key + "'",
                       file.string());
  return text.substr(begin, end - begin);
}

static std::uint64_t baseline_integer_field(
    const string& text, const string& key,
    const std::filesystem::path& file) {
  string marker = "\"" + key + "\": ";
  size_t begin = text.find(marker);
  if (begin == string::npos)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "benchmark baseline is missing '" + key + "'",
                       file.string());
  begin += marker.size();
  size_t end = begin;
  while (end < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[end])))
    ++end;
  if (end == begin)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "benchmark baseline has an invalid '" + key + "'",
                       file.string());
  try {
    return static_cast<std::uint64_t>(
        std::stoull(text.substr(begin, end - begin)));
  } catch (const std::exception&) {
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "benchmark baseline has an invalid '" + key + "'",
                       file.string());
  }
}

static BenchmarkBaseline load_benchmark_baseline(
    const ProjectManifest& manifest, const string& name) {
  std::filesystem::path file = baseline_file(manifest, name);
  std::ifstream input(file);
  if (!input)
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        "benchmark baseline '" + name + "' does not exist", file.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  string text = buffer.str();
  if (baseline_string_field(text, "format", file) !=
      "moss-benchmark-baseline")
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "file is not a Moss benchmark baseline",
                       file.string());
  std::uint64_t version = baseline_integer_field(text, "version", file);
  if (version != 1 && version != 2)
    throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                       "unsupported benchmark baseline version",
                       file.string());
  BenchmarkBaseline baseline;
  baseline.compiler_version = baseline_string_field(
      text, "compiler_version", file);
  baseline.profile = baseline_string_field(text, "profile", file);
  baseline.platform = decode_protocol_hex(
      baseline_string_field(text, "platform_hex", file));
  if (version >= 2) {
    baseline.backend_toolchain.fingerprint = baseline_string_field(
        text, "fingerprint", file);
    baseline.backend_toolchain.rustc_executable = decode_protocol_hex(
        baseline_string_field(text, "resolved_rustc_hex", file));
    baseline.backend_toolchain.version_verbose = decode_protocol_hex(
        baseline_string_field(text, "version_verbose_hex", file));
    baseline.backend_toolchain.profile = baseline_string_field(
        text, "backend_profile", file);
    string flags = decode_protocol_hex(
        baseline_string_field(text, "compile_flags_hex", file));
    std::istringstream flag_lines(flags);
    string flag;
    while (std::getline(flag_lines, flag))
      baseline.backend_toolchain.compile_flags.push_back(flag);
    baseline.has_backend_toolchain = true;
  }
  size_t position = 0;
  while ((position = text.find("{\"id_hex\": \"", position)) !=
         string::npos) {
    size_t end = text.find('}', position);
    if (end == string::npos)
      throw ProjectError("BENCHMARK_CONFIGURATION_ERROR",
                         "benchmark baseline contains a malformed entry",
                         file.string());
    string entry = text.substr(position, end - position + 1);
    ProjectBenchmarkResult result;
    result.id = decode_protocol_hex(
        baseline_string_field(entry, "id_hex", file));
    result.name = decode_protocol_hex(
        baseline_string_field(entry, "name_hex", file));
    result.median_ns = baseline_integer_field(entry, "median_ns", file);
    result.p25_ns = baseline_integer_field(entry, "p25_ns", file);
    result.p75_ns = baseline_integer_field(entry, "p75_ns", file);
    result.samples = static_cast<size_t>(
        baseline_integer_field(entry, "samples", file));
    result.warmup = static_cast<size_t>(
        baseline_integer_field(entry, "warmup", file));
    result.iterations = static_cast<size_t>(
        baseline_integer_field(entry, "iterations", file));
    baseline.benchmarks[result.id] = std::move(result);
    position = end + 1;
  }
  return baseline;
}

struct BenchmarkComparison {
  const ProjectBenchmarkResult* current = nullptr;
  const ProjectBenchmarkResult* baseline = nullptr;
  double change_percent = 0.0;
};

static vector<BenchmarkComparison> compare_benchmarks(
    const vector<ProjectBenchmarkResult>& results,
    const BenchmarkBaseline& baseline) {
  vector<BenchmarkComparison> comparisons;
  for (const auto& result : results) {
    auto prior = baseline.benchmarks.find(result.id);
    if (prior == baseline.benchmarks.end()) continue;
    double change = prior->second.median_ns == 0
        ? 0.0
        : (static_cast<double>(result.median_ns) /
               static_cast<double>(prior->second.median_ns) -
           1.0) * 100.0;
    comparisons.push_back({&result, &prior->second, change});
  }
  return comparisons;
}

static int report_project_benchmarks(
    const ProjectManifest& manifest, const string& filter, bool json,
    const string& save_name, const string& compare_name,
    const std::optional<double>& fail_over) {
  ProjectBenchmarkRun run = run_project_benchmarks(manifest, filter);
  const vector<ProjectBenchmarkResult>& results = run.results;
  auto benchmark_sources = project_declaration_sources(manifest, "benches");
  write_semantic_snapshot(manifest, benchmark_sources,
                          analyze_project_snapshot(manifest, benchmark_sources));
  if (!save_name.empty())
    save_benchmark_baseline(
        manifest, save_name, results, run.backend_toolchain);
  std::optional<BenchmarkBaseline> baseline;
  vector<BenchmarkComparison> comparisons;
  vector<string> warnings;
  bool baseline_compatible = true;
  if (!compare_name.empty()) {
    baseline = load_benchmark_baseline(manifest, compare_name);
    if (baseline->compiler_version != kCompilerVersion ||
        baseline->profile != "release" ||
        baseline->platform != benchmark_platform()) {
      warnings.push_back(
          "baseline Moss compiler, profile, or platform differs from the "
          "current benchmark environment; comparison and --fail-over "
          "enforcement were skipped");
      baseline_compatible = false;
    }
    if (!baseline->has_backend_toolchain ||
        !same_backend_toolchain(
            baseline->backend_toolchain, run.backend_toolchain)) {
      warnings.push_back(
          "baseline backend Rust toolchain differs from the current "
          "toolchain; comparison and --fail-over enforcement were skipped");
      baseline_compatible = false;
    }
    if (baseline_compatible)
      comparisons = compare_benchmarks(results, *baseline);
    if (baseline_compatible && comparisons.empty())
      warnings.push_back(
          "no current benchmark IDs exist in the selected baseline");
  }
  bool regression = baseline_compatible && fail_over && std::any_of(
      comparisons.begin(), comparisons.end(),
      [&](const BenchmarkComparison& comparison) {
        return comparison.change_percent > *fail_over;
      });
  if (json) {
    write_agent_envelope_begin(std::cout, "bench", !regression);
    std::cout << "  \"result\": {\"project\": ";
    write_debug_json_string(std::cout, manifest.name);
    std::cout << ", \"profile\": \"release\", \"filter\": ";
    if (filter.empty()) std::cout << "null";
    else write_debug_json_string(std::cout, filter);
    std::cout << ", \"benchmarks\": [";
    for (size_t index = 0; index < results.size(); ++index) {
      if (index) std::cout << ", ";
      const auto& result = results[index];
      std::cout << "{\"id\": ";
      write_debug_json_string(std::cout, result.id);
      std::cout << ", \"name\": ";
      write_debug_json_string(std::cout, result.name);
      std::cout << ", \"source_file\": ";
      write_debug_json_string(std::cout, result.source_file);
      std::cout << ", \"line\": " << result.line
                << ", \"median_ns\": " << result.median_ns
                << ", \"p25_ns\": " << result.p25_ns
                << ", \"p75_ns\": " << result.p75_ns
                << ", \"samples\": " << result.samples
                << ", \"warmup\": " << result.warmup
                << ", \"iterations\": " << result.iterations << "}";
    }
    std::cout << "], \"backend_toolchain\": ";
    write_backend_toolchain_json(std::cout, run.backend_toolchain);
    std::cout << ", \"baseline_compatible\": ";
    if (compare_name.empty()) std::cout << "null";
    else std::cout << (baseline_compatible ? "true" : "false");
    std::cout << ", \"comparisons\": [";
    for (size_t index = 0; index < comparisons.size(); ++index) {
      if (index) std::cout << ", ";
      const auto& comparison = comparisons[index];
      std::cout << "{\"id\": ";
      write_debug_json_string(std::cout, comparison.current->id);
      std::cout << ", \"baseline_ns\": "
                << comparison.baseline->median_ns
                << ", \"current_ns\": " << comparison.current->median_ns
                << ", \"change_percent\": " << std::fixed
                << std::setprecision(3) << comparison.change_percent << "}";
    }
    std::cout << "], \"warnings\": [";
    for (size_t index = 0; index < warnings.size(); ++index) {
      if (index) std::cout << ", ";
      std::cout << "{\"code\": \"BASELINE_INCOMPATIBLE\", "
                   "\"severity\": \"warning\", \"message\": ";
      write_debug_json_string(std::cout, warnings[index]);
      std::cout << "}";
    }
    std::cout << "], \"saved_baseline\": ";
    if (save_name.empty()) std::cout << "null";
    else write_debug_json_string(std::cout, save_name);
    std::cout << ", \"regression\": "
              << (regression ? "true" : "false")
              << ", \"diagnostic\": ";
    if (!regression) std::cout << "null";
    else
      std::cout << "{\"code\": \"BENCHMARK_REGRESSION\", "
                   "\"severity\": \"error\", \"message\": "
                   "\"benchmark change exceeded --fail-over\"}";
    std::cout << "}\n}\n";
  } else {
    for (const auto& result : results) {
      std::cout << result.name << "\n\n"
                << "median: " << result.median_ns << " ns\n"
                << "p25: " << result.p25_ns << " ns\n"
                << "p75: " << result.p75_ns << " ns\n"
                << "samples: " << result.samples << "\n\n";
    }
    if (!save_name.empty())
      std::cout << "Saved baseline '" << save_name << "'.\n";
    for (const auto& warning : warnings)
      std::cerr << "warning[BASELINE_INCOMPATIBLE]: " << warning << "\n";
    for (const auto& comparison : comparisons) {
      std::cout << comparison.current->name << "\n"
                << "baseline: " << comparison.baseline->median_ns << " ns\n"
                << "current: " << comparison.current->median_ns << " ns\n"
                << "change: " << std::showpos << std::fixed
                << std::setprecision(1) << comparison.change_percent
                << "%" << std::noshowpos << "\n";
    }
    if (regression)
      std::cerr << "error[BENCHMARK_REGRESSION]: benchmark change exceeded "
                   "--fail-over\n";
  }
  return regression ? 1 : 0;
}

static double parse_fail_over(const string& value) {
  try {
    string numeric = value;
    if (!numeric.empty() && numeric.back() == '%') numeric.pop_back();
    size_t used = 0;
    double result = std::stod(numeric, &used);
    if (used != numeric.size() || !std::isfinite(result) || result < 0.0)
      throw std::invalid_argument("invalid threshold");
    return result;
  } catch (const std::exception&) {
    throw ProjectError(
        "BENCHMARK_CONFIGURATION_ERROR",
        "--fail-over requires a non-negative percentage");
  }
}

struct FormatResult {
  std::filesystem::path file;
  bool changed = false;
  string formatted;
};

static size_t comment_start_outside_string(const string& line) {
  bool in_string = false;
  bool escaped = false;
  for (size_t index = 0; index < line.size(); ++index) {
    char character = line[index];
    if (in_string) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') in_string = false;
    } else if (character == '"') {
      in_string = true;
    } else if (character == '#') {
      return index;
    }
  }
  return string::npos;
}

static bool format_word_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) ||
      character == '_';
}

static string canonicalize_code_spacing(const string& input) {
  struct Token { string text; bool word = false; };
  vector<Token> tokens;
  for (size_t index = 0; index < input.size();) {
    unsigned char character = static_cast<unsigned char>(input[index]);
    if (std::isspace(character)) {
      ++index;
      continue;
    }
    if (input[index] == '"') {
      size_t begin = index++;
      bool escaped = false;
      while (index < input.size()) {
        char current = input[index++];
        if (escaped) escaped = false;
        else if (current == '\\') escaped = true;
        else if (current == '"') break;
      }
      tokens.push_back({input.substr(begin, index - begin), true});
      continue;
    }
    if (format_word_character(input[index])) {
      size_t begin = index++;
      while (index < input.size() && format_word_character(input[index]))
        ++index;
      tokens.push_back({input.substr(begin, index - begin), true});
      continue;
    }
    string two = index + 1 < input.size()
        ? input.substr(index, 2) : string();
    if (two == "->" || two == "|>" || two == "==" || two == "!=" ||
        two == "<=" || two == ">=") {
      tokens.push_back({two, false});
      index += 2;
    } else {
      tokens.push_back({string(1, input[index++]), false});
    }
  }

  auto is_binary = [](const string& token) {
    return token == "=" || token == "+" || token == "-" ||
        token == "*" || token == "/" || token == "%" ||
        token == "==" || token == "!=" || token == "<" ||
        token == ">" || token == "<=" || token == ">=" ||
        token == "->" || token == "|>";
  };
  string result;
  for (size_t index = 0; index < tokens.size(); ++index) {
    const Token& token = tokens[index];
    const string previous = index ? tokens[index - 1].text : string();
    bool unary_minus = token.text == "-" &&
        (index == 0 || is_binary(previous) || previous == "(" ||
         previous == "[" || previous == "{" || previous == "," ||
         previous == ":");
    bool space_before = !result.empty();
    if (token.text == ")" || token.text == "]" || token.text == "}" ||
        token.text == "," || token.text == ":" || token.text == "." ||
        token.text == "(")
      space_before = false;
    if (previous == "(" || previous == "[" || previous == "{" ||
        previous == "." ||
        (previous == "-" && index > 1 &&
         (is_binary(tokens[index - 2].text) ||
          tokens[index - 2].text == "(" ||
          tokens[index - 2].text == "[")))
      space_before = false;
    if (unary_minus) space_before = index != 0 && previous != "(" &&
        previous != "[" && previous != "{" && previous != ".";
    if (is_binary(token.text) && !unary_minus) space_before = !result.empty();
    if (space_before && result.back() != ' ') result.push_back(' ');
    result += token.text;
    if (token.text == "," || token.text == ":" ||
        (is_binary(token.text) && !unary_minus))
      result.push_back(' ');
  }
  return rtrim(std::move(result));
}

static string canonical_format_moss(const string& source) {
  vector<string> raw_lines;
  std::istringstream input(source);
  string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    string expanded;
    for (char character : line) {
      if (character == '\t') expanded += "  ";
      else expanded.push_back(character);
    }
    raw_lines.push_back(std::move(expanded));
  }
  std::set<size_t> indentation{0};
  for (const auto& raw : raw_lines) {
    size_t first = raw.find_first_not_of(' ');
    if (first != string::npos) indentation.insert(first);
  }
  vector<size_t> levels(indentation.begin(), indentation.end());
  auto canonical_indent = [&](size_t width) {
    size_t level = static_cast<size_t>(std::upper_bound(
        levels.begin(), levels.end(), width) - levels.begin());
    if (level > 0) --level;
    return string(level * 2, ' ');
  };

  vector<string> output;
  bool prior_blank = true;
  size_t previous_code_width = 0;
  for (const auto& raw : raw_lines) {
    size_t first = raw.find_first_not_of(' ');
    if (first == string::npos) {
      if (!prior_blank && !output.empty()) output.push_back("");
      prior_blank = true;
      continue;
    }
    string content = rtrim(raw.substr(first));
    if (content.empty()) continue;
    string formatted;
    if (content.front() == '#') {
      formatted = canonical_indent(first) + rtrim(content);
    } else {
      size_t comment = comment_start_outside_string(content);
      string code = rtrim(content.substr(0, comment));
      string suffix = comment == string::npos
          ? string() : trim(content.substr(comment));
      string indent = canonical_indent(first);
      if (starts_with(trim(code), "|>"))
        indent = canonical_indent(previous_code_width) + "  ";
      formatted = indent + canonicalize_code_spacing(code);
      if (!suffix.empty()) formatted += "  " + suffix;
      previous_code_width = first;
    }
    output.push_back(std::move(formatted));
    prior_blank = false;
  }
  while (!output.empty() && output.back().empty()) output.pop_back();
  std::ostringstream result;
  for (const auto& output_line : output) result << output_line << "\n";
  return result.str();
}

static void validate_formatted_source(const string& source,
                                      const std::filesystem::path& file) {
  try {
    std::istringstream input(source);
    Program program = Parser(lex_lines(input)).parse();
    Checker checker(program);
    checker.run();
  } catch (const CompileError& error) {
    throw ProjectError(
        "FORMAT_PARSE_ERROR", error.what(), file.string(), error.line);
  }
}

static vector<std::filesystem::path> project_moss_sources(
    const ProjectManifest& manifest) {
  vector<std::filesystem::path> files =
      project_declaration_sources(manifest, "tests");
  vector<std::filesystem::path> benches =
      project_declaration_sources(manifest, "benches");
  files.insert(files.end(), benches.begin(), benches.end());
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

static int run_project_format(const ProjectManifest& manifest,
                              const vector<std::filesystem::path>& requested,
                              bool check, bool json) {
  bool project_scope = requested.empty();
  vector<std::filesystem::path> files = requested.empty()
      ? project_moss_sources(manifest) : requested;
  vector<FormatResult> results;
  for (auto file : files) {
    if (file.is_relative()) file = manifest.root / file;
    file = std::filesystem::absolute(file).lexically_normal();
    string original = read_text_file(file, "FORMAT_SOURCE_NOT_FOUND");
    string normalized;
    for (char character : original) {
      if (character == '\t') normalized += "  ";
      else normalized.push_back(character);
    }
    if (!project_scope) validate_formatted_source(normalized, file);
    string formatted = canonical_format_moss(original);
    if (!project_scope) validate_formatted_source(formatted, file);
    FormatResult result{file, formatted != original, formatted};
    if (result.changed && !check) {
      std::ofstream output(file, std::ios::binary);
      if (!output)
        throw ProjectError("FORMAT_WRITE_ERROR",
                           "cannot write formatted source", file.string());
      output << formatted;
    }
    results.push_back(std::move(result));
  }
  if (project_scope) {
    Program merged;
    try {
      for (const auto& result : results) {
        std::istringstream input(result.formatted);
        Program parsed = Parser(lex_lines(input, result.file.string())).parse();
        assign_project_declaration_identities(
            parsed, project_relative_path(manifest, result.file));
        merge_project_program(merged, std::move(parsed), result.file.string());
      }
      Checker checker(merged);
      checker.run();
    } catch (const CompileError& error) {
      throw ProjectError("FORMAT_PARSE_ERROR", error.what(),
                         error.source_file.empty()
                             ? manifest.manifest_file.string()
                             : error.source_file,
                         error.line);
    }
  }
  bool changed = std::any_of(
      results.begin(), results.end(),
      [](const FormatResult& result) { return result.changed; });
  if (json) {
    write_agent_envelope_begin(std::cout, "fmt", !(check && changed));
    std::cout << "  \"result\": {\"check\": "
              << (check ? "true" : "false") << ", \"files\": [";
    for (size_t index = 0; index < results.size(); ++index) {
      if (index) std::cout << ", ";
      std::cout << "{\"file\": ";
      write_debug_json_string(std::cout, results[index].file.string());
      std::cout << ", \"changed\": "
                << (results[index].changed ? "true" : "false") << "}";
    }
    std::cout << "], \"would_change\": " << (changed ? "true" : "false")
              << ", \"diagnostic\": ";
    if (check && changed)
      std::cout << "{\"code\": \"FORMAT_CHECK_FAILED\", "
                   "\"severity\": \"error\", \"message\": "
                   "\"Moss source is not canonically formatted\"}";
    else
      std::cout << "null";
    std::cout << "}\n}\n";
  } else if (check && changed) {
    for (const auto& result : results)
      if (result.changed)
        std::cerr << result.file.string() <<
            ": error[FORMAT_CHECK_FAILED]: source is not canonically "
            "formatted\n";
  } else if (!check) {
    for (const auto& result : results)
      if (result.changed) std::cout << "Formatted " << result.file << ".\n";
  }
  return check && changed ? 1 : 0;
}

static vector<string> split_source_lines(const string& source) {
  vector<string> lines;
  std::istringstream input(source);
  string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
  }
  return lines;
}

static string join_source_lines(const vector<string>& lines) {
  std::ostringstream out;
  for (const auto& line : lines) out << line << "\n";
  return out.str();
}

static size_t replace_identifier_on_line(string& line, const string& old_name,
                                         const string& new_name) {
  size_t replacements = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t index = 0; index + old_name.size() <= line.size();) {
    char character = line[index];
    if (in_string) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') in_string = false;
      ++index;
      continue;
    }
    if (character == '"') {
      in_string = true;
      ++index;
      continue;
    }
    if (character == '#') break;
    bool left = index == 0 || !format_word_character(line[index - 1]);
    bool right = index + old_name.size() == line.size() ||
        !format_word_character(line[index + old_name.size()]);
    if (left && right && line.compare(index, old_name.size(), old_name) == 0) {
      line.replace(index, old_name.size(), new_name);
      index += new_name.size();
      ++replacements;
    } else {
      ++index;
    }
  }
  return replacements;
}

static const SemanticTargetFact* resolve_edit_target(
    const vector<SemanticTargetFact>& facts, const string& selector) {
  vector<const SemanticTargetFact*> matches;
  bool exact_identity = starts_with(selector, "entity-v1:") ||
      selector.find('@') != string::npos;
  for (const auto& fact : facts) {
    bool match = fact.durable_identity == selector ||
        fact.semantic_identity == selector;
    if (!exact_identity)
      match = match || fact.context == selector || fact.name == selector ||
          fact.kind + ":" + fact.name == selector;
    if (match) matches.push_back(&fact);
  }
  if (matches.empty())
    throw ProjectError(
        "EDIT_TARGET_STALE",
        "semantic edit target '" + selector +
            "' does not exist in the current checked source");
  std::sort(matches.begin(), matches.end(),
            [](const SemanticTargetFact* left,
               const SemanticTargetFact* right) {
              return left->durable_identity < right->durable_identity;
            });
  matches.erase(std::unique(
                    matches.begin(), matches.end(),
                    [](const SemanticTargetFact* left,
                       const SemanticTargetFact* right) {
                      return left->durable_identity == right->durable_identity;
                    }),
                matches.end());
  if (matches.size() != 1)
    throw ProjectError(
        "EDIT_TARGET_AMBIGUOUS",
        "semantic edit target '" + selector +
            "' is ambiguous; use an exact durable identity");
  return matches.front();
}

static size_t assignment_operator(const string& line) {
  bool in_string = false;
  bool escaped = false;
  int parens = 0, brackets = 0, braces = 0;
  for (size_t index = 0; index < line.size(); ++index) {
    char character = line[index];
    if (in_string) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') in_string = false;
      continue;
    }
    if (character == '"') { in_string = true; continue; }
    if (character == '#') break;
    if (character == '(') ++parens;
    else if (character == ')') --parens;
    else if (character == '[') ++brackets;
    else if (character == ']') --brackets;
    else if (character == '{') ++braces;
    else if (character == '}') --braces;
    else if (character == '=' && parens == 0 && brackets == 0 &&
             braces == 0 &&
             (index == 0 || (line[index - 1] != '=' &&
                             line[index - 1] != '!' &&
                             line[index - 1] != '<' &&
                             line[index - 1] != '>')) &&
             (index + 1 == line.size() || line[index + 1] != '='))
      return index;
  }
  return string::npos;
}

static bool replace_call_argument_on_line(
    string& line, const string& call_target, size_t argument_index,
    const string& replacement) {
  string leaf = call_target;
  size_t colon = leaf.rfind(':');
  if (colon != string::npos) leaf = leaf.substr(colon + 1);
  size_t dot = leaf.rfind('.');
  if (dot != string::npos) leaf = leaf.substr(dot + 1);
  vector<std::pair<size_t,size_t>> calls;
  bool in_string = false;
  bool escaped = false;
  for (size_t index = 0; index + leaf.size() < line.size(); ++index) {
    char character = line[index];
    if (in_string) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') in_string = false;
      continue;
    }
    if (character == '"') { in_string = true; continue; }
    if (character == '#') break;
    if (line.compare(index, leaf.size(), leaf) != 0) continue;
    bool left = index == 0 || !format_word_character(line[index - 1]);
    size_t open = index + leaf.size();
    while (open < line.size() && line[open] == ' ') ++open;
    if (!left || open >= line.size() || line[open] != '(') continue;
    int depth = 0;
    bool call_string = false;
    bool call_escaped = false;
    for (size_t end = open; end < line.size(); ++end) {
      char current = line[end];
      if (call_string) {
        if (call_escaped) call_escaped = false;
        else if (current == '\\') call_escaped = true;
        else if (current == '"') call_string = false;
        continue;
      }
      if (current == '"') { call_string = true; continue; }
      if (current == '(') ++depth;
      else if (current == ')' && --depth == 0) {
        calls.push_back({open, end});
        break;
      }
    }
  }
  if (calls.size() != 1) return false;
  size_t open = calls.front().first;
  size_t close = calls.front().second;
  string arguments_text = line.substr(open + 1, close - open - 1);
  vector<string> arguments = trim(arguments_text).empty()
      ? vector<string>{} : split_top_level(arguments_text, ',');
  if (argument_index >= arguments.size())
    throw ProjectError(
        "EDIT_ARGUMENT_INDEX_INVALID",
        "call has " + std::to_string(arguments.size()) +
            " arguments; index " + std::to_string(argument_index) +
            " is out of range");
  arguments[argument_index] = replacement;
  std::ostringstream rewritten;
  for (size_t index = 0; index < arguments.size(); ++index) {
    if (index) rewritten << ", ";
    rewritten << arguments[index];
  }
  line.replace(open + 1, close - open - 1, rewritten.str());
  return true;
}

static int run_semantic_edit(
    const ProjectManifest& manifest, const string& operation,
    const string& selector, const vector<string>& operands,
    const std::filesystem::path& requested_source, bool json) {
  if (!json)
    throw ProjectError("EDIT_JSON_REQUIRED",
                       "semantic edit commands require --json");
  std::filesystem::path source = requested_source.empty()
      ? project_source_file(manifest) : requested_source;
  if (source.is_relative()) source = manifest.root / source;
  source = std::filesystem::absolute(source).lexically_normal();
  SourceCompilationContext context = analyze_source_context(source);
  if (!context.project) {
    context.manifest = manifest;
    context.sources = {source};
  }
  CompiledProjectUnit unit = analyze_project_sources(
      context.manifest, context.sources, true, false, context.mode);
  vector<SemanticTargetFact> facts = semantic_target_facts(
      unit.program, unit.plan);
  const SemanticTargetFact* target = resolve_edit_target(facts, selector);
  std::filesystem::path target_source = target->source_file.empty()
      ? source : std::filesystem::path(target->source_file);
  target_source = std::filesystem::absolute(target_source).lexically_normal();
  std::map<std::filesystem::path, vector<string>> edited_lines;
  std::map<std::filesystem::path, string> original_text;
  for (const auto& file : context.sources) {
    auto normalized = std::filesystem::absolute(file).lexically_normal();
    original_text[normalized] = read_text_file(normalized,
                                               "EDIT_SOURCE_NOT_FOUND");
    edited_lines[normalized] = split_source_lines(original_text[normalized]);
  }
  auto target_it = edited_lines.find(target_source);
  if (target_it == edited_lines.end())
    throw ProjectError("EDIT_TARGET_STALE",
                       "semantic target source is not in the logical project context",
                       target_source.string());
  vector<string>& lines = target_it->second;
  if (target->line <= 0 || static_cast<size_t>(target->line) > lines.size())
    throw ProjectError("EDIT_TARGET_STALE",
                       "semantic target no longer has an exact source range",
                       source.string());
  string resulting_selector = target->durable_identity;
  std::map<std::filesystem::path, std::set<int>> changed_line_numbers;
  if (operation == "rename") {
    if (operands.size() != 1 || !plain_identifier(operands[0]))
      throw ProjectError("EDIT_ARGUMENT_INVALID",
                         "rename requires one valid Moss identifier");
    if (target->kind != "function")
      throw ProjectError(
          "EDIT_KIND_UNSUPPORTED",
          "rename currently supports exact function entities only");
    string old_name = target->name;
    string new_name = operands[0];
    changed_line_numbers[target_source].insert(target->line);
    for (const auto& edge : unit.program.semantic_call_edges)
      if (edge.target == target->context && !edge.source_file.empty())
        changed_line_numbers[std::filesystem::absolute(edge.source_file)
                                 .lexically_normal()].insert(edge.line);
    for (const auto& pipeline : unit.program.functional_pipelines) {
      bool references_target = std::any_of(
          pipeline.nodes.begin(), pipeline.nodes.end(), [&](const FunctionalNode& node) {
            return node.callable_identity == target->context;
          });
      if (references_target && !pipeline.source_file.empty())
        changed_line_numbers[std::filesystem::absolute(pipeline.source_file)
                                 .lexically_normal()].insert(pipeline.line);
    }
    size_t replacements = 0;
    size_t expected = 0;
    for (const auto& file_lines : changed_line_numbers) {
      auto file_it = edited_lines.find(file_lines.first);
      if (file_it == edited_lines.end())
        throw ProjectError("EDIT_TARGET_STALE",
                           "a resolved reference is outside the logical project context",
                           file_lines.first.string());
      expected += file_lines.second.size();
      for (int line_number : file_lines.second) {
        if (line_number <= 0 || static_cast<size_t>(line_number) > file_it->second.size())
          throw ProjectError("EDIT_TARGET_STALE",
                             "a resolved reference has no exact source range",
                             file_lines.first.string(), line_number);
        replacements += replace_identifier_on_line(
            file_it->second[static_cast<size_t>(line_number - 1)], old_name,
            new_name);
      }
    }
    if (replacements != expected)
      throw ProjectError(
          "EDIT_TARGET_AMBIGUOUS",
          "rename could not map every semantic reference to one exact token",
          source.string(), target->line);
    resulting_selector = "entity-v1:function:" + new_name;
  } else if (operation == "replace-expression") {
    if (operands.size() != 1)
      throw ProjectError("EDIT_ARGUMENT_INVALID",
                         "replace-expression requires one expression");
    if (target->kind != "binding")
      throw ProjectError(
          "EDIT_KIND_UNSUPPORTED",
          "replace-expression currently requires an exact binding entity");
    string& edit_line = lines[static_cast<size_t>(target->line - 1)];
    size_t equals = assignment_operator(edit_line);
    if (equals == string::npos)
      throw ProjectError(
          "EDIT_TARGET_AMBIGUOUS",
          "binding does not have one replaceable source expression",
          source.string(), target->line);
    size_t comment = comment_start_outside_string(edit_line);
    string suffix = comment == string::npos
        ? string() : "  " + trim(edit_line.substr(comment));
    edit_line = rtrim(edit_line.substr(0, equals + 1)) + " " + operands[0] +
        suffix;
    changed_line_numbers[target_source].insert(target->line);
  } else if (operation == "change-argument") {
    if (operands.size() != 2)
      throw ProjectError(
          "EDIT_ARGUMENT_INVALID",
          "change-argument requires a zero-based index and expression");
    if (target->kind != "call")
      throw ProjectError(
          "EDIT_KIND_UNSUPPORTED",
          "change-argument requires an exact call entity");
    size_t argument_index = 0;
    try {
      size_t used = 0;
      argument_index = std::stoull(operands[0], &used);
      if (used != operands[0].size()) throw std::invalid_argument("index");
    } catch (const std::exception&) {
      throw ProjectError("EDIT_ARGUMENT_INDEX_INVALID",
                         "argument index must be a non-negative integer");
    }
    string& edit_line = lines[static_cast<size_t>(target->line - 1)];
    if (!replace_call_argument_on_line(
            edit_line, target->name, argument_index, operands[1]))
      throw ProjectError(
          "EDIT_TARGET_AMBIGUOUS",
          "call source line does not contain one exact target invocation",
          source.string(), target->line);
    changed_line_numbers[target_source].insert(target->line);
  } else {
    throw ProjectError("EDIT_OPERATION_INVALID",
                       "unknown semantic edit operation '" + operation + "'");
  }

  std::map<std::filesystem::path, string> candidate_text;
  vector<std::filesystem::path> changed_files;
  for (auto& entry : edited_lines) {
    string candidate = original_text[entry.first];
    if (changed_line_numbers.count(entry.first))
      candidate = canonical_format_moss(join_source_lines(entry.second));
    candidate_text[entry.first] = candidate;
    if (candidate != original_text[entry.first]) changed_files.push_back(entry.first);
  }
  if (changed_files.empty())
    throw ProjectError("EDIT_TARGET_STALE", "semantic edit made no source change",
                       target_source.string(), target->line);
  vector<std::pair<std::filesystem::path, string>> candidate_files;
  for (const auto& file : context.sources) {
    auto normalized = std::filesystem::absolute(file).lexically_normal();
    candidate_files.push_back({normalized, candidate_text[normalized]});
  }
  // Validate the complete logical target before touching any physical file.
  CompiledProjectUnit updated = analyze_project_texts(
      context.manifest, candidate_files, true, false, context.mode);
  for (const auto& file : changed_files) {
    std::ofstream output(file, std::ios::binary);
    if (!output)
      throw ProjectError("EDIT_WRITE_ERROR", "cannot write edited Moss source",
                         file.string());
    output << candidate_text[file];
  }
  vector<SemanticTargetFact> updated_facts = semantic_target_facts(
      updated.program, updated.plan);
  const SemanticTargetFact* resulting = nullptr;
  for (const auto& fact : updated_facts)
    if (fact.durable_identity == resulting_selector) {
      resulting = &fact;
      break;
    }
  write_agent_envelope_begin(std::cout, "edit " + operation, true);
  std::cout << "  \"result\": {\"operation\": ";
  write_debug_json_string(std::cout, operation);
  std::cout << ", \"target\": ";
  write_debug_json_string(std::cout, selector);
  std::cout << ", \"changed_files\": [";
  for (size_t index = 0; index < changed_files.size(); ++index) {
    if (index) std::cout << ", ";
    write_debug_json_string(std::cout, changed_files[index].string());
  }
  std::cout << "], \"changed_ranges\": [";
  bool first_range = true;
  for (const auto& file_lines : changed_line_numbers) {
    if (std::find(changed_files.begin(), changed_files.end(),
                  file_lines.first) == changed_files.end()) continue;
    for (int line : file_lines.second) {
      if (!first_range) std::cout << ", ";
      first_range = false;
      std::cout << "{\"source_file\": ";
      write_debug_json_string(std::cout, file_lines.first.string());
      std::cout << ", \"start_line\": " << line
                << ", \"end_line\": " << line << "}";
    }
  }
  std::cout << "], \"resulting_target\": ";
  if (resulting) write_semantic_target_json(std::cout, *resulting,
                                             target_source.string());
  else std::cout << "null";
  std::cout << ", \"formatted\": true}\n}\n";
  return 0;
}

static int run_project_command(int argc, char** argv) {
  string command = argv[1];
  bool json = false;
  for (int index = 2; index < argc; ++index)
    if (string(argv[index]) == "--json") json = true;
  try {
    ProjectManifest manifest = load_project_manifest(
        std::filesystem::current_path());
    if (command == "build") {
      bool release = false;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--release") release = true;
        else if (argument != "--json")
          throw ProjectError("BUILD_PROFILE_ERROR",
                             "unexpected build argument '" + argument + "'");
      }
      return run_project_build(manifest, release, json);
    }
    if (command == "fmt") {
      bool check = false;
      vector<std::filesystem::path> sources;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--json") continue;
        if (argument == "--check") check = true;
        else sources.emplace_back(argument);
      }
      return run_project_format(manifest, sources, check, json);
    }
    if (command == "edit") {
      if (argc < 5)
        throw ProjectError(
            "EDIT_ARGUMENT_INVALID",
            "usage: moss edit <operation> <semantic-id> <operands> --json");
      string operation = argv[2];
      string selector = argv[3];
      vector<string> operands;
      std::filesystem::path source;
      for (int index = 4; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--json") continue;
        if (argument == "--source") {
          if (++index >= argc)
            throw ProjectError("EDIT_SOURCE_REQUIRED",
                               "--source requires a Moss source path");
          source = argv[index];
          continue;
        }
        operands.push_back(argument);
      }
      return run_semantic_edit(
          manifest, operation, selector, operands, source, json);
    }
    if (command == "impact") {
      string selector;
      std::filesystem::path source = project_source_file(manifest);
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--json") continue;
        if (argument == "--source") {
          if (++index >= argc)
            throw ProjectError("IMPACT_SOURCE_REQUIRED",
                               "--source requires a Moss source path");
          source = std::filesystem::absolute(argv[index]).lexically_normal();
          continue;
        }
        if (!selector.empty())
          throw ProjectError("IMPACT_TARGET_INVALID",
                             "moss impact accepts one semantic target");
        selector = argument;
      }
      if (selector.empty())
        throw ProjectError("IMPACT_TARGET_INVALID",
                           "moss impact requires a semantic target");
      return run_project_impact(manifest, source, selector, json);
    }
    if (command == "clean") {
      for (int index = 2; index < argc; ++index)
        if (string(argv[index]) != "--json")
          throw ProjectError("BUILD_PROFILE_ERROR",
                             "unexpected clean argument '" +
                                 string(argv[index]) + "'");
      std::filesystem::path build = manifest.root / "build";
      std::error_code error;
      std::filesystem::remove_all(build, error);
      if (error)
        throw ProjectError("MOSS_INTERNAL_OR_IO_ERROR",
                           "cannot clean project build directory: " +
                               error.message(),
                           build.string());
      if (json) {
        write_agent_envelope_begin(std::cout, "clean", true);
        std::cout << "  \"result\": {\"project\": ";
        write_debug_json_string(std::cout, manifest.name);
        std::cout << ", \"removed\": ";
        write_debug_json_string(std::cout, build.string());
        std::cout << "}\n}\n";
      } else {
        std::cout << "Cleaned " << manifest.name << ".\n";
      }
      return 0;
    }
    if (command == "test") {
      string filter;
      bool affected = false;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--json") continue;
        if (argument == "--affected") {
          affected = true;
          continue;
        }
        if (!filter.empty())
          throw ProjectError("TEST_DISCOVERY_ERROR",
                             "moss test accepts at most one filter");
        filter = argument;
      }
      return report_project_tests(manifest, filter, json, affected);
    }
    if (command == "bench") {
      string filter;
      string save_name;
      string compare_name;
      std::optional<double> fail_over;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--json") continue;
        if (argument == "--save" || argument == "--compare" ||
            argument == "--fail-over") {
          if (++index >= argc)
            throw ProjectError(
                "BENCHMARK_CONFIGURATION_ERROR",
                argument + " requires a value");
          string value = argv[index];
          if (argument == "--save") save_name = value;
          else if (argument == "--compare") compare_name = value;
          else fail_over = parse_fail_over(value);
          continue;
        }
        if (!filter.empty())
          throw ProjectError(
              "BENCHMARK_CONFIGURATION_ERROR",
              "moss bench accepts at most one filter");
        filter = argument;
      }
      if (fail_over && compare_name.empty())
        throw ProjectError(
            "BENCHMARK_CONFIGURATION_ERROR",
            "--fail-over requires --compare <baseline>");
      return report_project_benchmarks(
          manifest, filter, json, save_name, compare_name, fail_over);
    }
    throw ProjectError("PROJECT_COMMAND_ERROR",
                       "unknown project command '" + command + "'");
  } catch (const ProjectError& error) {
    if (json) write_project_error_json(std::cout, command, error);
    else {
      if (!error.source_file.empty())
        std::cerr << error.source_file;
      else
        std::cerr << "moss";
      if (error.line > 0) std::cerr << ":" << error.line;
      std::cerr << ": error[" << error.code << "]: " << error.what()
                << "\n";
    }
    return 1;
  }
}

} // namespace moss

static std::optional<string> nearest_moss_project_root(
    std::filesystem::path start) {
  std::error_code error;
  start = std::filesystem::absolute(start, error).lexically_normal();
  if (error) return std::nullopt;
  if (!std::filesystem::is_directory(start, error)) start = start.parent_path();
  while (!start.empty()) {
    if (std::filesystem::is_regular_file(start / "Moss.toml", error) ||
        std::filesystem::is_regular_file(start / "moss.toml", error))
      return start.string();
    std::filesystem::path parent = start.parent_path();
    if (parent == start) break;
    start = std::move(parent);
  }
  return std::nullopt;
}

static void usage() {
  std::cerr << "Moss v0.1 - static compiler to Rust with domains and functional dataflow\n\n"
            << "Usage:\n"
            << "  moss <input.moss> [-Oshared-memory] [--debug] [--dump-functional-ir] [--explain-fusion] [-o output.rs]\n"
            << "  moss --check <input.moss>\n"
            << "  moss check <input.moss> [--json]\n"
            << "  moss agent bootstrap|capabilities|schema --json\n"
            << "  moss agent session-report-template --json\n"
            << "  moss inspect|type|effects|ownership|calls|why|cost <target> --source <input.moss> --json\n"
            << "  moss impact <target> [--source <input.moss>] --json\n"
            << "  moss fmt [--check] [--json]\n"
            << "  moss edit rename|replace-expression|change-argument ... --json\n"
            << "  moss build [--release] [--json]\n"
            << "  moss clean [--json]\n"
            << "  moss test [filter] [--affected] [--json]\n"
            << "  moss bench [filter] [--json] [--save NAME] [--compare NAME] [--fail-over PERCENT]\n\n"
            << "  moss run --interp <input.moss> [--trace]\n\n"
            << "  moss debug <project-or-source> [--trace]\n\n"
            << "Backend optimization:\n"
            << "  -O, -Oshared-memory    apply safe functional rewrites/fusion; use planned handler-level 2PL\n"
            << "  -O0                    retain eager pipelines; use planned handler-level 2PL\n\n"
            << "  --dump-functional-ir   print typed nodes, semantic rewrites, and lowering decisions\n"
            << "  --explain-fusion       print deterministic functional optimization decisions\n\n"
            << "  --debug                 emit -O0 Rust with stable native symbols for source debugging\n"
            << "  --emit-debug-map FILE   write the shared Moss provenance map to FILE\n"
            << "  --native-output FILE    record the intended native executable in the debug map\n"
            << "  --diagnostic-paths      prefix diagnostics with the Moss source path\n\n"
            << "Request/reply:\n"
            << "  message domain.Message(args...)\n"
            << "  fn Message(args...) [-> Type]\n"
            << "  reply value\n"
            << "  value = message domain.Message(args...)\n"
            << "  fn square(x) = x * x\n"
            << "  type Quote:\n";
}

static std::filesystem::path resolve_fast_debug_entry(const string& target) {
  std::filesystem::path requested(target);
  std::error_code error;
  if (std::filesystem::is_regular_file(requested, error))
    return requested.lexically_normal();
  if (std::filesystem::is_directory(requested, error) &&
      std::filesystem::is_regular_file(requested / "moss.toml", error)) {
    return moss::project_source_file(moss::load_project_manifest(requested));
  }
  if (requested.extension() != ".moss") {
    std::filesystem::path with_extension = requested;
    with_extension += ".moss";
    if (std::filesystem::is_regular_file(with_extension, error))
      return with_extension.lexically_normal();
  }
  auto root = nearest_moss_project_root(std::filesystem::current_path());
  if (root) {
    moss::ProjectManifest manifest = moss::load_project_manifest(*root);
    if (target == "." || target == "app" || target == manifest.name)
      return moss::project_source_file(manifest);
    std::filesystem::path candidate = manifest.root / manifest.source / requested;
    if (std::filesystem::is_regular_file(candidate, error))
      return candidate.lexically_normal();
    if (candidate.extension() != ".moss") {
      candidate += ".moss";
      if (std::filesystem::is_regular_file(candidate, error))
        return candidate.lexically_normal();
    }
  }
  return requested.lexically_normal();
}

static moss::Program load_checked_interpreter_program(const std::filesystem::path& input) {
  try {
    auto context = moss::analyze_source_context(input);
    if (context.project) {
      auto sources = moss::fast_debug_source_closure(context);
      auto unit = moss::analyze_project_sources(
          context.manifest, sources, false, true, context.mode);
      if (!unit.program.external_modules.empty()) {
        throw moss::ProjectError(
            "FAST_DEBUG_NATIVE_DEPENDENCY",
            "Fast Debug cannot mix interpreted Moss with compiled Moss module "
            "dependencies; include every reachable Moss module in the source "
            "project",
            context.requested_source.string());
      }
      return std::move(unit.program);
    }
    std::ifstream source(input);
    if (!source) {
      moss::CompileError error(0, "cannot open Moss source '" + input.string() + "'");
      error.source_file = input.string();
      throw error;
    }
    auto lines = moss::lex_lines(
        source, std::filesystem::absolute(input).lexically_normal().string());
    moss::Parser parser(std::move(lines));
    moss::Program program = parser.parse();
    moss::Checker checker(program);
    checker.run();
    moss::FunctionalOptimizer(program).run(false);
    return program;
  } catch (const moss::ProjectError& error) {
    moss::CompileError converted(error.line, error.what());
    converted.source_file = error.source_file;
    converted.code = error.code;
    throw converted;
  }
}

static int run_fast_interpreter_source(const std::filesystem::path& input,
                                       bool trace) {
  moss::Program program = load_checked_interpreter_program(input);
  moss::FastInterpreter::Options options;
  options.trace = trace;
  options.source_file = std::filesystem::absolute(input).lexically_normal().string();
  moss::FastInterpreter interpreter(program, options);
  interpreter.run_main(std::cout);
  if (trace) interpreter.write_trace(std::cerr);
  return 0;
}

static int run_fast_interpreter_tests(const std::filesystem::path& input,
                                      const string& filter, bool trace) {
  moss::Program program = load_checked_interpreter_program(input);
  moss::FastInterpreter::Options options;
  options.trace = trace;
  options.source_file = std::filesystem::absolute(input).lexically_normal().string();
  moss::FastInterpreter interpreter(program, options);
  interpreter.run_tests(std::cout, filter);
  if (trace) interpreter.write_trace(std::cerr);
  return 0;
}

int main(int argc, char** argv) {
  string diagnostic_source = "moss";
  string active_command = "compile";
  string active_input;
  bool json_output = false;
  std::optional<moss::Program> active_program;
  try {
    if (argc < 2) { usage(); return 2; }

    if (string(argv[1]) == "run" || string(argv[1]) == "debug") {
      const bool debug_command = string(argv[1]) == "debug";
      bool interpreter = false, trace = false;
      string input;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--interp") interpreter = true;
        else if (argument == "--trace") trace = true;
        else if (argument == "--json") {
          std::cerr << "moss: " << argv[1]
                    << " does not yet support --json\n";
          return 2;
        } else if (input.empty()) input = argument;
        else {
          std::cerr << "moss: " << argv[1]
                    << " accepts one Moss source or project path\n";
          return 2;
        }
      }
      if (!debug_command && !interpreter) {
        std::cerr << "moss: run currently requires --interp\n";
        return 2;
      }
      if (input.empty()) {
        std::cerr << "moss: " << argv[1]
                  << " requires a Moss source or project path\n";
        return 2;
      }
      try {
        return run_fast_interpreter_source(
            resolve_fast_debug_entry(input), trace);
      } catch (const moss::FastInterpreter::RuntimeError& error) {
        std::cerr << "moss:" << error.line() << ": interpreter error: "
                  << error.what() << "\n";
        return 1;
      }
    }

    // A standalone test file can use the same checked interpreter backend;
    // project test discovery remains on the compiled path until the domain
    // scheduler checkpoint is complete.
    bool test_requests_interpreter = false;
    if (string(argv[1]) == "test") {
      for (int index = 2; index < argc; ++index)
        if (string(argv[index]) == "--interp") test_requests_interpreter = true;
    }
    if (test_requests_interpreter) {
      bool interpreter = false, trace = false;
      string input, filter;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--interp") interpreter = true;
        else if (argument == "--trace") trace = true;
        else if (argument == "--json") {
          std::cerr << "moss: test --interp does not yet support --json\n";
          return 2;
        } else if (input.empty()) input = argument;
        else if (filter.empty()) filter = argument;
        else {
          std::cerr << "moss: test accepts one source path and one filter\n";
          return 2;
        }
      }
      if (interpreter) {
        if (input.empty()) {
          std::cerr << "moss: test --interp requires a standalone Moss source path\n";
          return 2;
        }
        try {
          return run_fast_interpreter_tests(
              resolve_fast_debug_entry(input), filter, trace);
        } catch (const moss::FastInterpreter::RuntimeError& error) {
          std::cerr << "moss:" << error.line() << ": interpreter error: "
                    << error.what() << "\n";
          return 1;
        }
      }
    }

    // Formatting is useful for a standalone source file as well as a
    // manifest-backed project.  Project mode below discovers every convention
    // directory; this small path keeps `moss fmt file.moss` dependency-free.
    if (string(argv[1]) == "fmt" &&
        !nearest_moss_project_root(std::filesystem::current_path())) {
      bool check = false;
      bool json = false;
      vector<std::filesystem::path> requested;
      for (int index = 2; index < argc; ++index) {
        string argument = argv[index];
        if (argument == "--check") check = true;
        else if (argument == "--json") json = true;
        else requested.emplace_back(argument);
      }
      if (requested.empty()) {
        moss::write_structured_error(
            std::cout, "fmt", "FORMAT_SOURCE_REQUIRED",
            "standalone moss fmt requires a .moss source path");
        return 2;
      }
      moss::ProjectManifest manifest;
      manifest.root = std::filesystem::current_path();
      try {
        return moss::run_project_format(manifest, requested, check, json);
      } catch (const moss::ProjectError& error) {
        if (json) moss::write_project_error_json(std::cout, "fmt", error);
        else std::cerr << "moss: error[" << error.code << "]: "
                        << error.what() << "\n";
        return 1;
      }
    }

    static const std::set<string> project_commands = {
        "build", "clean", "test", "bench", "impact", "fmt", "edit"};
    if (project_commands.count(argv[1]))
      return moss::run_project_command(argc, argv);

    if (string(argv[1]) == "agent") {
      if (argc < 3) {
        moss::write_structured_error(
            std::cout, "agent", "AGENT_COMMAND_INVALID",
            "expected bootstrap, capabilities, schema, or session-report-template");
        return 2;
      }
      string agent_command = argv[2];
      bool requested_json = false;
      for (int index = 3; index < argc; ++index) {
        if (string(argv[index]) == "--json") requested_json = true;
        else {
          moss::write_structured_error(
              std::cout, "agent " + agent_command,
              "AGENT_ARGUMENT_INVALID",
              "unexpected agent argument '" + string(argv[index]) + "'");
          return 2;
        }
      }
      if (agent_command != "bootstrap" && agent_command != "capabilities" &&
          agent_command != "schema" &&
          agent_command != "session-report-template") {
        moss::write_structured_error(
            std::cout, "agent " + agent_command, "AGENT_COMMAND_INVALID",
            "unknown Moss agent command '" + agent_command + "'");
        return 2;
      }
      if (!requested_json) {
        std::cerr << "moss: agent commands require --json\n";
        return 2;
      }
      moss::write_bootstrap_json(
          std::cout, agent_command,
          nearest_moss_project_root(std::filesystem::current_path()));
      return 0;
    }

    bool check_only = false;
    bool optimize_shared_memory = false;
    bool dump_functional = false;
    bool explain_fusion = false;
    bool debug_build = false;
    bool diagnostic_paths = false;
    string input, output, debug_map_output, native_output;
    string query_command, query_target, query_source;
    int first_argument = 1;
    static const std::set<string> semantic_commands = {
        "inspect", "type", "effects", "ownership", "calls",
        "why", "cost"};
    if (string(argv[1]) == "check") {
      check_only = true;
      active_command = "check";
      first_argument = 2;
    } else if (semantic_commands.count(argv[1])) {
      query_command = argv[1];
      active_command = query_command;
      check_only = true;
      first_argument = 2;
    }
    for (int i = first_argument; i < argc; ++i) {
      string a = argv[i];
      if (a == "--check") check_only = true;
      else if (a == "--json") json_output = true;
      else if (a == "--source") {
        if (++i >= argc) { usage(); return 2; }
        query_source = argv[i];
      }
      else if (a == "--dump-functional-ir") dump_functional = true;
      else if (a == "--explain-fusion") explain_fusion = true;
      else if (a == "--debug") debug_build = true;
      else if (a == "--diagnostic-paths") diagnostic_paths = true;
      else if (a == "--emit-debug-map") {
        if (++i >= argc) { usage(); return 2; }
        debug_map_output = argv[i];
      }
      else if (a == "--native-output") {
        if (++i >= argc) { usage(); return 2; }
        native_output = argv[i];
      }
      else if (a == "-O" || a == "-Oshared-memory" || a == "--optimize-shared-memory")
        optimize_shared_memory = true;
      else if (a == "-O0") optimize_shared_memory = false;

      else if (a == "-o") {
        if (++i >= argc) { usage(); return 2; }
        output = argv[i];
      } else if (a == "-h" || a == "--help") { usage(); return 0; }
      else if (!a.empty() && a.front() == '-') { std::cerr << "moss: unknown option: " << a << "\n"; return 2; }
      else if (!query_command.empty() && query_target.empty()) query_target = a;
      else if (input.empty()) input = a;
      else { std::cerr << "unexpected argument: " << a << "\n"; return 2; }
    }
    if (!query_command.empty() || check_only) {
      if (!json_output) {
        if (!query_command.empty()) {
          std::cerr << "moss: semantic query commands require --json\n";
          return 2;
        }
      }
      if (!query_command.empty() && query_target.empty()) {
        moss::write_structured_error(
            std::cout, query_command, "QUERY_TARGET_INVALID",
            "semantic query requires a target");
        return 2;
      }
      if (!query_command.empty()) input = query_source;
      if (!query_command.empty() && input.empty()) {
        size_t marker = query_target.rfind(".moss:");
        if (marker != string::npos) {
          size_t separator = marker + string(".moss").size();
          string possible_line = query_target.substr(separator + 1);
          if (!possible_line.empty() &&
              std::all_of(possible_line.begin(), possible_line.end(),
                          [](unsigned char ch) { return std::isdigit(ch); })) {
            input = query_target.substr(0, separator);
            query_target = "line:" + possible_line;
          }
        }
      }
      if (!query_command.empty() && input.empty()) {
        moss::write_structured_error(
            std::cout, query_command, "QUERY_SOURCE_REQUIRED",
            "provide --source <input.moss> or use <input.moss>:<line>");
        return 2;
      }
    }
    if (input.empty()) { usage(); return 2; }
    active_input =
        std::filesystem::absolute(input).lexically_normal().string();
    if (active_command == "compile" && check_only) active_command = "check";
    diagnostic_source = diagnostic_paths
        ? std::filesystem::absolute(input).lexically_normal().string()
        : "moss";
    if (debug_build) {
      optimize_shared_memory = false;

    }

    moss::OptimizationPlan plan;
    vector<moss::Warning> warnings;
    bool project_query = false;
    if (!query_command.empty() || check_only) {
      try {
        auto context = moss::analyze_source_context(input);
        if (context.project) {
          auto unit = moss::analyze_project_sources(
              context.manifest, context.sources, optimize_shared_memory,
              debug_build, context.mode);
          active_program = std::move(unit.program);
          plan = std::move(unit.plan);
          warnings = std::move(unit.warnings);
          project_query = true;
        }
      } catch (const moss::ProjectError& error) {
        if (json_output) moss::write_project_error_json(
            std::cout, query_command.empty() ? active_command : query_command, error);
        else std::cerr << (error.source_file.empty() ? diagnostic_source
                                                     : error.source_file)
                          << (error.line > 0 ? ":" + std::to_string(error.line)
                                             : "")
                          << ": error: " << error.what() << "\n";
        return 1;
      }
    }
    if (!project_query) {
      std::ifstream f(input);
      if (!f) {
        if (json_output)
          moss::write_structured_error(
              std::cout, active_command, "SOURCE_NOT_FOUND",
              "cannot open Moss source '" + input + "'", input);
        else
          std::cerr << "moss: cannot open " << input << "\n";
        return 1;
      }
      auto lines = moss::lex_lines(
          f, std::filesystem::absolute(input).lexically_normal().string());
      moss::Parser parser(std::move(lines));
      active_program = parser.parse();
      auto& standalone_program = *active_program;
      moss::Checker checker(standalone_program);
      checker.run();
      warnings = checker.warnings();
      if (!json_output) {
        for (const auto& warning : warnings)
          std::cerr << diagnostic_source << ":" << warning.line
                    << ": warning: " << warning.message << "\n";
      }
      moss::FunctionalOptimizer(standalone_program).run(optimize_shared_memory);
      plan = moss::OptimizationPlan{optimize_shared_memory};
    }
    auto& program = *active_program;
    if (!project_query && (dump_functional || explain_fusion))
      moss::dump_functional_ir(std::cout, program,
                               explain_fusion && !dump_functional);
    if (project_query && (dump_functional || explain_fusion))
      moss::dump_functional_ir(std::cout, program,
                               explain_fusion && !dump_functional);

    // A successful project check seeds the semantic snapshot used by the
    // later impact/affected-test commands.  Query and standalone compilation
    // remain read-only; a malformed or unrelated manifest never masks the
    // ordinary Moss check result.
    if (check_only && query_command.empty()) {
      auto project_root = nearest_moss_project_root(
          std::filesystem::path(active_input));
      if (project_root) {
        try {
          moss::ProjectManifest manifest = moss::load_project_manifest(
              std::filesystem::path(*project_root));
          std::filesystem::path source_path =
              std::filesystem::absolute(input).lexically_normal();
          std::error_code path_error;
          std::filesystem::path relative =
              std::filesystem::relative(source_path, manifest.root,
                                         path_error);
          if (!path_error && !relative.empty() && relative.native()[0] != '.')
            moss::write_semantic_snapshot(
                manifest, source_path,
                moss::analyze_project_snapshot(manifest, source_path));
        } catch (const std::exception&) {
          // Project snapshots are an accelerator, never part of language
          // validity.  The checked program and its diagnostics remain valid.
        }
      }
    }

    if (!query_command.empty())
      return moss::write_semantic_query_json(
                 std::cout, query_command, query_target,
                 std::filesystem::absolute(input).lexically_normal().string(),
                 program, plan, warnings)
          ? 0 : 1;

    if (check_only) {
      if (json_output)
        moss::write_check_json(
            std::cout,
            std::filesystem::absolute(input).lexically_normal().string(),
            warnings, &program);
      else
        std::cout << input << ": ok\n";
      return 0;
    }

    if (output.empty()) {
      auto pos = input.find_last_of('.');
      output = (pos == string::npos ? input : input.substr(0, pos)) + ".rs";
    }
    if (debug_map_output.empty()) {
      std::filesystem::path map_path(output);
      map_path.replace_extension(".mossmap");
      debug_map_output = map_path.string();
    }
    string absolute_input = std::filesystem::absolute(input).lexically_normal().string();
    string absolute_output = std::filesystem::absolute(output).lexically_normal().string();
    string absolute_native = native_output.empty()
        ? string()
        : std::filesystem::absolute(native_output).lexically_normal().string();

    moss::Generator gen(program, plan, debug_build);
    string rust = gen.generate();
    std::ofstream out(output);
    if (!out) { std::cerr << "moss: cannot write " << output << "\n"; return 1; }
    out << rust;
    out.close();
    std::ofstream map_out(debug_map_output);
    if (!map_out) {
      std::cerr << "moss: cannot write " << debug_map_output << "\n";
      return 1;
    }
    moss::write_debug_map(
        map_out,
        moss::build_debug_map(program, rust, absolute_input, absolute_output,
                              absolute_native, debug_build,
                              optimize_shared_memory));
    std::cout << "generated " << output << "\n";
    std::cout << "generated " << debug_map_output << "\n";
    return 0;
  } catch (const moss::CompileError& e) {
    if (json_output) {
      string identity = !e.semantic_identity.empty()
          ? e.semantic_identity
          : moss::semantic_identity_near_line(
                active_program ? &*active_program : nullptr, e.line);
      moss::write_structured_error(
          std::cout, active_command,
          e.code.empty() ? moss::diagnostic_code_for_message(e.what()) : e.code,
          e.what(), active_input, e.line, identity, e.symbol);
    } else {
      std::cerr << diagnostic_source << ":" << e.line
                << ": error: " << e.what() << "\n";
    }
    return 1;
  } catch (const std::exception& e) {
    if (json_output)
      moss::write_structured_error(
          std::cout, active_command, "MOSS_INTERNAL_OR_IO_ERROR", e.what(),
          active_input);
    else
      std::cerr << "moss: error: " << e.what() << "\n";
    return 1;
  }
}
