// Phase 20 Agent D -- compiler-side legality for `Executor` and
// `executor.invoke(concrete_domain.Handler(args...))`.
//
// Normative behavior: docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md (secs. 3-7),
// docs/ROOT_RUNTIME_ABI.md ("Executor/root runtime ABI", "RootDescriptor
// representation").
//
// `Executor` is a compiler-recognized pinned-capability construction, never a
// general nominal type: there is no "executor" entry in valid_type(), no
// Executor-typed field/parameter/return is ever legal, and nothing outside
// this file and the matching codegen in executor_invoke_codegen.inc
// recognizes the pseudo-type string "executor". That alone rejects every
// attempt to store an Executor in domain state, pass it as an ordinary
// argument, return it, or otherwise let it escape `main`: those positions all
// go through valid_type()/field-type checking, which never admits "executor".
//
// This file adds inside class Checker:
//  1. check_executor_or_fileio_statement: per-statement recognition wired
//     into check_stmts' existing TypeEnvVisitor, validating one statement in
//     isolation (shape, argument legality, target handler).
//  2. check_executor_invoke: full legality for one invoke statement
//     (target/handler/arity/one-way/argument type compatibility/capability
//     rejection), building the checked RootSubmissionPlan (ast.hpp) that
//     executor_invoke_codegen.inc consumes -- it never re-derives these
//     facts from source text.
//  3. check_executor_lifecycle: a dedicated sequencing pass over one body
//     (today only `main`, since executor.invoke/join are main-only) that
//     tracks every Executor binding's state (Unstarted/Active/Consumed) as
//     an explicit per-binding state machine, mirroring
//     every_handler_path_replies' indentation-based CFG walk, including
//     terminating statements (`return`) so an early exit before `join()` is
//     rejected exactly like falling off the end of the body.
//
// executor.invoke itself never lowers to a synchronous handler call (see
// executor_invoke_codegen.inc): there is no valid inline production
// fallback for "submit an independent Root and continue after submission".
// The checked ExecutorStartPlan/RootSubmissionPlan are lowered against
// Agent C's root-admission ABI; Agent D implements no scheduler. Invoked
// handlers participate in ordinary untyped-parameter inference
// (infer_statement_expressions / infer_domain_specializations treat an
// invoke site like a message site).

// Validates `Executor().<config>*.start()` argument types and statically
// obvious bounds, and records the ordered configuration in an
// ExecutorStartPlan so codegen evaluates each argument exactly once, in
// source order, before start() (corrective-pass items 10-11). Agent C's
// runtime still validates defensively at start().
void check_executor_start_plan(
    const Stmt& statement,
    const vector<std::pair<string, vector<string>>>& calls,
    const TypeEnv& env) {
  ExecutorStartPlan plan;
  plan.binding = statement.a;
  std::optional<long long> threads_literal, max_threads_literal;
  for (size_t index = 0; index + 1 < calls.size(); ++index) {
    const auto& call = calls[index];
    if (!executor_config_methods().count(call.first))
      err(statement.line,
          "unknown Executor configuration method '" + call.first + "'",
          "EXECUTOR_UNKNOWN_CONFIG_METHOD");
    if (call.second.size() != 1)
      err(statement.line,
          "Executor." + call.first + " requires exactly one argument",
          "EXECUTOR_CONFIG_ARITY");
    const string& argument = call.second.front();
    check_expression(statement.line, argument, env);
    auto actual = inferred_expr_type(argument, env);
    string expected = call.first == "affinity" ? "vector[int]" : "int";
    if (!actual || !same_type(*actual, expected))
      err(statement.line,
          "Executor." + call.first + " requires " +
              (call.first == "affinity" ? string("a Vector[Int] of core ids")
                                        : string("an Int")) +
              "; found '" + (actual ? canonical_type_name(*actual) : string("unresolved")) + "'",
          "EXECUTOR_CONFIG_TYPE");
    bool count = call.first == "threads" || call.first == "max_threads" ||
        call.first == "queue_capacity";
    string literal = trim(argument);
    bool is_literal = !literal.empty() &&
        std::all_of(literal.begin() + (literal[0] == '-' ? 1 : 0), literal.end(),
                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) &&
        literal != "-";
    if (count && is_literal) {
      long long value = std::stoll(literal);
      if (value <= 0)
        err(statement.line,
            "Executor." + call.first + " must be positive; found " + literal,
            "EXECUTOR_CONFIG_RANGE");
      if (call.first == "threads") threads_literal = value;
      if (call.first == "max_threads") max_threads_literal = value;
    }
    plan.calls.push_back({call.first, argument, canonical_type_name(expected)});
  }
  if (threads_literal && max_threads_literal && *max_threads_literal < *threads_literal)
    err(statement.line,
        "Executor.max_threads (" + std::to_string(*max_threads_literal) +
            ") cannot be less than threads (" + std::to_string(*threads_literal) + ")",
        "EXECUTOR_CONFIG_RANGE");
  p_.executor_start_plans.push_back(std::move(plan));
  statement.executor_start_plan_id = p_.executor_start_plans.size();
}

void check_executor_invoke(const Stmt& statement, const TypeEnv& env,
                           bool in_main) {
  if (!in_main)
    err(statement.line,
        "executor.invoke is legal only directly in main's body",
        "EXECUTOR_INVOKE_OUTSIDE_MAIN");
  if (statement.args.size() != 1)
    err(statement.line,
        "executor.invoke takes exactly one handler-call argument: "
        "'executor.invoke(domain.Handler(args...))'",
        "EXECUTOR_INVOKE_ARITY");
  string domain_binding, handler_name;
  vector<string> handler_args;
  if (!parse_member_call(statement.args.front(), domain_binding, handler_name,
                         handler_args) ||
      !plain_identifier(domain_binding) || !plain_identifier(handler_name))
    err(statement.line,
        "executor.invoke requires a statically known "
        "'domain.Handler(args...)' call expression",
        "EXECUTOR_INVOKE_SHAPE");
  auto receiver = env.find(domain_binding);
  if (receiver == env.end() ||
      !domains_.count(canonical_type_name(receiver->second)))
    err(statement.line,
        "executor.invoke target '" + domain_binding +
            "' is not a concrete domain instance",
        "EXECUTOR_INVOKE_TARGET_NOT_DOMAIN");
  const Domain& domain = *domains_.at(canonical_type_name(receiver->second));
  const Handler* handler = find_handler(domain, handler_name);
  if (!handler)
    err(statement.line,
        "unknown handler '" + domain.name + "." + handler_name + "'",
        "EXECUTOR_INVOKE_UNKNOWN_HANDLER");
  if (handler->reply_type)
    err(statement.line,
        "executor.invoke requires a one-way handler; '" + domain.name + "." +
            handler_name + "' returns '" + *handler->reply_type + "' via reply",
        "EXECUTOR_INVOKE_VALUE_RETURNING");
  if (handler_args.size() != handler->params.size())
    err(statement.line,
        "handler '" + domain.name + "." + handler_name + "' expects " +
            std::to_string(handler->params.size()) + " argument(s), found " +
            std::to_string(handler_args.size()),
        "EXECUTOR_INVOKE_ARGUMENT_COUNT");

  RootSubmissionPlan plan;
  plan.executor_binding = statement.a;
  plan.target_domain_binding = domain_binding;
  plan.target_domain_semantic_identity = domain_semantic_identity(domain);
  plan.handler_semantic_identity = handler_semantic_identity(domain, *handler);
  plan.handler_name = handler_name;
  plan.one_way = true;

  for (size_t index = 0; index < handler_args.size(); ++index) {
    const string& argument = handler_args[index];
    check_expression(statement.line, argument, env);
    // Infer the argument's semantic type first, then ask Agent A's checked
    // boundary fact (phase20_capability_type) about that *type* -- this also
    // catches capability-producing expressions and nested scoped types, not
    // just a bare capability binding (corrective-pass item 13).
    auto actual = inferred_expr_type(argument, env);
    if (!actual)
      err(statement.line,
          "cannot infer the type of executor.invoke argument '" + argument + "'",
          "EXECUTOR_INVOKE_ARGUMENT_TYPE_UNRESOLVED");
    if (actual && phase20_capability_type(*actual))
      err(statement.line,
          "a Phase 20 capability (Executor/FileIO/Range/chunk source) cannot "
          "be passed as an executor.invoke argument",
          "EXECUTOR_INVOKE_CAPABILITY_ARGUMENT");
    // Same formal/actual payload compatibility as an ordinary handler call
    // (corrective-pass item 8). An untyped handler parameter receives its
    // type from this call site through the ordinary inference pass
    // (infer_statement_expressions / infer_domain_specializations treat
    // executor.invoke exactly like `message`); Phase 20 adds no
    // explicit-type requirement for invoked handlers.
    const Param& formal = handler->params[index];
    string formal_type = formal.type.empty() ? *actual : formal.type;
    if (!starts_with(*actual, "_") && !starts_with(formal_type, "_") &&
        !same_type(formal_type, *actual))
      err(statement.line,
          "argument " + std::to_string(index + 1) + " to executor.invoke(" +
              domain.name + "." + handler_name + "(...)) has type '" +
              canonical_type_name(*actual) + "', expected '" +
              canonical_type_name(formal_type) + "'",
          "EXECUTOR_INVOKE_ARGUMENT_TYPE_MISMATCH");
    plan.argument_expressions.push_back(argument);
    plan.argument_types.push_back(canonical_type_name(formal_type));
  }

  p_.root_submission_plans.push_back(std::move(plan));
  statement.root_submission_plan_id = p_.root_submission_plans.size();
}

// Validates one statement that constructs or uses an Executor/FileIO value,
// returning true when it fully owns (and has fully checked) that statement so
// the generic Assign/Call handling in check_stmts must not also run.
bool check_executor_or_fileio_statement(const Stmt& statement,
                                        const TypeEnv& env,
                                        const Domain* current,
                                        const Function* current_function,
                                        const vector<Stmt>* statements) {
  bool in_main = current == nullptr && current_function == nullptr &&
      current_object_ == nullptr && p_.main && statements == &p_.main->body;

  if (statement.kind == Stmt::Kind::Let || statement.kind == Stmt::Kind::Var ||
      statement.kind == Stmt::Kind::Assign) {
    vector<std::pair<string, vector<string>>> calls;
    if (parse_executor_chain(statement.b, calls)) {
      if (!in_main)
        err(statement.line,
            "an Executor may only be configured and started directly in "
            "main's body",
            "EXECUTOR_CONSTRUCT_OUTSIDE_MAIN");
      if (calls.empty() || calls.back().first != "start")
        err(statement.line,
            "Executor configuration must end with '.start()'",
            "EXECUTOR_MISSING_START");
      if (!calls.back().second.empty())
        err(statement.line, "Executor.start() takes no arguments",
            "EXECUTOR_START_ARITY");
      check_executor_start_plan(statement, calls, env);
      return true;
    }

    // FileIO legality (ownership, mode, bounds, lifecycle) belongs to Agent
    // A's semantics (fileio_semantics.inc), reached through the ordinary
    // Assign/Call walk. Agent D adds only the chunk-source shape rule: the
    // scoped seq[Range] source is not a storable collection (spec sec. 13).
    string receiver, method;
    vector<string> arguments;
    // A functional pipeline (e.g. `file.chunks(n) |> map(...) |> reduce(...)`)
    // is handled entirely by the existing functional-pipeline IR (see
    // file_chunk_lowering.inc); parse_member_call on the whole multi-stage
    // text would otherwise misparse past the first '|>'.
    if (!parse_functional_pipeline(statement.b) &&
        parse_member_call(statement.b, receiver, method, arguments) &&
        fileio_typed(env, receiver) && method == "chunks") {
      // Report A's operand/bound diagnostics before the shape rule.
      check_expression(statement.line, statement.b, env);
      err(statement.line,
          "file.chunks(size) is a scoped seq[Range] source, not a "
          "materializable collection; consume it immediately in a "
          "'chunks(size) |> map(f) |> reduce(initial, combine)' pipeline",
          "FILEIO_CHUNKS_NOT_MATERIALIZABLE");
      return true;
    }
    return false;
  }

  if (statement.kind == Stmt::Kind::Call) {
    if (executor_typed(env, statement.a)) {
      if (statement.b == "invoke") {
        check_executor_invoke(statement, env, in_main);
        return true;
      }
      if (statement.b == "join") {
        if (!statement.args.empty())
          err(statement.line, "Executor.join takes no arguments",
              "EXECUTOR_JOIN_ARITY");
        return true;
      }
      err(statement.line,
          "unknown Executor operation '" + statement.b + "'",
          "EXECUTOR_UNKNOWN_METHOD");
    }
    return false;
  }
  return false;
}

// Phase 20 Agent D: one Executor binding's lifecycle state. Unifying
// "active" and "joined" into one per-binding state (rather than an active
// map plus a separately-discarded joined set) is required to answer "has
// this Executor already been consumed" after it was joined inside a branch
// (AGENTS.md corrective-pass item 13): a stale `joined` set that merge logic
// drops would make a later `invoke`/`join` report merely "not active"
// instead of the more precise "already joined and consumed".
enum class ExecutorLifecycleState { Unstarted, Active, Consumed };

struct ExecutorLifecycle {
  ExecutorLifecycleState state = ExecutorLifecycleState::Unstarted;
  int started_line = 0;
  bool operator==(const ExecutorLifecycle& other) const {
    return state == other.state;
  }
  bool operator!=(const ExecutorLifecycle& other) const {
    return !(*this == other);
  }
};

// Sequencing pass: configure/start -> invoke* -> join, at most one Active
// Executor at a time, no use after join (Consumed), every normal path after
// start() reaching join() before the body ends *or* terminates (`return`).
// Mirrors every_handler_path_replies' flat indentation-based CFG walk.
//
// `continuing` distinguishes a path that terminates early (via `return`)
// from one that falls through to the next statement: a terminated path must
// not be merged into the state that flows into later statements, matching
// AGENTS.md corrective-pass item 12's early-exit requirement.
std::map<string, ExecutorLifecycle> check_executor_lifecycle_block(
    const vector<Stmt>& statements, size_t& index, int level,
    std::map<string, ExecutorLifecycle> state, bool& continuing) const {
  continuing = true;
  while (continuing && index < statements.size()) {
    const Stmt& statement = statements[index];
    if (statement.indent < level) return state;
    if (statement.indent > level)
      err(statement.line, "indentation jumps more than one block level");
    if (statement.kind == Stmt::Kind::Else) return state;

    if (statement.kind == Stmt::Kind::If) {
      ++index;
      bool then_continuing = true;
      auto then_state = check_executor_lifecycle_block(
          statements, index, level + 1, state, then_continuing);
      auto else_state = state;
      bool else_continuing = true;
      if (index < statements.size() && statements[index].indent == level &&
          statements[index].kind == Stmt::Kind::Else) {
        ++index;
        else_state = check_executor_lifecycle_block(
            statements, index, level + 1, state, else_continuing);
      }
      state = merge_executor_lifecycle_paths(
          statement.line, {{then_state, then_continuing}, {else_state, else_continuing}});
      continuing = then_continuing || else_continuing;
      continue;
    }
    if (statement.kind == Stmt::Kind::While || statement.kind == Stmt::Kind::For) {
      ++index;
      bool body_continuing = true;
      auto body_state = check_executor_lifecycle_block(
          statements, index, level + 1, state, body_continuing);
      // The loop may execute zero or more times: its body's lifecycle
      // effect on bindings that survive the loop must be a no-op, and a
      // body that always terminates (e.g. always `return`s) can never
      // legally be entered more than the first iteration's worth.
      if (body_continuing && body_state != state)
        err(statement.line,
            "an Executor cannot change lifecycle state across a loop "
            "iteration; start and join a complete lifecycle inside the "
            "loop body, or do neither",
            "EXECUTOR_LIFECYCLE_LOOP_MISMATCH");
      continue;
    }
    if (statement.kind == Stmt::Kind::Match) {
      ++index;
      std::optional<std::map<string, ExecutorLifecycle>> agreed;
      bool any_continuing = false;
      while (index < statements.size() &&
             statements[index].indent == level + 1 &&
             statements[index].kind == Stmt::Kind::Case) {
        ++index;
        bool case_continuing = true;
        auto case_state = check_executor_lifecycle_block(
            statements, index, level + 2, state, case_continuing);
        any_continuing = any_continuing || case_continuing;
        if (!case_continuing) continue;
        if (!agreed) agreed = std::move(case_state);
        else if (*agreed != case_state)
          err(statement.line,
              "an Executor's lifecycle state must agree on every match "
              "case that continues past the match",
              "EXECUTOR_LIFECYCLE_BRANCH_MISMATCH");
      }
      if (agreed) state = std::move(*agreed);
      continuing = any_continuing;
      continue;
    }
    if (statement.kind == Stmt::Kind::Return) {
      // Every normal path leaving main must reach join() -- an early
      // `return` is itself a normal exit, not an escape from the
      // requirement (AGENTS.md corrective-pass item 12).
      for (const auto& entry : state)
        if (entry.second.state == ExecutorLifecycleState::Active)
          err(statement.line,
              "'return' leaves Executor '" + entry.first +
                  "' (started at line " +
                  std::to_string(entry.second.started_line) +
                  ") active and unjoined; call Executor.join() on every "
                  "normal exit path, including 'return'",
              "EXECUTOR_JOIN_MISSING");
      ++index;
      continuing = false;
      continue;
    }

    vector<std::pair<string, vector<string>>> calls;
    if ((statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
         statement.kind == Stmt::Kind::Var) &&
        parse_executor_chain(statement.b, calls) && !calls.empty() &&
        calls.back().first == "start") {
      for (const auto& entry : state)
        if (entry.second.state == ExecutorLifecycleState::Active)
          err(statement.line,
              "at most one Executor may be active at a time; '" +
                  entry.first + "' (started at line " +
                  std::to_string(entry.second.started_line) +
                  ") is still active",
              "EXECUTOR_MULTIPLE_ACTIVE");
      state[statement.a] = {ExecutorLifecycleState::Active, statement.line};
    } else if (statement.kind == Stmt::Kind::Call &&
               (statement.b == "invoke" || statement.b == "join")) {
      auto found = state.find(statement.a);
      if (found == state.end() || found->second.state != ExecutorLifecycleState::Active) {
        if (found != state.end() &&
            found->second.state == ExecutorLifecycleState::Consumed)
          err(statement.line,
              "Executor '" + statement.a +
                  "' was already joined and consumed",
              "EXECUTOR_USE_AFTER_JOIN");
        err(statement.line,
            "Executor '" + statement.a + "' is not active here",
            "EXECUTOR_NOT_ACTIVE");
      }
      if (statement.b == "join")
        found->second.state = ExecutorLifecycleState::Consumed;
    }
    ++index;
  }
  return state;
}

// Merges lifecycle state across several alternative paths (if/else arms,
// match cases), ignoring any path that terminated (`continuing == false`):
// a terminated path contributes no state to what flows into later
// statements (AGENTS.md corrective-pass item 12). If every path terminates,
// the merged state is irrelevant to the caller, which also stops
// continuing.
std::map<string, ExecutorLifecycle> merge_executor_lifecycle_paths(
    int line,
    const vector<std::pair<std::map<string, ExecutorLifecycle>, bool>>& paths) const {
  std::optional<std::map<string, ExecutorLifecycle>> agreed;
  for (const auto& path : paths) {
    if (!path.second) continue;  // terminated path: contributes nothing.
    if (!agreed) agreed = path.first;
    else if (*agreed != path.first)
      err(line,
          "an Executor's lifecycle state must agree on every branch that "
          "continues past this point",
          "EXECUTOR_LIFECYCLE_BRANCH_MISMATCH");
  }
  return agreed.value_or(std::map<string, ExecutorLifecycle>{});
}

void check_executor_lifecycle(const vector<Stmt>& body) const {
  size_t index = 0;
  bool continuing = true;
  auto state = check_executor_lifecycle_block(body, index, 0, {}, continuing);
  if (!continuing) return;  // every normal path terminated before falling through.
  for (const auto& entry : state)
    if (entry.second.state == ExecutorLifecycleState::Active)
      err(entry.second.started_line,
          "every normal path after Executor.start() must reach "
          "Executor.join(); '" + entry.first + "' is never joined",
          "EXECUTOR_JOIN_MISSING");
}
