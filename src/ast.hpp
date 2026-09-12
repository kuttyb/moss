#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "constraints.hpp"

namespace moss {

using std::string;
using std::vector;

struct Line { int no = 0; int indent = 0; string text; };
struct Field { string name, type, init, header; int line = 0; };
struct Param { string name, type; };
struct Stmt {
  enum class Kind { Raw, Assign, Call, Message, Echo, If, Else, While, Let, Var, AwaitMessage, Reply, Return } kind = Kind::Raw;
  int line = 0; int indent = 0; string text, a, b, c; vector<string> args;
  bool is_mutable = false; bool declaration = true;
};
struct Handler { string name, header; vector<Param> params; std::optional<string> reply_type; vector<Stmt> body; int line = 0; };
struct Domain { string name, header; vector<Field> state; vector<Handler> handlers; int line = 0; };
struct ObjectType { string name, header; vector<Field> fields; int line = 0; };
struct MainProc { vector<Stmt> body; int line = 0; string header; };
struct Function {
  string name, header; vector<Param> params; std::optional<string> return_type; vector<Stmt> body;
  std::optional<string> result_expression; int result_line = 0; bool expression_body = false;
  bool generic = false; std::unordered_map<string,string> generic_results; int line = 0;
  vector<Constraint> constraints;
};
struct TraitMethod { string name; vector<Param> params; std::optional<string> return_type; int line = 0; };
struct Trait { string name, header; vector<TraitMethod> methods; int line = 0; };
struct Program { vector<Function> functions; vector<Trait> traits; vector<ObjectType> objects; vector<Domain> domains; std::optional<MainProc> main; };

} // namespace moss
