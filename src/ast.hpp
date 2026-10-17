#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "constraints.hpp"
#include "functional_ir.hpp"
#include "synchronization.hpp"
#include "type_ir.hpp"

namespace moss {

struct TypedSemanticModuleV1;

using std::string;
using std::vector;

// The checked ownership rule shared by the checker, native lowering, and
// Fast Debug. Callers may pass canonical names or source primitive spellings.
inline bool copy_type_name(string type) {
  auto first = type.find_first_not_of(" \t\n\r");
  if (first == string::npos) return false;
  auto last = type.find_last_not_of(" \t\n\r");
  type = type.substr(first, last - first + 1);
  if (type == "int" || type == "Int" || type == "float" ||
      type == "Float" || type == "bool" || type == "Bool") return true;
  if (type.size() > 8 && type.compare(0, 7, "option[") == 0 &&
      type.back() == ']')
    return copy_type_name(type.substr(7, type.size() - 8));
  return false;
}

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
  bool exported = false;
};
struct Param {
  string name, type;
  // True when the concrete type was inferred from an untyped parameter.
  bool inferred = false;
};
// A call that instantiates an open result has its own inference variables.
// These checked facts survive body closure for specialization registration and
// semantic export. Ordinals refer to the callee's result variables, not the
// caller's local binding names.
struct OpenCallResultFacts {
  string expression_id;
  string callable_id;
  TypeRef result;
  vector<std::pair<std::uint32_t, TypeRef>> ordered_bindings;
};
// A reusable open callable type scheme. Summary variables use session zero;
// each call substitutes them with fresh variables in its caller's solver.
struct OpenCallableSummaryFacts {
  string callable_id;
  std::uint32_t bound_variable_count = 0;
  vector<TypeRef> parameter_types;
  TypeRef result_type;
  vector<Requirement> requirements;
  vector<MemberDispatchRequirement> member_requirements;
  vector<string> semantic_dependencies;
  vector<AccessMode> parameter_access;
  ObservableEffects observable_effects;
  RequirementOrigin origin;
  bool complete = false;
};
struct Stmt {
  enum class Kind {
    Raw, Pass, Assign, Call, Message, Echo, If, Else, While, For, Match, Case,
    Let, Var, Reply, Return,
    // Phase 21 exceptional control-flow is represented explicitly.  These
    // nodes are consumed by the same semantic walkers as ordinary branches;
    // code generation never rediscovers them from source text.
    Try, Recover, Raise
  } kind = Kind::Raw;
  int line = 0; int indent = 0; string text, a, b; vector<string> args;
  // A synchronous message may be used as an expression initializer.  The
  // receiver/handler remain in `a`/`b` (the canonical domain-call slots),
  // while this records the optional destination binding.  Keeping this on
  // the checked statement preserves one synchronous invocation.
  string message_result;
  vector<int> continuation_lines;
  string semantic_type;
  std::unordered_map<string, OpenCallResultFacts> open_call_results_by_context;
  // Exact checked specialization selected for an open-result call in this
  // body context. Native lowering consumes this key at the source statement.
  std::unordered_map<string, string> open_call_keys_by_context;
  // Nested call results can close from an enclosing parameter type. Preserve
  // each expression's exact checked key for native lowering in that context.
  std::unordered_map<string, std::unordered_map<string, string>>
      nested_open_call_keys_by_context;
  // Concrete receiver after collection element inference, keyed by the exact
  // callable/specialization context. Native lowering projects this checked fact.
  std::unordered_map<string,string> receiver_types_by_context;
  // A generic body can be checked at several concrete call sites. Preserve
  // the inferred binding type for each checked specialization.
  std::unordered_map<string,string> semantic_types_by_context;
  // Types that remain definite after this control-flow statement. Concrete
  // entries let the backend hoist bindings created on every incoming path.
  // This is checker-to-backend metadata, never Moss source syntax.
  std::unordered_map<string,string> joined_types;
  // A generic function's shared source statement is checked once per
  // concrete specialization. Keep each exact join environment separately.
  std::unordered_map<string,std::unordered_map<string,string>>
      joined_types_by_context;
  // Exact functional plans for the expression slots emitted by this
  // statement, keyed by the concrete semantic context.  A function body can
  // have several static specializations, so one AST statement can legitimately
  // point at several distinct compilation-local pipeline IDs.
  mutable std::unordered_map<string,vector<std::size_t>> functional_pipeline_ids;
  // Phase 20 Agent D: nonzero once check_executor_invoke has built a checked
  // RootSubmissionPlan for this `executor.invoke(...)` statement (index into
  // Program::root_submission_plans, offset by one so 0 means "none"). See
  // executor_invoke_lowering.inc / executor_invoke_codegen.inc.
  mutable std::size_t root_submission_plan_id = 0;
  // Phase 20 Agent D: nonzero once the checker has built an ExecutorStartPlan
  // for this `name = Executor()...start()` statement (index into
  // Program::executor_start_plans, offset by one).
  mutable std::size_t executor_start_plan_id = 0;
  // Phase 20 Agent D: set by the checker on statements that execute directly
  // in `main`. A `message` evaluated by such a statement is root ingress
  // (an independent synchronous Root through unified admission, sec. 5),
  // not a nested message inside an existing Root. Codegen and Fast Debug
  // select their lowering from this checked fact, never from source text.
  mutable bool message_root_ingress = false;
  // Checked Phase 21 exceptional IR.  `exceptional_alternatives` is the
  // complete normalized set produced by this source node after concrete
  // resolution; the node/scope identities feed ABI-v8 CFG verification and
  // every downstream consumer.
  RaiseSet exceptional_alternatives;
  std::uint32_t exceptional_node = 0;
  std::uint32_t exceptional_scope = 0;
  // Exact whole owned bindings consumed while checking this statement.
  // Native recovery lowering uses these facts across variant-specific edges.
  mutable std::set<string> ownership_consumed_bindings;
  string source_file;
};

struct FailureArm {
  int line = 0;
  string source_file;
  // Canonical source patterns (for example `FileError.Full`).  An empty list
  // denotes the final catch-all.  `binding` is present only for the approved
  // single-concrete-error-type catch-all form.
  vector<string> patterns;
  string binding;
  // Reachable identities selected from the owning handler's fixed-point
  // raise set. Populated by the checker; code generation must not reconstruct
  // reachability from source spelling.
  RaiseSet exceptional_alternatives;
  // D7-proved owned handler locals read by this Root-only continuation.
  // Populated from every escaping source-site ownership snapshot.
  struct Capture { string name, type, binding_identity; };
  mutable vector<Capture> captures;
  // Checked pseudo-handler footprint exported for a compiled provider's
  // fresh Root failure interval. This is distinct from the normal handler.
  std::optional<StateLeafEffects> state_effects;
  vector<Stmt> body;
};
// ABI-v9 diagnostic provenance. A witness is emitted only for a checked
// no-op catch-all whose selected raise can follow these formal WRITE leaves.
// The consumer maps formal leaves to protected actual storage at a call site.
struct RecoveryWarningWitness {
  int line = 0;
  bool on_fail = false;
  RaiseSet selected;
  StateLeafSet formal_writes;
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
  vector<RecoveryWarningWitness> recovery_warning_witnesses;
  int line = 0;
  string source_file;
};
struct TraitMethod { string name; vector<Param> params; std::optional<string> return_type; int line = 0; string source_file; };
struct Handler {
  string name, header;
  vector<Param> params;
  std::optional<string> reply_type;
  vector<Stmt> body;
  vector<FailureArm> failure_arms;
  ObservableEffects observable_effects;
  mutable vector<RecoveryWarningWitness> recovery_warning_witnesses;
  int line = 0;
  string source_file;
  // Checked semantic state effects, also carried by compiled providers.
  std::optional<StateLeafEffects> state_effects;
  // Concrete provider-owned raised carrier when an on_fail arm captures a
  // handler local. Empty handlers keep the ordinary __MossRaised ABI.
  mutable string phase21_capture_carrier_type;
  // ABI-v8 provider metadata. Source handlers receive a deterministic empty
  // contract during interface emission; source-free handlers must carry the
  // complete record or loading fails closed.
  std::optional<ProviderHandlerAbi> provider_abi;
};
struct DomainRoute {
  string name, type;
  int line = 0;
  string source_file;
};
// Nominal declarations keep their defining module spelling ("" for the root
// of an unmodular program) and source declaration name, set where module
// rewriting qualifies `name`. Identity is derived from these, never by
// splitting the qualified spelling (TYPE_IR_CONTRACT §3.1).
struct Domain { string name, header; vector<Field> state; vector<DomainRoute> routes; vector<Handler> handlers; bool exported = false; int line = 0; string source_file; string defining_module; string declaration_name; };
struct ObjectType { string name, header; vector<Field> fields; vector<Method> methods; bool exported = false; int line = 0; string source_file; string defining_module; string declaration_name; };
struct EnumCase { string name; vector<Field> fields; int line = 0; string source_file; };
struct EnumType { string name, header; vector<EnumCase> cases; bool exported = false; int line = 0; string source_file; string defining_module; string declaration_name; };
struct MainProc { vector<Stmt> body; int line = 0; string header; string source_file; };
struct StaticSpecializationDependency {
  // Canonical Moss callable identity and concrete parameter types.  This is
  // checked specialization metadata, not a backend symbol reference.
  string function_name;
  vector<string> parameter_types;
  string specialization_key;
};
struct FunctionSpecialization {
  string generated_name;
  vector<string> parameter_types;
  string return_type;
  // The complete input to specialization-v2. In particular, result bindings
  // distinguish calls with equal parameters whose open results close apart.
  SpecializationKey semantic_key;
  // The checked semantic identity; text above is only an ABI/display projection.
  vector<TypeRef> parameter_type_refs;
  TypeRef return_type_ref;
  string stable_type_key;
  // A direct return of another open-result callable selects that callee's
  // exact checked instance after this specialization's result closes.
  string result_call_key;
  // Concrete observable effects drive the native Result ABI of this static
  // specialization. A duck-typed method may raise even when the unspecialized
  // function body cannot resolve that method's exact variant.
  ObservableEffects observable_effects;
  // Ownership is checked in this complete concrete parameter environment.
  vector<Effect> parameter_effects;
  vector<bool> parameter_mutations;
  // Static calls observed while checking this concrete body.  Artifact
  // projection closes this graph so an owned specialization never references
  // a specialization that exists only in some other artifact.
  vector<StaticSpecializationDependency> dependencies;
  // Closed member requirements checked for this exact static instance.
  vector<MemberDispatchRequirement> member_requirements;
};

struct DomainSpecialization {
  string source_domain;
  string instance;
  // Typed instances also have an exact semantic record, but can reuse the
  // source declaration's already-concrete backend layout.
  bool materialized_layout = true;
  string identity() const {
    return "domain-specialization:" + source_domain + ":" + instance;
  }
  std::unordered_map<string,string> state_types;
  // Handler parameter specializations are indexed by parameter slot, not by a
  // flat call history. One concrete type per handler parameter slot:
  // handler_parameter_types[handler][index].
  std::unordered_map<string,vector<string>> handler_parameter_types;
  std::unordered_map<string,string> handler_reply_types;
};
struct Function {
  string name, header; vector<Param> params; std::optional<string> return_type; vector<Stmt> body;
  // Bound at module name resolution, before checking. Source-free functions
  // retain the same defining package/module/declaration identity.
  string defining_module, declaration_name, semantic_callable_id;
  std::optional<string> result_expression; int result_line = 0; bool expression_body = false;
  vector<int> result_continuation_lines;
  std::unordered_map<string,std::size_t> result_functional_pipeline_ids;
  bool generic = false; bool static_dispatch = false;
  std::unordered_map<string,string> generic_results; int line = 0;
  vector<Constraint> constraints;
  vector<FunctionSpecialization> specializations;
  std::optional<OpenCallableSummaryFacts> open_summary;
  // Concrete callable specializations referenced by functional code.  This is
  // retained as semantic dependency information for later incremental work.
  vector<string> callable_dependencies;
  bool exported = false;
  ObservableEffects observable_effects;
  vector<RecoveryWarningWitness> recovery_warning_witnesses;
  // Inferred parameter effects, parallel to `params`.
  vector<Effect> parameter_effects;
  // A parameter may be both written and consumed. The joined effect is
  // CONSUME, while native lowering still needs the checked WRITE fact to
  // declare its owned binding mutable.
  vector<bool> parameter_mutations;
  std::optional<StateLeafEffects> parameter_leaf_effects;
  string source_file;
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
// and concrete routing.  They are retained so tooling can inspect the exact
// facts used by the compiler without walking the AST a second time.
struct SemanticCallEdge {
  string source;
  string target;
  int line = 0;
  vector<string> argument_types;
  // Physical source provenance for cross-file project tooling.  The callable
  // context above remains the semantic identity used by the checker.
  string source_file;
  // Checked invocation classification.  Message edges are recorded from the
  // validated Stmt::Message receiver/type pair, never reconstructed from
  // generated code.
  string invocation_kind = "ordinary_call";
  string receiver;
  // A generic source statement can be checked under several concrete caller
  // instances. This key keeps those call edges distinct after closure.
  string source_specialization_key;
  // Set by the checked call closure. Query and module projection consume this
  // exact identity instead of reconstructing it from display argument types.
  string specialization_key;
  // Source-order occurrence among calls with the same caller, line, and
  // target. This distinguishes two expressions on one source line without
  // changing the callable or specialization identity.
  uint32_t occurrence = 0;
};

// Minimal resolved-use facts retained by the ordinary checker for semantic
// tooling.  `target_identity` names the declaration selected by normal name
// or type resolution; tooling must never reconstruct that selection from
// source text.
struct SemanticUse {
  string target_identity;
  string kind;
  string source_file;
  int line = 0;
  string enclosing_identity;
  string call_site_identity;
};





// Whole-program concrete routing topology.  These identities are semantic
// compiler facts; generated Rust names must not be used as their identity.
struct ConcreteDomainInstance {
  string identity;
  string binding;
  string domain;
  string specialization;
  // Explicit reference into Program::domain_specializations. Populated for
  // every checked concrete instance; never a generated Rust type spelling.
  std::optional<size_t> specialization_index;
  size_t source_domain_index = 0;
  string source_file;
  int line = 0;
  int domain_rank = -1;
};
struct ConcreteRouteEdge {
  string source_instance;
  string route;
  string target_instance;
  string source_file;
  int line = 0;
};
struct ConcreteDomainGraph {
  string identity;
  vector<ConcreteDomainInstance> instances;
  vector<ConcreteRouteEdge> edges;
  // Set only after all executable bodies pass routing-capability checks.
  bool closed = false;
};

struct ModuleImport {
  string name;
  string owner_module;
  int line = 0;
  string source_file;
};

// Phase 20 Agent D: the checked, compiler-owned plan for one
// `executor.invoke(concrete_domain.Handler(args...))` statement. This is
// compiler IR -- the semantic facts a root submission needs -- not the
// runtime `RootDescriptor` itself (docs/ROOT_RUNTIME_ABI.md
// "RootDescriptor representation" remains Agent C's to define physically).
// Built once by executor_invoke_lowering.inc's check_executor_invoke and
// consumed as-is by executor_invoke_codegen.inc; codegen never re-derives
// these facts from source text.
struct RootSubmissionPlan {
  string executor_binding;
  string target_domain_binding;
  string target_domain_semantic_identity;
  string handler_semantic_identity;
  string handler_name;
  vector<string> argument_expressions;
  vector<string> argument_types;
  // Always true in Phase 20: executor.invoke never admits a value-returning
  // handler (docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md sec. 4.3).
  bool one_way = true;
};

// Phase 20 Agent D: the checked configuration chain of one
// `name = Executor().threads(..)...start()` statement. Calls are kept in
// source order so codegen evaluates each argument exactly once, in that
// order, before `start()` (docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md sec.
// 3). Built by executor_invoke_lowering.inc, consumed by
// executor_invoke_codegen.inc.
struct ExecutorConfigCall {
  string method;        // threads | max_threads | queue_capacity | affinity | priority
  string argument;      // checked Moss expression, evaluated once at start
  string argument_type; // canonical checked type (int, or vector[int] for affinity)
};
struct ExecutorStartPlan {
  string binding;
  vector<ExecutorConfigCall> calls;
};

struct Program {
  // Compiler-internal typed projection of checked facts, populated once by
  // Checker::run. ABI serializers consume this rather than diagnostic text.
  std::shared_ptr<TypedSemanticModuleV1> typed_semantics;
  // Parsed physical source lines retained for compiler-owned editor
  // resolution.  They preserve indentation and file identity, while all
  // semantic selection still comes from checked declarations/uses/calls.
  vector<Line> source_lines;
  string module_name;
  string main_module;
  bool explicit_module = false;
  // Providers loaded from a compiled .mossi/.rlib pair.  Their declarations
  // participate in Moss name/type checking, but are never regenerated into
  // this build's Rust crates.
  std::set<string> external_modules;
  // ABI module identity (`module-v1:<package>@<version>::<module>`) of each
  // module spelling in this program; "" is the root of an unmodular program.
  // Local modules take the package manifest; loaded providers take their
  // interface's identity (TYPE_IR_CONTRACT §3.1).
  std::map<string, string> module_identities;
  // True when module identities come from a package manifest; a missing
  // identity is then an error, never the standalone fallback.
  bool package_identities = false;
  // The session declaration table, built by the checker. It interns every
  // record/enum/domain identity and outlives the checker with the program.
  std::shared_ptr<TypeContext> type_context;
  vector<ModuleImport> imports;
  vector<Function> functions;
  vector<Trait> traits;
  vector<ObjectType> objects;
  vector<EnumType> enums;
  vector<Domain> domains;
  vector<TestDecl> tests;
  vector<BenchDecl> benchmarks;
  std::optional<MainProc> main;
  vector<FunctionalPipeline> functional_pipelines;
  vector<FunctionalTraversalGroup> functional_traversal_groups;
  vector<SemanticCallEdge> semantic_call_edges;
  vector<SemanticUse> semantic_uses;
  // Per-declared-instance facts for source domains whose untyped state and
  // handler parameters were inferred at concrete call sites.
  vector<DomainSpecialization> domain_specializations;
  vector<RootSubmissionPlan> root_submission_plans;
  vector<ExecutorStartPlan> executor_start_plans;
  ConcreteDomainGraph concrete_domain_graph;
  SynchronizationPlan synchronization_plan;
};

inline string functional_function_context(
    const Function& function,
    const FunctionSpecialization* specialization = nullptr) {
  if (!specialization) return "fn:" + function.name;
  string context = "fn:" + function.name + "<";
  for (size_t index = 0; index < specialization->parameter_types.size(); ++index) {
    if (index) context += ",";
    context += specialization->parameter_types[index];
  }
  return context + ">";
}

inline string functional_method_context(const Method& method) {
  return "method:" + method.owner + "." + method.name;
}

inline string functional_method_context(const ObjectType&, const Method& method) {
  return functional_method_context(method);
}

inline string functional_handler_context(const Domain& domain,
                                         const Handler& handler) {
  return "handler:" + domain.name + "." + handler.name;
}

} // namespace moss
