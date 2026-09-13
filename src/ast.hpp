#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "constraints.hpp"
#include "functional_ir.hpp"

namespace moss {

using std::string;
using std::vector;

struct Line {
  int no = 0;
  int indent = 0;
  string text;
  vector<int> continuation_lines;
};
struct Field { string name, type, init, header; int line = 0; };
struct Param { string name, type; };
struct Stmt {
  enum class Kind { Raw, Assign, Call, Message, Echo, If, Else, While, Let, Var, AwaitMessage, Reply, Return } kind = Kind::Raw;
  int line = 0; int indent = 0; string text, a, b, c; vector<string> args;
  vector<int> continuation_lines;
  bool is_mutable = false; bool declaration = true; string semantic_type;
  // Types that remain definite after this control-flow statement. Concrete
  // entries let the backend hoist bindings created on every incoming path.
  // This is checker-to-backend metadata, never Moss source syntax.
  std::unordered_map<string,string> joined_types;
  // Exact functional plans for the expression slots emitted by this
  // statement, keyed by the concrete semantic context.  A function body can
  // have several static specializations, so one AST statement can legitimately
  // point at several distinct compilation-local pipeline IDs.
  mutable std::unordered_map<string,vector<std::size_t>> functional_pipeline_ids;
};
struct Method {
  string owner, name;
  vector<Param> params;
  std::optional<string> return_type;
  vector<Stmt> body;
  std::optional<string> result_expression;
  int result_line = 0;
  vector<int> result_continuation_lines;
  std::unordered_map<string,std::size_t> result_functional_pipeline_ids;
  // Inferred receiver/parameter effects used by ownership checking and Rust
  // lowering.  They are never written in Moss source.
  Effect receiver_effect = Effect::Read;
  vector<Effect> parameter_effects;
  ObservableEffects observable_effects;
  int line = 0;
};
struct TraitMethod { string name; vector<Param> params; std::optional<string> return_type; int line = 0; };
struct Handler { string name, header; vector<Param> params; std::optional<string> reply_type; vector<Stmt> body; int line = 0; };
struct Domain { string name, header; vector<Field> state; vector<Handler> handlers; int line = 0; };
struct ObjectType { string name, header; vector<Field> fields; vector<Method> methods; int line = 0; };
struct MainProc { vector<Stmt> body; int line = 0; string header; };
struct FunctionSpecialization {
  string generated_name;
  vector<string> parameter_types;
  string return_type;
};
struct Function {
  string name, header; vector<Param> params; std::optional<string> return_type; vector<Stmt> body;
  std::optional<string> result_expression; int result_line = 0; bool expression_body = false;
  vector<int> result_continuation_lines;
  std::unordered_map<string,std::size_t> result_functional_pipeline_ids;
  bool generic = false; bool static_dispatch = false;
  std::unordered_map<string,string> generic_results; int line = 0;
  vector<Constraint> constraints;
  vector<FunctionSpecialization> specializations;
  // Concrete callable specializations referenced by functional code.  This is
  // retained as semantic dependency information for later incremental work.
  vector<string> callable_dependencies;
  ObservableEffects observable_effects;
  // Inferred parameter effects, parallel to `params`.
  vector<Effect> parameter_effects;
};
struct Trait { string name, header; vector<TraitMethod> methods; int line = 0; };
struct Program {
  vector<Function> functions;
  vector<Trait> traits;
  vector<ObjectType> objects;
  vector<Domain> domains;
  std::optional<MainProc> main;
  vector<FunctionalPipeline> functional_pipelines;
};

} // namespace moss
