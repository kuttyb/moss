#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "constraints.hpp"
#include "diagnostics.hpp"
#include "functional_ir.hpp"
#include "synchronization.hpp"
#include "type_ir.hpp"
#include "est/expression_tree.hpp"

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
      type == "Float" || type == "bool" || type == "Bool" ||
      type == "char" || type == "Char") return true;
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
  vector<est::Handle> expression_handles;
};
struct Param {
  string name, type;
  // True when the concrete type was inferred from an untyped parameter.
  bool inferred = false;
  TypeRef type_ref = nullptr;
  Param() = default;
  Param(string n, string t, bool inf = false)
      : name(std::move(n)), inferred(inf) {
    set_type(std::move(t));
  }
  Param(string n, TypeRef ref, bool inf = false)
      : name(std::move(n)), inferred(inf) {
    bind_type(std::move(ref));
  }
  void bind_type(TypeRef term) {
    type_ref = std::move(term);
    if (type_ref) type = type_ref->to_canonical_string();
    else type.clear();
  }
  void set_type(string raw) {
    type = std::move(raw);
    if (!type.empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(type);
      if (parsed && (*parsed)->is_closed()) {
        type_ref = *parsed;
        return;
      }
    }
    type_ref = nullptr;
  }
  void sync() {
    if (type_ref) {
      type = type_ref->to_canonical_string();
    } else if (!type.empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(type);
      if (parsed && (*parsed)->is_closed()) type_ref = *parsed;
    }
  }
  void assert_consistent() const {
    if (type_ref && type != type_ref->to_canonical_string()) {
      throw std::logic_error("Param '" + name + "' type string projection '" +
                             type + "' contradicts bound TypeRef '" +
                             type_ref->to_canonical_string() + "'");
    }
  }
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
  struct OperationFact {
    struct TypeRelationFact {
      enum class Kind { SameType };
      Kind kind = Kind::SameType;
      TypeRef left;
      TypeRef right;
      std::uint32_t left_operand_ordinal = UINT32_MAX;
      std::uint32_t right_operand_ordinal = UINT32_MAX;
    };
    string operation_id;
    TypeRef receiver;
    vector<TypeRef> arguments;
    TypeRef result;
    int line = 0;
    // Checker-local links into BodyTypeState's occurrence graph. These values
    // are not part of ABI identity; the checked-body encoder assigns canonical
    // dense ordinals after it has a complete tree.
    std::uint32_t expression_occurrence_id = UINT32_MAX;
    std::uint32_t occurrence_ordinal = 0;
    // Exact checker-derived value-flow dependencies. Control-flow joins can
    // give one operation multiple incoming operation dependencies.
    vector<std::uint32_t> dependency_occurrence_ids;
    // Source-order operand ordinals for operation families whose receiver and
    // arguments are selected from a variable-arity occurrence (for example,
    // vector literals and calls). UINT32_MAX means the family has no such
    // source-operand mapping.
    std::uint32_t receiver_operand_ordinal = UINT32_MAX;
    vector<std::uint32_t> argument_operand_ordinals;
    // Deferred checker constraints that relate exact source operands without
    // prematurely unifying distinct open generic parameters. Consumers must
    // preserve these relations through specialization and body lowering.
    vector<TypeRelationFact> type_relations;
  };
  // Checker-retained builtin operations used to close an otherwise open
  // generic contract. These are semantic facts, not serialized body IR.
  vector<OperationFact> operations;
  vector<MemberDispatchRequirement> member_requirements;
  vector<string> semantic_dependencies;
  // Open callable effects may depend on an exact statically selected callback
  // requirement. These IDs remain checker-owned until ABI encoding assigns
  // canonical requirement ordinals.
  vector<string> effect_requirement_ids;
  vector<AccessMode> parameter_access;
  ObservableEffects observable_effects;
  RequirementOrigin origin;
  // `scheme_complete` makes a generic signature and its requirements
  // available to typed queries and ordinary call checking before whole-body
  // effects, dependency closure, and occurrence coverage are known. `complete`
  // requires all those facts. ABI-v10 body admission additionally requires
  // common checked-body bytes. A typed scheme can therefore remain queryable
  // when its checked body is not yet representable, while source-free body
  // consumers still fail closed.
  bool scheme_complete = false;
  bool complete = false;
  bool occurrence_ledger_complete = false;
};
// Body analysis may allocate a provisional result variable before discovering
// that the result aliases a parameter. Discard only unused trailing ordinals;
// an interior hole is noncanonical and must not be silently renumbered.
inline bool close_open_summary_ordinals(OpenCallableSummaryFacts& summary) {
  vector<bool> used(summary.bound_variable_count, false);
  bool valid = true;
  auto visit = [&](const auto& self, const TypeRef& type) -> void {
    if (!type) return;
    if (type->kind == TypeKind::InferenceVar) {
      const auto& id = type->var_id;
      if (id.session_id || id.owner_id ||
          id.ordinal >= summary.bound_variable_count) valid = false;
      else used[id.ordinal] = true;
    }
    for (const auto& argument : type->type_args) self(self, argument);
    for (const auto& field : type->field_substitutions)
      self(self, field.second);
  };
  for (const auto& type : summary.parameter_types) visit(visit, type);
  visit(visit, summary.result_type);
  for (const auto& operation : summary.operations) {
    visit(visit, operation.receiver);
    for (const auto& argument : operation.arguments) visit(visit, argument);
    visit(visit, operation.result);
  }
  for (const auto& requirement : summary.requirements) {
    visit(visit, requirement.subject);
    for (const auto& argument : requirement.arguments) visit(visit, argument);
    if (requirement.result) visit(visit, *requirement.result);
  }
  for (const auto& member : summary.member_requirements) {
    visit(visit, member.receiver);
    for (const auto& argument : member.argument_types) visit(visit, argument);
    if (member.result) visit(visit, *member.result);
  }
  while (!used.empty() && !used.back()) used.pop_back();
  if (!valid || std::find(used.begin(), used.end(), false) != used.end())
    return false;
  summary.bound_variable_count = static_cast<std::uint32_t>(used.size());
  return true;
}
// A versioned, checked semantic body subset. Every published node has a
// resolved operand identity and TypeRef. Other source forms have no body hash.
struct CheckedBodyExprV1 {
  enum class Kind {
    Parameter,
    Integer,
    Add,
    Field,
    VectorLiteral,
    Call
  } kind;
  TypeRef type;
  std::uint32_t parameter_ordinal = 0;
  string integer_value;
  vector<CheckedBodyExprV1> operands;
  string semantic_target_id;
  std::uint32_t member_ordinal = 0;
};
struct CheckedBodyStmtV1 {
  enum class Kind { Return } kind;
  CheckedBodyExprV1 value;
};
struct CheckedBodyV1 {
  vector<CheckedBodyStmtV1> statements;
  string semantic_hash;
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
  vector<est::Handle> expression_handles;
  std::optional<est::RawForm> est_raw_form;
  // A synchronous message may be used as an expression initializer.  The
  // receiver/handler remain in `a`/`b` (the canonical domain-call slots),
  // while this records the optional destination binding.  Keeping this on
  // the checked statement preserves one synchronous invocation.
  string message_result;
  vector<int> continuation_lines;
  string semantic_type;
  std::unordered_map<string, OpenCallResultFacts> open_call_results_by_context;
  // Solver-closed member facts for the direct call in this statement. The
  // specialization registrar consumes these exact terms and access decisions.
  std::unordered_map<string, vector<MemberDispatchRequirement>>
      member_requirements_by_context;
  // Exact checked specialization selected for an open-result call in this
  // body context. Native lowering consumes this key at the source statement.
  std::unordered_map<string, string> open_call_keys_by_context;
  // Nested call results can close from an enclosing parameter type. Preserve
  // each expression's exact checked key for native lowering in that context.
  std::unordered_map<string, std::unordered_map<string, vector<string>>>
      nested_open_call_keys_by_context;
  // Concrete receiver after collection element inference, keyed by the exact
  // callable/specialization context. Native lowering projects this checked fact.
  std::unordered_map<string,string> receiver_types_by_context;
  // A generic body can be checked at several concrete call sites. Preserve
  // the inferred binding type for each checked specialization.
  std::unordered_map<string,string> semantic_types_by_context;
  // `echo` argument types the checker accepted under the display contract,
  // by specialization context ("" for a non-generic body). Native lowering
  // selects the display helper from this checked fact. Checker metadata only.
  std::unordered_map<string,vector<string>> echo_types_by_context;
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
  // Source-free Fast Debug executable body from ABI-v9 Section 2.
  std::optional<vector<Stmt>> execution_body;
  string execution_source_file;
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
  TypeRef return_type_ref = nullptr;
  void bind_return_type(TypeRef term) {
    return_type_ref = std::move(term);
    if (return_type_ref) return_type = return_type_ref->to_canonical_string();
    else return_type.reset();
  }
  void set_return_type(std::optional<string> raw) {
    return_type = std::move(raw);
    if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) {
        return_type_ref = *parsed;
        return;
      }
    }
    return_type_ref = nullptr;
  }
  void sync() {
    if (return_type_ref) {
      return_type = return_type_ref->to_canonical_string();
    } else if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) return_type_ref = *parsed;
    }
  }
  void assert_consistent() const {
    if (return_type_ref &&
        (!return_type || *return_type != return_type_ref->to_canonical_string())) {
      throw std::logic_error("Method '" + owner + "." + name +
                             "' return type string projection '" + return_type.value_or("<missing>") +
                             "' contradicts bound TypeRef '" +
                             return_type_ref->to_canonical_string() + "'");
    }
    for (const auto& param : params) param.assert_consistent();
  }
  vector<Stmt> body;
  // Source-free Fast Debug executable body from ABI-v9 Section 2.
  std::optional<vector<Stmt>> execution_body;
  std::optional<string> execution_result_expression;
  string execution_source_file;
  std::optional<string> result_expression;
  vector<est::Handle> expression_handles;
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
struct TraitMethod {
  string name;
  vector<Param> params;
  std::optional<string> return_type;
  TypeRef return_type_ref = nullptr;
  void bind_return_type(TypeRef term) {
    return_type_ref = std::move(term);
    if (return_type_ref) return_type = return_type_ref->to_canonical_string();
    else return_type.reset();
  }
  void set_return_type(std::optional<string> raw) {
    return_type = std::move(raw);
    if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) {
        return_type_ref = *parsed;
        return;
      }
    }
    return_type_ref = nullptr;
  }
  void sync() {
    if (return_type_ref) {
      return_type = return_type_ref->to_canonical_string();
    } else if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) return_type_ref = *parsed;
    }
  }
  void assert_consistent() const {
    if (return_type_ref &&
        (!return_type || *return_type != return_type_ref->to_canonical_string())) {
      throw std::logic_error("TraitMethod '" + name +
                             "' return type string projection '" + return_type.value_or("<missing>") +
                             "' contradicts bound TypeRef '" +
                             return_type_ref->to_canonical_string() + "'");
    }
    for (const auto& param : params) param.assert_consistent();
  }
  int line = 0;
  string source_file;
};
struct Handler {
  vector<est::Handle> expression_handles;
  string name, header;
  vector<Param> params;
  std::optional<string> reply_type;
  TypeRef reply_type_ref = nullptr;
  void bind_reply_type(TypeRef term) {
    reply_type_ref = std::move(term);
    if (reply_type_ref) reply_type = reply_type_ref->to_canonical_string();
    else reply_type.reset();
  }
  void set_reply_type(std::optional<string> raw) {
    reply_type = std::move(raw);
    if (reply_type && !reply_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*reply_type);
      if (parsed && (*parsed)->is_closed()) {
        reply_type_ref = *parsed;
        return;
      }
    }
    reply_type_ref = nullptr;
  }
  void sync() {
    if (reply_type_ref) {
      reply_type = reply_type_ref->to_canonical_string();
    } else if (reply_type && !reply_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*reply_type);
      if (parsed && (*parsed)->is_closed()) reply_type_ref = *parsed;
    }
  }
  void assert_consistent() const {
    if (reply_type_ref &&
        (!reply_type || *reply_type != reply_type_ref->to_canonical_string())) {
      throw std::logic_error("Handler '" + name +
                             "' reply type string projection '" + reply_type.value_or("<missing>") +
                             "' contradicts bound TypeRef '" +
                             reply_type_ref->to_canonical_string() + "'");
    }
    for (const auto& param : params) param.assert_consistent();
  }
  vector<Stmt> body;
  // Source-free Fast Debug executable body from ABI-v9 Section 2.
  std::optional<vector<Stmt>> execution_body;
  std::optional<string> execution_result_expression;
  std::optional<vector<FailureArm>> execution_failure_arms;
  string execution_source_file;
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
struct ObjectType { string name, header; vector<Field> fields; vector<Method> methods; bool exported = false; int line = 0; string source_file; string defining_module; string declaration_name; string execution_source_file; };
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
  vector<est::Handle> expression_handles;
  string name, header;
  vector<Param> params;
  std::optional<string> return_type;
  TypeRef return_type_ref = nullptr;
  void bind_return_type(TypeRef term) {
    return_type_ref = std::move(term);
    if (return_type_ref) return_type = return_type_ref->to_canonical_string();
    else return_type.reset();
  }
  void set_return_type(std::optional<string> raw) {
    return_type = std::move(raw);
    if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) {
        return_type_ref = *parsed;
        return;
      }
    }
    return_type_ref = nullptr;
  }
  void sync() {
    if (return_type_ref) {
      return_type = return_type_ref->to_canonical_string();
    } else if (return_type && !return_type->empty() && TypeContext::current()) {
      auto parsed = Type::parse_canonical(*return_type);
      if (parsed && (*parsed)->is_closed()) return_type_ref = *parsed;
    }
  }
  void assert_consistent() const {
    if (return_type_ref &&
        (!return_type || *return_type != return_type_ref->to_canonical_string())) {
      throw std::logic_error("Function '" + name +
                             "' return type string projection '" + return_type.value_or("<missing>") +
                             "' contradicts bound TypeRef '" +
                             return_type_ref->to_canonical_string() + "'");
    }
    for (const auto& param : params) param.assert_consistent();
  }
  vector<Stmt> body;
  // Source-free Fast Debug executable body from ABI-v9 Section 2.
  std::optional<vector<Stmt>> execution_body;
  std::optional<string> execution_result_expression;
  string execution_source_file;
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
  std::optional<CheckedBodyV1> checked_body_v1;
  // Canonical checker-retained checked-body-v1 bytes for statement bodies.
  // This is distinct from CheckedBodyV1, the older expression-only semantic
  // summary projection retained for existing API clients.
  std::optional<std::string> checked_body_ir_v1_bytes;
  std::optional<std::string> checked_body_ir_v1_hash;
  // True only when the retained bytes came directly from checked
  // statement/expression occurrences. The bounded source-text projection is
  // a migration fallback and must not be counted as common-lowerer evidence.
  bool checked_body_ir_v1_occurrence_lowered = false;
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
  // Optional provider-authored logical source name retained by a compiled
  // interface for consumer diagnostics after its source tree is removed.
  string provider_origin_file;
  // ABI-v10 callable provenance is authoritative for imported diagnostic
  // origins. ABI-v9 keeps using the legacy interface fields above and line.
  int provider_origin_column = 1;
  string provider_origin_module_id;
  string provider_origin_identity;
  // ABI-v10 sealed records retained across module rewriting. The checker
  // decodes these before inference and replaces any legacy generic_stmt body.
  string imported_checked_summary_payload;
  string imported_checked_body_payload;
  vector<std::pair<string, string>> imported_checked_provenance_records;
  // Callable provenance records retained for staged ABI-v10 admission when
  // the callable has no typed checked summary yet. They are validated only
  // if that callable is selected for execution.
  vector<std::pair<string, string>> imported_callable_provenance_records;
  // True once a validated ABI-v10 checked body replaced the imported legacy
  // generic_stmt projection for this callable.
  bool imported_checked_body_projected = false;
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
  // Null in MOSS_EST=off: no arena is allocated by the legacy path.
  std::shared_ptr<est::Arena> expression_arena;
  // Explicit-module projects may merge independently parsed source units.
  // Handles carry an index into this shared immutable arena list.
  vector<std::shared_ptr<est::Arena>> expression_arenas;
  // Set only by `moss check <file>` on a project. This tells checker passes
  // that callable bodies have already been reduced to the file's reachable
  // closure and that package-wide rules belong to Margo's build path.
  bool file_check_scope = false;
  // Declaration identities retained for scoped Fast Debug queries after their
  // executable bodies have been pruned from the checked program.
  vector<Function> file_scope_pruned_functions;
  std::set<string> file_scope_objects;
  std::set<string> file_scope_enums;
  std::set<string> file_scope_domains;
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
  // Staged ABI-v10 canonical key proof IDs supplied by compiled providers,
  // keyed by interned nominal declaration identity. Source-owned proofs are
  // checked from the methods and emitted when writing the local interface.
  std::map<string, string> imported_key_proof_ids;
  // Checked key-protocol bindings and keyed operation specializations used by
  // staged native lowering and the project build fingerprint.
  std::map<string, string> key_proof_ids_by_type;
  std::map<string, string> key_operation_specialization_ids;
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
  void assert_declaration_type_consistency() const {
    for (const auto& f : functions) f.assert_consistent();
    for (const auto& o : objects)
      for (const auto& m : o.methods) m.assert_consistent();
    for (const auto& t : traits)
      for (const auto& m : t.methods) m.assert_consistent();
    for (const auto& d : domains)
      for (const auto& h : d.handlers) h.assert_consistent();
  }
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
