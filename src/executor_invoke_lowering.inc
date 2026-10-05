// Phase 20 Agent D -- compiler-side legality for `Executor` and
// `executor.invoke(concrete_domain.Handler(args...))`.
//
// Normative behavior: docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md (secs. 3-7),
// docs/ROOT_RUNTIME_ABI.md ("Executor/root runtime ABI").
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
// This file adds two things inside class Checker:
//  1. check_executor_or_fileio_statement: per-statement recognition wired
//     into check_stmts' existing TypeEnvVisitor, validating one statement in
//     isolation (shape, argument legality, target handler).
//  2. check_executor_lifecycle: a dedicated sequencing pass over one body
//     (today only `main`, since executor.invoke/join are main-only) that
//     tracks configure/start -> invoke* -> join as an explicit state machine,
//     mirroring every_handler_path_replies' indentation-based CFG walk.
//
// There is no real Agent-C root-admission/worker-pool runtime in this tree.
// executor.invoke is lowered (see executor_invoke_codegen.inc) to an inline,
// synchronous call of the target handler at the submission point. This
// preserves one-way semantics, argument snapshotting, and submission order,
// but not physical concurrency -- an explicit, documented integration gap.

void check_fileio_mode_argument(int line, const string& mode) const {
  string trimmed = trim(mode);
  if (trimmed != "ro" && trimmed != "rw" && trimmed != "create")
    err(line,
        "FileIO.open mode must be the bare word 'ro', 'rw', or 'create'; found '" +
            trimmed + "'",
        "FILEIO_INVALID_MODE");
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
  for (const auto& argument : handler_args) {
    string trimmed = trim(argument);
    if (executor_typed(env, trimmed))
      err(statement.line,
          "an Executor is a static routing-like capability and cannot be "
          "passed as an executor.invoke argument",
          "EXECUTOR_INVOKE_CAPABILITY_ARGUMENT");
    if (fileio_typed(env, trimmed))
      err(statement.line,
          "a FileIO capability cannot be passed as an executor.invoke "
          "argument",
          "EXECUTOR_INVOKE_CAPABILITY_ARGUMENT");
    check_expression(statement.line, argument, env);
  }
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
        check_expression(statement.line, call.second.front(), env);
      }
      return true;
    }

    vector<string> open_arguments;
    if (parse_fileio_open(statement.b, open_arguments)) {
      check_fileio_mode_argument(statement.line, open_arguments[1]);
      check_expression(statement.line, open_arguments[0], env);
      return true;
    }

    string receiver, method;
    vector<string> arguments;
    // A functional pipeline (e.g. `file.chunks(n) |> map(...) |> reduce(...)`)
    // is handled entirely by the existing functional-pipeline IR (see
    // file_chunk_lowering.inc); parse_member_call on the whole multi-stage
    // text would otherwise misparse past the first '|>'.
    if (!parse_functional_pipeline(statement.b) &&
        parse_member_call(statement.b, receiver, method, arguments) &&
        fileio_typed(env, receiver)) {
      if (method == "read" && arguments.size() == 2) {
        check_expression(statement.line, arguments[0], env);
        check_expression(statement.line, arguments[1], env);
        return true;
      }
      err(statement.line,
          "FileIO has no result-producing operation '" + method +
              "' for this form",
          "FILEIO_UNKNOWN_METHOD");
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
    if (fileio_typed(env, statement.a)) {
      if (statement.b == "write" && statement.args.size() == 2) {
        check_expression(statement.line, statement.args[0], env);
        check_expression(statement.line, statement.args[1], env);
        return true;
      }
      if (statement.b == "sync" && statement.args.size() <= 1) {
        if (statement.args.size() == 1 && trim(statement.args[0]) != "dataonly")
          err(statement.line,
              "FileIO.sync accepts only the bare argument 'dataonly'",
              "FILEIO_SYNC_ARGUMENT");
        return true;
      }
      if (statement.b == "close" && statement.args.empty()) return true;
      err(statement.line,
          "unknown FileIO operation '" + statement.b + "'",
          "FILEIO_UNKNOWN_METHOD");
    }
    return false;
  }
  return false;
}

// Sequencing pass: configure/start -> invoke* -> join, one active Executor at
// a time, no use after join, every normal path after start() reaches join().
// Mirrors every_handler_path_replies' flat indentation-based CFG walk.
std::map<string,int> check_executor_lifecycle_block(
    const vector<Stmt>& statements, size_t& index, int level,
    std::map<string,int> active, std::set<string> joined) const {
  while (index < statements.size()) {
    const Stmt& statement = statements[index];
    if (statement.indent < level) return active;
    if (statement.indent > level)
      err(statement.line, "indentation jumps more than one block level");
    if (statement.kind == Stmt::Kind::Else) return active;

    if (statement.kind == Stmt::Kind::If) {
      ++index;
      auto then_active = check_executor_lifecycle_block(
          statements, index, level + 1, active, joined);
      auto else_active = active;
      if (index < statements.size() && statements[index].indent == level &&
          statements[index].kind == Stmt::Kind::Else) {
        ++index;
        else_active = check_executor_lifecycle_block(
            statements, index, level + 1, active, joined);
      }
      if (then_active != else_active)
        err(statement.line,
            "an Executor's lifecycle state must agree on every branch "
            "reaching this point",
            "EXECUTOR_LIFECYCLE_BRANCH_MISMATCH");
      active = std::move(then_active);
      continue;
    }
    if (statement.kind == Stmt::Kind::While || statement.kind == Stmt::Kind::For) {
      ++index;
      auto body_active = check_executor_lifecycle_block(
          statements, index, level + 1, active, joined);
      if (body_active != active)
        err(statement.line,
            "an Executor cannot change lifecycle state across a loop "
            "iteration",
            "EXECUTOR_LIFECYCLE_LOOP_MISMATCH");
      continue;
    }
    if (statement.kind == Stmt::Kind::Match) {
      ++index;
      std::optional<std::map<string,int>> agreed;
      while (index < statements.size() &&
             statements[index].indent == level + 1 &&
             statements[index].kind == Stmt::Kind::Case) {
        ++index;
        auto case_active = check_executor_lifecycle_block(
            statements, index, level + 2, active, joined);
        if (!agreed) agreed = std::move(case_active);
        else if (*agreed != case_active)
          err(statement.line,
              "an Executor's lifecycle state must agree on every match case",
              "EXECUTOR_LIFECYCLE_BRANCH_MISMATCH");
      }
      if (agreed) active = std::move(*agreed);
      continue;
    }

    vector<std::pair<string, vector<string>>> calls;
    if ((statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
         statement.kind == Stmt::Kind::Var) &&
        parse_executor_chain(statement.b, calls) && !calls.empty() &&
        calls.back().first == "start") {
      if (!active.empty())
        err(statement.line,
            "at most one Executor may be active at a time; '" +
                active.begin()->first + "' (started at line " +
                std::to_string(active.begin()->second) + ") is still active",
            "EXECUTOR_MULTIPLE_ACTIVE");
      active[statement.a] = statement.line;
      joined.erase(statement.a);
    } else if (statement.kind == Stmt::Kind::Call &&
               (statement.b == "invoke" || statement.b == "join")) {
      if (!active.count(statement.a)) {
        if (joined.count(statement.a))
          err(statement.line,
              "Executor '" + statement.a +
                  "' was already joined and consumed",
              "EXECUTOR_USE_AFTER_JOIN");
        err(statement.line,
            "Executor '" + statement.a + "' is not active here",
            "EXECUTOR_NOT_ACTIVE");
      }
      if (statement.b == "join") {
        active.erase(statement.a);
        joined.insert(statement.a);
      }
    }
    ++index;
  }
  return active;
}

void check_executor_lifecycle(const vector<Stmt>& body) const {
  size_t index = 0;
  auto active = check_executor_lifecycle_block(body, index, 0, {}, {});
  if (!active.empty()) {
    auto first = active.begin();
    err(first->second,
        "every normal path after Executor.start() must reach "
        "Executor.join(); '" + first->first + "' is never joined",
        "EXECUTOR_JOIN_MISSING");
  }
}
