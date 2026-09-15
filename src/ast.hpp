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
  // Physical source path retained when several files are linked into one
  // project compilation unit.
  string source_file;
};
struct Field {
  string name, type, init, header;
  int line = 0;
  string source_file;
  // True when the concrete type was inferred from an untyped declaration.
  // The source declaration remains the specialization template; this bit
  // lets semantic consumers distinguish it from an explicitly typed field.
  bool inferred = false;
};
struct Param {
  string name, type;
  // True when the concrete type was inferred from an untyped parameter.
  bool inferred = false;
};
struct Stmt {
  enum class Kind { Raw, Assign, Call, Message, Echo, If, Else, While, For, Let, Var, AwaitMessage, Reply, Return } kind = Kind::Raw;
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
  string source_file;
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
  string source_file;
};
struct TraitMethod { string name; vector<Param> params; std::optional<string> return_type; int line = 0; string source_file; };
struct Handler {
  string name, header;
  vector<Param> params;
  std::optional<string> reply_type;
  vector<Stmt> body;
  ObservableEffects observable_effects;
  int line = 0;
  string source_file;
};
struct Domain { string name, header; vector<Field> state; vector<Handler> handlers; bool exported = false; int line = 0; string source_file; };
struct ObjectType { string name, header; vector<Field> fields; vector<Method> methods; bool exported = false; int line = 0; string source_file; };
struct MainProc { vector<Stmt> body; int line = 0; string header; string source_file; };
struct FunctionSpecialization {
  string generated_name;
  vector<string> parameter_types;
  string return_type;
};
struct AwaitBoundary {
  size_t parameter_index = 0;
  string handler;
  string domain;
  int line = 0;
};
struct DomainSpecialization {
  string source_domain;
  string instance;
  std::unordered_map<string,string> state_types;
  // Handler parameter specializations are indexed by parameter slot, not by a
  // flat call history. One concrete type per handler parameter slot:
  // handler_parameter_types[handler][index].
  std::unordered_map<string,vector<string>> handler_parameter_types;
  std::unordered_map<string,string> handler_reply_types;
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
  bool exported = false;
  ObservableEffects observable_effects;
  // Inferred parameter effects, parallel to `params`.
  vector<Effect> parameter_effects;
  string source_file;
  vector<AwaitBoundary> await_boundaries;
};
struct Trait { string name, header; vector<TraitMethod> methods; bool exported = false; int line = 0; string source_file; };

struct TestDecl {
  string name;
  string header;
  string semantic_identity;
  vector<Stmt> body;
  int line = 0;
  string source_file;
};

struct BenchDecl {
  string name;
  string header;
  string semantic_identity;
  vector<Stmt> body;
  int line = 0;
  string source_file;
};

// Existing checker passes populate these records while validating recursion
// and await boundedness.  They are retained so tooling can inspect the exact
// facts used by the compiler without walking the AST a second time.
struct SemanticCallEdge {
  string source;
  string target;
  int line = 0;
  vector<string> argument_types;
  // Physical source provenance for cross-file project tooling.  The callable
  // context above remains the semantic identity used by the checker.
  string source_file;
};

struct SemanticAwaitSite {
  string source;
  string target_domain;
  int line = 0;
  string source_instance;
  string target_instance;
};

struct SemanticAwaitEdge {
  string source_domain;
  string target_domain;
  int line = 0;
  string source_instance;
  string target_instance;
};

struct ModuleImport {
  string name;
  string owner_module;
  int line = 0;
  string source_file;
};

struct Program {
  string module_name;
  string main_module;
  bool explicit_module = false;
  // Providers loaded from a compiled .mossi/.rlib pair.  Their declarations
  // participate in Moss name/type checking, but are never regenerated into
  // this build's Rust crates.
  std::set<string> external_modules;
  vector<ModuleImport> imports;
  vector<Function> functions;
  vector<Trait> traits;
  vector<ObjectType> objects;
  vector<Domain> domains;
  vector<TestDecl> tests;
  vector<BenchDecl> benchmarks;
  std::optional<MainProc> main;
  vector<FunctionalPipeline> functional_pipelines;
  vector<FunctionalTraversalGroup> functional_traversal_groups;
  vector<SemanticCallEdge> semantic_call_edges;
  vector<SemanticAwaitSite> semantic_await_sites;
  vector<SemanticAwaitEdge> semantic_await_edges;
  // Per-declared-instance facts for source domains whose untyped state and
  // handler parameters were inferred at concrete call sites.
  vector<DomainSpecialization> domain_specializations;
};

} // namespace moss
