// SUMT feasibility slice (shadow only).
//
// Enabled by MOSS_SUMT_SHADOW=<path> (or "-" for stderr). Nothing here may
// change a verdict, a diagnostic, or any fact consumed by another stage.
//
// Two parts:
//   1. Ownership instrumentation: the existing ownership walk records, at
//      its own decision points, each place where an unresolved generic type
//      made it skip a transfer, and each later use that would depend on it.
//   2. A shadow summary fold over the retained, checked generic body
//      (BodyTypeState). It reads checker-owned occurrence facts only: no
//      statement text is parsed and no type inference is run.

struct SumtOwnershipEvent {
  string function;
  // conditional_transfer            the walk skipped a transfer whose existence
  //                                 depends on Copy(T) (decision Conditional)
  // deferred_unconditional_transfer the walk skipped a transfer that happens
  //                                 for every T (placeholder nested in a
  //                                 never-copy constructor)
  // assumed_transfer                the walk transferred a value whose transfer
  //                                 depends on Copy(T) (transfer_type default)
  // use_after_conditional_transfer, loop_conditional_transfer,
  // on_fail_conditional_transfer    consequences checked by the walk's own
  //                                 rules against a conditional entry
  // ownership_deferred_generic_match the walk skipped ownership entirely
  string kind;
  string binding;   // source name, for display only
  string identity;  // the walk's own binding identity (parameter:N or a declaration id)
  string type;      // the unresolved type the decision depended on
  int transfer_line = 0;
  int use_line = 0;
};

// Package hardening turns on the same recording, without the env var.
bool sumt_shadow_enabled_ =
    (std::getenv("MOSS_SUMT_SHADOW") != nullptr || p_.sumt_harden) &&
    !sumt_nested_checker_active();
const Function* sumt_ownership_function_ = nullptr;
mutable vector<SumtOwnershipEvent> sumt_ownership_events_;
// Every placeholder-dependent decision taken with a historical default,
// keyed by (function, line, which default).
mutable std::map<std::tuple<string, int, string>, int> sumt_placeholder_defaults_;

// Parameter effects (#5): a method on an open receiver is the consumer's;
// the checker leaves its effect out of the generic body's access (see
// possible_method_effects), and the fold records it as an `effect|...`
// requirement that Inst resolves against the consumer's own method.
static string sumt_effect_name(Effect effect) {
  return effect == Effect::Consume ? "consume" : effect == Effect::Write ? "write" : "read";
}

// Nested checkers (the shadow validator's copies) never record or emit.
static bool& sumt_nested_flag() {
  static thread_local bool nested = false;
  return nested;
}
static bool sumt_nested_checker_active() { return sumt_nested_flag(); }

bool sumt_recording_ownership() const {
  return sumt_shadow_enabled_ && sumt_ownership_function_ &&
      !checking_specialization_;
}

static bool sumt_bare_placeholder(const string& canonical) {
  return internal_semantic_placeholder(trim(canonical));
}

void sumt_note_placeholder_default(const string& reason, const string& type) const {
  if (!sumt_recording_ownership()) return;
  (void)type;
  const int line = ownership_current_stmt_ ? ownership_current_stmt_->line : 0;
  ++sumt_placeholder_defaults_[{sumt_ownership_function_->name, line, reason}];
}

static string sumt_identity(const OwnershipEnv& env, const string& name) {
  auto found = env.binding_identities.find(name);
  return found == env.binding_identities.end() ? string() : found->second;
}

void sumt_record(const string& kind, const string& binding, const string& identity,
                 const string& type, int transfer_line, int use_line) const {
  if (!sumt_recording_ownership()) return;
  sumt_ownership_events_.push_back({sumt_ownership_function_->name, kind, binding,
      identity, type, transfer_line, use_line});
}

// Called where the ownership walk decided "no transfer" for a simple binding
// source because the type is unresolved. Records the skipped transfer in the
// walk's own `moved` map (marked conditional), so every existing join,
// shadowing, loop and recovery rule carries it; consequences are recorded by
// the walk's own checks instead of raising.
void sumt_conditional_transfer(int line, const string& source,
                               const std::optional<string>& source_type,
                               const string& destination, OwnershipEnv& env) {
  if (!sumt_recording_ownership() || !source_type) return;
  const string canonical = canonical_type_name(*source_type);
  if (canonical.empty() || !contains_internal_placeholder(canonical)) return;
  const auto decision = ownership_transfer_decision(canonical);
  if (decision == TransferDecision::NoTransfer) return;
  if (!tree_simple_identifier(source) || !env.types.count(source)) return;
  auto existing = env.moved.find(source);
  if (existing != env.moved.end() && existing->second.conditional_on.empty())
    return;  // already unconditionally moved; the walk reports that itself
  env.moved[source] = MoveInfo{line, destination, canonical};
  sumt_record(decision == TransferDecision::Conditional ? "conditional_transfer"
                                                        : "deferred_unconditional_transfer",
              source, sumt_identity(env, source), canonical, line, 0);
}

// The walk let two overlapping READ arguments through. If the callee's own
// summary says either parameter is consume-unless-copy, the call conflicts
// exactly when the argument's type is not copy: record that obligation. If
// the callee has no summary yet, the permissive outcome is unbacked.
void sumt_conditional_conflict(int line, const string& callee_name, const string& root,
                               const string& expression, size_t left_index,
                               size_t right_index, const OwnershipEnv& env) const {
  if (!sumt_recording_ownership()) return;
  auto type = inferred_expr_type(expression, env.types);
  if (!type || ownership_transfer_decision(*type) != TransferDecision::Conditional) return;
  const Function* callee = nullptr;
  for (const auto& candidate : p_.functions)
    if (candidate.name == callee_name) callee = &candidate;
  if (!callee) {
    sumt_note_placeholder_default("argument conflict check: callee summary unavailable", *type);
    return;
  }
  auto memo = sumt_folds_.find(callee->name);
  if (memo == sumt_folds_.end() || !memo->second.done) {
    sumt_note_placeholder_default("argument conflict check: callee summary unavailable", *type);
    return;
  }
  const auto& access = memo->second.access;
  auto conditional = [&](size_t index) {
    return index < access.size() &&
        (access[index] == "consume_unless_copy" || access[index] == "consume");
  };
  if (conditional(left_index) || conditional(right_index))
    sumt_record("conflict_unless_copy", root, sumt_identity(env, root),
                canonical_type_name(*type), line, line);
}

// Call-argument decision point. The walk applied READ to an argument of a
// generic callee because the argument's type is a placeholder. The callee's
// own summary decides what that READ means:
//   consume / consume_unless_copy -> the argument binding is conditionally
//     transferred in the caller (so later uses become obligations);
//   read / write -> the callee's summary backs the READ (evidence event).
// No complete callee summary yet (e.g. callee declared later): the permissive
// default stays unbacked and the caller's entry is incomplete.
void sumt_call_argument(int line, const string& callee_name, size_t index,
                        const string& argument, Effect applied, OwnershipEnv& env) {
  if (!sumt_recording_ownership() || applied != Effect::Read) return;
  const string value = trim(argument);
  auto type = inferred_expr_type(value, env.types);
  if (!type) return;
  const string canonical = canonical_type_name(*type);
  if (canonical.empty() || !contains_internal_placeholder(canonical)) return;
  if (ownership_transfer_decision(canonical) == TransferDecision::NoTransfer) return;
  auto memo = sumt_folds_.find(callee_name);
  if (memo == sumt_folds_.end() || !memo->second.done ||
      memo->second.status != "complete") {
    sumt_note_placeholder_default("call argument: callee summary unavailable or incomplete",
                                  canonical);
    return;
  }
  const auto& access = memo->second.access;
  const string mode = index < access.size() ? access[index] : "read";
  if (mode == "consume_unless_copy" || mode == "consume") {
    if (!tree_simple_identifier(value) || !env.types.count(value)) {
      sumt_note_placeholder_default("call argument: non-binding argument to a consuming parameter",
                                    canonical);
      return;
    }
    sumt_conditional_transfer(line, value, type, "argument " + std::to_string(index) +
                              " of '" + callee_name + "'", env);
    return;
  }
  sumt_record("callee_summary_backs_read", value, sumt_identity(env, value), canonical,
              line, 0);
}

// Express an obligation's placeholder type over the function's parameters:
// `_generic:p` -> `$i`. Empty if a placeholder is not a parameter of `function`.
static string sumt_placeholder_term(const string& type, const Function& function) {
  string out;
  const string marker = "_generic:";
  size_t at = 0;
  while (true) {
    const size_t found = type.find(marker, at);
    if (found == string::npos) { out += type.substr(at); break; }
    out += type.substr(at, found - at);
    size_t end = found + marker.size();
    while (end < type.size() && (std::isalnum(static_cast<unsigned char>(type[end])) ||
                                 type[end] == '_')) ++end;
    const string name = type.substr(found + marker.size(), end - found - marker.size());
    size_t ordinal = function.params.size();
    for (size_t index = 0; index < function.params.size(); ++index)
      if (function.params[index].name == name) ordinal = index;
    if (ordinal == function.params.size()) return "";
    out += "$" + std::to_string(ordinal);
    at = end;
  }
  return out;
}

static bool sumt_obligation_kind(const string& kind) {
  return kind == "use_after_conditional_transfer" || kind == "loop_conditional_transfer" ||
         kind == "on_fail_conditional_transfer" || kind == "conflict_unless_copy";
}

// A merge must never let a conditional entry hide an unconditional move.
static void sumt_merge_move(std::map<string, MoveInfo>& into,
                            const std::pair<const string, MoveInfo>& entry) {
  auto found = into.find(entry.first);
  if (found == into.end()) { into.emplace(entry); return; }
  if (!found->second.conditional_on.empty() && entry.second.conditional_on.empty())
    found->second = entry.second;
}

// ---------------------------------------------------------------------------
// Shadow summary fold.

static void sumt_json_string(std::ostream& out, const string& value) {
  out << '"';
  for (char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) out << ' ';
        else out << c;
    }
  }
  out << '"';
}

struct SumtVarMap {
  // Body-solver variable -> summary ordinal, in parameter order.
  vector<std::pair<moss::TypeVarId, uint32_t>> entries;
  std::optional<uint32_t> find(const moss::TypeVarId& id) const {
    for (const auto& entry : entries)
      if (entry.first == id) return entry.second;
    return std::nullopt;
  }
};

static void sumt_collect_vars(const moss::TypeRef& type, SumtVarMap& map) {
  if (!type) return;
  if (type->kind == moss::TypeKind::InferenceVar) {
    if (!map.find(type->var_id))
      map.entries.emplace_back(type->var_id,
                               static_cast<uint32_t>(map.entries.size()));
    return;
  }
  for (const auto& argument : type->type_args) sumt_collect_vars(argument, map);
  for (const auto& field : type->field_substitutions)
    sumt_collect_vars(field.second, map);
}

static string sumt_var_key(const moss::TypeVarId& id) {
  return std::to_string(id.session_id) + ":" + std::to_string(id.owner_id) +
      ":" + std::to_string(id.ordinal);
}

struct SumtRenderContext {
  SumtVarMap vars;
  // Body variables the fold has related to the inputs: var key -> symbol.
  std::map<string, string> derived;
  std::map<string, uint32_t> parameter_by_name;
};

// Render a resolved body term in summary vocabulary: parameter variables
// become $0, $1, …; a variable the fold related to the inputs becomes its
// symbol (e.g. $0.score.result); a `_generic:<p>` placeholder becomes the
// ordinal of parameter p. Anything else is ?free and makes the entry
// incomplete: the retained facts do not relate it to the inputs.
static string sumt_render(const moss::TypeRef& type, const SumtRenderContext& context,
                          bool& has_free) {
  if (!type) { has_free = true; return "?null"; }
  if (type->kind == moss::TypeKind::InferenceVar) {
    if (auto ordinal = context.vars.find(type->var_id))
      return "$" + std::to_string(*ordinal);
    auto derived = context.derived.find(sumt_var_key(type->var_id));
    if (derived != context.derived.end()) return derived->second;
    has_free = true;
    return "?free";
  }
  if (type->type_args.empty() && type->field_substitutions.empty()) {
    const string canonical = type->to_canonical_string();
    if (starts_with(canonical, "_generic:")) {
      auto parameter = context.parameter_by_name.find(canonical.substr(9));
      if (parameter != context.parameter_by_name.end())
        return "$" + std::to_string(parameter->second);
      has_free = true;
      return "?placeholder";
    }
    return canonical;
  }
  string out = type->name + "[";
  bool first = true;
  for (const auto& argument : type->type_args) {
    if (!first) out += ",";
    first = false;
    out += sumt_render(argument, context, has_free);
  }
  for (const auto& field : type->field_substitutions) {
    if (!first) out += ",";
    first = false;
    out += field.first + "=" + sumt_render(field.second, context, has_free);
  }
  return out + "]";
}

// Replace $k tokens in a callee's symbolic term with the caller's argument
// terms. Pure substitution; no solving.
static string sumt_substitute(const string& term, const vector<string>& arguments,
                              bool& has_free) {
  string out;
  for (size_t index = 0; index < term.size(); ++index) {
    if (term[index] != '$' || index + 1 >= term.size() ||
        !std::isdigit(static_cast<unsigned char>(term[index + 1]))) {
      out += term[index];
      continue;
    }
    size_t end = index + 1;
    while (end < term.size() && std::isdigit(static_cast<unsigned char>(term[end]))) ++end;
    const auto ordinal = std::stoul(term.substr(index + 1, end - index - 1));
    if (ordinal < arguments.size()) out += arguments[ordinal];
    else { out += "?arg"; has_free = true; }
    index = end - 1;
  }
  return out;
}

static string sumt_operator_class(const string& operation) {
  if (operation == "+") return "add";
  if (operation == "-" || operation == "*" || operation == "/") return "arith";
  if (operation == "%") return "integral";
  if (operation == "<" || operation == ">" || operation == "<=" ||
      operation == ">=") return "ordered";
  if (operation == "==" || operation == "!=") return "equatable";
  return "op" + operation;
}

struct SumtFold {
  bool computing = false;
  bool done = false;
  vector<string> parameters;
  string result;
  vector<string> requirements;   // member|$0.score|0, op|+|l|r, same|a|b
  // Where each requirement comes from: "function:line", extended through
  // composition as "callee:line <- caller:line" (deepest first).
  std::map<string, string> origins;
  std::set<string> callees;
  vector<string> missing_facts;
  vector<string> missing_representation;
  vector<string> notes;          // facts the checker left open but the fold could represent
  // Conditional parameter access: read | write | consume | consume_unless_copy
  vector<string> access;
  string status;
};
std::map<string, SumtFold> sumt_folds_;

const SumtFold& sumt_fold(const Function& function) {
  auto& fold = sumt_folds_[function.name];
  if (fold.done || fold.computing) return fold;
  fold = SumtFold{};  // drop a report left by an earlier, pre-check request
  fold.computing = true;
  auto finish = [&]() -> const SumtFold& {
    fold.status = !fold.missing_facts.empty() ? "missing_fact" :
        !fold.missing_representation.empty() ? "missing_representation" : "complete";
    fold.computing = false;
    fold.done = true;
    return fold;
  };
  auto retained = checked_body_states_.find(&function.body);
  if (retained == checked_body_states_.end() || !retained->second) {
    // Not checked yet (declaration order): report, but do not memoize.
    fold = SumtFold{};
    fold.missing_facts.push_back("no retained generic body state");
    fold.status = "missing_fact";
    return fold;
  }
  auto& state = *retained->second;
  // A recorded relation may carry a term from another solver session (e.g. an
  // instantiated callee summary). Such a term is not related to this body's
  // inputs: treat it as unrelated instead of failing.
  auto resolve_relation = [&](const moss::TypeRef& type) -> moss::TypeRef {
    if (!type) return nullptr;
    try { return state.solver.resolve(type); }
    catch (const std::invalid_argument&) { return nullptr; }
  };
  SumtRenderContext context;
  vector<moss::TypeRef> parameter_terms;
  for (size_t index = 0; index < function.params.size(); ++index) {
    const auto& parameter = function.params[index];
    auto term = state.resolved_local(parameter.name);
    if (!term) fold.missing_facts.push_back("parameter '" + parameter.name +
                                            "' has no checked term");
    parameter_terms.push_back(term);
  }
  // Numbering: a parameter whose checked term is a bare variable gets its own
  // position ($i is parameter i), so `_generic:<p>` placeholders, obligations
  // and Inst's sigma all agree. Variables nested in a structured parameter
  // term are numbered after the parameters.
  const auto parameter_count = static_cast<uint32_t>(function.params.size());
  for (uint32_t index = 0; index < parameter_count; ++index) {
    const auto& term = parameter_terms[index];
    if (term && term->kind == moss::TypeKind::InferenceVar && !context.vars.find(term->var_id))
      context.vars.entries.emplace_back(term->var_id, index);
  }
  {
    uint32_t next = parameter_count;
    std::function<void(const moss::TypeRef&)> rest = [&](const moss::TypeRef& type) {
      if (!type) return;
      if (type->kind == moss::TypeKind::InferenceVar) {
        if (!context.vars.find(type->var_id)) context.vars.entries.emplace_back(type->var_id, next++);
        return;
      }
      for (const auto& argument : type->type_args) rest(argument);
      for (const auto& field : type->field_substitutions) rest(field.second);
    };
    for (const auto& term : parameter_terms) rest(term);
  }
  // Parameter placeholders map by name to the ordinal of the parameter's own
  // variable, when it has exactly one; otherwise to the parameter position.
  for (size_t index = 0; index < function.params.size(); ++index) {
    const auto& term = parameter_terms[index];
    if (term && term->kind == moss::TypeKind::InferenceVar)
      context.parameter_by_name[function.params[index].name] = *context.vars.find(term->var_id);
  }
  for (uint32_t index = 0; index < parameter_count; ++index) {
    bool free = false;
    const string rendered = sumt_render(parameter_terms[index], context, free);
    fold.parameters.push_back(rendered);
    // Inst binds $i to sigma[i]. A parameter term is usable only if every $k
    // in it is a parameter position; a structured pattern over fresh
    // variables (e.g. vector[$n]) is not yet representable.
    // Inst matches structured terms (e.g. vector[$n]) against sigma; a term
    // not related to the inputs at all is not representable.
    if (free)
      fold.missing_representation.push_back("parameter '" + function.params[index].name +
          "' has a term not related to the inputs: " + rendered);
  }

  using Kind = BodyTypeState::ExpressionOccurrence::Kind;
  std::map<std::uint32_t, vector<const BodyTypeState::ExpressionOccurrence*>> children;
  for (const auto& occurrence : state.expression_occurrences)
    if (occurrence.parent_id != UINT32_MAX)
      children[occurrence.parent_id].push_back(&occurrence);
  auto child_with_role = [&](std::uint32_t parent, const string& role)
      -> const BodyTypeState::ExpressionOccurrence* {
    for (const auto* child : children[parent])
      if (child->child_role == role) return child;
    return nullptr;
  };
  auto term_of = [&](const BodyTypeState::ExpressionOccurrence& occurrence,
                     bool& free) -> string {
    if (!occurrence.checked_type) { free = true; return "?untyped"; }
    return sumt_render(resolve_relation(occurrence.checked_type), context, free);
  };
  std::set<string> requirement_set;
  int origin_line = function.line;
  auto require = [&](const string& requirement) {
    if (requirement_set.insert(requirement).second) {
      fold.requirements.push_back(requirement);
      fold.origins[requirement] = function.name + ":" + std::to_string(origin_line);
    }
  };
  // Symbolic value of an occurrence: its checked term when the fold can
  // express it; for an operator the checker left untyped, the operator-class
  // term over its operands (recorded as a note, not inferred).
  // Occurrence values given by a checker record rather than a checked type
  // (a deferred pipeline stage: the stage expression itself).
  std::map<uint32_t, string> recorded_values;
  // Parameters whose checked structure came only from indexing them: the
  // generic check's index rule defaults an open container to vector[elem]
  // and an open key to int, which the concrete check does not (maps index
  // too). Such a parameter stays bare ($i); the index itself is checked and
  // typed by the checker at Inst.
  std::set<uint32_t> bare_parameters;
  std::set<uint32_t> explained_indexes;
  std::function<string(const BodyTypeState::ExpressionOccurrence&, bool&)> value_of =
      [&](const BodyTypeState::ExpressionOccurrence& occurrence, bool& free) -> string {
    auto recorded = recorded_values.find(occurrence.id);
    if (recorded != recorded_values.end()) return recorded->second;
    if (occurrence.kind == Kind::Identifier && starts_with(occurrence.resolved_binding_id, "parameter:")) {
      const auto ordinal = static_cast<uint32_t>(std::stoul(occurrence.resolved_binding_id.substr(10)));
      if (bare_parameters.count(ordinal)) return "$" + std::to_string(ordinal);
    }
    if (occurrence.checked_type) {
      bool local_free = false;
      const string term = term_of(occurrence, local_free);
      if (!local_free && occurrence.kind == Kind::Binary && term.find('$') != string::npos) {
        // An open operator result equal to one operand is the generic
        // solver's inference choice (result := operand), not the operator's
        // checked result (int + float is float). Represent the result as the
        // operation itself; Inst has the checker type it at sigma.
        const auto* left = child_with_role(occurrence.id, "left");
        const auto* right = child_with_role(occurrence.id, "right");
        if (left && right) {
          bool operand_free = false;
          const string l = value_of(*left, operand_free);
          const string r = value_of(*right, operand_free);
          if (!operand_free && (term == l || term == r))
            return sumt_operator_class(occurrence.operation_id) + "(" + l + "," + r + ")";
        }
      }
      if (!local_free) return term;
    }
    if (occurrence.kind == Kind::Binary) {
      const auto* left = child_with_role(occurrence.id, "left");
      const auto* right = child_with_role(occurrence.id, "right");
      if (left && right) {
        bool operand_free = false;
        const string l = value_of(*left, operand_free);
        const string r = value_of(*right, operand_free);
        if (!operand_free) {
          if (!occurrence.checked_type)
            fold.notes.push_back("untyped operator at line " +
                std::to_string(occurrence.line) + " represented as " +
                sumt_operator_class(occurrence.operation_id));
          return sumt_operator_class(occurrence.operation_id) + "(" + l + "," + r + ")";
        }
      }
    }
    free = true;
    return occurrence.checked_type ? "?free" : "?untyped";
  };

  // An occurrence is open when its symbolic value mentions an input or is not
  // related to the inputs at all. Every open occurrence must be explained by a
  // rule below; otherwise the entry cannot claim completeness.
  auto open = [&](const BodyTypeState::ExpressionOccurrence& occurrence) {
    bool free = false;
    const string term = value_of(occurrence, free);
    return free || term.find('$') != string::npos;
  };
  std::set<string> unhandled_seen;
  bool final_round = false;
  std::set<int> callback_lines;
  std::set<uint32_t> covered_pipelines;
  auto inside_covered_pipeline = [&](const BodyTypeState::ExpressionOccurrence& occurrence) {
    for (uint32_t at = occurrence.id; at != UINT32_MAX && at < state.expression_occurrences.size();
         at = state.expression_occurrences[at].parent_id)
      if (covered_pipelines.count(at)) return true;
    return false;
  };
  auto unhandled = [&](const BodyTypeState::ExpressionOccurrence& occurrence,
                       const string& why) {
    if (!final_round) return;  // earlier rounds may still relate it
    if (inside_covered_pipeline(occurrence)) return;  // explained by the recorded pipeline
    const string text = why + " at line " + std::to_string(occurrence.line);
    if (unhandled_seen.insert(text).second) fold.missing_representation.push_back(text);
  };

  // Deferred pipelines: the checker recorded the source term and the stage
  // text, with marked references to locals. The pipeline's value is the
  // pipeline expression itself; Inst has the checker check and type it at
  // sigma. SUMT knows nothing about the stage operations.
  for (const auto& relation : state.sumt_deferred_relations) {
    origin_line = relation.line;
    if (relation.kind != "pipeline") continue;
    bool free = false;
    const string source = sumt_render(resolve_relation(relation.left), context, free);
    if (free) continue;
    string text;
    bool mapped = true;
    for (size_t at = 0; at < relation.origin.size();) {
      const size_t open_brace = relation.origin.find('{', at);
      if (open_brace == string::npos) { text += relation.origin.substr(at); break; }
      text += relation.origin.substr(at, open_brace - at);
      const size_t close_brace = relation.origin.find('}', open_brace);
      const string token = relation.origin.substr(open_brace + 1, close_brace - open_brace - 1);
      const size_t colon = token.find(':');
      const string name = token.substr(colon + 1);
      size_t ordinal = function.params.size();
      for (size_t index = 0; index < function.params.size(); ++index)
        if (function.params[index].name == name) ordinal = index;
      if (ordinal == function.params.size()) { mapped = false; break; }
      text += "{" + token.substr(0, colon) + ":$" + std::to_string(ordinal) + "}";
      at = close_brace + 1;
    }
    if (!mapped) continue;
    const string pipeline = "pipeline(" + source + ";" + text + ")";
    require("stage|" + pipeline);
    recorded_values[relation.occurrence] = pipeline;
    callback_lines.insert(relation.line);
    covered_pipelines.insert(relation.occurrence);
  }

  {
    auto open_parameter = [&](const BodyTypeState::ExpressionOccurrence* occurrence) -> int {
      if (!occurrence || !starts_with(occurrence->resolved_binding_id, "parameter:")) return -1;
      const auto ordinal = std::stoul(occurrence->resolved_binding_id.substr(10));
      if (ordinal >= function.params.size() || !function.params[ordinal].type.empty()) return -1;
      return static_cast<int>(ordinal);
    };
    for (const auto& occurrence : state.expression_occurrences) {
      if (occurrence.kind != Kind::Index || !occurrence.checked_type) continue;
      const int container = open_parameter(child_with_role(occurrence.id, "container"));
      if (container < 0) continue;
      auto element = resolve_relation(occurrence.checked_type);
      if (!element || element->kind != moss::TypeKind::InferenceVar) continue;
      bare_parameters.insert(static_cast<uint32_t>(container));
      const auto* key = child_with_role(occurrence.id, "index");
      const int key_parameter = open_parameter(key);
      if (key_parameter >= 0) bare_parameters.insert(static_cast<uint32_t>(key_parameter));
      explained_indexes.insert(occurrence.id);
    }
    for (const auto& occurrence : state.expression_occurrences) {
      if (!explained_indexes.count(occurrence.id)) continue;
      const auto* key = child_with_role(occurrence.id, "index");
      bool free = false;
      const string key_term = key ? value_of(*key, free) : string();
      const string container_term = "$" + child_with_role(occurrence.id, "container")->resolved_binding_id.substr(10);
      if (free || key_term.empty()) { explained_indexes.erase(occurrence.id); continue; }
      const string index = "index(" + container_term + "," + key_term + ")";
      auto element = resolve_relation(occurrence.checked_type);
      // The element is no longer a position of a structured parameter term:
      // it is the index result.
      auto& entries = context.vars.entries;
      entries.erase(std::remove_if(entries.begin(), entries.end(),
                                   [&](const auto& entry) { return entry.first == element->var_id; }),
                    entries.end());
      context.derived[sumt_var_key(element->var_id)] = index;
      origin_line = occurrence.line;
      require("check|" + index);
    }
    for (const auto ordinal : bare_parameters)
      if (ordinal < fold.parameters.size()) fold.parameters[ordinal] = "$" + std::to_string(ordinal);
  }

  // Element relations recorded by the checker where it deferred iteration:
  // the loop variable is the element of the source.
  for (const auto& relation : state.sumt_deferred_relations) {
    origin_line = relation.line;
    if (relation.kind != "element") continue;
    auto element = resolve_relation(relation.right);
    if (!element || element->kind != moss::TypeKind::InferenceVar) continue;
    bool free = false;
    const string source = sumt_render(resolve_relation(relation.left), context, free);
    if (free) continue;  // reported with the other relations below
    if (traits_.count(source)) {
      // The element of a trait-bounded source depends on the implementor;
      // a trait-named term cannot carry that dependence.
      fold.missing_representation.push_back("iteration over a trait-bounded source (" + source +
          ") at line " + std::to_string(relation.line) + "; element depends on the implementor");
      continue;
    }
    context.derived[sumt_var_key(element->var_id)] = source + ".element";
    require("iterable|" + source);
  }

  // Relate body variables to the inputs, in occurrence order, a few rounds
  // (children are visited after parents in id order).
  for (int round = 0; round < 3; ++round) {
    final_round = round == 2;
    for (const auto& occurrence : state.expression_occurrences) {
      origin_line = occurrence.line;
      const auto resolved = occurrence.checked_type
          ? resolve_relation(occurrence.checked_type) : nullptr;
      const bool open_var = resolved && resolved->kind == moss::TypeKind::InferenceVar &&
          !context.vars.find(resolved->var_id) &&
          !context.derived.count(sumt_var_key(resolved->var_id));
      if (occurrence.kind == Kind::MemberCall) {
        const auto* receiver = child_with_role(occurrence.id, "receiver");
        if (!receiver) continue;
        bool free = false;
        const string receiver_term = value_of(*receiver, free);
        if (free) { unhandled(occurrence, "member call on a receiver not related to the inputs"); continue; }
        if (starts_with(occurrence.semantic_target_id, "trait-method:")) {
          // A trait method's effect is the implementor's (#5). The checker
          // leaves it out of the generic body's access; it is resolved at
          // Inst for a parameter passed directly. Through a local alias it
          // has no attribution yet.
          auto effect_term = [&](const BodyTypeState::ExpressionOccurrence& child, bool& unrelated) -> string {
            if (starts_with(child.resolved_binding_id, "parameter:"))
              return "$" + child.resolved_binding_id.substr(10);
            return value_of(child, unrelated);
          };
          bool unrelated = false;
          const string receiver_effect_term = effect_term(*receiver, unrelated);
          if (!starts_with(receiver->resolved_binding_id, "parameter:")) {
            unhandled(occurrence, "trait method effect on a value not passed as a parameter");
            continue;
          }
          string trait_member = occurrence.operation_id;
          if (starts_with(trait_member, "method:")) trait_member = trait_member.substr(7);
          string terms;
          size_t count = 0;
          for (;; ++count) {
            const auto* argument = child_with_role(occurrence.id, "argument:" + std::to_string(count));
            if (!argument) break;
            terms += (count ? ";" : "") + effect_term(*argument, unrelated);
          }
          if (unrelated) { unhandled(occurrence, "trait method argument not related to the inputs"); continue; }
          require("effect|" + receiver_effect_term + "." + trait_member + "|" + std::to_string(count) +
                  (count ? "|" + terms : string()));
        }
        bool any_open_argument = false;
        for (const auto* child : children[occurrence.id])
          if (child->child_role != "receiver" && open(*child)) any_open_argument = true;
        bool related = false;
        for (const auto& relation : state.sumt_deferred_relations)
          if (relation.line == occurrence.line) related = true;
        const bool bare_receiver = receiver_term.size() > 1 && receiver_term[0] == '$' &&
            receiver_term.find_first_not_of("0123456789", 1) == string::npos;
        if (related) continue;  // the deferred relation recorded here is the requirement
        if (receiver_term.empty() || receiver_term.find('$') == string::npos) {
          // Concrete receiver: the checker resolved the member. With only
          // concrete arguments nothing depends on the inputs; an open argument
          // is related by the member requirement below.
          if (!any_open_argument) continue;
        }
        if (!bare_receiver && starts_with(occurrence.semantic_target_id, "builtin-method:")) {
          // A builtin member on a structured receiver: Inst has no resolver
          // for builtin members, so an open use is not representable yet.
          if (any_open_argument || open_var)
            unhandled(occurrence, "builtin member over an open term");
          continue;
        }
        string member = occurrence.operation_id;
        if (starts_with(member, "method:")) member = member.substr(7);
        // Argument terms travel with the requirement, so Inst resolves the
        // member against the argument types the call supplies.
        string argument_terms;
        size_t arity = 0;
        bool argument_free = false;
        for (;; ++arity) {
          const auto* argument = child_with_role(occurrence.id, "argument:" + std::to_string(arity));
          if (!argument) break;
          // A literal argument travels as its source text (#4), not its type:
          // some member checks depend on the value (FileIO's bounded read
          // request), which a typed placeholder cannot carry.
          const string literal = trim(argument->source_expression);
          const bool literal_safe = argument->kind == Kind::Literal && !literal.empty() &&
              literal.find_first_of("|;(),") == string::npos;
          argument_terms += (arity ? ";" : "") +
              (literal_safe ? "#" + literal : value_of(*argument, argument_free));
        }
        if (argument_free) { unhandled(occurrence, "member call argument not related to the inputs"); continue; }
        require("member|" + receiver_term + "." + member + "|" + std::to_string(arity) +
                (arity ? "|" + argument_terms : string()));
        if (open(*receiver)) {
          // The method is the consumer's: its effect on the receiver and on
          // its arguments is deferred to Inst (#5). Inst attributes it to a
          // parameter passed directly; a place inside a parameter (an element
          // or an index) has no attribution yet.
          auto place_of_input = [](const string& term) {
            return term.find('$') != string::npos && !ends_with(term, ".result") &&
                (term.find(".element") != string::npos || term.find("index(") != string::npos);
          };
          bool place = place_of_input(receiver_term);
          for (size_t from = 0; !place && !argument_terms.empty();) {
            const size_t semi = argument_terms.find(';', from);
            place = place_of_input(argument_terms.substr(from, semi == string::npos ? string::npos : semi - from));
            if (semi == string::npos) break;
            from = semi + 1;
          }
          if (place) { unhandled(occurrence, "method effect on a place inside a parameter"); continue; }
          require("effect|" + receiver_term + "." + member + "|" + std::to_string(arity) +
                  (arity ? "|" + argument_terms : string()));
        }
        if (open_var)
          context.derived[sumt_var_key(resolved->var_id)] = receiver_term + "." + member +
              (arity ? "(" + argument_terms + ")" : string()) + ".result";
      } else if (occurrence.kind == Kind::Call && !occurrence.semantic_target_id.empty()) {
        const Function* callee = nullptr;
        for (const auto& candidate : p_.functions)
          if (candidate.semantic_callable_id == occurrence.semantic_target_id) callee = &candidate;
        if (!callee && std::any_of(state.sumt_deferred_relations.begin(),
                                   state.sumt_deferred_relations.end(),
                                   [&](const auto& relation) {
                                     return relation.kind == "result" &&
                                            relation.occurrence == occurrence.id;
                                   })) {
          // A builtin whose result the checker recorded as its argument's
          // type (copy): the result is the argument child's term.
          const auto* argument = child_with_role(occurrence.id, "argument:0");
          bool free = false;
          const string term = argument ? value_of(*argument, free) : string();
          if (open_var && argument && !free)
            context.derived[sumt_var_key(resolved->var_id)] = term;
          continue;
        }
        if (!callee || !(callee->generic || callee->static_dispatch)) {
          bool any_open = !callee;  // a call through a binding (callable parameter)
          for (const auto* child : children[occurrence.id]) any_open = any_open || open(*child);
          if (any_open) unhandled(occurrence, callee ? "open argument to a concrete callee"
                                                     : "call through a callable binding");
          continue;
        }
        fold.callees.insert(callee->name);
        const auto& callee_fold = sumt_fold(*callee);
        if (callee_fold.computing) { unhandled(occurrence, "recursive generic call"); continue; }
        if (callee_fold.status != "complete")
          unhandled(occurrence, "callee '" + callee->name + "' entry is " + callee_fold.status);
        vector<string> arguments, effect_arguments;
        bool free = false;
        for (size_t index = 0; ; ++index) {
          const auto* argument = child_with_role(occurrence.id, "argument:" + std::to_string(index));
          if (!argument) break;
          arguments.push_back(value_of(*argument, free));
          // An effect lands on storage: a parameter passed directly keeps
          // its identity even when its type term is a trait name (#5).
          effect_arguments.push_back(starts_with(argument->resolved_binding_id, "parameter:")
              ? "$" + argument->resolved_binding_id.substr(10) : arguments.back());
        }
        if (free) { unhandled(occurrence, "generic call argument not related to the inputs"); continue; }
        bool lost_effect = false;
        for (const auto& requirement : callee_fold.requirements) {
          bool substitution_free = false;
          const bool effect_requirement = starts_with(requirement, "effect|");
          const string substituted = sumt_substitute(
              requirement, effect_requirement ? effect_arguments : arguments, substitution_free);
          if (effect_requirement) {
            const string target = substituted.substr(7, substituted.find('|', 7) - 7);
            if (target.find('$') == string::npos) { lost_effect = true; continue; }
          }
          const bool fresh_requirement = !requirement_set.count(substituted);
          require(substituted);
          if (fresh_requirement) {
            auto origin = callee_fold.origins.find(requirement);
            fold.origins[substituted] =
                (origin != callee_fold.origins.end() ? origin->second : callee->name + ":?") +
                " <- " + function.name + ":" + std::to_string(occurrence.line);
          }
        }
        if (lost_effect)
          unhandled(occurrence, "callee method effect on a value not passed as a parameter");
        if (open_var && callee_fold.status != "missing_fact") {
          bool substitution_free = false;
          const string term = sumt_substitute(callee_fold.result, arguments, substitution_free);
          if (!substitution_free && term.find('?') == string::npos)
            context.derived[sumt_var_key(resolved->var_id)] = term;
        } else if (!occurrence.checked_type && callee_fold.status == "complete") {
          // The caller's generic check left the call untyped (it cannot see
          // through the callee). The callee's complete summary gives the
          // result; the checker types anything built on it at Inst.
          bool substitution_free = false;
          const string term = sumt_substitute(callee_fold.result, arguments, substitution_free);
          if (!substitution_free && term.find('?') == string::npos)
            recorded_values[occurrence.id] = term;
        }
      } else if (occurrence.kind == Kind::Binary) {
        const auto* left = child_with_role(occurrence.id, "left");
        const auto* right = child_with_role(occurrence.id, "right");
        if (!left || !right) { unhandled(occurrence, "operator without both operands"); continue; }
        bool free = false;
        const string l = value_of(*left, free);
        const string r = value_of(*right, free);
        if (free) { unhandled(occurrence, "operator operand not related to the inputs"); continue; }
        if (l.find('$') != string::npos || r.find('$') != string::npos) {
          // When the generic check fixed the operator's result type (e.g. by
          // unifying it with the binding it is assigned to), that fixed type
          // is part of the requirement, not only that the operator exists.
          string fixed;
          if (!open_var) {
            bool result_free = false;
            const string result = value_of(occurrence, result_free);
            // Only a concrete fixed result: an open one is the solver's
            // inference choice (e.g. result := left operand), not a legality
            // constraint of the operator.
            if (!result_free && !result.empty() && result.find('$') == string::npos)
              fixed = "|=" + result;
          }
          require("op|" + occurrence.operation_id + "|" + l + "|" + r + fixed);
        }
        if (open_var)
          context.derived[sumt_var_key(resolved->var_id)] =
              sumt_operator_class(occurrence.operation_id) + "(" + l + "," + r + ")";
      }
    }
  }

  // Callback contracts the checker recorded where it deferred a stage over a
  // callable parameter. Proof of concept: `filter` only (the element type is
  // preserved, so the stage result is unaffected by the callback).
  for (const auto& relation : state.sumt_deferred_relations) {
    origin_line = relation.line;
    if (relation.kind != "callback") continue;
    vector<string> parts;
    for (size_t from = 0;;) {
      const size_t bar = relation.origin.find('|', from);
      parts.push_back(relation.origin.substr(from, bar == string::npos ? string::npos : bar - from));
      if (bar == string::npos) break;
      from = bar + 1;
    }
    if (parts.size() < 3 || parts[0] != "filter") continue;
    size_t ordinal = function.params.size();
    for (size_t index = 0; index < function.params.size(); ++index)
      if (function.params[index].name == parts[1]) ordinal = index;
    if (ordinal == function.params.size()) continue;
    string inputs;
    for (size_t index = 2; index < parts.size(); ++index) inputs += (index > 2 ? ";" : "") + parts[index];
    require("callback|filter|$" + std::to_string(ordinal) + "|" + inputs);
    callback_lines.insert(relation.line);
  }

  // Coverage: occurrence kinds without a rule, and statement roots whose use
  // is not mere value flow, must not be open.
  for (const auto& occurrence : state.expression_occurrences) {
    if (callback_lines.count(occurrence.line) &&
        (occurrence.kind == Kind::Pipeline || occurrence.kind == Kind::PipelineStage))
      continue;  // explained by the recorded callback contract or pipeline
    {
      // Occurrences inside a recorded pipeline are explained by it.
      bool inside = false;
      for (uint32_t at = occurrence.parent_id; at != UINT32_MAX && at < state.expression_occurrences.size();
           at = state.expression_occurrences[at].parent_id)
        if (covered_pipelines.count(at)) { inside = true; break; }
      if (inside) continue;
    }
    if ((occurrence.kind == Kind::Call || occurrence.kind == Kind::PipelineStage) &&
        (starts_with(occurrence.semantic_target_id, "parameter:") ||
         starts_with(occurrence.semantic_target_id, "binding:pending:")))
      unhandled(occurrence, "callable parameter used as a call target or callback");
    switch (occurrence.kind) {
      case Kind::Index: case Kind::Field: {
        if (explained_indexes.count(occurrence.id)) break;  // checked at Inst
        // The checker resolved an index or field against the subject's
        // structured checked term (vector[$k], a nominal with field terms):
        // the relation is already in the terms Inst matches. A subject that
        // is a bare or unrelated variable was not resolved.
        const auto* subject = child_with_role(occurrence.id,
            occurrence.kind == Kind::Index ? "container" : "receiver");
        bool free = false;
        const string subject_term = subject ? value_of(*subject, free) : string();
        const bool structured = subject && !free && subject_term.find('[') != string::npos;
        bool other_free = false;
        for (const auto* child : children[occurrence.id])
          if (child != subject) value_of(*child, other_free);
        value_of(occurrence, other_free);
        if (!structured || other_free) {
          bool any_open = open(occurrence);
          for (const auto* child : children[occurrence.id]) any_open = any_open || open(*child);
          if (any_open) unhandled(occurrence, string(occurrence.kind == Kind::Index ? "index" : "field") +
              " over an open subject not resolved by the checker");
        }
        break;
      }
      case Kind::Unary: case Kind::Pipeline:
      case Kind::PipelineStage: case Kind::Message: case Kind::Constructor:
      case Kind::CollectionLiteral: case Kind::Unknown: {
        bool any_open = open(occurrence);
        for (const auto* child : children[occurrence.id]) any_open = any_open || open(*child);
        if (any_open) unhandled(occurrence, "no summary rule for " +
            std::to_string(static_cast<int>(occurrence.kind)) + "-kind occurrence over an open term");
        break;
      }
      default: break;
    }
  }
  for (const auto& [slot, occurrence_id] : state.expression_root_slots) {
    if (slot.second == "return-value" || slot.second == "initializer") continue;
    if (slot.second == "for-iterable" &&
        std::any_of(state.sumt_deferred_relations.begin(), state.sumt_deferred_relations.end(),
                    [&](const auto& relation) { return relation.kind == "element"; }))
      continue;  // explained by the recorded element relation and its iterable requirement
    if (occurrence_id >= state.expression_occurrences.size()) continue;
    const auto& root = state.expression_occurrences[occurrence_id];
    if (slot.second == "assignment-place" &&
        std::any_of(state.sumt_deferred_relations.begin(), state.sumt_deferred_relations.end(),
                    [&](const auto& relation) {
                      return relation.kind == "same_type" && relation.line == root.line;
                    }))
      continue;  // explained by the place/value relation the checker recorded
    if (open(root)) unhandled(root, "open term in statement slot '" + slot.second + "'");
  }

  // Results: every checked return-value root.
  vector<string> results;
  for (const auto& [slot, occurrence_id] : state.expression_root_slots) {
    if (slot.second != "return-value") continue;
    if (occurrence_id >= state.expression_occurrences.size()) {
      fold.missing_facts.push_back("return root points outside the ledger");
      continue;
    }
    const auto& root = state.expression_occurrences[occurrence_id];
    bool free = false;
    const string term = value_of(root, free);
    if (free) {
      if (!root.checked_type)
        fold.missing_facts.push_back("return at line " + std::to_string(root.line) +
                                     " has no checked type");
      else
        fold.missing_representation.push_back("return at line " +
            std::to_string(root.line) + " is not related to the inputs");
      continue;
    }
    results.push_back(term);
  }
  const bool declared_unit = !function.return_type ||
      canonical_type_name(*function.return_type) == "unit";
  if (results.empty())
    fold.result = declared_unit ? "unit" : "?none";
  else
    fold.result = results.front();
  for (size_t index = 1; index < results.size(); ++index)
    if (results[index] != results.front())
      require("same|" + results.front() + "|" + results[index]);

  // Relations the generic check deferred to specialization, recorded where it
  // deferred them (e.g. a push argument against the element type).
  for (const auto& relation : state.sumt_deferred_relations) {
    origin_line = relation.line;
    bool free = false;
    if (relation.kind == "result") continue;  // folded into derived terms above
    if (relation.kind == "callback" || relation.kind == "stage" || relation.kind == "pipeline")
      continue;  // folded above
    if (relation.kind == "binding_value") {
      // The value is the initializer root checked at this line.
      const BodyTypeState::ExpressionOccurrence* value = nullptr;
      for (const auto& [slot, occurrence_id] : state.expression_root_slots)
        if (slot.second == "initializer" && occurrence_id < state.expression_occurrences.size() &&
            state.expression_occurrences[occurrence_id].line == relation.line)
          value = &state.expression_occurrences[occurrence_id];
      const string binding = sumt_render(resolve_relation(relation.left), context, free);
      const string term = value ? value_of(*value, free) : string();
      if (!value || free) {
        fold.missing_representation.push_back("deferred " + relation.origin + " at line " +
            std::to_string(relation.line) + " is not related to the inputs");
        continue;
      }
      if (binding != term) require("same|" + binding + "|" + term);
      continue;
    }
    if (relation.kind == "element") {
      sumt_render(resolve_relation(relation.left), context, free);
      if (free)
        fold.missing_representation.push_back("deferred " + relation.origin + " at line " +
            std::to_string(relation.line) + " is not related to the inputs");
      continue;
    }
    const string left = sumt_render(resolve_relation(relation.left), context, free);
    const string right = sumt_render(resolve_relation(relation.right), context, free);
    if (free) {
      fold.missing_representation.push_back("deferred " + relation.origin + " at line " +
          std::to_string(relation.line) + " is not related to the inputs");
      continue;
    }
    if (left != right) require("same|" + left + "|" + right);
  }

  // Ownership evidence for this function, from its own ownership walk.
  std::set<int> lines_with_obligations;
  bool match_skipped = false;
  for (const auto& event : sumt_ownership_events_) {
    if (event.function != function.name) continue;
    origin_line = event.use_line ? event.use_line : event.transfer_line;
    if (event.kind == "ownership_deferred_generic_match") match_skipped = true;
    if (event.kind == "implicit_copy_unless_primitive") {
      // The value read is the index occurrence checked on that line.
      bool found = false;
      for (const auto& occurrence : state.expression_occurrences) {
        if (occurrence.kind != Kind::Index || occurrence.line != event.use_line) continue;
        bool free = false;
        const string term = value_of(occurrence, free);
        if (free) continue;
        require("primitive|" + term);
        found = true;
      }
      if (!found)
        fold.missing_representation.push_back("implicit-copy decision for '" + event.binding +
            "' at line " + std::to_string(event.use_line) + " is not related to the inputs");
      lines_with_obligations.insert(event.transfer_line);
      continue;
    }
    if (sumt_obligation_kind(event.kind)) {
      // The obligation becomes a requirement over the inputs, so a caller
      // composes it by the same substitution it uses for every requirement.
      const string term = sumt_placeholder_term(event.type, function);
      if (term.empty())
        fold.missing_representation.push_back(event.kind + " of '" + event.binding +
            "' at line " + std::to_string(event.use_line) + " is over a non-parameter type " +
            event.type);
      else
        require("copy|" + term);
    }
    lines_with_obligations.insert(event.transfer_line);
    if (event.use_line) lines_with_obligations.insert(event.use_line);
  }
  if (match_skipped)
    fold.missing_facts.push_back("ownership is not checked for this generic match "
                                 "(deferred_match); no ownership evidence");
  for (const auto& [key, count] : sumt_placeholder_defaults_) {
    if (std::get<0>(key) != function.name) continue;
    if (lines_with_obligations.count(std::get<1>(key))) continue;
    // An assumed transfer is the restrictive default: the walk treated the
    // value as moving, and the generic check still passed. For a copy type
    // the concrete check moves less, so this site cannot add an error there;
    // for a non-copy type the assumption is exact. It needs no obligation.
    // The permissive default (assumed no transfer) can hide an error and
    // must be backed by a recorded obligation, or the entry is incomplete.
    if (starts_with(std::get<2>(key), "transfer_type: assumed transfer")) continue;
    fold.missing_facts.push_back("placeholder-dependent ownership decision at line " +
        std::to_string(std::get<1>(key)) + " has no recorded obligation (" +
        std::get<2>(key) + ")");
  }

  // Conditional parameter access: the legacy fixed mode, raised by recorded
  // transfers of the parameter binding itself.
  for (size_t index = 0; index < function.params.size(); ++index) {
    string mode = "read";
    if (function.open_summary && index < function.open_summary->parameter_access.size()) {
      const auto legacy = function.open_summary->parameter_access[index];
      mode = legacy == moss::AccessMode::Consume ? "consume" :
             legacy == moss::AccessMode::Write ? "write" : "read";
    }
    const string identity = "parameter:" + std::to_string(index);
    for (const auto& event : sumt_ownership_events_) {
      if (event.function != function.name || event.identity != identity) continue;
      if (event.kind == "deferred_unconditional_transfer") mode = "consume";
      else if ((event.kind == "conditional_transfer" || event.kind == "assumed_transfer") &&
               mode != "consume")
        mode = "consume_unless_copy";
    }
    fold.access.push_back(mode);
  }
  return finish();
}

// phase: "generic_check" right after the function's own generic check (so a
// later error elsewhere cannot hide it), "final" after summary projection.
void sumt_shadow_emit_function(const Function& function, const char* phase) {
  if (!sumt_shadow_enabled_) return;
  if (compiled_interface_file(function.source_file)) return;
  if (!function.generic && !function.static_dispatch) return;
  // The line is built in three parts: head and legacy go only to the shadow
  // file; `entry` (shadow summary plus ownership) is also what hardening
  // publishes, without the ownership instrumentation.
  std::ostringstream head_text, legacy_text, entry_text, ownership_text;
  std::ostream* out = &head_text;
  {
    *out << "{\"phase\":";
    sumt_json_string(*out, phase);
    *out << ",\"function\":";
    sumt_json_string(*out, function.name);
    *out << ",\"source\":";
    sumt_json_string(*out, function.source_file);

    // Existing producer, for comparison.
    out = &legacy_text;
    *out << ",\"legacy\":{";
    if (function.open_summary) {
      const auto& legacy = *function.open_summary;
      *out << "\"present\":true,\"canonical_result\":";
      sumt_json_string(*out, sumt_canonical_legacy_result(legacy));
      *out << ",\"canonical_members\":";
      {
        vector<string> members;
        for (const auto& member : legacy.member_requirements)
          members.push_back(sumt_legacy_var(member.receiver) + "." + member.member_name);
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        *out << "[";
        for (size_t index = 0; index < members.size(); ++index) {
          if (index) *out << ",";
          sumt_json_string(*out, members[index]);
        }
        *out << "]";
      }
      *out << ",\"scheme_complete\":" << legacy.scheme_complete
           << ",\"complete\":" << legacy.complete
           << ",\"occurrence_ledger_complete\":" << legacy.occurrence_ledger_complete
           << ",\"variables\":" << legacy.bound_variable_count
           << ",\"result\":";
      sumt_json_string(*out, legacy.result_type
          ? legacy.result_type->to_canonical_string() : string("?null"));
      *out << ",\"requirements\":" << legacy.requirements.size()
           << ",\"member_requirements\":[";
      bool first = true;
      for (const auto& member : legacy.member_requirements) {
        if (!first) *out << ",";
        first = false;
        sumt_json_string(*out, member.member_name);
      }
      *out << "],\"effects_unresolved\":" << legacy.observable_effects.unresolved
           << ",\"parameter_access\":[";
      for (size_t index = 0; index < legacy.parameter_access.size(); ++index) {
        if (index) *out << ",";
        const auto mode = legacy.parameter_access[index];
        *out << (mode == moss::AccessMode::Consume ? "\"consume\"" :
                 mode == moss::AccessMode::Write ? "\"write\"" : "\"read\"");
      }
      *out << "]";
    } else {
      *out << "\"present\":false";
    }
    *out << "}";

    // Shadow fold over the retained generic body.
    const auto& fold = sumt_fold(function);
    if (std::getenv("MOSS_SUMT_SHADOW_DUMP")) {
      auto retained = checked_body_states_.find(&function.body);
      if (retained != checked_body_states_.end() && retained->second)
        for (const auto& relation : retained->second->sumt_deferred_relations)
          std::cerr << "SUMT_REL|" << function.name << "|" << relation.kind << "|" << relation.origin
                    << "|line=" << relation.line << "|occ=" << relation.occurrence << "\n";
      if (retained != checked_body_states_.end() && retained->second)
        for (const auto& occurrence : retained->second->expression_occurrences) {
          std::cerr << "SUMT_OCC|" << function.name << "|" << occurrence.id
                    << "|kind=" << static_cast<int>(occurrence.kind)
                    << "|parent=" << occurrence.parent_id
                    << "|role=" << occurrence.child_role
                    << "|op=" << occurrence.operation_id
                    << "|target=" << occurrence.semantic_target_id
                    << "|binding=" << occurrence.resolved_binding_id
                    << "|slot=" << occurrence.root_slot
                    << "|typed=" << static_cast<bool>(occurrence.checked_type)
                    << "|type=" << (occurrence.checked_type
                        ? [&]() -> string { try { return retained->second->solver.resolve(occurrence.checked_type)->to_canonical_string(); } catch (const std::invalid_argument&) { return "<foreign>"; } }()
                        : string());
          for (const auto& constraint : occurrence.linked_constraints) {
            std::cerr << "|constraint=" << static_cast<int>(constraint.kind) << ":"
                      << constraint.subject << ":" << constraint.detail << ":(";
            for (const auto& argument : constraint.arguments) std::cerr << argument << ";";
            std::cerr << ")->" << constraint.result;
          }
          std::cerr << "\n";
        }
    }
    auto write_list = [&](const vector<string>& values) {
      *out << "[";
      bool first_value = true;
      for (const auto& value : values) {
        if (!first_value) *out << ",";
        first_value = false;
        sumt_json_string(*out, value);
      }
      *out << "]";
    };
    out = &entry_text;
    *out << ",\"shadow\":{\"parameters\":";
    write_list(fold.parameters);
    *out << ",\"result\":";
    sumt_json_string(*out, fold.result);
    *out << ",\"access\":";
    write_list(fold.access);
    *out << ",\"requirements\":";
    write_list(fold.requirements);
    *out << ",\"callees\":";
    write_list(vector<string>(fold.callees.begin(), fold.callees.end()));
    *out << ",\"missing_facts\":";
    write_list(fold.missing_facts);
    *out << ",\"missing_representation\":";
    write_list(fold.missing_representation);
    *out << ",\"notes\":";
    write_list(fold.notes);
    *out << ",\"requirement_origins\":{";
    {
      bool first = true;
      for (const auto& [requirement, origin] : fold.origins) {
        *out << (first ? "" : ",");
        sumt_json_string(*out, requirement);
        *out << ":";
        sumt_json_string(*out, origin);
        first = false;
      }
    }
    *out << "}";
    *out << ",\"status\":";
    sumt_json_string(*out, fold.status);
    *out << "}";
    bool first = true;

    // Ownership instrumentation for this function. The fold has consumed it
    // (status, access, copy obligations); it records the checker's own walk,
    // which can see the consumer's types, so it is not published.
    out = &ownership_text;
    *out << ",\"ownership\":{\"events\":[";
    first = true;
    for (const auto& event : sumt_ownership_events_) {
      if (event.function != function.name) continue;
      if (!first) *out << ",";
      first = false;
      *out << "{\"kind\":";
      sumt_json_string(*out, event.kind);
      *out << ",\"binding\":";
      sumt_json_string(*out, event.binding);
      *out << ",\"identity\":";
      sumt_json_string(*out, event.identity);
      *out << ",\"type\":";
      sumt_json_string(*out, event.type);
      *out << ",\"transfer_line\":" << event.transfer_line
           << ",\"use_line\":" << event.use_line << "}";
    }
    *out << "],\"placeholder_defaults\":[";
    first = true;
    for (const auto& [key, count] : sumt_placeholder_defaults_) {
      if (std::get<0>(key) != function.name) continue;
      if (!first) *out << ",";
      first = false;
      *out << "{\"line\":" << std::get<1>(key) << ",\"default\":";
      sumt_json_string(*out, std::get<2>(key));
      *out << ",\"count\":" << count << "}";
    }
    *out << "]}}";
  }
  if (const char* target = std::getenv("MOSS_SUMT_SHADOW")) {
    std::ofstream file;
    std::ostream* sink = &std::cerr;
    if (string(target) != "-" && *target) {
      file.open(target, std::ios::app);
      if (file) sink = &file;
    }
    *sink << head_text.str() << legacy_text.str() << entry_text.str()
          << ownership_text.str() << "\n";
  }
  if (p_.sumt_harden) {
    std::ostringstream hardened;
    hardened << "{\"function\":";
    sumt_json_string(hardened, function.name);
    hardened << entry_text.str() << "}";
    p_.sumt_hardened[function.name] = hardened.str();
  }
}

// The generic check of `function` itself raised a diagnostic. Today's checker
// then rejects every program containing the function, so the prediction is
// "reject". The entry is NOT complete: whether the failure is independent of
// the substitution (a genuine universal rejection) or caused by unresolved
// generic information cannot be established, because the failed check
// retains no error provenance. Invariant: only a rejection proven
// type-independent may be recorded as universal; none is, today.
void sumt_shadow_emit_rejected(const Function& function) {
  if (!sumt_shadow_enabled_ || !std::getenv("MOSS_SUMT_SHADOW")) return;
  if (!function.generic && !function.static_dispatch) return;
  const string target = std::getenv("MOSS_SUMT_SHADOW");
  std::ofstream file;
  std::ostream* out = &std::cerr;
  if (target != "-" && !target.empty()) {
    file.open(target, std::ios::app);
    if (file) out = &file;
  }
  *out << "{\"phase\":\"generic_rejected\",\"function\":";
  sumt_json_string(*out, function.name);
  *out << ",\"legacy\":{},\"shadow\":{\"status\":\"rejected_unresolved\",\"requirements\":[],"
          "\"result\":\"?none\",\"callees\":[],\"missing_facts\":[\"generic check rejected; "
          "type-independence not established\"]},\"ownership\":{\"events\":[]}}\n";
}


// ---------------------------------------------------------------------------
// R6: canonical form of the legacy producer's summary. Legacy summary
// variables are rendered $k; a variable that is the result of a member
// requirement is rendered by its path ($k.member.result), which is the
// shadow fold's vocabulary for the same fact.
static string sumt_legacy_var(const moss::TypeRef& type) {
  if (!type) return "?null";
  if (type->kind == moss::TypeKind::InferenceVar)
    return "$" + std::to_string(type->var_id.ordinal);
  return type->to_canonical_string();
}

static string sumt_canonical_legacy_result(const moss::OpenCallableSummaryFacts& legacy) {
  if (!legacy.result_type) return "?null";
  std::map<string, string> member_results;
  for (const auto& member : legacy.member_requirements)
    if (member.result && *member.result &&
        (*member.result)->kind == moss::TypeKind::InferenceVar)
      member_results[sumt_legacy_var(*member.result)] =
          sumt_legacy_var(member.receiver) + "." + member.member_name + ".result";
  std::function<string(const moss::TypeRef&)> render = [&](const moss::TypeRef& type) -> string {
    if (!type) return "?null";
    if (type->kind == moss::TypeKind::InferenceVar) {
      const string var = sumt_legacy_var(type);
      auto member = member_results.find(var);
      return member == member_results.end() ? var : member->second;
    }
    if (type->type_args.empty() && type->field_substitutions.empty())
      return type->to_canonical_string();
    string out = type->name + "[";
    bool first = true;
    for (const auto& argument : type->type_args) {
      if (!first) out += ",";
      first = false;
      out += render(argument);
    }
    for (const auto& field : type->field_substitutions) {
      if (!first) out += ",";
      first = false;
      out += field.first + "=" + render(field.second);
    }
    return out + "]";
  };
  return render(legacy.result_type);
}

// ---------------------------------------------------------------------------
// R7: instantiation (Inst) and the compiler-side validator.
//
// Inst(S, sigma) evaluates a shadow entry at concrete parameter types using
// the checker's own expression typing and method resolution (no rule is
// restated here). The oracle is the checker's existing specialization entry
// point, register_static_specialization(), run on a fresh copy of the
// program in a nested checker so the live check is untouched.

std::unique_ptr<Program> sumt_pristine_;

void sumt_capture_pristine() {
  if (!sumt_shadow_enabled_ || !std::getenv("MOSS_SUMT_VALIDATE")) return;
  sumt_pristine_ = std::make_unique<Program>(p_);
  sumt_pristine_->type_context.reset();
  sumt_pristine_->typed_semantics.reset();
}

mutable string sumt_failing_requirement_;
struct SumtVerdict {
  string verdict;  // accept | reject | unknown
  string result;   // result type when accepted
  string reason;
  string origin;   // prediction: origin chain of the failing requirement; oracle: error line
  vector<string> access;  // per-parameter effect when accepted (read|write|consume)
};

static string sumt_class_operator(const string& cls) {
  if (cls == "add") return "+";
  if (cls == "arith") return "*";
  if (cls == "integral") return "%";
  if (cls == "ordered") return "<";
  if (cls == "equatable") return "==";
  return "";
}

// Turn a shadow term into an expression over environment bindings, so the
// checker's own expression typing evaluates it at sigma.
std::optional<string> sumt_term_expression(const string& term, TypeEnv& env,
                                           const vector<string>& sigma,
                                           int& fresh) const {
  const string value = trim(term);
  if (value.empty() || value[0] == '?') return std::nullopt;
  if (value[0] == '#') return value.substr(1);  // a literal, used as written
  if (starts_with(value, "index(") && value.back() == ')') {
    const string inner = value.substr(6, value.size() - 7);
    int depth = 0;
    size_t split = string::npos;
    for (size_t at = 0; at < inner.size(); ++at) {
      const char c = inner[at];
      if (c == '(' || c == '[') ++depth;
      else if (c == ')' || c == ']') --depth;
      else if (c == ',' && depth == 0) { split = at; break; }
    }
    if (split == string::npos) return std::nullopt;
    auto container = sumt_term_expression(inner.substr(0, split), env, sigma, fresh);
    auto key = sumt_term_expression(inner.substr(split + 1), env, sigma, fresh);
    if (!container || !key) return std::nullopt;
    return *container + "[" + *key + "]";
  }
  if (starts_with(value, "pipeline(") && value.back() == ')') {
    const string inner = value.substr(9, value.size() - 10);
    const size_t split = inner.find(';');
    if (split == string::npos) return std::nullopt;
    auto source = sumt_term_expression(inner.substr(0, split), env, sigma, fresh);
    if (!source) return std::nullopt;
    string text;
    const string stages = inner.substr(split + 1);
    for (size_t at = 0; at < stages.size();) {
      const size_t open_brace = stages.find('{', at);
      if (open_brace == string::npos) { text += stages.substr(at); break; }
      text += stages.substr(at, open_brace - at);
      const size_t close_brace = stages.find('}', open_brace);
      const string token = stages.substr(open_brace + 1, close_brace - open_brace - 1);
      const size_t colon = token.find(':');
      const string term = token.substr(colon + 1);
      if (token.substr(0, colon) == "fn") {
        const size_t ordinal = std::stoul(term.substr(1));
        if (ordinal >= sigma.size() || sigma[ordinal].empty()) return std::nullopt;
        text += sigma[ordinal];
      } else {
        auto bound = sumt_term_expression(term, env, sigma, fresh);
        if (!bound) return std::nullopt;
        text += *bound;
      }
      at = close_brace + 1;
    }
    for (size_t at = text.find(" ~> "); at != string::npos; at = text.find(" ~> ", at))
      text.replace(at, 4, " |> ");
    return "(" + *source + " |> " + text + ")";
  }
  if (starts_with(value, "stage(") && value.back() == ')') {
    // stage(op,$i,source[,init]): the pipeline `source |> op([init,] <sigma[i]>)`.
    const string inner = value.substr(6, value.size() - 7);
    vector<string> operands;
    int depth = 0;
    size_t from = 0;
    for (size_t index = 0; index <= inner.size(); ++index) {
      if (index < inner.size()) {
        const char c = inner[index];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        if (!(c == ',' && depth == 0)) continue;
      }
      operands.push_back(inner.substr(from, index - from));
      from = index + 1;
    }
    if (operands.size() < 3 || operands.size() > 4) return std::nullopt;
    const string& callable = operands[1];
    if (callable.size() < 2 || callable[0] != '$') return std::nullopt;
    const size_t ordinal = std::stoul(callable.substr(1));
    if (ordinal >= sigma.size() || sigma[ordinal].empty()) return std::nullopt;
    auto source = sumt_term_expression(operands[2], env, sigma, fresh);
    if (!source) return std::nullopt;
    string arguments = sigma[ordinal];
    if (operands.size() == 4) {
      auto initializer = sumt_term_expression(operands[3], env, sigma, fresh);
      if (!initializer) return std::nullopt;
      arguments = *initializer + ", " + arguments;
    }
    return "(" + *source + " |> " + operands[0] + "(" + arguments + "))";
  }
  if (ends_with(value, ".element")) {
    // Element of an iterable term: the checker's iteration rule decides it;
    // bind a fresh name of that type.
    auto element = sumt_element_type(value.substr(0, value.size() - 8), env, sigma, fresh);
    if (!element) return std::nullopt;
    const string name = "__sumt_c" + std::to_string(fresh++);
    env.bindings[name] = moss::Type::parse_canonical(*element).value_or(moss::Type::primitive(*element));
    return name;
  }
  if (value[0] == '$') {
    size_t end = 1;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) ++end;
    const auto ordinal = std::stoul(value.substr(1, end - 1));
    if (ordinal >= sigma.size() || sigma[ordinal].empty()) return std::nullopt;
    string expression = "__sumt_p" + std::to_string(ordinal);
    if (end == value.size()) return expression;
    // <receiver>.member.result or <receiver>.member(a;b).result, where the
    // receiver is $k or itself a member result (chains: $0.read(#0;#4).result.length.result).
    const string rest = value.substr(end);
    if (!starts_with(rest, ".") || rest.size() < 9 ||
        rest.substr(rest.size() - 7) != ".result") return std::nullopt;
    const string chain = value.substr(0, value.size() - 7);  // drop the last ".result"
    // The last member segment: scan back over a trailing (...) and the name.
    size_t cut = chain.size();
    if (chain.back() == ')') {
      int depth = 0;
      for (size_t at = chain.size(); at-- > 0;) {
        if (chain[at] == ')') ++depth;
        else if (chain[at] == '(' && --depth == 0) { cut = at; break; }
      }
    }
    const size_t dot = chain.rfind('.', cut == chain.size() ? string::npos : cut);
    if (dot == string::npos || dot < end) return std::nullopt;
    if (dot > end) {
      auto receiver = sumt_term_expression(chain.substr(0, dot), env, sigma, fresh);
      if (!receiver) return std::nullopt;
      expression = *receiver;
    }
    const string call = chain.substr(dot);
    const size_t open_paren = call.find('(');
    if (open_paren == string::npos) return expression + call + "()";
    if (call.back() != ')') return std::nullopt;
    const string inner = call.substr(open_paren + 1, call.size() - open_paren - 2);
    string arguments;
    int depth = 0;
    size_t from = 0;
    for (size_t index = 0; index <= inner.size(); ++index) {
      if (index < inner.size()) {
        const char c = inner[index];
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        if (!(c == ';' && depth == 0)) continue;
      }
      auto argument = sumt_term_expression(inner.substr(from, index - from), env, sigma, fresh);
      if (!argument) return std::nullopt;
      arguments += (arguments.empty() ? "" : ", ") + *argument;
      from = index + 1;
    }
    return expression + call.substr(0, open_paren) + "(" + arguments + ")";
  }
  for (const char* cls : {"add", "arith", "integral", "ordered", "equatable"}) {
    const string head = string(cls) + "(";
    if (!starts_with(value, head) || value.back() != ')') continue;
    const string inner = value.substr(head.size(), value.size() - head.size() - 1);
    int depth = 0;
    size_t split = string::npos;
    for (size_t index = 0; index < inner.size(); ++index) {
      if (inner[index] == '(' || inner[index] == '[') ++depth;
      else if (inner[index] == ')' || inner[index] == ']') --depth;
      else if (inner[index] == ',' && depth == 0) { split = index; break; }
    }
    if (split == string::npos) return std::nullopt;
    auto left = sumt_term_expression(inner.substr(0, split), env, sigma, fresh);
    auto right = sumt_term_expression(inner.substr(split + 1), env, sigma, fresh);
    if (!left || !right) return std::nullopt;
    return "(" + *left + " " + sumt_class_operator(cls) + " " + *right + ")";
  }
  // A concrete term (possibly containing $k inside a constructor): substitute
  // and bind a fresh name to it.
  bool free = false;
  const string concrete = sumt_substitute(value, sigma, free);
  if (free || concrete.find('$') != string::npos || concrete.find("[]") != string::npos)
    return std::nullopt;
  const string name = "__sumt_c" + std::to_string(fresh++);
  env.bindings[name] = moss::Type::parse_canonical(concrete).value_or(
      moss::Type::primitive(concrete));
  return name;
}

// The checker's own expression check (not only its typing) on a synthesized
// expression over sigma-typed bindings.
bool sumt_checks(const string& expression, TypeEnv& env, string& why) const {
  try {
    const_cast<Checker*>(this)->check_expression(0, expression, env.to_string_map());
    return true;
  } catch (const CompileError& error) {
    why = error.code.empty() ? string(error.what()) : error.code + ": " + error.what();
    return false;
  } catch (const std::exception& error) {
    why = error.what();
    return false;
  }
}

// Element type of an iterable term, by the checker's own iteration rule.
std::optional<string> sumt_element_type(const string& term, TypeEnv& env,
                                        const vector<string>& sigma, int& fresh) const {
  auto source = sumt_type_of(term, env, sigma, fresh);
  if (!source) return std::nullopt;
  const string name = "__sumt_it" + std::to_string(fresh++);
  env.bindings[name] = moss::Type::parse_canonical(*source).value_or(moss::Type::primitive(*source));
  try {
    auto element = iterator_element_type(0, name, env);
    if (!element || element->empty() || starts_with(*element, "_iterator_element:") ||
        contains_internal_placeholder(*element)) return std::nullopt;
    return canonical_type_name(*element);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<string> sumt_type_of(const string& term, TypeEnv& env,
                                   const vector<string>& sigma, int& fresh) const {
  auto expression = sumt_term_expression(term, env, sigma, fresh);
  if (!expression) return std::nullopt;
  try {
    auto type = inferred_expr_type(*expression, env);
    if (!type || type->empty() || contains_internal_placeholder(*type) ||
        *type == "unknown") return std::nullopt;
    return canonical_type_name(*type);
  } catch (...) {
    return std::nullopt;
  }
}

// Structural match of a rendered parameter term against a concrete canonical
// type: each $k captures one balanced type and must capture it consistently.
static bool sumt_match_term(const string& pattern, const string& concrete,
                            std::map<size_t, string>& bound) {
  size_t i = 0, j = 0;
  while (i < pattern.size()) {
    if (pattern[i] == '$' && i + 1 < pattern.size() &&
        std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
      size_t end = i + 1;
      while (end < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[end]))) ++end;
      const size_t ordinal = std::stoul(pattern.substr(i + 1, end - i - 1));
      const char stop = end < pattern.size() ? pattern[end] : '\0';
      const size_t start = j;
      int depth = 0;
      while (j < concrete.size()) {
        const char c = concrete[j];
        if (depth == 0 && stop && c == stop) break;
        if (c == '[' || c == '(') ++depth;
        else if (c == ']' || c == ')') { if (depth == 0) break; --depth; }
        ++j;
      }
      const string value = concrete.substr(start, j - start);
      if (value.empty()) return false;
      auto found = bound.find(ordinal);
      if (found != bound.end() && found->second != value) return false;
      bound[ordinal] = value;
      i = end;
      continue;
    }
    if (j >= concrete.size() || pattern[i] != concrete[j]) return false;
    ++i; ++j;
  }
  return j == concrete.size();
}

SumtVerdict sumt_instantiate(const Function& function, const SumtFold& fold,
                             const vector<string>& sigma_in) const {
  if (fold.status != "complete") return {"unknown", "", "entry " + fold.status};
  // The generic check's own conclusion about each parameter's type: a bare
  // $i takes sigma[i]; a concrete or structured term must match sigma[i],
  // binding the variables nested in it.
  std::map<size_t, string> bound;
  for (size_t index = 0; index < fold.parameters.size() && index < sigma_in.size(); ++index)
    if (fold.parameters[index] == "$" + std::to_string(index))
      bound[index] = canonical_type_name(sigma_in[index]);
  for (size_t index = 0; index < fold.parameters.size() && index < sigma_in.size(); ++index) {
    if (fold.parameters[index] == "$" + std::to_string(index)) continue;
    if (traits_.count(fold.parameters[index])) {
      // A trait-bounded parameter: the checker's own conformance test.
      string why;
      if (!trait_conforms(canonical_type_name(sigma_in[index]), fold.parameters[index], &why))
        return {"reject", "", "parameter " + std::to_string(index) + " does not satisfy " +
                fold.parameters[index] + ": " + why};
      continue;
    }
    if (!sumt_match_term(fold.parameters[index], canonical_type_name(sigma_in[index]), bound))
      return {"reject", "", "parameter " + std::to_string(index) + " must be " +
              fold.parameters[index] + ", found " + canonical_type_name(sigma_in[index])};
  }
  // Call-site constraints the checker recorded for the callee's parameters:
  // an indexed parameter needs an indexable argument (the call check's rule).
  for (size_t index = 0; index < function.params.size() && index < sigma_in.size(); ++index) {
    if (!has_constraint(function, function.params[index].name)) continue;
    for (const auto& op : constraint_details(function, function.params[index].name)) {
      if (op != "[]") continue;
      auto shape = moss::Type::parse_canonical(canonical_type_name(sigma_in[index]));
      if (!shape || !indexable_argument_shape(*shape))
        return {"reject", "", "argument " + std::to_string(index + 1) + " (" +
                canonical_type_name(sigma_in[index]) + ") is not an indexable container"};
    }
  }
  vector<string> sigma = sigma_in;
  for (const auto& [ordinal, value] : bound) {
    if (ordinal >= sigma.size()) sigma.resize(ordinal + 1);
    sigma[ordinal] = value;
  }
  // Ownership obligations are `copy|term` requirements (own and composed).
  TypeEnv env;
  for (size_t index = 0; index < sigma.size(); ++index)
    if (!sigma[index].empty())
      env.bindings["__sumt_p" + std::to_string(index)] =
          moss::Type::parse_canonical(sigma[index]).value_or(moss::Type::primitive(sigma[index]));
  int fresh = 0;
  // Instantiated parameter effects: the base access at sigma, joined with
  // each deferred method effect (#5).
  vector<Effect> effects;
  for (size_t index = 0; index < fold.access.size(); ++index) {
    const string& mode = fold.access[index];
    Effect effect = mode == "consume" ? Effect::Consume : mode == "write" ? Effect::Write : Effect::Read;
    if (mode == "consume_unless_copy")
      effect = index < sigma.size() && !sigma[index].empty() &&
          ownership_transfer_decision(sigma[index]) != TransferDecision::NoTransfer
          ? Effect::Consume : Effect::Read;
    effects.push_back(effect);
  }
  auto bare_parameter = [](const string& term) -> std::optional<size_t> {
    if (term.size() < 2 || term[0] != '$' ||
        term.find_first_not_of("0123456789", 1) != string::npos) return std::nullopt;
    return std::stoul(term.substr(1));
  };
  for (const auto& requirement : fold.requirements) {
    sumt_failing_requirement_ = requirement;
    vector<string> parts;
    size_t from = 0;
    while (true) {
      const size_t bar = requirement.find('|', from);
      parts.push_back(requirement.substr(from, bar == string::npos ? string::npos : bar - from));
      if (bar == string::npos) break;
      from = bar + 1;
    }
    if (parts[0] == "member" && parts.size() >= 3) {
      const string path = parts[1];
      const size_t dot = path.rfind('.');
      if (dot == string::npos) return {"unknown", "", "malformed member requirement"};
      const string member = path.substr(dot + 1);
      auto trait = traits_.find(path.substr(0, dot));
      if (trait != traits_.end()) {
        // Member of a trait bound: checked against the trait's declared
        // signature (conformance of the substituted type is checked above).
        const TraitMethod* declared = nullptr;
        for (const auto& method : trait->second->methods)
          if (method.name == member) declared = &method;
        const size_t arity = parts.size() >= 3 ? std::stoul(parts[2]) : 0;
        if (!declared || declared->params.size() != arity)
          return {"reject", "", "trait " + trait->first + " has no member '" + member + "/" +
                  std::to_string(arity) + "'"};
        if (parts.size() >= 4) {
          size_t from = 0, position = 0;
          while (true) {
            const size_t semi = parts[3].find(';', from);
            auto type = sumt_type_of(parts[3].substr(from, semi == string::npos ? string::npos : semi - from),
                                     env, sigma, fresh);
            if (!type) return {"unknown", "", "member argument not expressible: " + requirement};
            const string expected = canonical_type_name(declared->params[position].type);
            if (!expected.empty() && !is_type_compatible(expected, *type))
              return {"reject", "", trait->first + "." + member + " argument " +
                      std::to_string(position) + " expects " + expected + ", found " + *type};
            ++position;
            if (semi == string::npos) break;
            from = semi + 1;
          }
        }
        continue;
      }
      auto receiver = sumt_type_of(path.substr(0, dot), env, sigma, fresh);
      if (!receiver) return {"unknown", "", "member receiver not expressible: " + requirement};
      vector<string> argument_types;
      if (parts.size() >= 4) {
        size_t from = 0;
        while (true) {
          const size_t semi = parts[3].find(';', from);
          auto type = sumt_type_of(parts[3].substr(from, semi == string::npos ? string::npos : semi - from),
                                   env, sigma, fresh);
          if (!type) return {"unknown", "", "member argument not expressible: " + requirement};
          argument_types.push_back(*type);
          if (semi == string::npos) break;
          from = semi + 1;
        }
      }
      if (!resolve_method(*receiver, member, argument_types)) {
        // Not a declared method (e.g. a builtin member of a collection or
        // string): let the checker check the call expression itself.
        auto receiver_expression = sumt_term_expression(path.substr(0, dot), env, sigma, fresh);
        string arguments_expression;
        bool expressible = static_cast<bool>(receiver_expression);
        if (parts.size() >= 4) {
          size_t from = 0;
          while (expressible) {
            const size_t semi = parts[3].find(';', from);
            auto argument = sumt_term_expression(
                parts[3].substr(from, semi == string::npos ? string::npos : semi - from), env, sigma, fresh);
            if (!argument) { expressible = false; break; }
            arguments_expression += (arguments_expression.empty() ? "" : ", ") + *argument;
            if (semi == string::npos) break;
            from = semi + 1;
          }
        }
        if (!expressible) return {"unknown", "", "member call not expressible: " + requirement};
        string why;
        const string call = *receiver_expression + "." + member + "(" + arguments_expression + ")";
        if (!sumt_checks(call, env, why))
          return {"reject", "", *receiver + " member '" + member + "' rejected: " + why};
      }
    } else if (parts[0] == "effect" && parts.size() >= 3) {
      // The consumer's method, resolved at sigma: its receiver and argument
      // effects apply to the parameters passed directly.
      const string path = parts[1];
      const size_t dot = path.rfind('.');
      if (dot == string::npos) return {"unknown", "", "malformed effect requirement"};
      const string receiver_term = path.substr(0, dot);
      const string member = path.substr(dot + 1);
      auto receiver = sumt_type_of(receiver_term, env, sigma, fresh);
      if (!receiver) return {"unknown", "", "effect receiver not expressible: " + requirement};
      vector<string> argument_terms, argument_types;
      if (parts.size() >= 4) {
        size_t from = 0;
        while (true) {
          const size_t semi = parts[3].find(';', from);
          argument_terms.push_back(parts[3].substr(from, semi == string::npos ? string::npos : semi - from));
          auto type = sumt_type_of(argument_terms.back(), env, sigma, fresh);
          if (!type) return {"unknown", "", "effect argument not expressible: " + requirement};
          argument_types.push_back(*type);
          if (semi == string::npos) break;
          from = semi + 1;
        }
      }
      Effect receiver_effect = Effect::Read;
      vector<Effect> argument_effects(argument_terms.size(), Effect::Read);
      const string concrete = canonical_type_name(*receiver);
      if (const Method* method = resolve_method(*receiver, member, argument_types)) {
        receiver_effect = method->receiver_effect;
        for (size_t index = 0; index < argument_effects.size(); ++index)
          argument_effects[index] = method_parameter_effect(*method, index);
      } else if (const auto* builtin = builtin_operation(concrete, member);
                 builtin && builtin->argument_effects.size() == argument_terms.size()) {
        receiver_effect = builtin->receiver_effect;
        argument_effects = builtin->argument_effects;
      } else if (concrete == "FileIO" && fileio_operation_effect(member)) {
        receiver_effect = *fileio_operation_effect(member);
      } else {
        return {"unknown", "", "no effect model for " + concrete + "." + member};
      }
      {
        // The call's own conflicting-access rule, with the consumer's
        // effects: e.g. c.absorb(c) where absorb writes self.
        vector<string> expressions;
        vector<Effect> call_effects;
        if (auto expression = sumt_term_expression(receiver_term, env, sigma, fresh)) {
          expressions.push_back(*expression);
          call_effects.push_back(receiver_effect);
        }
        for (size_t position = 0; position < argument_terms.size(); ++position)
          if (auto expression = sumt_term_expression(argument_terms[position], env, sigma, fresh)) {
            expressions.push_back(*expression);
            call_effects.push_back(argument_effects[position]);
          }
        try {
          OwnershipEnv ownership;
          ownership.types = env;
          check_conflicting_call_accesses(0, member, expressions, call_effects, ownership);
        } catch (const CompileError& error) {
          return {"reject", "", string("conflicting access: ") + error.what()};
        }
      }
      if (auto index = bare_parameter(receiver_term); index && *index < effects.size())
        effects[*index] = join_effect(effects[*index], receiver_effect);
      for (size_t position = 0; position < argument_terms.size(); ++position)
        if (auto index = bare_parameter(argument_terms[position]); index && *index < effects.size())
          effects[*index] = join_effect(effects[*index], argument_effects[position]);
    } else if (parts[0] == "op" && parts.size() >= 4) {
      auto left = sumt_term_expression(parts[2], env, sigma, fresh);
      auto right = sumt_term_expression(parts[3], env, sigma, fresh);
      if (!left || !right) return {"unknown", "", "operand not expressible: " + requirement};
      std::optional<string> type;
      const string operation = "(" + *left + " " + parts[1] + " " + *right + ")";
      string why;
      if (!sumt_checks(operation, env, why))
        return {"reject", "", "operator " + parts[1] + " rejected: " + why};
      try {
        type = inferred_expr_type(operation, env);
      } catch (...) { type.reset(); }
      if (!type || type->empty() || contains_internal_placeholder(*type))
        return {"reject", "", "operator " + parts[1] + " not defined: " + requirement};
      if (parts.size() >= 5 && starts_with(parts[4], "=")) {
        auto expected = sumt_type_of(parts[4].substr(1), env, sigma, fresh);
        if (!expected) return {"unknown", "", "operator result not expressible: " + requirement};
        if (canonical_type_name(*type) != *expected)
          return {"reject", "", "operator " + parts[1] + " yields " + canonical_type_name(*type) +
                  ", generic check fixed " + *expected};
      }
    } else if ((parts[0] == "stage" || parts[0] == "check") && parts.size() >= 2) {
      auto expression = sumt_term_expression(parts[1], env, sigma, fresh);
      if (!expression) return {"unknown", "", "stage not expressible: " + requirement};
      string why;
      if (!sumt_checks(*expression, env, why))
        return {"reject", "", "stage rejected: " + why};
    } else if (parts[0] == "callback" && parts.size() >= 4) {
      // The substituted callable is checked by the checker's own pipeline
      // stage check against the recorded input types.
      const size_t ordinal = std::stoul(parts[2].substr(1));
      if (ordinal >= sigma.size()) return {"unknown", "", "callback on non-parameter"};
      const string source = "__sumt_cb" + std::to_string(fresh++);
      env.bindings[source] = moss::Type::parse_canonical("vector[" + parts[3] + "]")
                                 .value_or(moss::Type::primitive("vector[" + parts[3] + "]"));
      string why;
      if (!sumt_checks(source + " |> " + parts[1] + "(" + sigma[ordinal] + ")", env, why))
        return {"reject", "", "callback " + sigma[ordinal] + " rejected: " + why};
    } else if (parts[0] == "primitive" && parts.size() >= 2) {
      auto type = sumt_type_of(parts[1], env, sigma, fresh);
      if (!type) return {"unknown", "", "implicit-copy operand not expressible: " + requirement};
      if (!is_implicit_copy_primitive(*type))
        return {"reject", "", "implicit copy of non-primitive " + *type};
    } else if (parts[0] == "iterable" && parts.size() >= 2) {
      if (!sumt_element_type(parts[1], env, sigma, fresh))
        return {"reject", "", "not iterable: " + parts[1]};
    } else if (parts[0] == "copy" && parts.size() >= 2) {
      auto type = sumt_type_of(parts[1], env, sigma, fresh);
      if (!type) return {"unknown", "", "copy() operand not expressible: " + requirement};
      if (ownership_transfer_decision(*type) != TransferDecision::NoTransfer)
        return {"reject", "", "ownership obligation copy(" + *type + ") fails"};
    } else if (parts[0] == "same" && parts.size() >= 3) {
      auto left = sumt_type_of(parts[1], env, sigma, fresh);
      auto right = sumt_type_of(parts[2], env, sigma, fresh);
      if (!left || !right) return {"unknown", "", "same() operand not expressible"};
      if (*left != *right)
        return {"reject", "", "same(" + *left + ", " + *right + ")"};
    } else {
      return {"unknown", "", "unknown requirement " + requirement};
    }
  }
  vector<string> access;
  for (const auto effect : effects) access.push_back(sumt_effect_name(effect));
  if (fold.result == "unit" || fold.result == "?none")
    return {"accept", fold.result, "", "", access};
  auto result = sumt_type_of(fold.result, env, sigma, fresh);
  if (!result) return {"unknown", "", "result not expressible: " + fold.result};
  return {"accept", *result, "", "", access};
}

// The oracle: today's checker, through its specialization entry point.
// Source spelling of a canonical type, for the call-site probe.
static string sumt_source_spelling(const string& canonical) {
  static const std::map<string, string> primitives = {
      {"int", "Int"}, {"float", "Float"}, {"bool", "Bool"}, {"string", "String"}};
  auto primitive = primitives.find(canonical);
  if (primitive != primitives.end()) return primitive->second;
  if (starts_with(canonical, "vector[") && canonical.back() == ']')
    return "Vector[" + sumt_source_spelling(canonical.substr(7, canonical.size() - 8)) + "]";
  return canonical;
}

SumtVerdict sumt_concrete(const Function& function, const vector<string>& sigma, bool call_site,
                          const std::set<size_t>& callables = {}) {
  const string function_name = function.name;
  SumtVerdict verdict{"reject", "", ""};
  auto program = std::make_unique<Program>(*sumt_pristine_);
  program->type_context.reset();
  program->typed_semantics.reset();
  if (call_site) {
    std::ostringstream probe;
    probe << "fn __sumt_probe(";
    bool first = true;
    for (size_t index = 0; index < sigma.size(); ++index) {
      if (callables.count(index)) continue;  // a function name is passed directly
      probe << (first ? "" : ", ") << "__sumt_a" << index << ": " << sumt_source_spelling(sigma[index]);
      first = false;
    }
    // The result is bound, not returned: a probe returning it would make the
    // probe's own return inference part of the verdict. With a callable
    // argument the result is also used, so the specialization is not skipped.
    probe << "):\n  __sumt_r = " << function_name << "(";
    for (size_t index = 0; index < sigma.size(); ++index)
      probe << (index ? ", " : "") << (callables.count(index) ? sigma[index] : "__sumt_a" + std::to_string(index));
    probe << ")\n" << (callables.empty() ? "" : "  echo __sumt_r\n") << "  return 0\n";
    std::istringstream input(probe.str());
    try {
      Program parsed = parse_source(input, function.source_file);
      for (auto& extra : parsed.functions) {
        extra.line += 1000000;  // keep probe lines distinct from the program's
        program->functions.push_back(std::move(extra));
      }
    } catch (const std::exception& error) {
      return {"unknown", "", string("probe did not parse: ") + error.what()};
    }
  }
  sumt_nested_flag() = true;
  try {
    auto checker = std::make_unique<Checker>(*program);
    checker->run();
    Function* target = nullptr;
    for (auto& candidate : program->functions)
      if (candidate.name == function_name) target = &candidate;
    if (!target) {
      verdict = {"unknown", "", "function not found in copy"};
    } else {
      if (!call_site) checker->register_static_specialization(target->line, *target, sigma);
      verdict = {"accept", "", ""};
      for (const auto& specialization : target->specializations) {
        // A callable position is specialized on the function identity, not a
        // type name: match the other positions, and the callable by name.
        bool matches = specialization.parameter_types.size() == sigma.size();
        for (size_t index = 0; matches && index < sigma.size(); ++index)
          matches = callables.count(index)
              ? specialization.parameter_types[index].find(sigma[index]) != string::npos
              : specialization.parameter_types[index] == sigma[index];
        if (matches) {
          verdict.result = canonical_type_name(specialization.return_type);
          verdict.access.clear();
          for (const auto effect : specialization.parameter_effects)
            verdict.access.push_back(sumt_effect_name(effect));
        }
      }
    }
    checker.reset();
  } catch (const CompileError& error) {
    verdict = {"reject", "", error.code.empty() ? string(error.what())
                                                : error.code + ": " + error.what(),
               std::to_string(error.line)};
  } catch (const std::exception& error) {
    // Not a language rejection: the compiler failed internally (e.g. an
    // internal synchronization invariant on a known-bug reproducer). That is
    // no evidence about the summary either way.
    verdict = {"crash", "", error.what()};
  }
  sumt_nested_flag() = false;
  program.reset();
  return verdict;
}

void sumt_validate() {
  if (!sumt_shadow_enabled_ || !sumt_pristine_ || !std::getenv("MOSS_SUMT_SHADOW")) return;
  const string target = std::getenv("MOSS_SUMT_SHADOW");
  std::ofstream file;
  std::ostream* out = &std::cerr;
  if (target != "-" && !target.empty()) {
    file.open(target, std::ios::app);
    if (file) out = &file;
  }
  // Candidate concrete types: primitives, every program nominal type, and a
  // collection.
  const vector<string> primitives = {"int", "float", "bool", "string", "vector[int]",
                                     "vector[string]", "vector[float]"};
  vector<string> candidates = primitives;
  for (const auto& object : p_.objects)
    if (!compiled_interface_file(object.source_file) && object.name.find('.') == string::npos &&
        std::none_of(object.fields.begin(), object.fields.end(),
                     [](const Field& field) { return field.inferred || field.type.empty(); }))
      candidates.push_back(object.name);  // a template type is not a nameable sigma
  vector<string> objects;
  for (size_t index = primitives.size(); index < candidates.size(); ++index)
    objects.push_back(candidates[index]);
  for (const auto& function : p_.functions) {
    if (compiled_interface_file(function.source_file)) continue;
    if (!function.generic && !function.static_dispatch) continue;
    auto memo = sumt_folds_.find(function.name);
    if (memo == sumt_folds_.end()) continue;
    const SumtFold fold = memo->second;
    const auto& pristine = *std::find_if(sumt_pristine_->functions.begin(),
        sumt_pristine_->functions.end(),
        [&](const Function& candidate) { return candidate.name == function.name; });
    // Per-position candidates. An open parameter takes every candidate. A
    // trait-bounded parameter takes every conforming object (valid
    // substitutions) plus non-conforming objects and primitives (invalid
    // substitutions). Any other declared type is fixed.
    vector<vector<string>> positions;
    bool trait_bounded = false, any_open = false;
    for (size_t index = 0; index < function.params.size(); ++index) {
      const string declared = index < pristine.params.size() ? pristine.params[index].type : "";
      if (declared.empty() || contains_internal_placeholder(declared)) {
        positions.push_back(candidates);
        any_open = true;
      } else if (traits_.count(canonical_type_name(declared)) || traits_.count(declared)) {
        const string trait = traits_.count(declared) ? declared : canonical_type_name(declared);
        vector<string> valid, invalid;
        for (const auto& object : objects)
          (trait_conforms(object, trait) ? valid : invalid).push_back(object);
        vector<string> list = valid;
        for (size_t k = 0; k < invalid.size() && k < 2; ++k) list.push_back(invalid[k]);
        list.push_back("int");
        list.push_back("string");
        positions.push_back(list);
        trait_bounded = true;
      } else if (canonical_type_name(declared) == "vector") {
        // A bare collection annotation leaves the element open.
        vector<string> list;
        for (const auto& candidate : candidates)
          if (starts_with(candidate, "vector[")) list.push_back(candidate);
        list.push_back("int");
        positions.push_back(list);
        any_open = true;
      } else {
        positions.push_back({canonical_type_name(declared)});
      }
    }
    // Callable parameters with a recorded callback contract take function
    // names: every non-generic function of the program (valid and invalid
    // callbacks) plus a non-function value.
    std::set<size_t> callables;
    for (const auto& requirement : fold.requirements)
      if (starts_with(requirement, "callback|")) {
        const size_t dollar = requirement.find('$');
        callables.insert(std::stoul(requirement.substr(dollar + 1)));
      } else if (starts_with(requirement, "stage|")) {
        for (size_t at = requirement.find("{fn:$"); at != string::npos;
             at = requirement.find("{fn:$", at + 1))
          callables.insert(std::stoul(requirement.substr(at + 5)));
      }
    for (const size_t index : callables) {
      if (index >= positions.size()) continue;
      vector<string> names;
      for (const auto& other : p_.functions)
        if (!other.generic && !other.static_dispatch && other.name != "main" &&
            other.name != function.name && !other.params.empty() &&
            !compiled_interface_file(other.source_file) && names.size() < 6)
          names.push_back(other.name);
      names.push_back("int");
      positions[index] = names;
    }
    if (!any_open && !trait_bounded) continue;
    // Substitutions: the diagonal over varying positions, plus the cartesian
    // product over the first few candidates of each varying position.
    vector<vector<string>> sigmas;
    std::set<vector<string>> seen;
    auto add = [&](const vector<string>& sigma) { if (seen.insert(sigma).second) sigmas.push_back(sigma); };
    // The program's own specializations come first: they are the known-valid
    // substitutions, so accepted cases are always exercised.
    for (const auto& specialization : function.specializations) {
      if (specialization.parameter_types.size() != positions.size()) continue;
      vector<string> sigma;
      for (size_t index = 0; index < positions.size(); ++index) {
        string type = specialization.parameter_types[index];
        if (callables.count(index)) {
          for (const auto& name : positions[index])
            if (type.find(name) != string::npos) type = name;
        }
        sigma.push_back(type);
      }
      add(sigma);
    }
    const size_t widest = std::max_element(positions.begin(), positions.end(),
        [](const auto& l, const auto& r) { return l.size() < r.size(); })->size();
    for (size_t k = 0; k < widest; ++k) {
      vector<string> sigma;
      for (const auto& list : positions) sigma.push_back(list[std::min(k, list.size() - 1)]);
      add(sigma);
    }
    std::function<void(size_t, vector<string>&)> product = [&](size_t index, vector<string>& sigma) {
      if (sigmas.size() >= 96) return;
      if (index == positions.size()) { add(sigma); return; }
      const size_t limit = positions[index].size() == 1 ? 1 : std::min<size_t>(positions[index].size(), 6);
      for (size_t k = 0; k < limit; ++k) {
        sigma.push_back(positions[index][k]);
        product(index + 1, sigma);
        sigma.pop_back();
      }
    };
    { vector<string> sigma; product(0, sigma); }
    size_t runs = 0, agreed = 0, unobserved = 0, oracle_splits = 0, call_site_unavailable = 0,
           oracle_crashes = 0;
    for (const auto& sigma : sigmas) {
      sumt_failing_requirement_.clear();
      auto predicted = sumt_instantiate(function, fold, sigma);
      if (predicted.verdict == "reject") {
        auto origin = fold.origins.find(sumt_failing_requirement_);
        predicted.origin = sumt_failing_requirement_.empty() ? "parameter"
            : origin != fold.origins.end() ? origin->second : "?";
      }
      // Oracle: a typed call site appended to a pristine copy, which runs the
      // compiler's own call checks (trait conformance, argument types) and
      // then its specialization path. The direct specialization entry is
      // kept as a cross-check where it applies (it does not check trait
      // conformance, which belongs to the call site).
      // Two compiler paths, combined: the typed call site checks what only a
      // call checks (trait conformance, argument types) and the
      // specialization entry checks the body for sigma. The program is
      // accepted for sigma only if both accept; the result type comes from
      // the registered specialization.
      auto call = sumt_concrete(function, sigma, true, callables);
      if (call.verdict == "reject" &&
          call.reason.find("MODULE_IDENTITY_UNRESOLVED") != string::npos)
        call = {"unknown", "", "call-site probe unavailable in a module program"};
      // The direct entry takes types, not function names: skip it when a
      // callable position is substituted.
      const auto direct = callables.empty() ? sumt_concrete(function, sigma, false)
                                            : SumtVerdict{"unknown", "", "direct entry takes no callables"};
      if (call.verdict != direct.verdict && call.verdict != "unknown" && callables.empty()) ++oracle_splits;
      if (call.verdict == "unknown") ++call_site_unavailable;
      SumtVerdict concrete = direct;
      if (!callables.empty() && call.verdict == "unknown" &&
          call.reason.find("probe unavailable") != string::npos) {
        // A function-name argument can only be checked through a call site,
        // and the probe cannot run in a module program: no oracle at all.
        concrete = {"crash", "", "no oracle: function argument in a module program"};
      } else if (call.verdict == "crash" || direct.verdict == "crash") {
        concrete = call.verdict == "crash" ? call : direct;
        concrete.verdict = "crash";
      } else
      if (!callables.empty()) concrete = call;  // only the call site can take a function name
      else if (call.verdict == "reject") concrete = call;
      else if (call.verdict == "unknown" && direct.verdict == "accept" &&
               call.reason.find("probe unavailable") == string::npos)
        concrete = call;  // a probe that could not run is not evidence either way
      const bool verdict_agrees = predicted.verdict == concrete.verdict;
      const bool result_observed = concrete.verdict != "accept" || !concrete.result.empty();
      // Parameter effects (#5): compared where the oracle's specialization
      // reports them. A callable position is the function identity, not an
      // access, and is skipped.
      bool access_agrees = true;
      if (verdict_agrees && predicted.verdict == "accept" && !concrete.access.empty() &&
          concrete.access.size() == predicted.access.size())
        for (size_t index = 0; index < concrete.access.size(); ++index)
          if (!callables.count(index) && concrete.access[index] != predicted.access[index])
            access_agrees = false;
      const bool result_agrees = (!verdict_agrees || predicted.verdict != "accept" ||
          !result_observed || predicted.result == concrete.result) && access_agrees;
      if (concrete.verdict == "crash") ++oracle_crashes;
      else {
        ++runs;
        if (!result_observed) ++unobserved;
        if (verdict_agrees && result_agrees) ++agreed;
      }
      *out << "{\"phase\":\"validate\",\"function\":";
      sumt_json_string(*out, function.name);
      *out << ",\"status\":";
      sumt_json_string(*out, fold.status);
      *out << ",\"sigma\":[";
      for (size_t index = 0; index < sigma.size(); ++index) {
        if (index) *out << ",";
        sumt_json_string(*out, sigma[index]);
      }
      *out << "],\"predicted\":";
      sumt_json_string(*out, predicted.verdict);
      *out << ",\"predicted_result\":";
      sumt_json_string(*out, predicted.result);
      *out << ",\"predicted_reason\":";
      sumt_json_string(*out, predicted.reason);
      *out << ",\"concrete\":";
      sumt_json_string(*out, concrete.verdict);
      *out << ",\"concrete_result\":";
      sumt_json_string(*out, concrete.result);
      *out << ",\"concrete_reason\":";
      sumt_json_string(*out, concrete.reason);
      auto write_access = [&](const vector<string>& access) {
        *out << "[";
        for (size_t index = 0; index < access.size(); ++index) {
          if (index) *out << ",";
          sumt_json_string(*out, access[index]);
        }
        *out << "]";
      };
      *out << ",\"predicted_access\":";
      write_access(predicted.access);
      *out << ",\"concrete_access\":";
      write_access(concrete.access);
      *out << ",\"access_agrees\":" << (access_agrees ? "true" : "false");
      // Location: the deepest origin of the failing requirement against the
      // line of the compiler's error.
      string predicted_line;
      if (!predicted.origin.empty()) {
        const string deepest = predicted.origin.substr(0, predicted.origin.find(' '));
        const size_t colon = deepest.rfind(':');
        if (colon != string::npos) predicted_line = deepest.substr(colon + 1);
      }
      *out << ",\"predicted_origin\":";
      sumt_json_string(*out, predicted.origin);
      *out << ",\"concrete_line\":";
      sumt_json_string(*out, concrete.origin);
      *out << ",\"location_agrees\":"
           << (predicted.verdict == "reject" && concrete.verdict == "reject" &&
               !predicted_line.empty() && predicted_line == concrete.origin ? "true" : "false");
      *out << ",\"direct\":";
      sumt_json_string(*out, direct.verdict);
      *out << ",\"call_site\":";
      sumt_json_string(*out, call.verdict);
      *out << ",\"result_observed\":" << (result_observed ? "true" : "false")
           << ",\"agree\":" << (verdict_agrees && result_agrees ? "true" : "false")
           << ",\"result_agrees\":" << (result_agrees ? "true" : "false") << "}\n";
    }
    // Completeness (structural) and validation (differential) are separate
    // properties: an entry is validated only if it is complete and every
    // substitution exercised agreed.
    *out << "{\"phase\":\"validation_summary\",\"function\":";
    sumt_json_string(*out, function.name);
    *out << ",\"complete\":" << (fold.status == "complete" ? "true" : "false")
         << ",\"trait_bounded\":" << (trait_bounded ? "true" : "false")
         << ",\"runs\":" << runs << ",\"agreed\":" << agreed
         << ",\"result_unobserved\":" << unobserved
         << ",\"oracle_splits\":" << oracle_splits
         << ",\"call_site_unavailable\":" << call_site_unavailable
         << ",\"oracle_crashes\":" << oracle_crashes
         << ",\"validated\":"
         << (fold.status == "complete" && runs > 0 && agreed == runs ? "true" : "false") << "}\n";
  }
}

void sumt_shadow_emit() {
  if (!sumt_shadow_enabled_) return;
  for (const auto& function : p_.functions)
    sumt_shadow_emit_function(function, "final");
}
