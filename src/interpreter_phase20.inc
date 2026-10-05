// Phase 20 Agent D -- Fast Debug (interpreter) support for `Executor` /
// `executor.invoke`, and the FileIO integration boundary.
//
// Executor: Fast Debug models Phase 20 roots as a deterministic sequential
// schedule (corrective item 17), preserving the semantic difference between
// `executor.invoke` and synchronous `message`:
//   start  -> create an active logical executor (configuration was already
//             checked; Fast Debug evaluates it for its effects only);
//   invoke -> evaluate/snapshot the handler arguments *now*, enqueue an
//             independent logical root, and return after submission;
//   join   -> drain the queued roots in submission order through the normal
//             synchronous handler machinery (run_handler_call), then consume
//             the executor.
// This is a valid schedule of Phase 20 roots, not a competing worker pool:
// it models no physical parallelism, fairness, or timing.
//
// FileIO: native execution uses Agent B's production runtime. Fast Debug
// intentionally does not implement a second POSIX FileIO backend in Phase 20;
// `FileIO.open(...)` (and therefore any chunk pipeline) reports that FileIO
// execution requires the native backend.

  std::map<std::string, std::vector<PreparedHandlerCall>> fast_debug_executors_;

  bool interp_phase20_statement(const Stmt& statement, Frame& frame,
                                std::ostream& output) {
    if (statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
        statement.kind == Stmt::Kind::Var) {
      std::vector<std::string> config_arguments;
      if (interp_executor_construction(statement.b, config_arguments)) {
        for (const auto& argument : config_arguments)
          eval(argument, frame, statement.line, output);  // source order, once
        frame.locals[statement.a] = Value::unit();
        frame.local_types[statement.a] = "executor";
        fast_debug_executors_[statement.a].clear();
        emit("executor_start", frame, statement.line, statement.a);
        return true;
      }
      std::vector<std::string> open_arguments;
      if (interp_parse_fileio_open(statement.b, open_arguments)) {
        throw RuntimeError(statement.line,
            "Fast Debug FileIO execution is intentionally unsupported in Phase 20; "
            "run this program with the native backend");
      }
      return false;
    }
    if (statement.kind != Stmt::Kind::Call) return false;
    auto receiver_type = frame.local_types.find(statement.a);
    if (receiver_type == frame.local_types.end() || receiver_type->second != "executor")
      return false;
    auto executor = fast_debug_executors_.find(statement.a);
    if (executor == fast_debug_executors_.end())
      throw RuntimeError(statement.line, "internal error: unchecked inactive Executor");
    if (statement.b == "invoke") {
      // Submission: snapshot arguments now, run later at join.
      executor->second.push_back(
          prepare_handler_call(statement.args.front(), frame, statement.line, output));
      emit("root_submit", frame, statement.line, statement.args.front());
      return true;
    }
    if (statement.b == "join") {
      auto roots = std::move(executor->second);
      fast_debug_executors_.erase(executor);
      for (auto& root : roots) run_handler_call(std::move(root), frame, output);
      emit("executor_join", frame, statement.line, statement.a);
      return true;
    }
    return false;
  }

  // Recognizes `Executor()(.config(arg))*.start()` and collects the
  // configuration arguments in source order.
  static bool interp_executor_construction(const std::string& text,
                                           std::vector<std::string>& config_arguments) {
    std::string value = trim_copy(text);
    std::vector<std::string> reversed;
    while (true) {
      auto member = top_level_member(value);
      if (!member) {
        std::string callee;
        std::vector<std::string> args;
        if (!(parse_call(value, callee, args) && callee == "Executor" && args.empty()))
          return false;
        config_arguments.assign(reversed.rbegin(), reversed.rend());
        return true;
      }
      std::string method;
      std::vector<std::string> args;
      if (parse_call(member->second, method, args) && method != "start")
        for (auto it = args.rbegin(); it != args.rend(); ++it) reversed.push_back(*it);
      value = member->first;
    }
  }

  static bool interp_parse_fileio_open(const std::string& text,
                                       std::vector<std::string>& args) {
    auto member = top_level_member(text);
    if (!member || member->first != "FileIO") return false;
    std::string method_name;
    return parse_call(member->second, method_name, args) && method_name == "open" &&
        args.size() == 2;
  }
